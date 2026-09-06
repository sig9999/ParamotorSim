#include "GrassFieldSpawner.h"
#include "GrassField.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/Pawn.h"

void UGrassFieldSpawner::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	bHasSpawned = false;

	// Only spawn in game worlds (not main menu, not editor preview)
	UWorld* World = GetWorld();
	if (!World)
	{
		bIsGameWorld = false;
		return;
	}

	// Check if this is a game world (not editor, not menu)
	bool bIsEditor = World->WorldType == EWorldType::Editor;
	bool bIsEditorPreview = World->WorldType == EWorldType::EditorPreview;
	bool bIsPIE = World->WorldType == EWorldType::PIE;
	bool bIsGame = World->WorldType == EWorldType::Game;

	bIsGameWorld = (bIsPIE || bIsGame) && !bIsEditor && !bIsEditorPreview;

	if (bIsGameWorld)
	{
		UE_LOG(LogTemp, Log, TEXT("GrassFieldSpawner: Initialized in game world (%s)"), *World->GetName());
	}
}

void UGrassFieldSpawner::Deinitialize()
{
	Super::Deinitialize();
}

void UGrassFieldSpawner::Tick(float DeltaTime)
{
	if (!bIsGameWorld || bHasSpawned)
		return;

	UWorld* World = GetWorld();
	if (!World)
		return;

	// Wait until the world has finished loading / actors are present
	// Check for the player start or any pawn to confirm the level is ready
	bool bWorldReady = false;

	// Check if any pawn exists (means the level and player are loaded)
	for (TActorIterator<APawn> It(World); It; ++It)
	{
		bWorldReady = true;
		break;
	}

	// Also check for PlayerStart
	if (!bWorldReady)
	{
		for (TActorIterator<APlayerStart> It(World); It; ++It)
		{
			bWorldReady = true;
			break;
		}
	}

	if (!bWorldReady)
		return;

	// Check if a GrassField already exists (don't double-spawn)
	for (TActorIterator<AGrassField> It(World); It; ++It)
	{
		UE_LOG(LogTemp, Log, TEXT("GrassFieldSpawner: GrassField already exists, skipping spawn"));
		bHasSpawned = true;
		return;
	}

	// Spawn the GrassField at the origin
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	FVector SpawnLocation(0.0f, 0.0f, 0.0f);
	FRotator SpawnRotation(0.0f, 0.0f, 0.0f);

	AGrassField* GrassField = World->SpawnActor<AGrassField>(
		AGrassField::StaticClass(),
		SpawnLocation,
		SpawnRotation,
		SpawnParams);

	if (GrassField)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("GrassFieldSpawner: Spawned GrassField at origin (0,0,0)"));
		bHasSpawned = true;
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("GrassFieldSpawner: Failed to spawn GrassField"));
	}
}

TStatId UGrassFieldSpawner::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UGrassFieldSpawner, STATGROUP_Tickables);
}
