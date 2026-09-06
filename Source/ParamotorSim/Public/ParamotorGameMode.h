// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "ParamotorGameMode.generated.h"

class UAudioComponent;
class UMaterial;
class UMaterialInstanceDynamic;

UCLASS()
class PARAMOTORSIM_API AParamotorGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AParamotorGameMode();

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY()
	UAudioComponent* BirdsAudio;

	UPROPERTY()
	UAudioComponent* OceanAudio;

	// Creates a high-quality landscape material at runtime using grass + dirt textures
	// with slope-based blending (flat = grass, steep = dirt)
	UMaterialInstanceDynamic* CreateLandscapeMaterial();

	// Scatters grass mesh instances around the spawn area for 3D grass effect
	void ScatterGrassMeshes();
};
