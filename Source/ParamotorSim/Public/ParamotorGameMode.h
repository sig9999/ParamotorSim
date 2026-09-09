// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "ParamotorGameMode.generated.h"

class UAudioComponent;
class ADirectionalLight;
class ASkyLight;
class AExponentialHeightFog;
class ASkyAtmosphere;
class APostProcessVolume;
class AStaticMeshActor;

UCLASS()
class PARAMOTORSIM_API AParamotorGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AParamotorGameMode();

	// Update sun position from TimeOfDay (called each tick)
	void UpdateSunPosition(float TimeH);

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

private:
	// Environment setup
	void SetupSkyAndLighting();
	void SetupPostProcessing();
	void SetupWater();
	void SetupCoastlineDirection();
	void SetupCoastalTerrain();

	// Coastal terrain height function (meters above sea level)
	float GetCoastalHeight(float WorldX, float WorldY) const;

	FVector GetLaunchFieldSpawnLocation() const;

	// Overridden to spawn players at the launch field / Westerly Airport
	virtual void RestartPlayer(AController* NewPlayer) override;
	virtual AActor* FindPlayerStart_Implementation(AController* Player, const FString& IncomingName) override;

	UPROPERTY()
	UAudioComponent* BirdsAudio = nullptr;

	UPROPERTY()
	UAudioComponent* OceanAudio = nullptr;

	// Environment actors
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

	UPROPERTY()
	AStaticMeshActor* WaterPlane = nullptr;

	UPROPERTY()
	AActor* TerrainActor = nullptr;
};
