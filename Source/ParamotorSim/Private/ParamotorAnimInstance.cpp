// Copyright Epic Games, Inc. All Rights Reserved.

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
		Proxy.SitAmount = OwningCharacter->SitAmount;
		Proxy.KiteAmount = OwningCharacter->KiteAmount;
		Proxy.AirAmount = OwningCharacter->AirAmount;
		Proxy.AirTuck = OwningCharacter->AirTuck;
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

	// Step 2: Seated flight pose / ground-kiting brace — both run before
	// arm IK so the hands re-solve onto the handles after the torso moves.
	if (SitAmount > 0.01f)
	{
		ApplySitPose(Context, SitAmount);
	}
	if (KiteAmount > 0.01f)
	{
		ApplyKitePose(Context, KiteAmount);
	}
	if (AirAmount > 0.01f)
	{
		ApplyAirPose(Context, AirAmount, AirTuck);
	}

	// Step 3: Apply arm IK for brake handles
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

// ============================================================
// Sit pose — seated-in-harness flight posture
// Folds the thighs up, bends the knees, lets the feet dangle, and leans
// the torso back slightly. Component-space rotations about the
// character's right axis; Sit is smoothed by the character (butt-slide).
// ============================================================

void FParamotorAnimProxy::ApplySitPose(FPoseContext& Context, float Sit)
{
	USkeletalMeshComponent* SkelMesh = GetSkelMeshComponent();
	if (!SkelMesh) return;
	AActor* Owner = SkelMesh->GetOwner();
	const FBoneContainer& BC = Context.Pose.GetBoneContainer();

	FCSPose<FCompactPose> CS;
	CS.InitPose(Context.Pose);

	// Character-right axis in component space.
	const FTransform CompToWorld = SkelMesh->GetComponentToWorld();
	const FVector RightCS = CompToWorld.InverseTransformVectorNoScale(
		Owner ? Owner->GetActorRightVector() : FVector::RightVector).GetSafeNormal();

	// Rotate a transform about an axis through a pivot point (CS space).
	auto RotAbout = [](const FTransform& T, const FVector& Pivot, const FQuat& Q) -> FTransform {
		FTransform R = T;
		R.SetLocation(Pivot + Q.RotateVector(T.GetLocation() - Pivot));
		R.SetRotation(Q * T.GetRotation());
		return R;
	};

	// Degrees at full sit — thigh forward/up, knee folded under, toes dangle.
	const float HipDeg   = -78.0f * Sit;
	const float KneeDeg  =  82.0f * Sit;
	const float AnkleDeg = -18.0f * Sit;
	const float LeanDeg  =  -9.0f * Sit;

	for (int S = 0; S < 2; ++S)
	{
		const TCHAR* Suf = S == 0 ? TEXT("_l") : TEXT("_r");
		FName Names[4] = {
			FName(*FString::Printf(TEXT("thigh%s"), Suf)),
			FName(*FString::Printf(TEXT("calf%s"), Suf)),
			FName(*FString::Printf(TEXT("foot%s"), Suf)),
			FName(*FString::Printf(TEXT("ball%s"), Suf)),
		};
		FCompactPoseBoneIndex Idx[4];
		bool bOk = true;
		for (int B = 0; B < 4; ++B)
		{
			const int32 MeshIdx = SkelMesh->GetBoneIndex(Names[B]);
			if (MeshIdx < 0) { bOk = false; break; }
			Idx[B] = BC.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIdx));
			if (!Idx[B].IsValid()) { bOk = false; break; }
		}
		if (!bOk) continue;

		FTransform T[4];
		for (int B = 0; B < 4; ++B) T[B] = CS.GetComponentSpaceTransform(Idx[B]);

		// Hip swing — rotates the whole leg about the hip joint.
		const FQuat QHip(RightCS, FMath::DegreesToRadians(HipDeg));
		const FVector HipJoint = T[0].GetLocation();
		for (int B = 0; B < 4; ++B) T[B] = RotAbout(T[B], HipJoint, QHip);

		// Knee fold — calf/foot/ball about the (moved) knee joint.
		const FQuat QKnee(RightCS, FMath::DegreesToRadians(KneeDeg));
		const FVector KneeJoint = T[1].GetLocation();
		for (int B = 1; B < 4; ++B) T[B] = RotAbout(T[B], KneeJoint, QKnee);

		// Ankle — feet dangle slightly.
		const FQuat QAnkle(RightCS, FMath::DegreesToRadians(AnkleDeg));
		const FVector AnkleJoint = T[2].GetLocation();
		for (int B = 2; B < 4; ++B) T[B] = RotAbout(T[B], AnkleJoint, QAnkle);

		TArray<FBoneTransform> Out;
		for (int B = 0; B < 4; ++B) Out.Add(FBoneTransform(Idx[B], T[B]));
		CS.SafeSetCSBoneTransforms(Out);
	}

	// Torso lean-back — rotate spine_02 and every descendant about the
	// character-right axis through the spine joint.
	const int32 SpineMeshIdx = SkelMesh->GetBoneIndex(TEXT("spine_02"));
	if (SpineMeshIdx >= 0)
	{
		const FCompactPoseBoneIndex SpineIdx = BC.MakeCompactPoseIndex(FMeshPoseBoneIndex(SpineMeshIdx));
		if (SpineIdx.IsValid())
		{
			const FVector SpineJoint = CS.GetComponentSpaceTransform(SpineIdx).GetLocation();
			const FQuat QLean(RightCS, FMath::DegreesToRadians(LeanDeg));
			const int32 NumBones = BC.GetNumBones();
			TArray<FBoneTransform> Out;
			for (int32 I = 0; I < NumBones; ++I)
			{
				const FCompactPoseBoneIndex CBI(I);
				// Descendant test: walk the parent chain to the root.
				bool bDesc = false;
				for (FCompactPoseBoneIndex P = CBI; P.IsValid(); P = BC.GetParentBoneIndex(P))
				{
					if (P == SpineIdx) { bDesc = true; break; }
				}
				if (bDesc)
					Out.Add(FBoneTransform(CBI, RotAbout(CS.GetComponentSpaceTransform(CBI), SpineJoint, QLean)));
			}
			if (Out.Num()) CS.SafeSetCSBoneTransforms(Out);
		}
	}

	FCSPose<FCompactPose>::ConvertComponentPosesToLocalPoses(MoveTemp(CS), Context.Pose);
}

// ============================================================
// Kite pose — braced stance while the wing is overhead on the ground.
// Torso leans into the lines' pull (forward launch: forward lean,
// shoulders loaded), knees soft. Component-space rotations like the
// sit pose; Kite is smoothed by the character.
// ============================================================

void FParamotorAnimProxy::ApplyKitePose(FPoseContext& Context, float Kite)
{
	USkeletalMeshComponent* SkelMesh = GetSkelMeshComponent();
	if (!SkelMesh) return;
	AActor* Owner = SkelMesh->GetOwner();
	const FBoneContainer& BC = Context.Pose.GetBoneContainer();

	FCSPose<FCompactPose> CS;
	CS.InitPose(Context.Pose);

	const FTransform CompToWorld = SkelMesh->GetComponentToWorld();
	const FVector RightCS = CompToWorld.InverseTransformVectorNoScale(
		Owner ? Owner->GetActorRightVector() : FVector::RightVector).GetSafeNormal();

	auto RotAbout = [](const FTransform& T, const FVector& Pivot, const FQuat& Q) -> FTransform {
		FTransform R = T;
		R.SetLocation(Pivot + Q.RotateVector(T.GetLocation() - Pivot));
		R.SetRotation(Q * T.GetRotation());
		return R;
	};

	// Forward brace — torso pitches INTO the riser pull (opposite the sit
	// lean), knees flex slightly for the athletic stance.
	const float LeanDeg =  14.0f * Kite;
	const float HipDeg  =  -8.0f * Kite;
	const float KneeDeg =  10.0f * Kite;

	for (int S = 0; S < 2; ++S)
	{
		const TCHAR* Suf = S == 0 ? TEXT("_l") : TEXT("_r");
		FName Names[4] = {
			FName(*FString::Printf(TEXT("thigh%s"), Suf)),
			FName(*FString::Printf(TEXT("calf%s"), Suf)),
			FName(*FString::Printf(TEXT("foot%s"), Suf)),
			FName(*FString::Printf(TEXT("ball%s"), Suf)),
		};
		FCompactPoseBoneIndex Idx[4];
		bool bOk = true;
		for (int B = 0; B < 4; ++B)
		{
			const int32 MeshIdx = SkelMesh->GetBoneIndex(Names[B]);
			if (MeshIdx < 0) { bOk = false; break; }
			Idx[B] = BC.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIdx));
			if (!Idx[B].IsValid()) { bOk = false; break; }
		}
		if (!bOk) continue;

		FTransform T[4];
		for (int B = 0; B < 4; ++B) T[B] = CS.GetComponentSpaceTransform(Idx[B]);

		const FQuat QHip(RightCS, FMath::DegreesToRadians(HipDeg));
		const FVector HipJoint = T[0].GetLocation();
		for (int B = 0; B < 4; ++B) T[B] = RotAbout(T[B], HipJoint, QHip);

		const FQuat QKnee(RightCS, FMath::DegreesToRadians(KneeDeg));
		const FVector KneeJoint = T[1].GetLocation();
		for (int B = 1; B < 4; ++B) T[B] = RotAbout(T[B], KneeJoint, QKnee);

		// Feet stay flat — counter-rotate the ankle by hip+knee.
		const FQuat QAnkle(RightCS, FMath::DegreesToRadians(-(HipDeg + KneeDeg)));
		const FVector AnkleJoint = T[2].GetLocation();
		for (int B = 2; B < 4; ++B) T[B] = RotAbout(T[B], AnkleJoint, QAnkle);

		TArray<FBoneTransform> Out;
		for (int B = 0; B < 4; ++B) Out.Add(FBoneTransform(Idx[B], T[B]));
		CS.SafeSetCSBoneTransforms(Out);
	}

	// Torso lean — spine_02 subtree, forward into the pull.
	const int32 SpineMeshIdx = SkelMesh->GetBoneIndex(TEXT("spine_02"));
	if (SpineMeshIdx >= 0)
	{
		const FCompactPoseBoneIndex SpineIdx = BC.MakeCompactPoseIndex(FMeshPoseBoneIndex(SpineMeshIdx));
		if (SpineIdx.IsValid())
		{
			const FVector SpineJoint = CS.GetComponentSpaceTransform(SpineIdx).GetLocation();
			const FQuat QLean(RightCS, FMath::DegreesToRadians(LeanDeg));
			const int32 NumBones = BC.GetNumBones();
			TArray<FBoneTransform> Out;
			for (int32 I = 0; I < NumBones; ++I)
			{
				const FCompactPoseBoneIndex CBI(I);
				bool bDesc = false;
				for (FCompactPoseBoneIndex P = CBI; P.IsValid(); P = BC.GetParentBoneIndex(P))
				{
					if (P == SpineIdx) { bDesc = true; break; }
				}
				if (bDesc)
					Out.Add(FBoneTransform(CBI, RotAbout(CS.GetComponentSpaceTransform(CBI), SpineJoint, QLean)));
			}
			if (Out.Num()) CS.SafeSetCSBoneTransforms(Out);
		}
	}

	FCSPose<FCompactPose>::ConvertComponentPosesToLocalPoses(MoveTemp(CS), Context.Pose);
}

// ============================================================
// Air pose — jump/fall posture when the wing isn't carrying.
// AirTuck follows vertical velocity: rising tucks the knees up,
// falling extends the legs to meet the ground. Arms splay for balance.
// ============================================================

void FParamotorAnimProxy::ApplyAirPose(FPoseContext& Context, float Air, float Tuck)
{
	USkeletalMeshComponent* SkelMesh = GetSkelMeshComponent();
	if (!SkelMesh) return;
	AActor* Owner = SkelMesh->GetOwner();
	const FBoneContainer& BC = Context.Pose.GetBoneContainer();

	FCSPose<FCompactPose> CS;
	CS.InitPose(Context.Pose);

	const FTransform CompToWorld = SkelMesh->GetComponentToWorld();
	const FVector RightCS = CompToWorld.InverseTransformVectorNoScale(
		Owner ? Owner->GetActorRightVector() : FVector::RightVector).GetSafeNormal();
	const FVector FwdCS = CompToWorld.InverseTransformVectorNoScale(
		Owner ? Owner->GetActorForwardVector() : FVector::ForwardVector).GetSafeNormal();

	auto RotAbout = [](const FTransform& T, const FVector& Pivot, const FQuat& Q) -> FTransform {
		FTransform R = T;
		R.SetLocation(Pivot + Q.RotateVector(T.GetLocation() - Pivot));
		R.SetRotation(Q * T.GetRotation());
		return R;
	};

	// Tuck: 1 = knees driven up (ascent), negative = legs extend (descent).
	const float HipDeg   = (-20.0f - 45.0f * Tuck) * Air;
	const float KneeDeg  = ( 25.0f + 55.0f * Tuck) * Air;
	const float AnkleDeg = ( -5.0f + 15.0f * Tuck) * Air;
	const float SplayDeg =  24.0f * Air;
	const float RaiseDeg = -15.0f * Air;   // arms lift slightly forward — hands clear the hips
	const float ElbowDeg = -25.0f * Air;

	for (int S = 0; S < 2; ++S)
	{
		const TCHAR* Suf = S == 0 ? TEXT("_l") : TEXT("_r");
		const float SideSign = S == 0 ? 1.0f : -1.0f;

		FName LegNames[4] = {
			FName(*FString::Printf(TEXT("thigh%s"), Suf)),
			FName(*FString::Printf(TEXT("calf%s"), Suf)),
			FName(*FString::Printf(TEXT("foot%s"), Suf)),
			FName(*FString::Printf(TEXT("ball%s"), Suf)),
		};
		FCompactPoseBoneIndex Leg[4];
		bool bOk = true;
		for (int B = 0; B < 4; ++B)
		{
			const int32 MeshIdx = SkelMesh->GetBoneIndex(LegNames[B]);
			if (MeshIdx < 0) { bOk = false; break; }
			Leg[B] = BC.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIdx));
			if (!Leg[B].IsValid()) { bOk = false; break; }
		}
		if (bOk)
		{
			FTransform T[4];
			for (int B = 0; B < 4; ++B) T[B] = CS.GetComponentSpaceTransform(Leg[B]);

			const FQuat QHip(RightCS, FMath::DegreesToRadians(HipDeg));
			const FVector HipJoint = T[0].GetLocation();
			for (int B = 0; B < 4; ++B) T[B] = RotAbout(T[B], HipJoint, QHip);

			const FQuat QKnee(RightCS, FMath::DegreesToRadians(KneeDeg));
			const FVector KneeJoint = T[1].GetLocation();
			for (int B = 1; B < 4; ++B) T[B] = RotAbout(T[B], KneeJoint, QKnee);

			const FQuat QAnkle(RightCS, FMath::DegreesToRadians(AnkleDeg));
			const FVector AnkleJoint = T[2].GetLocation();
			for (int B = 2; B < 4; ++B) T[B] = RotAbout(T[B], AnkleJoint, QAnkle);

			TArray<FBoneTransform> Out;
			for (int B = 0; B < 4; ++B) Out.Add(FBoneTransform(Leg[B], T[B]));
			CS.SafeSetCSBoneTransforms(Out);
		}

		// Arms splay outward, elbows bent — balance reflex.
		const int32 UpperM = SkelMesh->GetBoneIndex(*FString::Printf(TEXT("upperarm%s"), Suf));
		const int32 LowerM = SkelMesh->GetBoneIndex(*FString::Printf(TEXT("lowerarm%s"), Suf));
		const int32 HandM  = SkelMesh->GetBoneIndex(*FString::Printf(TEXT("hand%s"), Suf));
		if (UpperM >= 0 && LowerM >= 0 && HandM >= 0)
		{
			const FCompactPoseBoneIndex UpperIdx = BC.MakeCompactPoseIndex(FMeshPoseBoneIndex(UpperM));
			const FCompactPoseBoneIndex LowerIdx = BC.MakeCompactPoseIndex(FMeshPoseBoneIndex(LowerM));
			const FCompactPoseBoneIndex HandIdx  = BC.MakeCompactPoseIndex(FMeshPoseBoneIndex(HandM));
			if (UpperIdx.IsValid() && LowerIdx.IsValid() && HandIdx.IsValid())
			{
				FTransform UpperT = CS.GetComponentSpaceTransform(UpperIdx);
				FTransform LowerT = CS.GetComponentSpaceTransform(LowerIdx);
				FTransform HandT  = CS.GetComponentSpaceTransform(HandIdx);

				const FVector ShoulderJoint = UpperT.GetLocation();
				// Splay out + lift forward — hands clear the hips.
				const FQuat QSplay(FwdCS, FMath::DegreesToRadians(SplayDeg * SideSign));
				const FQuat QRaise(RightCS, FMath::DegreesToRadians(RaiseDeg));
				const FQuat QShoulder = QRaise * QSplay;
				UpperT = RotAbout(UpperT, ShoulderJoint, QShoulder);
				LowerT = RotAbout(LowerT, ShoulderJoint, QShoulder);
				HandT  = RotAbout(HandT,  ShoulderJoint, QShoulder);

				const FQuat QElbow(RightCS, FMath::DegreesToRadians(ElbowDeg));
				const FVector ElbowJoint = LowerT.GetLocation();
				LowerT = RotAbout(LowerT, ElbowJoint, QElbow);
				HandT  = RotAbout(HandT,  ElbowJoint, QElbow);

				// Self-clearance: the hand mesh must stay outside the thigh
				// capsule — re-solve the arm IK to a pushed-out wrist target.
				if (bOk)
				{
					const FVector HipJoint  = CS.GetComponentSpaceTransform(Leg[0]).GetLocation();
					const FVector KneeJoint = CS.GetComponentSpaceTransform(Leg[1]).GetLocation();
					const FVector AB = KneeJoint - HipJoint;
					const float Seg2 = AB.SizeSquared();
					FVector WristPos = HandT.GetLocation();
					if (Seg2 > 1.0f)
					{
						const float tSeg = FMath::Clamp(FVector::DotProduct(WristPos - HipJoint, AB) / Seg2, 0.0f, 1.0f);
						const FVector Closest = HipJoint + AB * tSeg;
						FVector Away = WristPos - Closest;
						float Dist = Away.Size();
						const float MinClear = 14.0f;   // wrist-to-thigh skin radius
						if (Dist < MinClear)
						{
							if (Dist < 1.0f) { Away = RightCS * SideSign; Dist = 1.0f; }
							const FVector SafeWrist = Closest + Away * (MinClear / Dist);
							// Pole = elbow target, pushed outward (left arm → -RightCS,
							// right arm → +RightCS) and slightly back.
							const FVector PoleCS = LowerT.GetLocation()
								+ RightCS * (-SideSign) * 40.0f - FwdCS * 10.0f;
							AnimationCore::SolveTwoBoneIK(UpperT, LowerT, HandT, PoleCS, SafeWrist, false, 1.0f, 1.05f);
						}
					}
				}

				TArray<FBoneTransform> Out;
				Out.Add(FBoneTransform(UpperIdx, UpperT));
				Out.Add(FBoneTransform(LowerIdx, LowerT));
				Out.Add(FBoneTransform(HandIdx,  HandT));
				CS.SafeSetCSBoneTransforms(Out);
			}
		}
	}

	FCSPose<FCompactPose>::ConvertComponentPosesToLocalPoses(MoveTemp(CS), Context.Pose);
}
