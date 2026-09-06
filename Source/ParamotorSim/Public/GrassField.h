// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GrassField.generated.h"

class UProceduralMeshComponent;
class UMaterialInterface;

UCLASS()
class PARAMOTORSIM_API AGrassField : public AActor
{
	GENERATED_BODY()

public:
	AGrassField();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grass")
	UMaterialInterface* GrassMaterial;

	// Center of the grass field. (0,0,0) = auto-detect PlayerStart.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grass")
	FVector FieldCenter;

	// Radius in cm (default 5000 = 50m radius)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grass",
		meta = (ClampMin = "500.0", ClampMax = "20000.0"))
	float FieldRadius;

	// Blade height in cm
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grass",
		meta = (ClampMin = "2.0", ClampMax = "30.0"))
	float BladeHeight;

	// Blade width at base in cm
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grass",
		meta = (ClampMin = "0.5", ClampMax = "5.0"))
	float BladeWidth;

	// Blades per square meter
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grass",
		meta = (ClampMin = "10.0", ClampMax = "200.0"))
	float DensityPerSqM;

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY()
	TArray<UProceduralMeshComponent*> GrassMeshComponents;

	void GenerateGrass();
};
