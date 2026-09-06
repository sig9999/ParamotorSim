// GrassFieldSpawner — auto-spawns a GrassField actor in the game world.
// Uses UTickableWorldSubsystem so it requires NO level placement and NO
// changes to any existing file (GameMode, PlayerController, etc).
//
// The subsystem auto-initializes when the world loads and spawns the
// GrassField on the first tick. It only runs in game worlds (not menus).

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GrassFieldSpawner.generated.h"

UCLASS()
class PARAMOTORSIM_API UGrassFieldSpawner : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// UTickableWorldSubsystem interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

private:
	bool bHasSpawned;
	bool bIsGameWorld;
};
