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

	/** Clear all spawned actors (paramotor, wing, mesh parts) */
	UFUNCTION(BlueprintCallable, Category = "Spawn")
	void ClearSpawnedActors();

	/** Dev tool: teleport pawn into fly mode at a world position — "PsFlyTo X Y Z [Pitch Yaw]" */
	UFUNCTION(Exec)
	void PsFlyTo(float X, float Y, float Z, float Pitch = 0.0f, float Yaw = 0.0f);

	/** Dev tool: orbit the follow camera relative to the pawn — "PsCamYaw <deg> [pitch]" */
	UFUNCTION(Exec)
	void PsCamYaw(float Yaw, float Pitch = 0.0f);

	/** Dev tool: rotate the pawn in place — "PsTurn <deg>" */
	UFUNCTION(Exec)
	void PsTurn(float Yaw);

	/** Dev tool: rotate the character mesh relative to the actor — "PsMeshYaw <deg>" */
	UFUNCTION(Exec)
	void PsMeshYaw(float Yaw);

	/** Dev tool: fixed camera in front of the pawn for a face view — "PsFaceCam [dist] [aimZ]" */
	UFUNCTION(Exec)
	void PsFaceCam(float Dist = 0.0f, float AimZ = 0.0f);

	/** Dev tool (globe maps): re-center the georeference and teleport to
	 *  lat/lon, holding fly mode until streamed terrain collision is found —
	 *  "PsFlyToLL <lat> <lon> [altM] [pitch] [yaw]". altM<=0 uses a safe entry. */
	UFUNCTION(Exec)
	void PsFlyToLL(float Lat, float Lon, float AltM = 0.0f, float Pitch = 0.0f, float Yaw = 0.0f);

	/** Dev tool: force an ocean sea state 0=Calm 1=LightWind 2=Normal 3=Rough 4=Storm, -1 = weather */
	UFUNCTION(Exec)
	void PsSea(int32 State);

	/** Dev tool: return the pawn to normal walking after PsFlyTo */
	UFUNCTION(Exec)
	void PsWalk();

	/** Dev tool: sand debug — 0 off, 1 region+wake viz, 2 +stats, 3 stamp test, 4 dump state. */
	UFUNCTION(Exec)
	void PsSand(int32 Mode);

	/** Dev tool (globe maps): dump vegetation/land-cover state — "PsVeg" */
	UFUNCTION(Exec)
	void PsVeg();

	/** Dev tool: semantic classification under the pawn — NLCD pixel,
	 *  surface semantic, grass/tree/shrub semantics + resolved assets —
	 *  "PsSemGeo" */
	UFUNCTION(Exec)
	void PsSemGeo();

	/** Dev tool: dump the vegetation registry — every semantic + its bound
	 *  meshes — "PsVegLib" */
	UFUNCTION(Exec)
	void PsVegLib();

	/** Dev tool: semantic-cell debug spheres — "PsVegDebug 0|1" */
	UFUNCTION(Exec)
	void PsVegDebug(int32 Mode);

	/** Dev tool: runtime asset swap — "PsVegSwap <SemanticName> <MeshPath>"
	 *  rebinds the category and rebuilds live cells. Proves the registry is
	 *  the only art binding. */
	UFUNCTION(Exec)
	void PsVegSwap(const FString& Category, const FString& MeshPath);

	/** Dev tool: re-resolve all semantics from the library — "PsVegReset" */
	UFUNCTION(Exec)
	void PsVegReset();

	/** Dev tool (globe maps): resolved SURFACE under the pawn — semantic,
	 *  physical type, wetness, bound materials/profiles — "PsSurfGeo" */
	UFUNCTION(Exec)
	void PsSurfGeo();

	/** Dev tool: dump the surface registry — every semantic's phys type +
	 *  bound assets — "PsSurfLib" */
	UFUNCTION(Exec)
	void PsSurfLib();

	/** Dev tool: surface-semantic debug discs on the ground grid — "PsSurfDebug 0|1" */
	UFUNCTION(Exec)
	void PsSurfDebug(int32 Mode);

	/** Dev tool: runtime material swap — "PsSurfSwap <SemanticName> <MatPath>"
	 *  rebinds a semantic's detail material live; classification untouched. */
	UFUNCTION(Exec)
	void PsSurfSwap(const FString& Category, const FString& MatPath);

	/** Dev tool: restore all debug surface swaps — "PsSurfReset" */
	UFUNCTION(Exec)
	void PsSurfReset();

	/** Dev tool (globe maps): vertical/geodetic truth dump — pawn lat/lon,
	 *  ellipsoid h, MSL estimate (EGM96 geoid), world Z, traced ground height,
	 *  AGL, water datum — "PsGeo" */
	UFUNCTION(Exec)
	void PsGeo();

	/** Dev tool: spawn + equip paramotor, set throttle 0-1, crank engine — "PsEngine 0.8" */
	UFUNCTION(Exec)
	void PsEngine(float Throttle01);

	/** Dev tool: pilot helmet on/off — "PsHelmet 0|1" */
	UFUNCTION(Exec)
	void PsHelmet(int32 State);

	/** Dev tool: helmet visor up/down — "PsVisor 0|1" */
	UFUNCTION(Exec)
	void PsVisor(int32 State);

	/** Dev tool: pilot hair groom on/off — "PsHair 0|1" */
	UFUNCTION(Exec)
	void PsHair(int32 State);

	/** Dev tool (editor builds): dump body mesh verts + skin weights + ref
	 *  skeleton to C:/DevinTools/body_dump.json for the garment builder — "PsDumpBody" */
	UFUNCTION(Exec)
	void PsDumpBody();

	/** Dev tool (editor builds): dump an arbitrary skeletal mesh asset's
	 *  sections + weights + RefBasesInvMatrix to a json — "PsDumpMesh <path> <outfile>" */
	UFUNCTION(Exec)
	void PsDumpMesh(const FString& MeshPath, const FString& OutFile);

	/** Dev tool: garment debug — "PsGarment 0" hide, "1" normal leader-pose,
	 *  "2" drop leader pose so garments render at their own bind pose */
	UFUNCTION(Exec)
	void PsGarment(int32 State);

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
	/** Dev latch: >=0 pins the throttle for scripted wash testing (-1 = free). */
	float DevThrottleHold = -1.0f;
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

	// Pending geodetic teleport — PsFlyToLL sets it; Tick waits a few frames
	// for the georeference rebase to apply, then moves the pawn.
	bool bPendingLLTeleport = false;
	float LLTeleportTimer = 0.0f;
	double PendingLat = 0.0, PendingLon = 0.0;
	float PendingAltM = 0.0f, PendingPitch = 0.0f, PendingYaw = 0.0f;

	// Dev file-pipe: Tick polls C:\DevinTools\ps_cmd.txt; on any contents it
	// ConsoleCommands the text and deletes the file. Automation-safe input
	// path that doesn't depend on the game window holding OS focus.
	float CmdPollT = 0.0f;
};

