// Copyright Epic Games, Inc. All Rights Reserved.

#include "ParamotorCharacter.h"
#include "ParamotorLogging.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "ParamotorGameInstance.h"

// ==== INPUT ====

void AParamotorCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	PlayerInputComponent->BindAxis("MoveForward", this, &AParamotorCharacter::MoveForward);
	PlayerInputComponent->BindAxis("MoveRight", this, &AParamotorCharacter::MoveRight);
	PlayerInputComponent->BindAxis("Turn", this, &AParamotorCharacter::Turn);
	PlayerInputComponent->BindAxis("LookUp", this, &AParamotorCharacter::LookUp);
	PlayerInputComponent->BindAxis("MouseWheel", this, &AParamotorCharacter::MouseWheel);
	PlayerInputComponent->BindAction("Jump", IE_Pressed, this, &AParamotorCharacter::StartJump);
	PlayerInputComponent->BindAction("Jump", IE_Released, this, &AParamotorCharacter::StopJump);
	PlayerInputComponent->BindAction("Run", IE_Pressed, this, &AParamotorCharacter::StartSprint);
	PlayerInputComponent->BindAction("Run", IE_Released, this, &AParamotorCharacter::StopSprint);
}
void AParamotorCharacter::MoveForward(float Value)
{
	// During flight, the wing physics control all movement — ignore WASD input
	if (bWeightShiftActive) return;
	if (Value != 0.0f && GetController())
	{
		const FRotator YawRotation(0.0f, GetController()->GetControlRotation().Yaw, 0.0f);
		AddMovementInput(FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X), Value);
	}
}
void AParamotorCharacter::MoveRight(float Value)
{
	// During flight, A/D = weight shift (not strafe)
	// A = left weight shift (Value = -1), D = right weight shift (Value = +1)
	if (bWeightShiftActive)
	{
		float Dt = GetWorld()->GetDeltaSeconds();
		// Direct set from key input (instant response, smoothed by FInterpTo)
		WeightShiftTarget = FMath::Clamp(Value, -1.0f, 1.0f);
		WeightShift = FMath::FInterpTo(WeightShift, WeightShiftTarget, Dt, 6.0f);
		// Visual roll of character mesh (lean into the shift)
		WeightShiftVisualRoll = FMath::FInterpTo(WeightShiftVisualRoll, WeightShift * 15.0f, Dt, 5.0f);
		return;
	}
	if (Value != 0.0f && GetController())
	{
		const FRotator YawRotation(0.0f, GetController()->GetControlRotation().Yaw, 0.0f);
		AddMovementInput(FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y), Value);
	}
}
void AParamotorCharacter::Turn(float Value)
{
	// Mouse X = camera orbit.
	// When flying, WingActor controls player rotation (bUseControllerRotationYaw=false),
	// so this only orbits the camera. Weight shift is controlled by A/D keys.
	// On the ground, this turns the player (bUseControllerRotationYaw=true).
	float Sensitivity = 1.0f;
	if (UParamotorGameInstance* GI = GetWorld()->GetGameInstance<UParamotorGameInstance>())
	{
		Sensitivity = GI->MouseSensitivity;
	}
	AddControllerYawInput(Value * Sensitivity);

	// Decay weight shift when not flying
	if (!bWeightShiftActive)
	{
		float Dt = GetWorld()->GetDeltaSeconds();
		WeightShiftTarget = FMath::FInterpTo(WeightShiftTarget, 0.0f, Dt, 3.0f);
		WeightShift = FMath::FInterpTo(WeightShift, 0.0f, Dt, 3.0f);
		WeightShiftVisualRoll = FMath::FInterpTo(WeightShiftVisualRoll, 0.0f, Dt, 3.0f);
	}
}
void AParamotorCharacter::LookUp(float Value)
{
	float Sensitivity = 1.0f;
	if (UParamotorGameInstance* GI = GetWorld()->GetGameInstance<UParamotorGameInstance>())
	{
		Sensitivity = GI->MouseSensitivity;
	}
	AddControllerPitchInput(Value * Sensitivity);
}
void AParamotorCharacter::MouseWheel(float Value)
{
	if (Value != 0.0f && SpringArm)
	{
		// Scroll up = zoom in (shorter arm), scroll down = zoom out (longer arm)
		float CurrentLength = SpringArm->TargetArmLength;
		float NewLength = CurrentLength - Value * 60.0f; // 60cm per scroll notch
		// Clamp between 80cm (first person-ish) and 2000cm (far out)
		NewLength = FMath::Clamp(NewLength, 80.0f, 2000.0f);
		SpringArm->TargetArmLength = NewLength;
	}
}
void AParamotorCharacter::StartJump()
{
	if (bIsFallen) return;  // Can't jump while fallen
	// Can't jump with a broken leg
	if (CurrentInjury == EInjuryType::BrokenLeg && InjurySeverity > 0.5f)
	{
		UE_LOG(LogParamotor, Log, TEXT("ParamotorCharacter: Can't jump — broken leg!"));
		return;
	}
	// Apply injury jump penalty
	GetCharacterMovement()->JumpZVelocity = 420.0f * InjuryJumpMultiplier;
	// If thrust is active, jumping = loss of ground contact = thrown by thrust
	if (CurrentThrust > 0.1f)
	{
		UE_LOG(LogParamotor, Log, TEXT("ParamotorCharacter: Jumped with thrust=%.2f — will trip on landing!"), CurrentThrust);
		bWasInAirWithThrust = true;
	}
	bPressedJump = true;
}
void AParamotorCharacter::StopJump()
{
	bPressedJump = false;
	StopJumping();
}
void AParamotorCharacter::StartSprint()
{
	bSprintRequested = true;
}
void AParamotorCharacter::StopSprint()
{
	bSprintRequested = false;
}