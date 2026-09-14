// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SandDebrisScatter.generated.h"

class UInstancedStaticMeshComponent;
class APawn;

/**
 * ASandDebrisScatter — micro debris (pebbles/shell bits) scattered on the
 * sand band around the player. Squashed spheres wearing the world-position
 * landscape material pick up the sand look for free. Real geometry catching
 * real light is what makes sand read 3D at foot height.
 */
UCLASS()
class PARAMOTORSIM_API ASandDebrisScatter : public AActor
{
	GENERATED_BODY()

public:
	ASandDebrisScatter();
	virtual void Tick(float DeltaTime) override;

	/** Z band that counts as beach (cm). */
	UPROPERTY(EditAnywhere)
	float SandMaxZCm = 210.0f;

private:
	struct FCellKey
	{
		int32 X = 0, Y = 0;
		bool operator==(const FCellKey& O) const { return X == O.X && Y == O.Y; }
		friend uint32 GetTypeHash(const FCellKey& K) { return HashCombine(K.X, K.Y); }
	};

	TMap<FCellKey, UInstancedStaticMeshComponent*> LiveCells;
	float RepopulateTimer = 0.0f;
	UPROPERTY()
	APawn* CachedPawn = nullptr;

	const float CellCm = 30000.0f;    // 300m cells
	const float RadiusCm = 12000.0f;  // 120m live radius — close-range only

	void Repopulate(APawn* Pawn);
	void SpawnCell(const FCellKey& K);
};
