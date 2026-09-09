#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "ParamotorAnimInstance.generated.h"

class UAnimSequence;
class AParamotorCharacter;

// Proxy that evaluates animation and applies arm IK
struct FParamotorAnimProxy : public FAnimInstanceProxy
{
	FParamotorAnimProxy() {}
	FParamotorAnimProxy(UAnimInstance* InAnimInstance) : FAnimInstanceProxy(InAnimInstance) {}

	virtual bool Evaluate(FPoseContext& Context) override;

	// IK target data — set from NativeUpdateAnimation
	bool bLeftArmActive = false;
	bool bRightArmActive = false;
	FVector LeftHandTarget = FVector::ZeroVector;   // World space
	FVector RightHandTarget = FVector::ZeroVector;  // World space
	float LeftArmRaise = 0.0f;
	float RightArmRaise = 0.0f;
	float LeftArmPull = 0.0f;
	float RightArmPull = 0.0f;

	// Current animation to play
	UAnimSequence* CurrentAnim = nullptr;
	float AnimTime = 0.0f;
	float PlayRate = 1.0f;
	bool bLoops = true;

private:
	void ApplyArmIK(FPoseContext& Context, bool bLeftArm, FVector TargetPos, float Raise, float Pull);
};

UCLASS()
class PARAMOTORSIM_API UParamotorAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override { return &Proxy; }
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override {} // Prevent delete of member variable

	// Called by the character to play a specific animation
	void PlaySequence(UAnimSequence* Anim, bool bLooping);

	// Called by the character to set play rate
	void SetPlayRate(float Rate);

private:
	// The proxy handles evaluation
	FParamotorAnimProxy Proxy;

	// Cached owning character
	UPROPERTY()
	AParamotorCharacter* OwningCharacter = nullptr;

	// Strong reference so the animation asset is not garbage collected
	UPROPERTY()
	UAnimSequence* CurrentAnim = nullptr;
};
