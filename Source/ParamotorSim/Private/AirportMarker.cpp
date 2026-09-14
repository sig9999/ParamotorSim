// Copyright Epic Games, Inc. All Rights Reserved.

#include "AirportMarker.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"

AAirportMarker::AAirportMarker()
{
	PrimaryActorTick.bCanEverTick = true;

	ClickSphere = CreateDefaultSubobject<USphereComponent>(TEXT("ClickSphere"));
	SetRootComponent(ClickSphere);
	ClickSphere->SetSphereRadius(50000.0f); // 500m — generous click target
	ClickSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	ClickSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	ClickSphere->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	ClickSphere->SetGenerateOverlapEvents(false);

	PinMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PinMesh"));
	PinMesh->SetupAttachment(ClickSphere);
	PinMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PinMesh->SetCastShadow(false);

	// Small sphere as the pin visual (base diameter = 100 cm)
	UStaticMesh* SphereMesh = Cast<UStaticMesh>(StaticLoadObject(
		UStaticMesh::StaticClass(), nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")));
	if (SphereMesh)
	{
		PinMesh->SetStaticMesh(SphereMesh);
		PinMesh->SetRelativeScale3D(FVector(50.0f)); // 50m initial, overridden by UpdateVisualScale
	}

	// ICAO code label above the pin
	NameLabel = CreateDefaultSubobject<UTextRenderComponent>(TEXT("NameLabel"));
	NameLabel->SetupAttachment(ClickSphere);
	NameLabel->SetRelativeLocation(FVector(0.0f, 0.0f, 12000.0f)); // above pin
	NameLabel->SetHorizontalAlignment(EHTA_Center);
	NameLabel->SetVerticalAlignment(EVRTA_TextBottom);
	NameLabel->SetWorldSize(8000.0f); // large text visible at distance
	NameLabel->SetTextRenderColor(FColor::Cyan);
}

void AAirportMarker::BeginPlay()
{
	Super::BeginPlay();
}

void AAirportMarker::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UpdateVisualScale();
}

void AAirportMarker::InitializeAirport(const FAirportInfo& Info, int32 Index)
{
	AirportData = Info;
	AirportIndex = Index;

	if (NameLabel)
	{
		NameLabel->SetText(FText::FromString(Info.ICAO));
		NameLabel->SetTextRenderColor(Info.bAvailable ? FColor::Cyan : FColor(100, 100, 100));
	}
}

void AAirportMarker::UpdateVisualScale()
{
	// Scale pin, label, and click target proportionally to camera distance
	// so they stay a constant screen size regardless of zoom level.
	APlayerCameraManager* CamMgr = UGameplayStatics::GetPlayerCameraManager(GetWorld(), 0);
	if (!CamMgr) return;

	FVector CamLoc = CamMgr->GetCameraLocation();
	float Dist = FVector::Dist(CamLoc, GetActorLocation());
	if (Dist < 1.0f) Dist = 1.0f;

	// Sphere mesh base = 100 cm diameter. Pin appears as ~1.5% of camera
	// distance (~10px dot at globe zoom, scaling down as you zoom in).
	float PinScale = Dist * 0.015f / 100.0f;
	PinMesh->SetRelativeScale3D(FVector(PinScale));

	// ICAO label — world-space text height ~1.6% of camera distance (~12px
	// tall), sitting just above the pin and facing the camera.
	float LabelSize = Dist * 0.016f;
	NameLabel->SetWorldSize(LabelSize);
	NameLabel->SetRelativeLocation(FVector(0.0f, 0.0f, PinScale * 58.0f));
	NameLabel->SetWorldRotation((CamLoc - NameLabel->GetComponentLocation()).Rotation());

	// Click sphere — ~2.5x the pin's visual radius, minimum 30 m
	ClickSphere->SetSphereRadius(FMath::Max(Dist * 0.025f, 3000.0f));
}
