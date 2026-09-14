// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "HangarPawn.generated.h"

UCLASS()
class PARAMOTORSIM_API AHangarPawn : public APawn
{
	GENERATED_BODY()

public:
	AHangarPawn(const FObjectInitializer& ObjectInitializer);

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	class UCameraComponent* CameraComp;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hangar")
	float OrbitYaw = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hangar")
	float OrbitPitch = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hangar")
	float CameraDistance = 950.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hangar")
	float ZoomMin = 180.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hangar")
	float ZoomMax = 1400.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hangar")
	float ZoomStep = 30.0f;

protected:
	void ZoomIn();
	void ZoomOut();
	void Turn(float Value);
	void LookUp(float Value);
	void OnDragStart();
	void OnDragEnd();
	float GetMouseSensitivity() const;

	/** LMB held — drags spin the podium and tilt the camera. */
	bool bDragging = false;
	/** Grace period after a drag before the turntable resumes. */
	float TurntableHold = 0.0f;
};
