// Copyright Epic Games, Inc. All Rights Reserved.

#include "VegetationLibrary.h"
#include "ParamotorLogging.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"

static UVegetationLibrary* GCachedLib = nullptr;

UVegetationLibrary* UVegetationLibrary::Get()
{
	if (GCachedLib && IsValid(GCachedLib)) return GCachedLib;
	GCachedLib = LoadObject<UVegetationLibrary>(nullptr, TEXT("/Game/Data/DA_VegetationLibrary"));
	if (!GCachedLib)
	{
		// Transient code default — the sim works before the DataAsset exists
		// and on cooks where it was never packaged.
		GCachedLib = NewObject<UVegetationLibrary>(GetTransientPackage(), TEXT("VegLibrary_Default"));
		GCachedLib->BuildDefaults();
		GCachedLib->AddToRoot();
		UE_LOG(LogParamotor, Log, TEXT("VegetationLibrary: /Game/Data/DA_VegetationLibrary missing — using code defaults"));
	}
	else
	{
		GCachedLib->AddToRoot();
		UE_LOG(LogParamotor, Log, TEXT("VegetationLibrary: using /Game/Data/DA_VegetationLibrary (%d categories)"),
			GCachedLib->Categories.Num());
	}
	return GCachedLib;
}

void UVegetationLibrary::ResolveInto(const TArray<TSoftObjectPtr<UStaticMesh>>& Src, TArray<UStaticMesh*>& Dst) const
{
	Dst.Reset(Src.Num());
	for (const TSoftObjectPtr<UStaticMesh>& S : Src)
		if (UStaticMesh* M = S.LoadSynchronous()) Dst.Add(M);
}

void UVegetationLibrary::ResolveAll(EVegSemantic Sem, FVegRuntimeSet& Out) const
{
	const FVegAssetSet* Set = Categories.Find(Sem);
	if (!Set)
	{
		// Fall back to the code default for this semantic.
		UVegetationLibrary* Def = NewObject<UVegetationLibrary>(GetTransientPackage());
		Def->BuildDefaults();
		Set = Def->Categories.Find(Sem);
		if (!Set) return;
	}
	Out = FVegRuntimeSet();
	ResolveInto(Set->NearMeshes, Out.Near);
	ResolveInto(Set->MidMeshes, Out.Mid);
	ResolveInto(Set->FarMeshes, Out.Far);
	for (const TSoftObjectPtr<UMaterialInterface>& M : Set->MaterialOverrides)
		if (UMaterialInterface* MI = M.LoadSynchronous()) Out.Mats.Add(MI);
	Out.ScaleRange = Set->ScaleRange;
	Out.DrynessRange = Set->DrynessRange;
	Out.DensityScale = Set->DensityScale;
	Out.MaxSlopeDeg = Set->MaxSlopeDeg;
	Out.GroundClearanceM = Set->GroundClearanceM;
	Out.NearRadiusM = Set->NearRadiusM;
	Out.MaxDrawDistanceM = Set->MaxDrawDistanceM;
	Out.bCastShadow = Set->bCastShadow;
	if (Out.Mid.IsEmpty()) Out.Mid = Out.Near;
	if (Out.Far.IsEmpty()) Out.Far = Out.Mid;
}

void UVegetationLibrary::Resolve(EVegSemantic Sem, float DistM, FVegRuntimeSet& Out) const
{
	ResolveAll(Sem, Out);
	if (DistM > Out.NearRadiusM && Out.Mid.Num()) Out.Near = Out.Mid;
}

void UVegetationLibrary::BuildDefaults()
{
	Categories.Empty();
	// Serialized soft paths need the full "Package.AssetName" form — a bare
	// package path loads via LoadObject but fails LoadSynchronous after
	// serialization into a .uasset.
	auto Soft = [](const TCHAR* P)
	{
		return TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(
			FString::Printf(TEXT("%s.%s"), P, *FPackageName::GetShortName(P))));
	};
	auto SoftMat = [](const TCHAR* P)
	{
		return TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(
			FString::Printf(TEXT("%s.%s"), P, *FPackageName::GetShortName(P))));
	};

	const TCHAR* Firs[] = {
		TEXT("/Game/Meshes/trees/fir_tree_01_a_LOD0"),
		TEXT("/Game/Meshes/trees/fir_tree_01_b_LOD0"),
		TEXT("/Game/Meshes/trees/fir_tree_01_c_LOD0"),
	};
	const TCHAR* Pines[] = {
		TEXT("/Game/Meshes/trees/pine_tree_01_a_LOD0"),
		TEXT("/Game/Meshes/trees/pine_tree_01_b_LOD0"),
		TEXT("/Game/Meshes/trees/pine_tree_01_c_LOD0"),
	};
	const TCHAR* Hardwoods[] = {
		TEXT("/Game/Meshes/gltf_test/island_tree_01_1k/StaticMeshes/island_tree_01_1k"),
		TEXT("/Game/Meshes/gltf_test/island_tree_02_1k/StaticMeshes/island_tree_02_1k"),
		TEXT("/Game/Meshes/gltf_test/jacaranda_tree_1k/StaticMeshes/jacaranda_tree_1k"),
	};
	const TCHAR* Grass = TEXT("/Game/Meshes/veg/SM_GrassClump");

	auto TreeSet = [&](TArray<const TCHAR*> List, float SMin, float SMax)
	{
		FVegAssetSet S;
		for (const TCHAR* P : List) S.NearMeshes.Add(Soft(P));
		S.ScaleRange = FVector2D(SMin, SMax);
		S.DrynessRange = FVector2D(0.0f, 0.0f);
		S.MaxSlopeDeg = 45.0f;
		S.bCastShadow = true;
		return S;
	};
	auto GrassSet = [&](float Density, float SMin, float SMax, FVector2D Dry, float NearR)
	{
		FVegAssetSet S;
		S.NearMeshes.Add(Soft(Grass));
		S.MaterialOverrides.Add(SoftMat(TEXT("/Game/Materials/M_GrassClump")));
		S.ScaleRange = FVector2D(SMin, SMax);
		S.DrynessRange = Dry;
		S.DensityScale = Density;
		S.MaxSlopeDeg = 55.0f;
		S.bCastShadow = false;
		S.NearRadiusM = NearR;
		return S;
	};

	// ---- trees ----
	{
		FVegAssetSet S = TreeSet({ Hardwoods[0], Hardwoods[1], Hardwoods[2] }, 0.7f, 1.5f);
		Categories.Add(EVegSemantic::Tree_Deciduous, S);
	}
	{
		FVegAssetSet S = TreeSet({ Firs[0], Firs[1], Firs[2], Pines[0], Pines[1], Pines[2] }, 0.7f, 1.5f);
		Categories.Add(EVegSemantic::Tree_Conifer, S);
	}
	{
		// Wetland trees — hardwoods slightly smaller.
		FVegAssetSet S = TreeSet({ Hardwoods[0], Hardwoods[1] }, 0.6f, 1.2f);
		Categories.Add(EVegSemantic::Tree_Wetland, S);
	}
	{
		// Maritime scrub — same hardwoods, stunted + wind-sculpted scale.
		FVegAssetSet S = TreeSet({ Hardwoods[1] }, 0.4f, 0.9f);
		Categories.Add(EVegSemantic::Tree_Coastal, S);
	}
	{
		FVegAssetSet S = TreeSet({ Hardwoods[0], Hardwoods[1], Hardwoods[2] }, 0.3f, 0.6f);
		Categories.Add(EVegSemantic::Tree_Sapling, S);
	}
	{
		// Dead trees — conifer trunks render bare well enough for now.
		FVegAssetSet S = TreeSet({ Pines[0] }, 0.6f, 1.1f);
		Categories.Add(EVegSemantic::Tree_Dead, S);
	}

	// ---- shrubs (deciduous meshes at scrub scale until real shrub assets land)
	auto ShrubSet = [&](const TCHAR* Mesh, float SMin, float SMax)
	{
		FVegAssetSet S;
		S.NearMeshes.Add(Soft(Mesh));
		S.ScaleRange = FVector2D(SMin, SMax);
		S.DrynessRange = FVector2D(0.0f, 0.35f);
		S.MaxSlopeDeg = 50.0f;
		S.bCastShadow = false;
		return S;
	};
	Categories.Add(EVegSemantic::Shrub_Low,        ShrubSet(Hardwoods[1], 0.10f, 0.22f));
	Categories.Add(EVegSemantic::Shrub_Coastal,    ShrubSet(Hardwoods[1], 0.08f, 0.18f));
	Categories.Add(EVegSemantic::Shrub_Wetland,    ShrubSet(Hardwoods[0], 0.10f, 0.22f));
	Categories.Add(EVegSemantic::Shrub_Understory, ShrubSet(Hardwoods[2], 0.08f, 0.18f));

	// ---- grass family — one current mesh, DIFFERENT semantic presentation
	// (density/scale/dryness are the placeholders' differentiation until a
	// real grass pack replaces them).
	Categories.Add(EVegSemantic::Grass_AirportMowed,
		GrassSet(0.85f, 1.1f, 1.8f, FVector2D(0.05f, 0.35f), 300.0f));
	Categories.Add(EVegSemantic::Grass_Mowed,
		GrassSet(0.80f, 1.1f, 1.9f, FVector2D(0.05f, 0.35f), 300.0f));
	Categories.Add(EVegSemantic::Grass_NaturalShort,
		GrassSet(0.90f, 1.3f, 2.3f, FVector2D(0.10f, 0.60f), 260.0f));
	Categories.Add(EVegSemantic::Grass_NaturalTall,
		GrassSet(1.00f, 1.6f, 2.9f, FVector2D(0.20f, 0.85f), 260.0f));
	Categories.Add(EVegSemantic::Grass_Pasture,
		GrassSet(0.95f, 1.4f, 2.5f, FVector2D(0.15f, 0.70f), 260.0f));
	Categories.Add(EVegSemantic::Grass_Cropland,
		GrassSet(0.55f, 1.2f, 2.0f, FVector2D(0.25f, 0.80f), 240.0f));
	Categories.Add(EVegSemantic::Grass_Coastal,
		GrassSet(0.85f, 1.3f, 2.4f, FVector2D(0.30f, 0.90f), 260.0f));
	Categories.Add(EVegSemantic::Grass_Dune,
		GrassSet(0.45f, 1.2f, 2.2f, FVector2D(0.50f, 0.95f), 220.0f)); // sparse beachgrass
	Categories.Add(EVegSemantic::Grass_Wetland,
		GrassSet(0.90f, 1.6f, 2.8f, FVector2D(0.00f, 0.35f), 260.0f)); // lush reeds
	Categories.Add(EVegSemantic::Grass_ForestFloor,
		GrassSet(0.40f, 0.9f, 1.7f, FVector2D(0.10f, 0.50f), 200.0f));
}
