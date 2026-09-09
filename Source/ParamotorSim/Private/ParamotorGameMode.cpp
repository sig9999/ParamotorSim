// Copyright Epic Games, Inc. All Rights Reserved.

#include "ParamotorGameMode.h"
#include "Components/BoxComponent.h"
#include "ParamotorPlayerController.h"
#include "ParamotorCharacter.h"
#include "ParamotorGameInstance.h"
#include "ParamotorEnvironment.h"
#include "ProceduralMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"
#include "Landscape.h"
#include "GameFramework/PlayerStart.h"
#include "Engine/World.h"

AParamotorGameMode::AParamotorGameMode()
{
	PlayerControllerClass = AParamotorPlayerController::StaticClass();
	DefaultPawnClass = AParamotorCharacter::StaticClass();
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
}

void AParamotorGameMode::BeginPlay()
{
	Super::BeginPlay();

	// Spawn the environment actor — owns sky/sun/fog/post-process/water/audio
	// and its own tick for time-of-day sun updates.
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	GetWorld()->SpawnActor<AParamotorEnvironment>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);

	SetupCoastalTerrain();

	// Reposition PlayerStart to the launch field — also handled in RestartPlayer
	{
		TArray<AActor*> PlayerStarts;
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), APlayerStart::StaticClass(), PlayerStarts);
		FVector FieldLoc = GetLaunchFieldSpawnLocation();
		for (AActor* PS : PlayerStarts)
		{
			PS->SetActorLocation(FieldLoc);
			UE_LOG(LogTemp, Warning, TEXT("GameMode: PlayerStart pre-positioned at Ninigret at %s"), *FieldLoc.ToString());
		}
		if (PlayerStarts.Num() == 0)
			UE_LOG(LogTemp, Warning, TEXT("GameMode: No PlayerStart found!"));
	}
}

// === Coastal terrain height function ===
// Returns height in centimeters above sea level (Z=0)
// World coordinates: X = east/west, Y = south/north (south = ocean, north = hills)
float AParamotorGameMode::GetCoastalHeight(float WorldX, float WorldY) const
{
	// Terrain spans -1000m to +1000m in X and Y (2km x 2km)
	// Normalize to 0..1 where Y=0 is south (ocean), Y=1 is north (hills)
	float Yn = FMath::Clamp((WorldY + 100000.0f) / 200000.0f, 0.0f, 1.0f);
	float Xn = FMath::Clamp((WorldX + 100000.0f) / 200000.0f, 0.0f, 1.0f);

	float HeightM = 0.0f;  // height in meters

	if (Yn < 0.15f)
	{
		// Ocean floor — below sea level, sloping down
		HeightM = -1.0f - ((0.15f - Yn) / 0.15f) * 4.0f;  // -1m to -5m
	}
	else if (Yn < 0.22f)
	{
		// Beach — gradual rise from sea level to 3m
		float T = (Yn - 0.15f) / 0.07f;
		HeightM = -1.0f + T * 4.0f;
	}
	else if (Yn < 0.45f)
	{
		// Launch/landing field — flat at ~5m with very gentle undulation
		HeightM = 5.0f + FMath::Sin(Xn * 31.4f) * FMath::Sin(Yn * 47.1f) * 0.3f;
	}
	else if (Yn < 0.60f)
	{
		// Transition zone — rise from 5m to 15m with some variation
		float T = (Yn - 0.45f) / 0.15f;
		HeightM = 5.0f + T * 10.0f + FMath::Sin(Xn * 25.1f) * 0.8f + FMath::Sin(Yn * 39.2f) * 0.5f;
	}
	else
	{
		// Rolling hills — 15m to 40m with multi-frequency noise
		float T = (Yn - 0.60f) / 0.40f;
		float Base = 15.0f + T * 25.0f;
		float H1 = FMath::Sin(Xn * 12.56f) * 3.0f;
		float H2 = FMath::Sin(Xn * 28.27f + 0.7f) * 1.5f;
		float H3 = FMath::Sin(Yn * 18.84f + 1.3f) * 2.0f;
		float H4 = FMath::Sin(Xn * 6.28f + Yn * 6.28f) * 2.5f;
		HeightM = Base + H1 + H2 + H3 + H4;
	}

	return HeightM * 100.0f;  // convert meters to cm
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
			UE_LOG(LogTemp, Warning, TEXT("GameMode: Real landscape found (%d actors) — skipping procedural field"), Existing.Num());
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
		UE_LOG(LogTemp, Warning, TEXT("GameMode: Removed %d old landscape actors"), Landscapes.Num());

	// Spawn a real AStaticMeshActor for the airport/field so we get reliable collision and rendering
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	TerrainActor = GetWorld()->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), FVector(0.0f, 0.0f, 500.0f), FRotator(0.0f, 0.0f, 0.0f), SpawnParams);
	if (TerrainActor)
	{
		TerrainActor->SetActorLabel(TEXT("WesterlyAirportField"));
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
			UE_LOG(LogTemp, Warning, TEXT("GameMode: Westerly Airport field spawned at Z=500cm, 2km x 2km"));

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
			UE_LOG(LogTemp, Warning, TEXT("GameMode: Westerly Airport collision floor added"));
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("GameMode: Failed to load Plane mesh for field!"));
		}
	}
}
// Procedural coastal terrain replaced with a flat Westerly Airport field for reliable collision/rendering

FVector AParamotorGameMode::GetLaunchFieldSpawnLocation() const
{
	// Ninigret Park parking lot beside the old auxiliary airstrip remnant,
	// just east of Ninigret Pond. UTM 19N E=279150 N=4583800;
	// map center UTM (269580, 4580015) = world origin.
	const float SpawnX = (279150.0f - 269580.0f) * 100.0f;   // +957000 cm
	const float SpawnY = (4583800.0f - 4580015.0f) * 100.0f; // +378500 cm

	// Drop a line trace to sit exactly on the landscape surface — multi-hit so we
	// can skip non-terrain blockers (ocean collision volumes, trees, etc.)
	FVector Start(SpawnX, SpawnY, 50000.0f);
	FVector End(SpawnX, SpawnY, -50000.0f);
	TArray<FHitResult> Hits;
	FCollisionQueryParams Params;
	Params.bTraceComplex = false;
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
			if (Cast<ALandscapeProxy>(HA))
			{
				TerrainZ = H.ImpactPoint.Z;
				bFound = true;
				UE_LOG(LogTemp, Warning, TEXT("GameMode: Spawn trace hit landscape at Z=%.0fcm"), TerrainZ);
				break;
			}
			UE_LOG(LogTemp, Warning, TEXT("GameMode: Spawn trace skipped %s at Z=%.0fcm"),
				HA ? *HA->GetName() : TEXT("null"), H.ImpactPoint.Z);
			if (HA) Ignored.AddUnique(HA);
		}
	}
	if (!bFound)
	{
		UE_LOG(LogTemp, Warning, TEXT("GameMode: Spawn trace found no landscape at (%.0f, %.0f)"), SpawnX, SpawnY);
	}
	return FVector(SpawnX, SpawnY, TerrainZ + 200.0f);
}

AActor* AParamotorGameMode::FindPlayerStart_Implementation(AController* Player, const FString& IncomingName)
{
	// On the real-terrain map, guarantee a PlayerStart exists at the launch field
	// BEFORE the engine picks a spawn point (prevents spawning at world origin).
	TArray<AActor*> Existing;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), ALandscapeProxy::StaticClass(), Existing);
	if (Existing.Num() > 0)
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
				UE_LOG(LogTemp, Warning, TEXT("GameMode: FindPlayerStart created PlayerStart at Ninigret %s"), *FieldLoc.ToString());
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
			UE_LOG(LogTemp, Warning, TEXT("GameMode: Created PlayerStart at Ninigret Park %s"), *FieldLoc.ToString());
		}
	}
	for (AActor* PS : PlayerStarts)
	{
		PS->SetActorLocation(FieldLoc);
		UE_LOG(LogTemp, Warning, TEXT("GameMode: PlayerStart set to Ninigret Park at %s"), *FieldLoc.ToString());
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
			UE_LOG(LogTemp, Warning, TEXT("GameMode: pawn was at %s — teleported to launch field"), *Loc.ToString());
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("GameMode: pawn spawned at %s"), *Loc.ToString());
		}
	}
}
