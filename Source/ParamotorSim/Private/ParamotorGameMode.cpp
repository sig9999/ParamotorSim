// Copyright Epic Games, Inc. All Rights Reserved.

#include "ParamotorGameMode.h"
#include "ParamotorPlayerController.h"
#include "ParamotorCharacter.h"
#include "Components/AudioComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Sound/SoundBase.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Landscape.h"
#include "Engine/World.h"

AParamotorGameMode::AParamotorGameMode()
{
	PlayerControllerClass = AParamotorPlayerController::StaticClass();
	DefaultPawnClass = AParamotorCharacter::StaticClass();
}

void AParamotorGameMode::BeginPlay()
{
	Super::BeginPlay();

	// Spawn a large flat plane underneath the landscape to hide the
	// default concrete material visible from below
	UStaticMesh* PlaneMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (PlaneMesh)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		FVector UnderLoc(0.0f, 0.0f, -2000.0f);
		FRotator UnderRot(180.0f, 0.0f, 0.0f);
		AStaticMeshActor* UnderPlane = GetWorld()->SpawnActor<AStaticMeshActor>(UnderLoc, UnderRot, SpawnParams);
		if (UnderPlane)
		{
			UnderPlane->GetStaticMeshComponent()->SetStaticMesh(PlaneMesh);
			UnderPlane->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
			UnderPlane->SetActorScale3D(FVector(200.0f, 200.0f, 1.0f));
			UnderPlane->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

			UMaterialInterface* DarkMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/WorldGridMaterial.WorldGridMaterial"));
			if (DarkMat)
			{
				UnderPlane->GetStaticMeshComponent()->SetMaterial(0, DarkMat);
			}

			UE_LOG(LogTemp, Warning, TEXT("GameMode: Spawned under-map plane at Z=-2000"));
		}
	}

	// Play ambient birds sound
	USoundBase* BirdsSound = LoadObject<USoundBase>(nullptr, TEXT("/Game/Sounds/birds_ambient.birds_ambient"));
	if (BirdsSound)
	{
		BirdsAudio = UGameplayStatics::SpawnSound2D(GetWorld(), BirdsSound);
		if (BirdsAudio)
		{
			BirdsAudio->bIsUISound = false;
			BirdsAudio->SetVolumeMultiplier(0.08f);
			UE_LOG(LogTemp, Warning, TEXT("GameMode: Birds ambient started (vol=0.08)"));
		}
	}

	// Play ambient ocean waves sound
	USoundBase* OceanSound = LoadObject<USoundBase>(nullptr, TEXT("/Game/Sounds/ocean_waves.ocean_waves"));
	if (OceanSound)
	{
		OceanAudio = UGameplayStatics::SpawnSound2D(GetWorld(), OceanSound);
		if (OceanAudio)
		{
			OceanAudio->bIsUISound = false;
			OceanAudio->SetVolumeMultiplier(0.1f);
			UE_LOG(LogTemp, Warning, TEXT("GameMode: Ocean ambient started (vol=0.10)"));
		}
	}

	// === Apply landscape material ===
	UMaterialInterface* LandscapeMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_Landscape.M_Landscape"));

	if (LandscapeMat)
	{
		TArray<AActor*> FoundActors;
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), ALandscapeProxy::StaticClass(), FoundActors);
		int32 Count = 0;
		for (AActor* Actor : FoundActors)
		{
			ALandscapeProxy* Landscape = Cast<ALandscapeProxy>(Actor);
			if (Landscape)
			{
				Landscape->LandscapeMaterial = LandscapeMat;
				Count++;
				UE_LOG(LogTemp, Warning, TEXT("GameMode: Applied landscape material to %s"), *Landscape->GetName());
			}
		}
		UE_LOG(LogTemp, Warning, TEXT("GameMode: Applied landscape material to %d landscape proxies"), Count);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("GameMode: No landscape material available"));
	}
}
