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
	UAudioComponent* BirdsAudio = nullptr;

	UPROPERTY()
	UAudioComponent* OceanAudio = nullptr;
};
