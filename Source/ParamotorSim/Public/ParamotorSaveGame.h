// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "ParamotorGameInstance.h"
#include "ParamotorSaveGame.generated.h"

/** One named build in the garage list — a frozen snapshot of the assembly. */
USTRUCT(BlueprintType)
struct FSavedBuildEntry
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadWrite)
	FString Name;

	UPROPERTY(SaveGame, BlueprintReadWrite)
	FString SavedWhen;

	UPROPERTY(SaveGame, BlueprintReadWrite)
	FParamotorBuild Build;

	UPROPERTY(SaveGame, BlueprintReadWrite)
	float TotalWeightKg = 0.0f;
};

UCLASS()
class PARAMOTORSIM_API UParamotorSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY(SaveGame, BlueprintReadWrite)
	FParamotorBuild CurrentBuild;

	/** The garage — every build the player has stashed. */
	UPROPERTY(SaveGame, BlueprintReadWrite)
	TArray<FSavedBuildEntry> Garage;
};
