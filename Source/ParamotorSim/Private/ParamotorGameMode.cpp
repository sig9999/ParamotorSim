// Copyright Epic Games, Inc. All Rights Reserved.

#include "ParamotorGameMode.h"
#include "Components/BoxComponent.h"
#include "ParamotorPlayerController.h"
#include "ParamotorCharacter.h"
#include "ParamotorGameInstance.h"
#include "Components/AudioComponent.h"
#include "Components/StaticMeshComponent.h"
#include "ProceduralMeshComponent.h"
#include "Sound/SoundBase.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"
#include "Landscape.h"
#include "GameFramework/PlayerStart.h"
#include "Engine/World.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "WaterZoneActor.h"
#include "WaterBodyOceanActor.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/PostProcessVolume.h"

AParamotorGameMode::AParamotorGameMode()
{
	PlayerControllerClass = AParamotorPlayerController::StaticClass();
	DefaultPawnClass = AParamotorCharacter::StaticClass();
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
}

void AParamotorGameMode::BeginPlay()
{
	Super::BeginPlay();

	SetupSkyAndLighting();
	SetupCoastalTerrain();
	SetupPostProcessing();
	SetupWater();
	SetupCoastlineDirection();

	// Reposition PlayerStart to the launch field — also handled in RestartPlayer
	{
		TArray<AActor*> PlayerStarts;
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), APlayerStart::StaticClass(), PlayerStarts);
		FVector FieldLoc = GetLaunchFieldSpawnLocation();
		for (AActor* PS : PlayerStarts)
		{
			PS->SetActorLocation(FieldLoc);
			UE_LOG(LogTemp, Warning, TEXT("GameMode: PlayerStart pre-positioned at Westerly State Airport at %s"), *FieldLoc.ToString());
		}
		if (PlayerStarts.Num() == 0)
			UE_LOG(LogTemp, Warning, TEXT("GameMode: No PlayerStart found!"));
	}

	// Spawn under-map plane to hide default concrete from below
	UStaticMesh* PlaneMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (PlaneMesh)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		FVector UnderLoc(0.0f, 0.0f, -2000.0f);
		FRotator UnderRot(180.0f, 0.0f, 0.0f);
		AStaticMeshActor* UnderPlane = GetWorld()->SpawnActor<AStaticMeshActor>(UnderLoc, UnderRot, SpawnParams);
		if (UnderPlane)
		{
			UnderPlane->GetStaticMeshComponent()->SetStaticMesh(PlaneMesh);
			UnderPlane->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
			UnderPlane->SetActorScale3D(FVector(2000.0f, 2000.0f, 1.0f));
			UnderPlane->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			UMaterialInterface* DarkMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/WorldGridMaterial.WorldGridMaterial"));
			if (DarkMat) UnderPlane->GetStaticMeshComponent()->SetMaterial(0, DarkMat);
			UE_LOG(LogTemp, Warning, TEXT("GameMode: Spawned under-map plane at Z=-2000"));
		}
	}

	// Ambient birds
	USoundBase* BirdsSound = LoadObject<USoundBase>(nullptr, TEXT("/Game/Sounds/birds_ambient.birds_ambient"));
	if (BirdsSound)
	{
		BirdsAudio = UGameplayStatics::SpawnSound2D(GetWorld(), BirdsSound);
		if (BirdsAudio) { BirdsAudio->bIsUISound = false; BirdsAudio->SetVolumeMultiplier(0.08f); }
	}

	// Ambient ocean
	USoundBase* OceanSound = LoadObject<USoundBase>(nullptr, TEXT("/Game/Sounds/ocean_waves.ocean_waves"));
	if (OceanSound)
	{
		OceanAudio = UGameplayStatics::SpawnSound2D(GetWorld(), OceanSound);
		if (OceanAudio) { OceanAudio->bIsUISound = false; OceanAudio->SetVolumeMultiplier(0.1f); }
	}

	// Set initial sun position
	float TimeH = 12.0f;
	if (UParamotorGameInstance* GI = GetWorld()->GetGameInstance<UParamotorGameInstance>())
		TimeH = GI->TimeOfDay;
	UpdateSunPosition(TimeH);

	UE_LOG(LogTemp, Warning, TEXT("GameMode: Environment setup complete"));
}

void AParamotorGameMode::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (UParamotorGameInstance* GI = GetWorld()->GetGameInstance<UParamotorGameInstance>())
		UpdateSunPosition(GI->TimeOfDay);
	// temp debug: log pawn position a few seconds in
	static float T = 0.f; T += DeltaTime;
	if (T > 4.f && T < 5.f)
	{
		if (APawn* P = GetWorld()->GetFirstPlayerController() ? GetWorld()->GetFirstPlayerController()->GetPawn() : nullptr)
			UE_LOG(LogTemp, Warning, TEXT("GameMode: pawn at %s"), *P->GetActorLocation().ToString());
	}
}

void AParamotorGameMode::SetupSkyAndLighting()
{
	UE_LOG(LogTemp, Warning, TEXT("GameMode: Setting up sky and lighting..."));

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// === 1. SKY ATMOSPHERE ===
	SkyAtmosphereActor = GetWorld()->SpawnActor<ASkyAtmosphere>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
	if (SkyAtmosphereActor)
	{
		USkyAtmosphereComponent* SkyComp = SkyAtmosphereActor->GetComponent();
		if (SkyComp)
		{
			// Earth-like atmosphere (units in km)
			SkyComp->BottomRadius = 6361.0f;           // Earth radius in km
			SkyComp->AtmosphereHeight = 100.0f;         // Atmosphere depth in km
			SkyComp->RayleighScatteringScale = 0.0331f;
			SkyComp->MieScatteringScale = 0.00399f;
			SkyComp->MieAbsorptionScale = 0.000443f;
			SkyComp->RayleighExponentialDistribution = 8.0f;
			SkyComp->MieExponentialDistribution = 1.2f;
			SkyComp->MultiScatteringFactor = 1.0f;
			SkyComp->GroundAlbedo = FColor(128, 128, 128, 255);
		}
		UE_LOG(LogTemp, Warning, TEXT("GameMode: Sky Atmosphere spawned"));
	}

	// === 2. DIRECTIONAL LIGHT (SUN) ===
	SunLight = GetWorld()->SpawnActor<ADirectionalLight>(FVector(0, 0, 5000), FRotator(-45.0f, 180.0f, 0), SpawnParams);
	if (SunLight)
	{
		UDirectionalLightComponent* SunComp = Cast<UDirectionalLightComponent>(SunLight->GetLightComponent());
		SunComp->SetMobility(EComponentMobility::Movable);
		SunComp->Intensity = 75000.0f;
		SunComp->LightColor = FColor(255, 240, 220);
		SunComp->CastShadows = true;
		SunComp->DynamicShadowCascades = 4;
		SunComp->CascadeDistributionExponent = 2.0f;
		if (SkyAtmosphereActor)
		{
			SunComp->bAtmosphereSunLight = true;
			SunComp->AtmosphereSunLightIndex = 0;
		}
		UE_LOG(LogTemp, Warning, TEXT("GameMode: Sun directional light spawned"));
	}

	// === 3. SKY LIGHT ===
	SkyLightActor = GetWorld()->SpawnActor<ASkyLight>(FVector(0, 0, 1000), FRotator::ZeroRotator, SpawnParams);
	if (SkyLightActor)
	{
		USkyLightComponent* SkyComp = Cast<USkyLightComponent>(SkyLightActor->GetLightComponent());
		if (SkyComp)
		{
			SkyComp->SetMobility(EComponentMobility::Movable);
			SkyComp->Intensity = 1.0f;
			SkyComp->CastShadows = false;
			SkyComp->LowerHemisphereColor = FLinearColor(0.1f, 0.1f, 0.15f, 1.0f);
		}
		UE_LOG(LogTemp, Warning, TEXT("GameMode: Sky light spawned"));
	}

	// === 4. EXPONENTIAL HEIGHT FOG ===
	FogActor = GetWorld()->SpawnActor<AExponentialHeightFog>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
	if (FogActor)
	{
		UExponentialHeightFogComponent* FogComp = FogActor->GetComponentByClass<UExponentialHeightFogComponent>();
		if (FogComp)
		{
			FogComp->SetMobility(EComponentMobility::Movable);
			FogComp->FogDensity = 0.005f;
			FogComp->FogHeightFalloff = 0.002f;
			FogComp->FogMaxOpacity = 0.9f;
			FogComp->StartDistance = 5000.0f;
			FogComp->FogCutoffDistance = 2000000.0f;
			FogComp->FogInscatteringLuminance = FLinearColor(0.6f, 0.7f, 0.85f, 1.0f);
			FogComp->DirectionalInscatteringExponent = 4.0f;
			FogComp->DirectionalInscatteringStartDistance = 10000.0f;
			FogComp->DirectionalInscatteringLuminance = FLinearColor(0.8f, 0.7f, 0.5f, 1.0f);
			FogComp->bEnableVolumetricFog = true;
			FogComp->VolumetricFogScatteringDistribution = 0.8f;
			FogComp->VolumetricFogAlbedo = FColor(230, 230, 230, 255);
			FogComp->VolumetricFogEmissive = FLinearColor::Black;
			FogComp->VolumetricFogExtinctionScale = 1.0f;
			UE_LOG(LogTemp, Warning, TEXT("GameMode: Exponential height fog spawned (fall atmosphere)"));
		}
	}
}

void AParamotorGameMode::UpdateSunPosition(float TimeH)
{
	if (!SunLight) return;

	const float MaxElevation = 45.0f;  // Fall equinox at 41.3N
	const float SunriseH = 6.0f;
	const float SunsetH = 18.0f;

	float SunPitch = 0.0f;
	float SunYaw = 0.0f;

	if (TimeH >= SunriseH && TimeH <= SunsetH)
	{
		float DayProgress = (TimeH - SunriseH) / (SunsetH - SunriseH);
		float SunAngle = DayProgress * 180.0f;
		float ElevationDeg = FMath::Sin(FMath::DegreesToRadians(SunAngle)) * MaxElevation;
		SunPitch = -ElevationDeg;
		SunYaw = 90.0f + SunAngle;
	}
	else
	{
		SunPitch = -10.0f;  // Below horizon
		float NightProgress = (TimeH < SunriseH) ? (TimeH + 6.0f) / 12.0f : (TimeH - SunsetH) / 12.0f;
		SunYaw = 270.0f + NightProgress * 180.0f;
	}

	SunLight->SetActorRotation(FRotator(SunPitch, SunYaw, 0.0f));

	// Adjust intensity and color based on elevation
	float ElevationDeg = -SunPitch;
	float IntensityFactor = FMath::Clamp(FMath::Sin(FMath::DegreesToRadians(ElevationDeg)) / FMath::Sin(FMath::DegreesToRadians(MaxElevation)), 0.0f, 1.0f);
	float Warmth = 1.0f - FMath::Clamp(ElevationDeg / 20.0f, 0.0f, 1.0f);

	UDirectionalLightComponent* SunComp = Cast<UDirectionalLightComponent>(SunLight->GetLightComponent());
	SunComp->Intensity = FMath::Lerp(2000.0f, 75000.0f, IntensityFactor);
	SunComp->LightColor = FColor(
		255,
		FMath::Lerp(240, 180, Warmth * 0.5f),
		FMath::Lerp(220, 100, Warmth * 0.7f)
	);

	if (SkyLightActor)
		SkyLightActor->GetLightComponent()->Intensity = FMath::Lerp(0.1f, 1.0f, IntensityFactor);

	if (FogActor)
	{
		UExponentialHeightFogComponent* FogComp = FogActor->GetComponentByClass<UExponentialHeightFogComponent>();
		if (FogComp)
		{
			FLinearColor DayFog(0.6f, 0.7f, 0.85f, 1.0f);
			FLinearColor SunsetFog(0.8f, 0.5f, 0.3f, 1.0f);
			FLinearColor NightFog(0.05f, 0.05f, 0.1f, 1.0f);
			FLinearColor TargetFog = (IntensityFactor > 0.1f)
				? FLinearColor::LerpUsingHSV(SunsetFog, DayFog, FMath::Clamp(ElevationDeg / 20.0f, 0.0f, 1.0f))
				: NightFog;
			FogComp->FogInscatteringLuminance = TargetFog;
		}
	}
}

void AParamotorGameMode::SetupPostProcessing()
{
	UE_LOG(LogTemp, Warning, TEXT("GameMode: Setting up post-processing..."));

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	PostProcessVol = GetWorld()->SpawnActor<APostProcessVolume>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
	if (PostProcessVol)
	{
		PostProcessVol->bUnbound = true;
		PostProcessVol->bEnabled = true;

		FPostProcessSettings& S = PostProcessVol->Settings;

		// Color grading — fall warm tones
		S.ColorSaturation = FVector(1.0f, 1.0f, 1.05f);
		S.ColorContrast = FVector(1.05f);
		S.ColorGamma = FVector(1.0f);
		S.ColorGain = FVector(1.02f, 1.0f, 0.98f);
		S.ColorOffset = FVector(0.0f, 0.005f, 0.01f);
		S.WhiteTemp = 5600.0f;
		S.WhiteTint = 0.0f;

		// Auto exposure
		S.AutoExposureMinBrightness = -2.0f;
		S.AutoExposureMaxBrightness = 4.0f;
		S.AutoExposureSpeedUp = 3.0f;
		S.AutoExposureSpeedDown = 1.0f;
		S.AutoExposureBias = 0.5f;

		// Bloom
		S.BloomIntensity = 0.8f;
		S.BloomThreshold = 1.0f;

		// Lens effects
		S.LensFlareIntensity = 0.3f;
		S.VignetteIntensity = 0.3f;

		// Motion blur (subtle for flight feel)
		S.MotionBlurAmount = 0.3f;
		S.MotionBlurMax = 5.0f;

		// Screen space reflections
		S.ScreenSpaceReflectionIntensity = 1.0f;
		S.ScreenSpaceReflectionQuality = 50.0f;
		S.ScreenSpaceReflectionMaxRoughness = 0.6f;

		UE_LOG(LogTemp, Warning, TEXT("GameMode: Post-process volume spawned (fall color grading)"));
	}
}

void AParamotorGameMode::SetupWater()
{
	UE_LOG(LogTemp, Warning, TEXT("GameMode: Setting up ocean water..."));

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// Prefer the Water plugin (real waves + shoreline) when a landscape exists.
	{
		TArray<AActor*> Existing;
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), ALandscapeProxy::StaticClass(), Existing);
		if (Existing.Num() > 0)
		{
			// WaterZone defines where water is rendered — cover the whole 20x12km map
			AWaterZone* Zone = GetWorld()->SpawnActor<AWaterZone>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
			if (Zone)
			{
				Zone->SetZoneExtent(FVector2D(2500000.0, 1500000.0));  // ~25km x 15km coverage
				// Water collision volumes span the whole zone and would swallow the pawn
				for (UActorComponent* C : Zone->GetComponents())
				{
					if (UPrimitiveComponent* P = Cast<UPrimitiveComponent>(C))
					{
						P->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
						P->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
						P->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
					}
				}
				UE_LOG(LogTemp, Warning, TEXT("GameMode: WaterZone spawned, extent 25x15km"));
			}
			// Ocean water body at sea level — fills everywhere the terrain dips below Z=0
			AWaterBodyOcean* Ocean = GetWorld()->SpawnActor<AWaterBodyOcean>(FVector(0.0f, 0.0f, 0.0f), FRotator::ZeroRotator, SpawnParams);
			if (Ocean)
			{
				for (UActorComponent* C : Ocean->GetComponents())
				{
					if (UPrimitiveComponent* P = Cast<UPrimitiveComponent>(C))
					{
						P->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
						P->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
						P->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
					}
				}
				UE_LOG(LogTemp, Warning, TEXT("GameMode: WaterBodyOcean spawned at Z=0"));
				return;
			}
			UE_LOG(LogTemp, Warning, TEXT("GameMode: WaterBodyOcean spawn failed — falling back to water plane"));
		}
	}

	UStaticMesh* PlaneMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (!PlaneMesh) return;

	// Fallback: flat water plane at sea level (slightly below 0 to avoid z-fighting)
	FVector WaterLoc(0.0f, 0.0f, -15.0f);
	WaterPlane = GetWorld()->SpawnActor<AStaticMeshActor>(WaterLoc, FRotator::ZeroRotator, SpawnParams);
	if (WaterPlane)
	{
		WaterPlane->GetStaticMeshComponent()->SetStaticMesh(PlaneMesh);
		WaterPlane->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		WaterPlane->SetActorScale3D(FVector(8000.0f, 8000.0f, 1.0f));  // 80km x 80km
		WaterPlane->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

		// Try water material, fall back to dark blue
		UMaterialInterface* WaterMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_Water.M_Water"));
		if (WaterMat)
		{
			WaterPlane->GetStaticMeshComponent()->SetMaterial(0, WaterMat);
		}
		else
		{
			UMaterial* BaseMat = LoadObject<UMaterial>(nullptr, TEXT("/Engine/EngineMaterials/WorldGridMaterial.WorldGridMaterial"));
			if (BaseMat)
			{
				UMaterialInstanceDynamic* DynMat = UMaterialInstanceDynamic::Create(BaseMat, WaterPlane);
				if (DynMat) DynMat->SetVectorParameterValue(FName("Color"), FLinearColor(0.05f, 0.15f, 0.25f, 0.9f));
				if (DynMat) WaterPlane->GetStaticMeshComponent()->SetMaterial(0, DynMat);
			}
		}
		UE_LOG(LogTemp, Warning, TEXT("GameMode: Ocean water plane spawned at Z=0, 20km x 20km"));
	}
}

void AParamotorGameMode::SetupCoastlineDirection()
{
	// Ocean is to the south (negative Y). Sea breeze blows north (+Y).
	UE_LOG(LogTemp, Warning, TEXT("GameMode: Coastline = ocean south (180 deg), sea breeze blows north"));
}

// === Coastal terrain height function ===
// Returns height in centimeters above sea level (Z=0)
// World coordinates: X = east/west, Y = south/north (south = ocean, north = hills)
float AParamotorGameMode::GetCoastalHeight(float WorldX, float WorldY) const
{
	// Terrain spans -1000m to +1000m in X and Y (2km x 2km)
	// Normalize to 0..1 where Y=0 is south (ocean), Y=1 is north (hills)
	float Yn = FMath::Clamp((WorldY + 100000.0f) / 200000.0f, 0.0f, 1.0f);
	float Xn = FMath::Clamp((WorldX + 100000.0f) / 200000.0f, 0.0f, 1.0f);

	float HeightM = 0.0f;  // height in meters

	if (Yn < 0.15f)
	{
		// Ocean floor — below sea level, sloping down
		HeightM = -1.0f - ((0.15f - Yn) / 0.15f) * 4.0f;  // -1m to -5m
	}
	else if (Yn < 0.22f)
	{
		// Beach — gradual rise from sea level to 3m
		float T = (Yn - 0.15f) / 0.07f;
		HeightM = -1.0f + T * 4.0f;
	}
	else if (Yn < 0.45f)
	{
		// Launch/landing field — flat at ~5m with very gentle undulation
		HeightM = 5.0f + FMath::Sin(Xn * 31.4f) * FMath::Sin(Yn * 47.1f) * 0.3f;
	}
	else if (Yn < 0.60f)
	{
		// Transition zone — rise from 5m to 15m with some variation
		float T = (Yn - 0.45f) / 0.15f;
		HeightM = 5.0f + T * 10.0f + FMath::Sin(Xn * 25.1f) * 0.8f + FMath::Sin(Yn * 39.2f) * 0.5f;
	}
	else
	{
		// Rolling hills — 15m to 40m with multi-frequency noise
		float T = (Yn - 0.60f) / 0.40f;
		float Base = 15.0f + T * 25.0f;
		float H1 = FMath::Sin(Xn * 12.56f) * 3.0f;
		float H2 = FMath::Sin(Xn * 28.27f + 0.7f) * 1.5f;
		float H3 = FMath::Sin(Yn * 18.84f + 1.3f) * 2.0f;
		float H4 = FMath::Sin(Xn * 6.28f + Yn * 6.28f) * 2.5f;
		HeightM = Base + H1 + H2 + H3 + H4;
	}

	return HeightM * 100.0f;  // convert meters to cm
}

void AParamotorGameMode::SetupCoastalTerrain()
{
	UE_LOG(LogTemp, Warning, TEXT("GameMode: Building Westerly Airport coastal field..."));

	// If the level already has a real Landscape (L_Westerly uses real 1m LiDAR terrain),
	// skip the procedural field entirely.
	{
		TArray<AActor*> Existing;
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), ALandscapeProxy::StaticClass(), Existing);
		if (Existing.Num() > 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("GameMode: Real landscape found (%d actors) — skipping procedural field"), Existing.Num());
			return;
		}
	}

	// Delete old template landscape actors
	TArray<AActor*> Landscapes;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), ALandscapeProxy::StaticClass(), Landscapes);
	for (AActor* L : Landscapes)
	{
		L->Destroy();
	}
	if (Landscapes.Num() > 0)
		UE_LOG(LogTemp, Warning, TEXT("GameMode: Removed %d old landscape actors"), Landscapes.Num());

	// Spawn a real AStaticMeshActor for the airport/field so we get reliable collision and rendering
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	TerrainActor = GetWorld()->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), FVector(0.0f, 0.0f, 500.0f), FRotator(0.0f, 0.0f, 0.0f), SpawnParams);
	if (TerrainActor)
	{
		TerrainActor->SetActorLabel(TEXT("WesterlyAirportField"));
		TerrainActor->SetActorEnableCollision(true);
		UStaticMesh* PlaneMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
		if (PlaneMesh)
		{
			AStaticMeshActor* SMActor = Cast<AStaticMeshActor>(TerrainActor);
			UStaticMeshComponent* MeshComp = SMActor->GetStaticMeshComponent();
			MeshComp->SetStaticMesh(PlaneMesh);
			MeshComp->SetWorldScale3D(FVector(2000.0f, 2000.0f, 1.0f));  // 2km x 2km
			MeshComp->SetMobility(EComponentMobility::Movable);
			MeshComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			MeshComp->SetCollisionProfileName(TEXT("BlockAll"));
			UMaterialInterface* LandMat = UMaterial::GetDefaultMaterial(MD_Surface);
			if (LandMat)
			{
				MeshComp->SetMaterial(0, LandMat);
				MeshComp->MarkRenderStateDirty();
				UE_LOG(LogTemp, Warning, TEXT("GameMode: Applied default surface material to Westerly field"));
			}
			MeshComp->MarkRenderStateDirty();
			MeshComp->RecreatePhysicsState();
			UE_LOG(LogTemp, Warning, TEXT("GameMode: Westerly Airport field spawned at Z=500cm, 2km x 2km"));

			// Add a large flat collision box since the Plane mesh may have no usable collision
			UBoxComponent* Box = NewObject<UBoxComponent>(TerrainActor, TEXT("FieldCollision"));
			Box->SetupAttachment(MeshComp);
			Box->SetBoxExtent(FVector(200000.0f, 200000.0f, 10.0f));  // 2000m x 2000m x 20cm
			Box->SetRelativeLocation(FVector(0.0f, 0.0f, 0.0f));
			Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			Box->SetCollisionProfileName(TEXT("BlockAll"));
			Box->CanCharacterStepUpOn = ECB_Yes;
			Box->RegisterComponent();
			Box->RecreatePhysicsState();
			UE_LOG(LogTemp, Warning, TEXT("GameMode: Westerly Airport collision floor added"));
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("GameMode: Failed to load Plane mesh for field!"));
		}
	}
}
// Procedural coastal terrain replaced with a flat Westerly Airport field for reliable collision/rendering

FVector AParamotorGameMode::GetLaunchFieldSpawnLocation() const
{
	// Ninigret Park parking lot beside the old auxiliary airstrip remnant,
	// just east of Ninigret Pond. UTM 19N E=279150 N=4583800;
	// map center UTM (269580, 4580015) = world origin.
	const float SpawnX = (279150.0f - 269580.0f) * 100.0f;   // +957000 cm
	const float SpawnY = (4583800.0f - 4580015.0f) * 100.0f; // +378500 cm

	// Drop a line trace to sit exactly on the landscape surface — multi-hit so we
	// can skip non-terrain blockers (ocean collision volumes, trees, etc.)
	FVector Start(SpawnX, SpawnY, 50000.0f);
	FVector End(SpawnX, SpawnY, -50000.0f);
	TArray<FHitResult> Hits;
	FCollisionQueryParams Params;
	Params.bTraceComplex = false;
	float TerrainZ = 1500.0f;  // fallback ~15m
	bool bFound = false;
	TArray<const AActor*> Ignored;
	for (int32 Iter = 0; Iter < 8 && !bFound; ++Iter)
	{
		Params.ClearIgnoredActors();
		Params.AddIgnoredActors(Ignored);
		Hits.Reset();
		if (!GetWorld()->LineTraceMultiByChannel(Hits, Start, End, ECC_Visibility, Params))
		{
			break;
		}
		for (const FHitResult& H : Hits)
		{
			AActor* HA = H.GetActor();
			if (Cast<ALandscapeProxy>(HA))
			{
				TerrainZ = H.ImpactPoint.Z;
				bFound = true;
				UE_LOG(LogTemp, Warning, TEXT("GameMode: Spawn trace hit landscape at Z=%.0fcm"), TerrainZ);
				break;
			}
			UE_LOG(LogTemp, Warning, TEXT("GameMode: Spawn trace skipped %s at Z=%.0fcm"),
				HA ? *HA->GetName() : TEXT("null"), H.ImpactPoint.Z);
			if (HA) Ignored.AddUnique(HA);
		}
	}
	if (!bFound)
	{
		UE_LOG(LogTemp, Warning, TEXT("GameMode: Spawn trace found no landscape at (%.0f, %.0f)"), SpawnX, SpawnY);
	}
	return FVector(SpawnX, SpawnY, TerrainZ + 200.0f);
}

AActor* AParamotorGameMode::FindPlayerStart_Implementation(AController* Player, const FString& IncomingName)
{
	// On the real-terrain map, guarantee a PlayerStart exists at the launch field
	// BEFORE the engine picks a spawn point (prevents spawning at world origin).
	TArray<AActor*> Existing;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), ALandscapeProxy::StaticClass(), Existing);
	if (Existing.Num() > 0)
	{
		TArray<AActor*> PlayerStarts;
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), APlayerStart::StaticClass(), PlayerStarts);
		FVector FieldLoc = GetLaunchFieldSpawnLocation();
		if (PlayerStarts.Num() == 0)
		{
			FActorSpawnParameters SP;
			SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			APlayerStart* NewStart = GetWorld()->SpawnActor<APlayerStart>(FieldLoc, FRotator::ZeroRotator, SP);
			if (NewStart)
			{
				UE_LOG(LogTemp, Warning, TEXT("GameMode: FindPlayerStart created PlayerStart at Ninigret %s"), *FieldLoc.ToString());
				return NewStart;
			}
		}
		else
		{
			PlayerStarts[0]->SetActorLocation(FieldLoc);
			return PlayerStarts[0];
		}
	}
	return Super::FindPlayerStart_Implementation(Player, IncomingName);
}

void AParamotorGameMode::RestartPlayer(AController* NewPlayer)
{
	if (!IsValid(NewPlayer))
	{
		return;
	}

	// Move any existing PlayerStart to the launch field before the player spawns
	TArray<AActor*> PlayerStarts;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), APlayerStart::StaticClass(), PlayerStarts);
	FVector FieldLoc = GetLaunchFieldSpawnLocation();
	if (PlayerStarts.Num() == 0)
	{
		// No PlayerStart in the map — spawn one at the launch field so the player
		// doesn't spawn at the world origin.
		FActorSpawnParameters SP;
		SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		APlayerStart* NewStart = GetWorld()->SpawnActor<APlayerStart>(FieldLoc, FRotator::ZeroRotator, SP);
		if (NewStart)
		{
			PlayerStarts.Add(NewStart);
			UE_LOG(LogTemp, Warning, TEXT("GameMode: Created PlayerStart at Ninigret Park %s"), *FieldLoc.ToString());
		}
	}
	for (AActor* PS : PlayerStarts)
	{
		PS->SetActorLocation(FieldLoc);
		UE_LOG(LogTemp, Warning, TEXT("GameMode: PlayerStart set to Ninigret Park at %s"), *FieldLoc.ToString());
	}

	// Fall back to default restart logic, which will now use the moved PlayerStart
	Super::RestartPlayer(NewPlayer);

	// The engine's collision-adjust can fling the pawn far away if the spawn point
	// overlaps a volume (e.g. water collision). Verify and force the location.
	if (APawn* P = NewPlayer->GetPawn())
	{
		FVector Loc = P->GetActorLocation();
		if (FVector::Dist2D(Loc, FieldLoc) > 500.0f || FMath::Abs(Loc.Z - FieldLoc.Z) > 500.0f)
		{
			P->SetActorLocation(FieldLoc, false, nullptr, ETeleportType::TeleportPhysics);
			UE_LOG(LogTemp, Warning, TEXT("GameMode: pawn was at %s — teleported to launch field"), *Loc.ToString());
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("GameMode: pawn spawned at %s"), *Loc.ToString());
		}
	}
}
