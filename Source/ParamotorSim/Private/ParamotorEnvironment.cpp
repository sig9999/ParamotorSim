// Copyright Epic Games, Inc. All Rights Reserved.

#include "ParamotorEnvironment.h"
#include "ParamotorGameInstance.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundBase.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"
#include "Landscape.h"
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
#include "Components/PrimitiveComponent.h"
#include "Engine/PostProcessVolume.h"

AParamotorEnvironment::AParamotorEnvironment()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
}

void AParamotorEnvironment::BeginPlay()
{
	Super::BeginPlay();

	SetupSkyAndLighting();
	SetupPostProcessing();
	SetupWater();
	SetupAmbientAudio();
	SetupUnderMapPlane();

	// Ocean is to the south (negative Y). Sea breeze blows north (+Y).
	UE_LOG(LogTemp, Warning, TEXT("Env: Coastline = ocean south (180 deg), sea breeze blows north"));

	float TimeH = 12.0f;
	if (UParamotorGameInstance* GI = GetWorld()->GetGameInstance<UParamotorGameInstance>())
		TimeH = GI->TimeOfDay;
	UpdateSunPosition(TimeH);

	UE_LOG(LogTemp, Warning, TEXT("Env: Environment setup complete"));
}

void AParamotorEnvironment::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (UParamotorGameInstance* GI = GetWorld()->GetGameInstance<UParamotorGameInstance>())
		UpdateSunPosition(GI->TimeOfDay);
}

void AParamotorEnvironment::SetupSkyAndLighting()
{
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
		}
	}
}

void AParamotorEnvironment::UpdateSunPosition(float TimeH)
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

void AParamotorEnvironment::SetupPostProcessing()
{
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
	}
}

void AParamotorEnvironment::SetupWater()
{
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
				UE_LOG(LogTemp, Warning, TEXT("Env: WaterBodyOcean spawned at Z=0"));
				return;
			}
			UE_LOG(LogTemp, Warning, TEXT("Env: WaterBodyOcean spawn failed — falling back to water plane"));
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
	}
}

void AParamotorEnvironment::SetupAmbientAudio()
{
	USoundBase* BirdsSound = LoadObject<USoundBase>(nullptr, TEXT("/Game/Sounds/birds_ambient.birds_ambient"));
	if (BirdsSound)
	{
		BirdsAudio = UGameplayStatics::SpawnSound2D(GetWorld(), BirdsSound);
		if (BirdsAudio) { BirdsAudio->bIsUISound = false; BirdsAudio->SetVolumeMultiplier(0.08f); }
	}

	USoundBase* OceanSound = LoadObject<USoundBase>(nullptr, TEXT("/Game/Sounds/ocean_waves.ocean_waves"));
	if (OceanSound)
	{
		OceanAudio = UGameplayStatics::SpawnSound2D(GetWorld(), OceanSound);
		if (OceanAudio) { OceanAudio->bIsUISound = false; OceanAudio->SetVolumeMultiplier(0.1f); }
	}
}

void AParamotorEnvironment::SetupUnderMapPlane()
{
	// Under-map plane hides the default concrete from below
	UStaticMesh* PlaneMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (!PlaneMesh) return;

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AStaticMeshActor* UnderPlane = GetWorld()->SpawnActor<AStaticMeshActor>(
		FVector(0.0f, 0.0f, -2000.0f), FRotator(180.0f, 0.0f, 0.0f), SpawnParams);
	if (UnderPlane)
	{
		UnderPlane->GetStaticMeshComponent()->SetStaticMesh(PlaneMesh);
		UnderPlane->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		UnderPlane->SetActorScale3D(FVector(2000.0f, 2000.0f, 1.0f));
		UnderPlane->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		UMaterialInterface* DarkMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/WorldGridMaterial.WorldGridMaterial"));
		if (DarkMat) UnderPlane->GetStaticMeshComponent()->SetMaterial(0, DarkMat);
	}
}
