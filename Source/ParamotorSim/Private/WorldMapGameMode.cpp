// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldMapGameMode.h"
#include "ReflectionProps.h"
#include "CesiumWorldHelpers.h"
#include "ParamotorLogging.h"
#include "WorldMapPawn.h"
#include "AirportMarker.h"
#include "UW_FreeFlightMenu.h"
#include "Engine/LocalPlayer.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Engine/StaticMeshActor.h"
#include "GameFramework/PlayerController.h"
#include "ParamotorGameInstance.h"
#include "Engine/DirectionalLight.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/LightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Engine/SkyLight.h"
#include "Engine/PostProcessVolume.h"
#include "Camera/PlayerCameraManager.h"
#include "HAL/IConsoleManager.h"

AWorldMapGameMode::AWorldMapGameMode(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = true;
	DefaultPawnClass = AWorldMapPawn::StaticClass();
}

void AWorldMapGameMode::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Keep the sun aimed along the view direction so the hemisphere facing
	// the camera is always lit — no day/night terminator on the world map.
	// A small offset keeps subtle shading instead of flat head-on lighting.
	if (SunActor)
	{
		if (APlayerCameraManager* CamMgr = UGameplayStatics::GetPlayerCameraManager(GetWorld(), 0))
		{
			FRotator SunRot = CamMgr->GetCameraRotation();
			SunRot.Pitch -= 14.0f;
			SunRot.Yaw += 22.0f;
			SunRot.Roll = 0.0f;
			SunActor->SetActorRotation(SunRot);
		}
	}

	// Spawn airport markers once the georeference has had a few frames to
	// initialize its coordinate transform.
	if (!bMarkersSpawned)
	{
		MarkerSpawnDelay -= DeltaTime;
		if (MarkerSpawnDelay <= 0.0f)
		{
			bMarkersSpawned = true;
			SpawnAirportMarkers();
		}
	}
}

void AWorldMapGameMode::BeginPlay()
{
	Super::BeginPlay();

	// Build the airport list
	Airports.Empty();
	{
		FAirportInfo A;
		A.Name = TEXT("Westerly State Airport"); A.ICAO = TEXT("WST");
		A.City = TEXT("Westerly"); A.State = TEXT("RI");
		A.Latitude = 41.35; A.Longitude = -71.80; A.ElevationM = 24.0f;
		A.Description = TEXT("Primary launch site. Ninigret Park coastal field beside the old auxiliary airstrip.");
		Airports.Add(A);
	}
	{
		FAirportInfo A;
		A.Name = TEXT("Ninigret Airstrip"); A.ICAO = TEXT("NINI");
		A.City = TEXT("Charlestown"); A.State = TEXT("RI");
		A.Latitude = 41.3650; A.Longitude = -71.6600; A.ElevationM = 6.0f;
		A.Description = TEXT("Ninigret Park auxiliary strip. Open mowed field — the home field.");
		Airports.Add(A);
	}
	{
		FAirportInfo A;
		A.Name = TEXT("Misquamicut Beach"); A.ICAO = TEXT("MISQ");
		A.City = TEXT("Westerly"); A.State = TEXT("RI");
		A.Latitude = 41.3255; A.Longitude = -71.7970; A.ElevationM = 2.0f;
		A.Description = TEXT("Open sand on Block Island Sound — classic beach launch.");
		Airports.Add(A);
	}
	{
		FAirportInfo A;
		A.Name = TEXT("Block Island State Airport"); A.ICAO = TEXT("BID");
		A.City = TEXT("Block Island"); A.State = TEXT("RI");
		A.Latitude = 41.17; A.Longitude = -71.58; A.ElevationM = 33.0f;
		A.Description = TEXT("Island airport on the glacial plateau. Coastal cliffs on all sides.");
		Airports.Add(A);
	}
	{
		FAirportInfo A;
		A.Name = TEXT("Groton-New London Airport"); A.ICAO = TEXT("GON");
		A.City = TEXT("Groton"); A.State = TEXT("CT");
		A.Latitude = 41.33; A.Longitude = -72.05; A.ElevationM = 3.0f;
		A.Description = TEXT("Coastal airport across the state line in Connecticut.");
		Airports.Add(A);
	}
	{
		FAirportInfo A;
		A.Name = TEXT("T.F. Green Airport"); A.ICAO = TEXT("PVD");
		A.City = TEXT("Providence"); A.State = TEXT("RI");
		A.Latitude = 41.72; A.Longitude = -71.43; A.ElevationM = 17.0f;
		A.Description = TEXT("Rhode Island's main commercial airport. Class C airspace.");
		Airports.Add(A);
	}
	{
		FAirportInfo A;
		A.Name = TEXT("Quonset State Airport"); A.ICAO = TEXT("OQU");
		A.City = TEXT("North Kingstown"); A.State = TEXT("RI");
		A.Latitude = 41.60; A.Longitude = -71.41; A.ElevationM = 6.0f;
		A.Description = TEXT("Former NAS Quonset Point. Large runways, naval aviation museum.");
		Airports.Add(A);
	}
	{
		FAirportInfo A;
		A.Name = TEXT("New Bedford Regional"); A.ICAO = TEXT("EWB");
		A.City = TEXT("New Bedford"); A.State = TEXT("MA");
		A.Latitude = 41.68; A.Longitude = -70.96; A.ElevationM = 24.0f;
		A.Description = TEXT("Regional airport east of the Taunton River.");
		Airports.Add(A);
	}
	{
		FAirportInfo A;
		A.Name = TEXT("Montauk Airport"); A.ICAO = TEXT("MTP");
		A.City = TEXT("Montauk"); A.State = TEXT("NY");
		A.Latitude = 41.08; A.Longitude = -71.92; A.ElevationM = 2.0f;
		A.Description = TEXT("Eastern tip of Long Island. Dramatic coastal scenery.");
		Airports.Add(A);
	}
	{
		FAirportInfo A;
		A.Name = TEXT("Fishers Island Airport"); A.ICAO = TEXT("FID");
		A.City = TEXT("Fishers Island"); A.State = TEXT("NY");
		A.Latitude = 41.25; A.Longitude = -72.03; A.ElevationM = 3.0f;
		A.Description = TEXT("Small island airport between Long Island and Connecticut.");
		Airports.Add(A);
	}

	// Availability comes from the curated launch-site table — an airport is
	// flyable iff a flat-map field exists for its ICAO. Adding a site to
	// LaunchSites lights up its pin automatically.
	if (UParamotorGameInstance* GI = Cast<UParamotorGameInstance>(GetGameInstance()))
	{
		for (FAirportInfo& A : Airports)
		{
			A.bAvailable = GI->FindSiteByICAO(A.ICAO) != nullptr;
		}
	}

	APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0);
	if (!PC)
	{
		UE_LOG(LogParamotor, Log, TEXT("WorldMapGameMode: no player controller"));
		return;
	}

	if (!PC->GetPawn())
	{
		RestartPlayer(PC);
	}

	PC->bShowMouseCursor = true;
	FInputModeGameAndUI InputMode;
	InputMode.SetHideCursorDuringCapture(false);
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	PC->SetInputMode(InputMode);

	SetupGlobe();
	// Airport markers are spawned from Tick after a short delay —
	// the georeference needs a few frames to initialize its transform.
	ShowMenu(PC);
}

void AWorldMapGameMode::SetupGlobe()
{
	UWorld* World = GetWorld();
	if (!World) return;

	// ---- World settings — the camera pawn sits at Earth center (~-6371 km)
	// and airport markers live at planetary distances. KillZ / world bounds
	// checks would destroy them, so disable bounds checks for this map. ----
	if (AWorldSettings* WS = World->GetWorldSettings())
	{
		FProperty* BoundsProp = AWorldSettings::StaticClass()->FindPropertyByName(FName(TEXT("bEnableWorldBoundsChecks")));
		if (FBoolProperty* BP = CastField<FBoolProperty>(BoundsProp))
		{
			BP->SetPropertyValue(BoundsProp->ContainerPtrToValuePtr<void>(WS), false);
		}
		WS->KillZ = -1e10f;
	}

	// ---- Hide every legacy static mesh in the level (old sky sphere, floor
	// plane, fake globe). The Cesium tileset is ACesium3DTileset, not a
	// static mesh actor, so it is unaffected. ----
	for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
	{
		It->SetActorHiddenInGame(true);
		It->SetActorEnableCollision(false);
	}

	// ---- Cesium Georeference ----
	// Centered on Westerly, RI (41.35N, 71.80W)
	UClass* GeoClass = FindObject<UClass>(nullptr, TEXT("/Script/CesiumRuntime.CesiumGeoreference"));
	if (GeoClass)
	{
		GeoreferenceActor = World->SpawnActor<AActor>(GeoClass, FVector::ZeroVector, FRotator::ZeroRotator);
		if (GeoreferenceActor)
		{
			// Set the origin through the BlueprintCallable setter so the
			// internal coordinate system is recomputed. Reflection writes to
			// OriginLatitude/Longitude would leave stale transforms.
			if (UFunction* SetOrigin = GeoreferenceActor->FindFunction(TEXT("SetOriginLongitudeLatitudeHeight")))
			{
				uint8* Params = (uint8*)FMemory_Alloca(SetOrigin->ParmsSize);
				FMemory::Memzero(Params, SetOrigin->ParmsSize);
				// X = longitude (deg), Y = latitude (deg), Z = height (m)
				for (TFieldIterator<FProperty> It(SetOrigin); It; ++It)
				{
					if (It->GetFName() == TEXT("TargetLongitudeLatitudeHeight") || It->IsA<FStructProperty>())
					{
						*It->ContainerPtrToValuePtr<FVector>(Params) = FVector(-71.80, 41.35, 0.0);
						break;
					}
				}
				GeoreferenceActor->ProcessEvent(SetOrigin, Params);
				UE_LOG(LogParamotor, Log, TEXT("WorldMapGameMode: Cesium georeference spawned at Westerly, RI"));
			}
			else
			{
				UE_LOG(LogParamotor, Log, TEXT("WorldMapGameMode: SetOriginLongitudeLatitudeHeight not found — origin left at default"));
			}
		}
	}
	else
	{
		UE_LOG(LogParamotor, Log, TEXT("WorldMapGameMode: CesiumGeoreference class not found — Cesium plugin may not be loaded"));
	}

	// ---- Cesium 3D Tileset — Cesium World Terrain (geometry) + Bing Aerial (imagery overlay) ----
	// Cesium World Terrain (IonAssetID=1) provides the globe geometry.
	// Bing Maps Aerial (IonAssetID=3) is a raster overlay draped over the terrain.
	UClass* TilesetClass = FindObject<UClass>(nullptr, TEXT("/Script/CesiumRuntime.Cesium3DTileset"));
	if (TilesetClass)
	{
		// Spawn deferred so properties are set before the tileset tries to load tiles.
		AActor* Tileset = World->SpawnActorDeferred<AActor>(TilesetClass,
			FTransform::Identity, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Tileset)
		{
			// Cesium World Terrain for the globe geometry
			Refl::SetNumberOrBool(Tileset, TEXT("IonAssetID"), 1);
			// High detail — lower SSE = more tiles streamed = sharper imagery
			Refl::SetNumberOrBool(Tileset, TEXT("MaximumScreenSpaceError"), 2.0);
			Refl::SetNumberOrBool(Tileset, TEXT("MaximumCachedBytes"), 2147483648.0); // 2GB cache
			Refl::SetNumberOrBool(Tileset, TEXT("MaximumSimultaneousTileLoads"), 20);
			// Keep parent tiles loaded while children stream in — avoids
			// blurry gaps while higher-detail imagery loads during zoom.
			Refl::SetNumberOrBool(Tileset, TEXT("PreloadAncestors"), 1.0);
			Refl::SetNumberOrBool(Tileset, TEXT("PreloadSiblings"), 1.0);

			// Add CesiumIonRasterOverlay for Bing Maps Aerial satellite imagery
			UClass* OverlayClass = FindObject<UClass>(nullptr, TEXT("/Script/CesiumRuntime.CesiumIonRasterOverlay"));
			if (OverlayClass)
			{
				UActorComponent* Overlay = NewObject<UActorComponent>(Tileset, OverlayClass, TEXT("BingAerial"));
				if (Overlay)
				{
					// Set the overlay's IonAssetID to 3 (Bing Aerial with Labels)
					FProperty* AssetProp = OverlayClass->FindPropertyByName(FName(TEXT("IonAssetID")));
					if (FNumericProperty* NumProp = CastField<FNumericProperty>(AssetProp))
					{
						NumProp->SetIntPropertyValue(AssetProp->ContainerPtrToValuePtr<void>(Overlay), static_cast<int64>(3));
					}
					Overlay->RegisterComponent();
				}
			}

			UGameplayStatics::FinishSpawningActor(Tileset, FTransform::Identity);
			UE_LOG(LogParamotor, Log, TEXT("WorldMapGameMode: Cesium tileset spawned (World Terrain + Bing Aerial)"));
		}
	}

	// ---- Directional light (sun) — tracks the camera in Tick so the
	// hemisphere facing the camera is always lit (no day/night terminator). ----
	{
		for (TActorIterator<ADirectionalLight> It(World); It; ++It)
		{
			if (It->GetLightComponent())
			{
				SunActor = *It;
				break;
			}
		}
		if (!SunActor)
		{
			SunActor = World->SpawnActor<ADirectionalLight>();
		}
		if (SunActor)
		{
			if (USceneComponent* RootComp = SunActor->GetRootComponent())
			{
				RootComp->SetMobility(EComponentMobility::Movable);
			}
			if (ULightComponent* LC = SunActor->GetLightComponent())
			{
				// Level-placed lights default to stationary — this one must
				// rotate to follow the camera every frame.
				LC->SetMobility(EComponentMobility::Movable);
				LC->SetIntensity(60000.0f);
				LC->SetLightColor(FLinearColor(1.0f, 0.98f, 0.94f));
				if (UDirectionalLightComponent* DirLC = Cast<UDirectionalLightComponent>(LC))
				{
					DirLC->bAtmosphereSunLight = true;
					DirLC->SetCastShadows(false);
				}
			}
		}
	}

	// ---- Sky light — gentle ambient fill so nothing crushes to black ----
	{
		UClass* SkyLightClass = FindObject<UClass>(nullptr, TEXT("/Script/Engine.SkyLight"));
		if (!SkyLightClass) SkyLightClass = ASkyLight::StaticClass();
		if (SkyLightClass)
		{
			AActor* SkyLightActor = World->SpawnActor<AActor>(SkyLightClass, FVector::ZeroVector, FRotator::ZeroRotator);
			if (SkyLightActor)
			{
				FProperty* IntensityProp = SkyLightClass->FindPropertyByName(FName(TEXT("Intensity")));
				if (FFloatProperty* FP = CastField<FFloatProperty>(IntensityProp))
				{
					FP->SetPropertyValue(IntensityProp->ContainerPtrToValuePtr<void>(SkyLightActor), 1.5f);
				}
			}
		}
	}

	// ---- Sky atmosphere for the Earth's blue limb ----
	{
		UClass* SkyClass = FindObject<UClass>(nullptr, TEXT("/Script/Engine.SkyAtmosphere"));
		if (!SkyClass) SkyClass = ASkyAtmosphere::StaticClass();
		if (SkyClass)
		{
			AActor* Sky = World->SpawnActor<AActor>(SkyClass, FVector::ZeroVector, FRotator::ZeroRotator);
			if (Sky)
			{
				// Match the atmosphere shell to the real Earth radius
				if (USkyAtmosphereComponent* SkyComp = Sky->FindComponentByClass<USkyAtmosphereComponent>())
				{
					SkyComp->BottomRadius = 6371.0f;
				}
				UE_LOG(LogParamotor, Log, TEXT("WorldMapGameMode: sky atmosphere spawned"));
			}
		}
	}

	// ---- Post process — lock exposure so the view never blows out or crushes.
	// Min == Max disables eye adaptation entirely. Values are EV100 (project
	// has ExtendDefaultLuminanceRange enabled). EV ~14 suits a sun-lit globe. ----
	{
		APostProcessVolume* PPV = World->SpawnActor<APostProcessVolume>();
		if (PPV)
		{
			PPV->bUnbound = true;
			PPV->Settings.bOverride_AutoExposureMinBrightness = true;
			PPV->Settings.bOverride_AutoExposureMaxBrightness = true;
			PPV->Settings.AutoExposureMinBrightness = 14.0f;
			PPV->Settings.AutoExposureMaxBrightness = 14.0f;
			PPV->Settings.bOverride_BloomIntensity = true;
			PPV->Settings.BloomIntensity = 0.0f;
			PPV->Settings.bOverride_VignetteIntensity = true;
			PPV->Settings.VignetteIntensity = 0.0f;
			// Aerial imagery reads soft at globe distances — a light sharpen
			// pass crisps coastlines and labels without visible halos.
			PPV->Settings.bOverride_Sharpen = true;
			PPV->Settings.Sharpen = 0.7f;

			// Modest supersample — the globe streams heavy tiles, so keep it
			// lighter than the hangar but above native res.
			if (IConsoleVariable* SP = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage")))
			{
				SP->Set(120.0f);
			}
			UE_LOG(LogParamotor, Log, TEXT("WorldMapGameMode: exposure locked at EV14"));
		}
	}
}

void AWorldMapGameMode::SpawnAirportMarkers()
{
	UWorld* World = GetWorld();
	if (!World || !GeoreferenceActor)
	{
		UE_LOG(LogParamotor, Log, TEXT("WorldMapGameMode: SpawnAirportMarkers aborted — World=%p Georeference=%p"), World, GeoreferenceActor);
		return;
	}

	UE_LOG(LogParamotor, Log, TEXT("WorldMapGameMode: spawning %d airport markers"), Airports.Num());

	AirportMarkers.SetNum(Airports.Num());

	// The georeference may need a frame to initialize before
	// TransformLongitudeLatitudeHeightToUnreal returns valid results.
	// Retry positions that come back as zero.
	for (int32 i = 0; i < Airports.Num(); i++)
	{
		const FAirportInfo& Info = Airports[i];
		// +500m above airport elevation so the pin floats above the terrain surface
		FVector WorldPos = CesiumWorld::GeodeticToUnreal(World, Info.Longitude, Info.Latitude, Info.ElevationM + 500.0);

		// If the transform isn't ready yet, the result will be zero.
		// For airports far from the origin this is obvious; for Westerly
		// (near the origin) a small non-zero result is still valid.
		// We accept the position even if small — the georeference handles it.
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AAirportMarker* Marker = World->SpawnActor<AAirportMarker>(WorldPos, FRotator::ZeroRotator, Params);
		if (Marker)
		{
			Marker->InitializeAirport(Info, i);
			AirportMarkers[i] = Marker;
			UE_LOG(LogParamotor, Log, TEXT("WorldMapGameMode: airport marker %s at %s"), *Info.ICAO, *WorldPos.ToString());
		}
	}
}

void AWorldMapGameMode::SelectAirport(int32 AirportIndex)
{
	if (!Airports.IsValidIndex(AirportIndex)) return;

	SelectedAirportIndex = AirportIndex;
	const FAirportInfo& Info = Airports[AirportIndex];

	UE_LOG(LogParamotor, Log, TEXT("WorldMapGameMode: selected airport %s (%s)"), *Info.Name, *Info.ICAO);

	// Store in GameInstance for the flight map
	if (UParamotorGameInstance* GI = Cast<UParamotorGameInstance>(GetGameInstance()))
	{
		GI->SelectedLaunchSite = AirportIndex;
		GI->SelectedAirportLat = Info.Latitude;
		GI->SelectedAirportLon = Info.Longitude;
		GI->SelectedAirportElev = Info.ElevationM;
		GI->SelectedAirportName = Info.Name;
		GI->SelectedAirportICAO = Info.ICAO;
		GI->bAirportSelected = true;
	}

	// Update the menu UI
	if (IsValid(MenuRef))
	{
		MenuRef->UpdateAirportDisplay(Info);
	}
}

void AWorldMapGameMode::ShowMenu(APlayerController* PC)
{
	TSubclassOf<UUW_FreeFlightMenu> MenuClass = FreeFlightMenuClass;
	if (!MenuClass)
	{
		MenuClass = UUW_FreeFlightMenu::StaticClass();
	}

	UWorld* World = GetWorld();
	if (!World || !PC)
	{
		return;
	}

	UUW_FreeFlightMenu* Menu = NewObject<UUW_FreeFlightMenu>(PC, MenuClass);
	if (!Menu)
	{
		UE_LOG(LogParamotor, Log, TEXT("WorldMapGameMode: failed to create menu"));
		return;
	}

	Menu->SetOwningPlayer(PC);
	Menu->SetPlayerContext(FLocalPlayerContext(PC));
	Menu->bWorldMapBackground = true;
	Menu->Initialize();
	Menu->AddToViewport(100);
	Menu->SetVisibility(ESlateVisibility::Visible);
	Menu->SetIsFocusable(true);
	MenuRef = Menu;

	FInputModeGameAndUI InputMode;
	InputMode.SetHideCursorDuringCapture(false);
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	InputMode.SetWidgetToFocus(Menu->TakeWidget());
	PC->SetInputMode(InputMode);
}
