// Copyright Epic Games, Inc. All Rights Reserved.

#include "ParamotorEditorUtils.h"
#include "Misc/FileHelper.h"

#if WITH_EDITOR
#include "Landscape.h"
#include "LandscapeProxy.h"
#include "Editor.h"
#include "Materials/Material.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#endif

bool UParamotorEditorUtils::ScatterStaticMeshInstances(const FString& CsvPath, const FString& MeshAssetPath, const FString& ActorLabel)
{
#if WITH_EDITOR
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) return false;
	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *MeshAssetPath);
	if (!Mesh)
	{
		UE_LOG(LogTemp, Error, TEXT("Scatter: mesh not found %s"), *MeshAssetPath);
		return false;
	}
	FString Content;
	if (!FFileHelper::LoadFileToString(Content, *CsvPath))
	{
		UE_LOG(LogTemp, Error, TEXT("Scatter: cannot read %s"), *CsvPath);
		return false;
	}
	TArray<FTransform> Xf;
	TArray<FString> Lines;
	Content.ParseIntoArrayLines(Lines);
	for (const FString& L : Lines)
	{
		TArray<FString> P;
		L.ParseIntoArray(P, TEXT(","));
		if (P.Num() < 5) continue;
		Xf.Add(FTransform(FRotator(0.f, FCString::Atof(*P[3]), 0.f),
			FVector(FCString::Atof(*P[0]), FCString::Atof(*P[1]), FCString::Atof(*P[2])),
			FVector(FCString::Atof(*P[4]))));
	}
	if (Xf.Num() == 0) return false;

	AActor* A = World->SpawnActor<AActor>();
	if (!A) return false;
	A->SetActorLabel(*ActorLabel);
	UInstancedStaticMeshComponent* C = NewObject<UInstancedStaticMeshComponent>(A);
	C->CreationMethod = EComponentCreationMethod::Instance;
	A->AddInstanceComponent(C);
	A->SetRootComponent(C);
	C->RegisterComponent();
	C->SetStaticMesh(Mesh);
	C->SetMobility(EComponentMobility::Static);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->AddInstances(Xf, false);
	UE_LOG(LogTemp, Warning, TEXT("Scatter: %s -> %d instances of %s"), *ActorLabel, Xf.Num(), *MeshAssetPath);
	return true;
#else
	return false;
#endif
}

bool UParamotorEditorUtils::ConfigureDecalMaterial(UMaterial* Mat)
{
#if WITH_EDITOR
	if (!Mat) return false;
	Mat->MaterialDomain = MD_DeferredDecal;
	Mat->BlendMode = BLEND_Translucent;
	Mat->DecalBlendMode = EDecalBlendMode::DBM_Translucent;
	Mat->PostEditChange();
	Mat->MarkPackageDirty();
	UE_LOG(LogTemp, Warning, TEXT("ConfigureDecalMaterial: done on %s"), *Mat->GetName());
	return true;
#else
	return false;
#endif
}

bool UParamotorEditorUtils::ImportLandscapeFromRawHeightmap(const FString& R16Path, int32 SizeX, int32 SizeY,
	int32 QuadsPerSection, int32 SectionsPerComponent, float ScaleXY, float ScaleZ, bool bCenterAtOrigin)
{
#if WITH_EDITOR
	TArray<uint8> Raw;
	if (!FFileHelper::LoadFileToArray(Raw, *R16Path))
	{
		UE_LOG(LogTemp, Error, TEXT("LandscapeImport: cannot read %s"), *R16Path);
		return false;
	}
	const int64 Expected = (int64)SizeX * SizeY * 2;
	if (Raw.Num() != Expected)
	{
		UE_LOG(LogTemp, Error, TEXT("LandscapeImport: %s has %lld bytes, expected %lld (%dx%d uint16)"),
			*R16Path, (int64)Raw.Num(), Expected, SizeX, SizeY);
		return false;
	}

	TArray<uint16> HeightData;
	HeightData.SetNumUninitialized(SizeX * SizeY);
	FMemory::Memcpy(HeightData.GetData(), Raw.GetData(), Raw.Num());

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("LandscapeImport: no editor world"));
		return false;
	}

	const int32 QuadsPerComponent = QuadsPerSection * SectionsPerComponent;
	const int32 CompX = (SizeX - 1) / QuadsPerComponent;
	const int32 CompY = (SizeY - 1) / QuadsPerComponent;

	FVector Offset = FVector::ZeroVector;
	if (bCenterAtOrigin)
	{
		Offset = FTransform(FRotator::ZeroRotator, FVector::ZeroVector,
			FVector(ScaleXY, ScaleXY, ScaleZ)).TransformVector(
			FVector(-CompX * QuadsPerComponent / 2.0, -CompY * QuadsPerComponent / 2.0, 0.0));
	}

	ALandscape* Landscape = World->SpawnActor<ALandscape>(Offset, FRotator::ZeroRotator);
	if (!Landscape)
	{
		UE_LOG(LogTemp, Error, TEXT("LandscapeImport: spawn failed"));
		return false;
	}
	Landscape->SetActorRelativeScale3D(FVector(ScaleXY, ScaleXY, ScaleZ));
	Landscape->StaticLightingLOD = 3;

	TMap<FGuid, TArray<uint16>> HeightDataPerLayers;
	HeightDataPerLayers.Add(FGuid(), MoveTemp(HeightData));
	TMap<FGuid, TArray<FLandscapeImportLayerInfo>> EmptyLayers;
	EmptyLayers.Add(FGuid(), TArray<FLandscapeImportLayerInfo>());

	Landscape->Import(FGuid::NewGuid(), 0, 0, SizeX - 1, SizeY - 1,
		SectionsPerComponent, QuadsPerSection, HeightDataPerLayers, *R16Path,
		EmptyLayers, ELandscapeImportAlphamapType::Additive,
		TArrayView<const FLandscapeLayer>());

	if (ULandscapeInfo* Info = Landscape->GetLandscapeInfo())
	{
		Info->UpdateLayerInfoMap(Landscape);
	}

	UE_LOG(LogTemp, Warning, TEXT("LandscapeImport: done — %dx%d verts, %dx%d components, scale %.0f/%.0f/%.0f"),
		SizeX, SizeY, CompX, CompY, ScaleXY, ScaleXY, ScaleZ);
	return true;
#else
	UE_LOG(LogTemp, Error, TEXT("LandscapeImport: editor-only function"));
	return false;
#endif
}

void UParamotorEditorUtils::DumpMeshSections(const FString& MeshAssetPath)
{
#if WITH_EDITOR
	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *MeshAssetPath);
	if (!Mesh) { UE_LOG(LogTemp, Error, TEXT("DumpMeshSections: no mesh %s"), *MeshAssetPath); return; }
	if (FStaticMeshRenderData* RD = Mesh->GetRenderData())
	{
		if (RD->LODResources.Num() > 0)
		{
			FStaticMeshLODResources& LOD = RD->LODResources[0];
			for (int32 i = 0; i < LOD.Sections.Num(); ++i)
			{
				const FStaticMeshSection& S = LOD.Sections[i];
				UMaterialInterface* M = Mesh->GetMaterial(S.MaterialIndex);
				UE_LOG(LogTemp, Warning, TEXT("Sec%d tris=%d mat=%s"),
					i, S.NumTriangles, M ? *M->GetPathName() : TEXT("none"));
			}
		}
	}
	else { UE_LOG(LogTemp, Error, TEXT("DumpMeshSections: no render data")); }
#endif
}
