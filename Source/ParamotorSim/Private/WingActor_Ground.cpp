// Copyright Epic Games, Inc. All Rights Reserved.

#include "WingActor.h"
#include "ParamotorLogging.h"
#include "ParamotorActor.h"
#include "ProceduralMeshComponent.h"
#include "Materials/Material.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

// ==== GROUND HANDLING / RIGGING ====

bool AWingActor::UnpackFromBag(){
	if (WingState != EWingState::InBag) return false;
WingState = EWingState::Spread;
SetSpreadState();
return true;
}

bool AWingActor::GrabNearestEdge(FVector PlayerHandPos){
	if (WingState != EWingState::Spread && WingState != EWingState::Crinkled) return false;
	// Base class has no cloth — the Charger overrides this with its own
	// ground-cloth grab. Fallback just tracks the hand position.
	GrabHandPos = PlayerHandPos;
	return false;
}

void AWingActor::ReleaseEdge(){
	GrabbedParticleIndex = -1;
}

void AWingActor::UpdateGrabPosition(FVector NewHandPos){
	GrabHandPos = NewHandPos;
}

void AWingActor::ConnectRisers(AParamotorActor* Paramotor, FVector LeftCarabiner, FVector RightCarabiner){
	ConnectedParamotor = Paramotor;
LeftCarabinerPos = LeftCarabiner;
RightCarabinerPos = RightCarabiner;
bRisersConnected = true;
WingState = EWingState::Connected;
// Capture wind direction from the pilot's facing direction.
// Wind blows FROM the direction the pilot faces (headwind for takeoff).
// This is realistic — you always lay out your wing and face into the wind.

if (Paramotor)	{
		APawn* Pawn = Cast<APawn>(Paramotor->GetAttachParentActor());
if (Pawn)		{
			FVector PilotFwd = Pawn->GetActorForwardVector();
PilotFwd.Z = 0.0f;
PilotFwd.Normalize();
// Wind direction = opposite of pilot forward = blows toward pilot's face
			WindDirection = -PilotFwd;
}
}
	// === INITIALIZE WING YAW TO FACE INTO WIND ===
	// Wing leading edge (-X local) should point upwind (into wind).
	// WindDirection = direction wind blows TOWARD.
	// Upwind = -WindDirection.
	// Wing +X points downwind = WindDirection.
	// Yaw for +X pointing in WindDirection = atan2(WindDir.Y, WindDir.X)
	float InitialWingYaw = FMath::Atan2(WindDirection.Y, WindDirection.X) * RAD_TO_DEG;
	UE_LOG(LogParamotor, Log, TEXT("WingActor: Risers connected — L=%s R=%s WindDir=%s WingYaw=%.1f°"),		*LeftCarabiner.ToString(), *RightCarabiner.ToString(), *WindDirection.ToString(), InitialWingYaw);
}

void AWingActor::DisconnectRisers(){
	// Reset character gravity and movement mode BEFORE nulling ConnectedParamotor
if (ConnectedParamotor)	{
		APawn* Pawn = Cast<APawn>(ConnectedParamotor->GetAttachParentActor());
if (Pawn)		{
			ACharacter* Char = Cast<ACharacter>(Pawn);
if (Char && Char->GetCharacterMovement())			{
				Char->GetCharacterMovement()->GravityScale = 1.0f;
			// Flight uses MOVE_None (not MOVE_Flying), so IsFlying() is always false.
			// Restore appropriate movement mode based on ground contact.
			if (Char->GetCharacterMovement()->MovementMode == MOVE_None)
			{
				if (Char->GetCharacterMovement()->IsMovingOnGround())
					Char->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
				else
					Char->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
			}
}
}
}
	bRisersConnected = false;
ConnectedParamotor = nullptr;
if (WingState == EWingState::Connected || WingState == EWingState::Inflating ||
	WingState == EWingState::Inflated || WingState == EWingState::Flying)
{
	WingState = EWingState::Spread;
	// Reset flight state to avoid stale values on next connect
	bIsAirborne = false;
	InflationProgress = 0.0f;
}
}

void AWingActor::UpdateCarabinerPositions(FVector LeftCarabiner, FVector RightCarabiner){
	LeftCarabinerPos = LeftCarabiner;
RightCarabinerPos = RightCarabiner;
}

FVector AWingActor::GetLeftBrakeHandleWorldPos() const
{
	// Brake handles hang below the carabiners at roughly riser length
	return LeftCarabinerPos - FVector(0, 0, RISER_LENGTH_CM);
}

FVector AWingActor::GetRightBrakeHandleWorldPos() const
{
	return RightCarabinerPos - FVector(0, 0, RISER_LENGTH_CM);
}

float AWingActor::GetGroundZ(FVector Pos) const{
	if (!GetWorld()) return 0.0f;
FHitResult Hit;
FCollisionQueryParams QueryParams;
QueryParams.AddIgnoredActor(this);
FVector Start = Pos + FVector(0, 0, 200.0f);
FVector End = Pos - FVector(0, 0, 1000.0f);
if (GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, QueryParams))		return Hit.Location.Z;
return 0.0f;
}

float AWingActor::GetCachedGroundZ(FVector Pos)
{
	// If close to cached wing position, reuse the cached value
	if (bGroundZCachedThisTick)
	{
		FVector WingPos = GetActorLocation();
		float DistSq = FVector::DistSquared2D(Pos, WingPos);
		if (DistSq < 10000.0f)  // Within 100cm of wing position
			return CachedWingGroundZ;
	}
	// Otherwise do a fresh trace and cache it
	float Z = GetGroundZ(Pos);
	CachedWingGroundZ = Z;
	bGroundZCachedThisTick = true;
	return Z;
}
