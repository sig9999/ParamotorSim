// Copyright Epic Games, Inc. All Rights Reserved.

#include "WindsockActor.h"
#include "Components/StaticMeshComponent.h"
#include "Math/UnrealMathUtility.h"

AWindsockActor::AWindsockActor()
{
	PrimaryActorTick.bCanEverTick = true;

	// Pole is the root - it stays fixed
	PoleMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PoleMesh"));
	RootComponent = PoleMesh;

	// Sock is a child - it rotates/scales with wind
	SockMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SockMesh"));
	SockMesh->SetupAttachment(PoleMesh);

	SmoothedYaw = WindDirection;  // Start aligned with target (no snap)
	SmoothedInflation = 0.0f;
}

void AWindsockActor::BeginPlay()
{
	Super::BeginPlay();

	SmoothedYaw = WindDirection;  // Start aligned with target (no snap)
	SmoothedInflation = MaxInflationSpeed > 0.0f ? FMath::Clamp(WindSpeed / MaxInflationSpeed, 0.0f, 1.0f) : 0.0f;
}

void AWindsockActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!SockMesh)
		return;

	// ============================================================
	// Wind direction -> sock rotation
	// WindDirection is where wind comes FROM.
	// The sock mesh extends in -X, so we rotate by WindDirection
	// (the -X direction will point toward where wind comes FROM,
	//  meaning the sock body points downwind = where wind goes TO)
	// ============================================================
	float TargetYaw = WindDirection;
	while (TargetYaw < 0.0f) TargetYaw += 360.0f;
	while (TargetYaw >= 360.0f) TargetYaw -= 360.0f;

	// Smooth yaw with wraparound
	float YawDiff = TargetYaw - SmoothedYaw;
	while (YawDiff > 180.0f) YawDiff -= 360.0f;
	while (YawDiff < -180.0f) YawDiff += 360.0f;
	SmoothedYaw += YawDiff * FMath::Clamp(DeltaTime * 2.0f, 0.0f, 1.0f);

	// ============================================================
	// Wind speed -> inflation
	// 0 kt = limp, hanging down
	// 15+ kt = fully inflated, horizontal
	// ============================================================
	float TargetInflation = MaxInflationSpeed > 0.0f ? FMath::Clamp(WindSpeed / MaxInflationSpeed, 0.0f, 1.0f) : 0.0f;
	SmoothedInflation = FMath::FInterpTo(SmoothedInflation, TargetInflation, DeltaTime, 1.5f);

	// Scale: partially deflate at low wind
	float ScaleX = 0.3f + 0.7f * SmoothedInflation;   // length
	float ScaleYZ = 0.4f + 0.6f * SmoothedInflation;   // girth

	// Droop: at 0 wind sock hangs 80 degrees down, at full wind it's horizontal
	float DroopPitch = (1.0f - SmoothedInflation) * 80.0f;

	// The sock mesh extends in -X direction from the pivot at the top of the pole
	// Yaw rotates it to point downwind, pitch makes it droop
	SockMesh->SetRelativeRotation(FRotator(DroopPitch, SmoothedYaw + 180.0f, 0.0f));
	SockMesh->SetRelativeScale3D(FVector(ScaleX, ScaleYZ, ScaleYZ));
}
