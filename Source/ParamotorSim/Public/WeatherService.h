// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "WeatherService.generated.h"

class UParamotorGameInstance;

/**
 * Live-weather service — fetches METARs from aviationweather.gov for the
 * focused airport (ICAO lookup when a pin is selected, lat/lon bbox
 * otherwise), parses the JSON, and writes the GameInstance weather fields.
 * UI subscribes to OnWeatherUpdated to re-render.
 */
UCLASS()
class PARAMOTORSIM_API UWeatherService : public UObject
{
	GENERATED_BODY()

public:
	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnWeatherUpdated);
	UPROPERTY(BlueprintAssignable)
	FOnWeatherUpdated OnWeatherUpdated;

	/** Issue a METAR request for the GameInstance's selected/focused airport. */
	void Fetch(UParamotorGameInstance* GI);

private:
	void OnMetarResponse(TSharedPtr<IHttpRequest> Req, TSharedPtr<IHttpResponse> Resp, bool bConnected);

	UPROPERTY()
	UParamotorGameInstance* GIRef = nullptr;
};
