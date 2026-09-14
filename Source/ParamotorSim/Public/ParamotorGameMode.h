// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "ParamotorGameMode.generated.h"

class AStaticMeshActor;

UCLASS()
class PARAMOTORSIM_API AParamotorGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AParamotorGameMode();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

public:
	/** Hold the pawn at HoldZ in fly mode until a downward trace finds streamed
	 *  terrain, then place it and restore walking. Used by globe spawn and the
	 *  PsFlyToLL dev teleport. */
	void RequestGlobeSettle(float HoldZCm = 5000.0f);
	bool IsAwaitingGlobeGround() const { return bAwaitingGlobeGround; }

protected:
	// Fallback flat field for maps without a real landscape
	void SetupCoastalTerrain();

	// True when the map uses a Cesium globe (CesiumGeoreference present).
	// Set in BeginPlay — skips the procedural field and flat-earth spawn offsets.
	bool bGlobeMap = false;

	// Globe spawn settle: while true, Tick re-traces the ground and drops the
	// pawn onto streamed terrain once collision tiles exist.
	bool bAwaitingGlobeGround = false;
	float GlobeSettleTimer = 0.0f;
	float GlobeHoldZ = 5000.0f;
	float GlobeEntryZ = 5000.0f;  // settle-entry altitude — ground probe traces from here
	double GlobeLastHitM = 0.0;   // geodetic height of last valid ground probe (m)
	float GlobeHitStableT = 0.0f; // seconds the reported ground height has been stable
	float GlobeWaitT = 0.0f;      // total seconds spent waiting for ground
	float GlobeVerifyT = 0.0f;    // post-settle watchdog window (s); 0 = off
	float GlobeVerifyTick = 0.0f; // watchdog trace cadence accumulator
	int32 GlobeFloatChecks = 0;   // consecutive "pawn floating on phantom shell" hits
	float NoFloorT = 0.0f;        // seconds a walking pawn has had no floor — re-settle past 3 s

	void DetectGlobeMap();

	FVector GetLaunchFieldSpawnLocation();

	// Overridden to spawn players at the launch field / Ninigret Park
	virtual void RestartPlayer(AController* NewPlayer) override;
	virtual AActor* FindPlayerStart_Implementation(AController* Player, const FString& IncomingName) override;

	UPROPERTY()
	AStaticMeshActor* TerrainActor = nullptr;

	// PRT landmark-clip overlay — created in BeginPlay; Refresh-retried in
	// Tick until the native tileset exists to receive it.
	UPROPERTY()
	UActorComponent* ClipOverlay = nullptr;
	int32 ClipRetriesLeft = 0;
	float ClipRetryTimer = 0.0f;

	// Every clip polygon as lon/lat vertex lists (X=lon, Y=lat). Inside one,
	// terrain never loads — the settle loop may accept the water sheet as
	// the floor immediately instead of waiting out the land-cover timeouts.
	TArray<TArray<FVector2D>> ClipZoneLonLat;
	bool InsideClipZone(double Lat, double Lon) const;
};
