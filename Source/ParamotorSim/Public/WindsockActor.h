#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WindsockActor.generated.h"

UCLASS()
class PARAMOTORSIM_API AWindsockActor : public AActor
{
	GENERATED_BODY()

public:
	AWindsockActor();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	// Pole + base + strings mesh (static, doesn't move)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Windsock")
	class UStaticMeshComponent* PoleMesh = nullptr;

	// Fabric sock mesh (rotates/scales with wind)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Windsock")
	class UStaticMeshComponent* SockMesh = nullptr;

	// Wind direction in degrees (0=N=+X, 90=E=+Y, 180=S, 270=W)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind")
	float WindDirection = 45.0f;

	// Wind speed in knots
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind")
	float WindSpeed = 8.0f;

	// Max wind speed for full inflation (knots)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind")
	float MaxInflationSpeed = 15.0f;

protected:
	float SmoothedYaw = 0.0f;
	float SmoothedInflation = 0.0f;
};
