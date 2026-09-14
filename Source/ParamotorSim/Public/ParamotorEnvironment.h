// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ParamotorEnvironment.generated.h"

class UAudioComponent;
class ADirectionalLight;
class ASkyLight;
class AExponentialHeightFog;
class ASkyAtmosphere;
class APostProcessVolume;
class AParamotorOceanSystem;

/** Runtime world environment: sky, sun, fog, post-processing, ocean water,
 *  ambient audio, under-map plane. Spawned by the GameMode in BeginPlay —
 *  owns its own Tick for time-of-day sun updates. */
UCLASS()
class PARAMOTORSIM_API AParamotorEnvironment : public AActor
{
	GENERATED_BODY()

public:
	AParamotorEnvironment();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	/** Update sun position/intensity/color for the given hour of day (0-24). */
	void UpdateSunPosition(float TimeH);

private:
	void SetupSkyAndLighting();
	void SetupPostProcessing();
	void SetupAmbientAudio();
	void SetupUnderMapPlane();

	UPROPERTY()
	UAudioComponent* BirdsAudio = nullptr;

	UPROPERTY()
	UAudioComponent* OceanAudio = nullptr;

	UPROPERTY()
	ADirectionalLight* SunLight = nullptr;

	UPROPERTY()
	ASkyLight* SkyLightActor = nullptr;

	UPROPERTY()
	AExponentialHeightFog* FogActor = nullptr;

	UPROPERTY()
	ASkyAtmosphere* SkyAtmosphereActor = nullptr;

	UPROPERTY()
	APostProcessVolume* PostProcessVol = nullptr;

	// The ocean lives in its own system (waves, sea states, weather drive).
	UPROPERTY()
	AParamotorOceanSystem* OceanSys = nullptr;
	float OceanUpdateTimer = 0.0f;
};
