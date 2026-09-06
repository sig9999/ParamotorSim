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
	float GetEngineTemp() const { return EngineTemp; }
	float GetFuelLevel() const { return FuelLevel; }
	float GetTotalRunTime() const { return TotalRunTime; }
	float GetEGT() const { return EGT; }
	bool IsFuelCutoffOn() const { return bFuelCutoff; }
	void ToggleFuelCutoff();

	// === REAL PROPULSION API ===
	// Compute actual thrust force in Newtons based on RPM, propeller model, and airspeed
	// Uses momentum theory: T = 2 * rho * A * Vi² where Vi is induced velocity
	// Static thrust at full throttle ~736N (75 kgf) for a Moster 185
	float GetThrustForceN(float AirspeedMs = 0.0f) const;
	// Get propeller RPM (separate from engine RPM via reduction ratio)
	float GetPropRPM() const;
	// Get engine power output in Watts
	float GetEnginePowerW() const;
	// Get propeller absorbed torque in N·m (from blade element theory)
	float GetPropTorqueNM(float AirspeedMs = 0.0f) const;

	// Wing inflation - plays the crispy fabric inflation sound
	void PlayWingInflate();

protected:
	// Root scene component
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paramotor")
	USceneComponent* Root;

	// Interaction sphere
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paramotor")
	USphereComponent* InteractionSphere;

	// Prompt widget component
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Paramotor")
	UWidgetComponent* PromptWidget;

	// Audio components
	UPROPERTY()
	UAudioComponent* StarterAudio;
	UPROPERTY()
	UAudioComponent* EngineAudio;
	UPROPERTY()
	UAudioComponent* ExhaustAudio;
	UPROPERTY()
	UAudioComponent* PropWindAudio;
	UPROPERTY()
	UAudioComponent* WingInflateAudio;

	// All mesh components (populated in BeginPlay)
	UPROPERTY()
	TArray<UStaticMeshComponent*> MeshComponents;

	// Prop mesh components (for rotation)
	UPROPERTY()
	TArray<UStaticMeshComponent*> PropMeshes;

	bool bEquipped = false;

	// Engine state
	EEngineState EngineState = EEngineState::Off;
	float Throttle = 0.0f;        // 0 to 1
	float PropRotation = 0.0f;    // Current prop angle (radians)
	float StarterTimer = 0.0f;    // Time since starter began
	float EngineRPM = 0.0f;       // Current RPM (smoothed)
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

	// Mesh paths for all paramotor parts
	static const TArray<FString> GetParamotorMeshPaths();

	// Initialize audio components
	void InitAudio();
};
