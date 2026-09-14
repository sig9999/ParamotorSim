// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LandCoverService.generated.h"

/**
 * NLCD class IDs (subset used by the sim).
 */
enum class ELandCoverClass : uint8
{
	Unknown = 0,
	OpenWater = 11,
	IceSnow = 12,
	DevOpen = 21,        // developed, open space (lawns/parks)
	DevLow = 22,
	DevMed = 23,
	DevHigh = 24,
	Barren = 31,
	DeciduousForest = 41,
	EvergreenForest = 42,
	MixedForest = 43,
	ShrubScrub = 52,
	Grassland = 71,
	PastureHay = 81,
	Cultivated = 82,
	WoodyWetlands = 90,
	EmergentWetlands = 95,
};

/** One classification sample — 4 channels packed per grid cell. */
struct FLandCoverPixel
{
	uint8 Cover = 0;      // ELandCoverClass
	uint8 Impervious = 0; // 0..100 %
	uint8 Road = 0;       // impervious descriptor: 0 none, 1..4 road classes, 5 non-road impervious
	uint8 Canopy = 0;     // tree-canopy % estimate
};

/**
 * ALandCoverService — the project's real-geography classification layer.
 *
 * Streams NLCD 2021 (CONUS) classification data as compact geodetic tiles:
 *   land cover class + impervious % + road/impervious descriptor + tree canopy %.
 * Tiles are fetched over WMS from the USGS/MRLC GeoServer (public domain data),
 * decoded async, cached to ProjectSavedDir/LandCover, and LRU-evicted.
 *
 * Tile grid: fixed 0.1° lat x 0.1° lon cells, 512x512 px (~22 m/px at 30°N).
 * Queries are pure lat/lon — no state or map boundaries exist anywhere.
 */
UCLASS()
class PARAMOTORSIM_API ALandCoverService : public AActor
{
	GENERATED_BODY()

public:
	ALandCoverService();

	virtual void Tick(float DeltaTime) override;

	/** Sample classification at a geodetic position. Returns false if the tile
	 *  isn't loaded yet (the fetch is kicked automatically). */
	bool GetPixelAt(double Lat, double Lon, FLandCoverPixel& Out);

	/** Fraction of a ~60m circle around Lat/Lon covered by forest classes
	 *  (41/42/43/90) — smooths the hard pixel boundary. -1 if no data. */
	float GetForestFractionAt(double Lat, double Lon);

	/** true while any tile overlapping a ~RadiusM circle around Lat/Lon is still
	 *  being fetched (kicks fetches for missing tiles). */
	bool IsAreaPending(double Lat, double Lon, double RadiusM);

	// ---- tunables ----------------------------------------------------
	/** Tile edge in degrees. */
	static constexpr double TileDeg = 0.1;
	/** Pixels per tile edge (per channel). */
	static constexpr int32 TileRes = 512;
	/** Live tiles kept in memory (LRU). ~1MB each. */
	static constexpr int32 MaxLiveTiles = 25;
	/** Disk cache cap (files). */
	static constexpr int32 MaxCachedFiles = 600;

private:
	struct FTileKey
	{
		int32 X = 0, Y = 0; // floor(lon/TileDeg), floor(lat/TileDeg)
		bool operator==(const FTileKey& O) const { return X == O.X && Y == O.Y; }
	};
	friend uint32 GetTypeHash(const FTileKey& K)
	{
		return HashCombine(::GetTypeHash(K.X), ::GetTypeHash(K.Y));
	}

	struct FTile
	{
		TArray<FLandCoverPixel> Px; // TileRes*TileRes
		double LastUsed = 0.0;
		bool bFetching = false;
		bool bFailed = false; // offline/no-data — stop retrying this session
		int32 ChannelsReceived = 0;
		TArray<uint8> Chan[4]; // raw per-channel scratch while assembling
	};

	TMap<FTileKey, TSharedPtr<FTile>> Tiles;

	FTileKey KeyFor(double Lat, double Lon) const;
	void KickFetch(const FTileKey& Key);
	void FetchChannel(const FTileKey& Key, int32 Channel);
	void OnChannelPng(const FTileKey& Key, int32 Channel, const TArray<uint8>& Png, bool bOK);
	void FinishTile(const FTileKey& Key);
	void EvictIfNeeded();

	FString CachePathFor(const FTileKey& Key) const;
	bool LoadFromDisk(const FTileKey& Key, FTile& Out);
	void SaveToDisk(const FTileKey& Key, const FTile& T);
	void BoundDiskCache();

	// decode helpers (background thread)
	static uint8 ClassFromColor(uint8 R, uint8 G, uint8 B);
	static uint8 PercentFromRamp(uint8 R, uint8 G, uint8 B); // white->dark ramp
	static uint8 RoadFromDescriptor(uint8 R, uint8 G, uint8 B);

	// WMS
	static FString WmsUrl(const FTileKey& Key, const FString& Layer);
	int32 FetchesInFlight = 0;
	static constexpr int32 MaxFetches = 6;
	TQueue<FTileKey, EQueueMode::Mpsc> FetchQueue;
	double Now() const { return GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0; }
};
