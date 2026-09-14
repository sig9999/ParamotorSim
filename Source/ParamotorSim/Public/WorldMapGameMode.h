// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "AirportMarker.h"
#include "WorldMapGameMode.generated.h"

UCLASS()
class PARAMOTORSIM_API AWorldMapGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AWorldMapGameMode(const FObjectInitializer& ObjectInitializer);

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI")
	TSubclassOf<class UUW_FreeFlightMenu> FreeFlightMenuClass;

	/** Called when the player clicks an airport pin on the globe. */
	void SelectAirport(int32 AirportIndex);

	UPROPERTY(BlueprintReadOnly, Category = "Airports")
	TArray<FAirportInfo> Airports;

protected:
	void SetupGlobe();
	void SpawnAirportMarkers();
	void ShowMenu(class APlayerController* PC);

	UPROPERTY()
	AActor* GeoreferenceActor = nullptr;

	/** Sun that tracks the camera so the visible side of the globe is always lit. */
	UPROPERTY()
	class ADirectionalLight* SunActor = nullptr;

	UPROPERTY()
	TArray<AAirportMarker*> AirportMarkers;

	UPROPERTY()
	class UUW_FreeFlightMenu* MenuRef = nullptr;

	int32 SelectedAirportIndex = -1;

	/** Delay before spawning markers — the georeference transform needs a few
	 *  frames to initialize. Driven from Tick instead of a timer. */
	float MarkerSpawnDelay = 0.6f;
	bool bMarkersSpawned = false;
};
