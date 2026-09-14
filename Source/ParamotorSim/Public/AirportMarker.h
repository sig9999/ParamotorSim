// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AirportMarker.generated.h"

USTRUCT(BlueprintType)
struct FAirportInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) FString Name;
	UPROPERTY(BlueprintReadOnly) FString ICAO;
	UPROPERTY(BlueprintReadOnly) FString City;
	UPROPERTY(BlueprintReadOnly) FString State;
	UPROPERTY(BlueprintReadOnly) double Latitude = 0.0;
	UPROPERTY(BlueprintReadOnly) double Longitude = 0.0;
	UPROPERTY(BlueprintReadOnly) float ElevationM = 0.0f;
	UPROPERTY(BlueprintReadOnly) bool bAvailable = false;
	UPROPERTY(BlueprintReadOnly) FString Description;
};

UCLASS()
class PARAMOTORSIM_API AAirportMarker : public AActor
{
	GENERATED_BODY()

public:
	AAirportMarker();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	/** Initialize the marker with airport data. Call after spawn. */
	void InitializeAirport(const FAirportInfo& Info, int32 Index);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	class USphereComponent* ClickSphere;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	class UStaticMeshComponent* PinMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	class UTextRenderComponent* NameLabel;

	UPROPERTY(BlueprintReadWrite, Category = "Airport")
	FAirportInfo AirportData;

	UPROPERTY(BlueprintReadWrite, Category = "Airport")
	int32 AirportIndex = -1;

protected:
	/** Scale the pin and label based on camera distance so they stay readable. */
	void UpdateVisualScale();
};
