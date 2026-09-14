// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/StaticMeshActor.h"
#include "ParamotorOceanSystem.generated.h"

class AWaterZone;
class AWaterBodyOcean;
class UGerstnerWaterWaves;
class UGerstnerWaterWaveGeneratorSimple;
class AStaticMeshActor;
class UParamotorGameInstance;
class UProceduralMeshComponent;

/** Named sea states — presets for the wave spectrum, not just scaled amplitude. */
UENUM(BlueprintType)
enum class EOceanState : uint8
{
	Calm,
	LightWind,
	Normal,
	Rough,
	Storm,
};

/** Full parameter set for the Gerstner spectrum — every value cm/deg units
 *  as the generator expects. Presets tune the spectrum shape, not just size. */
USTRUCT(BlueprintType)
struct FOceanParams
{
	GENERATED_BODY()

	/** Spectrum width — dominant swell wavelength ceiling (cm). */
	UPROPERTY(EditAnywhere) float MaxWavelengthCm = 14000.f;
	/** Shortest chop wavelength (cm). */
	UPROPERTY(EditAnywhere) float MinWavelengthCm = 300.f;
	/** Wave count across the spectrum. */
	UPROPERTY(EditAnywhere) int32 NumWaves = 20;
	/** Peak wave amplitude (cm). */
	UPROPERTY(EditAnywhere) float MaxAmplitudeCm = 45.f;
	/** Faintest ripple amplitude (cm). */
	UPROPERTY(EditAnywhere) float MinAmplitudeCm = 1.5f;
	/** How fast amplitude falls toward short waves (higher = choppier). */
	UPROPERTY(EditAnywhere) float AmplitudeFalloff = 2.0f;
	/** Wavelength distribution exponent across the spectrum. */
	UPROPERTY(EditAnywhere) float WavelengthFalloff = 2.0f;
	/** Crest sharpness of big swell (0..1 — keep <0.5 or it looks wrong). */
	UPROPERTY(EditAnywhere) float LargeWaveSteepness = 0.22f;
	/** Crest sharpness of chop. */
	UPROPERTY(EditAnywhere) float SmallWaveSteepness = 0.45f;
	/** Steepness falloff across the spectrum. */
	UPROPERTY(EditAnywhere) float SteepnessFalloff = 1.0f;
	/** Dominant wave direction in UE yaw (0 = +X). Set by wind. */
	UPROPERTY(EditAnywhere) float WindAngleDeg = 0.0f;
	/** Directional spread of the spectrum (deg). */
	UPROPERTY(EditAnywhere) float DirectionSpreadDeg = 30.0f;
};

/**
 * AParamotorOceanSystem — the world ocean.
 *
 * Owns the WaterZone + WaterBodyOcean + Gerstner wave field, maps weather
 * onto the wave spectrum, exposes named sea states, and answers water-height
 * queries for future boat/float/contact work.
 *
 * Spawned by AParamotorEnvironment on flat maps (not the Cesium globe).
 */
UCLASS()
class PARAMOTORSIM_API AParamotorOceanSystem : public AActor
{
	GENERATED_BODY()

public:
	AParamotorOceanSystem();

	virtual void Tick(float DeltaTime) override;

	/** Build the whole ocean: zone + water body + wave field + material. */
	void Setup();

	/** Named preset — blends to the new state over a few seconds. */
	UFUNCTION(BlueprintCallable)
	void SetSeaState(EOceanState State);

	/** Force a preset and hold it (weather won't override until cleared).
	 *  State -1 returns to weather drive. */
	UFUNCTION(BlueprintCallable)
	void ForceSeaState(int32 State);

	/** Continuous weather drive — wind speed (m/s), direction (METAR deg FROM), gusts. */
	UFUNCTION(BlueprintCallable)
	void UpdateFromWeather(float WindSpeedMS, float WindDirDeg, float GustMS);

	/** If true, weather calls set the spectrum directly; if false they only
	 *  pick the nearest preset. */
	UPROPERTY(EditAnywhere)
	bool bContinuousWeather = true;

	/** World-space water height including waves — for future float/contact work. */
	UFUNCTION(BlueprintCallable)
	float GetWaterHeightAt(const FVector& WorldPos) const;

	/** The live state name for HUD/debug. */
	UFUNCTION(BlueprintCallable)
	EOceanState GetCurrentState() const { return CurrentState; }

	/** Current significant wave height in meters (for telemetry/debug). */
	UPROPERTY(BlueprintReadOnly)
	float CurrentHsMeters = 0.0f;

	/** The ocean actor (nullptr if the Water plugin failed and we fell back). */
	UPROPERTY(BlueprintReadOnly)
	AWaterBodyOcean* Ocean = nullptr;

	/** Flat fallback plane, if the Water plugin path failed. */
	AActor* GetFallbackPlane() const { return FallbackPlane; }

private:
	UPROPERTY()
	AWaterZone* Zone = nullptr;
	// NewObject'd in globe mode — must be UPROPERTY or GC reclaims them
	// (~60 s in) and RecomputeWaves jumps into freed memory.
	UPROPERTY()
	UGerstnerWaterWaves* Waves = nullptr;
	UPROPERTY()
	UGerstnerWaterWaveGeneratorSimple* Gen = nullptr;
	UPROPERTY()
	AStaticMeshActor* FallbackPlane = nullptr;

	FOceanParams Target;
	FOceanParams Current;
	EOceanState CurrentState = EOceanState::Normal;

	// ---- globe mode -------------------------------------------------------
	// Cesium globe path: no Landscape/WaterZone. A geoid-curved procedural
	// disc at true MSL (ellipsoid + EGM96 geoid) recenters under the pawn.
	// The weather→Gerstner drive is unchanged — it rides on top of the sheet.
	UPROPERTY()
	UProceduralMeshComponent* GlobeDisc = nullptr;
	bool bGlobeMode = false;
	double DiscCenterLat = 0.0, DiscCenterLon = 0.0;
	float DiscWaterZ = 0.0f;              // world Z of the MSL sheet at its center
	void SetupGlobe();
	void RebuildGlobeDisc(double CenLat, double CenLon);

	float RecomputeTimer = 0.0f;
	float DebugSampleTimer = 0.0f;
	bool bDirty = false;
	bool bManualOverride = false;

	/** The preset table — physically plausible fetch-limited coastal spectra. */
	static FOceanParams Preset(EOceanState State);
	void ApplyToGenerator(const FOceanParams& P);
};
