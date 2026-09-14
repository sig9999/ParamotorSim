// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Cesium world helpers — the project doesn't link the Cesium module directly,
 * so these talk to the plugin via reflection (FindObject + property writes).
 * Used by the flight GameMode for globe maps; will also drive the Google
 * Photorealistic 3D Tiles layer once a Maps API key lands.
 */
namespace CesiumWorld
{
	/** True when the world already has a CesiumGeoreference actor. */
	bool DetectGlobe(UWorld* World);

	/** Spawn a georeference at the given lat/lon/height + a Cesium World
	 *  Terrain tileset with a Bing Maps Aerial raster overlay.
	 *  Returns true when the georeference was created. */
	bool SetupGlobeTerrain(UWorld* World, double Lat, double Lon, double HeightM);

	/** Push high-detail streaming knobs onto every Cesium3DTileset in the level. */
	void SetHires(UWorld* World);

	/** Flight maps only: destroy saved tileset actors (their overlays/culling
	 *  are already bound) and spawn a flight-configured tileset — World
	 *  Terrain + Bing base + USGS NAIP, physics meshes on, culling off. */
	void EnsureFlightTileset(UWorld* World);

	/** lat/lon/height → Unreal world position via the first georeference. */
	FVector GeodeticToUnreal(UWorld* World, double Lon, double Lat, double HeightM);

	/** Unreal world position → (lon, lat, heightM) via the first georeference. */
	FVector UnrealToGeodetic(UWorld* World, const FVector& UnrealPos);

	/** Current georeference origin as (lon, lat, heightM). False w/o georef. */
	bool GetGeorefOrigin(UWorld* World, FVector& OutLLH);

	/** Attach a CesiumGlobeAnchorComponent so the actor stays glued to the
	 *  globe across georeference origin rebasing. No-op without a georeference. */
	void AnchorToGlobe(UWorld* World, AActor* Actor);

	/** Re-center an existing (level-saved) georeference's origin on the given
	 *  lat/lon/height — saved georeferences point wherever they were authored. */
	void SetGeoreferenceOrigin(UWorld* World, double Lon, double Lat, double HeightM);

	/** Spawn a CesiumCartographicPolygon from geodetic (lon,lat,h) points. */
	AActor* SpawnClipPolygon(UWorld* World, const TArray<FVector>& LLHPoints);

	/** Attach a polygon raster-overlay clipper to the PRT tileset so tiles
	 *  inside the polygons are excluded (kills melted-PRT landmark shells).
	 *  Returns the overlay component for Refresh retries, or nullptr. */
	UActorComponent* AddPolygonClipper(UWorld* World, const TArray<AActor*>& Polygons);

	/** EGM96 geoid undulation N (meters, negative in CONUS) via IDW over an
	 *  embedded sample grid — ±3 m worst case. MSL ≈ ellipsoid h − N.
	 *  Diagnostic/datum-bridging estimate, NOT survey grade. */
	double GeoidUndulationM(double Lat, double Lon);
}
