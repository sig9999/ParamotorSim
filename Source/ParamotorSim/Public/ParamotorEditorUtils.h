// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ParamotorEditorUtils.generated.h"

/** Editor-only helpers for building the real-terrain Westerly map.
 *  Callable from Unreal Python scripts (editor builds only). */
UCLASS()
class PARAMOTORSIM_API UParamotorEditorUtils : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Spawn an ALandscape in the current editor world and import a raw 16-bit
	 *  little-endian heightmap (.r16). SizeX/SizeY = full vertex counts.
	 *  Heightmap convention: value = 32768 + elev_m * 128 (Z scale 100 => ~0.78cm/unit).
	 *  Returns true on success. */
	UFUNCTION(BlueprintCallable, Category = "Landscape Import")
	static bool ImportLandscapeFromRawHeightmap(const FString& R16Path, int32 SizeX, int32 SizeY,
		int32 QuadsPerSection, int32 SectionsPerComponent, float ScaleXY, float ScaleZ, bool bCenterAtOrigin);

	/** Configure a material as a deferred-decal material (DBuffer color) — protected from Python. */
	UFUNCTION(BlueprintCallable, Category = "Landscape Import")
	static bool ConfigureDecalMaterial(class UMaterial* Mat);

	/** Scatter a static mesh as ISM instances in the current level.
	 *  CsvPath lines: x,y,z,yawDeg,scale  (world cm). */
	UFUNCTION(BlueprintCallable, Category = "Landscape Import")
	static bool ScatterStaticMeshInstances(const FString& CsvPath, const FString& MeshAssetPath, const FString& ActorLabel);

	/** Log per-section triangle counts + material for a static mesh (leaf debugging). */
	UFUNCTION(BlueprintCallable, Category = "Landscape Import")
	static void DumpMeshSections(const FString& MeshAssetPath);

	/** Connect a material expression output to Pixel Depth Offset — the enum
	 *  isn't exposed to Python's MaterialProperty list. */
	UFUNCTION(BlueprintCallable, Category = "Landscape Import")
	static bool ConnectPixelDepthOffset(class UMaterial* Mat, class UMaterialExpression* Expr);

	/** One-shot: parent the ocean MI on the plugin Water_Material, enable foam/
	    transition/caustics, apply coastal water colors, update the permutation. */
	UFUNCTION(BlueprintCallable, Category = "Landscape Import")
	static bool ConfigureOceanInstance(class UMaterialInstanceConstant* MI);

	/** Add a scalar parameter to a Material Parameter Collection if missing.
	 *  Python can't create FGuid-initialized collection params. */
	UFUNCTION(BlueprintCallable, Category = "Landscape Import")
	static bool AddCollectionScalar(class UMaterialParameterCollection* MPC, const FString& Name, float DefaultValue);

	/** Add a vector parameter to a Material Parameter Collection if missing. */
	UFUNCTION(BlueprintCallable, Category = "Landscape Import")
	static bool AddCollectionVector(class UMaterialParameterCollection* MPC, const FString& Name);

	/** Log every expression class in a material + whether WorldPositionOffset
	    is already connected. For inspecting materials Python can't read. */
	UFUNCTION(BlueprintCallable, Category = "Landscape Import")
	static void DumpMaterialGraph(class UMaterial* Mat);

	/** Delete CollectionParameter expressions whose ParameterId is unbound
	    (left by a crashed sink run — they fail material compilation). */
	UFUNCTION(BlueprintCallable, Category = "Landscape Import")
	static int32 PruneInvalidCollectionParams(class UMaterial* Mat);

	/** Wire the sand-sink WPO into the landscape material: inside the deform
	    window the landscape drops by (deform - SinkCm) so it can't poke
	    through the detailed sand overlay. Safe to call repeatedly. */
	UFUNCTION(BlueprintCallable, Category = "Landscape Import")
	static bool EnsureLandscapeSandSink(class UMaterial* Mat);
};
