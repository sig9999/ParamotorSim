// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LandCoverService.h"
#include "VegetationSemantics.h"
#include "VegetationLibrary.h"
#include "VegetationField.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialParameterCollectionInstance;
class UMaterialInterface;

/**
 * AVegetationField — deterministic, geography-driven vegetation for globe maps.
 *
 * Cells are keyed by GEODETIC position (fixed lat/lon grid) — never by map,
 * state, or local coordinate frame. Each cell asks the LandCoverService for the
 * real NLCD class + imperviousness + road descriptor + canopy % at every
 * candidate position, classifies the SEMANTIC vegetation category
 * (VegSem::Classify*), resolves art through UVegetationLibrary, applies
 * exclusions (water/roads/runways/impervious/slope), then spawns
 * deterministic instances from a per-cell hash seed.
 *
 * IMPORTANT: nothing in this file names a mesh asset. The library owns the
 * semantic→art binding; placement/intent lives here and survives art swaps.
 *
 * Two grids: a 120 m tree/shrub grid (~600 m ring) and a 40 m grass grid
 * (~220 m ring). Both thin out with pawn AGL — above ~350 m the grass grid
 * shuts off entirely, above ~900 m the tree ring shrinks.
 */
UCLASS()
class PARAMOTORSIM_API AVegetationField : public AActor
{
	GENERATED_BODY()

public:
	AVegetationField();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	/** Fill a classifier context for a geodetic point (coastal flag, runway
	 *  distance, height). Shared with ASurfaceService so both classify with
	 *  identical geography. False when no land-cover service. */
	bool BuildContextAt(double Lat, double Lon, double GroundH, FVegContext& Out) const;
	/** Land-cover pixel at a point; false if the tile isn't loaded yet. */
	bool GetCoverAt(double Lat, double Lon, FLandCoverPixel& Out) const;

	/** Dev stats for logging / psveg. */
	int32 NumLiveCells = 0;
	int32 NumInstances = 0;
	int32 NumTreeInst = 0;
	int32 NumGrassInst = 0;
	int32 RejExcluded = 0;
	int32 RejNoGround = 0;

	// ---- dev diagnostics -------------------------------------------------
	/** Dump the semantic classification + resolved asset set at lat/lon. */
	void DumpSemanticsAt(double Lat, double Lon);
	/** Runtime asset swap: bind a different mesh to a semantic and force a
	 *  full rebuild — proves the registry is the only binding. */
	void DebugSwap(EVegSemantic Sem, const FString& MeshPath);
	/** Re-resolve everything from the library and rebuild. */
	void DebugReset();
	/** 0 off, 1 = per-cell semantic-colored debug spheres. */
	int32 DebugViz = 0;
	/** Registry dump for PsVegLib. */
	void DumpLibrary();

private:
	// ---- geodetic grid ------------------------------------------------
	static constexpr double CellDeg = 120.0 / 110540.0;      // ~120 m
	static constexpr double GrassCellDeg = 40.0 / 110540.0;  // ~40 m
	struct FCellKey
	{
		int32 X = 0, Y = 0;
		bool operator==(const FCellKey& O) const { return X == O.X && Y == O.Y; }
	};
	friend uint32 GetTypeHash(const FCellKey& K)
	{
		return HashCombine(::GetTypeHash(K.X), ::GetTypeHash(K.Y));
	}

	struct FCell
	{
		// Registered components are kept alive through the owning actor.
		TArray<UInstancedStaticMeshComponent*> Comps;
		int32 Inst = 0;
		float GroundZ = 0.0f;        // probed center ground at spawn (world Z, cm)
		float LastCheckT = 0.0f;     // last ground re-probe time
		uint8 DominantSem = 0;       // EVegSemantic of the cell center (debug viz)
	};

	TMap<FCellKey, FCell> Cells;      // tree/shrub grid
	TMap<FCellKey, FCell> GrassCells; // grass grid

	// ---- runtime ------------------------------------------------------
	UPROPERTY()
	ALandCoverService* LandCover = nullptr;

	FVector LastGeorefOrigin = FVector::ZeroVector;
	FVector LastPawnPos = FVector::ZeroVector;
	double PawnLat = 0.0, PawnLon = 0.0, PawnH = 0.0;
	float PawnAGL = 0.0f;
	bool bHavePawnLLH = false;

	float SpawnBudget = 0.0f;
	float StatsTimer = 0.0f;
	float DetailBoost = 0.0f;  // 0..1 regional fidelity mask (Rhode Island)

	// ---- cell streaming ------------------------------------------------
	void UpdatePawnState();
	void UpdateCells();
	void PopulateCell(const FCellKey& Key, bool bGrass);
	void DespawnCell(TMap<FCellKey, FCell>& Map, const FCellKey& Key);
	void DespawnAll();
	void DrawDebugCells();

	// ---- placement ------------------------------------------------------
	// Convert a geodetic point to a world position near the pawn using the
	// pawn's local east/south/up frame (accurate to cm within ~10 km).
	FVector LLToUnrealNearPawn(double Lat, double Lon, double HeightM) const;
	void UnrealToLLNearPawn(const FVector& Pos, double& OutLat, double& OutLon) const;
	bool PawnLLH(FVector& OutLLH);

	void SpawnTreeCellContent(const FCellKey& Key, FCell& Cell, double CenLat, double CenLon);
	bool SpawnGrassCellContent(const FCellKey& Key, FCell& Cell, double CenLat, double CenLon);

	/** Ground Z via downward trace; false if nothing hit. */
	bool GroundAt(const FVector& XY, FVector& OutPos, FVector& OutNormal);

	/** Context for the semantic classifiers: coastal flag + runway distance. */
	FVegContext MakeCtx(double Lat, double Lon, double GroundH) const;
	bool IsCoastal(double Lat, double Lon, double RadiusM) const;

	/** Combined exclusion test: land cover + impervious + roads + runway +
	 *  slope. Returns false when vegetation of the given kind is excluded. */
	bool AllowedAt(double Lat, double Lon, bool bTree, const FLandCoverPixel& Px, float SlopeDeg);

	// ---- semantic → asset resolution (registry is the only binding) ----
	const FVegRuntimeSet* ResolveSet(EVegSemantic Sem);
	UPROPERTY()
	UVegetationLibrary* Library = nullptr;
	TMap<uint8, FVegRuntimeSet> ResolvedSets; // key = (uint8)EVegSemantic
	UPROPERTY()
	TArray<UStaticMesh*> ResolvedMeshKeepAlive;
	UPROPERTY()
	TArray<UMaterialInterface*> ResolvedMatKeepAlive;

	UInstancedStaticMeshComponent* MakeISM(UStaticMesh* Mesh, const FVector& CellOrigin, bool bCastShadow);

	// ---- wind / prop wash ------------------------------------------------
	void UpdateWindParams();
	UPROPERTY()
	UMaterialParameterCollectionInstance* VegMPC = nullptr;
	UPROPERTY()
	UMaterialParameterCollection* VegMPCAsset = nullptr;
	UMaterialParameterCollection* LoadVegMPC();
	bool bVegParamsChecked = false;
	bool bHasWindDir = false, bHasWindSpeed = false, bHasGust = false, bHasTime = false;
	bool bHasPropPos = false, bHasPropDir = false, bHasPropStr = false;

	// ---- runways ---------------------------------------------------------
	struct FRunway { float Lat1, Lon1, Lat2, Lon2, HalfWidthM; int32 Closed; };
	TArray<FRunway> Runways;
	TMap<int64, TArray<int32>> RunwayGrid; // 0.1° cell → runway indices
	void LoadRunways();
	float RunwayDistM(double Lat, double Lon) const; // -1 if no runway near
};
