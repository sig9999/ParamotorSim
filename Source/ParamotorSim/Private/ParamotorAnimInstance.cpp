#include "ParamotorAnimInstance.h"
#include "ParamotorCharacter.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstanceProxy.h"
#include "AnimationRuntime.h"
#include "BonePose.h"
#include "TwoBoneIK.h"
#include "AnimationCoreLibrary.h"

void UParamotorAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();

	OwningCharacter = Cast<AParamotorCharacter>(GetOwningActor());
}

void UParamotorAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	// Advance animation time
	if (IsValid(Proxy.CurrentAnim))
	{
		float SeqLength = Proxy.CurrentAnim->GetPlayLength();
		if (SeqLength > 0.0f)
		{
			Proxy.AnimTime += DeltaSeconds * Proxy.PlayRate;
			if (Proxy.AnimTime >= SeqLength)
			{
				if (Proxy.bLoops)
					Proxy.AnimTime = FMath::Fmod(Proxy.AnimTime, SeqLength);
				else
					Proxy.AnimTime = SeqLength;  // Clamp to end (hold last frame)
			}
		}
	}

	// Update IK data from owning character
	if (IsValid(OwningCharacter))
	{
		Proxy.bLeftArmActive = OwningCharacter->bLeftBrakeArmActive;
		Proxy.bRightArmActive = OwningCharacter->bRightBrakeArmActive;
		Proxy.LeftHandTarget = OwningCharacter->LeftBrakeHandTarget;
		Proxy.RightHandTarget = OwningCharacter->RightBrakeHandTarget;
		Proxy.LeftArmRaise = OwningCharacter->LeftArmRaise;
		Proxy.RightArmRaise = OwningCharacter->RightArmRaise;
		Proxy.LeftArmPull = OwningCharacter->LeftArmPull;
		Proxy.RightArmPull = OwningCharacter->RightArmPull;
	}
}

void UParamotorAnimInstance::PlaySequence(UAnimSequence* Anim, bool bLooping)
{
	CurrentAnim = Anim;
	if (Proxy.CurrentAnim != Anim)
	{
		Proxy.CurrentAnim = Anim;
		Proxy.AnimTime = 0.0f;
	}
	Proxy.bLoops = bLooping;
}

void UParamotorAnimInstance::SetPlayRate(float Rate)
{
	Proxy.PlayRate = Rate;
}

// ============================================================
// Proxy Evaluate — plays animation and applies arm IK
// ============================================================

bool FParamotorAnimProxy::Evaluate(FPoseContext& Context)
{
	// Step 1: Fill the pose from the current animation sequence
	if (IsValid(CurrentAnim) && CurrentAnim->GetPlayLength() > 0.0f)
	{
		FAnimExtractContext ExtractCtx(AnimTime, false);
		FAnimationPoseData AnimPoseData(Context);
		CurrentAnim->GetBonePose(AnimPoseData, ExtractCtx);
	}
	else
	{
		Context.Pose.ResetToRefPose();
	}

	// Step 2: Apply arm IK for brake handles
	// Only apply if arms are being raised toward handles
	if (bLeftArmActive && LeftArmRaise > 0.01f)
	{
		ApplyArmIK(Context, true, LeftHandTarget, LeftArmRaise, LeftArmPull);
	}
	if (bRightArmActive && RightArmRaise > 0.01f)
	{
		ApplyArmIK(Context, false, RightHandTarget, RightArmRaise, RightArmPull);
	}

	return true;
}

void FParamotorAnimProxy::ApplyArmIK(FPoseContext& Context, bool bLeftArm, FVector TargetPos, float Raise, float Pull)
{
	USkeletalMeshComponent* SkelMesh = GetSkelMeshComponent();
	if (!SkelMesh) return;
	if (Raise < 0.05f) return;

	// Bone names for MetaHuman skeleton
	const FName UpperArmName = bLeftArm ? TEXT("upperarm_l") : TEXT("upperarm_r");
	const FName LowerArmName = bLeftArm ? TEXT("lowerarm_l") : TEXT("lowerarm_r");
	const FName HandName = bLeftArm ? TEXT("hand_l") : TEXT("hand_r");

	int32 UpperArmMeshIdx = SkelMesh->GetBoneIndex(UpperArmName);
	int32 LowerArmMeshIdx = SkelMesh->GetBoneIndex(LowerArmName);
	int32 HandMeshIdx = SkelMesh->GetBoneIndex(HandName);

	if (UpperArmMeshIdx < 0 || LowerArmMeshIdx < 0 || HandMeshIdx < 0) return;

	const FBoneContainer& BoneContainer = Context.Pose.GetBoneContainer();
	FCompactPoseBoneIndex UpperArmCompact = BoneContainer.MakeCompactPoseIndex(FMeshPoseBoneIndex(UpperArmMeshIdx));
	FCompactPoseBoneIndex LowerArmCompact = BoneContainer.MakeCompactPoseIndex(FMeshPoseBoneIndex(LowerArmMeshIdx));
	FCompactPoseBoneIndex HandCompact = BoneContainer.MakeCompactPoseIndex(FMeshPoseBoneIndex(HandMeshIdx));

	if (!UpperArmCompact.IsValid() || !LowerArmCompact.IsValid() || !HandCompact.IsValid()) return;

	// === STEP 1: DON'T reset to T-pose — keep the animation pose ===
	// The old code reset arm bones to ref pose (T-pose), which caused a jarring
	// disconnect from the running animation and made arms look floppy.
	// Instead, we apply IK directly on top of the current animation pose.
	// This preserves the natural shoulder/arm position from the locomotion animation.

	// === STEP 2: Convert to component space ===
	FCSPose<FCompactPose> CSPose;
	CSPose.InitPose(Context.Pose);

	FTransform CompToWorld = SkelMesh->GetComponentToWorld();
	FTransform UpperArmCS = CSPose.GetComponentSpaceTransform(UpperArmCompact);
	FTransform LowerArmCS = CSPose.GetComponentSpaceTransform(LowerArmCompact);
	FTransform HandCS = CSPose.GetComponentSpaceTransform(HandCompact);

	// World positions
	FVector UpperArmWorld = CompToWorld.TransformPosition(UpperArmCS.GetTranslation());
	FVector LowerArmWorld = CompToWorld.TransformPosition(LowerArmCS.GetTranslation());
	FVector HandWorld = CompToWorld.TransformPosition(HandCS.GetTranslation());

	// Stable actor directions
	AActor* Owner = SkelMesh->GetOwner();
	FVector ActorRight = Owner ? Owner->GetActorRightVector() : SkelMesh->GetRightVector();
	FVector ActorForward = Owner ? Owner->GetActorForwardVector() : SkelMesh->GetForwardVector();
	FVector ActorUp = FVector(0, 0, 1);
	ActorRight.Z = 0.0f; ActorRight = ActorRight.GetSafeNormal2D();
	ActorForward.Z = 0.0f; ActorForward = ActorForward.GetSafeNormal2D();

	// Bone lengths (from current pose)
	float UpperArmLen = (LowerArmWorld - UpperArmWorld).Size();
	float LowerArmLen = (HandWorld - LowerArmWorld).Size();
	float TotalArmLen = UpperArmLen + LowerArmLen;
	float MaxReach = TotalArmLen * 0.95f;

	// === STEP 3: Reach for the ACTUAL brake handle position ===
	// The brake handle world position is passed in as TargetPos.
	// We reach toward it directly, clamped to the arm's max reach.
	// When brakes are pulled, the handle moves down (the wing code handles this),
	// so the hand naturally follows it down.
	float SideSign = bLeftArm ? -1.0f : 1.0f;
	FVector ShoulderPos = UpperArmWorld;

	// The actual brake handle position from the wing
	FVector HandlePos = TargetPos;

	// Clamp to reachable distance from shoulder
	FVector ToHandle = HandlePos - ShoulderPos;
	float DistToHandle = ToHandle.Size();
	if (DistToHandle > MaxReach)
	{
		HandlePos = ShoulderPos + ToHandle.GetSafeNormal() * MaxReach;
		DistToHandle = MaxReach;
	}

	// When not pulling, hands rest at the handle position (up, reaching for it)
	// When pulling, the handle moves down (wing physics), so we just follow it
	// The Pull value is already reflected in the handle position from the wing.
	// We add a small downward offset when pulling for extra visual emphasis.
	float ClampedPull = FMath::Clamp(Pull, 0.0f, 1.0f);
	float SmoothPull = ClampedPull * ClampedPull * (3.0f - 2.0f * ClampedPull);  // Smoothstep
	FVector PullOffset = FVector(0, 0, -SmoothPull * 10.0f);  // Extra 10cm down when fully pulled
	FVector BrakeTarget = HandlePos + PullOffset;

	// Blend from current hand position to brake target based on Raise
	float SmoothRaise = FMath::Clamp(Raise, 0.0f, 1.0f);
	SmoothRaise = SmoothRaise * SmoothRaise * (3.0f - 2.0f * SmoothRaise);  // Smoothstep
	FVector EffectiveTarget = FMath::Lerp(HandWorld, BrakeTarget, SmoothRaise);

	// Clamp to max reach
	FVector ToTarget = EffectiveTarget - UpperArmWorld;
	float DistToTarget = ToTarget.Size();
	if (DistToTarget > MaxReach)
	{
		EffectiveTarget = UpperArmWorld + ToTarget.GetSafeNormal() * MaxReach;
		DistToTarget = MaxReach;
	}

	// === STEP 4: Use UE's built-in TwoBone IK solver ===
	FVector TargetCS = CompToWorld.InverseTransformPosition(EffectiveTarget);

	// Pole vector: elbow points outward and slightly down (natural for reaching up)
	// When hands are up holding brakes, elbows point out to the sides and slightly down
	FVector PoleDir = ActorRight * SideSign;
	FVector PoleDown = FVector(0, 0, -0.5f);   // Elbows point slightly down
	FVector PoleFwd = ActorForward * 0.2f;      // Slightly forward
	FVector JointTargetWorld = UpperArmWorld + (PoleDir + PoleDown + PoleFwd).GetSafeNormal() * 40.0f;
	FVector JointTargetCS = CompToWorld.InverseTransformPosition(JointTargetWorld);

	// Run the IK solver
	AnimationCore::SolveTwoBoneIK(
		UpperArmCS,     // Root (upper arm)
		LowerArmCS,     // Joint (lower arm)
		HandCS,         // End (hand)
		JointTargetCS,  // Joint target (pole vector)
		TargetCS,       // Effector target
		false,          // No stretching
		1.0f,           // Start stretch ratio
		1.1f            // Max stretch scale
	);

	// === STEP 5: Write back ===
	TArray<FBoneTransform> BoneTransforms;
	BoneTransforms.Add(FBoneTransform(UpperArmCompact, UpperArmCS));
	BoneTransforms.Add(FBoneTransform(LowerArmCompact, LowerArmCS));
	BoneTransforms.Add(FBoneTransform(HandCompact, HandCS));

	CSPose.SafeSetCSBoneTransforms(BoneTransforms);

	// Convert back to local space
	FCSPose<FCompactPose>::ConvertComponentPosesToLocalPoses(MoveTemp(CSPose), Context.Pose);
}
