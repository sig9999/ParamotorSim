// Copyright Epic Games, Inc. All Rights Reserved.

#include "HangarPawn.h"
#include "HangarGameMode.h"
#include "Components/SphereComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/InputComponent.h"
#include "ParamotorGameInstance.h"
#include "Kismet/GameplayStatics.h"

AHangarPawn::AHangarPawn(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = true;

	USphereComponent* Root = CreateDefaultSubobject<USphereComponent>(TEXT("Root"));
	SetRootComponent(Root);
	Root->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	CameraComp = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	CameraComp->SetupAttachment(RootComponent);
	CameraComp->SetFieldOfView(55.0f);
}

void AHangarPawn::BeginPlay()
{
	Super::BeginPlay();
	SetActorLocation(FVector::ZeroVector);
}

void AHangarPawn::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Pause the podium turntable while the user is dragging (plus a short
	// grace after release) so it never fights the mouse.
	AHangarGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<AHangarGameMode>() : nullptr;
	if (GM)
	{
		if (bDragging)
		{
			TurntableHold = 1.5f;
		}
		else if (TurntableHold > 0.0f)
		{
			TurntableHold -= DeltaTime;
		}
		GM->SetTurntablePaused(TurntableHold > 0.0f);
	}

	if (!CameraComp) return;

	const FVector PodiumTarget = GetActorLocation() + FVector(0.0f, 0.0f, 8.0f);
	const float Yaw = FMath::DegreesToRadians(OrbitYaw);
	const float Pitch = FMath::DegreesToRadians(OrbitPitch);
	const FVector Direction(
		FMath::Cos(Pitch) * FMath::Cos(Yaw),
		FMath::Cos(Pitch) * FMath::Sin(Yaw),
		FMath::Sin(Pitch));
	const FVector CameraLocation = PodiumTarget + Direction * CameraDistance;
	const FRotator BaseRotation = (PodiumTarget - CameraLocation).Rotation();
	const FVector CameraRight = BaseRotation.RotateVector(FVector::RightVector);
	const FVector CameraUp = BaseRotation.RotateVector(FVector::UpVector);
	const float HalfWidth = CameraDistance * FMath::Tan(FMath::DegreesToRadians(CameraComp->FieldOfView * 0.5f));
	const float AspectRatio = CameraComp->AspectRatio > 0.0f ? CameraComp->AspectRatio : (16.0f / 9.0f);
	const float HalfHeight = HalfWidth / AspectRatio;
	// Center the podium in the preview area between the two UI rails —
	// slight upward aim keeps the rig on the podium in frame.
	const FVector LookTarget = PodiumTarget + CameraUp * (HalfHeight * 0.12f);
	CameraComp->SetWorldLocation(CameraLocation);
	CameraComp->SetWorldRotation((LookTarget - CameraLocation).Rotation());

}

void AHangarPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	PlayerInputComponent->BindAxis("Turn", this, &AHangarPawn::Turn);
	PlayerInputComponent->BindAxis("LookUp", this, &AHangarPawn::LookUp);
	PlayerInputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &AHangarPawn::OnDragStart);
	PlayerInputComponent->BindKey(EKeys::LeftMouseButton, IE_Released, this, &AHangarPawn::OnDragEnd);
	PlayerInputComponent->BindKey(EKeys::MouseScrollUp, IE_Pressed, this, &AHangarPawn::ZoomIn);
	PlayerInputComponent->BindKey(EKeys::MouseScrollDown, IE_Pressed, this, &AHangarPawn::ZoomOut);
}

void AHangarPawn::ZoomIn()
{
	CameraDistance = FMath::Clamp(CameraDistance - ZoomStep, ZoomMin, ZoomMax);
}

void AHangarPawn::ZoomOut()
{
	CameraDistance = FMath::Clamp(CameraDistance + ZoomStep, ZoomMin, ZoomMax);
}

float AHangarPawn::GetMouseSensitivity() const
{
	if (UParamotorGameInstance* GI = Cast<UParamotorGameInstance>(UGameplayStatics::GetGameInstance(this)))
	{
		return GI->MouseSensitivity;
	}
	return 1.0f;
}

void AHangarPawn::OnDragStart()
{
	bDragging = true;
}

void AHangarPawn::OnDragEnd()
{
	bDragging = false;
}

void AHangarPawn::Turn(float Value)
{
	// LMB-drag spins the podium (turntable), not the camera — like grabbing
	// the turntable in a showroom.
	if (bDragging && FMath::Abs(Value) > 0.01f)
	{
		if (AHangarGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<AHangarGameMode>() : nullptr)
		{
			if (AActor* Podium = GM->GetPodium())
			{
				// Negative: positive mouse-X (drag right) should bring the
				// right side of the rig toward the viewer — clockwise from
				// above, which is negative yaw in UE.
				Podium->AddActorLocalRotation(FRotator(0.0f, Value * -1.1f, 0.0f));
			}
		}
	}
}

void AHangarPawn::LookUp(float Value)
{
	if (bDragging && FMath::Abs(Value) > 0.01f)
	{
		OrbitPitch = FMath::Clamp(OrbitPitch + Value * GetMouseSensitivity(), -5.0f, 35.0f);
	}
}
