// Copyright Epic Games, Inc. All Rights Reserved.

#include "ParamotorPlayerController.h"
#include "ParamotorLogging.h"
#include "UW_PauseMenu.h"
#include "UW_FlightHUD.h"
#include "ParamotorActor.h"
#include "WingActor.h"
#include "ChargerWingActor.h"
#include "CesiumWorldHelpers.h"
#include "ParamotorCharacter.h"
#include "ParamotorGameInstance.h"
#include "ParamotorSaveGame.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Camera/CameraActor.h"
#include "Blueprint/UserWidget.h"
#include "Kismet/GameplayStatics.h"
#include "ParamotorOceanSystem.h"
#include "NewportBridge.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "ParamotorSandSystem.h"
#include "VegetationField.h"
#include "SurfaceService.h"
#include "AudioDevice.h"
#include "Misc/FileHelper.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"

// ==== EQUIPMENT / TEST SPAWNING ====

void AParamotorPlayerController::SpawnParamotor()
{
	APawn* MyPawn = GetPawn();
	if (!MyPawn) return;

	// Kill any previously spawned paramotor — no stale builds mixing in.
	if (SpawnedParamotorActor)
	{
		SpawnedActors.Remove(SpawnedParamotorActor);
		SpawnedParamotorActor->Destroy();
		SpawnedParamotorActor = nullptr;
	}

	FVector Forward = MyPawn->GetActorForwardVector();
	FVector Start = MyPawn->GetActorLocation() + Forward * 200.0f;
	float SpawnZ = FindGroundZ(Start, MyPawn);
	FVector SpawnLoc = FVector(Start.X, Start.Y, SpawnZ);
	FRotator SpawnRot = MyPawn->GetActorRotation();

	// Deferred spawn — bUseCodexBuild + build config must be set before
	// BeginPlay runs BuildParamotorMeshes.
	FTransform SpawnTransform(SpawnRot, SpawnLoc);
	SpawnedParamotorActor = GetWorld()->SpawnActorDeferred<AParamotorActor>(
		AParamotorActor::StaticClass(), SpawnTransform, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (SpawnedParamotorActor)
	{
		if (UParamotorGameInstance* GI = Cast<UParamotorGameInstance>(GetGameInstance()))
		{
			GI->EnsureSpecData();
			// Restore the saved hangar build if this session never visited it.
			if (GI->CurrentBuild.FrameIndex < 0)
			{
				if (UParamotorSaveGame* Save = Cast<UParamotorSaveGame>(
						UGameplayStatics::LoadGameFromSlot(TEXT("ParamotorBuildSlot"), 0)))
				{
					GI->CurrentBuild = Save->CurrentBuild;
				}
			}
			// No configured/saved build → default to the first part in each
			// category (the Codex pack is the stock rig).
			FParamotorBuild& B = GI->CurrentBuild;
			if (B.FrameIndex   < 0 && GI->FrameSpecs.Num()   > 0) B.FrameIndex   = 0;
			if (B.EngineIndex  < 0 && GI->EngineSpecs.Num()  > 0) B.EngineIndex  = 0;
			if (B.PropIndex    < 0 && GI->PropSpecs.Num()    > 0) B.PropIndex    = 0;
			if (B.HarnessIndex < 0 && GI->HarnessSpecs.Num() > 0) B.HarnessIndex = 0;
			// A configured hangar build → mount the real Codex parts and tune
			// the engine to the selected motor's power rating.
			const bool bCodex = (GI->CurrentBuild.FrameIndex >= 0);
			SpawnedParamotorActor->bUseCodexBuild = bCodex;
			// Codex parts bake a -90 yaw into actor space (prop ends at +Y).
			// Rotate -90 so the prop points aft, away from the player.
			if (bCodex)
			{
				SpawnTransform.SetRotation((SpawnRot + FRotator(0.0f, -90.0f, 0.0f)).Quaternion());
			}
			SpawnedParamotorActor->ConfigureFromBuild(GI);
		}
		else
		{
			UE_LOG(LogParamotor, Log, TEXT("SpawnParamotor: no ParamotorGameInstance — GI class=%s"),
				GetGameInstance() ? *GetGameInstance()->GetClass()->GetName() : TEXT("null"));
		}
		SpawnedParamotorActor->FinishSpawning(SpawnTransform);
		// Globe maps: anchor the rig so it tracks georeference origin rebasing.
		CesiumWorld::AnchorToGlobe(GetWorld(), SpawnedParamotorActor);
		SpawnedActors.Add(SpawnedParamotorActor);
		UE_LOG(LogParamotor, Log, TEXT("Spawned ParamotorActor at %s (codex=%d)"),
			*SpawnLoc.ToString(), (int32)SpawnedParamotorActor->bUseCodexBuild);
	}
}
float AParamotorPlayerController::FindGroundZ(FVector Start, APawn* IgnorePawn)
{
	FHitResult Hit;
	FCollisionQueryParams QueryParams;
	if (IgnorePawn) QueryParams.AddIgnoredActor(IgnorePawn);
	FVector TraceEnd = Start - FVector(0, 0, 500.0f);
	if (GetWorld()->LineTraceSingleByChannel(Hit, Start, TraceEnd, ECC_Visibility, QueryParams))
	{
		return Hit.Location.Z;
	}
	return 0.0f;
}
void AParamotorPlayerController::SpawnTestWing()
{
	APawn* MyPawn = GetPawn();
	if (!MyPawn) return;

	// If wing already exists, destroy it first
	if (SpawnedWingActor)
	{
		SpawnedActors.Remove(SpawnedWingActor);
		SpawnedWingActor->Destroy();
		SpawnedWingActor = nullptr;
	}

	// Layout follows the pilot: the wing goes on the ground behind you,
	// facing your launch direction. Face into the wind and it pressurizes
	// on the spot; face downwind and you'll need to outrun the tailwind
	// before the intakes ram — a real (if sketchy) technique.
	FVector Forward = MyPawn->GetActorForwardVector();
	FVector LayoutPos = MyPawn->GetActorLocation() - Forward * 750.0f;

	float SpawnZ = FindGroundZ(LayoutPos, MyPawn) + 5.0f;
	FVector SpawnLoc = FVector(LayoutPos.X, LayoutPos.Y, SpawnZ);
	FRotator SpawnRot = FRotator(0.0f, MyPawn->GetActorRotation().Yaw, 0.0f);

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// Use deferred spawn so we can set wing model before BeginPlay runs
	FTransform SpawnTransform(SpawnRot, SpawnLoc);
	SpawnedWingActor = GetWorld()->SpawnActorDeferred<AWingActor>(AChargerWingActor::StaticClass(), SpawnTransform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (SpawnedWingActor)
	{
		// Wing model from the hangar build — the wing you picked is the wing
		// you fly. Falls back to the tuned Charger 25 numbers.
		const FParamotorWingSpec* WS = nullptr;
		if (UParamotorGameInstance* GI = Cast<UParamotorGameInstance>(GetGameInstance()))
		{
			GI->EnsureSpecData();
			if (GI->CurrentBuild.WingIndex < 0)
			{
				if (UParamotorSaveGame* Save = Cast<UParamotorSaveGame>(
						UGameplayStatics::LoadGameFromSlot(TEXT("ParamotorBuildSlot"), 0)))
				{
					GI->CurrentBuild = Save->CurrentBuild;
				}
			}
			// No saved wing → fly the first catalog wing (Charger 25).
			if (GI->CurrentBuild.WingIndex < 0 && GI->WingSpecs.Num() > 0)
			{
				GI->CurrentBuild.WingIndex = 0;
			}
			if (GI->WingSpecs.IsValidIndex(GI->CurrentBuild.WingIndex))
			{
				WS = &GI->WingSpecs[GI->CurrentBuild.WingIndex];
			}
			// Live/custom wind heading — the wing lays out and flies into it.
			if (AChargerWingActor* CW = Cast<AChargerWingActor>(SpawnedWingActor))
			{
				CW->WindHeadingDegrees = GI->WindDirection;
			}
		}
		if (WS)
		{
			SpawnedWingActor->WingModel.Name = FString::Printf(TEXT("%s %s"), *WS->Brand, *WS->Model);
			SpawnedWingActor->WingModel.WingArea = WS->FlatArea_m2 > 0.0f ? WS->FlatArea_m2 : 25.43f;
			SpawnedWingActor->WingModel.MaxTakeoffWeight = WS->MaxWeightKg > 0 ? (float)WS->MaxWeightKg : 137.0f;
			SpawnedWingActor->WingModel.WingWeight = WS->WingWeight_kg > 0.0f ? WS->WingWeight_kg : 5.6f;
			SpawnedWingActor->WingModel.CL0 = WS->CL0;
			SpawnedWingActor->WingModel.CLMax = WS->CLMax;
			SpawnedWingActor->WingModel.CD0 = WS->CD0;
			SpawnedWingActor->WingModel.AR = WS->AspectRatioFlat > 0.0f ? WS->AspectRatioFlat : 5.14f;
			SpawnedWingActor->WingModel.StallAoA = WS->StallAoA;
			SpawnedWingActor->WingModel.TrimAoA = WS->TrimAoA;
		}
		else
		{
			SpawnedWingActor->WingModel.Name = TEXT("MacPara Charger 25M");
			SpawnedWingActor->WingModel.WingArea = 25.43f;
			SpawnedWingActor->WingModel.MaxTakeoffWeight = 137.0f;
			SpawnedWingActor->WingModel.WingWeight = 5.6f;
			SpawnedWingActor->WingModel.CL0 = 0.50f;
			SpawnedWingActor->WingModel.CLMax = 1.45f;
			SpawnedWingActor->WingModel.CD0 = 0.035f;
			SpawnedWingActor->WingModel.AR = 5.14f;
			SpawnedWingActor->WingModel.StallAoA = 16.0f;
			SpawnedWingActor->WingModel.TrimAoA = 1.5f;
		}

		SpawnedWingActor->FinishSpawning(SpawnTransform);
		CesiumWorld::AnchorToGlobe(GetWorld(), SpawnedWingActor);

		SpawnedActors.Add(SpawnedWingActor);
		UE_LOG(LogParamotor, Log, TEXT("Spawned wing '%s' at %s"), *SpawnedWingActor->WingModel.Name, *SpawnLoc.ToString());
	}
}
void AParamotorPlayerController::ClearSpawnedActors()
{
	for (AActor* Actor : SpawnedActors)
	{
		if (Actor)
		{
			Actor->Destroy();
		}
	}
	SpawnedActors.Empty();
	SpawnedParamotorActor = nullptr;
	SpawnedWingActor = nullptr;
	UE_LOG(LogParamotor, Log, TEXT("Cleared all spawned actors"));
}
void AParamotorPlayerController::PsTurn(float Yaw)
{
	// rotate the pawn in place without moving the camera — "PsTurn 180" for a front view
	APawn* P = GetPawn();
	if (!P) return;
	P->SetActorRotation(FRotator(0.0f, P->GetActorRotation().Yaw + Yaw, 0.0f));
	UE_LOG(LogParamotor, Log, TEXT("PsTurn rel=%.1f"), Yaw);
}

void AParamotorPlayerController::PsFaceCam(float Dist, float AimZ)
{
	// spawn a fixed camera in front of the pawn and view through it — the
	// follow cam always shows the back; this gives a true front view.
	// Dist/AimZ default to a head shot; larger Dist frames the full body.
	APawn* P = GetPawn();
	if (!P) return;
	if (Dist <= 0.0f) Dist = 320.0f;
	if (AimZ <= 0.0f) AimZ = 150.0f;
	const FVector Aim = P->GetActorLocation() + FVector(0, 0, AimZ);
	const FVector CamPos = Aim + P->GetActorForwardVector() * Dist;
	ACameraActor* Cam = GetWorld()->SpawnActor<ACameraActor>(CamPos, (Aim - CamPos).Rotation());
	if (Cam)
	{
		SetViewTargetWithBlend(Cam, 0.3f);
		UE_LOG(LogParamotor, Log, TEXT("PsFaceCam at %s"), *CamPos.ToCompactString());
	}
}

void AParamotorPlayerController::PsMeshYaw(float Yaw)
{
	// rotate the pawn's mesh relative to the actor — front view without the
	// character re-orienting to the control rotation
	APawn* P = GetPawn();
	if (!P) return;
	if (ACharacter* Ch = Cast<ACharacter>(P))
	{
		FRotator R = Ch->GetMesh()->GetRelativeRotation();
		R.Yaw += Yaw;
		Ch->GetMesh()->SetRelativeRotation(R);
		UE_LOG(LogParamotor, Log, TEXT("PsMeshYaw rel=%.1f mesh_yaw=%.1f"), Yaw, R.Yaw);
	}
}

void AParamotorPlayerController::PsCamYaw(float Yaw, float Pitch)
{
	// rotate the follow camera relative to the pawn without turning the pawn —
	// e.g. PsCamYaw 180 swings the camera to the character's front
	APawn* P = GetPawn();
	if (!P) return;
	SetControlRotation(FRotator(Pitch, P->GetActorRotation().Yaw + Yaw, 0.0f));
	UE_LOG(LogParamotor, Log, TEXT("PsCamYaw rel=%.1f"), Yaw);
}

void AParamotorPlayerController::PsFlyTo(float X, float Y, float Z, float Pitch, float Yaw)
{
	APawn* P = GetPawn();
	if (!P) return;
	if (ACharacter* Ch = Cast<ACharacter>(P))
	{
		if (UCharacterMovementComponent* Move = Ch->GetCharacterMovement())
		{
			Move->SetMovementMode(MOVE_Flying);
			Move->StopMovementImmediately();
			Move->GravityScale = 0.0f;
		}
	}
	P->TeleportTo(FVector(X, Y, Z), FRotator(Pitch, Yaw, 0.0f));
	SetControlRotation(FRotator(Pitch, Yaw, 0.0f));
	UE_LOG(LogParamotor, Log, TEXT("PsFlyTo -> %.0f %.0f %.0f"), X, Y, Z);
}

void AParamotorPlayerController::PsFlyToLL(float Lat, float Lon, float AltM, float Pitch, float Yaw)
{
	if (!CesiumWorld::DetectGlobe(GetWorld()))
	{
		UE_LOG(LogParamotor, Warning, TEXT("PsFlyToLL: not a globe map — no georeference"));
		return;
	}
	// A wing/paramotor left 4000 km behind would explode the riser constraints —
	// drop the rig; re-equip at the destination.
	ClearSpawnedActors();
	CesiumWorld::SetGeoreferenceOrigin(GetWorld(), Lon, Lat, 0.0);
	PendingLat = Lat; PendingLon = Lon;
	PendingAltM = AltM > 0.0f ? AltM : 5000.0f;
	PendingPitch = Pitch; PendingYaw = Yaw;
	bPendingLLTeleport = true;
	LLTeleportTimer = 0.4f; // let the rebase apply before teleporting
	UE_LOG(LogParamotor, Log, TEXT("PsFlyToLL -> lat %.5f lon %.5f alt %.0fm"), Lat, Lon, PendingAltM);
}

void AParamotorPlayerController::PsSea(int32 State)
{
	TArray<AActor*> Found;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AParamotorOceanSystem::StaticClass(), Found);
	if (Found.Num() > 0)
		Cast<AParamotorOceanSystem>(Found[0])->ForceSeaState(State);
}

void AParamotorPlayerController::PsWalk()
{
	APawn* P = GetPawn();
	if (!P) return;
	if (ACharacter* Ch = Cast<ACharacter>(P))
	{
		if (UCharacterMovementComponent* Move = Ch->GetCharacterMovement())
		{
			Move->GravityScale = 1.0f;
			Move->Velocity = FVector::ZeroVector;
			Move->SetMovementMode(MOVE_Walking);
		}
	}
	UE_LOG(LogParamotor, Log, TEXT("PsWalk"));
}

void AParamotorPlayerController::PsSand(int32 Mode)
{
	TArray<AActor*> Found;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AParamotorSandSystem::StaticClass(), Found);
	if (Found.Num() == 0) { UE_LOG(LogParamotor, Warning, TEXT("PsSand: no sand system")); return; }
	AParamotorSandSystem* S = Cast<AParamotorSandSystem>(Found[0]);
	if (Mode <= 2) S->DebugMode = Mode;
	else if (Mode == 3) S->DebugTestStamp();
	else if (Mode == 4) S->DebugDump();
	else if (Mode >= 5 && Mode <= 7) S->DebugSetView(Mode - 5);
}

void AParamotorPlayerController::PsVeg()
{
	TArray<AActor*> Found;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AVegetationField::StaticClass(), Found);
	if (Found.Num() == 0) { UE_LOG(LogParamotor, Warning, TEXT("PsVeg: no vegetation field (globe map only)")); return; }
	AVegetationField* V = Cast<AVegetationField>(Found[0]);
	FVector LLH = CesiumWorld::UnrealToGeodetic(GetWorld(), GetPawn() ? GetPawn()->GetActorLocation() : FVector::ZeroVector);
	UE_LOG(LogParamotor, Warning, TEXT("PsVeg: cells=%d instances=%d pawnLLH=%.5f,%.5f,%.0fm"),
		V->NumLiveCells, V->NumInstances, LLH.Y, LLH.X, LLH.Z);
}

static AVegetationField* FindVegField(UWorld* W)
{
	TArray<AActor*> Found;
	UGameplayStatics::GetAllActorsOfClass(W, AVegetationField::StaticClass(), Found);
	return Found.Num() ? Cast<AVegetationField>(Found[0]) : nullptr;
}

void AParamotorPlayerController::PsSemGeo()
{
	AVegetationField* V = FindVegField(GetWorld());
	if (!V) { UE_LOG(LogParamotor, Warning, TEXT("PsSemGeo: no vegetation field (globe map only)")); return; }
	const FVector LLH = CesiumWorld::UnrealToGeodetic(GetWorld(),
		GetPawn() ? GetPawn()->GetActorLocation() : FVector::ZeroVector);
	V->DumpSemanticsAt(LLH.Y, LLH.X);
}

void AParamotorPlayerController::PsVegLib()
{
	AVegetationField* V = FindVegField(GetWorld());
	if (!V) { UE_LOG(LogParamotor, Warning, TEXT("PsVegLib: no vegetation field")); return; }
	V->DumpLibrary();
}

void AParamotorPlayerController::PsVegDebug(int32 Mode)
{
	AVegetationField* V = FindVegField(GetWorld());
	if (!V) { UE_LOG(LogParamotor, Warning, TEXT("PsVegDebug: no vegetation field")); return; }
	V->DebugViz = Mode;
	UE_LOG(LogParamotor, Warning, TEXT("PsVegDebug: mode=%d (grass=greens shrub=orange tree=blues/reds none=gray)"), Mode);
}

void AParamotorPlayerController::PsVegSwap(const FString& Category, const FString& MeshPath)
{
	AVegetationField* V = FindVegField(GetWorld());
	if (!V) { UE_LOG(LogParamotor, Warning, TEXT("PsVegSwap: no vegetation field")); return; }
	EVegSemantic Sem;
	if (!VegSem::FromName(Category, Sem))
	{
		UE_LOG(LogParamotor, Warning, TEXT("PsVegSwap: unknown semantic '%s' — try Grass_NaturalTall, Tree_Deciduous…"), *Category);
		return;
	}
	V->DebugSwap(Sem, MeshPath);
}

void AParamotorPlayerController::PsVegReset()
{
	AVegetationField* V = FindVegField(GetWorld());
	if (!V) { UE_LOG(LogParamotor, Warning, TEXT("PsVegReset: no vegetation field")); return; }
	V->DebugReset();
}

static ASurfaceService* FindSurfService(UWorld* W)
{
	TArray<AActor*> Found;
	UGameplayStatics::GetAllActorsOfClass(W, ASurfaceService::StaticClass(), Found);
	return Found.Num() ? Cast<ASurfaceService>(Found[0]) : nullptr;
}

void AParamotorPlayerController::PsSurfGeo()
{
	ASurfaceService* S = FindSurfService(GetWorld());
	if (!S) { UE_LOG(LogParamotor, Warning, TEXT("PsSurfGeo: no surface service (globe map only)")); return; }
	S->DumpAt(GetPawn() ? GetPawn()->GetActorLocation() : FVector::ZeroVector);
}

void AParamotorPlayerController::PsSurfLib()
{
	ASurfaceService* S = FindSurfService(GetWorld());
	if (!S) { UE_LOG(LogParamotor, Warning, TEXT("PsSurfLib: no surface service")); return; }
	S->DumpLibrary();
}

void AParamotorPlayerController::PsSurfDebug(int32 Mode)
{
	ASurfaceService* S = FindSurfService(GetWorld());
	if (!S) { UE_LOG(LogParamotor, Warning, TEXT("PsSurfDebug: no surface service")); return; }
	S->DebugViz = Mode;
	UE_LOG(LogParamotor, Warning, TEXT("PsSurfDebug: mode=%d (sand=gold wet=dark-sand paved=dark vegetation=green water=blue)"), Mode);
}

void AParamotorPlayerController::PsSurfSwap(const FString& Category, const FString& MatPath)
{
	ASurfaceService* S = FindSurfService(GetWorld());
	if (!S) { UE_LOG(LogParamotor, Warning, TEXT("PsSurfSwap: no surface service")); return; }
	ESurfaceSemantic Sem;
	if (!VegSem::FromName(Category, Sem))
	{
		UE_LOG(LogParamotor, Warning, TEXT("PsSurfSwap: unknown surface semantic '%s' — try Gravel, DrySand, Meadow…"), *Category);
		return;
	}
	S->DebugSwap(Sem, MatPath);
}

void AParamotorPlayerController::PsSurfReset()
{
	ASurfaceService* S = FindSurfService(GetWorld());
	if (!S) { UE_LOG(LogParamotor, Warning, TEXT("PsSurfReset: no surface service")); return; }
	S->DebugReset();
}

void AParamotorPlayerController::PsGeo()
{
	APawn* P = GetPawn();
	if (!P) { UE_LOG(LogParamotor, Warning, TEXT("PsGeo: no pawn")); return; }
	const FVector PL = P->GetActorLocation();
	const FVector LLH = CesiumWorld::UnrealToGeodetic(GetWorld(), PL);

	const double GeoidN = CesiumWorld::GeoidUndulationM(LLH.Y, LLH.X);
	const double Msl = LLH.Z - GeoidN;

	// Ground probe: complex trace down 6 km — reports the FIRST sane surface
	// (which may legitimately be a roof/bridge deck).
	FHitResult Hit;
	FCollisionQueryParams P2;
	P2.bTraceComplex = true;
	P2.AddIgnoredActor(P);
	const FVector S(PL.X, PL.Y, PL.Z + 500.0f);
	const FVector E(PL.X, PL.Y, PL.Z - 600000.0f);
	FString GroundStr = TEXT("none");
	double GroundMsl = 0.0, Agl = 0.0;
	if (GetWorld()->LineTraceSingleByChannel(Hit, S, E, ECC_Visibility, P2))
	{
		const FVector GLLH = CesiumWorld::UnrealToGeodetic(GetWorld(), Hit.ImpactPoint);
		GroundMsl = GLLH.Z - GeoidN;
		Agl = (PL.Z - Hit.ImpactPoint.Z) / 100.0;
		GroundStr = FString::Printf(TEXT("worldZ=%.0fcm geo=%.1fm msl~%.1fm actor=%s"),
			Hit.ImpactPoint.Z, GLLH.Z, GroundMsl,
			Hit.GetActor() ? *Hit.GetActor()->GetName() : TEXT("null"));
	}
	// Water datum at the pawn, if the ocean system is live.
	FString WaterStr = TEXT("none");
	if (UWorld* W = GetWorld())
	{
		TArray<AActor*> Oc;
		UGameplayStatics::GetAllActorsOfClass(W, AParamotorOceanSystem::StaticClass(), Oc);
		if (Oc.Num() > 0)
		{
			AParamotorOceanSystem* O = Cast<AParamotorOceanSystem>(Oc[0]);
			const float WZ = O->GetWaterHeightAt(PL);
			const FVector WLLH = CesiumWorld::UnrealToGeodetic(W, FVector(PL.X, PL.Y, WZ));
			WaterStr = FString::Printf(TEXT("waterZ=%.0fcm geo=%.1fm msl~%.1fm"),
				WZ, WLLH.Z, WLLH.Z - GeoidN);
		}
	}
	UE_LOG(LogParamotor, Warning, TEXT(
		"PsGeo: lat=%.5f lon=%.5f | pawnWorldZ=%.0fcm | ellipsoid=%.1fm geoidN=%.1fm msl~%.1fm | AGL=%.1fm | %s | ground: %s"),
		LLH.Y, LLH.X, PL.Z, LLH.Z, GeoidN, Msl, Agl, *WaterStr, *GroundStr);

	// Landmark sanity: report any spawned NewportBridge's live geodetic fix.
	TArray<AActor*> Br;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), ANewportBridge::StaticClass(), Br);
	for (AActor* A : Br)
	{
		const FVector BLLH = CesiumWorld::UnrealToGeodetic(GetWorld(), A->GetActorLocation());
		UE_LOG(LogParamotor, Warning, TEXT(
			"PsGeo: bridge %s at lat=%.5f lon=%.5f ellipsoid=%.1fm worldZ=%.0fcm dist=%.0fm hidden=%d"),
			*A->GetName(), BLLH.Y, BLLH.X, BLLH.Z, A->GetActorLocation().Z,
			FVector::Dist(A->GetActorLocation(), PL) / 100.0f, A->IsHidden());
		TArray<UActorComponent*> Comps;
		A->GetComponents(Comps);
		for (UActorComponent* C : Comps)
		{
			if (UInstancedStaticMeshComponent* I = Cast<UInstancedStaticMeshComponent>(C))
			{
				const FVector BLLH2 = CesiumWorld::UnrealToGeodetic(GetWorld(), I->Bounds.Origin);
				UE_LOG(LogParamotor, Warning, TEXT(
					"PsGeo:   ism %s reg=%d vis=%d inst=%d mesh=%s boundsR=%.0fm bOrg=%.5f,%.5f,%.0fm"),
					*I->GetName(), I->IsRegistered(), I->IsVisible(), I->GetNumInstances(),
					I->GetStaticMesh() ? *I->GetStaticMesh()->GetName() : TEXT("null"),
					I->Bounds.SphereRadius / 100.0f, BLLH2.Y, BLLH2.X, BLLH2.Z);
			}
		}
	}
}

void AParamotorPlayerController::PsEngine(float Throttle01)
{
	if (!SpawnedParamotorActor) SpawnParamotor();
	AParamotorCharacter* Ch = Cast<AParamotorCharacter>(GetPawn());
	if (SpawnedParamotorActor && !SpawnedParamotorActor->IsEquipped() && Ch)
		SpawnedParamotorActor->Interact(Ch);
	if (!SpawnedParamotorActor || !SpawnedParamotorActor->IsEquipped())
	{
		UE_LOG(LogParamotor, Warning, TEXT("PsEngine: paramotor not equipped"));
		return;
	}
	DevThrottleHold = Throttle01 < 0.0f ? -1.0f : FMath::Clamp(Throttle01, 0.0f, 1.0f);
	SpawnedParamotorActor->SetThrottle(FMath::Max(0.0f, DevThrottleHold));
	if (SpawnedParamotorActor->GetEngineState() == EEngineState::Off)
		SpawnedParamotorActor->StartCranking();
	UE_LOG(LogParamotor, Log, TEXT("PsEngine: thr=%.2f state=%d"), Throttle01, (int32)SpawnedParamotorActor->GetEngineState());
}

void AParamotorPlayerController::PsHelmet(int32 State)
{
	AParamotorCharacter* Ch = Cast<AParamotorCharacter>(GetPawn());
	if (!Ch) { UE_LOG(LogParamotor, Warning, TEXT("PsHelmet: no character")); return; }
	Ch->SetHelmetOn(State != 0);
	UE_LOG(LogParamotor, Log, TEXT("PsHelmet: %d"), State);
}

void AParamotorPlayerController::PsVisor(int32 State)
{
	AParamotorCharacter* Ch = Cast<AParamotorCharacter>(GetPawn());
	if (!Ch) { UE_LOG(LogParamotor, Warning, TEXT("PsVisor: no character")); return; }
	Ch->SetVisorUp(State != 0);
	UE_LOG(LogParamotor, Log, TEXT("PsVisor: up=%d"), State);
}

void AParamotorPlayerController::PsHair(int32 State)
{
	AParamotorCharacter* Ch = Cast<AParamotorCharacter>(GetPawn());
	if (!Ch) { UE_LOG(LogParamotor, Warning, TEXT("PsHair: no character")); return; }
	// Turning hair on implies helmet off so the groom is actually visible.
	Ch->SetHelmetOn(State == 0);
	UE_LOG(LogParamotor, Log, TEXT("PsHair: %d"), State);
}

void AParamotorPlayerController::PsDumpBody()
{
#if WITH_EDITOR
	AParamotorCharacter* Ch = Cast<AParamotorCharacter>(GetPawn());
	USkeletalMeshComponent* Skel = Ch ? Ch->GetMesh() : nullptr;
	USkeletalMesh* Mesh = Skel ? Skel->GetSkeletalMeshAsset() : nullptr;
	if (!Mesh) { UE_LOG(LogParamotor, Warning, TEXT("PsDumpBody: no mesh")); return; }

	FString Out;
	Out.Reserve(64 * 1024 * 1024);
	const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
	const int32 NB = Ref.GetNum();
	Out += TEXT("{\"bones\":[");
	for (int32 i = 0; i < NB; ++i)
	{
		const FTransform& T = Ref.GetRefBonePose()[i];
		const FVector P = T.GetLocation();
		const FQuat Q = T.GetRotation();
		const FVector S = T.GetScale3D();
		Out += FString::Printf(TEXT("{\"n\":\"%s\",\"p\":%d,\"t\":[%.4f,%.4f,%.4f],\"q\":[%.5f,%.5f,%.5f,%.5f],\"s\":[%.4f,%.4f,%.4f]}%s"),
			*Ref.GetBoneName(i).ToString(), Ref.GetParentIndex(i),
			P.X, P.Y, P.Z, Q.X, Q.Y, Q.Z, Q.W, S.X, S.Y, S.Z,
			i + 1 < NB ? TEXT(",") : TEXT(""));
	}
	Out += TEXT("],\"sections\":[");

	const FSkeletalMeshModel* Model = Mesh->GetImportedModel();
	if (!Model || Model->LODModels.Num() == 0)
	{
		Out += TEXT("]}");
		FFileHelper::SaveStringToFile(Out, TEXT("C:/DevinTools/body_dump.json"));
		UE_LOG(LogParamotor, Warning, TEXT("PsDumpBody: no imported model data"));
		return;
	}
	const FSkeletalMeshLODModel& LOD = Model->LODModels[0];
	bool bFirstSec = true;
	for (const FSkelMeshSection& Sec : LOD.Sections)
	{
		if (!bFirstSec) Out += TEXT(",");
		bFirstSec = false;
		Out += FString::Printf(TEXT("{\"mat\":%d,\"base\":%d,\"verts\":["),
			(int32)Sec.MaterialIndex, (int32)Sec.BaseVertexIndex);
		const TArray<FSoftSkinVertex>& Verts = Sec.SoftVertices;
		for (int32 v = 0; v < Verts.Num(); ++v)
		{
			const FSoftSkinVertex& V = Verts[v];
			Out += FString::Printf(TEXT("%s{\"v\":[%.3f,%.3f,%.3f],\"w\":["),
				v ? TEXT(",") : TEXT(""), V.Position.X, V.Position.Y, V.Position.Z);
			bool bFirstW = true;
			for (int32 k = 0; k < MAX_TOTAL_INFLUENCES; ++k)
			{
				const uint8 W = V.InfluenceWeights[k];
				if (W == 0) break;
				const int32 BoneIdx = Sec.BoneMap.IsValidIndex(V.InfluenceBones[k]) ? Sec.BoneMap[V.InfluenceBones[k]] : -1;
				Out += FString::Printf(TEXT("%s[%d,%d]"), bFirstW ? TEXT("") : TEXT(","), BoneIdx, (int32)W);
				bFirstW = false;
			}
			Out += TEXT("]}");
		}
		Out += TEXT("]}");
	}
	Out += TEXT("]}");
	const bool bWrote = FFileHelper::SaveStringToFile(Out, TEXT("C:/DevinTools/body_dump.json"));
	if (bWrote)
	{
		UE_LOG(LogParamotor, Log, TEXT("PsDumpBody: wrote body_dump.json (sections=%d bones=%d)"), LOD.Sections.Num(), NB);
	}
	else
	{
		UE_LOG(LogParamotor, Warning, TEXT("PsDumpBody: write failed"));
	}
#else
	UE_LOG(LogParamotor, Warning, TEXT("PsDumpBody: editor builds only"));
#endif
}

void AParamotorPlayerController::PsDumpMesh(const FString& MeshPath, const FString& OutFile)
{
#if WITH_EDITOR
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *MeshPath);
	if (!Mesh) { UE_LOG(LogParamotor, Warning, TEXT("PsDumpMesh: cannot load %s"), *MeshPath); return; }

	FString Out;
	Out.Reserve(8 * 1024 * 1024);
	const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
	const int32 NB = Ref.GetNum();
	Out += TEXT("{\"bones\":[");
	for (int32 i = 0; i < NB; ++i)
	{
		const FTransform& T = Ref.GetRefBonePose()[i];
		Out += FString::Printf(TEXT("{\"n\":\"%s\",\"p\":%d,\"t\":[%.4f,%.4f,%.4f]}%s"),
			*Ref.GetBoneName(i).ToString(), Ref.GetParentIndex(i),
			T.GetLocation().X, T.GetLocation().Y, T.GetLocation().Z,
			i + 1 < NB ? TEXT(",") : TEXT(""));
	}
	Out += TEXT("],\"invbases\":[");
	{
		const TArray<FMatrix44f>& Inv = Mesh->GetRefBasesInvMatrix();
		for (int32 i = 0; i < Inv.Num(); ++i)
		{
			const FMatrix44f& M = Inv[i];
			Out += FString::Printf(TEXT("[%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f]%s"),
				M.M[0][0],M.M[0][1],M.M[0][2],M.M[0][3], M.M[1][0],M.M[1][1],M.M[1][2],M.M[1][3],
				M.M[2][0],M.M[2][1],M.M[2][2],M.M[2][3], M.M[3][0],M.M[3][1],M.M[3][2],M.M[3][3],
				i + 1 < Inv.Num() ? TEXT(",") : TEXT(""));
		}
	}
	Out += TEXT("],\"sections\":[");
	const FSkeletalMeshModel* Model = Mesh->GetImportedModel();
	if (Model && Model->LODModels.Num() > 0)
	{
		const FSkeletalMeshLODModel& LOD = Model->LODModels[0];
		bool bFirstSec = true;
		for (const FSkelMeshSection& Sec : LOD.Sections)
		{
			if (!bFirstSec) Out += TEXT(",");
			bFirstSec = false;
			Out += FString::Printf(TEXT("{\"mat\":%d,\"nv\":%d,\"bonemap\":["), (int32)Sec.MaterialIndex, Sec.SoftVertices.Num());
			for (int32 i = 0; i < Sec.BoneMap.Num(); ++i)
				Out += FString::Printf(TEXT("%s%d"), i ? TEXT(",") : TEXT(""), (int32)Sec.BoneMap[i]);
			Out += TEXT("],\"verts\":[");
			const TArray<FSoftSkinVertex>& Verts = Sec.SoftVertices;
			for (int32 v = 0; v < Verts.Num(); ++v)
			{
				const FSoftSkinVertex& V = Verts[v];
				Out += FString::Printf(TEXT("%s{\"v\":[%.3f,%.3f,%.3f],\"w\":["),
					v ? TEXT(",") : TEXT(""), V.Position.X, V.Position.Y, V.Position.Z);
				bool bFirstW = true;
				for (int32 k = 0; k < MAX_TOTAL_INFLUENCES; ++k)
				{
					const uint8 W = V.InfluenceWeights[k];
					if (W == 0 && k > 0) break;
					const int32 BoneIdx = Sec.BoneMap.IsValidIndex(V.InfluenceBones[k]) ? Sec.BoneMap[V.InfluenceBones[k]] : -1;
					Out += FString::Printf(TEXT("%s[%d,%d]"), bFirstW ? TEXT("") : TEXT(","), BoneIdx, (int32)W);
					bFirstW = false;
					if (W == 0) break;
				}
				Out += TEXT("]}");
			}
			Out += TEXT("]}");
		}
	}
	Out += TEXT("]}");
	const bool bWrote = FFileHelper::SaveStringToFile(Out, *OutFile);
	UE_LOG(LogParamotor, Log, TEXT("PsDumpMesh: %s -> %s wrote=%d bones=%d"), *MeshPath, *OutFile, (int32)bWrote, NB);
#else
	UE_LOG(LogParamotor, Warning, TEXT("PsDumpMesh: editor builds only"));
#endif
}

void AParamotorPlayerController::PsGarment(int32 State)
{
	AParamotorCharacter* Ch = Cast<AParamotorCharacter>(GetPawn());
	if (!Ch) { UE_LOG(LogParamotor, Warning, TEXT("PsGarment: no pawn")); return; }
	TArray<USkeletalMeshComponent*> Comps;
	Ch->GetComponents<USkeletalMeshComponent>(Comps);
	for (USkeletalMeshComponent* C : Comps)
	{
		if (C == Ch->GetMesh()) continue;
		const FName N = C->GetFName();
		if (N != TEXT("ShirtMesh") && N != TEXT("PantsMesh") && N != TEXT("BootsMesh")) continue;
		if (State == 0)
		{
			C->SetVisibility(false);
		}
		else
		{
			C->SetVisibility(true);
			if (State == 2)
				C->SetLeaderPoseComponent(nullptr);   // ref pose at own transform
			else if (State == 4)
			{
				C->SetLeaderPoseComponent(nullptr);
				// detach to world: floating shell 2m to the side, ref pose
				C->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
				C->SetWorldLocation(Ch->GetMesh()->GetComponentLocation() + FVector(0.0f, 200.0f, 0.0f));
			}
			else
			{
				if (!C->GetAttachParent())
					C->AttachToComponent(Ch->GetMesh(), FAttachmentTransformRules::SnapToTargetIncludingScale);
				C->SetLeaderPoseComponent(Ch->GetMesh());
			}
		}
	}
	UE_LOG(LogParamotor, Log, TEXT("PsGarment: state=%d"), State);

	if (State == 3)
	{
		// Compare garment vs body bone transforms in component space
		USkeletalMeshComponent* Body = Ch->GetMesh();
		{
			const FTransform BC = Body->GetComponentTransform();
			UE_LOG(LogParamotor, Log, TEXT("  body comp t=(%.2f,%.2f,%.2f) r=(%.1f,%.1f,%.1f) s=(%.3f,%.3f,%.3f)"),
				BC.GetLocation().X, BC.GetLocation().Y, BC.GetLocation().Z,
				BC.GetRotation().Rotator().Pitch, BC.GetRotation().Rotator().Yaw, BC.GetRotation().Rotator().Roll,
				BC.GetScale3D().X, BC.GetScale3D().Y, BC.GetScale3D().Z);
		}
		{
			const FBoxSphereBounds BB = Body->Bounds;
			UE_LOG(LogParamotor, Log, TEXT("  body bounds c=(%.1f,%.1f,%.1f) ext=(%.1f,%.1f,%.1f) r=%.1f"),
				BB.Origin.X, BB.Origin.Y, BB.Origin.Z, BB.BoxExtent.X, BB.BoxExtent.Y, BB.BoxExtent.Z, BB.SphereRadius);
		}
		for (USkeletalMeshComponent* C : Comps)
		{
			const FName N = C->GetFName();
			if (N != TEXT("ShirtMesh") && N != TEXT("PantsMesh") && N != TEXT("BootsMesh")) continue;
			{
				const FTransform CT = C->GetComponentTransform();
				const FTransform RT = C->GetRelativeTransform();
				const FBoxSphereBounds GB = C->Bounds;
				UE_LOG(LogParamotor, Log, TEXT("  %s comp t=(%.2f,%.2f,%.2f) rel t=(%.2f,%.2f,%.2f) rel r=(%.1f,%.1f,%.1f) bounds c=(%.1f,%.1f,%.1f) ext=(%.1f,%.1f,%.1f) r=%.1f"),
					*N.ToString(), CT.GetLocation().X, CT.GetLocation().Y, CT.GetLocation().Z,
					RT.GetLocation().X, RT.GetLocation().Y, RT.GetLocation().Z,
					RT.GetRotation().Rotator().Pitch, RT.GetRotation().Rotator().Yaw, RT.GetRotation().Rotator().Roll,
					GB.Origin.X, GB.Origin.Y, GB.Origin.Z, GB.BoxExtent.X, GB.BoxExtent.Y, GB.BoxExtent.Z, GB.SphereRadius);
			}
			for (const TCHAR* BN : { TEXT("pelvis"), TEXT("spine_03"), TEXT("head"), TEXT("thigh_l"), TEXT("foot_l") })
			{
				const int32 BI = C->GetBoneIndex(FName(BN));
				const int32 BB = Body->GetBoneIndex(FName(BN));
				if (BI < 0 || BB < 0) continue;
				const FVector GT = C->GetBoneTransform(BI).GetLocation();
				const FVector BT = Body->GetBoneTransform(BB).GetLocation();
				UE_LOG(LogParamotor, Log, TEXT("  %s %s: garment=(%.1f,%.1f,%.1f) body=(%.1f,%.1f,%.1f)"),
					*N.ToString(), BN, GT.X, GT.Y, GT.Z, BT.X, BT.Y, BT.Z);
			}
			// actual skinned vertex positions in component space
			TArray<FVector> GSkin, BSkin;
			{
				auto Skinned = [](USkeletalMeshComponent* Comp, TArray<FVector>& Out)
				{
					const FSkeletalMeshRenderData* RD = Comp->GetSkeletalMeshRenderData();
					if (!RD || !RD->LODRenderData.Num()) return;
					const FSkeletalMeshLODRenderData& LOD = RD->LODRenderData[0];
					TArray<FMatrix44f> R2L;
					Comp->CacheRefToLocalMatrices(R2L);
					TArray<FVector3f> Pos;
					USkinnedMeshComponent::ComputeSkinnedPositions(Comp, Pos, R2L, LOD, LOD.SkinWeightVertexBuffer);
					Out.Reset(Pos.Num());
					for (const FVector3f& P : Pos) Out.Add(FVector(P));
				};
				Skinned(C, GSkin);
				Skinned(Body, BSkin);
			}
			if (GSkin.Num() && BSkin.Num())
			{
				double Sum = 0.0, MaxD = 0.0, MinD = 1e9;
				for (const FVector& G : GSkin)
				{
					double Best = 1e9;
					for (const FVector& B : BSkin)
						Best = FMath::Min(Best, (G - B).Size());
					Sum += Best; MaxD = FMath::Max(MaxD, Best); MinD = FMath::Min(MinD, Best);
				}
				// signed-ish: also report centroid of garment vs body
				FVector GC(0), BC(0);
				for (const FVector& G : GSkin) GC += G;
				for (const FVector& B : BSkin) BC += B;
				GC /= GSkin.Num(); BC /= BSkin.Num();
				UE_LOG(LogParamotor, Log, TEXT("  %s skinned: n=%d nearest-body mean=%.2f min=%.2f max=%.2f | gcent=(%.1f,%.1f,%.1f) bcent=(%.1f,%.1f,%.1f)"),
					*N.ToString(), GSkin.Num(), Sum / GSkin.Num(), MinD, MaxD,
					GC.X, GC.Y, GC.Z, BC.X, BC.Y, BC.Z);
			}
		}
	}
}
