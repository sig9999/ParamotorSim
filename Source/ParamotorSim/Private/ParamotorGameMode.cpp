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
#include "Materials/MaterialInstanceDynamic.h"
#include "Landscape.h"
#include "TextureResource.h"
#include "Engine/Texture2D.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Materials/MaterialExpressionVertexNormalWS.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionClamp.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionDivide.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/World.h"
#include "Math/RandomStream.h"

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

	// === Apply high-quality landscape material ===
	// Try M_Landscape first (may be created by Python script), then create runtime material
	UMaterialInterface* LandscapeMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_Landscape.M_Landscape"));
	if (!LandscapeMat)
	{
		UE_LOG(LogTemp, Warning, TEXT("GameMode: M_Landscape not found, creating runtime landscape material"));
		UMaterialInstanceDynamic* RuntimeMat = CreateLandscapeMaterial();
		if (RuntimeMat)
		{
			LandscapeMat = RuntimeMat;
		}
	}

	if (!LandscapeMat)
	{
		// Final fallback to M_Grass
		LandscapeMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_Grass.M_Grass"));
	}

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

	// === Scatter 3D grass meshes around spawn area ===
	ScatterGrassMeshes();
}

UMaterialInstanceDynamic* AParamotorGameMode::CreateLandscapeMaterial()
{
	// Load the existing M_Grass as a base material
	UMaterial* BaseMat = LoadObject<UMaterial>(nullptr, TEXT("/Game/Materials/M_Grass.M_Grass"));
	if (!BaseMat)
	{
		UE_LOG(LogTemp, Warning, TEXT("GameMode: M_Grass base material not found for runtime landscape material"));
		return nullptr;
	}

	// Create a Material Instance Dynamic from M_Grass
	UMaterialInstanceDynamic* DynMat = UMaterialInstanceDynamic::Create(BaseMat, this);
	if (!DynMat)
	{
		UE_LOG(LogTemp, Warning, TEXT("GameMode: Failed to create dynamic landscape material"));
		return nullptr;
	}

	// Load grass textures and set as parameters
	UTexture2D* GrassD = LoadObject<UTexture2D>(nullptr, TEXT("/Game/Textures/Grass/T_LeafyGrass_D"));
	UTexture2D* GrassN = LoadObject<UTexture2D>(nullptr, TEXT("/Game/Textures/Grass/T_LeafyGrass_N"));
	UTexture2D* GrassR = LoadObject<UTexture2D>(nullptr, TEXT("/Game/Textures/Grass/T_LeafyGrass_R"));

	if (GrassD)
	{
		DynMat->SetTextureParameterValue(TEXT("GrassDiffuse"), GrassD);
		DynMat->SetTextureParameterValue(TEXT("BaseColorTexture"), GrassD);
		DynMat->SetTextureParameterValue(TEXT("Diffuse"), GrassD);
		UE_LOG(LogTemp, Warning, TEXT("GameMode: Set grass diffuse texture on landscape material"));
	}
	if (GrassN)
	{
		DynMat->SetTextureParameterValue(TEXT("GrassNormal"), GrassN);
		DynMat->SetTextureParameterValue(TEXT("NormalTexture"), GrassN);
		DynMat->SetTextureParameterValue(TEXT("Normal"), GrassN);
	}
	if (GrassR)
	{
		DynMat->SetTextureParameterValue(TEXT("GrassRoughness"), GrassR);
		DynMat->SetTextureParameterValue(TEXT("RoughnessTexture"), GrassR);
		DynMat->SetTextureParameterValue(TEXT("Roughness"), GrassR);
	}

	// Set scalar parameters for tiling if the material supports them
	DynMat->SetScalarParameterValue(TEXT("Tiling"), 10.0f);
	DynMat->SetScalarParameterValue(TEXT("UVTiling"), 10.0f);

	UE_LOG(LogTemp, Warning, TEXT("GameMode: Created runtime landscape material with grass textures"));
	return DynMat;
}

void AParamotorGameMode::ScatterGrassMeshes()
{
	// Load grass clump meshes
	UStaticMesh* GrassMesh1 = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Meshes/Grass/SM_GrassClump_01"));
	UStaticMesh* GrassMesh2 = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Meshes/Grass/SM_GrassClump_02"));
	UStaticMesh* GrassMesh3 = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Meshes/Grass/SM_GrassClump_03"));

	if (!GrassMesh1 && !GrassMesh2 && !GrassMesh3)
	{
		UE_LOG(LogTemp, Warning, TEXT("GameMode: No grass meshes found, skipping grass scatter"));
		return;
	}

	// Load grass blade material (high-quality with wind, AO, color variation)
	UMaterialInterface* GrassMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_GrassBlade"));
	if (!GrassMat)
	{
		GrassMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_Grass"));
	}

	// Find the player start to center the grass scatter
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	FVector Center(0.0f, 0.0f, 0.0f);
	if (PC && PC->GetPawn())
	{
		Center = PC->GetPawn()->GetActorLocation();
	}

	// Create instanced static mesh components for each grass mesh type
	TArray<UStaticMesh*> GrassMeshes = { GrassMesh1, GrassMesh2, GrassMesh3 };
	FRandomStream RNG(12345);

	// Dense lawn: 150m radius, 3000 instances per mesh type = 9000 total
	float FieldRadius = 15000.0f;  // 150m radius for wide grass field
	int32 NumPerType = 3000;
	int32 TotalInstances = 0;

	for (int32 MeshIdx = 0; MeshIdx < GrassMeshes.Num(); MeshIdx++)
	{
		UStaticMesh* Mesh = GrassMeshes[MeshIdx];
		if (!Mesh) continue;

		// Spawn a static mesh actor to host the instanced grass
		FActorSpawnParameters GrassSpawnParams;
		GrassSpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AStaticMeshActor* GrassHost = GetWorld()->SpawnActor<AStaticMeshActor>(Center, FRotator::ZeroRotator, GrassSpawnParams);
		if (!GrassHost)
		{
			UE_LOG(LogTemp, Warning, TEXT("GameMode: Failed to spawn grass host actor %d"), MeshIdx);
			continue;
		}
		GrassHost->GetStaticMeshComponent()->SetVisibility(false);
		GrassHost->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

		// Create ISM as a child of the host actor
		UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(GrassHost, FName(*FString::Printf(TEXT("GrassISM_%d"), MeshIdx)));
		ISM->SetupAttachment(GrassHost->GetRootComponent());
		ISM->SetStaticMesh(Mesh);
		if (GrassMat)
		{
			ISM->SetMaterial(0, GrassMat);
		}
		ISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ISM->CastShadow = true;
		ISM->SetMobility(EComponentMobility::Static);
		ISM->SetCastContactShadow(true);
		ISM->SetRenderCustomDepth(false);
		// Enable nanite if possible for better performance with high instance count
		ISM->RegisterComponent();

		// Scatter instances in a dense grid pattern with jitter for natural look
		int32 Placed = 0;
		int32 Attempts = 0;
		int32 MaxAttempts = NumPerType * 3;

		while (Placed < NumPerType && Attempts < MaxAttempts)
		{
			Attempts++;

			// Random position within radius
			float Angle = RNG.FRandRange(0.0f, 2.0f * PI);
			float Dist = FMath::Sqrt(RNG.FRand()) * FieldRadius;  // sqrt for uniform distribution
			FVector Loc = Center + FVector(FMath::Cos(Angle) * Dist, FMath::Sin(Angle) * Dist, 0.0f);

			// Trace down to find ground
			FVector TraceStart = Loc + FVector(0, 0, 5000.0f);
			FVector TraceEnd = Loc + FVector(0, 0, -5000.0f);
			FHitResult Hit;
			FCollisionQueryParams QueryParams;
			QueryParams.bTraceComplex = false;

			if (GetWorld()->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Visibility, QueryParams))
			{
				Loc = Hit.Location;

				// Only place on flat ground (clean-cut lawn on takeoff field)
				float Slope = FMath::Abs(FVector::DotProduct(Hit.Normal, FVector::UpVector));
				if (Slope < 0.85f) continue;  // Only flat areas

				// Short, clean-cut grass scale
				float ScaleXY = RNG.FRandRange(0.6f, 0.9f);
				float ScaleZ = RNG.FRandRange(0.35f, 0.55f);  // Short = clean cut
				FVector FinalScale(ScaleXY, ScaleXY, ScaleZ);

				float Yaw = RNG.FRandRange(0.0f, 360.0f);
				FTransform InstanceTransform(FRotator(0, Yaw, 0), Loc, FinalScale);
				ISM->AddInstance(InstanceTransform);
				TotalInstances++;
				Placed++;
			}
		}

		UE_LOG(LogTemp, Warning, TEXT("GameMode: Mesh %d: placed %d instances (attempted %d)"), MeshIdx, Placed, Attempts);
	}

	UE_LOG(LogTemp, Warning, TEXT("GameMode: Scattered %d grass mesh instances around player (dense lawn)"), TotalInstances);
}
