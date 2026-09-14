// Copyright Epic Games, Inc. All Rights Reserved.

#include "ParamotorGameMode.h"
#include "ParamotorLogging.h"
#include "Components/BoxComponent.h"
#include "ParamotorPlayerController.h"
#include "ParamotorCharacter.h"
#include "ParamotorGameInstance.h"
#include "ParamotorEnvironment.h"
#include "CesiumWorldHelpers.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"
#include "Landscape.h"
#include "ForestScatter.h"
#include "SandDebrisScatter.h"
#include "ParamotorSandSystem.h"
#include "RoadsActor.h"
#include "LandCoverService.h"
#include "VegetationField.h"
#include "SurfaceService.h"
#include "NewportBridge.h"
#include "Misc/Paths.h"
#include "GameFramework/PlayerStart.h"
#include "Engine/World.h"

AParamotorGameMode::AParamotorGameMode()
{
	PlayerControllerClass = AParamotorPlayerController::StaticClass();
	DefaultPawnClass = AParamotorCharacter::StaticClass();
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
}

void AParamotorGameMode::RequestGlobeSettle(float HoldZCm)
{
	GlobeHoldZ = HoldZCm;
	GlobeEntryZ = HoldZCm;
	GlobeHitStableT = 0.0f;
	GlobeWaitT = 0.0f;
	GlobeVerifyT = 0.0f;
	GlobeFloatChecks = 0;
	bAwaitingGlobeGround = true;
	GlobeSettleTimer = 0.0f;
}

void AParamotorGameMode::DetectGlobeMap()
{
	bGlobeMap = CesiumWorld::DetectGlobe(GetWorld());

	UParamotorGameInstance* GI = Cast<UParamotorGameInstance>(GetGameInstance());

	// L_Earth is a globe flight map — if no georeference exists in the level,
	// spawn one along with a tileset so the player can fly anywhere on Earth.
	if (!bGlobeMap && GetWorld())
	{
		FString MapName = GetWorld()->GetMapName();
		MapName.RemoveFromStart(GetWorld()->StreamingLevelsPrefix);
		if (MapName == TEXT("L_Earth"))
		{
			bGlobeMap = CesiumWorld::SetupGlobeTerrain(GetWorld(),
				GI ? GI->SelectedAirportLat : 41.35,
				GI ? GI->SelectedAirportLon : -71.80,
				GI ? (double)GI->SelectedAirportElev : 0.0);
		}
	}

	// A saved georeference points wherever it was authored — re-center on the
	// selected airport BEFORE any spawn math runs (this function is called
	// from FindPlayerStart/RestartPlayer ahead of BeginPlay). Gated at 50km
	// so a nearby origin isn't disturbed mid-load.
	if (bGlobeMap && GI)
	{
		CesiumWorld::SetGeoreferenceOrigin(GetWorld(),
			GI->SelectedAirportLon, GI->SelectedAirportLat, (double)GI->SelectedAirportElev);
	}

	UE_LOG(LogParamotor, Log, TEXT("GameMode: %s map detected"), bGlobeMap ? TEXT("Cesium globe") : TEXT("landscape/flat"));
}

void AParamotorGameMode::BeginPlay()
{
	Super::BeginPlay();

	// Detect a Cesium globe map (CesiumGeoreference present). Globe maps skip
	// the procedural field and spawn at the georeference origin instead of the
	// flat-earth launch-site offsets.
	DetectGlobeMap();
	if (bGlobeMap)
	{
		// Physics meshes on + culling off on every tileset (applied live —
		// overlays can't be retro-attached, they bind at tileset BeginPlay).
		CesiumWorld::EnsureFlightTileset(GetWorld());

		// Real-geography environment: NLCD land-cover classification streamer
		// (WMS + disk cache) + deterministic vegetation reconstruction +
		// semantic surface resolution (footsteps/prop-wash/detail patch).
		GetWorld()->SpawnActor<ALandCoverService>();
		GetWorld()->SpawnActor<AVegetationField>();
		GetWorld()->SpawnActor<ASurfaceService>();

		// Deformable sand — local pawn-following patch; on the globe its
		// activation is gated by surface semantics (beach cells only).
		GetWorld()->SpawnActor<AParamotorSandSystem>(
			FVector::ZeroVector, FRotator::ZeroRotator);

		// Hero landmarks — geodetically anchored structures PRT can't carry.
		// Deferred spawn so each actor gets its bridge spec before BeginPlay.
		const TArray<FBridgeSpec>& BridgeSpecs = ANewportBridge::GetSpecs();
		for (int32 i = 0; i < BridgeSpecs.Num(); ++i)
		{
			ANewportBridge* B = Cast<ANewportBridge>(UGameplayStatics::BeginDeferredActorSpawnFromClass(
				GetWorld(), ANewportBridge::StaticClass(), FTransform::Identity));
			if (!B) continue;
			B->SetSpecIndex(i);
			UGameplayStatics::FinishSpawningActor(B, FTransform::Identity);
		}

		// PRT leaves a melted shell of each bridge in the photogrammetry.
		// Clip a corridor polygon over every crossing so the real structure
		// stands alone — tiles fully inside stop loading, partial tiles get
		// the Cesium Clipping material mask.
		TArray<AActor*> ClipPolys;
		for (const FBridgeSpec& S : BridgeSpecs)
		{
			const double LatC = (S.LatA + S.LatB) * 0.5;
			const double LonC = (S.LonA + S.LonB) * 0.5;
			const double MPerDegLat = 111320.0;
			const double MPerDegLon = 111320.0 * FMath::Cos(FMath::DegreesToRadians(LatC));
			TArray<FVector> Pts;
			if (S.ClipPolyLonLat.Num() >= 3)
			{
				// Explicit water-hugging polygon for melt fields too big or
				// oddly shaped for the axis corridor (Mount Hope's debris
				// spans most of the bay).
				for (const FVector2D& LL : S.ClipPolyLonLat)
					Pts.Add(FVector(LL.X, LL.Y, 0.0));
			}
			else
			{
				const double AxLonM = (S.LonB - S.LonA) * MPerDegLon;
				const double AxLatM = (S.LatB - S.LatA) * MPerDegLat;
				const double AxisLen = FMath::Sqrt(AxLonM * AxLonM + AxLatM * AxLatM);
				if (AxisLen < 1.0) continue;
				const double Ux = AxLonM / AxisLen, Uy = AxLatM / AxisLen;
				const double Px = -Uy, Py = Ux;
				// Corridor width is per-spec — exclusion only drops tiles
				// that are ENTIRELY inside the polygon (the pixel Clipping
				// mask doesn't mask PRT fragments), so it must cover each
				// melt field. Pell's stays tight so the Newport waterfront
				// tiles survive.
				const double HL = S.ClipHalfLengthM > 0.0f ? S.ClipHalfLengthM
					: AxisLen * 0.5 + 160.0;
				const double HW = S.ClipHalfWidthM;
				const double Sxs[4] = {1.0, 1.0, -1.0, -1.0};
				const double Sys[4] = {1.0, -1.0, -1.0, 1.0};
				for (int32 k = 0; k < 4; ++k)
					Pts.Add(FVector(
						LonC + (Ux * Sxs[k] * HL + Px * Sys[k] * HW) / MPerDegLon,
						LatC + (Uy * Sxs[k] * HL + Py * Sys[k] * HW) / MPerDegLat, 0.0));
			}
			if (Pts.Num() >= 3)
			{
				TArray<FVector2D> LL;
				for (const FVector& V : Pts) LL.Add(FVector2D(V.X, V.Y));
				ClipZoneLonLat.Add(LL);
			}
			if (AActor* P = CesiumWorld::SpawnClipPolygon(GetWorld(), Pts))
				ClipPolys.Add(P);
		}

		// Non-bridge PRT melt fields — corrupted photogrammetry domes over
		// tidal water, invisible from altitude but enclosing low fliers in
		// km-scale shards. Water-bounded tiles, so hug the shorelines:
		// excluding them also strips their over-land overhang while
		// land-bounded tiles (village, forest) survive.
		static const TArray<TArray<FVector2D>> MeltZones = {
			{ // Pawcatuck River mouth / Little Narragansett Bay (RI-CT line)
				{-71.880,41.352}, {-71.888,41.368}, {-71.875,41.383},
				{-71.858,41.390}, {-71.845,41.385}, {-71.840,41.372},
				{-71.845,41.358}, {-71.862,41.350},
			},
		};
		for (const TArray<FVector2D>& Zone : MeltZones)
		{
			ClipZoneLonLat.Add(Zone);
			TArray<FVector> Pts;
			for (const FVector2D& LL : Zone) Pts.Add(FVector(LL.X, LL.Y, 0.0));
			if (AActor* P = CesiumWorld::SpawnClipPolygon(GetWorld(), Pts))
				ClipPolys.Add(P);
		}
		ClipOverlay = CesiumWorld::AddPolygonClipper(GetWorld(), ClipPolys);
		ClipRetriesLeft = 10;   // native tileset may not exist yet — retry in Tick
		ClipRetryTimer = 0.0f;
	}

	// Spawn the environment actor — owns sky/sun/fog/post-process/water/audio
	// and its own tick for time-of-day sun updates.
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	GetWorld()->SpawnActor<AParamotorEnvironment>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);

	if (!bGlobeMap)
	{
		SetupCoastalTerrain();

		// Runtime forest scatter — OSM woodland grid, real positions,
		// placeholder meshes until the real foliage pack lands.
		if (AForestScatter* Forest = GetWorld()->SpawnActor<AForestScatter>(
				FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams))
		{
			Forest->LoadGrid(FPaths::ProjectContentDir() / TEXT("Data/forests/ninigret.bin"),
				269580.0, 4580015.0);
		}

		// Beach micro-debris — pebbles/shell bits on the sand band.
		GetWorld()->SpawnActor<ASandDebrisScatter>(
			FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);

		// Interactive sand — deformable overlay + footprints + prop wash.
		GetWorld()->SpawnActor<AParamotorSandSystem>(
			FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);

		// Real road network — built from OSM geometry at runtime.
		GetWorld()->SpawnActor<ARoadsActor>(
			FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
	}

	// Reposition PlayerStart to the launch field — also handled in RestartPlayer
	{
		TArray<AActor*> PlayerStarts;
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), APlayerStart::StaticClass(), PlayerStarts);
		FVector FieldLoc = GetLaunchFieldSpawnLocation();
		for (AActor* PS : PlayerStarts)
		{
			PS->SetActorLocation(FieldLoc);
			UE_LOG(LogParamotor, Log, TEXT("GameMode: PlayerStart pre-positioned at launch site %s"), *FieldLoc.ToString());
		}
		if (PlayerStarts.Num() == 0)
			UE_LOG(LogParamotor, Log, TEXT("GameMode: No PlayerStart found!"));
	}
}

void AParamotorGameMode::SetupCoastalTerrain()
{
	// If the level already has a real Landscape (L_Westerly uses real 1m LiDAR terrain),
	// skip the procedural field entirely.
	{
		TArray<AActor*> Existing;
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), ALandscapeProxy::StaticClass(), Existing);
		if (Existing.Num() > 0)
		{
			UE_LOG(LogParamotor, Log, TEXT("GameMode: Real landscape found (%d actors) — skipping procedural field"), Existing.Num());
			return;
		}
	}

	// Delete old template landscape actors
	TArray<AActor*> Landscapes;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), ALandscapeProxy::StaticClass(), Landscapes);
	for (AActor* L : Landscapes)
	{
		L->Destroy();
	}
	if (Landscapes.Num() > 0)
		UE_LOG(LogParamotor, Log, TEXT("GameMode: Removed %d old landscape actors"), Landscapes.Num());

	// Spawn a real AStaticMeshActor for the airport/field so we get reliable collision and rendering
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	TerrainActor = GetWorld()->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), FVector(0.0f, 0.0f, 500.0f), FRotator(0.0f, 0.0f, 0.0f), SpawnParams);
	if (TerrainActor)
	{
#if WITH_EDITOR
		TerrainActor->SetActorLabel(TEXT("WesterlyAirportField"));
#endif
		TerrainActor->SetActorEnableCollision(true);
		UStaticMesh* PlaneMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
		if (PlaneMesh)
		{
			AStaticMeshActor* SMActor = Cast<AStaticMeshActor>(TerrainActor);
			UStaticMeshComponent* MeshComp = SMActor->GetStaticMeshComponent();
			MeshComp->SetStaticMesh(PlaneMesh);
			MeshComp->SetWorldScale3D(FVector(2000.0f, 2000.0f, 1.0f));  // 2km x 2km
			MeshComp->SetMobility(EComponentMobility::Movable);
			MeshComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			MeshComp->SetCollisionProfileName(TEXT("BlockAll"));
			UMaterialInterface* LandMat = UMaterial::GetDefaultMaterial(MD_Surface);
			if (LandMat)
			{
				MeshComp->SetMaterial(0, LandMat);
				MeshComp->MarkRenderStateDirty();
			}
			MeshComp->RecreatePhysicsState();
			UE_LOG(LogParamotor, Log, TEXT("GameMode: Westerly Airport field spawned at Z=500cm, 2km x 2km"));

			// Add a large flat collision box since the Plane mesh may have no usable collision
			UBoxComponent* Box = NewObject<UBoxComponent>(TerrainActor, TEXT("FieldCollision"));
			Box->SetupAttachment(MeshComp);
			Box->SetBoxExtent(FVector(200000.0f, 200000.0f, 10.0f));  // 2000m x 2000m x 20cm
			Box->SetRelativeLocation(FVector(0.0f, 0.0f, 0.0f));
			Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			Box->SetCollisionProfileName(TEXT("BlockAll"));
			Box->CanCharacterStepUpOn = ECB_Yes;
			Box->RegisterComponent();
			Box->RecreatePhysicsState();
			UE_LOG(LogParamotor, Log, TEXT("GameMode: Westerly Airport collision floor added"));
		}
		else
		{
			UE_LOG(LogParamotor, Error, TEXT("GameMode: Failed to load Plane mesh for field!"));
		}
	}
}

void AParamotorGameMode::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// The polygon clipper no-ops if the tileset's native Tileset object isn't
	// created yet — Refresh-retried here for the first ~20 s of the session.
	if (ClipOverlay && ClipRetriesLeft > 0)
	{
		ClipRetryTimer += DeltaTime;
		if (ClipRetryTimer >= 2.0f)
		{
			ClipRetryTimer = 0.0f;
			--ClipRetriesLeft;
			if (UFunction* Refresh = ClipOverlay->FindFunction(TEXT("Refresh")))
				ClipOverlay->ProcessEvent(Refresh, nullptr);
		}
	}

	// Globe spawn settle: Cesium tiles stream in asynchronously, so the spawn
	// trace can come back empty. Keep the pawn in flying mode and trace down
	// until we find real ground, then place it and switch to walking.
	if (bGlobeMap && bAwaitingGlobeGround)
	{
		GlobeSettleTimer += DeltaTime;
		GlobeWaitT += DeltaTime;
		if (GlobeSettleTimer >= 0.2f)
		{
			GlobeSettleTimer = 0.0f;
			APlayerController* PC = GetWorld()->GetFirstPlayerController();
			APawn* P = PC ? PC->GetPawn() : nullptr;
			if (P)
			{
				// Keep the pawn frozen in the air so it can't fall while tiles stream in.
				if (ACharacter* C = Cast<ACharacter>(P))
				{
					if (C->GetCharacterMovement()->MovementMode != MOVE_Flying)
					{
						C->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
					}
					C->GetCharacterMovement()->Velocity = FVector::ZeroVector;
				}

				FVector PawnLoc = P->GetActorLocation();
				FHitResult Hit;
				FCollisionQueryParams Params;
				Params.bTraceComplex = true;
				Params.AddIgnoredActor(P);
				// Always probe from the ENTRY altitude, not the pawn's current Z:
				// the pawn may have descended below the surface while waiting for
				// collision, and a ray from inside the terrain never finds its top.
				FVector TraceStart(PawnLoc.X, PawnLoc.Y, GlobeEntryZ + 1000.0f);
				FVector TraceEnd(PawnLoc.X, PawnLoc.Y, GlobeEntryZ - 3000000.0f);
				bool bPlaced = false;
				const FVector PawnGeo = CesiumWorld::UnrealToGeodetic(GetWorld(), PawnLoc);
				if (GetWorld()->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Visibility, Params))
				{
					// Reject impossible surfaces — deep-void hits (stale geometry,
					// helper volumes) are not terrain. CONUS ground is between
					// -150 m and 6000 m geodetic.
					FVector HitGeo = CesiumWorld::UnrealToGeodetic(GetWorld(), Hit.ImpactPoint);
					if (HitGeo.Z >= -150.0 && HitGeo.Z <= 6000.0)
					{
						// Is the pawn's geodetic position over open water? The
						// water disc is only a valid floor there — over land it's
						// a phantom sheet that beats unstreamed terrain to the
						// trace and would drop the pawn under the world.
						bool bOpenWater = false, bLandCoverKnown = false;
						{
							TArray<AActor*> LC;
							UGameplayStatics::GetAllActorsOfClass(GetWorld(), ALandCoverService::StaticClass(), LC);
							if (LC.Num() > 0)
							{
								FLandCoverPixel Px;
								if (Cast<ALandCoverService>(LC[0])->GetPixelAt(PawnGeo.Y, PawnGeo.X, Px))
								{
									bLandCoverKnown = true;
									bOpenWater = (Px.Cover == (uint8)ELandCoverClass::OpenWater);
								}
							}
							else { bOpenWater = true; bLandCoverKnown = true; } // no classifier — bias to accepting the sheet
						}

						// Peel phantom shells: with culling disabled, coarse
						// ancestor tiles can leave collision surfaces floating far
						// above refined ground. If another sane surface lies >75 m
						// below, the upper layer was a shell — take the deeper one.
						// Only Cesium hits can be shells — a hit on any other actor
						// (the NewportBridge deck, a building) is a real structure
						// even when it towers >75 m over the terrain below it.
						// The ocean disc over land is treated as a shell too:
						// peel straight through it to whatever is really below.
						for (int32 Layer = 0; Layer < 8; ++Layer)
						{
							bool bCesium = false, bOcean = false;
							if (AActor* HA = Hit.GetActor())
							{
								const FString CN = HA->GetClass()->GetName();
								bCesium = CN.Contains(TEXT("Cesium"));
								bOcean  = CN.Contains(TEXT("Ocean"));
							}
							if (bOcean) { if (bOpenWater) break; }  // sea floor = the sheet
							else if (!bCesium) break;               // real structure — done
							FHitResult Sub;
							const FVector S(Hit.ImpactPoint.X, Hit.ImpactPoint.Y, Hit.ImpactPoint.Z - 150.0f);
							const FVector E(S.X, S.Y, S.Z - 500000.0f);
							if (!GetWorld()->LineTraceSingleByChannel(Sub, S, E, ECC_Visibility, Params)) break;
							// Skip interior faces: shells are closed hulls whose
							// underside reports normal -Z — without this the peel
							// stalls on the shell's own floor and never reaches
							// the real surface below.
							for (int32 G = 0; G < 8 && Sub.ImpactNormal.Z < 0.2f; ++G)
							{
								const FVector S3(Sub.ImpactPoint.X, Sub.ImpactPoint.Y, Sub.ImpactPoint.Z - 150.0f);
								if (!GetWorld()->LineTraceSingleByChannel(Sub, S3, E, ECC_Visibility, Params)) break;
							}
							if (Sub.ImpactNormal.Z < 0.2f) break; // ran out of guard
							const FVector SubGeo = CesiumWorld::UnrealToGeodetic(GetWorld(), Sub.ImpactPoint);
							if (SubGeo.Z < -150.0 || SubGeo.Z > 6000.0) break;
							bool bSubCesium = false, bSubOcean = false;
							if (AActor* SA = Sub.GetActor())
							{
								const FString SN = SA->GetClass()->GetName();
								bSubCesium = SN.Contains(TEXT("Cesium"));
								bSubOcean  = SN.Contains(TEXT("Ocean"));
							}
							// The disc over land is never a floor — but only when
							// it was the TOPMOST surface (Layer==0 = nothing above
							// it). Found deeper, it just means "sea level" and the
							// Cesium surface above it (real terrain) must be kept.
							if (bSubOcean && !bOpenWater)
							{
								if (bOcean) { Hit = Sub; continue; } // stacked disc — keep peeling
								break;                               // disc below terrain — keep terrain
							}
							if (bSubOcean && bOpenWater) { Hit = Sub; break; }
							if (bOcean && !bOpenWater) { Hit = Sub; continue; } // topmost disc over land
							if (!bSubCesium) { Hit = Sub; break; }   // real structure below
							if (Hit.ImpactPoint.Z - Sub.ImpactPoint.Z < 7500.0f) break;
							Hit = Sub;
						}
						HitGeo = CesiumWorld::UnrealToGeodetic(GetWorld(), Hit.ImpactPoint);
						// Coarse tiles report phantom surfaces high above real
						// terrain; as tiles refine the hit height keeps dropping.
						// Only place once the reported height has been stable for
						// ~8 s AND the pawn has reached (or overshot) it.
						if (FMath::Abs(HitGeo.Z - GlobeLastHitM) > 5.0)
						{
							GlobeLastHitM = HitGeo.Z;
							GlobeHitStableT = 0.0f;
						}
						else GlobeHitStableT += 0.2f;

						// The water sheet as the final surface is only a valid
						// floor over confirmed open water. Over land it wins the
						// trace merely because the real terrain above it hasn't
						// streamed yet — placing there buries the pawn under the
						// world. Hold the settle instead: cooked terrain takes
						// the topmost trace on its own. Escape valves: 45s with
						// no land-cover data accepts the sheet (offshore beyond
						// NLCD coverage); 150s is the hard last resort.
						bool bFinalOcean = false;
						if (AActor* FA = Hit.GetActor())
							bFinalOcean = FA->GetClass()->GetName().Contains(TEXT("Ocean"));
						// Inside a clip polygon the tiles are excluded for
						// good — no terrain is coming, so the water sheet
						// IS the floor regardless of land-cover class.
						const bool bInsideClip = InsideClipZone(PawnGeo.Y, PawnGeo.X);
						const bool bDiscGate = !bFinalOcean || bOpenWater || bInsideClip
							|| (!bLandCoverKnown && GlobeWaitT > 45.0f) || GlobeWaitT > 150.0f;

						if (bDiscGate && ((GlobeHitStableT >= 12.0f && PawnGeo.Z <= HitGeo.Z + 350.0) || GlobeWaitT > 90.0f))
						{
							float GroundZ = Hit.ImpactPoint.Z;
							P->SetActorLocation(FVector(PawnLoc.X, PawnLoc.Y, GroundZ + 150.0f), false, nullptr, ETeleportType::TeleportPhysics);
							if (ACharacter* C = Cast<ACharacter>(P))
							{
								C->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
								C->GetCharacterMovement()->Velocity = FVector::ZeroVector;
								// PsFlyToLL zeroed gravity for the hop — restore it or
								// the pawn hangs wherever it was placed, floor never
								// registers, and the phantom-shell watchdog can't see it.
								C->GetCharacterMovement()->GravityScale = 1.0f;
							}
							bAwaitingGlobeGround = false;
							bPlaced = true;
							GlobeVerifyT = 120.0f; // watchdog: phantom shells can outlive placement
							GlobeVerifyTick = 0.0f;
							GlobeFloatChecks = 0;
							UE_LOG(LogParamotor, Log, TEXT("GameMode: Globe ground found at Z=%.0fcm (geo-h %.0fm) — pawn placed"), GroundZ, HitGeo.Z);
						}
					}
					else
					{
						UE_LOG(LogParamotor, Log, TEXT("GameMode: rejected phantom ground hit at geo-h=%.0fm actor=%s"),
							HitGeo.Z, Hit.GetActor() ? *Hit.GetActor()->GetName() : TEXT("null"));
					}
				}
				if (!bPlaced)
				{
					// No collision yet — descend progressively. Holding at a fixed
					// altitude high above terrain leaves the pawn staring at
					// coarse-LOD tiles that may never get collision cooked;
					// sinking forces the tileset to refine under the pawn.
					// Geodetic floor: never sink below -200 m MSL (lowest CONUS
					// surface is ~-86 m); the trace-from-entry still finds the
					// surface and teleports the pawn back up onto it.
					if (PawnGeo.Z > -200.0)
						GlobeHoldZ -= 3000.0f; // 150 m/s at 0.2 s cadence
					if (FMath::Abs(PawnLoc.Z - GlobeHoldZ) > 500.0f)
						P->SetActorLocation(FVector(PawnLoc.X, PawnLoc.Y, GlobeHoldZ), false, nullptr, ETeleportType::TeleportPhysics);
					UE_LOG(LogParamotor, Log, TEXT("GameMode: Waiting for globe tiles — descending to Z=%.0f"), GlobeHoldZ);
				}
			}
		}
	}

	// Post-settle watchdog: coarse streamed tiles can leave a collision shell
	// parked high above real terrain — the pawn lands on it before fine LOD
	// cooks, then the shell never unloads. Probe BELOW the pawn's feet: if the
	// first sane surface is >60 m down (air under the plate = shell signature;
	// solid rock/buildings would hit within meters) AND the deepest sane
	// surface is >400 m down (beyond any real structure), the pawn is floating
	// on a phantom — snap it to real ground.
	if (bGlobeMap && !bAwaitingGlobeGround)
	{
		if (GlobeVerifyT > 0.0f) GlobeVerifyT -= DeltaTime;
		GlobeVerifyTick += DeltaTime;
		if (GlobeVerifyTick >= 0.5f)
		{
			GlobeVerifyTick = 0.0f;
			APlayerController* PC = GetWorld()->GetFirstPlayerController();
			APawn* P = PC ? PC->GetPawn() : nullptr;
			ACharacter* C = Cast<ACharacter>(P);
			const EMovementMode MM = C ? (EMovementMode)C->GetCharacterMovement()->MovementMode.GetValue() : MOVE_None;
			if (P && C && (MM == MOVE_Walking || MM == MOVE_Falling))
			{
				const FVector PL = P->GetActorLocation();
				const FFindFloorResult& Floor = C->GetCharacterMovement()->CurrentFloor;
				if (!Floor.bBlockingHit)
				{
					GlobeFloatChecks = 0;
					// Floor vanished under a walking pawn — it will otherwise fall
					// through the world forever. Re-enter the settle loop from a
					// sane altitude at the same lat/lon (the probe must start
					// ABOVE the surface, so clamp geodetic height to >=120 m).
					NoFloorT += 0.5f;
					if (NoFloorT > 3.0f)
					{
						NoFloorT = 0.0f;
						const FVector Geo = CesiumWorld::UnrealToGeodetic(GetWorld(), PL);
						const double ReZ = FMath::Max(Geo.Z, 120.0);
						const FVector Re = CesiumWorld::GeodeticToUnreal(GetWorld(), Geo.X, Geo.Y, ReZ);
						P->TeleportTo(Re, P->GetActorRotation());
						RequestGlobeSettle(Re.Z);
						UE_LOG(LogParamotor, Warning, TEXT("GameMode: pawn floorless — re-settling at geo-h %.0fm"), ReZ);
					}
				}
				else if (GlobeVerifyT > 0.0f)
				{
					FCollisionQueryParams VP;
					VP.bTraceComplex = true;
					VP.AddIgnoredActor(P);
					// Cascade below the SUPPORT surface (not the pawn — the pawn
					// may be a capsule-height above it) to the deepest sane layer.
					const float FloorZ = Floor.HitResult.ImpactPoint.Z;
					float DeepZ = 0.0f;
					bool bDeep = false;
					FVector S(PL.X, PL.Y, FloorZ - 150.0f);
					for (int32 L = 0; L < 8; ++L)
					{
						FHitResult VH;
						if (!GetWorld()->LineTraceSingleByChannel(VH, S, S - FVector(0, 0, 500000.0f), ECC_Visibility, VP)) break;
						// Skip interior faces (closed-hull undersides, normal -Z)
						for (int32 G = 0; G < 8 && VH.ImpactNormal.Z < 0.2f; ++G)
						{
							S.Z = VH.ImpactPoint.Z - 150.0f;
							if (!GetWorld()->LineTraceSingleByChannel(VH, S, S - FVector(0, 0, 500000.0f), ECC_Visibility, VP)) break;
						}
						if (VH.ImpactNormal.Z < 0.2f) break;
						const FVector G = CesiumWorld::UnrealToGeodetic(GetWorld(), VH.ImpactPoint);
						S.Z = VH.ImpactPoint.Z - 150.0f;
						if (G.Z < -150.0 || G.Z > 6000.0) continue; // skip void plates
						DeepZ = VH.ImpactPoint.Z;
						bDeep = true;
					}
					if (bDeep && (FloorZ - DeepZ) > 40000.0f)
				{
					if (++GlobeFloatChecks >= 3)
					{
						GlobeFloatChecks = 0;
						P->SetActorLocation(FVector(PL.X, PL.Y, DeepZ + 150.0f), false, nullptr, ETeleportType::TeleportPhysics);
						C->GetCharacterMovement()->Velocity = FVector::ZeroVector;
						C->GetCharacterMovement()->GravityScale = 1.0f;
						UE_LOG(LogParamotor, Log, TEXT("GameMode: pawn floated %.0fm on a phantom shell — snapped to real ground Z=%.0f"),
							(PL.Z - DeepZ) / 100.0f, DeepZ);
					}
				}
				else GlobeFloatChecks = 0;
			}
		}
	}
	}
}

FVector AParamotorGameMode::GetLaunchFieldSpawnLocation()
{
	// On globe maps, use the selected airport's lat/lon to compute spawn position.
	if (bGlobeMap)
	{
		UParamotorGameInstance* GI = Cast<UParamotorGameInstance>(GetGameInstance());
		if (GI && GI->bAirportSelected)
		{
			FVector SpawnPos = CesiumWorld::GeodeticToUnreal(GetWorld(), GI->SelectedAirportLon, GI->SelectedAirportLat, (double)GI->SelectedAirportElev);
			bAwaitingGlobeGround = true;
			GlobeHoldZ = GlobeEntryZ = (float)(SpawnPos.Z + 5000.0);
			UE_LOG(LogParamotor, Log, TEXT("GameMode: globe spawn at %s (lat %.4f, lon %.4f)"),
				*SpawnPos.ToString(), GI->SelectedAirportLat, GI->SelectedAirportLon);
			return SpawnPos + FVector(0.0f, 0.0f, 5000.0f); // 50m above, settle loop finds ground
		}
		// Fallback: georeference origin
		bAwaitingGlobeGround = true;
		return FVector(0.0f, 0.0f, 5000.0f);
	}

	// Flat-earth map: spawn at the active launch site (curated site table —
	// LocalSpawn is precomputed world-cm vs the map origin).
	float SpawnX = 0.0f;
	float SpawnY = 0.0f;
	if (UParamotorGameInstance* GI = Cast<UParamotorGameInstance>(GetGameInstance()))
	{
		GI->EnsureSiteData();
		// Honor the selected airport on direct map loads too — the menu path
		// sets this via OnFlyClicked, but a straight L_Westerly launch doesn't.
		GI->SelectSiteByICAO(GI->SelectedAirportICAO);
		if (const FLaunchSite* Site = GI->GetActiveSite())
		{
			SpawnX = (float)Site->LocalSpawn.X;
			SpawnY = (float)Site->LocalSpawn.Y;
		}
	}

	// Drop a line trace to sit exactly on the landscape surface — multi-hit so we
	// can skip non-terrain blockers (ocean collision volumes, trees, etc.)
	FVector Start(SpawnX, SpawnY, 10000.0f);
	FVector End(SpawnX, SpawnY, -10000.0f);
	TArray<FHitResult> Hits;
	FCollisionQueryParams Params;
	Params.bTraceComplex = true;
	float TerrainZ = 1500.0f;  // fallback ~15m
	bool bFound = false;
	TArray<const AActor*> Ignored;
	for (int32 Iter = 0; Iter < 8 && !bFound; ++Iter)
	{
		Params.ClearIgnoredActors();
		Params.AddIgnoredActors(Ignored);
		Hits.Reset();
		if (!GetWorld()->LineTraceMultiByChannel(Hits, Start, End, ECC_Visibility, Params))
		{
			break;
		}
		for (const FHitResult& H : Hits)
		{
			AActor* HA = H.GetActor();
			if (Cast<ALandscapeProxy>(HA) != nullptr)
			{
				TerrainZ = H.ImpactPoint.Z;
				bFound = true;
				UE_LOG(LogParamotor, Log, TEXT("GameMode: Spawn trace hit ground (%s) at Z=%.0fcm"),
					*HA->GetName(), TerrainZ);
				break;
			}
			UE_LOG(LogParamotor, Log, TEXT("GameMode: Spawn trace skipped %s at Z=%.0fcm"),
				HA ? *HA->GetName() : TEXT("null"), H.ImpactPoint.Z);
			if (HA) Ignored.AddUnique(HA);
		}
	}
	if (!bFound)
	{
		UE_LOG(LogParamotor, Log, TEXT("GameMode: Spawn trace found no ground at (%.0f, %.0f)"), SpawnX, SpawnY);
	}
	return FVector(SpawnX, SpawnY, TerrainZ + 200.0f);
}

AActor* AParamotorGameMode::FindPlayerStart_Implementation(AController* Player, const FString& IncomingName)
{
	// On the real-terrain or globe map, guarantee a PlayerStart exists at the
	// launch field BEFORE the engine picks a spawn point (prevents spawning at
	// world origin).
	DetectGlobeMap();

	TArray<AActor*> Existing;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), ALandscapeProxy::StaticClass(), Existing);
	if (Existing.Num() > 0 || bGlobeMap)
	{
		TArray<AActor*> PlayerStarts;
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), APlayerStart::StaticClass(), PlayerStarts);
		FVector FieldLoc = GetLaunchFieldSpawnLocation();
		if (PlayerStarts.Num() == 0)
		{
			FActorSpawnParameters SP;
			SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			APlayerStart* NewStart = GetWorld()->SpawnActor<APlayerStart>(FieldLoc, FRotator::ZeroRotator, SP);
			if (NewStart)
			{
				UE_LOG(LogParamotor, Log, TEXT("GameMode: FindPlayerStart created PlayerStart at launch site %s"), *FieldLoc.ToString());
				return NewStart;
			}
		}
		else
		{
			PlayerStarts[0]->SetActorLocation(FieldLoc);
			return PlayerStarts[0];
		}
	}
	return Super::FindPlayerStart_Implementation(Player, IncomingName);
}

void AParamotorGameMode::RestartPlayer(AController* NewPlayer)
{
	if (!IsValid(NewPlayer))
	{
		return;
	}

	// RestartPlayer can be called before BeginPlay on game startup, so make
	// sure we know whether this is a Cesium globe map before picking spawn coords.
	DetectGlobeMap();

	// Move any existing PlayerStart to the launch field before the player spawns
	TArray<AActor*> PlayerStarts;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), APlayerStart::StaticClass(), PlayerStarts);
	FVector FieldLoc = GetLaunchFieldSpawnLocation();
	if (PlayerStarts.Num() == 0)
	{
		// No PlayerStart in the map — spawn one at the launch field so the player
		// doesn't spawn at the world origin.
		FActorSpawnParameters SP;
		SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		APlayerStart* NewStart = GetWorld()->SpawnActor<APlayerStart>(FieldLoc, FRotator::ZeroRotator, SP);
		if (NewStart)
		{
			PlayerStarts.Add(NewStart);
			UE_LOG(LogParamotor, Log, TEXT("GameMode: Created PlayerStart at launch site %s"), *FieldLoc.ToString());
		}
	}
	for (AActor* PS : PlayerStarts)
	{
		PS->SetActorLocation(FieldLoc);
		UE_LOG(LogParamotor, Log, TEXT("GameMode: PlayerStart set to launch site %s"), *FieldLoc.ToString());
	}

	// Fall back to default restart logic, which will now use the moved PlayerStart
	Super::RestartPlayer(NewPlayer);

	// The engine's collision-adjust can fling the pawn far away if the spawn point
	// overlaps a volume (e.g. water collision). Verify and force the location.
	if (APawn* P = NewPlayer->GetPawn())
	{
		FVector Loc = P->GetActorLocation();
		if (FVector::Dist2D(Loc, FieldLoc) > 500.0f || FMath::Abs(Loc.Z - FieldLoc.Z) > 500.0f)
		{
			P->SetActorLocation(FieldLoc, false, nullptr, ETeleportType::TeleportPhysics);
			UE_LOG(LogParamotor, Log, TEXT("GameMode: pawn was at %s — teleported to launch field"), *Loc.ToString());
		}
		else
		{
			UE_LOG(LogParamotor, Log, TEXT("GameMode: pawn spawned at %s"), *Loc.ToString());
		}

		// On the globe map, keep the pawn in flying mode until the settle loop
		// finds the real ground. This prevents the character from falling far
		// before Cesium tiles have streamed in and their collision is ready.
		if (bGlobeMap)
		{
			// Anchor to the globe so the pawn tracks georeference origin rebasing.
			CesiumWorld::AnchorToGlobe(GetWorld(), P);

			if (bAwaitingGlobeGround)
			{
				if (ACharacter* C = Cast<ACharacter>(P))
				{
					C->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
					C->GetCharacterMovement()->Velocity = FVector::ZeroVector;
				}
			}
		}
	}
}

bool AParamotorGameMode::InsideClipZone(double Lat, double Lon) const
{
	for (const TArray<FVector2D>& Zone : ClipZoneLonLat)
	{
		bool bInside = false;
		const int32 N = Zone.Num();
		for (int32 i = 0, j = N - 1; i < N; j = i++)
		{
			const double Xi = Zone[i].X, Yi = Zone[i].Y;
			const double Xj = Zone[j].X, Yj = Zone[j].Y;
			if (((Yi > Lat) != (Yj > Lat)) &&
				(Lon < (Xj - Xi) * (Lat - Yi) / (Yj - Yi) + Xi))
				bInside = !bInside;
		}
		if (bInside) return true;
	}
	return false;
}
