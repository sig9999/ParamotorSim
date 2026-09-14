// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "VegetationSemantics.h"
#include "SurfaceLibrary.generated.h"

class UMaterialInterface;
class UPhysicalMaterial;
class USoundBase;
class UNiagaraSystem;

/**
 * Surface SEMANTIC → representation registry — the ground-surface twin of
 * UVegetationLibrary.
 *
 * The world classifies "this is wet sand" / "this is asphalt" via
 * ESurfaceSemantic (geographic truth). This library maps that identity to
 * everything that REPRESENTS it: the near-field detail material, the
 * physical-interaction profile (SurfaceType, footsteps, prop-wash response,
 * friction), and wetness behavior. Procedural systems never name a material
 * or physical material — they resolve a semantic through this registry.
 *
 * Upgrade path: import a material/audio pack, open
 * /Game/Data/DA_SurfaceLibrary, reassign entries, save. Classification,
 * geography, exclusions and interaction logic all survive untouched.
 */

/** Physical interaction categories — deliberately coarser than
 *  ESurfaceSemantic. Many semantics share one physical identity:
 *  AirportRunway/Taxiway/Apron/Asphalt all resolve to Asphalt here. */
UENUM(BlueprintType)
enum class ESurfacePhysType : uint8
{
	None = 0,
	Water,        // swimmable/splashable — no footsteps
	IceSnow,      // hard slick frozen surface
	DrySand,      // loose grains — high prop-wash response, deformable
	WetSand,      // cohesive — low dust, strong deformation
	Mud,          // saturated soil — wet deformable
	Dirt,         // packed soil
	Gravel,       // loose coarse aggregate
	Rock,         // bedrock / boulders
	GrassTurf,    // any turf/meadow/crop surface
	ForestFloor,  // leaf litter / needle duff
	Asphalt,      // roads, runways, taxiways, parking
	Concrete,     // aprons, sidewalks, structures
	MAX           UMETA(Hidden)
};

/** Which built-in footstep array a profile falls back to when it has no
 *  bound sounds (only grass + dirt audio exist in the project today). */
UENUM(BlueprintType)
enum class EFootstepFallback : uint8
{
	Grass = 0,
	Dirt,
};

/** What a physical category sounds/feels/behaves like. Bound per
 *  ESurfacePhysType so ~30 surface semantics collapse onto ~12 profiles. */
USTRUCT(BlueprintType)
struct FSurfacePhysProfile
{
	GENERATED_BODY()

	/** Optional real physical material asset — carries friction/restitution
	 *  and a project SurfaceType. Code never reads it for identity (the enum
	 *  IS the identity); it's here so spawned meshes/props can adopt the
	 *  correct physical material and so hit-reporting can surface a UPM. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TSoftObjectPtr<UPhysicalMaterial> PhysMat;

	/** Footstep sounds for this physical surface. Empty → FootstepFallback. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<TSoftObjectPtr<USoundBase>> FootstepSounds;

	/** Heavy (jog) footsteps. Empty → FootstepSounds → FootstepFallback. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<TSoftObjectPtr<USoundBase>> FootstepHeavySounds;

	/** Built-in sound set used while FootstepSounds is unbound. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EFootstepFallback FootstepFallback = EFootstepFallback::Dirt;

	/** Future FX hooks — bound when a real FX pack exists. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TSoftObjectPtr<UNiagaraSystem> ImpactFX;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TSoftObjectPtr<UNiagaraSystem> WashFX;

	/** Ground friction scalar (1 = neutral). Trikes/quads + future skid code
	 *  read this; the physics solver stays untouched. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float FrictionScalar = 1.0f;

	/** Prop-wash airborne response: multiplies loose-grain emission on this
	 *  surface (0 = sealed). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0"))
	float WashGrainMul = 0.0f;

	/** Prop-wash dust-puff multiplier. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0"))
	float WashDustMul = 0.0f;
};

/** What one ESurfaceSemantic resolves to. */
USTRUCT(BlueprintType)
struct FSurfaceAssetSet
{
	GENERATED_BODY()

	/** Physical interaction identity — drives footsteps, prop wash, and
	 *  every "what did we hit" answer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	ESurfacePhysType Phys = ESurfacePhysType::None;

	/** Near-field detail material for the pawn-following overlay patch.
	 *  Null = real photogrammetry stays visually dominant (the default for
	 *  most categories until a real material pack is bound). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TSoftObjectPtr<UMaterialInterface> DetailPatchMaterial;

	/** Radius of the detail patch when DetailPatchMaterial is bound. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "4.0", ClampMax = "64.0"))
	float DetailRadiusM = 18.0f;

	/** How much loose airborne material prop wash can lift (display hint —
	 *  the physical multipliers live on the phys profile). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Dustiness01 = 0.0f;

	/** How strongly this surface accepts stamped deformation
	 *  (footprints/wash scour) — the sand overlay's gate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Deformability01 = 0.0f;

	/** How well it records tire/skid tracks (future). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Trackability01 = 0.0f;

	/** How strongly wetness darkens the detail material / dust suppression. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WetDarken01 = 0.5f;

	/** Semantic this becomes when saturated (Dirt→Mud, DrySand→WetSand).
	 *  Unknown = no counterpart. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	ESurfaceSemantic WetCounterpart = ESurfaceSemantic::Unknown;
};

/** Display name for a physical category (debug dumps, UI). */
PARAMOTORSIM_API const TCHAR* SurfPhysTypeName(ESurfacePhysType T);

/** Runtime-resolved view of a surface — what consumers actually get back. */
struct FResolvedSurface
{
	ESurfaceSemantic Semantic = ESurfaceSemantic::Unknown;
	/** Semantic after wet-counterpart substitution (Dirt+wet → Mud). */
	ESurfaceSemantic EffectiveSemantic = ESurfaceSemantic::Unknown;
	ESurfacePhysType Phys = ESurfacePhysType::None;
	const FSurfaceAssetSet* Set = nullptr;
	const FSurfacePhysProfile* PhysProfile = nullptr;
	/** 0 dry .. 1 saturated — waterline + weather combined. */
	float Wetness01 = 0.0f;
	/** Ground height above local water surface, cm (negative = submerged). */
	float AboveWaterCm = TNumericLimits<float>::Max();
	bool bResolved = false;
};

UCLASS(BlueprintType)
class PARAMOTORSIM_API USurfaceLibrary : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Semantic → representation binding. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TMap<ESurfaceSemantic, FSurfaceAssetSet> Surfaces;

	/** Physical category → interaction profile. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TMap<ESurfacePhysType, FSurfacePhysProfile> PhysProfiles;

	/** Live registry: /Game/Data/DA_SurfaceLibrary if present, else a
	 *  transient code-built default so the sim never breaks. */
	static USurfaceLibrary* Get();

	/** Semantic → set, with code-default fallback for missing entries. */
	const FSurfaceAssetSet* Find(ESurfaceSemantic S) const;

	/** Physical category → profile. */
	const FSurfacePhysProfile* FindPhys(ESurfacePhysType T) const;

	/** Semantic → physical type (library binding, else built-in table). */
	ESurfacePhysType PhysTypeFor(ESurfaceSemantic S) const;

	/** Populate everything with built-in defaults. Seeds the editor asset
	 *  AND backs the transient fallback. */
	UFUNCTION(BlueprintCallable, CallInEditor)
	void BuildDefaults();

private:
	/** Static default table used when the DA has no entry (or no DA). */
	static const FSurfaceAssetSet& DefaultSet(ESurfaceSemantic S);
	static const FSurfacePhysProfile& DefaultProfile(ESurfacePhysType T);
};
