// GrassWindManager — syncs simulator wind to a Material Parameter Collection
// so grass materials can animate in the same direction and speed as the
// wing's wind model (gusts, turbulence, gradient included).
//
// Place this actor in the level. It finds the AWingActor automatically,
// reads GetWindDirection() and GetCurrentWindSpeedMs() each tick, and
// writes them to the assigned MaterialParameterCollection.
//
// The MPC (created by make_grass_material.py) exposes:
//   WindVector  (Vector4: XYZ = normalized dir, W = speed m/s)
//   WindStrength (Scalar: speed m/s, convenience for materials)
//   WindGust    (Scalar: gust intensity 0-1)
//
// Grass materials read these via CollectionParameter nodes for WPO.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GrassWindManager.generated.h"

class UMaterialParameterCollection;
class AWingActor;

UCLASS()
class PARAMOTORSIM_API AGrassWindManager : public AActor
{
	GENERATED_BODY()

public:
	AGrassWindManager();

	virtual void Tick(float DeltaTime) override;

	// MPC asset to update each frame. Assign in editor or auto-loaded.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind")
	UMaterialParameterCollection* WindMPC;

	// If true, auto-find the WingActor in the level on BeginPlay.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind")
	bool bAutoFindWing;

	// Manually assigned wing (overrides auto-find).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind")
	AWingActor* WingActorRef;

	// How quickly wind direction smooths toward the target (higher = snappier).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind|Tuning",
		meta = (ClampMin = "1.0", ClampMax = "20.0"))
	float DirectionInterpSpeed;

	// How quickly wind speed smooths toward the target.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind|Tuning",
		meta = (ClampMin = "1.0", ClampMax = "20.0"))
	float SpeedInterpSpeed;

protected:
	virtual void BeginPlay() override;

private:
	// Smoothed values written to the MPC (prevents jittery grass).
	FVector SmoothedWindDir;
	float SmoothedWindSpeed;
	float SmoothedGustIntensity;

	bool bInitialized;
};
