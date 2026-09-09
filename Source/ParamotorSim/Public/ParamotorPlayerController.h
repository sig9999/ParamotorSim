// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "ParamotorPlayerController.generated.h"

class AParamotorActor;
class AWingActor;
class AParamotorCharacter;

UCLASS()
class PARAMOTORSIM_API AParamotorPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AParamotorPlayerController();

	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;

	// Get the spawned paramotor actor (for HUD access)
	AParamotorActor* GetSpawnedParamotorActor() const { return SpawnedParamotorActor; }

	UFUNCTION(BlueprintCallable, Category = "Pause")
	void TogglePauseMenu();

	/** Spawn the paramotor in front of the player from all its static mesh parts */
	UFUNCTION(BlueprintCallable, Category = "Spawn")
	void SpawnParamotor();

	/** Spawn the MacPara Charger 25M test wing (the only wing in the game) */
	UFUNCTION(BlueprintCallable, Category = "Spawn")
	void SpawnTestWing();

	/** Spawn the test paramotor frame (ParamotorTestFrame mesh from ParamotorTestGood.blend) */
	UFUNCTION(BlueprintCallable, Category = "Spawn")
	void SpawnTestParamotor();

	/** Spawn the Rider Moster 185 paramotor (from Codex Blender model) */
	UFUNCTION(BlueprintCallable, Category = "Spawn")
	void SpawnRider185();

	/** Clear all spawned actors (paramotor, wing, mesh parts) */
	UFUNCTION(BlueprintCallable, Category = "Spawn")
	void ClearSpawnedActors();

	/** Called when F is pressed - interacts with nearby paramotor */
	void OnInteract();

	/** Called when left mouse button is pressed - grab/release wing via crosshair */
	void OnClickGrab();

	/** Engine controls */
	void OnStartEnginePressed();
	void OnStartEngineReleased();
	void OnThrottleUp();
	void OnThrottleDown();
	void OnThrottleUpReleased();
	void OnThrottleDownReleased();

	/** Wing brake controls (Q = left, E = right) */
	void OnLeftBrakePressed();
	void OnLeftBrakeReleased();
	void OnRightBrakePressed();
	void OnRightBrakeReleased();

	/** A-line lift control (R = lift A-lines to inflate wing) */
	void OnALinesPressed();
	void OnALinesReleased();

	/** Trimmer controls (= = trim fast, - = trim slow) */
	void OnTrimmerFastPressed();
	void OnTrimmerSlowPressed();
	void OnTrimmerReleased();

	void OnFuelCutoff();
	void OnForceTakeoff();

	/** Toggle seated/standing position (G key) — reduces drag in flight */
	void OnToggleSeat();

	/** Hang point adjustments ([ and ] keys) — lean motor forward/back */
	void OnHangPointForward();
	void OnHangPointBack();

	/** Free cam — middle mouse button toggles look-only mode (camera moves, character doesn't) */
	void OnFreeCamPressed();
	void OnFreeCamReleased();
	bool bFreeCamActive = false;

	/** The currently spawned wing actor */
	UPROPERTY()
	AWingActor* SpawnedWingActor = nullptr;

protected:
	UPROPERTY()
	UUserWidget* PauseMenuInstance = nullptr;

	UPROPERTY()
	UUserWidget* FlightHUDInstance = nullptr;

	bool bIsPaused = false;
	float IKLogTimer = 0.0f;  // Member variable (not static) for IK debug logging

	/** Track spawned actors so we can clean them up */
	UPROPERTY()
	TArray<AActor*> SpawnedActors;

	/** The currently spawned paramotor actor (for interaction) */
	UPROPERTY()
	AParamotorActor* SpawnedParamotorActor = nullptr;

	// Throttle input state
	bool bThrottleUpHeld = false;
	bool bThrottleDownHeld = false;

	// Brake input state
	bool bLeftBrakeHeld = false;
	bool bRightBrakeHeld = false;
	// Progressive brake amounts (0-1) — keys ramp up/down like real brake handles
	float LeftBrakeAmount = 0.0f;
	float RightBrakeAmount = 0.0f;

	virtual void Tick(float DeltaTime) override;

	// Override UpdateRotation to prevent the controller from forcing the pawn
	// to the control rotation (which has zero roll). During flight, the WingActor
	// controls the pawn's full rotation (bank, pitch, yaw). The controller should
	// only update the control rotation for camera look, NOT force it on the pawn.
	virtual void UpdateRotation(float DeltaTime) override;

private:
	// Compute carabiner world positions from the paramotor's real hardware,
	// falling back to character-position-based estimation if unavailable.
	// Returns true if real positions were used, false if fallback was used.
	bool GetCarabinerPositions(FVector& OutLeft, FVector& OutRight);

	// ============================================================
	// Tick helpers (split from Tick)
	// ============================================================

	// Process throttle input (ramp up/down + spring return) and pass thrust to character
	void UpdateThrottle(float DeltaTime);

	// Update grab position while grabbing — follows the crosshair aim point
	void UpdateGrabPosition(APawn* CachedPawn);

	// Update wing riser connection: carabiners, brakes, weight shift, arm IK
	void UpdateWingRig(float DeltaTime, APawn* CachedPawn);

	// Compute which brake handle is on the pilot's left vs right (wing may be yawed)
	void ComputeBrakeArmTargets(AParamotorCharacter* Char, AWingActor* Wing,
		FVector& OutPilotLeftTarget, FVector& OutPilotRightTarget);

	// ============================================================
	// Spawn helpers
	// ============================================================

	// Trace down from Start to find ground Z. Returns ground Z or 0.0f if no hit.
	float FindGroundZ(FVector Start, APawn* IgnorePawn);
};

