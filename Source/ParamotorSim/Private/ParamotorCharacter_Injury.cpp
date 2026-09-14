// Copyright Epic Games, Inc. All Rights Reserved.

#include "ParamotorCharacter.h"
#include "ParamotorLogging.h"
#include "WingActor.h"
#include "ParamotorAnimInstance.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/AudioComponent.h"

// ==== INJURY / RAGDOLL ====

void AParamotorCharacter::Trip()
{
	if (bIsFallen) return;

	bIsFallen = true;
	bIsRagdolling = true;
	bIsGettingUp = false;
	FallTimer = 0.0f;
	TripRiskAccumulator = 0.0f;

	// Capture velocity BEFORE stopping movement (StopMovementImmediately zeros it)
	FVector LaunchVel = GetVelocity();

	// Stop character movement
	if (UCharacterMovementComponent* TripCMC = GetCharacterMovement())
	{
		TripCMC->StopMovementImmediately();
		TripCMC->DisableMovement();
	}

	// === Enable ragdoll on the mesh ===
	// Re-enable physics and collision for the skeletal mesh
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	GetMesh()->SetCollisionResponseToAllChannels(ECR_Block);
	GetMesh()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	GetMesh()->SetSimulatePhysics(true);
	GetMesh()->WakeRigidBody();

	// Give the ragdoll the character's actual velocity at trip time
	// Real physics: the body inherits the momentum it had when it fell
	if (LaunchVel.Size2D() < 50.0f)
	{
		// Standing still: small forward tip from the thrust moment that caused the trip
		LaunchVel = GetActorForwardVector() * 50.0f;
	}
	// Add slight upward pop from the falling motion (CG passes outside base of support)
	LaunchVel.Z = FMath::Max(LaunchVel.Z, 50.0f);  // Small upward from tipping over

	// Apply impulse to the ragdoll bodies (bVelChange=true means cm/s velocity change)
	GetMesh()->AddImpulse(LaunchVel, NAME_None, true);

	UE_LOG(LogParamotor, Log, TEXT("ParamotorCharacter: RAGDOLL TRIP! (thrust was %.2f, launch vel=%s)"), CurrentThrust, *LaunchVel.ToString());

	// Play fall sound
	if (FallAudio)
	{
		FallAudio->SetVolumeMultiplier(0.8f);
		FallAudio->Play();
	}

	// === Trip can cause ankle injury if fast/hard ===
	// Running trips at speed can twist an ankle
	// Use LaunchVel (captured before StopMovementImmediately) not GetVelocity()
	float TripSpeed = LaunchVel.Size2D() / CM_PER_M;  // m/s
	ApplyImpactInjury(TripSpeed, true);
}
bool AParamotorCharacter::UpdateFallState(float DeltaTime, UCharacterMovementComponent* CMC, float EffectiveMaxSpeed)
{
	if (!bIsFallen) return false;

	if (bIsRagdolling)
	{
		FallTimer += DeltaTime;
		FVector RagVel = GetMesh() ? GetMesh()->GetPhysicsLinearVelocity() : FVector::ZeroVector;
		float RagSpeed = RagVel.Size();
		if (FallTimer >= FallRecoveryTime && RagSpeed < 50.0f)
		{
			SavedRagdollLocation = GetMesh()->GetComponentLocation();
			SavedRagdollQuat = GetMesh()->GetComponentQuat();
			GetMesh()->SetSimulatePhysics(false);
			GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			bIsRagdolling = false;
			bIsGettingUp = true;
			GetUpTimer = 0.0f;
			FVector RagLoc = SavedRagdollLocation;
			SetActorLocation(FVector(RagLoc.X, RagLoc.Y, GetActorLocation().Z));
			UE_LOG(LogParamotor, Log, TEXT("ParamotorCharacter: Starting get-up blend"));
		}
	}
	else if (bIsGettingUp)
	{
		GetUpTimer += DeltaTime;
		float BlendAlpha = FMath::Clamp(GetUpTimer / GetUpBlendTime, 0.0f, 1.0f);
		FVector StandLoc(0.0f, 0.0f, -90.0f);
		FQuat BaseQuat(FVector::UpVector, FMath::DegreesToRadians(-90.0f));
		FQuat StandQuat = BaseQuat;
		FVector CurrentMeshLoc = GetMesh()->GetRelativeLocation();
		FQuat CurrentMeshQuat = GetMesh()->GetRelativeRotation().Quaternion();
		FVector NewLoc = FMath::Lerp(CurrentMeshLoc, StandLoc, BlendAlpha);
		FQuat NewQuat = FQuat::Slerp(CurrentMeshQuat, StandQuat, BlendAlpha);
		GetMesh()->SetRelativeLocation(NewLoc);
		GetMesh()->SetRelativeRotation(NewQuat);
		if (BlendAlpha >= 1.0f)
		{
			bIsFallen = false;
			bIsGettingUp = false;
			GetUpTimer = 0.0f;
			FallTimer = 0.0f;
			TripRiskAccumulator = 0.0f;
			if (CMC)
			{
				CMC->SetMovementMode(MOVE_Walking);
				CMC->MaxWalkSpeed = EffectiveMaxSpeed;
			}
			GetMesh()->SetRelativeLocation(FVector(0.0f, 0.0f, -90.0f));
			FQuat FinalBaseQuat(FVector::UpVector, FMath::DegreesToRadians(-90.0f));
			GetMesh()->SetRelativeRotation(FinalBaseQuat);
			if (WalkIdleAnim)
			{
				CurrentAnim = WalkIdleAnim;
				if (UParamotorAnimInstance* PA = Cast<UParamotorAnimInstance>(GetMesh()->GetAnimInstance()))
					PA->PlaySequence(WalkIdleAnim, true);
			}
			UE_LOG(LogParamotor, Log, TEXT("ParamotorCharacter: Recovered from fall"));
		}
	}
	return true;  // Skip all normal movement/animation/audio code while fallen
}
FString AParamotorCharacter::GetInjuryName() const
{
	switch (CurrentInjury)
	{
	case EInjuryType::SprainedAnkle: return TEXT("Sprained Ankle");
	case EInjuryType::BrokenAnkle:   return TEXT("Broken Ankle");
	case EInjuryType::BrokenLeg:     return TEXT("Broken Leg");
	default:                         return TEXT("None");
	}
}
void AParamotorCharacter::ApplyImpactInjury(float ImpactSpeedMs, bool bFromTrip)
{
	// Impact thresholds (vertical descent speed in m/s):
	// - < 3 m/s: safe landing, no injury
	// - 3-5 m/s: possible sprained ankle (especially from trip)
	// - 5-8 m/s: likely broken ankle
	// - > 8 m/s: broken leg (hard landing)
	//
	// For trips, ImpactSpeedMs is the horizontal speed at fall time.
	// Trips at low speed = no injury, trips at high speed = sprain risk.

	EInjuryType NewInjury = EInjuryType::None;
	float Severity = 0.0f;

	if (bFromTrip)
	{
		// Trip injuries are based on horizontal speed
		if (ImpactSpeedMs > 8.0f)
		{
			NewInjury = EInjuryType::BrokenAnkle;
			Severity = FMath::Clamp((ImpactSpeedMs - 8.0f) / 4.0f, 0.5f, 0.9f);
		}
		else if (ImpactSpeedMs > 5.0f)
		{
			NewInjury = EInjuryType::SprainedAnkle;
			Severity = FMath::Clamp((ImpactSpeedMs - 5.0f) / 3.0f, 0.3f, 0.6f);
		}
		// Low-speed trips = no injury (just a stumble)
	}
	else
	{
		// Landing injuries based on vertical impact speed
		if (ImpactSpeedMs > 8.0f)
		{
			NewInjury = EInjuryType::BrokenLeg;
			Severity = FMath::Clamp((ImpactSpeedMs - 8.0f) / 4.0f, 0.7f, 1.0f);
		}
		else if (ImpactSpeedMs > 5.0f)
		{
			NewInjury = EInjuryType::BrokenAnkle;
			Severity = FMath::Clamp((ImpactSpeedMs - 5.0f) / 3.0f, 0.4f, 0.7f);
		}
		else if (ImpactSpeedMs > 3.0f)
		{
			NewInjury = EInjuryType::SprainedAnkle;
			Severity = FMath::Clamp((ImpactSpeedMs - 3.0f) / 2.0f, 0.2f, 0.5f);
		}
	}

	// Only upgrade injury, never downgrade from a new lesser impact
	if (NewInjury != EInjuryType::None)
	{
		int32 NewRank = (int32)NewInjury;
		int32 CurRank = (int32)CurrentInjury;
		if (NewRank > CurRank)
		{
			CurrentInjury = NewInjury;
			InjurySeverity = Severity;
		}
		else if (NewRank == CurRank)
		{
			// Same type — worsen the severity
			InjurySeverity = FMath::Clamp(FMath::Max(InjurySeverity, Severity), 0.0f, 1.0f);
		}

		// Apply movement penalties based on injury
		switch (CurrentInjury)
		{
		case EInjuryType::SprainedAnkle:
			InjuryMaxSpeedMultiplier = 0.65f;  // 65% speed — noticeable limp
			InjuryJumpMultiplier = 0.5f;       // 50% jump height
			break;
		case EInjuryType::BrokenAnkle:
			InjuryMaxSpeedMultiplier = 0.35f;  // 35% speed — severe limp
			InjuryJumpMultiplier = 0.2f;       // Barely can hop
			break;
		case EInjuryType::BrokenLeg:
			InjuryMaxSpeedMultiplier = 0.15f;  // 15% speed — can barely shuffle
			InjuryJumpMultiplier = 0.0f;       // No jumping
			break;
		default:
			break;
		}

		InjuryHealTimer = 0.0f;
		UE_LOG(LogParamotor, Log, TEXT("ParamotorCharacter: INJURY — %s (severity %.2f, impact %.1f m/s, fromTrip=%d)"),
			*GetInjuryName(), InjurySeverity, ImpactSpeedMs, bFromTrip ? 1 : 0);
	}
}
void AParamotorCharacter::HealInjury(float DeltaTime)
{
	if (CurrentInjury == EInjuryType::None) return;

	InjuryHealTimer += DeltaTime;

	// Healing times (seconds for full heal at max severity):
	// - Sprained ankle: 60s
	// - Broken ankle: 180s (3 min)
	// - Broken leg: 300s (5 min)
	float HealTime = 60.0f;
	switch (CurrentInjury)
	{
	case EInjuryType::SprainedAnkle: HealTime = 60.0f;  break;
	case EInjuryType::BrokenAnkle:   HealTime = 180.0f; break;
	case EInjuryType::BrokenLeg:     HealTime = 300.0f; break;
	default: return;
	}

	// Gradual healing — severity decreases over time
	float HealRate = (1.0f / HealTime) * DeltaTime;
	InjurySeverity = FMath::Max(0.0f, InjurySeverity - HealRate);

	// Gradually restore movement as severity decreases
	float HealFraction = FMath::Clamp(1.0f - InjurySeverity, 0.0f, 1.0f);  // 0 = just injured, 1 = fully healed
	switch (CurrentInjury)
	{
	case EInjuryType::SprainedAnkle:
		InjuryMaxSpeedMultiplier = FMath::Lerp(0.65f, 1.0f, HealFraction);
		InjuryJumpMultiplier = FMath::Lerp(0.5f, 1.0f, HealFraction);
		break;
	case EInjuryType::BrokenAnkle:
		InjuryMaxSpeedMultiplier = FMath::Lerp(0.35f, 1.0f, HealFraction);
		InjuryJumpMultiplier = FMath::Lerp(0.2f, 1.0f, HealFraction);
		break;
	case EInjuryType::BrokenLeg:
		InjuryMaxSpeedMultiplier = FMath::Lerp(0.15f, 1.0f, HealFraction);
		InjuryJumpMultiplier = FMath::Lerp(0.0f, 1.0f, HealFraction);
		break;
	default:
		break;
	}

	// Fully healed
	if (InjurySeverity <= 0.0f)
	{
		InjurySeverity = 0.0f;
		CurrentInjury = EInjuryType::None;
		InjuryMaxSpeedMultiplier = 1.0f;
		InjuryJumpMultiplier = 1.0f;
		UE_LOG(LogParamotor, Log, TEXT("ParamotorCharacter: Injury healed — fully recovered"));
	}
}