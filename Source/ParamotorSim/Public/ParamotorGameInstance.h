// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "ParamotorGameInstance.generated.h"

UCLASS()
class PARAMOTORSIM_API UParamotorGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadWrite, Category="Weather")
	float WindSpeed = 6.0f;  // m/s — ~13 mph, light breeze (paramotor-friendly)

	UPROPERTY(BlueprintReadWrite, Category="Weather")
	float WindGusts = 2.0f;  // m/s — gust variation

	UPROPERTY(BlueprintReadWrite, Category="Weather")
	float CloudCover = 0.25f;

	UPROPERTY(BlueprintReadWrite, Category="Weather")
	float Temperature = 20.0f;

	UPROPERTY(BlueprintReadWrite, Category="Weather")
	float Humidity = 60.0f;  // % — used for dew point, cloudbase, thermal strength

	UPROPERTY(BlueprintReadWrite, Category="Weather")
	float TimeOfDay = 12.0f;
};
