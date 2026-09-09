#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "ParamotorCharacter.generated.h"

class UAudioComponent;

UCLASS()
class PARAMOTORSIM_API AParamotorCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AParamotorCharacter();

	// Called by controller to set thrust level (for trip/fall logic)
	void SetThrust(float ThrustLevel) { CurrentThrust = ThrustLevel; }

	// Trip the character
	void Trip();

	// === INJURY SYSTEM ===
	// Injuries affect movement speed, jump height, and can prevent walking.
	// Caused by hard landings, trip falls, and excessive impact.
	// Severity ranges from 0.0 (uninjured) to 1.0 (critical).
	enum class EInjuryType : uint8
	{
		None,
		SprainedAnkle,    // Mild — limp, reduced speed
		BrokenAnkle,      // Severe — can't run, severe limp
		BrokenLeg,        // Critical — can't walk without assistance, very slow
	};

	// Get current injury status for HUD
	EInjuryType GetInjuryType() const { return CurrentInjury; }
	float GetInjurySeverity() const { return InjurySeverity; }
	FString GetInjuryName() const;
	bool IsInjured() const { return CurrentInjury != EInjuryType::None; }

	// Apply injury from an impact (called by WingActor on hard landing, or by Trip)
	void ApplyImpactInjury(float ImpactSpeedMs, bool bFromTrip);

	// Heal injury over time (called in Tick)
	void HealInjury(float DeltaTime);

	// Full heal (e.g. on respawn or menu action)
	void ClearInjury();

	// Set brake arm targets (for arm IK reaching to brake handles)
	void SetBrakeArmTargets(bool bLeftActive, bool bRightActive, FVector LeftTarget, FVector RightTarget,
		bool bLeftBraking = false, bool bRightBraking = false);
	void ClearBrakeArmTargets();

	// Brake arm IK state (public so anim instance can read)
	bool bLeftBrakeArmActive = false;
	bool bRightBrakeArmActive = false;
	FVector LeftBrakeHandTarget = FVector::ZeroVector;
	FVector RightBrakeHandTarget = FVector::ZeroVector;
	float LeftArmRaise = 0.0f;
	float RightArmRaise = 0.0f;
	float LeftArmPull = 0.0f;
	float RightArmPull = 0.0f;
	bool bLeftBrakePulling = false;   // Q held — pull left arm down
	bool bRightBrakePulling = false;  // E held — pull right arm down

	// === WEIGHT SHIFT (A/D keys while flying) ===
	// A = shift left (lowers left riser → turns left)
	// D = shift right (lowers right riser → turns right)
	// Mouse only orbits the camera when flying, does not control weight shift.
	float WeightShift = 0.0f;
	float WeightShiftTarget = 0.0f;  // Target from mouse input
	float WeightShiftVisualRoll = 0.0f;  // Smoothed visual roll for character mesh
	bool bWeightShiftActive = false;  // Set by controller when wing is flying

protected:
	virtual void BeginPlay() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	virtual void Tick(float DeltaTime) override;

private:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<class UCameraComponent> Camera = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SpringArm", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<class USpringArmComponent> SpringArm = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mesh", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<class USkeletalMeshComponent> HeadMesh = nullptr;

	// Clothing components (techwear outfit)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mesh", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<class USkeletalMeshComponent> OutfitMesh = nullptr;

	UPROPERTY()
	TObjectPtr<class UAnimSequence> WalkIdleAnim = nullptr;

	UPROPERTY()
	TObjectPtr<class UAnimSequence> WalkAnimAsset = nullptr;

	UPROPERTY()
	TObjectPtr<class UAnimSequence> JogAnimAsset = nullptr;

	UPROPERTY()
	TObjectPtr<class UAnimSequence> CurrentAnim = nullptr;

	// Audio
	UPROPERTY()
	UAudioComponent* FootstepAudio = nullptr;

	// Footstep sounds separated by surface type and intensity
	UPROPERTY()
	TArray<class USoundBase*> GrassFootstepSounds;

	UPROPERTY()
	TArray<class USoundBase*> DirtFootstepSounds;

	UPROPERTY()
	TArray<class USoundBase*> GrassFootstepHeavySounds;

	UPROPERTY()
	TArray<class USoundBase*> DirtFootstepHeavySounds;

	UPROPERTY()
	UAudioComponent* WindAmbientAudio = nullptr;

	// Fall sound
	UPROPERTY()
	UAudioComponent* FallAudio = nullptr;

	bool bSprintRequested = false;
	float CurrentMaxSpeed = 200.0f;
	float SmoothedAnimSpeed = 0.0f;

	// Footstep timing
	float FootstepTimer = 0.0f;

	// Trip/fall state
	bool bIsFallen = false;
	bool bIsRagdolling = false;
	bool bIsGettingUp = false;
	float FallTimer = 0.0f;
	float FallRecoveryTime = 2.5f; // Seconds on ground before getting up
	float GetUpBlendTime = 1.0f; // Seconds to blend from ragdoll to standing
	float GetUpTimer = 0.0f;
	FQuat SavedRagdollQuat;
	FVector SavedRagdollLocation;
	float TripRiskAccumulator = 0.0f; // Accumulates trip risk while running with high thrust
	float AirTimeAccum = 0.0f;       // Air time during thrust (instance member, was static)
	float MaxAirHeight = 0.0f;       // Max height during air episode (instance member, was static)

	// Current thrust from paramotor (set by player controller)
	float CurrentThrust = 0.0f;

	// True if player was in the air while thrust was active (causes trip on landing)
	bool bWasInAirWithThrust = false;

	// === INJURY STATE ===
	EInjuryType CurrentInjury = EInjuryType::None;
	float InjurySeverity = 0.0f;       // 0-1, how bad the injury is
	float InjuryHealTimer = 0.0f;      // Time since injury (for gradual healing)
	float InjuryMaxSpeedMultiplier = 1.0f;  // Applied to MaxWalkSpeed
	float InjuryJumpMultiplier = 1.0f;     // Applied to JumpZVelocity

	void UpdateBrakeArmIK(float DeltaTime);
	void PlayFootstep();
	void MoveForward(float Value);
	void MoveRight(float Value);
	void Turn(float Value);
	void LookUp(float Value);
	void MouseWheel(float Value);
	void StartJump();
	void StopJump();
	void StartSprint();
	void StopSprint();

	// === TICK SPLIT HELPERS ===
	// Returns true if the fall is being handled (caller should early-return).
	bool UpdateFallState(float DeltaTime, UCharacterMovementComponent* CMC, float EffectiveMaxSpeed);

	// === BEGINPLAY SPLIT HELPERS ===
	void LoadCharacterMesh();       // Replace body with techwear CombinedSkelMesh
	void LoadAnimations();          // Load anim assets and set up anim instance
	void ApplySkinTone();           // Apply skin material to head and body
	void LoadFootstepSounds();      // Load footstep sound banks
	void LoadAmbientAudio();        // Start wind and fall sounds
	void UpdateThrustGroundPhysics(float DeltaTime, UCharacterMovementComponent* CMC, float EffectiveMaxSpeed, bool bOnGround, bool bThrustActive, class AParamotorPlayerController* PC);
	// Apply forward thrust acceleration to a running pilot, clamped to wing-assisted max run speed.
	void ApplyThrustRunningBoost(float DeltaTime, UCharacterMovementComponent* CMC, class AParamotorPlayerController* PC);
	void UpdateThrustAirPhysics(float DeltaTime, UCharacterMovementComponent* CMC, bool bThrustActive, bool bOnGround, bool bWingFlying, class AParamotorPlayerController* PC);
	void UpdatePostureLean(float DeltaTime, UCharacterMovementComponent* CMC, class AParamotorPlayerController* PC);
	void UpdateAnimationState(float DeltaTime, UCharacterMovementComponent* CMC);
	void UpdateFootstepAudio(float DeltaTime, UCharacterMovementComponent* CMC);
	void UpdateWindAudio();
	void UpdateTripRisk(float DeltaTime, UCharacterMovementComponent* CMC, class AParamotorPlayerController* PC);
	void UpdateRotationHandling(float DeltaTime);

	// Get current thrust force (Newtons) from the equipped paramotor.
	// Returns 0 if no paramotor is equipped. Airspeed is computed from velocity.
	float GetEquippedThrustN();
};
