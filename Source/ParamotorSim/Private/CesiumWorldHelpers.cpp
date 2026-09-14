// Copyright Epic Games, Inc. All Rights Reserved.

#include "CesiumWorldHelpers.h"
#include "ParamotorLogging.h"
#include "ParamotorGameInstance.h"
#include "ReflectionProps.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

static void ConfigureFlightTileset(AActor* A);
static AActor* SpawnFlightTileset(UWorld* World);

// The ion access token is scrubbed from committed assets
// (LOCAL_CESIUM_ION_TOKEN_REMOVED_FOR_SOURCE_CONTROL). Developers keep the
// real token in <Project>/Saved/LocalSecrets/cesium_token.txt — Saved/ is
// never committed. These helpers re-inject it at runtime before tile
// requests fire.
static const TCHAR* LocalIonTokenPath()
{
	static FString P = FPaths::ProjectDir() / TEXT("Saved/LocalSecrets/cesium_token.txt");
	return *P;
}

static FString LoadLocalIonToken()
{
	FString T;
	if (FFileHelper::LoadFileToString(T, LocalIonTokenPath()))
	{
		T.TrimStartAndEndInline();
	}
	return T;
}

static bool PatchIonToken(UObject* O, const FString& Token)
{
	if (!O || Token.IsEmpty()) return false;
	bool bPatched = false;
	for (const FName PropName : {FName(TEXT("IonAccessToken")), FName(TEXT("DefaultIonAccessToken"))})
	{
		FProperty* P = O->GetClass()->FindPropertyByName(PropName);
		FStrProperty* S = CastField<FStrProperty>(P);
		if (!S) continue;
		FString* V = S->ContainerPtrToValuePtr<FString>(O);
		if (V->Contains(TEXT("REMOVED_FOR_SOURCE_CONTROL")) || V->IsEmpty())
		{
			*V = Token;
			bPatched = true;
		}
	}
	return bPatched;
}

// Re-injects the local ion token into the shared CesiumIonServer settings and
// every tileset/overlay still carrying the scrubbed placeholder, then asks
// each patched tileset to refresh so it re-requests its endpoint with the
// real token.
static void InjectIonTokens(UWorld* World)
{
	if (!World) return;
	static FString Token = LoadLocalIonToken();
	if (Token.IsEmpty())
	{
		UE_LOG(LogParamotor, Warning, TEXT("CesiumWorld: no local ion token at %s"), LocalIonTokenPath());
		return;
	}

	if (UObject* Server = LoadObject<UObject>(nullptr, TEXT("/Game/CesiumSettings/CesiumIonServers/CesiumIonSaaS.CesiumIonSaaS")))
	{
		if (PatchIonToken(Server, Token))
			UE_LOG(LogParamotor, Log, TEXT("CesiumWorld: ion token injected into CesiumIonSaaS defaults"));
	}
	if (UObject* Server = FindObject<UObject>(nullptr, TEXT("/Script/CesiumRuntime.Default__CesiumIonServer")))
		PatchIonToken(Server, Token);

	UClass* TilesetClass = FindObject<UClass>(nullptr, TEXT("/Script/CesiumRuntime.Cesium3DTileset"));
	if (!TilesetClass) return;
	TArray<AActor*> Tilesets;
	UGameplayStatics::GetAllActorsOfClass(World, TilesetClass, Tilesets);
	for (AActor* A : Tilesets)
	{
		if (!A) continue;
		bool bAny = PatchIonToken(A, Token);
		TArray<UActorComponent*> Comps;
		A->GetComponents(Comps);
		for (UActorComponent* C : Comps)
			if (PatchIonToken(C, Token)) bAny = true;
		if (bAny)
		{
			if (UFunction* F = A->FindFunction(TEXT("RefreshTileset")))
				A->ProcessEvent(F, nullptr);
			UE_LOG(LogParamotor, Log, TEXT("CesiumWorld: ion token injected into tileset %s (refreshed)"), *A->GetName());
		}
	}
}

bool CesiumWorld::DetectGlobe(UWorld* World)
{
	UClass* GeoClass = FindObject<UClass>(nullptr, TEXT("/Script/CesiumRuntime.CesiumGeoreference"));
	if (!GeoClass || !World) return false;
	TArray<AActor*> Geos;
	UGameplayStatics::GetAllActorsOfClass(World, GeoClass, Geos);
	if (Geos.Num() > 0)
		InjectIonTokens(World);
	return Geos.Num() > 0;
}

bool CesiumWorld::SetupGlobeTerrain(UWorld* World, double Lat, double Lon, double HeightM)
{
	if (!World) return false;

	// Cesium georeference centered on the selected airport
	UClass* GeoClass = FindObject<UClass>(nullptr, TEXT("/Script/CesiumRuntime.CesiumGeoreference"));
	bool bGeoSpawned = false;
	if (GeoClass)
	{
		AActor* Geo = World->SpawnActor<AActor>(GeoClass, FVector::ZeroVector, FRotator::ZeroRotator);
		if (Geo)
		{
			Refl::SetNumeric(Geo, TEXT("OriginLatitude"), Lat);
			Refl::SetNumeric(Geo, TEXT("OriginLongitude"), Lon);
			Refl::SetNumeric(Geo, TEXT("OriginHeight"), HeightM);
			bGeoSpawned = true;
			UE_LOG(LogParamotor, Log, TEXT("CesiumWorld: georeference spawned at %.4f, %.4f"), Lat, Lon);
		}
	}

	// Cesium World Terrain + overlays, spawned through the shared path
	SpawnFlightTileset(World);
	return bGeoSpawned;
}

// Deferred-spawns the flight tileset: Cesium World Terrain (ion 1) + Bing
// Aerial base (ion 3) + USGS NAIP top layer (0.6m/px CONUS). Overlays are
// created BEFORE FinishSpawningActor — raster overlays bind during the
// tileset's BeginPlay, so they cannot be attached to a streaming tileset.
static AActor* SpawnFlightTileset(UWorld* World)
{
	UClass* TilesetClass = FindObject<UClass>(nullptr, TEXT("/Script/CesiumRuntime.Cesium3DTileset"));
	if (!World || !TilesetClass) return nullptr;

	AActor* Terrain = World->SpawnActorDeferred<AActor>(TilesetClass,
		FTransform::Identity, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Terrain) return nullptr;

	Refl::SetInt(Terrain, TEXT("IonAssetID"), 1);
	Refl::SetNumeric(Terrain, TEXT("MaximumCachedBytes"), 2147483648.0);
	Refl::SetInt(Terrain, TEXT("MaximumSimultaneousTileLoads"), 64);
	// SSE 4.0 is the deepest LOD the source data can feed at foot level —
	// going lower just blows the texture pool (imagery is already at its
	// ~1m/px floor and lower-mip tiles pile up).
	Refl::SetNumeric(Terrain, TEXT("MaximumScreenSpaceError"), 4.0);

	// Physics: the player lands/walks on streamed tiles, so they must
	// generate collision meshes — and must NOT be unloaded by
	// frustum/occlusion culling or the ground vanishes under anyone
	// standing outside the camera view.
	Refl::SetBool(Terrain, TEXT("CreatePhysicsMeshes"), true);
	Refl::SetBool(Terrain, TEXT("EnableFrustumCulling"), false);
	Refl::SetBool(Terrain, TEXT("EnableOcclusionCulling"), false);

	// Bing Maps Aerial (ion asset 3) — base layer, works worldwide.
	if (UClass* OverlayClass = FindObject<UClass>(nullptr, TEXT("/Script/CesiumRuntime.CesiumIonRasterOverlay")))
	{
		UActorComponent* Overlay = NewObject<UActorComponent>(Terrain, OverlayClass, TEXT("BingAerial"));
		if (Overlay)
		{
			Refl::SetInt(Overlay, TEXT("IonAssetID"), 3);
			Overlay->RegisterComponent();
		}
	}

	// USGS NAIP — 0.6m/px public-domain aerial imagery, CONUS only (same
	// source the baked L_Westerly map uses). Stacks over Bing; missing tiles
	// outside US coverage fall through to the base layer. ArcGIS uses XYZ
	// scheme (y from the north) — {reverseY} supplies the row.
	if (UClass* UrlOverlayClass = FindObject<UClass>(nullptr, TEXT("/Script/CesiumRuntime.CesiumUrlTemplateRasterOverlay")))
	{
		UActorComponent* Naip = NewObject<UActorComponent>(Terrain, UrlOverlayClass, TEXT("USGSNAIP"));
		if (Naip)
		{
			const TCHAR* NaipUrl = TEXT("https://basemap.nationalmap.gov/arcgis/rest/services/USGSImageryOnly/MapServer/tile/{z}/{reverseY}/{x}");
			Refl::SetString(Naip, TEXT("Url"), NaipUrl);
			Refl::SetString(Naip, TEXT("TemplateUrl"), NaipUrl);
			Naip->RegisterComponent();
		}
	}

	UGameplayStatics::FinishSpawningActor(Terrain, FTransform::Identity);
	Terrain->Tags.Add(TEXT("FlightTileset"));
	ConfigureFlightTileset(Terrain);
	UE_LOG(LogParamotor, Log, TEXT("CesiumWorld: flight tileset spawned (World Terrain + Bing + NAIP)"));

	// Cesium OSM Buildings (ion 96188) — real 3D building geometry,
	// worldwide. Fills in towns/cities under the imagery. Collision OFF —
	// we land on terrain, not roofs; keeping it visual-only halves the cost.
	if (AActor* Buildings = World->SpawnActorDeferred<AActor>(TilesetClass,
		FTransform::Identity, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn))
	{
		Refl::SetInt(Buildings, TEXT("IonAssetID"), 96188);
		Refl::SetNumeric(Buildings, TEXT("MaximumScreenSpaceError"), 8.0);
		Refl::SetBool(Buildings, TEXT("CreatePhysicsMeshes"), false);
		Refl::SetNumeric(Buildings, TEXT("MaximumCachedBytes"), 536870912.0);
		Refl::SetInt(Buildings, TEXT("MaximumSimultaneousTileLoads"), 32);
		UGameplayStatics::FinishSpawningActor(Buildings, FTransform::Identity);
		Buildings->Tags.Add(TEXT("FlightTileset"));
		UE_LOG(LogParamotor, Log, TEXT("CesiumWorld: OSM Buildings tileset spawned (ion 96188)"));
	}

	// Google Photorealistic 3D Tiles — needs a Maps Platform API key
	// (GameInstance->GoogleMapsApiKey). When set, this REPLACES the OSM
	// buildings layer: photogrammetry cities + real tree geometry, the
	// highest-fidelity global 3D available.
	UParamotorGameInstance* GI = World->GetGameInstance<UParamotorGameInstance>();
	if (GI && !GI->GoogleMapsApiKey.IsEmpty())
	{
		UClass* UrlTilesetClass = FindObject<UClass>(nullptr, TEXT("/Script/CesiumRuntime.Cesium3DTileset"));
		AActor* Google = World->SpawnActorDeferred<AActor>(UrlTilesetClass,
			FTransform::Identity, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Google)
		{
			const FString TilesUrl = FString::Printf(
				TEXT("https://tile.googleapis.com/v1/3dtiles/root.json?key=%s"),
				*GI->GoogleMapsApiKey);
			Refl::SetString(Google, TEXT("Url"), TilesUrl);
			Refl::SetInt(Google, TEXT("IonAssetID"), 0);   // URL source, not ion
			Refl::SetNumeric(Google, TEXT("MaximumScreenSpaceError"), 4.0);
			Refl::SetBool(Google, TEXT("CreatePhysicsMeshes"), true);
			Refl::SetNumeric(Google, TEXT("MaximumCachedBytes"), 2147483648.0);
			Refl::SetInt(Google, TEXT("MaximumSimultaneousTileLoads"), 64);
			UGameplayStatics::FinishSpawningActor(Google, FTransform::Identity);
			Google->Tags.Add(TEXT("FlightTileset"));
			UE_LOG(LogParamotor, Log, TEXT("CesiumWorld: Google Photorealistic 3D Tiles enabled"));
		}
	}
	return Terrain;
}

// Applies the flight-ready configuration to every Cesium3DTileset in the
// level. The physics/culling/streaming flags are read per-frame so they
// take effect on already-streaming saved tilesets; raster overlays do NOT
// retro-attach (they bind at tileset BeginPlay), so the USGS NAIP layer
// only applies on tilesets spawned fresh through SpawnFlightTileset —
// i.e. maps with no saved tileset.
void CesiumWorld::EnsureFlightTileset(UWorld* World)
{
	if (!World) return;
	UClass* TilesetClass = FindObject<UClass>(nullptr, TEXT("/Script/CesiumRuntime.Cesium3DTileset"));
	if (!TilesetClass) return;

	TArray<AActor*> Tilesets;
	InjectIonTokens(World);
	UGameplayStatics::GetAllActorsOfClass(World, TilesetClass, Tilesets);
	for (AActor* A : Tilesets)
	{
		ConfigureFlightTileset(A);
	}
	UE_LOG(LogParamotor, Log, TEXT("CesiumWorld: configured %d tileset(s) for flight"), Tilesets.Num());
}

// Applies the flight-ready configuration to one tileset actor: physics
// meshes on, culling off (so ground exists under anyone off-camera),
// hi-res streaming knobs, and the USGS NAIP overlay for CONUS detail.
static void ConfigureFlightTileset(AActor* A)
{
	if (!A) return;
	Refl::SetNumeric(A, TEXT("MaximumScreenSpaceError"), 4.0);
	Refl::SetBool(A, TEXT("CreatePhysicsMeshes"), true);
	Refl::SetBool(A, TEXT("EnableFrustumCulling"), false);
	Refl::SetBool(A, TEXT("EnableOcclusionCulling"), false);
	// With frustum culling off, "would-be-culled" tiles keep loading but freeze
	// at whatever coarse LOD they first reached — giant shells that linger as
	// phantom collision/shadow overhead. Enforce a floor LOD so they refine to
	// moderate detail (SSE 32 = coarse but surface-accurate) instead.
	Refl::SetBool(A, TEXT("EnforceCulledScreenSpaceError"), true);
	Refl::SetNumeric(A, TEXT("CulledScreenSpaceError"), 32.0);
	FProperty* P;
	if ((P = A->GetClass()->FindPropertyByName(FName(TEXT("PreloadAncestors")))))
		if (FBoolProperty* B = CastField<FBoolProperty>(P)) B->SetPropertyValue(P->ContainerPtrToValuePtr<void>(A), true);
	if ((P = A->GetClass()->FindPropertyByName(FName(TEXT("PreloadSiblings")))))
		if (FBoolProperty* B = CastField<FBoolProperty>(P)) B->SetPropertyValue(P->ContainerPtrToValuePtr<void>(A), true);
	if ((P = A->GetClass()->FindPropertyByName(FName(TEXT("ForbidHoles")))))
		if (FBoolProperty* B = CastField<FBoolProperty>(P)) B->SetPropertyValue(P->ContainerPtrToValuePtr<void>(A), true);
	Refl::SetInt(A, TEXT("MaximumSimultaneousTileLoads"), 64);
	Refl::SetInt(A, TEXT("LoadingDescendantLimit"), 1000);

	TArray<UActorComponent*> Comps;
	A->GetComponents(Comps);
	for (UActorComponent* C : Comps)
	{
		if (!C) continue;
		if (C->GetClass()->GetName().Contains(TEXT("Raster")))
			Refl::SetNumeric(C, TEXT("MaximumScreenSpaceError"), 4.0);
	}
	// NOTE: adding a raster overlay after the tileset has begun streaming
	// does not take effect (overlays bind during tileset BeginPlay), so a
	// USGS NAIP url-template overlay must be created before FinishSpawning —
	// or baked into the level's tileset in the editor.
}

void CesiumWorld::SetHires(UWorld* World)
{
	UClass* TilesetClass = FindObject<UClass>(nullptr, TEXT("/Script/CesiumRuntime.Cesium3DTileset"));
	if (!TilesetClass || !World) return;

	TArray<AActor*> Tilesets;
	UGameplayStatics::GetAllActorsOfClass(World, TilesetClass, Tilesets);
	for (AActor* A : Tilesets)
	{
		ConfigureFlightTileset(A);
	}

	UE_LOG(LogParamotor, Log, TEXT("CesiumWorld: set %d tileset actors to high-detail streaming"), Tilesets.Num());
}

FVector CesiumWorld::GeodeticToUnreal(UWorld* World, double Lon, double Lat, double HeightM)
{
	UClass* GeoClass = FindObject<UClass>(nullptr, TEXT("/Script/CesiumRuntime.CesiumGeoreference"));
	if (!GeoClass || !World) return FVector::ZeroVector;

	TArray<AActor*> Geos;
	UGameplayStatics::GetAllActorsOfClass(World, GeoClass, Geos);
	if (Geos.Num() == 0) return FVector::ZeroVector;

	AActor* Geo = Geos[0];
	// Newer Cesium versions renamed the transform — try the current name first.
	UFunction* Func = Geo->FindFunction(TEXT("TransformLongitudeLatitudeHeightPositionToUnreal"));
	if (!Func)
	{
		Func = Geo->FindFunction(TEXT("TransformLongitudeLatitudeHeightToUnreal"));
	}
	if (!Func) return FVector::ZeroVector;

	// Params buffer matching the function's layout
	uint8* Params = (uint8*)FMemory_Alloca(Func->ParmsSize);
	FMemory::Memzero(Params, Func->ParmsSize);

	if (FProperty* InProp = Func->FindPropertyByName(TEXT("LongitudeLatitudeHeight")))
	{
		FVector* InVec = InProp->ContainerPtrToValuePtr<FVector>(Params);
		*InVec = FVector(Lon, Lat, HeightM);
	}

	Geo->ProcessEvent(Func, Params);

	FVector Result = FVector::ZeroVector;
	if (FProperty* RetProp = Func->GetReturnProperty())
	{
		FVector* RetVec = RetProp->ContainerPtrToValuePtr<FVector>(Params);
		if (RetVec) Result = *RetVec;
	}
	return Result;
}

FVector CesiumWorld::UnrealToGeodetic(UWorld* World, const FVector& UnrealPos)
{
	UClass* GeoClass = FindObject<UClass>(nullptr, TEXT("/Script/CesiumRuntime.CesiumGeoreference"));
	if (!GeoClass || !World) return FVector::ZeroVector;
	TArray<AActor*> Geos;
	UGameplayStatics::GetAllActorsOfClass(World, GeoClass, Geos);
	if (Geos.Num() == 0) return FVector::ZeroVector;

	AActor* Geo = Geos[0];
	UFunction* Func = Geo->FindFunction(TEXT("TransformUnrealPositionToLongitudeLatitudeHeight"));
	if (!Func) return FVector::ZeroVector;

	uint8* Params = (uint8*)FMemory_Alloca(Func->ParmsSize);
	FMemory::Memzero(Params, Func->ParmsSize);
	if (FProperty* InProp = Func->FindPropertyByName(TEXT("UnrealPosition")))
		*InProp->ContainerPtrToValuePtr<FVector>(Params) = UnrealPos;
	Geo->ProcessEvent(Func, Params);

	FVector Result = FVector::ZeroVector;
	if (FProperty* RetProp = Func->GetReturnProperty())
		if (FVector* RetVec = RetProp->ContainerPtrToValuePtr<FVector>(Params)) Result = *RetVec;
	return Result; // (lon, lat, heightM)
}

bool CesiumWorld::GetGeorefOrigin(UWorld* World, FVector& OutLLH)
{
	UClass* GeoClass = FindObject<UClass>(nullptr, TEXT("/Script/CesiumRuntime.CesiumGeoreference"));
	if (!GeoClass || !World) return false;
	TArray<AActor*> Geos;
	UGameplayStatics::GetAllActorsOfClass(World, GeoClass, Geos);
	if (Geos.Num() == 0) return false;

	AActor* Geo = Geos[0];
	UFunction* Func = Geo->FindFunction(TEXT("GetOriginLongitudeLatitudeHeight"));
	if (!Func) return false;
	uint8* Params = (uint8*)FMemory_Alloca(Func->ParmsSize);
	FMemory::Memzero(Params, Func->ParmsSize);
	Geo->ProcessEvent(Func, Params);
	if (FProperty* Ret = Func->GetReturnProperty())
	{
		if (FVector* RetVec = Ret->ContainerPtrToValuePtr<FVector>(Params)) { OutLLH = *RetVec; return true; }
	}
	return false;
}

void CesiumWorld::SetGeoreferenceOrigin(UWorld* World, double Lon, double Lat, double HeightM)
{
	UClass* GeoClass = FindObject<UClass>(nullptr, TEXT("/Script/CesiumRuntime.CesiumGeoreference"));
	if (!GeoClass || !World) return;
	TArray<AActor*> Geos;
	UGameplayStatics::GetAllActorsOfClass(World, GeoClass, Geos);
	if (Geos.Num() == 0) return;

	AActor* Geo = Geos[0];

	// Already close enough? A geodetic distance < ~50km keeps cm precision —
	// skip so we don't disturb tilesets that loaded against the current origin.
	if (UFunction* GetFn = Geo->FindFunction(TEXT("GetOriginLongitudeLatitudeHeight")))
	{
		uint8* GParams = (uint8*)FMemory_Alloca(GetFn->ParmsSize);
		FMemory::Memzero(GParams, GetFn->ParmsSize);
		Geo->ProcessEvent(GetFn, GParams);
		if (FProperty* Ret = GetFn->GetReturnProperty())
		{
			FVector Cur = *Ret->ContainerPtrToValuePtr<FVector>(GParams); // (lon, lat, h)
			double dLat = (Lat - Cur.Y) * 111320.0;
			double dLon = (Lon - Cur.X) * 111320.0 * FMath::Cos(FMath::DegreesToRadians(Lat));
			double DistM = FMath::Sqrt(dLat * dLat + dLon * dLon);
			if (DistM < 50000.0)
			{
				UE_LOG(LogParamotor, Log, TEXT("CesiumWorld: georef origin already %.0fkm away — keeping it"), DistM / 1000.0);
				return;
			}
		}
	}

	UFunction* Func = Geo->FindFunction(TEXT("SetOriginLongitudeLatitudeHeight"));
	if (!Func)
	{
		// Fall back to poking the raw origin properties.
		Refl::SetNumeric(Geo, TEXT("OriginLatitude"), Lat);
		Refl::SetNumeric(Geo, TEXT("OriginLongitude"), Lon);
		Refl::SetNumeric(Geo, TEXT("OriginHeight"), HeightM);
		return;
	}

	uint8* Params = (uint8*)FMemory_Alloca(Func->ParmsSize);
	FMemory::Memzero(Params, Func->ParmsSize);
	// Param is named TargetLongitudeLatitudeHeight in the plugin.
	FProperty* InProp = Func->FindPropertyByName(TEXT("TargetLongitudeLatitudeHeight"));
	if (!InProp) InProp = Func->FindPropertyByName(TEXT("LongitudeLatitudeHeight"));
	if (!InProp)
	{
		UE_LOG(LogParamotor, Warning, TEXT("CesiumWorld: SetGeoreferenceOrigin — no FVector param on %s"), *Func->GetName());
		return;
	}
	*InProp->ContainerPtrToValuePtr<FVector>(Params) = FVector(Lon, Lat, HeightM);
	Geo->ProcessEvent(Func, Params);
	UE_LOG(LogParamotor, Log, TEXT("CesiumWorld: georeference origin set to %.4f, %.4f (%.0fm)"), Lat, Lon, HeightM);
}

double CesiumWorld::GeoidUndulationM(double Lat, double Lon)
{
	// EGM96 geoid undulation (N), sampled across CONUS — IDW-interpolated.
	// ±3 m worst case over sparse regions; fine for sanity checks and for
	// placing a sea-level water sheet. NOT for surveying.
	static const double Pts[][3] = {
		{41.5,-71.5,-31.03},{39.7,-105,-16.88},{36.2,-116.8,-30.05},
		{47.9,-124,-20.74},{25.8,-80,-28.56},{46.9,-96.8,-26.55},
		{32.7,-117.1,-34.87},{44.9,-93.2,-28.00},{40.7,-74,-32.78},
		{41.0,-100,-23.29},{35.0,-90,-28.35},{48.5,-71,-29.77},
		{45.0,-75,-32.08},{42.0,-87.6,-34.09},{38.0,-97.5,-29.26},
		{30.0,-98,-26.72},{28.0,-97,-28.14},{45.0,-110,-7.48},
		{40.0,-112,-17.72},{35.0,-119,-32.51},{45.0,-122,-20.39},
		{30.0,-84,-28.64},{34.0,-80,-33.55},
	};
	double Num = 0.0, Den = 0.0;
	for (auto& Q : Pts)
	{
		const double dLa = Lat - Q[0], dLo = Lon - Q[1];
		const double w = 1.0 / FMath::Max(1e-6, dLa * dLa + dLo * dLo);
		Num += w * Q[2]; Den += w;
	}
	return Num / Den;
}

void CesiumWorld::AnchorToGlobe(UWorld* World, AActor* Actor)
{
	if (!World || !Actor || !DetectGlobe(World)) return;

	UClass* AnchorClass = FindObject<UClass>(nullptr, TEXT("/Script/CesiumRuntime.CesiumGlobeAnchorComponent"));
	if (!AnchorClass) return;

	// Don't double-anchor.
	if (Actor->FindComponentByClass(AnchorClass)) return;

	UActorComponent* Anchor = NewObject<UActorComponent>(Actor, AnchorClass, TEXT("GlobeAnchor"));
	if (Anchor)
	{
		Actor->AddInstanceComponent(Anchor);
		Anchor->RegisterComponent();
		UE_LOG(LogParamotor, Log, TEXT("CesiumWorld: globe-anchored %s"), *Actor->GetName());
	}
}

// Spawns an ACesiumCartographicPolygon and sets its points via reflection.
// LLHPoints are (lon, lat, heightM) — the polygon rasterizes to geodetic
// coordinates at overlay creation, so rebase can't strand the clip zone.
AActor* CesiumWorld::SpawnClipPolygon(UWorld* World, const TArray<FVector>& LLHPoints)
{
	if (!World || LLHPoints.Num() < 3) return nullptr;
	UClass* PolyClass = FindObject<UClass>(nullptr, TEXT("/Script/CesiumRuntime.CesiumCartographicPolygon"));
	if (!PolyClass) return nullptr;

	AActor* Poly = World->SpawnActor<AActor>(PolyClass, FVector::ZeroVector, FRotator::ZeroRotator);
	if (!Poly) return nullptr;

	UFunction* Fn = Poly->FindFunction(TEXT("SetPolygonPoints"));
	if (!Fn) { Poly->Destroy(); return nullptr; }
	uint8* Params = (uint8*)FMemory_Alloca(Fn->ParmsSize);
	FMemory::Memzero(Params, Fn->ParmsSize);
	if (FProperty* CRS = Fn->FindPropertyByName(TEXT("CoordinateReferenceSystem")))
		*CRS->ContainerPtrToValuePtr<uint8>(Params) = 0; // LongitudeLatitudeHeight
	if (FProperty* Pts = Fn->FindPropertyByName(TEXT("Points")))
		*Pts->ContainerPtrToValuePtr<TArray<FVector>>(Params) = LLHPoints;
	Poly->ProcessEvent(Fn, Params);
	return Poly;
}

// Attaches a UCesiumPolygonRasterOverlay to the PRT tileset (ion 2275207) so
// photogrammetry inside the polygons is clipped/excluded — kills the melted
// bridge shells PRT leaves where our procedural landmarks stand. Tiles fully
// inside a polygon stop loading; partial tiles get the Clipping material mask.
UActorComponent* CesiumWorld::AddPolygonClipper(UWorld* World, const TArray<AActor*>& Polygons)
{
	if (!World || Polygons.Num() == 0) return nullptr;
	UClass* TsClass  = FindObject<UClass>(nullptr, TEXT("/Script/CesiumRuntime.Cesium3DTileset"));
	UClass* OvClass  = FindObject<UClass>(nullptr, TEXT("/Script/CesiumRuntime.CesiumPolygonRasterOverlay"));
	if (!TsClass || !OvClass) return nullptr;

	TArray<AActor*> Tilesets;
	UGameplayStatics::GetAllActorsOfClass(World, TsClass, Tilesets);
	UActorComponent* Created = nullptr;
	for (AActor* Ts : Tilesets)
	{
		// Only the photogrammetry tileset — clipping the terrain tileset would
		// punch holes in the ground under approach spans.
		int64 IonId = 0;
		if (FProperty* P = Ts->GetClass()->FindPropertyByName(TEXT("IonAssetID")))
			IonId = P->ContainerPtrToValuePtr<int64>(Ts) ? *P->ContainerPtrToValuePtr<int64>(Ts) : 0;
		if (IonId != 2275207) continue;

		UActorComponent* Ov = NewObject<UActorComponent>(Ts, OvClass, TEXT("BridgeClipOverlay"));
		if (!Ov) continue;
		Ts->AddInstanceComponent(Ov);

		// Polygons: TArray<TSoftObjectPtr<ACesiumCartographicPolygon>>
		if (FArrayProperty* Arr = CastField<FArrayProperty>(Ov->GetClass()->FindPropertyByName(TEXT("Polygons"))))
		{
			FScriptArrayHelper In(Arr, Arr->ContainerPtrToValuePtr<void>(Ov));
			for (AActor* Poly : Polygons)
				if (Poly)
					*(FSoftObjectPtr*)In.GetRawPtr(In.AddValue()) = FSoftObjectPtr(Poly);
		}
		Refl::SetBool(Ov, TEXT("InvertSelection"), false);
		Refl::SetBool(Ov, TEXT("ExcludeSelectedTiles"), true);
		Ov->RegisterComponent();
		if (UFunction* Add = Ov->FindFunction(TEXT("AddToTileset")))
			Ov->ProcessEvent(Add, nullptr);
		Created = Ov;
		UE_LOG(LogParamotor, Log, TEXT("CesiumWorld: polygon clipper on %s with %d zones"), *Ts->GetName(), Polygons.Num());
	}
	return Created;
}
