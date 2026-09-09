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
};
