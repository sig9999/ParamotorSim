// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SurfaceLibrary.h"
#include "SurfaceService.generated.h"

class ALandCoverService;
class AVegetationField;
class AParamotorOceanSystem;
class UProceduralMeshComponent;
class UMaterialInstanceDynamic;
struct FLandCoverPixel;

/**
 * ASurfaceService — the live surface-semantics resolver.
 *
 * One actor (spawned on globe maps beside LandCoverService +
 * AVegetationField) answers "what surface is here" for every consumer:
 * footsteps, the sand system, prop wash, future tracks/skids/crash contact.
 *
 * Pipeline:  geodetic position → NLCD pixel + context (coastal/runway/
 * height) → ESurfaceSemantic → USurfaceLibrary → FResolvedSurface
 * (physical type, interaction scalars, bound materials, wetness).
 *
 * Wetness is layered on top of classification: waterline proximity (the same
 * curve the sand overlay uses) plus METAR precipitation, then a semantic's
 * WetCounterpart (Dirt→Mud, DrySand→WetSand) re-labels the effective surface.
 *
 * It also owns the optional near-field DETAIL PATCH — a pawn-following
 * translucent overlay disc that renders a semantic's DetailPatchMaterial for
 * close-range fidelity. Most categories bind none (real photogrammetry stays
 * dominant); binding one is the "make this surface look better up close" knob.
 */
UCLASS()
class PARAMOTORSIM_API ASurfaceService : public AActor
{
	GENERATED_BODY()

public:
	ASurfaceService();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	// ---- resolution -------------------------------------------------

	/** Resolve the surface at a geodetic point. GroundZM is the point's
	 *  geodetic height in metres (0 = unknown → waterline tests use the
	 *  traced/world ground Z instead when possible). */
	bool ResolveAtLL(double Lat, double Lon, double GroundZM, FResolvedSurface& Out) const;

	/** Resolve the surface at a world position. Converts to geodetic, samples
	 *  land cover, then refines against the traced/local ground Z and the
	 *  water datum. Pass a precomputed ground Z (cm) to skip the trace. */
	bool ResolveAtWorldPos(const FVector& Pos, FResolvedSurface& Out,
		float KnownGroundZ = TNumericLimits<float>::Max()) const;

	/** Last surface resolved under the player pawn (refreshed in Tick). */
	const FResolvedSurface& GetPawnSurface() const { return PawnSurface; }

	/** Combined wetness 0..1 at a world position: waterline proximity +
	 *  METAR precipitation + semantic bias. GroundZ in world cm. */
	float WetnessAt(const FVector& Pos, float GroundZ) const;

	/** METAR-derived precipitation wetness (0 = dry air). */
	float WeatherWetness() const;

	/** The actor instance (first one in the world), or null off-globe. */
	static ASurfaceService* Get(UWorld* World);

	// ---- dev diagnostics ---------------------------------------------

	/** Dump the full resolved surface at the pawn for PsSurfGeo. */
	void DumpAt(const FVector& Pos);
	/** Registry dump for PsSurfLib. */
	void DumpLibrary() const;
	/** Runtime material swap — bind a different DetailPatchMaterial to a
	 *  semantic without touching classification. Proves the registry. */
	void DebugSwap(ESurfaceSemantic Sem, const FString& MatPath);
	/** Re-resolve from the library asset (undo debug swaps). */
	void DebugReset();
	/** 0 off, 1 = semantic-colored ground discs around the pawn. */
	int32 DebugViz = 0;

private:
	UPROPERTY() ALandCoverService* LandCover = nullptr;
	UPROPERTY() AVegetationField* VegField = nullptr;
	UPROPERTY() AParamotorOceanSystem* Ocean = nullptr;
	UPROPERTY() USurfaceLibrary* Library = nullptr;

	FResolvedSurface PawnSurface;
	float ResolveTimer = 0.0f;
	FVector LastResolvePos = FVector::ZeroVector;

	/** Do the work and fill Out — shared by the LL and world-pos paths.
	 *  Pos is the corresponding world position (for water datum queries). */
	bool ResolveCore(const FLandCoverPixel& Px, double Lat, double Lon,
		double GroundZM, const FVector& Pos, float GroundWorldZ, FResolvedSurface& Out) const;

	/** Pre-swap set backups so DebugReset restores without touching the DA. */
	TMap<uint8, FSurfaceAssetSet> SwappedBackups;

	// ---- near-field detail patch -------------------------------------

	UPROPERTY() UProceduralMeshComponent* DetailPatch = nullptr;
	UPROPERTY() UMaterialInstanceDynamic* DetailMID = nullptr;
	ESurfaceSemantic DetailSemantic = ESurfaceSemantic::Unknown;
	void BuildDetailPatch(float RadiusM);
	void UpdateDetailPatch(APawn* Pawn);

	// ---- debug --------------------------------------------------------

	void DrawDebugSurface();
	static FColor DebugColorFor(ESurfaceSemantic S);
};
