// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "HangarGameMode.generated.h"

UCLASS()
class PARAMOTORSIM_API AHangarGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AHangarGameMode();

	virtual void StartPlay() override;
	virtual void PostLogin(APlayerController* NewPlayer) override;

	AActor* GetPodium() const { return PodiumActor; }
	void SetTurntablePaused(bool bPaused);

protected:
	UFUNCTION()
	void DoSpawns();
	void ApplyStudioQuality();
	void SpawnEnvironment();
	void SpawnBuilder();
	void CreateMenu();

	UPROPERTY()
	AActor* PodiumActor = nullptr;

	UPROPERTY()
	class URotatingMovementComponent* Turntable = nullptr;
	float TurntableRate = 12.0f;
};
