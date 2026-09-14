// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "SpecCatalog.h"
#include "ParamotorGameInstance.generated.h"
UCLASS()
class PARAMOTORSIM_API UParamotorGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	virtual void Init() override;

	UPROPERTY(BlueprintReadWrite, Category="Weather")
	float WindSpeed = 6.0f;  // m/s — ~13 mph, light breeze (paramotor-friendly)

	UPROPERTY(BlueprintReadWrite, Category="Weather")
	float WindGusts = 2.0f;  // m/s — gust variation

	UPROPERTY(BlueprintReadWrite, Category="Weather")
	float WindDirection = 180.0f;  // degrees the wind comes FROM (METAR convention)

	UPROPERTY(BlueprintReadWrite, Category="Weather")
	float CloudCover = 0.25f;

	UPROPERTY(BlueprintReadWrite, Category="Weather")
	float Temperature = 20.0f;

	UPROPERTY(BlueprintReadWrite, Category="Weather")
	float Humidity = 60.0f;  // % — used for dew point, cloudbase, thermal strength

	UPROPERTY(BlueprintReadWrite, Category="Weather")
	float DewPoint = 12.0f;  // °C — live data fills this; manual mode derives it

	UPROPERTY(BlueprintReadWrite, Category="Weather")
	float Altimeter_inHg = 29.92f;

	UPROPERTY(BlueprintReadWrite, Category="Weather")
	float Visibility_mi = 10.0f;

	UPROPERTY(BlueprintReadWrite, Category="Weather")
	float TimeOfDay = 12.0f;

	// ---- Live weather (METAR via aviationweather.gov) ----
	UPROPERTY(BlueprintReadWrite, Category="Weather")
	bool bLiveWeather = false;

	UPROPERTY(BlueprintReadOnly, Category="Weather")
	FString LiveStation;      // reporting station ICAO, e.g. "KWST"

	UPROPERTY(BlueprintReadOnly, Category="Weather")
	FString LiveMetarRaw;     // raw METAR text

	UPROPERTY(BlueprintReadOnly, Category="Weather")
	FString LiveObsTime;      // observation time (Zulu)

	UPROPERTY(BlueprintReadOnly, Category="Weather")
	FString LiveFlightCat;    // VFR / MVFR / IFR / LIFR

	UPROPERTY(BlueprintReadOnly, Category="Weather")
	FString LiveCloudLayers;  // e.g. "SCT 4,500 · BKN 9,000"

	UPROPERTY(BlueprintReadOnly, Category="Weather")
	FString LiveStatus;       // fetch status line for the UI

	UPROPERTY(BlueprintReadOnly, Category="Weather")
	double LiveFetchUTC = 0.0; // FDateTime::UtcNow unix time of last good fetch

	UPROPERTY(BlueprintReadWrite, Category="Settings")
	float MasterVolume = 1.0f;

	UPROPERTY(BlueprintReadWrite, Category="Settings")
	float MouseSensitivity = 1.0f;

	UPROPERTY(BlueprintReadWrite, Category="Hangar")
	TArray<FParamotorWingSpec> WingSpecs;

	UPROPERTY(BlueprintReadWrite, Category="Hangar")
	TArray<FParamotorEngineSpec> EngineSpecs;

	UPROPERTY(BlueprintReadWrite, Category="Hangar")
	TArray<FParamotorFrameSpec> FrameSpecs;

	UPROPERTY(BlueprintReadWrite, Category="Hangar")
	TArray<FParamotorPropSpec> PropSpecs;

	UPROPERTY(BlueprintReadWrite, Category="Hangar")
	TArray<FParamotorHarnessSpec> HarnessSpecs;

	UPROPERTY(BlueprintReadWrite, Category="Hangar")
	FParamotorBuild CurrentBuild;

	/** Index of the selected launch site in the FreeFlight menu World tab.
	 *  The GameMode reads this to decide where on the map to spawn. */
	UPROPERTY(BlueprintReadWrite, Category="World")
	int32 SelectedLaunchSite = 0;

	/** Selected airport data from the world map globe. */
	UPROPERTY(BlueprintReadWrite, Category="World")
	double SelectedAirportLat = 41.3650;

	UPROPERTY(BlueprintReadWrite, Category="World")
	double SelectedAirportLon = -71.6600;

	UPROPERTY(BlueprintReadWrite, Category="World")
	float SelectedAirportElev = 6.0f;

	UPROPERTY(BlueprintReadWrite, Category="World")
	FString SelectedAirportName = TEXT("Ninigret Airstrip");

	UPROPERTY(BlueprintReadWrite, Category="World")
	FString SelectedAirportICAO = TEXT("OTST");

	/** Google Maps Platform API key — enables Photorealistic 3D Tiles when set. */
	UPROPERTY(BlueprintReadWrite, Category="World")
	FString GoogleMapsApiKey;

	UPROPERTY(BlueprintReadWrite, Category="World")
	bool bAirportSelected = false;

	/** Curated launch sites — the flat-map fields you can actually operate
	 *  from. Drives spawn, weather focus, and the world-map FLY flow. */
	UPROPERTY(BlueprintReadOnly, Category="World")
	mutable TArray<FLaunchSite> LaunchSites;

	UPROPERTY(BlueprintReadWrite, Category="World")
	int32 ActiveSiteIndex = 0;

	/** Fill the launch site table if empty (const — table is a lazy cache). */
	void EnsureSiteData() const;

	/** The site the flat-map flight world spawns at (LaunchSites[ActiveSiteIndex]). */
	const FLaunchSite* GetActiveSite() const;

	/** Find the curated site for an ICAO code (matches "WST"↔"KWST"), or
	 *  nullptr when no flat-map field exists for it. */
	const FLaunchSite* FindSiteByICAO(const FString& ICAO) const;

	/** Point ActiveSiteIndex at the site whose ICAO matches the selected
	 *  world-map airport (returns true when a curated site was matched). */
	bool SelectSiteByICAO(const FString& ICAO);

	/** Fill the part spec tables (wings/engines/frames/props/harnesses) if
	 *  they're empty. The hangar calls this on init; world spawn paths call
	 *  it so a fresh game session has the catalog data without visiting the
	 *  hangar first. */
	void EnsureSpecData();
};
