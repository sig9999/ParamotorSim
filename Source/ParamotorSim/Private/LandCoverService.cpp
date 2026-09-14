// Copyright Epic Games, Inc. All Rights Reserved.

#include "LandCoverService.h"
#include "ParamotorLogging.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "IImageWrapperModule.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "Async/Async.h"

// NLCD 2021 legend palette (canonical MRLC colors — verified against live tiles).
static const struct { uint8 C; uint8 R, G, B; } kNLCDPalette[] = {
	{ 11,  70,107,159 }, { 12, 209,222,248 },
	{ 21, 222,202,202 }, { 22, 217,148,130 }, { 23, 235,  0,  0 }, { 24, 171,  0,  0 },
	{ 31, 179,174,163 },
	{ 41, 104,171, 99 }, { 42,  28, 99, 48 }, { 43, 181,201,142 },
	{ 51, 165,140,105 }, { 52, 204,186,124 },
	{ 71, 226,226,193 }, { 72, 209,209,130 }, { 73, 163,204, 81 }, { 74, 130,186,158 },
	{ 81, 219,216, 61 }, { 82, 170,112, 40 },
	{ 90, 186,216,235 }, { 95, 112,163,186 },
};

static const TCHAR* kWmsLayers[] = {
	TEXT("NLCD_2021_Land_Cover_L48"),
	TEXT("NLCD_2021_Impervious_L48"),
	TEXT("NLCD_2021_Impervious_descriptor_L48"),
	TEXT("nlcd_tcc_conus_2021_v2021-4"),
};

ALandCoverService::ALandCoverService()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.25f;
}

FString ALandCoverService::WmsUrl(const FTileKey& Key, const FString& Layer)
{
	const double W = Key.X * TileDeg, S = Key.Y * TileDeg;
	return FString::Printf(
		TEXT("https://www.mrlc.gov/geoserver/mrlc_display/ows?service=WMS&version=1.1.1&request=GetMap&layers=%s&styles=&srs=EPSG:4326&bbox=%.6f,%.6f,%.6f,%.6f&width=%d&height=%d&format=image/png"),
		*Layer, W, S, W + TileDeg, S + TileDeg, TileRes, TileRes);
}

ALandCoverService::FTileKey ALandCoverService::KeyFor(double Lat, double Lon) const
{
	return FTileKey{ (int32)FMath::FloorToInt(Lon / TileDeg), (int32)FMath::FloorToInt(Lat / TileDeg) };
}

bool ALandCoverService::GetPixelAt(double Lat, double Lon, FLandCoverPixel& Out)
{
	const FTileKey Key = KeyFor(Lat, Lon);
	TSharedPtr<FTile>* Found = Tiles.Find(Key);
	if (!Found)
	{
		// try disk cache, else kick a fetch
		FTile Disk;
		if (LoadFromDisk(Key, Disk))
		{
			TSharedPtr<FTile> T = MakeShared<FTile>(MoveTemp(Disk));
			T->LastUsed = Now();
			Tiles.Add(Key, T);
			Found = Tiles.Find(Key);
		}
		else
		{
			KickFetch(Key);
			return false;
		}
	}
	FTile& T = **Found;
	T.LastUsed = Now();
	if (T.Px.Num() != TileRes * TileRes) return false;

	// WMS 1.1.1: bbox=(W,S,E,N), row 0 of the image is the north edge.
	const double W = Key.X * TileDeg, S = Key.Y * TileDeg;
	int32 PX = FMath::Clamp((int32)((Lon - W) / TileDeg * TileRes), 0, TileRes - 1);
	int32 PY = FMath::Clamp((int32)((S + TileDeg - Lat) / TileDeg * TileRes), 0, TileRes - 1);
	Out = T.Px[PY * TileRes + PX];
	return true;
}

float ALandCoverService::GetForestFractionAt(double Lat, double Lon)
{
	// ~60 m circle: sample center + 4 ring points at ~30 m offsets.
	const double dLat = 30.0 / 110540.0;
	const double dLon = 30.0 / (111320.0 * FMath::Cos(FMath::DegreesToRadians(Lat)));
	static const double Off[5][2] = { {0,0},{1,0},{-1,0},{0,1},{0,-1} };
	int Hits = 0, Total = 0;
	for (auto& O : Off)
	{
		FLandCoverPixel P;
		if (GetPixelAt(Lat + O[0] * dLat, Lon + O[1] * dLon, P))
		{
			Total++;
			if (P.Cover == 41 || P.Cover == 42 || P.Cover == 43 || P.Cover == 90) Hits++;
		}
	}
	return Total ? (float)Hits / Total : -1.0f;
}

bool ALandCoverService::IsAreaPending(double Lat, double Lon, double RadiusM)
{
	const double dLat = RadiusM / 110540.0;
	const double dLon = RadiusM / (111320.0 * FMath::Cos(FMath::DegreesToRadians(Lat)));
	const int32 X0 = FMath::FloorToInt((Lon - dLon) / TileDeg), X1 = FMath::FloorToInt((Lon + dLon) / TileDeg);
	const int32 Y0 = FMath::FloorToInt((Lat - dLat) / TileDeg), Y1 = FMath::FloorToInt((Lat + dLat) / TileDeg);
	bool bPending = false;
	for (int32 Y = Y0; Y <= Y1; ++Y)
		for (int32 X = X0; X <= X1; ++X)
		{
			const FTileKey K{ X, Y };
			TSharedPtr<FTile>* F = Tiles.Find(K);
			if (!F) { KickFetch(K); bPending = true; }
			else if ((*F)->bFetching) bPending = true;
		}
	return bPending;
}

void ALandCoverService::KickFetch(const FTileKey& Key)
{
	if (TSharedPtr<FTile>* F = Tiles.Find(Key))
	{
		if ((*F)->bFailed || (*F)->bFetching || (*F)->Px.Num() > 0) return;
	}
	FetchQueue.Enqueue(Key);
}

void ALandCoverService::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	while (FetchesInFlight < MaxFetches && !FetchQueue.IsEmpty())
	{
		FTileKey Key;
		FetchQueue.Dequeue(Key);
		if (TSharedPtr<FTile>* F = Tiles.Find(Key))
			if ((*F)->Px.Num() > 0 || (*F)->bFetching || (*F)->bFailed) continue;

		if (!Tiles.Contains(Key)) Tiles.Add(Key, MakeShared<FTile>());
		Tiles[Key]->bFetching = true;
		for (int32 Ch = 0; Ch < 4; ++Ch) FetchChannel(Key, Ch);
	}
	EvictIfNeeded();
}

void ALandCoverService::FetchChannel(const FTileKey& Key, int32 Channel)
{
	FetchesInFlight++;
	TWeakObjectPtr<ALandCoverService> Weak(this);
	TSharedRef<IHttpRequest> Req = FHttpModule::Get().CreateRequest();
	Req->SetURL(WmsUrl(Key, kWmsLayers[Channel]));
	Req->SetVerb(TEXT("GET"));
	Req->SetTimeout(30.0f);
	Req->OnProcessRequestComplete().BindLambda(
		[Weak, Key, Channel](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOK)
		{
			TArray<uint8> Bytes;
			const bool bGood = bOK && Resp.IsValid() && Resp->GetResponseCode() == 200
				&& Resp->GetContent().Num() > 100;
			if (bGood) Bytes = Resp->GetContent();
			if (ALandCoverService* S = Weak.Get())
			{
				S->FetchesInFlight--;
				S->OnChannelPng(Key, Channel, Bytes, bGood);
			}
		});
	Req->ProcessRequest();
}

void ALandCoverService::OnChannelPng(const FTileKey& Key, int32 Channel, const TArray<uint8>& Png, bool bOK)
{
	TSharedPtr<FTile>* Found = Tiles.Find(Key);
	if (!Found) return;
	FTile& T = **Found;

	if (!bOK)
	{
		UE_LOG(LogParamotor, Warning, TEXT("LandCover: tile %d,%d ch%d fetch failed"), Key.X, Key.Y, Channel);
		T.bFailed = true;
		T.bFetching = false;
		return;
	}

	// Decode the PNG on a worker thread, merge back on the game thread.
	TWeakObjectPtr<ALandCoverService> Weak(this);
	TArray<uint8> Copy = Png;
	Async(EAsyncExecution::ThreadPool, [Weak, Key, Channel, Copy = MoveTemp(Copy)]()
	{
		TArray<uint8> Chan;
		IImageWrapperModule& Mod = FModuleManager::LoadModuleChecked<IImageWrapperModule>("ImageWrapper");
		TSharedPtr<IImageWrapper> W = Mod.CreateImageWrapper(EImageFormat::PNG);
		if (W->SetCompressed(Copy.GetData(), Copy.Num()) &&
			W->GetWidth() == TileRes && W->GetHeight() == TileRes)
		{
			TArray<uint8> Raw;
			if (W->GetRaw(ERGBFormat::BGRA, 8, Raw))
			{
				Chan.SetNumUninitialized(TileRes * TileRes);
				for (int32 i = 0; i < TileRes * TileRes; ++i)
				{
					const uint8 B = Raw[i * 4 + 0], G = Raw[i * 4 + 1], R = Raw[i * 4 + 2];
					switch (Channel)
					{
					case 0: Chan[i] = ClassFromColor(R, G, B); break;
					case 1: Chan[i] = PercentFromRamp(R, G, B); break;
					case 2: Chan[i] = RoadFromDescriptor(R, G, B); break;
					case 3: Chan[i] = PercentFromRamp(R, G, B); break; // TCC green ramp
					}
				}
			}
		}
		AsyncTask(ENamedThreads::GameThread, [Weak, Key, Channel, Chan = MoveTemp(Chan)]() mutable
		{
			ALandCoverService* S = Weak.Get();
			if (!S || Chan.Num() == 0) return;
			TSharedPtr<FTile>* F = S->Tiles.Find(Key);
			if (!F) return;
			(*F)->Chan[Channel] = MoveTemp(Chan);
			(*F)->ChannelsReceived |= (1 << Channel);
			if ((*F)->ChannelsReceived == 0xF) S->FinishTile(Key);
		});
	});
}

void ALandCoverService::FinishTile(const FTileKey& Key)
{
	TSharedPtr<FTile>* Found = Tiles.Find(Key);
	if (!Found) return;
	FTile& T = **Found;
	T.Px.SetNumUninitialized(TileRes * TileRes);
	for (int32 i = 0; i < TileRes * TileRes; ++i)
	{
		FLandCoverPixel& P = T.Px[i];
		P.Cover = T.Chan[0][i];
		P.Impervious = T.Chan[1][i];
		P.Road = T.Chan[2][i];
		P.Canopy = T.Chan[3][i];
	}
	for (auto& C : T.Chan) C.Empty();
	T.bFetching = false;
	SaveToDisk(Key, T);
	UE_LOG(LogParamotor, Log, TEXT("LandCover: tile %d,%d ready"), Key.X, Key.Y);
}

void ALandCoverService::EvictIfNeeded()
{
	if (Tiles.Num() <= MaxLiveTiles) return;
	// drop the stalest fully-loaded tiles
	TArray<TPair<double, FTileKey>> ByAge;
	for (auto& KV : Tiles)
		if (!KV.Value->bFetching) ByAge.Add({ KV.Value->LastUsed, KV.Key });
	ByAge.Sort([](auto& A, auto& B) { return A.Key < B.Key; });
	const int32 Excess = Tiles.Num() - MaxLiveTiles;
	for (int32 i = 0; i < Excess && i < ByAge.Num(); ++i)
		Tiles.Remove(ByAge[i].Value);
}

// ---- disk cache ----------------------------------------------------

FString ALandCoverService::CachePathFor(const FTileKey& Key) const
{
	return FPaths::ProjectSavedDir() / TEXT("LandCover") /
		FString::Printf(TEXT("lc_%d_%d.bin"), Key.X, Key.Y);
}

bool ALandCoverService::LoadFromDisk(const FTileKey& Key, FTile& Out)
{
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *CachePathFor(Key))) return false;
	const int32 Need = 8 + TileRes * TileRes * 4;
	if (Bytes.Num() != Need || *(int32*)Bytes.GetData() != 0x4C435631 /*LCV1*/) return false;
	Out.Px.SetNumUninitialized(TileRes * TileRes);
	FMemory::Memcpy(Out.Px.GetData(), Bytes.GetData() + 8, TileRes * TileRes * 4);
	return true;
}

void ALandCoverService::SaveToDisk(const FTileKey& Key, const FTile& T)
{
	TArray<uint8> Bytes;
	const int32 Magic = 0x4C435631;
	Bytes.Append((uint8*)&Magic, 4);
	const int32 Res = TileRes;
	Bytes.Append((uint8*)&Res, 4);
	Bytes.Append((uint8*)T.Px.GetData(), TileRes * TileRes * 4);
	const FString Path = CachePathFor(Key);
	FFileHelper::SaveArrayToFile(Bytes, *Path);
	BoundDiskCache();
}

void ALandCoverService::BoundDiskCache()
{
	const FString Dir = FPaths::ProjectSavedDir() / TEXT("LandCover");
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(Dir / TEXT("lc_*.bin")), true, false);
	if (Files.Num() <= MaxCachedFiles) return;
	TArray<TPair<FDateTime, FString>> ByAge;
	for (const FString& F : Files)
		ByAge.Add({ IFileManager::Get().GetTimeStamp(*(Dir / F)), F });
	ByAge.Sort([](auto& A, auto& B) { return A.Key < B.Key; });
	const int32 Excess = Files.Num() - MaxCachedFiles;
	for (int32 i = 0; i < Excess; ++i)
		IFileManager::Get().Delete(*(Dir / ByAge[i].Value));
}

// ---- pixel decoders -------------------------------------------------

uint8 ALandCoverService::ClassFromColor(uint8 R, uint8 G, uint8 B)
{
	if (R > 245 && G > 245 && B > 245) return 0; // outside coverage
	uint8 Best = 0; int32 BestD = MAX_int32;
	for (const auto& E : kNLCDPalette)
	{
		const int32 dR = R - E.R, dG = G - E.G, dB = B - E.B;
		const int32 D = dR * dR + dG * dG + dB * dB;
		if (D < BestD) { BestD = D; Best = E.C; }
	}
	return Best;
}

uint8 ALandCoverService::PercentFromRamp(uint8 R, uint8 G, uint8 B)
{
	// Both the impervious (white->red) and TCC (white->green) styles are
	// monotonic luminance ramps ending ~lum 110-150 at 100%.
	const int32 Lum = (R + 2 * G + B) / 4; // green-weighted luma
	return (uint8)FMath::Clamp((255 - Lum) * 100 / 150, 0, 100);
}

uint8 ALandCoverService::RoadFromDescriptor(uint8 R, uint8 G, uint8 B)
{
	if (R > 245 && G > 245 && B > 245) return 0;   // non-impervious
	if (B > 150 && R < 100) return 1;              // blue   — primary road
	if (R > 200 && G > 200 && B < 100) return 2;   // yellow — secondary road
	if (R > 200 && G < 100 && B < 100) return 3;   // red    — tertiary/other road
	return 5;                                       // pink   — non-road impervious
}
