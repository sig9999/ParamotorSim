// Copyright Epic Games, Inc. All Rights Reserved.

#include "VegetationSemantics.h"
#include "LandCoverService.h"

// NLCD 2021 class ids (mirrors ELandCoverClass — kept as raw ints so this
// file stays independent of service internals).
//  11 water  12 ice  21 dev-open  22 dev-low  23 dev-med  24 dev-high
//  31 barren  41 deciduous  42 evergreen  43 mixed  52 shrub-scrub
//  71 grassland  81 pasture/hay  82 cultivated  90 woody-wet  95 emergent-wet

namespace VegSem
{

const TCHAR* ToName(EVegSemantic S)
{
	switch (S)
	{
	case EVegSemantic::None:              return TEXT("None");
	case EVegSemantic::Grass_AirportMowed:return TEXT("Grass_AirportMowed");
	case EVegSemantic::Grass_Mowed:       return TEXT("Grass_Mowed");
	case EVegSemantic::Grass_NaturalShort:return TEXT("Grass_NaturalShort");
	case EVegSemantic::Grass_NaturalTall: return TEXT("Grass_NaturalTall");
	case EVegSemantic::Grass_Pasture:     return TEXT("Grass_Pasture");
	case EVegSemantic::Grass_Cropland:    return TEXT("Grass_Cropland");
	case EVegSemantic::Grass_Coastal:     return TEXT("Grass_Coastal");
	case EVegSemantic::Grass_Dune:        return TEXT("Grass_Dune");
	case EVegSemantic::Grass_Wetland:     return TEXT("Grass_Wetland");
	case EVegSemantic::Grass_ForestFloor: return TEXT("Grass_ForestFloor");
	case EVegSemantic::Shrub_Low:         return TEXT("Shrub_Low");
	case EVegSemantic::Shrub_Coastal:     return TEXT("Shrub_Coastal");
	case EVegSemantic::Shrub_Wetland:     return TEXT("Shrub_Wetland");
	case EVegSemantic::Shrub_Understory:  return TEXT("Shrub_Understory");
	case EVegSemantic::Tree_Deciduous:    return TEXT("Tree_Deciduous");
	case EVegSemantic::Tree_Conifer:      return TEXT("Tree_Conifer");
	case EVegSemantic::Tree_Coastal:      return TEXT("Tree_Coastal");
	case EVegSemantic::Tree_Wetland:      return TEXT("Tree_Wetland");
	case EVegSemantic::Tree_Dead:         return TEXT("Tree_Dead");
	case EVegSemantic::Tree_Sapling:      return TEXT("Tree_Sapling");
	default:                              return TEXT("?");
	}
}

const TCHAR* ToName(ESurfaceSemantic S)
{
	switch (S)
	{
	case ESurfaceSemantic::Unknown:             return TEXT("Unknown");
	case ESurfaceSemantic::OpenWater:           return TEXT("OpenWater");
	case ESurfaceSemantic::ShallowWater:        return TEXT("ShallowWater");
	case ESurfaceSemantic::WetSand:             return TEXT("WetSand");
	case ESurfaceSemantic::DrySand:             return TEXT("DrySand");
	case ESurfaceSemantic::Dune:                return TEXT("Dune");
	case ESurfaceSemantic::BareSoil:            return TEXT("BareSoil");
	case ESurfaceSemantic::Dirt:                return TEXT("Dirt");
	case ESurfaceSemantic::Gravel:              return TEXT("Gravel");
	case ESurfaceSemantic::Rock:                return TEXT("Rock");
	case ESurfaceSemantic::Asphalt:             return TEXT("Asphalt");
	case ESurfaceSemantic::Concrete:            return TEXT("Concrete");
	case ESurfaceSemantic::ParkingLot:          return TEXT("ParkingLot");
	case ESurfaceSemantic::AirportRunway:       return TEXT("AirportRunway");
	case ESurfaceSemantic::AirportTaxiway:      return TEXT("AirportTaxiway");
	case ESurfaceSemantic::AirportApron:        return TEXT("AirportApron");
	case ESurfaceSemantic::ResidentialLawn:     return TEXT("ResidentialLawn");
	case ESurfaceSemantic::CommercialLawn:      return TEXT("CommercialLawn");
	case ESurfaceSemantic::MowedGrass:          return TEXT("MowedGrass");
	case ESurfaceSemantic::TallGrass:           return TEXT("TallGrass");
	case ESurfaceSemantic::Meadow:              return TEXT("Meadow");
	case ESurfaceSemantic::Pasture:             return TEXT("Pasture");
	case ESurfaceSemantic::Cropland:            return TEXT("Cropland");
	case ESurfaceSemantic::Wetland:             return TEXT("Wetland");
	case ESurfaceSemantic::Marsh:               return TEXT("Marsh");
	case ESurfaceSemantic::Shrubland:           return TEXT("Shrubland");
	case ESurfaceSemantic::ForestFloor_Deciduous:return TEXT("ForestFloor_Deciduous");
	case ESurfaceSemantic::ForestFloor_Mixed:   return TEXT("ForestFloor_Mixed");
	case ESurfaceSemantic::ForestFloor_Conifer: return TEXT("ForestFloor_Conifer");
	case ESurfaceSemantic::DevelopedLow:        return TEXT("DevelopedLow");
	case ESurfaceSemantic::DevelopedMedium:     return TEXT("DevelopedMedium");
	case ESurfaceSemantic::DevelopedHigh:       return TEXT("DevelopedHigh");
	case ESurfaceSemantic::Mud:                 return TEXT("Mud");
	case ESurfaceSemantic::IceSnow:             return TEXT("IceSnow");
	default:                                    return TEXT("?");
	}
}

bool FromName(const FString& N, EVegSemantic& Out)
{
	for (uint8 i = 0; i < (uint8)EVegSemantic::MAX; ++i)
	{
		const EVegSemantic S = (EVegSemantic)i;
		if (N.Equals(ToName(S), ESearchCase::IgnoreCase)) { Out = S; return true; }
	}
	return false;
}

bool FromName(const FString& N, ESurfaceSemantic& Out)
{
	for (uint8 i = 0; i <= (uint8)ESurfaceSemantic::IceSnow; ++i)
	{
		const ESurfaceSemantic S = (ESurfaceSemantic)i;
		if (N.Equals(ToName(S), ESearchCase::IgnoreCase)) { Out = S; return true; }
	}
	return false;
}

bool IsSandLike(ESurfaceSemantic S)
{
	return S == ESurfaceSemantic::DrySand || S == ESurfaceSemantic::WetSand || S == ESurfaceSemantic::Dune;
}

bool IsPaved(ESurfaceSemantic S)
{
	switch (S)
	{
	case ESurfaceSemantic::Asphalt:
	case ESurfaceSemantic::Concrete:
	case ESurfaceSemantic::ParkingLot:
	case ESurfaceSemantic::AirportRunway:
	case ESurfaceSemantic::AirportTaxiway:
	case ESurfaceSemantic::AirportApron:
		return true;
	default:
		return false;
	}
}

bool IsWater(ESurfaceSemantic S)
{
	return S == ESurfaceSemantic::OpenWater || S == ESurfaceSemantic::ShallowWater;
}

bool IsVegetated(ESurfaceSemantic S)
{
	switch (S)
	{
	case ESurfaceSemantic::ResidentialLawn:
	case ESurfaceSemantic::CommercialLawn:
	case ESurfaceSemantic::MowedGrass:
	case ESurfaceSemantic::TallGrass:
	case ESurfaceSemantic::Meadow:
	case ESurfaceSemantic::Pasture:
	case ESurfaceSemantic::Cropland:
	case ESurfaceSemantic::Wetland:
	case ESurfaceSemantic::Marsh:
	case ESurfaceSemantic::Shrubland:
	case ESurfaceSemantic::ForestFloor_Deciduous:
	case ESurfaceSemantic::ForestFloor_Mixed:
	case ESurfaceSemantic::ForestFloor_Conifer:
		return true;
	default:
		return false;
	}
}

bool IsGrass(EVegSemantic S) { return S >= EVegSemantic::Grass_AirportMowed && S <= EVegSemantic::Grass_ForestFloor; }
bool IsShrub(EVegSemantic S) { return S >= EVegSemantic::Shrub_Low && S <= EVegSemantic::Shrub_Understory; }
bool IsTree (EVegSemantic S) { return S >= EVegSemantic::Tree_Deciduous && S <= EVegSemantic::Tree_Sapling; }

// ---- grass --------------------------------------------------------------

EVegSemantic ClassifyGrass(const FLandCoverPixel& Px, const FVegContext& Ctx)
{
	// Hard surface exclusions live in AVegetationField::AllowedAt; here we
	// only decide WHICH grass semantic a plantable point carries.
	if (Px.Cover == 11 || Px.Cover == 12) return EVegSemantic::None;
	if (Px.Cover == 24 || Px.Impervious > 70) return EVegSemantic::None; // pavement — no vegetation

	// Airport turf: near (not on) an active runway.
	if (Ctx.RunwayDistM >= 0.0f && Ctx.RunwayDistM < 60.0f)
		return EVegSemantic::Grass_AirportMowed;

	switch (Px.Cover)
	{
	case 21: // developed open space — lawns/parks/golf
		return Ctx.bCoastal ? EVegSemantic::Grass_Coastal : EVegSemantic::Grass_Mowed;
	case 22: case 23:
		return EVegSemantic::Grass_Mowed;
	case 31: // barren — beach/dune sand or bare rock
		return Ctx.bCoastal ? EVegSemantic::Grass_Dune : EVegSemantic::Grass_NaturalShort;
	case 41: case 42: case 43:
		return EVegSemantic::Grass_ForestFloor;
	case 52:
		return Ctx.bCoastal ? EVegSemantic::Grass_Coastal : EVegSemantic::Grass_NaturalShort;
	case 71:
		return Ctx.bCoastal ? EVegSemantic::Grass_Coastal : EVegSemantic::Grass_NaturalTall;
	case 81:
		return EVegSemantic::Grass_Pasture;
	case 82:
		return EVegSemantic::Grass_Cropland;
	case 90: case 95:
		return EVegSemantic::Grass_Wetland;
	default: // 0 no-data / anything else
		return EVegSemantic::Grass_NaturalShort;
	}
}

// ---- trees ----------------------------------------------------------------

EVegSemantic ClassifyTree(const FLandCoverPixel& Px, const FVegContext& Ctx, float SpeciesRoll)
{
	switch (Px.Cover)
	{
	case 41:
		return EVegSemantic::Tree_Deciduous;
	case 42:
		return EVegSemantic::Tree_Conifer;
	case 43:
		return SpeciesRoll < 0.5f ? EVegSemantic::Tree_Deciduous : EVegSemantic::Tree_Conifer;
	case 90:
		return EVegSemantic::Tree_Wetland;
	case 95:
		return EVegSemantic::Tree_Sapling;   // emergent marsh — woody growth is young/low
	case 52:
		return Ctx.bCoastal ? EVegSemantic::Tree_Coastal : EVegSemantic::Tree_Sapling;
	case 21: case 22: case 23:             // yard/street trees — mostly hardwoods
		return SpeciesRoll < 0.85f ? EVegSemantic::Tree_Deciduous : EVegSemantic::Tree_Conifer;
	case 71: case 81: case 82:             // scattered shade trees in fields
		return EVegSemantic::Tree_Deciduous;
	default:
		// Continental fallback: latitude/elevation conifer bias.
		return (Ctx.GroundH > 1200.0 || Ctx.Lat > 44.0) && SpeciesRoll < 0.7f
			? EVegSemantic::Tree_Conifer : EVegSemantic::Tree_Deciduous;
	}
}

// ---- shrubs ---------------------------------------------------------------

EVegSemantic ClassifyShrub(const FLandCoverPixel& Px, const FVegContext& Ctx)
{
	switch (Px.Cover)
	{
	case 52:
		return Ctx.bCoastal ? EVegSemantic::Shrub_Coastal : EVegSemantic::Shrub_Low;
	case 90: case 95:
		return EVegSemantic::Shrub_Wetland;
	case 41: case 42: case 43:
		return EVegSemantic::Shrub_Understory;
	case 21: case 22: case 23: case 71: case 81:
		return Ctx.bCoastal ? EVegSemantic::Shrub_Coastal : EVegSemantic::Shrub_Low;
	case 31:
		return Ctx.bCoastal ? EVegSemantic::Shrub_Coastal : EVegSemantic::None;
	default:
		return EVegSemantic::Shrub_Low;
	}
}

// ---- surface --------------------------------------------------------------

ESurfaceSemantic ClassifySurface(const FLandCoverPixel& Px, const FVegContext& Ctx)
{
	if (Px.Cover == 11) return ESurfaceSemantic::OpenWater;
	if (Px.Cover == 12) return ESurfaceSemantic::IceSnow;
	// Negative distance = on the runway pavement itself (dist minus
	// half-width); 0..20 m = runway edge strip. The -10000 sentinel
	// (no runway in this tile) must not classify.
	if (Ctx.RunwayDistM > -9000.0f && Ctx.RunwayDistM < 20.0f) return ESurfaceSemantic::AirportRunway;

	// Impervious descriptor: 1..4 = road classes, 5 = non-road impervious.
	if (Px.Road >= 1 && Px.Road <= 4)
		return Px.Cover == 21 || Px.Cover == 22 ? ESurfaceSemantic::Asphalt : ESurfaceSemantic::Concrete;
	if (Px.Impervious > 70 && (Px.Cover == 23 || Px.Cover == 24 || Px.Road == 5))
		return ESurfaceSemantic::ParkingLot;

	switch (Px.Cover)
	{
	case 21: return Ctx.RunwayDistM >= 0.0f ? ESurfaceSemantic::MowedGrass
		: (Px.Impervious > 40 ? ESurfaceSemantic::DevelopedLow : ESurfaceSemantic::ResidentialLawn);
	case 22: return Px.Impervious > 50 ? ESurfaceSemantic::DevelopedLow : ESurfaceSemantic::ResidentialLawn;
	case 23: return Px.Impervious > 60 ? ESurfaceSemantic::DevelopedMedium : ESurfaceSemantic::CommercialLawn;
	case 24: return ESurfaceSemantic::DevelopedHigh;
	case 31: return Ctx.bCoastal ? ESurfaceSemantic::DrySand : ESurfaceSemantic::BareSoil;
	case 41: return ESurfaceSemantic::ForestFloor_Deciduous;
	case 42: return ESurfaceSemantic::ForestFloor_Conifer;
	case 43: return ESurfaceSemantic::ForestFloor_Mixed;
	case 52: return ESurfaceSemantic::Shrubland;
	case 71: return ESurfaceSemantic::Meadow;
	case 81: return ESurfaceSemantic::Pasture;
	case 82: return ESurfaceSemantic::Cropland;
	case 90: return ESurfaceSemantic::Wetland;
	case 95: return ESurfaceSemantic::Marsh;
	default: return ESurfaceSemantic::Unknown;
	}
}

} // namespace VegSem
