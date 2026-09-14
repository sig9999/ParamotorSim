// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "WorldMapPawn.generated.h"

UCLASS()
class PARAMOTORSIM_API AWorldMapPawn : public APawn
{
	GENERATED_BODY()

public:
	AWorldMapPawn(const FObjectInitializer& ObjectInitializer);

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	class USpringArmComponent* CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	class UCameraComponent* CameraComp;

	/** Degrees per second for the idle globe rotation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Globe")
	float RotationSpeed = 0.35f;

	/** Desired boom length — zoom scrolls change this, Tick eases toward it. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Globe")
	float DesiredArmLength = 1911300000.0f;

	/** Minimum camera distance from Earth center (cm). ~Earth radius + 50 km —
	 *  sees a ~100 km region (whole RI coastline + pin cluster). Below this the
	 *  imagery tiles exceed their max detail level and go blurry. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Globe")
	float ZoomMin = 642100000.0f;

	/** Maximum camera distance from Earth center (cm). ~Earth radius * 4 = see whole Earth. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Globe")
	float ZoomMax = 2548400000.0f;

	/** Fraction of current zoom to change per scroll step (0.08 = 8%). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Globe")
	float ZoomFraction = 0.08f;

	/** True when the user is dragging the mouse to rotate the globe. */
	bool bUserDragging = false;

	/** Seconds of idle rotation lockout remaining after the last interaction. */
	float IdleTimer = 0.0f;

	/** How long the idle spin stays paused after a drag or zoom (seconds). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Globe")
	float IdleHoldTime = 3.0f;

protected:
	void ZoomIn();
	void ZoomOut();
	void Turn(float Value);
	void LookUp(float Value);
	void OnLeftMouseDown();
	void OnLeftMouseUp();
	float GetMouseSensitivity() const;

	/** Try to click an airport pin at the mouse position. */
	void TryClickAirportPin();
};
