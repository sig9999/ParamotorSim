// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "ParamotorPlayerController.generated.h"

class AParamotorActor;
class AWingActor;

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

	/** Spawn the wing in front of the player (uses current model index) */
	UFUNCTION(BlueprintCallable, Category = "Spawn")
	void SpawnWing();

	/** Spawn a specific wing model by index */
	UFUNCTION(BlueprintCallable, Category = "Spawn")
	void SpawnWingByModel(int32 ModelIndex);

	/** Get current wing model index and available models for HUD */
	int32 GetCurrentWingModelIndex() const { return CurrentWingModelIndex; }
	const TArray<FString>& GetWingModelNames() const { return WingModelNames; }

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

	/** Apply wing model specs to a spawned wing actor */
	void ApplyWingModel(AWingActor* Wing, int32 ModelIndex);

	/** Track spawned actors so we can clean them up */
	UPROPERTY()
	TArray<AActor*> SpawnedActors;

	/** The currently spawned paramotor actor (for interaction) */
	UPROPERTY()
	AParamotorActor* SpawnedParamotorActor = nullptr;

	// Throttle input state
	bool bThrottleUpHeld = false;
	bool bThrottleDownHeld = false;

	// Trimmer input state
	bool bTrimmerFastHeld = false;
	bool bTrimmerSlowHeld = false;

	// Brake input state
	bool bLeftBrakeHeld = false;
	bool bRightBrakeHeld = false;
	// Progressive brake amounts (0-1) — keys ramp up/down like real brake handles
	float LeftBrakeAmount = 0.0f;
	float RightBrakeAmount = 0.0f;

	// Wing model selection
	int32 CurrentWingModelIndex = 0;
	TArray<FString> WingModelNames;

	virtual void Tick(float DeltaTime) override;

	// Override UpdateRotation to prevent the controller from forcing the pawn
	// to the control rotation (which has zero roll). During flight, the WingActor
	// controls the pawn's full rotation (bank, pitch, yaw). The controller should
	// only update the control rotation for camera look, NOT force it on the pawn.
	virtual void UpdateRotation(float DeltaTime) override;
};
