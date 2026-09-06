#include "GrassWindManager.h"
#include "WingActor.h"
#include "Materials/MaterialParameterCollection.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "EngineUtils.h"
#include "Engine/World.h"

AGrassWindManager::AGrassWindManager()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	bAutoFindWing = true;
	WingActorRef = nullptr;
	WindMPC = nullptr;

	DirectionInterpSpeed = 6.0f;
	SpeedInterpSpeed = 4.0f;

	SmoothedWindDir = FVector(1.0f, 0.0f, 0.0f);
	SmoothedWindSpeed = 0.0f;
	SmoothedGustIntensity = 0.0f;
	bInitialized = false;
}

void AGrassWindManager::BeginPlay()
{
	Super::BeginPlay();

	// Auto-load the MPC asset if not assigned
	if (!WindMPC)
	{
		WindMPC = LoadObject<UMaterialParameterCollection>(
			nullptr,
			TEXT("/Game/Materials/MPC_Wind.MPC_Wind"));
		if (WindMPC)
		{
			UE_LOG(LogTemp, Log, TEXT("GrassWindManager: Auto-loaded MPC_Wind"));
		}
		else
		{
			UE_LOG(LogTemp, Warning,
				TEXT("GrassWindManager: MPC_Wind not found. Run make_grass_material.py to create it."));
		}
	}

	// Auto-find the wing actor
	if (bAutoFindWing && !WingActorRef)
	{
		for (TActorIterator<AWingActor> It(GetWorld()); It; ++It)
		{
			WingActorRef = *It;
			UE_LOG(LogTemp, Log, TEXT("GrassWindManager: Found WingActor %s"), *It->GetName());
			break;
		}
		if (!WingActorRef)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("GrassWindManager: No WingActor found in level. Wind will stay at default."));
		}
	}
}

void AGrassWindManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!WindMPC)
		return;

	// If we don't have a wing ref, try to find one each frame (wing may spawn late)
	if (!WingActorRef && bAutoFindWing)
	{
		for (TActorIterator<AWingActor> It(GetWorld()); It; ++It)
		{
			WingActorRef = *It;
			break;
		}
	}

	FVector TargetDir(1.0f, 0.0f, 0.0f);
	float TargetSpeed = 0.0f;
	float TargetGust = 0.0f;

	if (WingActorRef)
	{
		TargetDir = WingActorRef->GetWindDirection().GetSafeNormal();
		TargetSpeed = WingActorRef->GetCurrentWindSpeedMs();
		TargetGust = FMath::Clamp(WingActorRef->GetWindTurbulence(), 0.0f, 1.0f);
	}

	// Smooth the values so grass doesn't snap on gust edges
	if (!bInitialized)
	{
		SmoothedWindDir = TargetDir;
		SmoothedWindSpeed = TargetSpeed;
		SmoothedGustIntensity = TargetGust;
		bInitialized = true;
	}
	else
	{
		// Direction: spherical-ish lerp via simple vector interp (good enough for grass)
		SmoothedWindDir = FMath::VInterpTo(SmoothedWindDir, TargetDir, DeltaTime, DirectionInterpSpeed);
		SmoothedWindDir = SmoothedWindDir.GetSafeNormal();
		SmoothedWindSpeed = FMath::FInterpTo(SmoothedWindSpeed, TargetSpeed, DeltaTime, SpeedInterpSpeed);
		SmoothedGustIntensity = FMath::FInterpTo(SmoothedGustIntensity, TargetGust, DeltaTime, SpeedInterpSpeed);
	}

	// Write to the MPC
	// WindVector: XYZ = direction, W = speed (m/s)
	FLinearColor WindVec(SmoothedWindDir.X, SmoothedWindDir.Y, SmoothedWindDir.Z, SmoothedWindSpeed);
	UKismetMaterialLibrary::SetVectorParameterValue(this, WindMPC, FName("WindVector"), WindVec);
	UKismetMaterialLibrary::SetScalarParameterValue(this, WindMPC, FName("WindStrength"), SmoothedWindSpeed);
	UKismetMaterialLibrary::SetScalarParameterValue(this, WindMPC, FName("WindGust"), SmoothedGustIntensity);
}
