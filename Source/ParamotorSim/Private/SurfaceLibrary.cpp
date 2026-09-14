// Copyright Epic Games, Inc. All Rights Reserved.

#include "SurfaceLibrary.h"
#include "ParamotorLogging.h"
#include "UObject/ConstructorHelpers.h"

// ---------------------------------------------------------------------------
// Built-in default bindings — the same table the generated DA carries, so the
// sim behaves identically with or without /Game/Data/DA_SurfaceLibrary.
// ---------------------------------------------------------------------------

namespace SurfDefaults
{
	/** semantic → physical type (single source of truth). */
	static ESurfacePhysType PhysFor(ESurfaceSemantic S)
	{
		switch (S)
		{
		case ESurfaceSemantic::OpenWater:
		case ESurfaceSemantic::ShallowWater:  return ESurfacePhysType::Water;
		case ESurfaceSemantic::IceSnow:       return ESurfacePhysType::IceSnow;
		case ESurfaceSemantic::WetSand:       return ESurfacePhysType::WetSand;
		case ESurfaceSemantic::DrySand:
		case ESurfaceSemantic::Dune:          return ESurfacePhysType::DrySand;
		case ESurfaceSemantic::Mud:
		case ESurfaceSemantic::Marsh:         return ESurfacePhysType::Mud;
		case ESurfaceSemantic::BareSoil:
		case ESurfaceSemantic::Dirt:          return ESurfacePhysType::Dirt;
		case ESurfaceSemantic::Gravel:        return ESurfacePhysType::Gravel;
		case ESurfaceSemantic::Rock:          return ESurfacePhysType::Rock;
		case ESurfaceSemantic::Asphalt:
		case ESurfaceSemantic::ParkingLot:
		case ESurfaceSemantic::AirportRunway:
		case ESurfaceSemantic::AirportTaxiway:return ESurfacePhysType::Asphalt;
		case ESurfaceSemantic::Concrete:
		case ESurfaceSemantic::AirportApron:
		case ESurfaceSemantic::DevelopedLow:
		case ESurfaceSemantic::DevelopedMedium:
		case ESurfaceSemantic::DevelopedHigh: return ESurfacePhysType::Concrete;
		case ESurfaceSemantic::Wetland:
		case ESurfaceSemantic::ForestFloor_Deciduous:
		case ESurfaceSemantic::ForestFloor_Mixed:
		case ESurfaceSemantic::ForestFloor_Conifer: return ESurfacePhysType::ForestFloor;
		case ESurfaceSemantic::ResidentialLawn:
		case ESurfaceSemantic::CommercialLawn:
		case ESurfaceSemantic::MowedGrass:
		case ESurfaceSemantic::TallGrass:
		case ESurfaceSemantic::Meadow:
		case ESurfaceSemantic::Pasture:
		case ESurfaceSemantic::Cropland:
		case ESurfaceSemantic::Shrubland:     return ESurfacePhysType::GrassTurf;
		default:                              return ESurfacePhysType::None;
		}
	}

	struct FSurfRow { float Deform; float Dust; float Track; float WetDarken; ESurfaceSemantic WetTo; float Radius; };

	static FSurfRow Row(ESurfaceSemantic S)
	{
		switch (S)
		{
		case ESurfaceSemantic::WetSand:    return { 0.90f, 0.15f, 0.8f, 0.0f,  ESurfaceSemantic::Unknown, 24.0f };
		case ESurfaceSemantic::DrySand:    return { 1.00f, 0.90f, 0.9f, 0.6f,  ESurfaceSemantic::WetSand, 24.0f };
		case ESurfaceSemantic::Dune:       return { 0.85f, 0.80f, 0.8f, 0.6f,  ESurfaceSemantic::WetSand, 24.0f };
		case ESurfaceSemantic::Mud:        return { 0.80f, 0.05f, 0.7f, 0.0f,  ESurfaceSemantic::Unknown, 14.0f };
		case ESurfaceSemantic::Marsh:      return { 0.60f, 0.05f, 0.4f, 0.0f,  ESurfaceSemantic::Unknown, 14.0f };
		case ESurfaceSemantic::BareSoil:
		case ESurfaceSemantic::Dirt:       return { 0.35f, 0.50f, 0.6f, 0.7f,  ESurfaceSemantic::Mud, 14.0f };
		case ESurfaceSemantic::Gravel:     return { 0.15f, 0.30f, 0.4f, 0.3f,  ESurfaceSemantic::Unknown, 12.0f };
		case ESurfaceSemantic::Cropland:   return { 0.30f, 0.40f, 0.5f, 0.7f,  ESurfaceSemantic::Mud, 14.0f };
		case ESurfaceSemantic::Wetland:    return { 0.30f, 0.10f, 0.4f, 0.5f,  ESurfaceSemantic::Mud, 14.0f };
		default:                           return { 0.0f, 0.05f, 0.0f, 0.5f,  ESurfaceSemantic::Unknown, 18.0f };
		}
	}

	struct FPhysRow { float Friction; float Grain; float Dust; EFootstepFallback Fb; };

	static FPhysRow PhysRow(ESurfacePhysType T)
	{
		switch (T)
		{
		case ESurfacePhysType::Water:       return { 1.0f, 0.00f, 0.00f, EFootstepFallback::Dirt };
		case ESurfacePhysType::IceSnow:     return { 0.3f, 0.05f, 0.05f, EFootstepFallback::Dirt };
		case ESurfacePhysType::DrySand:     return { 0.7f, 1.00f, 1.00f, EFootstepFallback::Dirt };
		case ESurfacePhysType::WetSand:     return { 0.9f, 0.15f, 0.30f, EFootstepFallback::Dirt };
		case ESurfacePhysType::Mud:         return { 0.5f, 0.02f, 0.05f, EFootstepFallback::Dirt };
		case ESurfacePhysType::Dirt:        return { 0.8f, 0.25f, 0.60f, EFootstepFallback::Dirt };
		case ESurfacePhysType::Gravel:      return { 0.8f, 0.10f, 0.35f, EFootstepFallback::Dirt };
		case ESurfacePhysType::Rock:        return { 0.9f, 0.02f, 0.10f, EFootstepFallback::Dirt };
		case ESurfacePhysType::GrassTurf:   return { 0.8f, 0.03f, 0.15f, EFootstepFallback::Grass };
		case ESurfacePhysType::ForestFloor: return { 0.7f, 0.05f, 0.20f, EFootstepFallback::Dirt };
		case ESurfacePhysType::Asphalt:     return { 1.0f, 0.00f, 0.05f, EFootstepFallback::Dirt };
		case ESurfacePhysType::Concrete:    return { 1.0f, 0.00f, 0.05f, EFootstepFallback::Dirt };
		default:                            return { 1.0f, 0.00f, 0.00f, EFootstepFallback::Dirt };
		}
	}

	static const TCHAR* PhysMatPath(ESurfacePhysType T)
	{
		switch (T)
		{
		case ESurfacePhysType::Water:       return TEXT("/Game/PhysicalMaterials/PM_Water.PM_Water");
		case ESurfacePhysType::IceSnow:     return TEXT("/Game/PhysicalMaterials/PM_IceSnow.PM_IceSnow");
		case ESurfacePhysType::DrySand:     return TEXT("/Game/PhysicalMaterials/PM_DrySand.PM_DrySand");
		case ESurfacePhysType::WetSand:     return TEXT("/Game/PhysicalMaterials/PM_WetSand.PM_WetSand");
		case ESurfacePhysType::Mud:         return TEXT("/Game/PhysicalMaterials/PM_Mud.PM_Mud");
		case ESurfacePhysType::Dirt:        return TEXT("/Game/PhysicalMaterials/PM_Dirt.PM_Dirt");
		case ESurfacePhysType::Gravel:      return TEXT("/Game/PhysicalMaterials/PM_Gravel.PM_Gravel");
		case ESurfacePhysType::Rock:        return TEXT("/Game/PhysicalMaterials/PM_Rock.PM_Rock");
		case ESurfacePhysType::GrassTurf:   return TEXT("/Game/PhysicalMaterials/PM_Grass.PM_Grass");
		case ESurfacePhysType::ForestFloor: return TEXT("/Game/PhysicalMaterials/PM_ForestFloor.PM_ForestFloor");
		case ESurfacePhysType::Asphalt:     return TEXT("/Game/PhysicalMaterials/PM_Asphalt.PM_Asphalt");
		case ESurfacePhysType::Concrete:    return TEXT("/Game/PhysicalMaterials/PM_Concrete.PM_Concrete");
		default:                            return TEXT("");
		}
	}

	static const TCHAR* PhysName(ESurfacePhysType T)
	{
		switch (T)
		{
		case ESurfacePhysType::Water:       return TEXT("Water");
		case ESurfacePhysType::IceSnow:     return TEXT("IceSnow");
		case ESurfacePhysType::DrySand:     return TEXT("DrySand");
		case ESurfacePhysType::WetSand:     return TEXT("WetSand");
		case ESurfacePhysType::Mud:         return TEXT("Mud");
		case ESurfacePhysType::Dirt:        return TEXT("Dirt");
		case ESurfacePhysType::Gravel:      return TEXT("Gravel");
		case ESurfacePhysType::Rock:        return TEXT("Rock");
		case ESurfacePhysType::GrassTurf:   return TEXT("Grass");
		case ESurfacePhysType::ForestFloor: return TEXT("ForestFloor");
		case ESurfacePhysType::Asphalt:     return TEXT("Asphalt");
		case ESurfacePhysType::Concrete:    return TEXT("Concrete");
		default:                            return TEXT("None");
		}
	}
}

const TCHAR* SurfPhysTypeName(ESurfacePhysType T) { return SurfDefaults::PhysName(T); }

// ---------------------------------------------------------------------------

USurfaceLibrary* USurfaceLibrary::Get()
{
	static USurfaceLibrary* Cached = nullptr;
	if (!Cached)
	{
		Cached = LoadObject<USurfaceLibrary>(nullptr, TEXT("/Game/Data/DA_SurfaceLibrary.DA_SurfaceLibrary"));
		if (!Cached)
		{
			Cached = NewObject<USurfaceLibrary>(GetTransientPackage(), TEXT("SurfaceLibraryDefaults"));
			Cached->BuildDefaults();
			UE_LOG(LogParamotor, Log, TEXT("SurfaceLibrary: DA_SurfaceLibrary not found — using code defaults (%d semantics)"),
				Cached->Surfaces.Num());
		}
		else
		{
			UE_LOG(LogParamotor, Log, TEXT("SurfaceLibrary: using /Game/Data/DA_SurfaceLibrary (%d semantics, %d phys profiles)"),
				Cached->Surfaces.Num(), Cached->PhysProfiles.Num());
		}
	}
	return Cached;
}

const FSurfaceAssetSet* USurfaceLibrary::Find(ESurfaceSemantic S) const
{
	if (const FSurfaceAssetSet* Found = Surfaces.Find(S)) return Found;
	return &DefaultSet(S);
}

const FSurfacePhysProfile* USurfaceLibrary::FindPhys(ESurfacePhysType T) const
{
	if (const FSurfacePhysProfile* Found = PhysProfiles.Find(T)) return Found;
	return &DefaultProfile(T);
}

ESurfacePhysType USurfaceLibrary::PhysTypeFor(ESurfaceSemantic S) const
{
	const FSurfaceAssetSet* Set = Find(S);
	return Set ? Set->Phys : SurfDefaults::PhysFor(S);
}

void USurfaceLibrary::BuildDefaults()
{
	Surfaces.Empty();
	for (uint8 i = 1; i <= (uint8)ESurfaceSemantic::IceSnow; ++i)
	{
		const ESurfaceSemantic S = (ESurfaceSemantic)i;
		FSurfaceAssetSet Set;
		const SurfDefaults::FSurfRow R = SurfDefaults::Row(S);
		Set.Phys = SurfDefaults::PhysFor(S);
		Set.Deformability01 = R.Deform;
		Set.Dustiness01 = R.Dust;
		Set.Trackability01 = R.Track;
		Set.WetDarken01 = R.WetDarken;
		Set.WetCounterpart = R.WetTo;
		Set.DetailRadiusM = R.Radius;
		Surfaces.Add(S, Set);
	}

	PhysProfiles.Empty();
	for (uint8 i = 1; i < (uint8)ESurfacePhysType::MAX; ++i)
	{
		const ESurfacePhysType T = (ESurfacePhysType)i;
		FSurfacePhysProfile P;
		const SurfDefaults::FPhysRow R = SurfDefaults::PhysRow(T);
		P.FrictionScalar = R.Friction;
		P.WashGrainMul = R.Grain;
		P.WashDustMul = R.Dust;
		P.FootstepFallback = R.Fb;
		P.PhysMat = TSoftObjectPtr<UPhysicalMaterial>(FSoftObjectPath(SurfDefaults::PhysMatPath(T)));
		PhysProfiles.Add(T, P);
	}
}

const FSurfaceAssetSet& USurfaceLibrary::DefaultSet(ESurfaceSemantic S)
{
	static TMap<uint8, FSurfaceAssetSet> Table;
	if (Table.Num() == 0)
	{
		for (uint8 i = 1; i <= (uint8)ESurfaceSemantic::IceSnow; ++i)
		{
			const ESurfaceSemantic Sem = (ESurfaceSemantic)i;
			FSurfaceAssetSet Set;
			const SurfDefaults::FSurfRow R = SurfDefaults::Row(Sem);
			Set.Phys = SurfDefaults::PhysFor(Sem);
			Set.Deformability01 = R.Deform;
			Set.Dustiness01 = R.Dust;
			Set.Trackability01 = R.Track;
			Set.WetDarken01 = R.WetDarken;
			Set.WetCounterpart = R.WetTo;
			Set.DetailRadiusM = R.Radius;
			Table.Add(i, Set);
		}
	}
	static FSurfaceAssetSet Fallback;
	if (const FSurfaceAssetSet* Found = Table.Find((uint8)S)) return *Found;
	return Fallback;
}

const FSurfacePhysProfile& USurfaceLibrary::DefaultProfile(ESurfacePhysType T)
{
	static TMap<uint8, FSurfacePhysProfile> Table;
	if (Table.Num() == 0)
	{
		for (uint8 i = 1; i < (uint8)ESurfacePhysType::MAX; ++i)
		{
			const ESurfacePhysType PT = (ESurfacePhysType)i;
			FSurfacePhysProfile P;
			const SurfDefaults::FPhysRow R = SurfDefaults::PhysRow(PT);
			P.FrictionScalar = R.Friction;
			P.WashGrainMul = R.Grain;
			P.WashDustMul = R.Dust;
			P.FootstepFallback = R.Fb;
			P.PhysMat = TSoftObjectPtr<UPhysicalMaterial>(FSoftObjectPath(SurfDefaults::PhysMatPath(PT)));
			Table.Add(i, P);
		}
	}
	static FSurfacePhysProfile Fallback;
	if (const FSurfacePhysProfile* Found = Table.Find((uint8)T)) return *Found;
	return Fallback;
}
