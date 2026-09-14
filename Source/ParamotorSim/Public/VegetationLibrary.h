// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "VegetationSemantics.h"
#include "VegetationLibrary.generated.h"

class UStaticMesh;
class UMaterialInterface;
class UPhysicalMaterial;

/**
 * UVegetationLibrary — the single replaceable asset registry.
 *
 * ONE DataAsset maps every EVegSemantic to the art that currently
 * represents it. Procedural systems never name a mesh — they resolve a
 * semantic through this library. To upgrade the world's vegetation to a
 * premium pack later: import the pack, open /Game/Data/DA_VegetationLibrary,
 * reassign the meshes under the relevant semantic entries, save. No
 * repaint, no code change, no placement rebuild.
 *
 * Mesh references are SOFT — a semantic's assets only load when a cell
 * actually classifies to that semantic.
 *
 * LOD tiers are independent per entry: NearMeshes / MidMeshes / FarMeshes.
 * A category may leave later tiers empty (they fall back to the previous
 * tier) until better assets arrive.
 *
 * Interaction contract: all vegetation materials should sample the shared
 * MPC_Veg parameter collection (WindDir, WindSpeed, GustSpeed, VegTime,
 * PropPos, PropDir, PropStrength — fed by AVegetationField::UpdateWindParams)
 * so replacement assets inherit wind + prop-wash for free.
 */

USTRUCT(BlueprintType)
struct FVegAssetSet
{
	GENERATED_BODY()

	/** High-detail meshes used near the pawn. Multiple entries = variants,
	 *  picked deterministically per placement seed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<TSoftObjectPtr<UStaticMesh>> NearMeshes;

	/** Lower-cost meshes for the mid band (beyond NearRadiusM). Empty → reuse Near. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<TSoftObjectPtr<UStaticMesh>> MidMeshes;

	/** Cheapest representation for the far band. Empty → reuse Mid/Near. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<TSoftObjectPtr<UStaticMesh>> FarMeshes;

	/** Optional material slot-0 overrides applied to this category's ISMs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<TSoftObjectPtr<UMaterialInterface>> MaterialOverrides;

	/** Uniform-scale range applied per instance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector2D ScaleRange = FVector2D(0.8f, 1.3f);

	/** Multiplies the placement density the classifier asked for — an art
	 *  knob, not an identity knob (clumping meshes want <1, sparse <1…). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0"))
	float DensityScale = 1.0f;

	/** Per-instance "dryness" range written to custom data 0 (straw mix). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector2D DrynessRange = FVector2D(0.0f, 1.0f);

	/** Slope ceiling for this category's placements. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0", ClampMax = "90.0"))
	float MaxSlopeDeg = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float GroundClearanceM = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bCastShadow = true;

	/** Distance beyond which NearMeshes stop being used (Mid takes over). 0 = always near. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0"))
	float NearRadiusM = 250.0f;

	/** Beyond this the category isn't rendered at all. 0 = unlimited. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0"))
	float MaxDrawDistanceM = 0.0f;
};

/** Resolved (loaded) view of one set — runtime cache, not saved. */
struct FVegRuntimeSet
{
	TArray<UStaticMesh*> Near;
	TArray<UStaticMesh*> Mid;
	TArray<UStaticMesh*> Far;
	TArray<UMaterialInterface*> Mats;
	FVector2D ScaleRange = FVector2D(0.8f, 1.3f);
	FVector2D DrynessRange = FVector2D(0.0f, 1.0f);
	float DensityScale = 1.0f;
	float MaxSlopeDeg = 50.0f;
	float GroundClearanceM = 0.0f;
	float NearRadiusM = 250.0f;
	float MaxDrawDistanceM = 0.0f;
	bool bCastShadow = true;
};

UCLASS(BlueprintType)
class PARAMOTORSIM_API UVegetationLibrary : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Semantic → art binding. Missing entries fall back to code defaults. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TMap<EVegSemantic, FVegAssetSet> Categories;

	/** Optional physical materials per surface semantic (footsteps/tracks later). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TMap<ESurfaceSemantic, TSoftObjectPtr<UPhysicalMaterial>> SurfacePhysMats;

	/** The live registry: /Game/Data/DA_VegetationLibrary if it exists,
	 *  else a transient code-built default so the sim never breaks. */
	static UVegetationLibrary* Get();

	/** Resolve a semantic to loaded meshes. Tier fallbacks handled. */
	void Resolve(EVegSemantic Sem, float DistM, FVegRuntimeSet& Out) const;

	/** Full resolve (all tiers) — used by the field's per-semantic cache. */
	void ResolveAll(EVegSemantic Sem, FVegRuntimeSet& Out) const;

	/** Populate `Categories` with the built-in default bindings (current
	 *  project meshes). Used for the transient fallback AND to seed the
	 *  editor-created asset. */
	UFUNCTION(BlueprintCallable, CallInEditor)
	void BuildDefaults();

private:
	void ResolveInto(const TArray<TSoftObjectPtr<UStaticMesh>>& Src, TArray<UStaticMesh*>& Dst) const;
};
