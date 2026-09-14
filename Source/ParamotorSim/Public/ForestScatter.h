// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ForestScatter.generated.h"

class UInstancedStaticMeshComponent;
class UStaticMesh;
class APawn;

/**
 * AForestScatter — runtime tree scatter driven by a forest-density grid
 * built from real OSM woodland polygons (gen_forest_grid.py).
 *
 * The grid file (Content/Data/forests/<name>.bin) stores canopy density in
 * 60m cells over a UTM region. Each frame the actor keeps ~1200m of cells
 * live around the pawn, hashing each forested cell into a few deterministic
 * tree positions. Trees only exist near the player — the whole region can be
 * dressed for the price of a few thousand instances.
 *
 * Swap TreeMeshes to replace the placeholder foliage with real assets.
 */
UCLASS()
class PARAMOTORSIM_API AForestScatter : public AActor
{
	GENERATED_BODY()

public:
	AForestScatter();
	virtual void Tick(float DeltaTime) override;

	/** Load a forest grid and start scattering. Origin = UTM coords of the
	 *  map's local (0,0). Returns false if the file is missing/bad. */
	bool LoadGrid(const FString& Path, double MapOriginUtmE, double MapOriginUtmN);

	/** Tree meshes to scatter (placeholder until real foliage assets land). */
	UPROPERTY(EditAnywhere)
	TArray<TObjectPtr<UStaticMesh>> TreeMeshes;

	/** Live scatter radius around the pawn, meters. */
	UPROPERTY(EditAnywhere)
	float ScatterRadiusM = 1200.0f;

	/** Candidate positions per 480m live cell — density-filtered, so the
	 *  effective count is roughly this * forest fraction. */
	UPROPERTY(EditAnywhere)
	int32 TreesPerCell = 20;

private:
	struct FCellKey
	{
		int32 X = 0, Y = 0;
		bool operator==(const FCellKey& O) const { return X == O.X && Y == O.Y; }
		friend uint32 GetTypeHash(const FCellKey& K) { return HashCombine(K.X, K.Y); }
	};

	// Grid data (grid space = meters easting/northing of UTM region origin).
	TArray<uint8> Density;
	int32 GridCols = 0, GridRows = 0;
	double GridOriginUtmE = 0.0, GridOriginUtmN = 0.0;
	double MapOriginUtmE = 0.0, MapOriginUtmN = 0.0;
	float GridCellM = 60.0f;
	// Live-cell granularity (bigger than the data cells — one ISM per cell).
	float CellSizeCm = 48000.0f;

	// One ISM per live cell.
	TMap<FCellKey, UInstancedStaticMeshComponent*> LiveCells;
	float RepopulateTimer = 0.0f;
	UPROPERTY()
	APawn* CachedPawn = nullptr;

	uint8 DensityAt(int32 C, int32 R) const;
	void Repopulate(APawn* Pawn);
	void SpawnCell(const FCellKey& K);
	void DespawnCell(UInstancedStaticMeshComponent* ISM);
};
