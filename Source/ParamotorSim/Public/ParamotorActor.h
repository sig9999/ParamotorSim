// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ParamotorActor.generated.h"

class UStaticMeshComponent;
class USphereComponent;
class UWidgetComponent;
class UAudioComponent;

UENUM(BlueprintType)
enum class EEngineState : uint8
{
	Off,
	Cranking,
	Running
};

UCLASS()
class PARAMOTORSIM_API AParamotorActor : public AActor
{
	GENERATED_BODY()

public:
	AParamotorActor();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void Destroyed() override;

	// Called by the player controller when F is pressed while overlapping
	void Interact(class AParamotorCharacter* PlayerCharacter);

	// Whether this paramotor is currently equipped
	bool IsEquipped() const { return bEquipped; }

	// Engine control - called by player controller
	void StartCranking();
	void StopCranking();
	void StopEngine();
	void SetThrottle(float NewThrottle);
	float GetThrottle() const { return Throttle; }
	EEngineState GetEngineState() const { return EngineState; }
	float GetEngineRPM() const { return EngineRPM; }
	float GetRedlineRPM() const { return EngineRedlineRPM; }
	float GetEngineTemp() const { return EngineTemp; }
	float GetFuelLevel() const { return FuelLevel; }
	float GetTotalRunTime() const { return TotalRunTime; }
	float GetEGT() const { return EGT; }
	bool IsFuelCutoffOn() const { return bFuelCutoff; }
	void ToggleFuelCutoff();

	// When true, mount the real Codex part Blueprints (Amaruk frame, EOS
	// engine, PT130 prop, PowerSeat harness) instead of the procedural body
	// meshes. Rigging (A-arms, rings, carabiners, risers) stays procedural —
	// the wing physics reads carabiner positions from it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paramotor")
	bool bUseCodexBuild = false;

	// Engine tuning from the equipped hangar build — ratio vs the 25hp
	// Moster 185 baseline the blade-element model was tuned on.
	UPROPERTY(BlueprintReadOnly, Category = "Paramotor")
	float PowerScale = 1.0f;

	// Mass of the assembled rig in kg (parts + fuel) — the wing adds this to
	// pilot weight for takeoff/flight math.
	UPROPERTY(BlueprintReadOnly, Category = "Paramotor")
	float RigMassKg = 30.0f;

	float GetParamotorMassKg() const { return RigMassKg; }

	/** Pull engine/prop/frame/harness specs out of the game instance's
	 *  CurrentBuild and configure power + mass. Call before FinishSpawning. */
	void ConfigureFromBuild(class UParamotorGameInstance* GI);

	// === REAL PROPULSION API ===
	// Compute actual thrust force in Newtons based on RPM, propeller model, and airspeed
	// Uses momentum theory: T = 2 * rho * A * Vi² where Vi is induced velocity
	// Static thrust at full throttle ~736N (75 kgf) for a Moster 185
	float GetThrustForceN(float AirspeedMs = 0.0f) const;
	// Get propeller RPM (separate from engine RPM via reduction ratio)
	float GetPropRPM() const;

	/** World transform of the spinning prop (Codex prop actor when present,
	 *  else the actor transform). Used by prop-wash ground effects. */
	UFUNCTION(BlueprintPure)
	FTransform GetPropTransform() const;
	// Get engine power output in Watts
	float GetEnginePowerW() const;
	// Get propeller absorbed torque in N·m (from blade element theory)
	float GetPropTorqueNM(float AirspeedMs = 0.0f) const;

	// Get world-space carabiner positions from the wing clip rings on the frame
	// Returns false if the rings can't be found (falls back to estimated position)
	bool GetCarabinerWorldPositions(FVector& OutLeft, FVector& OutRight) const;

	// Update A-arm pivots to point toward the wing (called in Tick)
	void UpdateAArmPivots();

protected:
	// Root scene component
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paramotor")
	USceneComponent* Root = nullptr;

	// === HANG SWING ===
	// The harness welds to the body; the frame+engine+prop hang under this
	// pivot (shoulder-strap height) and swing like a damped pendulum —
	// lean-back under acceleration, sway in turns, settle at rest.
	UPROPERTY()
	USceneComponent* HangPivot = nullptr;

	// The harness part actor (CodexPartActors[3]) — offset is tunable.
	UPROPERTY()
	AActor* CodexHarnessActor = nullptr;

	FVector HangPivotLoc = FVector(0.0f, 0.0f, 42.0f);  // Root-space hang point
	FVector HarnessOffset = FVector(8.0f, 0.0f, -4.0f);  // Harness fit offset
	FRotator HarnessRotOffset = FRotator::ZeroRotator;
	float HangSwingK = 55.0f;       // Pendulum stiffness
	float HangSwingC = 9.0f;        // Damping
	float HangSwingMax = 16.0f;     // Max swing angle (deg)
	FVector2D HangAng = FVector2D::ZeroVector;  // Smoothed (pitch, roll) deg
	FVector2D HangVel = FVector2D::ZeroVector;
	FVector PrevPilotVel = FVector::ZeroVector;
	bool bHasPrevPilotVel = false;
	void UpdateHangSwing(float DeltaTime);

	// Interaction sphere
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paramotor")
	USphereComponent* InteractionSphere = nullptr;

	// Prompt widget component
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paramotor")
	UWidgetComponent* PromptWidget = nullptr;

	// Audio components
	UPROPERTY()
	UAudioComponent* StarterAudio = nullptr;
	UPROPERTY()
	UAudioComponent* EngineAudio = nullptr;
	UPROPERTY()
	UAudioComponent* ExhaustAudio = nullptr;
	UPROPERTY()
	UAudioComponent* PropWindAudio = nullptr;

	// All mesh components (populated in BeginPlay)
	UPROPERTY()
	TArray<UStaticMeshComponent*> MeshComponents;

	// === A-ARM PIVOT HIERARCHY ===
	// Real paramotor: A-arms pivot from the frame studs, carabiners clip through
	// the rings at the bottom, risers hang from the carabiners toward the wing.
	// The A-arms swing freely so the wing can move independently of the frame.

	// A-arm pivot scene components (rotate to follow wing direction)
	UPROPERTY()
	USceneComponent* AArmPivotLeft = nullptr;
	UPROPERTY()
	USceneComponent* AArmPivotRight = nullptr;

	// Fixed carabiner offset from A-arm pivot (captured at setup, before any rotation).
	// Used for physics carabiner positions so A-arm rotation doesn't feed back into line physics.
	FVector CarabinerOffsetFromPivot = FVector::ZeroVector;

	// A-arm mesh components (attached to pivots)
	UPROPERTY()
	UStaticMeshComponent* AArmMeshLeft = nullptr;
	UPROPERTY()
	UStaticMeshComponent* AArmMeshRight = nullptr;

	// Ring mesh components (at bottom of A-arms)
	UPROPERTY()
	UStaticMeshComponent* RingMeshLeft = nullptr;
	UPROPERTY()
	UStaticMeshComponent* RingMeshRight = nullptr;

	// Carabiner mesh components (clip through rings)
	UPROPERTY()
	UStaticMeshComponent* CarabinerMeshLeft = nullptr;
	UPROPERTY()
	UStaticMeshComponent* CarabinerMeshRight = nullptr;

	// Riser mesh components (webbing straps from carabiner toward wing)
	UPROPERTY()
	UStaticMeshComponent* RiserMeshLeft = nullptr;
	UPROPERTY()
	UStaticMeshComponent* RiserMeshRight = nullptr;

	// Brake toggle mesh components (handles on risers)
	UPROPERTY()
	UStaticMeshComponent* BrakeToggleMeshLeft = nullptr;
	UPROPERTY()
	UStaticMeshComponent* BrakeToggleMeshRight = nullptr;

	// Quick link mesh components (connect risers to lines)
	UPROPERTY()
	TMap<FString, UStaticMeshComponent*> QuickLinkMeshes;

	// Prop mesh components (for rotation)
	UPROPERTY()
	TArray<UStaticMeshComponent*> PropMeshes;

	// Base relative transforms of prop meshes (stored once to avoid rotation compounding)
	UPROPERTY()
	TArray<FTransform> PropMeshBaseTransforms;

	// Center of propeller rotation (world-relative during spool-up)
	UPROPERTY()
	FVector PropCenter = FVector(15.0f, 0.0f, 73.0f);

	bool bEquipped = false;

	// Engine state
	EEngineState EngineState = EEngineState::Off;
	float Throttle = 0.0f;        // 0 to 1
	float PropRotation = 0.0f;    // Current prop angle (radians)
	float StarterTimer = 0.0f;    // Time since starter began
	float EngineRPM = 0.0f;       // Current RPM (smoothed)

	// Per-engine rev limits — set from the selected engine spec at build time
	float EngineIdleRPM = 2500.0f;
	float EngineRedlineRPM = 7800.0f;
	float EngineTemp = 20.0f;     // Engine temperature (Celsius), starts at ambient
	float FuelLevel = 1.0f;       // Fuel level (0 to 1), starts full
	float TotalRunTime = 0.0f;    // Total engine run time (seconds)
	float EGT = 20.0f;            // Exhaust gas temperature (Celsius)
	bool bFuelCutoff = false;     // Fuel cutoff switch (kills engine when on)

	// The player currently overlapping (for prompt display)
	UPROPERTY()
	AParamotorCharacter* OverlappingPlayer = nullptr;

	UFUNCTION()
	void OnOverlapBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep,
		const FHitResult& SweepResult);

	UFUNCTION()
	void OnOverlapEnd(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	// Load and attach all paramotor mesh parts
	void BuildParamotorMeshes();

	// Spawn the Codex part Blueprints onto Root using the same frame-space
	// transforms as the hangar builder. Used when bUseCodexBuild is set.
	void BuildCodexVisuals();

	// Spawned Codex part actors (frame, engine, prop, harness)
	UPROPERTY()
	TArray<AActor*> CodexPartActors;

	// The Codex prop actor — spun in UpdatePropMeshRotation.
	UPROPERTY()
	AActor* CodexPropActor = nullptr;
	FQuat CodexPropBaseRot = FQuat::Identity;

	// Equip pose + walk-cycle bob state (codex build bounces with the player)
	FVector EquipBaseLoc = FVector::ZeroVector;
	FRotator EquipBaseRot = FRotator::ZeroRotator;
	float BobPhase = 0.0f;

	// Build the A-arm pivot hierarchy (frame → stud → pivot → A-arm → ring → carabiner → risers)
	void BuildAArmHierarchy();
	// Build one side (left or right) of the A-arm hierarchy. Returns the pivot scene component.
	USceneComponent* BuildSingleAArmSide(bool bLeft, USceneComponent* Stud);

	// Shared blade-element integration helper (used by GetThrustForceN and GetPropTorqueNM)
	// Returns thrust (N) and torque (N·m) for the current propeller state
	struct FBladeElementResult { float ThrustN; float TorqueNM; };
	FBladeElementResult ComputeBladeElement(float AirspeedMs) const;

	// Rotate prop meshes around the prop shaft (shared by running and spool-down paths)
	void UpdatePropMeshRotation();

	// === TICK HELPERS (split from Tick for readability) ===
	void UpdateCranking(float DeltaTime);     // Cranking state — engine catch logic
	void UpdateEngineRPM(float DeltaTime);    // Running state — RPM dynamics, torque balance
	void UpdateEngineTemp(float DeltaTime);   // Running state — CHT and EGT
	void UpdateFuel(float DeltaTime);         // Running state — fuel consumption
	void UpdateEngineAudio();                 // Running state — sound pitch/volume
	void UpdateEngineSpoolDown(float DeltaTime); // Not running — prop slows, engine cools

	// Mesh paths for all paramotor parts
	static const TArray<FString> GetParamotorMeshPaths();

	// Initialize audio components
	void InitAudio();
};
