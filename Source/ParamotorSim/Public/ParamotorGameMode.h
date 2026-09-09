// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "ParamotorGameMode.generated.h"

class AStaticMeshActor;

UCLASS()
class PARAMOTORSIM_API AParamotorGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AParamotorGameMode();

protected:
	virtual void BeginPlay() override;

private:
	// Fallback flat field for maps without a real landscape (L_OpenWorld etc.)
	void SetupCoastalTerrain();

	// Coastal terrain height function (meters above sea level)
	float GetCoastalHeight(float WorldX, float WorldY) const;

	FVector GetLaunchFieldSpawnLocation() const;

	// Overridden to spawn players at the launch field / Ninigret Park
	virtual void RestartPlayer(AController* NewPlayer) override;
	virtual AActor* FindPlayerStart_Implementation(AController* Player, const FString& IncomingName) override;

	UPROPERTY()
	AStaticMeshActor* TerrainActor = nullptr;
};
