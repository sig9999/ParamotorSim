// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VegetationSemantics.generated.h"

struct FLandCoverPixel;

/**
 * Vegetation + surface SEMANTIC layer.
 *
 * The world classifies geography into these categories — "this is natural
 * tall grass", "this is a marsh edge", "this is a paved road" — and the
 * procedural systems ask for a semantic, never a mesh. Art assets are bound
 * to semantics by UVegetationLibrary (a single replaceable DataAsset), so a
 * premium grass/tree pack swap changes ONLY the registry: land-cover
 * classification, placement, exclusions, density and determinism all
 * survive untouched.
 */

/** Vegetation semantics — what plant type belongs at a point. */
UENUM(BlueprintType)
enum class EVegSemantic : uint8
{
	None = 0,

	// ---- grass family ---------------------------------------------
	Grass_AirportMowed,   // maintained turf inside airport grounds
	Grass_Mowed,          // residential/commercial lawn, dev-open space
	Grass_NaturalShort,   // short field grass
	Grass_NaturalTall,    // meadow / tall-grass prairie
	Grass_Pasture,        // pasture / hay fields
	Grass_Cropland,       // cultivated land
	Grass_Coastal,        // salt-tolerant beach-adjacent grass
	Grass_Dune,           // sparse dune grass (American beachgrass)
	Grass_Wetland,        // marsh / emergent reeds
	Grass_ForestFloor,    // sparse understory grass/sedges

	// ---- shrub family ---------------------------------------------
	Shrub_Low,            // generic shrub-scrub
	Shrub_Coastal,        // bayberry/beach-plum style coastal scrub
	Shrub_Wetland,        // wetland shrub
	Shrub_Understory,     // forest understory

	// ---- tree family ----------------------------------------------
	Tree_Deciduous,
	Tree_Conifer,
	Tree_Coastal,         // wind-sculpted maritime trees
	Tree_Wetland,         // red maple / swamp species
	Tree_Dead,
	Tree_Sapling,

	MAX                   UMETA(Hidden)
};

/** Surface semantics — what the ground itself IS (rendering + future
 *  physical-surface/footstep/prop-wash routing). */
UENUM(BlueprintType)
enum class ESurfaceSemantic : uint8
{
	Unknown = 0,
	OpenWater,
	ShallowWater,
	WetSand,
	DrySand,
	Dune,
	BareSoil,
	Dirt,
	Gravel,
	Rock,
	Asphalt,
	Concrete,
	ParkingLot,
	AirportRunway,
	AirportTaxiway,
	AirportApron,
	ResidentialLawn,
	CommercialLawn,
	MowedGrass,
	TallGrass,
	Meadow,
	Pasture,
	Cropland,
	Wetland,
	Marsh,
	Shrubland,
	ForestFloor_Deciduous,
	ForestFloor_Mixed,
	ForestFloor_Conifer,
	DevelopedLow,
	DevelopedMedium,
	DevelopedHigh,

	// Appended — never reached by the NLCD classifier directly; Mud is the
	// wet counterpart of Dirt/BareSoil, IceSnow is NLCD class 12.
	Mud,
	IceSnow,
};

/** Context a classifier needs beyond the raw NLCD pixel. */
struct FVegContext
{
	bool   bCoastal = false;    // open water within ~150 m
	float  RunwayDistM = -10000.0f; // sentinel: no runway near; 0..-halfwidth = ON runway surface
	double GroundH = 0.0;       // ground geodetic height (m)
	double Lat = 0.0, Lon = 0.0;
};

namespace VegSem
{
	const TCHAR* ToName(EVegSemantic S);
	const TCHAR* ToName(ESurfaceSemantic S);
	bool FromName(const FString& N, EVegSemantic& Out);
	bool FromName(const FString& N, ESurfaceSemantic& Out);

	/** Is this point inside a grass-family category at all? */
	bool IsGrass(EVegSemantic S);
	bool IsShrub(EVegSemantic S);
	bool IsTree(EVegSemantic S);

	/** Surface-family tests used by interaction systems (sand overlay,
	 *  footsteps, prop wash) — never asset names. */
	bool IsSandLike(ESurfaceSemantic S);   // deformable granular: dry/wet sand + dune
	bool IsPaved(ESurfaceSemantic S);      // any hard impervious surface
	bool IsWater(ESurfaceSemantic S);      // open + shallow water
	bool IsVegetated(ESurfaceSemantic S);  // any turf/crop/forest-floor/marsh surface

	// Classifiers — pure functions, deterministic for a given input.
	EVegSemantic ClassifyGrass(const FLandCoverPixel& Px, const FVegContext& Ctx);
	EVegSemantic ClassifyTree(const FLandCoverPixel& Px, const FVegContext& Ctx, float SpeciesRoll);
	EVegSemantic ClassifyShrub(const FLandCoverPixel& Px, const FVegContext& Ctx);
	ESurfaceSemantic ClassifySurface(const FLandCoverPixel& Px, const FVegContext& Ctx);
}
