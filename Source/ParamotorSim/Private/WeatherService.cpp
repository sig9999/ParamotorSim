// Copyright Epic Games, Inc. All Rights Reserved.

#include "WeatherService.h"
#include "ParamotorLogging.h"
#include "ParamotorGameInstance.h"
#include "HttpModule.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

void UWeatherService::Fetch(UParamotorGameInstance* GI)
{
	if (!GI) return;
	GIRef = GI;

	FString URL;
	if (GI->bAirportSelected && !GI->SelectedAirportICAO.IsEmpty())
	{
		FString Id = GI->SelectedAirportICAO.ToUpper();
		if (Id.Len() == 3) Id = TEXT("K") + Id;  // US domestic ICAO
		URL = FString::Printf(TEXT("https://aviationweather.gov/api/data/metar?ids=%s&format=json"), *Id);
	}
	else
	{
		const double Lat = GI->SelectedAirportLat;
		const double Lon = GI->SelectedAirportLon;
		URL = FString::Printf(TEXT("https://aviationweather.gov/api/data/metar?bbox=%.3f,%.3f,%.3f,%.3f&format=json"),
			Lat - 0.7, Lon - 0.7, Lat + 0.7, Lon + 0.7);
	}

	TSharedRef<IHttpRequest> Req = FHttpModule::Get().CreateRequest();
	Req->SetURL(URL);
	Req->SetVerb(TEXT("GET"));
	Req->SetHeader(TEXT("Accept"), TEXT("application/json"));
	Req->OnProcessRequestComplete().BindUObject(this, &UWeatherService::OnMetarResponse);
	Req->ProcessRequest();
	UE_LOG(LogParamotor, Log, TEXT("Weather: fetching %s"), *URL);
}

void UWeatherService::OnMetarResponse(TSharedPtr<IHttpRequest> Req, TSharedPtr<IHttpResponse> Resp, bool bConnected)
{
	if (!IsValid(GIRef)) return;
	UParamotorGameInstance* GI = GIRef;

	if (!bConnected || !Resp.IsValid() || Resp->GetResponseCode() != 200)
	{
		GI->LiveStatus = FString::Printf(TEXT("fetch failed (%s)"),
			Resp.IsValid() ? *FString::FromInt(Resp->GetResponseCode()) : TEXT("no connection"));
		GI->bLiveWeather = false;
		OnWeatherUpdated.Broadcast();
		return;
	}

	TArray<TSharedPtr<FJsonValue>> Arr;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Resp->GetContentAsString());
	if (!TJsonSerializer<>::Deserialize(Reader, Arr) || Arr.Num() == 0 || !Arr[0]->AsObject().IsValid())
	{
		GI->LiveStatus = TEXT("no METAR near this location");
		GI->bLiveWeather = false;
		OnWeatherUpdated.Broadcast();
		return;
	}

	TSharedPtr<FJsonObject> M = Arr[0]->AsObject();
	const double KtToMs = 0.51444f;

	GI->LiveStation = M->GetStringField(TEXT("icaoId"));
	GI->LiveMetarRaw = M->HasField(TEXT("rawOb")) ? M->GetStringField(TEXT("rawOb")) : TEXT("");
	// obsTime arrives as a Unix timestamp (older payloads used ISO strings)
	double ObsUnix = 0.0;
	if (M->TryGetNumberField(TEXT("obsTime"), ObsUnix))
	{
		const FDateTime ObsDT = FDateTime::FromUnixTimestamp((int64)ObsUnix);
		GI->LiveObsTime = ObsDT.ToString(TEXT("%H:%M Z"));
	}
	else if (M->HasField(TEXT("obsTime")))
	{
		GI->LiveObsTime = M->GetStringField(TEXT("obsTime"));
	}
	GI->LiveFlightCat = M->HasField(TEXT("fltCat")) ? M->GetStringField(TEXT("fltCat")) : TEXT("");

	double Tmp;
	if (M->TryGetNumberField(TEXT("temp"), Tmp))  GI->Temperature = (float)Tmp;
	if (M->TryGetNumberField(TEXT("dewp"), Tmp))  GI->DewPoint = (float)Tmp;
	if (M->TryGetNumberField(TEXT("wspd"), Tmp))  GI->WindSpeed = (float)Tmp * (float)KtToMs;
	if (M->TryGetNumberField(TEXT("wgst"), Tmp))  GI->WindGusts = (float)Tmp * (float)KtToMs;
	else GI->WindGusts = 0.0f;

	// wdir can be a number or "VRB"
	if (M->TryGetNumberField(TEXT("wdir"), Tmp))
	{
		GI->WindDirection = (float)Tmp;
	}

	// altim arrives in hPa → inHg
	if (M->TryGetNumberField(TEXT("altim"), Tmp)) GI->Altimeter_inHg = (float)Tmp * 0.02953f;

	// visib: number or "10+"
	if (M->TryGetNumberField(TEXT("visib"), Tmp)) GI->Visibility_mi = (float)Tmp;
	else if (M->HasField(TEXT("visib")))
	{
		FString V = M->GetStringField(TEXT("visib"));
		V.RemoveFromEnd(TEXT("+"));
		GI->Visibility_mi = FCString::Atof(*V);
	}

	// Relative humidity from temp/dewpoint (Magnus)
	const float T = GI->Temperature;
	const float Td = GI->DewPoint;
	GI->Humidity = FMath::Clamp(
		100.0f * FMath::Exp(17.625f * Td / (243.04f + Td) - 17.625f * T / (243.04f + T)),
		0.0f, 100.0f);

	// Cloud cover = worst reported layer
	float Worst = 0.0f;
	FString Layers;
	const TArray<TSharedPtr<FJsonValue>>* Clouds;
	if (M->TryGetArrayField(TEXT("clouds"), Clouds))
	{
		for (const auto& CV : *Clouds)
		{
			TSharedPtr<FJsonObject> CL = CV->AsObject();
			if (!CL.IsValid()) continue;
			FString Cover = CL->GetStringField(TEXT("cover"));
			double Base = 0.0;
			CL->TryGetNumberField(TEXT("base"), Base);
			float F = Cover == TEXT("OVC") ? 1.0f : Cover == TEXT("BKN") ? 0.7f
				: Cover == TEXT("SCT") ? 0.35f : Cover == TEXT("VV") ? 1.0f
				: Cover == TEXT("FEW") ? 0.15f : 0.0f;
			Worst = FMath::Max(Worst, F);
			if (F > 0.0f)
			{
				if (!Layers.IsEmpty()) Layers += TEXT("  ·  ");
				Layers += FString::Printf(TEXT("%s %.0fft"), *Cover, (float)Base);
			}
		}
	}
	GI->CloudCover = Worst;
	GI->LiveCloudLayers = Layers.IsEmpty() ? TEXT("clear") : Layers;

	// Local solar time from obs UTC + longitude (15°/hour)
	if (ObsUnix > 0.0)
	{
		const FDateTime ObsDT = FDateTime::FromUnixTimestamp((int64)ObsUnix);
		const double LonHours = GI->SelectedAirportLon / 15.0;
		double LocalH = ObsDT.GetTimeOfDay().GetTotalHours() + LonHours;
		GI->TimeOfDay = (float)FMath::Fmod(LocalH + 24.0, 24.0);
	}

	GI->LiveFetchUTC = FDateTime::UtcNow().ToUnixTimestamp();
	GI->LiveStatus = TEXT("live");
	OnWeatherUpdated.Broadcast();
	UE_LOG(LogParamotor, Log, TEXT("Weather: live METAR %s — wind %.0f°/%.1fm/s, temp %.1f°C"),
		*GI->LiveStation, GI->WindDirection, GI->WindSpeed, GI->Temperature);
}
