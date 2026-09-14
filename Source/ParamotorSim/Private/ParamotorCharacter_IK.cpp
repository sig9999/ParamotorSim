// Copyright Epic Games, Inc. All Rights Reserved.

#include "ParamotorCharacter.h"

// ==== BRAKE-ARM IK ====

void AParamotorCharacter::SetBrakeArmTargets(bool bLeftActive, bool bRightActive, FVector LeftTarget, FVector RightTarget,
	bool bLeftBraking, bool bRightBraking)
{
	bLeftBrakeArmActive = bLeftActive;
	bRightBrakeArmActive = bRightActive;
	LeftBrakeHandTarget = LeftTarget;
	RightBrakeHandTarget = RightTarget;

	// Store desired pull state (0 = hand up at handle, 1 = pulled down)
	bLeftBrakePulling = bLeftBraking;
	bRightBrakePulling = bRightBraking;
}
void AParamotorCharacter::ClearBrakeArmTargets()
{
	bLeftBrakeArmActive = false;
	bRightBrakeArmActive = false;
	bLeftBrakePulling = false;
	bRightBrakePulling = false;
}
void AParamotorCharacter::UpdateBrakeArmIK(float DeltaTime)
{
	// Smoothly raise arms when brake handles are available (wing connected/inflated)
	float LeftTargetRaise = bLeftBrakeArmActive ? 1.0f : 0.0f;
	float RightTargetRaise = bRightBrakeArmActive ? 1.0f : 0.0f;

	LeftArmRaise = FMath::FInterpTo(LeftArmRaise, LeftTargetRaise, DeltaTime, 4.0f);
	RightArmRaise = FMath::FInterpTo(RightArmRaise, RightTargetRaise, DeltaTime, 4.0f);

	// Smoothly pull arms down when braking, return up when released
	// Faster pull-down (responsive brake), slightly slower return (natural release)
	float LeftPullTarget = (bLeftBrakeArmActive && bLeftBrakePulling) ? 1.0f : 0.0f;
	float RightPullTarget = (bRightBrakeArmActive && bRightBrakePulling) ? 1.0f : 0.0f;
	float LeftPullSpeed = bLeftBrakePulling ? 10.0f : 6.0f;
	float RightPullSpeed = bRightBrakePulling ? 10.0f : 6.0f;
	LeftArmPull = FMath::FInterpTo(LeftArmPull, LeftPullTarget, DeltaTime, LeftPullSpeed);
	RightArmPull = FMath::FInterpTo(RightArmPull, RightPullTarget, DeltaTime, RightPullSpeed);

	// The actual bone IK is applied in UParamotorAnimInstance::Evaluate
	// via the FParamotorAnimProxy. This function just updates the target values.
}