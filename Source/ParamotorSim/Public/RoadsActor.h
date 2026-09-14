// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RoadsActor.generated.h"

class UProceduralMeshComponent;

/**
 * ARoadsActor — the real road network, built in-engine from the OSM-derived
 * roads_geom.json (UTM verts + DEM elevations). No offline mesh baking —
 * the geometry file is the data, this renders it.
 *
 * Sections: 0 = asphalt (roads), 1 = worn/concrete (parking, runway,
 * sidewalks, taxiways). UVs are world-space so textures tile by meters.
 */
UCLASS()
class PARAMOTORSIM_API ARoadsActor : public AActor
{
	GENERATED_BODY()

public:
	ARoadsActor();
	virtual void BeginPlay() override;

	/** Path to roads_geom.json. */
	UPROPERTY(EditAnywhere)
	FString GeomJsonPath;

private:
	UPROPERTY()
	UProceduralMeshComponent* Mesh = nullptr;

	void Build();
};
