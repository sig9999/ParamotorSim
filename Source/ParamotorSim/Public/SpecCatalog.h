// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SpecCatalog.generated.h"

/**
 * Real-world part catalog — spec structs for the hangar configurator.
 * Data lives in the GameInstance's spec tables (filled by EnsureSpecData);
 * these structs describe a manufacturer's actual published numbers.
 */

USTRUCT(BlueprintType)
struct FParamotorWingSpec
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FString Brand;
	UPROPERTY(BlueprintReadOnly) FString Model;
	UPROPERTY(BlueprintReadOnly) FString Size;
	UPROPERTY(BlueprintReadOnly) int32 MaxWeightKg = 0;
	UPROPERTY(BlueprintReadOnly) int32 MaxWeightLb = 0;
	UPROPERTY(BlueprintReadOnly) FString TrimSpeeds;
	UPROPERTY(BlueprintReadOnly) FString TopSpeed;
	// Full spec sheet for the wing catalog detail panel.
	UPROPERTY(BlueprintReadOnly) int32 Cells = 0;
	UPROPERTY(BlueprintReadOnly) float FlatArea_m2 = 0.0f;
	UPROPERTY(BlueprintReadOnly) float ProjectedArea_m2 = 0.0f;
	UPROPERTY(BlueprintReadOnly) float FlatSpan_m = 0.0f;
	UPROPERTY(BlueprintReadOnly) float ProjectedSpan_m = 0.0f;
	UPROPERTY(BlueprintReadOnly) float AspectRatioFlat = 0.0f;
	UPROPERTY(BlueprintReadOnly) float AspectRatioProjected = 0.0f;
	UPROPERTY(BlueprintReadOnly) float WingWeight_kg = 0.0f;
	UPROPERTY(BlueprintReadOnly) FString TowRange;      // take-off weight range
	UPROPERTY(BlueprintReadOnly) FString Certification; // EN/LTF rating
	UPROPERTY(BlueprintReadOnly) FString Risers;        // riser/line config
	UPROPERTY(BlueprintReadOnly) FString Colors;        // stock colorways
	UPROPERTY(BlueprintReadOnly) FString ImagePath;     // planform texture for the catalog
	UPROPERTY(BlueprintReadOnly) FString Notes;

	// Flight model — feeds AWingActor::WingModel when this wing is flown.
	UPROPERTY(BlueprintReadOnly) float CL0 = 0.50f;      // cambered lift at 0 AoA
	UPROPERTY(BlueprintReadOnly) float CLMax = 1.45f;    // max lift coefficient
	UPROPERTY(BlueprintReadOnly) float CD0 = 0.035f;     // parasite drag
	UPROPERTY(BlueprintReadOnly) float StallAoA = 16.0f; // degrees
	UPROPERTY(BlueprintReadOnly) float TrimAoA = 1.5f;   // trim AoA, degrees
};

USTRUCT(BlueprintType)
struct FParamotorEngineSpec
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FString Brand;
	UPROPERTY(BlueprintReadOnly) FString Model;
	UPROPERTY(BlueprintReadOnly) float Displacement_cc = 0.0f;
	UPROPERTY(BlueprintReadOnly) float Power_hp = 0.0f;
	UPROPERTY(BlueprintReadOnly) float Torque_Nm = 0.0f;
	UPROPERTY(BlueprintReadOnly) float Weight_kg = 0.0f;
	UPROPERTY(BlueprintReadOnly) float IdleRPM = 2500.0f;
	UPROPERTY(BlueprintReadOnly) float RedlineRPM = 7800.0f;
	UPROPERTY(BlueprintReadOnly) FString HubSize;
	UPROPERTY(BlueprintReadOnly) FString StaticThrustByProp;
	UPROPERTY(BlueprintReadOnly) FString Notes;
};

USTRUCT(BlueprintType)
struct FParamotorFrameSpec
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FString Brand;
	UPROPERTY(BlueprintReadOnly) FString Model;
	UPROPERTY(BlueprintReadOnly) float Weight_kg = 0.0f;
	UPROPERTY(BlueprintReadOnly) FString EngineMount;
	UPROPERTY(BlueprintReadOnly) FString Notes;
};

USTRUCT(BlueprintType)
struct FParamotorPropSpec
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FString Brand;
	UPROPERTY(BlueprintReadOnly) FString Model;
	UPROPERTY(BlueprintReadOnly) FString Diameter;
	UPROPERTY(BlueprintReadOnly) FString Pitch;
	UPROPERTY(BlueprintReadOnly) float Weight_kg = 0.0f;
	UPROPERTY(BlueprintReadOnly) FString HubSize;
	UPROPERTY(BlueprintReadOnly) FString StaticThrust;
	UPROPERTY(BlueprintReadOnly) FString Notes;
};

USTRUCT(BlueprintType)
struct FParamotorHarnessSpec
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FString Brand;
	UPROPERTY(BlueprintReadOnly) FString Model;
	UPROPERTY(BlueprintReadOnly) float Weight_kg = 0.0f;
	UPROPERTY(BlueprintReadOnly) FString Sizes;
	UPROPERTY(BlueprintReadOnly) FString Notes;
};

/** A curated flat-map launch field — drives spawn, weather focus, and the
 *  world-map FLY flow. LocalSpawn is precomputed world-cm on MapName. */
USTRUCT(BlueprintType)
struct FLaunchSite
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FString Name;
	UPROPERTY(BlueprintReadOnly) FString ICAO;
	UPROPERTY(BlueprintReadOnly) double Lat = 0.0;
	UPROPERTY(BlueprintReadOnly) double Lon = 0.0;
	UPROPERTY(BlueprintReadOnly) float Elev_m = 0.0f;
	/** Flat-map this site lives on (empty = globe-only). */
	UPROPERTY(BlueprintReadOnly) FString MapName;
	/** World-cm spawn position on that flat map (precomputed vs the map origin). */
	UPROPERTY(BlueprintReadOnly) FVector LocalSpawn = FVector::ZeroVector;
};

/** The player's currently equipped hangar build — indices into the spec tables. */
USTRUCT(BlueprintType)
struct FParamotorBuild
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadWrite) int32 EngineIndex = -1;
	UPROPERTY(BlueprintReadWrite) int32 FrameIndex = -1;
	UPROPERTY(BlueprintReadWrite) int32 PropIndex = -1;
	UPROPERTY(BlueprintReadWrite) int32 HarnessIndex = -1;
	UPROPERTY(BlueprintReadWrite) int32 WingIndex = -1;
};
