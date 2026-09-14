// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldMapPawn.h"
#include "AirportMarker.h"
#include "WorldMapGameMode.h"
#include "Components/SphereComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/InputComponent.h"
#include "ParamotorGameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"

// Earth radius in cm — ~6371 km
static constexpr float EARTH_RADIUS_CM = 637100000.0f;

AWorldMapPawn::AWorldMapPawn(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = true;

	USphereComponent* Root = CreateDefaultSubobject<USphereComponent>(TEXT("Root"));
	SetRootComponent(Root);
	Root->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Root->SetSphereRadius(1.0f);

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->SetRelativeLocation(FVector::ZeroVector);
	// Whole-globe default — BeginPlay applies DesiredArmLength.
	CameraBoom->TargetArmLength = EARTH_RADIUS_CM * 3.0f;
	CameraBoom->bDoCollisionTest = false;
	CameraBoom->bInheritPitch = false;
	CameraBoom->bInheritYaw = false;
	CameraBoom->bInheritRoll = false;
	CameraBoom->bUsePawnControlRotation = false;

	CameraComp = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	CameraComp->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	CameraComp->SetRelativeRotation(FRotator(0.0f, 0.0f, 0.0f));
}

void AWorldMapPawn::BeginPlay()
{
	Super::BeginPlay();

	// Place the pawn at the Earth center so the boom orbits the globe.
	// The georeference origin is on the surface — Earth center is at -EarthRadius.
	SetActorLocation(FVector(0.0f, 0.0f, -EARTH_RADIUS_CM));

	// Point the boom so the camera orbits the globe. The pawn sits at Earth
	// center; the georeference origin (RI airport cluster) is at the pawn's
	// +Z. Pitch -89.5 puts the camera almost directly over that point looking
	// down, yaw -90 turns north up so labels read upright on screen.
	if (CameraBoom)
	{
		CameraBoom->SetRelativeRotation(FRotator(-89.5f, -90.0f, 0.0f));
		DesiredArmLength = EARTH_RADIUS_CM * 3.0f; // ~12742 km alt, whole-globe view
		CameraBoom->TargetArmLength = DesiredArmLength;
	}
}

void AWorldMapPawn::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!CameraBoom) return;

	// Ease the boom toward the scroll-wheel target distance — gentle glide
	if (!FMath::IsNearlyEqual(CameraBoom->TargetArmLength, DesiredArmLength, 1.0f))
	{
		CameraBoom->TargetArmLength = FMath::FInterpTo(
			CameraBoom->TargetArmLength, DesiredArmLength, DeltaTime, 1.8f);
	}

	// Slow idle rotation — pauses while dragging and for a few seconds after
	// any interaction (drag or zoom) so the view never fights the user.
	if (bUserDragging)
	{
		IdleTimer = IdleHoldTime;
	}
	else if (IdleTimer > 0.0f)
	{
		IdleTimer -= DeltaTime;
	}
	else
	{
		CameraBoom->AddLocalRotation(FRotator(0.0f, RotationSpeed * DeltaTime, 0.0f));
	}
}

void AWorldMapPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	PlayerInputComponent->BindAxis("Turn", this, &AWorldMapPawn::Turn);
	PlayerInputComponent->BindAxis("LookUp", this, &AWorldMapPawn::LookUp);
	PlayerInputComponent->BindKey(EKeys::MouseScrollUp, IE_Pressed, this, &AWorldMapPawn::ZoomIn);
	PlayerInputComponent->BindKey(EKeys::MouseScrollDown, IE_Pressed, this, &AWorldMapPawn::ZoomOut);
	PlayerInputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &AWorldMapPawn::OnLeftMouseDown);
	PlayerInputComponent->BindKey(EKeys::LeftMouseButton, IE_Released, this, &AWorldMapPawn::OnLeftMouseUp);
}

void AWorldMapPawn::ZoomIn()
{
	DesiredArmLength = FMath::Clamp(DesiredArmLength * (1.0f - ZoomFraction), ZoomMin, ZoomMax);
	IdleTimer = IdleHoldTime;
}

void AWorldMapPawn::ZoomOut()
{
	DesiredArmLength = FMath::Clamp(DesiredArmLength * (1.0f + ZoomFraction), ZoomMin, ZoomMax);
	IdleTimer = IdleHoldTime;
}

float AWorldMapPawn::GetMouseSensitivity() const
{
	if (UParamotorGameInstance* GI = Cast<UParamotorGameInstance>(UGameplayStatics::GetGameInstance(this)))
	{
		return GI->MouseSensitivity;
	}
	return 1.0f;
}

void AWorldMapPawn::Turn(float Value)
{
	if (CameraBoom && FMath::Abs(Value) > 0.01f && bUserDragging)
	{
		CameraBoom->AddLocalRotation(FRotator(0.0f, Value * GetMouseSensitivity(), 0.0f));
	}
}

void AWorldMapPawn::LookUp(float Value)
{
	if (CameraBoom && FMath::Abs(Value) > 0.01f && bUserDragging)
	{
		// Negated so the globe follows the cursor — drag down, view tips down.
		CameraBoom->AddLocalRotation(FRotator(-Value * GetMouseSensitivity(), 0.0f, 0.0f));

		// Clamp tilt: near straight-down (-89) to a shallow globe angle (-25).
		// -89 keeps the camera hovering the georeference-origin region.
		FRotator R = CameraBoom->GetRelativeRotation();
		R.Pitch = FMath::ClampAngle(R.Pitch, -89.0f, -25.0f);
		CameraBoom->SetRelativeRotation(R);
	}
}

void AWorldMapPawn::OnLeftMouseDown()
{
	bUserDragging = true;
	TryClickAirportPin();
}

void AWorldMapPawn::OnLeftMouseUp()
{
	bUserDragging = false;
}

void AWorldMapPawn::TryClickAirportPin()
{
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC) return;

	FHitResult Hit;
	bool bHit = PC->GetHitResultUnderCursor(ECC_Visibility, false, Hit);
	if (!bHit) return;

	AAirportMarker* Marker = Cast<AAirportMarker>(Hit.GetActor());
	if (!Marker) return;

	if (AWorldMapGameMode* GM = Cast<AWorldMapGameMode>(UGameplayStatics::GetGameMode(this)))
	{
		GM->SelectAirport(Marker->AirportIndex);
	}
}
