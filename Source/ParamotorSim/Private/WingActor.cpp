// Copyright Epic Games, Inc. All Rights Reserved.

#include "WingActor.h"
#include "ParamotorLogging.h"
#include "ParamotorActor.h"
#include "ParamotorGameInstance.h"
#include "AtmosphereModel.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "ProceduralMeshComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UObject/ConstructorHelpers.h"

// ============================================================
// WingActor.cpp — WingActor implementation split
// ============================================================

AWingActor::AWingActor()

{
	PrimaryActorTick.bCanEverTick = true;
	// Tick after the character so our rotation isn't overridden by character Tick
	PrimaryActorTick.TickGroup = TG_PostPhysics;
Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
SetRootComponent(Root);
Root->SetMobility(EComponentMobility::Movable);
CanopyMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("CanopyMesh"));
CanopyMesh->SetupAttachment(Root);
CanopyMesh->SetMobility(EComponentMobility::Movable);
CanopyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
CanopyMesh->SetSimulatePhysics(false);
CanopyMesh->SetCastShadow(false);
BagMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BagMesh"));
BagMesh->SetupAttachment(Root);
BagMesh->SetMobility(EComponentMobility::Movable);
BagMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
BagMesh->SetSimulatePhysics(false);
BagMesh->SetCastShadow(false);
BagMesh->SetVisibility(false);
LineMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("LineMesh"));
LineMesh->SetupAttachment(Root);
LineMesh->SetMobility(EComponentMobility::Movable);
LineMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
LineMesh->SetSimulatePhysics(false);
LineMesh->SetCastShadow(false);
LineMesh->SetVisibility(false);

}

// ============================================================
// BEGIN PLAY
// ============================================================

void AWingActor::LoadMaterials()
{
	// Load canopy material — use engine VertexColorMaterial (shows vertex colors as emissive)
	// This is in EngineDebugMaterials, not EngineMaterials
	CanopyMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineDebugMaterials/VertexColorMaterial.VertexColorMaterial"));
	if (!CanopyMaterial)
	{
		// Fallback to M_Canopy (project material with VertexColor node)
		CanopyMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_Canopy.M_Canopy"));
	}
	if (!CanopyMaterial)
	{
		CanopyMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/EmissiveMeshMaterial.EmissiveMeshMaterial"));
		UE_LOG(LogParamotor, Log, TEXT("WingActor: All canopy materials not found, using EmissiveMeshMaterial fallback"));
	}
	else
	{
		UE_LOG(LogParamotor, Log, TEXT("WingActor: Loaded canopy material"));
	}
	// Cache line mesh material — use VertexColorMaterial so per-vertex line colors show
	LineMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineDebugMaterials/VertexColorMaterial.VertexColorMaterial"));
	if (!LineMaterial)
		LineMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/EmissiveMeshMaterial.EmissiveMeshMaterial"));
}

void AWingActor::BeginPlay(){
	Super::BeginPlay();
	// Cache game instance (avoids per-frame GetWorld()->GetGameInstance calls)
	if (GetWorld())
		CachedGameInstance = GetWorld()->GetGameInstance<UParamotorGameInstance>();

	LoadMaterials();

	// The legacy test-wing init is gone — AChargerWingActor is the only wing
	// and builds its own sail in its own BeginPlay.
	WingState = EWingState::Spread;
	SetSpreadState();

	if (CanopyMesh) CanopyMesh->SetVisibility(true);
	if (LineMesh) LineMesh->SetVisibility(true);

	UE_LOG(LogParamotor, Log, TEXT("WingActor: base initialized (Charger overrides with fresh build)"));
}

void AWingActor::LiftALines()
{
	if (!bRisersConnected) return;
	if (WingState != EWingState::Connected && WingState != EWingState::Inflating) return;
	// A-line pull: physically opens the leading edge intakes.
	// This does NOT force inflation — the wing still needs apparent wind to fill.
	// In real life, pulling A-risers just opens the cell mouths so air can enter.
	// If there's no wind, the wing won't inflate no matter how hard you pull.
	bLiftingALines = true;
	// State transition to Inflating happens naturally in the physics update
	// when cells start filling from ram pressure through the open intakes.
}

void AWingActor::ReleaseALines()
{
	// Releasing A-risers closes the intakes back to normal position.
	// Cells that are already pressurized stay pressurized (air can't easily escape).
	// But new air can't enter as easily, so a deflating wing won't re-inflate.
	bLiftingALines = false;
}

void AWingActor::SetLeftBrake(float Amount) {
	LeftBrakeAmount = FMath::Clamp(Amount, 0.0f, 1.0f);
	LeftBrakeTarget = LeftBrakeAmount;
}

void AWingActor::SetRightBrake(float Amount) {
	RightBrakeAmount = FMath::Clamp(Amount, 0.0f, 1.0f);
	RightBrakeTarget = RightBrakeAmount;
}

void AWingActor::EnterFlightMode(ACharacter* Char)
{
	// BYPASS CMC ENTIRELY — use MOVE_None so CMC does zero physics processing
	// WingActor has 100% control over velocity, position, and rotation
	Char->GetCharacterMovement()->SetMovementMode(MOVE_None);
	Char->GetCharacterMovement()->GravityScale = 0.0f;
	Char->GetCharacterMovement()->AirControl = 0.0f;
	Char->GetCharacterMovement()->MaxAcceleration = 0.0f;
	// Disable all rotation flags — WingActor controls rotation
	Char->bUseControllerRotationYaw = false;
	Char->GetCharacterMovement()->bOrientRotationToMovement = false;
	Char->GetCharacterMovement()->bUseControllerDesiredRotation = false;
	// Set mesh to base relative rotation (MetaHuman -90° yaw)
	FQuat FlightBaseQuat(FVector::UpVector, FMath::DegreesToRadians(-90.0f));
	Char->GetMesh()->SetRelativeRotation(FlightBaseQuat);
}

void AWingActor::ForceTakeoff()
{
	if (WingState != EWingState::Inflated) return;

	APawn* Pawn = nullptr;
	if (ConnectedParamotor)
		Pawn = Cast<APawn>(ConnectedParamotor->GetAttachParentActor());
	if (!Pawn) return;

	ACharacter* Char = Cast<ACharacter>(Pawn);
	if (!Char) return;

	WingState = EWingState::Flying;
	bIsAirborne = true;

	EnterFlightMode(Char);

	// DEBUG SHORTCUT — editor/dev only. Real takeoff goes through ground handling.
#if WITH_EDITOR || UE_BUILD_DEVELOPMENT
	FVector PawnLoc = Pawn->GetActorLocation();
	float GroundZ = GetCachedGroundZ(PawnLoc);
	PawnLoc.Z = GroundZ + 500.0f;  // 5m — just enough to clear ground collision
	Pawn->SetActorLocation(PawnLoc, false);
	Char->GetCharacterMovement()->Velocity = FVector(0.0f, 0.0f, 100.0f);  // 1 m/s up
	PendulumAngle = 0.0f;
	PendulumAngularVel = 0.0f;  // Start at rest
#endif
	PilotRollAngle = 0.0f;
	PilotRollRate = 0.0f;
	FlightTime = 0.0f;
	PilotPitch = 0.0f;
	PilotPitchTarget = -75.0f;
	UE_LOG(LogParamotor, Log, TEXT("WingActor: FORCED TAKEOFF!"));
}

void AWingActor::SetPilotWeight(float TotalWeightKg){
	PilotWeight = FMath::Max(40.0f, TotalWeightKg);  // Real min pilot ~40kg
UE_LOG(LogParamotor, Log, TEXT("WingActor: Pilot weight set to %.1f kg (max=%.1f, loading=%.1f kg/m²)"),		PilotWeight, WingModel.MaxTakeoffWeight, GetWingLoading());
}

float AWingActor::GetWingLoading() const{
	return (PilotWeight + WingModel.WingWeight) / WingModel.WingArea;
// kg/m²
}

// === Functions moved from deleted procedural wing files ===

void AWingActor::SetSpreadState(){
	if (CanopyMesh) CanopyMesh->SetVisibility(true);
	if (LineMesh) LineMesh->SetVisibility(true);
	if (BagMesh) BagMesh->SetVisibility(false);
}

float AWingActor::GetTrimmerAoAOffset() const
{
	float TrimmerTravelCm = 5.0f;
	float WingspanM = FMath::Sqrt(WingModel.WingArea * WingModel.AR);
	float MeanChordCm = (WingspanM > 0.0f) ? (WingModel.WingArea / WingspanM) * CM_PER_M : 200.0f;
	float ADistanceCm = MeanChordCm * 0.6f;
	return -TrimmerPosition * FMath::Atan2(TrimmerTravelCm, ADistanceCm) * RAD_TO_DEG;
}

float AWingActor::GetTrimmerCollapseRisk() const
{
	float TrimAoA = WingModel.TrimAoA;
	float StallAoA = WingModel.StallAoA;
	float CurrentAoA = TrimAoA + GetTrimmerAoAOffset();
	float MarginToStall = StallAoA - CurrentAoA;
	float TrimMargin = StallAoA - TrimAoA;
	if (TrimMargin < 1.0f) return 1.0f;
	float RiskRatio = TrimMargin / FMath::Max(1.0f, MarginToStall);
	return RiskRatio * RiskRatio;
}

FString AWingActor::GetTrimmerLabel() const
{
	if (TrimmerPosition > 0.7f) return TEXT("FAST");
	if (TrimmerPosition > 0.3f) return TEXT("FAST+");
	if (TrimmerPosition > 0.05f) return TEXT("FAST-");
	if (TrimmerPosition < -0.7f) return TEXT("SLOW");
	if (TrimmerPosition < -0.3f) return TEXT("SLOW+");
	if (TrimmerPosition < -0.05f) return TEXT("SLOW-");
	return TEXT("NEUTRAL");
}

void AWingActor::ToggleSeat()
{
	bSeated = !bSeated;
}

void AWingActor::AdjustHangPoint(float Delta)
{
	HangPointOffset = FMath::Clamp(HangPointOffset + Delta, -1.0f, 1.0f);
}

float AWingActor::GetTakeoffChance() const
{
	float WeightRatio = WingModel.MaxTakeoffWeight > 0.0f ? PilotWeight / WingModel.MaxTakeoffWeight : 1.0f;
	if (WeightRatio >= 1.0f) return 0.0f;
	float WeightFactor = FMath::Clamp(1.0f - WeightRatio * 0.8f, 0.0f, 1.0f);
	float CurrentAirspeed = 0.0f;
	if (ConnectedParamotor)
	{
		APawn* Pawn = Cast<APawn>(ConnectedParamotor->GetAttachParentActor());
		if (Pawn)
		{
			FVector Vel = Pawn->GetVelocity();
			FVector Wind = GetWindVector();
			FVector Apparent = Wind - Vel;
			FVector Fwd = Pawn->GetActorForwardVector();
			Fwd.Z = 0;
			Fwd.Normalize();
			float Headwind = -FVector::DotProduct(Apparent, Fwd) / CM_PER_M;
			CurrentAirspeed = FMath::Max(0.0f, Headwind);
		}
	}
	float StallDenom = AIR_DENSITY_SEA_LEVEL * WingModel.WingArea * WingModel.CLMax;
	float TakeoffSpeed = StallDenom > 0.0f ? FMath::Sqrt(2.0f * (PilotWeight + WingModel.WingWeight) * GRAVITY_MS2 / StallDenom) : 5.4f;
	float SpeedFactor = FMath::Clamp(CurrentAirspeed / TakeoffSpeed, 0.0f, 1.5f);
	float ThrottleFactor = (ThrottleAmount > 0.25f) ? 1.0f : ThrottleAmount / 0.25f;
	float Chance = WeightFactor * SpeedFactor * ThrottleFactor * PERCENT_FACTOR;
	return FMath::Clamp(Chance, 0.0f, 100.0f);
}
// ============================================================
// TICK
// ============================================================

void AWingActor::Tick(float DeltaTime){
	Super::Tick(DeltaTime);
	// Invalidate ground Z cache at start of each tick
	InvalidateGroundZCache();
}

