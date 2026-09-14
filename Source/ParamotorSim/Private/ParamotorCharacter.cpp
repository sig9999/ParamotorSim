// Copyright Epic Games, Inc. All Rights Reserved.

#include "ParamotorCharacter.h"
#include "ParamotorLogging.h"
#include "ParamotorAnimInstance.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/AudioComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/SkeletalMesh.h"
#include "Sound/SoundBase.h"
#include "Kismet/GameplayStatics.h"
#include "ParamotorGameInstance.h"
#include "ParamotorPlayerController.h"
#include "ParamotorActor.h"
#include "WingActor.h"
#include "AtmosphereModel.h"
#include "GroomComponent.h"
#include "GroomAsset.h"
#include "GroomBindingAsset.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "ReferenceSkeleton.h"

AParamotorCharacter::AParamotorCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	static ConstructorHelpers::FObjectFinder<USkeletalMesh> BodyMesh(TEXT("/Game/Frey_Exported/MHC_Frey_FullBody.MHC_Frey_FullBody"));
	if (BodyMesh.Succeeded())
	{
		GetMesh()->SetSkeletalMesh(BodyMesh.Object);
		GetMesh()->SetRelativeLocation(FVector(0.0f, 0.0f, -90.0f));
		GetMesh()->SetRelativeRotation(FRotator(0.0f, -90.0f, 0.0f));
	}

	HeadMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("HeadMesh"));
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> HeadMeshFinder(TEXT("/Game/Frey_Exported/MHC_Frey_Head.MHC_Frey_Head"));
	if (HeadMeshFinder.Succeeded())
	{
		HeadMesh->SetSkeletalMesh(HeadMeshFinder.Object);
		HeadMesh->SetupAttachment(GetMesh());
		HeadMesh->SetRelativeTransform(FTransform::Identity);
		HeadMesh->SetLeaderPoseComponent(GetMesh());
		HeadMesh->SetVisibility(true);
		HeadMesh->SetHiddenInGame(false);
	}

	// === CLOTHING: Techwear outfit (jacket + pants) ===
	// NOTE: The techwear outfit uses UE5.8's Chaos Outfit Asset system, not standard
	// skeletal meshes. We load it dynamically in BeginPlay via the outfit asset system.
	// For now, the outfit mesh component is created but the mesh is loaded at runtime.
	OutfitMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("OutfitMesh"));
	OutfitMesh->SetupAttachment(GetMesh());
	OutfitMesh->SetRelativeTransform(FTransform::Identity);
	OutfitMesh->SetVisibility(true);
	OutfitMesh->SetHiddenInGame(false);
	OutfitMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	OutfitMesh->SetSimulatePhysics(false);

	// === PILOT GEAR COMPONENTS ===
	// Grooms bind to the head mesh (all MetaHuman heads share face-skeleton topology,
	// so the plugin bindings deform correctly onto MHC_Frey_Head).
	HairGroom = CreateDefaultSubobject<UGroomComponent>(TEXT("HairGroom"));
	HairGroom->SetupAttachment(HeadMesh);
	EyebrowGroom = CreateDefaultSubobject<UGroomComponent>(TEXT("EyebrowGroom"));
	EyebrowGroom->SetupAttachment(HeadMesh);
	EyelashGroom = CreateDefaultSubobject<UGroomComponent>(TEXT("EyelashGroom"));
	EyelashGroom->SetupAttachment(HeadMesh);
	MustacheGroom = CreateDefaultSubobject<UGroomComponent>(TEXT("MustacheGroom"));
	MustacheGroom->SetupAttachment(HeadMesh);
	PeachfuzzGroom = CreateDefaultSubobject<UGroomComponent>(TEXT("PeachfuzzGroom"));
	PeachfuzzGroom->SetupAttachment(HeadMesh);

	// PPG helmet + visor — static meshes authored in body-component space;
	// re-attached to the head bone in LoadPilotGear.
	HelmetComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HelmetComp"));
	HelmetComp->SetupAttachment(GetMesh());
	HelmetComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HelmetComp->SetGenerateOverlapEvents(false);
	VisorComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("VisorComp"));
	VisorComp->SetupAttachment(GetMesh());
	VisorComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	VisorComp->SetGenerateOverlapEvents(false);

	// Modular garment overlays — leader-posed to the body in LoadPilotGear.
	ShirtMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("ShirtMesh"));
	ShirtMesh->SetupAttachment(GetMesh());
	ShirtMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ShirtMesh->SetVisibility(false);
	PantsMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("PantsMesh"));
	PantsMesh->SetupAttachment(GetMesh());
	PantsMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PantsMesh->SetVisibility(false);
	BootsMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("BootsMesh"));
	BootsMesh->SetupAttachment(GetMesh());
	BootsMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BootsMesh->SetVisibility(false);

	// Leave animation asset loading for BeginPlay so we can load the original plugin clips.

	GetCharacterMovement()->MaxWalkSpeed = 140.0f;  // 1.4 m/s = 3.1 mph (normal walking pace)
	GetCharacterMovement()->MaxAcceleration = 600.0f;  // From leg force F=ma, ~600cm/s²
	GetCharacterMovement()->BrakingDecelerationWalking = 200.0f;  // From shoe friction
	GetCharacterMovement()->JumpZVelocity = 300.0f;  // ~30cm jump (paramotor gear)
	GetCharacterMovement()->AirControl = 0.05f;  // Limited air control with gear
	GetCharacterMovement()->bOrientRotationToMovement = false;
	GetCharacterMovement()->bIgnoreBaseRotation = true; // Never tilt with floor
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 180.0f, 0.0f);  // Slower rotation with gear
	bUseControllerRotationYaw = true;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;
	GetMesh()->SetAnimationMode(EAnimationMode::AnimationBlueprint);

	// CRITICAL: Disable all physics on the mesh so the paramotor's weight
	// can't tilt the body. MetaHuman meshes have physics assets that can
	// cause the mesh to swing/tilt when a heavy child actor is attached.
	GetMesh()->SetSimulatePhysics(false);
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GetMesh()->bBlendPhysics = false;

	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(RootComponent);
	SpringArm->SetRelativeLocation(FVector(0.0f, 0.0f, 70.0f));
	SpringArm->SetRelativeRotation(FRotator(-15.0f, 90.0f, 0.0f));
	SpringArm->TargetArmLength = 250.0f;
	SpringArm->bUsePawnControlRotation = true;
	SpringArm->bInheritPitch = true;
	SpringArm->bInheritYaw = true;
	SpringArm->bInheritRoll = true;
	SpringArm->bDoCollisionTest = false;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SpringArm);
	Camera->bUsePawnControlRotation = false;

	// Footstep audio component
	FootstepAudio = CreateDefaultSubobject<UAudioComponent>(TEXT("FootstepAudio"));
	FootstepAudio->SetupAttachment(RootComponent);
	FootstepAudio->bAutoActivate = false;
	FootstepAudio->SetRelativeLocation(FVector(0.0f, 0.0f, -90.0f));

	// Wind ambient audio component (attached to player, plays continuously)
	WindAmbientAudio = CreateDefaultSubobject<UAudioComponent>(TEXT("WindAmbientAudio"));
	WindAmbientAudio->SetupAttachment(RootComponent);
	WindAmbientAudio->bAutoActivate = false;

	// Fall audio
	FallAudio = CreateDefaultSubobject<UAudioComponent>(TEXT("FallAudio"));
	FallAudio->SetupAttachment(RootComponent);
	FallAudio->bAutoActivate = false;
	FallAudio->SetRelativeLocation(FVector(0.0f, 0.0f, 0.0f));
}

void AParamotorCharacter::BeginPlay()
{
	Super::BeginPlay();

	// CRITICAL: Ensure mesh physics is OFF (MetaHuman physics asset can cause tilt)
	GetMesh()->SetSimulatePhysics(false);
	GetMesh()->bBlendPhysics = false;

	LoadCharacterMesh();
	LoadAnimations();
	ApplySkinTone();
	LoadPilotGear();
	LoadFootstepSounds();
	LoadAmbientAudio();
}

void AParamotorCharacter::LoadCharacterMesh()
{
	// Replace body mesh with techwear CombinedSkelMesh (body + jacket + pants + shoes)
	USkeletalMesh* ClothedBody = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Techwear/techwearOutfit/Meshes/bodyShapeG_CombinedSkelMesh.bodyShapeG_CombinedSkelMesh"));
	if (!ClothedBody)
		ClothedBody = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Techwear/techwearOutfit/Meshes/bodyShapeH_CombinedSkelMesh.bodyShapeH_CombinedSkelMesh"));
	if (ClothedBody)
	{
		GetMesh()->SetSkeletalMesh(ClothedBody);
		// The combined mesh's LOD1+ sections have remapped material indices
		// (clothing collapses to the skin slot) — force LOD0 so the jacket,
		// pants and shoes render with their real materials.
		GetMesh()->SetForcedLOD(1);
		// Clothing sections can arrive disabled from the Chaos outfit export —
		// explicitly un-hide every material section at every LOD.
		const int32 NumLODs = FMath::Max(1, GetMesh()->GetNumLODs());
		for (int32 LOD = 0; LOD < NumLODs; ++LOD)
			GetMesh()->ShowAllMaterialSections(LOD);
		UE_LOG(LogParamotor, Log, TEXT("ParamotorCharacter: Replaced body mesh with techwear CombinedSkelMesh: %s (LODs=%d)"), *ClothedBody->GetName(), NumLODs);
		if (OutfitMesh) OutfitMesh->SetVisibility(false);
	}
	else
	{
		UE_LOG(LogParamotor, Log, TEXT("ParamotorCharacter: Techwear CombinedSkelMesh not found"));
	}
}

void AParamotorCharacter::LoadAnimations()
{
	if (!WalkIdleAnim)
		WalkIdleAnim = LoadObject<UAnimSequence>(nullptr, TEXT("/MetaHumanCharacter/Optional/Animation/UEFNAnimPreset/Locomotion/AS_MH_Neutral_Stand_Idle_Loop.AS_MH_Neutral_Stand_Idle_Loop"));
	if (!WalkAnimAsset)
		WalkAnimAsset = LoadObject<UAnimSequence>(nullptr, TEXT("/MetaHumanCharacter/Optional/Animation/UEFNAnimPreset/Locomotion/AS_MH_Neutral_Walk_Loop_F.AS_MH_Neutral_Walk_Loop_F"));
	if (!JogAnimAsset)
		JogAnimAsset = LoadObject<UAnimSequence>(nullptr, TEXT("/MetaHumanCharacter/Optional/Animation/UEFNAnimPreset/Locomotion/AS_MH_Neutral_Run_Loop_F.AS_MH_Neutral_Run_Loop_F"));

	// Directional locomotion — strafe/back clips so sidestepping doesn't
	// play the forward walk.
	if (!WalkBackAnimAsset)
		WalkBackAnimAsset = LoadObject<UAnimSequence>(nullptr, TEXT("/MetaHumanCharacter/Optional/Animation/UEFNAnimPreset/Locomotion/AS_MH_Neutral_Walk_Loop_B.AS_MH_Neutral_Walk_Loop_B"));
	if (!WalkLeftAnimAsset)
		WalkLeftAnimAsset = LoadObject<UAnimSequence>(nullptr, TEXT("/MetaHumanCharacter/Optional/Animation/UEFNAnimPreset/Locomotion/AS_MH_Neutral_Walk_Loop_LR.AS_MH_Neutral_Walk_Loop_LR"));
	if (!WalkRightAnimAsset)
		WalkRightAnimAsset = LoadObject<UAnimSequence>(nullptr, TEXT("/MetaHumanCharacter/Optional/Animation/UEFNAnimPreset/Locomotion/AS_MH_Neutral_Walk_Loop_RL.AS_MH_Neutral_Walk_Loop_RL"));
	if (!JogBackAnimAsset)
		JogBackAnimAsset = LoadObject<UAnimSequence>(nullptr, TEXT("/MetaHumanCharacter/Optional/Animation/UEFNAnimPreset/Locomotion/AS_MH_Neutral_Run_Loop_B.AS_MH_Neutral_Run_Loop_B"));
	if (!JogLeftAnimAsset)
		JogLeftAnimAsset = LoadObject<UAnimSequence>(nullptr, TEXT("/MetaHumanCharacter/Optional/Animation/UEFNAnimPreset/Locomotion/AS_MH_Neutral_Run_Loop_LR.AS_MH_Neutral_Run_Loop_LR"));
	if (!JogRightAnimAsset)
		JogRightAnimAsset = LoadObject<UAnimSequence>(nullptr, TEXT("/MetaHumanCharacter/Optional/Animation/UEFNAnimPreset/Locomotion/AS_MH_Neutral_Run_Loop_RL.AS_MH_Neutral_Run_Loop_RL"));

	UE_LOG(LogParamotor, Log, TEXT("BeginPlay: Idle=%p Walk=%p Jog=%p StrafeL=%p StrafeR=%p"),
		(void*)WalkIdleAnim, (void*)WalkAnimAsset, (void*)JogAnimAsset, (void*)WalkLeftAnimAsset, (void*)WalkRightAnimAsset);

	GetMesh()->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	GetMesh()->SetAnimInstanceClass(UParamotorAnimInstance::StaticClass());

	if (UParamotorAnimInstance* ParamotorAnim = Cast<UParamotorAnimInstance>(GetMesh()->GetAnimInstance()))
	{
		if (WalkAnimAsset)
		{
			ParamotorAnim->PlaySequence(WalkAnimAsset, true);
			CurrentAnim = WalkAnimAsset;
			UE_LOG(LogParamotor, Log, TEXT("BeginPlay: ParamotorAnim playing walk"));
		}
	}
	else
	{
		UE_LOG(LogParamotor, Error, TEXT("BeginPlay: Failed to get ParamotorAnimInstance"));
	}
}

void AParamotorCharacter::ApplySkinTone()
{
	const FLinearColor SkinColor(0.24f, 0.15f, 0.10f, 1.0f);

	if (HeadMesh)
	{
		HeadMesh->SetLeaderPoseComponent(GetMesh());
		HeadMesh->SetVisibility(true);
		HeadMesh->SetHiddenInGame(false);

		HeadMesh->SetMaterial(1, LoadObject<UMaterialInterface>(nullptr, TEXT("/MetaHumanCharacter/Materials/MI_Teeth_MHC_UI.MI_Teeth_MHC_UI")));
		HeadMesh->SetMaterial(3, LoadObject<UMaterialInterface>(nullptr, TEXT("/MetaHumanCharacter/Materials/MI_Eye_Left_MHC.MI_Eye_Left_MHC")));
		HeadMesh->SetMaterial(4, LoadObject<UMaterialInterface>(nullptr, TEXT("/MetaHumanCharacter/Materials/MI_Eye_Right_MHC.MI_Eye_Right_MHC")));
		for (int32 i = 5; i < HeadMesh->GetNumMaterials(); ++i)
		{
			HeadMesh->SetMaterial(i, LoadObject<UMaterialInterface>(nullptr, TEXT("/MetaHumanCharacter/Materials/M_Hide.M_Hide")));
		}

		UMaterialInterface* HeadSkinMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/MetaHumanCharacter/Materials/MI_M2M_Head.MI_M2M_Head"));
		if (HeadSkinMat)
		{
			UMaterialInstanceDynamic* HeadSkin = UMaterialInstanceDynamic::Create(HeadSkinMat, this);
			HeadSkin->SetVectorParameterValue(TEXT("BaseColor"), SkinColor);
			HeadSkin->SetScalarParameterValue(TEXT("Use BaseColor Map"), 0.0f);
			HeadMesh->SetMaterial(0, HeadSkin);
		}
	}

	UMaterialInterface* BodySkinMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/MetaHumanCharacter/Materials/MI_M2M_Body.MI_M2M_Body"));
	if (BodySkinMat)
	{
		UMaterialInstanceDynamic* BodySkin = UMaterialInstanceDynamic::Create(BodySkinMat, this);
		BodySkin->SetVectorParameterValue(TEXT("BaseColor"), SkinColor);
		BodySkin->SetScalarParameterValue(TEXT("Use BaseColor Map"), 0.0f);

		// Only apply skin material to slot 0 if using clothed body (CombinedSkelMesh)
		int32 SkinSlotCount = GetMesh()->GetNumMaterials();
		USkeletalMesh* CurrentMesh = GetMesh()->GetSkeletalMeshAsset();
		if (CurrentMesh && CurrentMesh->GetName().Contains(TEXT("CombinedSkelMesh")))
		{
			SkinSlotCount = 1;
			UE_LOG(LogParamotor, Log, TEXT("ParamotorCharacter: Using clothed body — only overriding slot 0 (skin)"));
		}
		for (int32 i = 0; i < SkinSlotCount; ++i)
		{
			GetMesh()->SetMaterial(i, BodySkin);
		}
	}
}

// Component-space rest-pose transform of a named bone (local chain accumulated up to root).
static FTransform GetRefBoneComponentTransform(USkeletalMeshComponent* Skel, FName BoneName)
{
	USkeletalMesh* Mesh = Skel ? Skel->GetSkeletalMeshAsset() : nullptr;
	if (!Mesh) return FTransform::Identity;
	const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
	const int32 BoneIdx = Ref.FindBoneIndex(BoneName);
	if (BoneIdx == INDEX_NONE) return FTransform::Identity;
	FTransform CS = Ref.GetRefBonePose()[BoneIdx];
	for (int32 P = Ref.GetParentIndex(BoneIdx); P != INDEX_NONE; P = Ref.GetParentIndex(P))
		CS = CS * Ref.GetRefBonePose()[P];
	return CS;
}

void AParamotorCharacter::LoadPilotGear()
{
	// === GROOMS — hair + face set, bound to the head mesh ===
	struct FGroomDef { UGroomComponent* Comp; const TCHAR* Groom; const TCHAR* Binding; };
	const FGroomDef Grooms[] = {
		{ HairGroom,      TEXT("/MetaHumanCharacter/Optional/Grooms/GroomAssets/Hair/Hair_S_BrushCut/Hair_S_BrushCut.Hair_S_BrushCut"),
		                  TEXT("/MetaHumanCharacter/Optional/Grooms/Bindings/Hair/Hair_S_BrushCut.Hair_S_BrushCut") },
		{ EyebrowGroom,   TEXT("/MetaHumanCharacter/Optional/Grooms/GroomAssets/Eyebrows/Eyebrows_M_Natural/Eyebrows_M_Natural.Eyebrows_M_Natural"),
		                  TEXT("/MetaHumanCharacter/Optional/Grooms/Bindings/Eyebrows/Eyebrows_M_Natural_Binding.Eyebrows_M_Natural_Binding") },
		{ EyelashGroom,   TEXT("/MetaHumanCharacter/Optional/Grooms/GroomAssets/Eyelashes/Eyelashes_S_Sparse/Eyelashes_S_Sparse.Eyelashes_S_Sparse"),
		                  TEXT("/MetaHumanCharacter/Optional/Grooms/Bindings/Eyelashes/Eyelashes_S_Sparse_Binding.Eyelashes_S_Sparse_Binding") },
		{ MustacheGroom,  TEXT("/MetaHumanCharacter/Optional/Grooms/GroomAssets/Mustaches/Mustache_S_Stubble/Mustache_S_Stubble.Mustache_S_Stubble"),
		                  TEXT("/MetaHumanCharacter/Optional/Grooms/Bindings/Mustaches/Mustache_S_Stubble_Binding.Mustache_S_Stubble_Binding") },
		{ PeachfuzzGroom, TEXT("/MetaHumanCharacter/Optional/Grooms/GroomAssets/Peachfuzz/Peachfuzz_M_Thin/Peachfuzz_M_Thin.Peachfuzz_M_Thin"),
		                  TEXT("/MetaHumanCharacter/Optional/Grooms/Bindings/Peachfuzz/Peachfuzz_M_Thin_Binding.Peachfuzz_M_Thin_Binding") },
	};
	for (const FGroomDef& G : Grooms)
	{
		if (!G.Comp) continue;
		UGroomAsset* GA = LoadObject<UGroomAsset>(nullptr, G.Groom);
		UGroomBindingAsset* GB = LoadObject<UGroomBindingAsset>(nullptr, G.Binding);
		if (GA)
		{
			G.Comp->SetGroomAsset(GA);
			if (GB) G.Comp->SetBindingAsset(GB);
			G.Comp->SetVisibility(true);
		}
		else
		{
			G.Comp->SetVisibility(false);
			UE_LOG(LogParamotor, Log, TEXT("LoadPilotGear: groom missing — %s"), G.Groom);
		}
	}

	// === PPG HELMET — mesh authored in body-component space; attach to the head
	// bone with the ref-pose inverse so the mesh lands exactly where authored ===
	const FTransform HeadRestInv = GetRefBoneComponentTransform(GetMesh(), TEXT("head")).Inverse();
	if (UStaticMesh* Helm = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Characters/PilotMale/Headgear/PPGHelmet/SM_PPGHelmet.SM_PPGHelmet")))
	{
		HelmetComp->SetStaticMesh(Helm);
		HelmetComp->AttachToComponent(GetMesh(), FAttachmentTransformRules::KeepRelativeTransform, TEXT("head"));
		HelmetComp->SetRelativeTransform(HeadRestInv);
	}
	else
	{
		UE_LOG(LogParamotor, Log, TEXT("LoadPilotGear: SM_PPGHelmet not built yet"));
	}
	if (UStaticMesh* Visor = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Characters/PilotMale/Headgear/PPGHelmet/SM_PPGVisor.SM_PPGVisor")))
	{
		VisorComp->SetStaticMesh(Visor);
		VisorComp->AttachToComponent(GetMesh(), FAttachmentTransformRules::KeepRelativeTransform, TEXT("head"));
		SetVisorUp(bVisorUp); // applies HeadRestInv * pivot rotation
	}
	// Helmet-on hides the hair groom (brush cut is short; the shell covers the
	// whole cap — hiding prevents any strand clipping at the rim).
	SetHelmetOn(bHelmetOn);

	// === MODULAR GARMENTS — custom skinned overlays; replace techwear sections ===
	auto TryGarment = [this](USkeletalMeshComponent* Comp, const TCHAR* Path) -> bool
	{
		USkeletalMesh* M = LoadObject<USkeletalMesh>(nullptr, Path);
		if (!M) return false;
		Comp->SetSkeletalMesh(M);
		Comp->SetLeaderPoseComponent(GetMesh());
		for (int32 LOD = 0; LOD < Comp->GetNumLODs(); ++LOD)
			Comp->ShowAllMaterialSections(LOD);
		Comp->SetVisibility(true);
		return true;
	};
	const bool bShirt = TryGarment(ShirtMesh, TEXT("/Game/Characters/PilotMale/Clothing/Shirt/SK_Pilot_Shirt.SK_Pilot_Shirt"));
	const bool bPants = TryGarment(PantsMesh, TEXT("/Game/Characters/PilotMale/Clothing/Pants/SK_Pilot_Pants.SK_Pilot_Pants"));
	const bool bBoots = TryGarment(BootsMesh, TEXT("/Game/Characters/PilotMale/Clothing/Boots/SK_Pilot_Boots.SK_Pilot_Boots"));
	UE_LOG(LogParamotor, Log, TEXT("LoadPilotGear: custom garments shirt=%d pants=%d boots=%d"), bShirt, bPants, bBoots);
}

void AParamotorCharacter::SetHelmetOn(bool bOn)
{
	bHelmetOn = bOn;
	if (HelmetComp) HelmetComp->SetVisibility(bOn);
	if (VisorComp)  VisorComp->SetVisibility(bOn);
	if (HairGroom)  HairGroom->SetVisibility(!bOn);
}

void AParamotorCharacter::SetVisorUp(bool bUp)
{
	bVisorUp = bUp;
	if (!VisorComp || !VisorComp->GetStaticMesh()) return;
	const FTransform HeadRestInv = GetRefBoneComponentTransform(GetMesh(), TEXT("head")).Inverse();
	FTransform Rel = HeadRestInv;
	if (bUp)
	{
		// Rotate the visor +75 deg about the temple pivot axis (component-space X).
		const FVector Pivot(0.0f, 1.2f, 161.0f);
		const FTransform AboutPivot =
			FTransform(FRotator::ZeroRotator, Pivot)
			* FTransform(FRotator(0.0f, 0.0f, 75.0f))
			* FTransform(FRotator::ZeroRotator, -Pivot);
		Rel = HeadRestInv * AboutPivot;
	}
	VisorComp->SetRelativeTransform(Rel);
}


void AParamotorCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);


	UCharacterMovementComponent* CMC = GetCharacterMovement();

	// PsFlyTo leaves the pawn in MOVE_Flying with no gravity so it can hover.
	// Restore normal walking the moment it reaches the ground — otherwise WASD
	// drives MaxFlySpeed (4x walk speed) instead of MaxWalkSpeed.
	if (CMC->MovementMode == MOVE_Flying)
	{
		FHitResult FloorHit;
		FCollisionQueryParams FloorQ;
		FloorQ.AddIgnoredActor(this);
		const FVector From = GetActorLocation();
		if (GetWorld()->LineTraceSingleByChannel(FloorHit, From, From - FVector(0.0f, 0.0f, 220.0f), ECC_Visibility, FloorQ))
		{
			CMC->GravityScale = 1.0f;
			CMC->Velocity = FVector::ZeroVector;
			CMC->SetMovementMode(MOVE_Walking);
			UE_LOG(LogParamotor, Log, TEXT("ParamotorCharacter: fly-mode landing — restored walking"));
		}
	}

	// === INJURY HEALING ===
	HealInjury(DeltaTime);
	float EffectiveMaxSpeed = CurrentMaxSpeed * InjuryMaxSpeedMultiplier;

	// === Fall handling — early out when fallen ===
	if (UpdateFallState(DeltaTime, CMC, EffectiveMaxSpeed))
		return;

	// === Force actor rotation to match controller yaw ===
	if (!bIsFallen && GetController())
	{
		if (bWeightShiftActive)
		{
			// Flying: WingActor controls pawn rotation (bank, pitch, yaw).
		}
		else
		{
			FRotator ControlRot = GetControlRotation();
			SetActorRotation(FRotator(0.0f, ControlRot.Yaw, 0.0f));
		}
	}

	// Shift now gradually pushes the character into a jog; there is no separate sprint.
	const float TargetMax = bSprintRequested ? 270.0f : 140.0f;  // Walk 3.1 mph, Jog 6.0 mph
	CurrentMaxSpeed = FMath::FInterpTo(CurrentMaxSpeed, TargetMax, DeltaTime, 2.0f);

	bool bOnGround = CMC->IsMovingOnGround();
	bool bThrustActive = !bIsFallen && CurrentThrust > 0.01f;
	AParamotorPlayerController* PC = Cast<AParamotorPlayerController>(GetController());

	auto IsWingFlying = [PC]() -> bool {
		return (PC && PC->SpawnedWingActor && PC->SpawnedWingActor->IsFlying());
	};

	// Air time tracking (instance members — was static, shared between instances)
	if (bThrustActive && !bOnGround && !IsWingFlying())
	{
		AirTimeAccum += DeltaTime;
		float GZ = 0.0f;
		FVector TraceStart = GetActorLocation();
		FHitResult Hit;
		FCollisionQueryParams Q;
		Q.bTraceComplex = false;
		if (GetWorld()->LineTraceSingleByChannel(Hit, TraceStart, TraceStart - FVector(0, 0, 500), ECC_Visibility, Q))
			GZ = Hit.Location.Z;
		else
			GZ = TraceStart.Z - 200.0f;
		float HeightAboveGround = TraceStart.Z - GZ;
		if (HeightAboveGround > MaxAirHeight) MaxAirHeight = HeightAboveGround;
		if (AirTimeAccum > 1.0f && MaxAirHeight > 200.0f)
			bWasInAirWithThrust = true;
	}
	else if (bOnGround)
	{
		AirTimeAccum = 0.0f;
		MaxAirHeight = 0.0f;
	}

	// Landed after being in air with thrust → trip
	if (bThrustActive && bOnGround && bWasInAirWithThrust)
	{
		bWasInAirWithThrust = false;
		AirTimeAccum = 0.0f;
		MaxAirHeight = 0.0f;
		if (!IsWingFlying())
		{
			UE_LOG(LogParamotor, Log, TEXT("ParamotorCharacter: Landed after air-thrust — TRIPPED!"));
			Trip();
		}
	}

	// === Thrust physics ===
	UpdateThrustGroundPhysics(DeltaTime, CMC, EffectiveMaxSpeed, bOnGround, bThrustActive, PC);

	bool bWingFlying = IsWingFlying();
	UpdateThrustAirPhysics(DeltaTime, CMC, bThrustActive, bOnGround, bWingFlying, PC);

	// === Posture lean ===
	UpdatePostureLean(DeltaTime, CMC, PC);

	// === Animation ===
	UpdateAnimationState(DeltaTime, CMC, PC);

	// === Brake arm IK ===
	UpdateBrakeArmIK(DeltaTime);

	// === Audio ===
	UpdateFootstepAudio(DeltaTime, CMC);
	UpdateWindAudio();

	// === Trip risk ===
	UpdateTripRisk(DeltaTime, CMC, PC);

	// === Rotation handling — last thing in Tick ===
	UpdateRotationHandling(DeltaTime);
}

// ============================================================
// TICK SPLIT HELPERS
// ============================================================


float AParamotorCharacter::GetEquippedThrustN()
{
	AParamotorPlayerController* PC = Cast<AParamotorPlayerController>(GetController());
	if (PC && PC->GetSpawnedParamotorActor() && PC->GetSpawnedParamotorActor()->IsEquipped())
	{
		float AirspeedMs = GetVelocity().Size() / CM_PER_M;
		return PC->GetSpawnedParamotorActor()->GetThrustForceN(AirspeedMs);
	}
	return 0.0f;
}

void AParamotorCharacter::UpdateThrustGroundPhysics(float DeltaTime, UCharacterMovementComponent* CMC, float EffectiveMaxSpeed, bool bOnGround, bool bThrustActive, AParamotorPlayerController* PC)
{
	if (!CMC) return;

	auto IsWingFlying = [PC]() -> bool {
		return (PC && PC->SpawnedWingActor && PC->SpawnedWingActor->IsFlying());
	};

	// === WIND DRAG ON INFLATED WING (knockdown risk only) ===
	if (bOnGround && !bIsFallen && !IsWingFlying())
	{
		bool bWingInflated = false;
		if (PC && PC->SpawnedWingActor)
		{
			EWingState WState = PC->SpawnedWingActor->GetWingState();
			bWingInflated = (WState == EWingState::Inflated);
		}
		if (bWingInflated)
		{
			float WindMs = 0.0f;
			if (UParamotorGameInstance* GI = GetWorld()->GetGameInstance<UParamotorGameInstance>())
				WindMs = GI->WindSpeed;
			if (!bThrustActive && WindMs > 12.0f)
			{
				TripRiskAccumulator += (WindMs - 12.0f) * 0.5f * DeltaTime;
			}
		}
	}

	// Use the actual movement input (WASD) or existing velocity to decide if the pilot is running.
	// The old check used CMC->GetCurrentAcceleration(), which is ~0 when running at a steady speed,
	// causing the character to be treated as "standing still" and stopped by thrust.
	bool bHasMovementInput = (CMC->GetLastInputVector().Size2D() > 0.1f) || (CMC->Velocity.Size2D() > 20.0f);

	if (bThrustActive && bOnGround)
	{
		if (bHasMovementInput)
		{
			ApplyThrustRunningBoost(DeltaTime, CMC, PC);
		}
		else
		{
			// === STANDING STILL + THRUST: feet planted, NO movement ===
			CMC->MaxWalkSpeed = EffectiveMaxSpeed;
			CMC->BrakingDecelerationWalking = 300.0f;
			CMC->GroundFriction = 8.0f;
		}
	}
	else if (!bThrustActive)
	{
		// No thrust: normal movement
		bWasInAirWithThrust = false;
		CMC->MaxWalkSpeed = EffectiveMaxSpeed;
		CMC->BrakingDecelerationWalking = 300.0f;
		CMC->GroundFriction = 8.0f;
	}
}

void AParamotorCharacter::ApplyThrustRunningBoost(float DeltaTime, UCharacterMovementComponent* CMC, AParamotorPlayerController* PC)
{
	// === WALKING + THRUST: boost forward speed ===
	float PilotLegPowerW = 350.0f;
	float CharMassForRun = CMC->Mass;
	if (CharMassForRun <= 0.0f) CharMassForRun = 100.0f;
	float TotalMass = CharMassForRun + 7.5f;
	float RollingMu = 0.4f;
	float RollingResistanceN = RollingMu * TotalMass * GRAVITY_MS2;
	float MaxRunSpeedMs = (RollingResistanceN > 1.0f) ? PilotLegPowerW / RollingResistanceN : 8.0f;
	MaxRunSpeedMs = FMath::Clamp(MaxRunSpeedMs, 1.5f, 12.0f);
	const float MaxRunSpeed = MaxRunSpeedMs * CM_PER_M;

	CMC->MaxWalkSpeed = MaxRunSpeed;
	CMC->BrakingDecelerationWalking = 100.0f;
	CMC->GroundFriction = 4.0f;

	float ThrustForceN = GetEquippedThrustN();
	float ThrustForceUE = ThrustForceN * CM_PER_M;
	float CharMass = CMC->Mass;
	if (CharMass <= 0.0f) CharMass = 100.0f;
	float Accel = ThrustForceUE / CharMass;

	FVector Forward = GetActorForwardVector();
	Forward.Z = 0.0f;
	Forward = Forward.GetSafeNormal2D();

	FVector ThrustVel = Forward * Accel * DeltaTime;
	FVector CurrentVel = GetVelocity();
	FVector NewVel = CurrentVel + ThrustVel;

	float EffectiveMaxRun = MaxRunSpeed;
	if (PC && PC->SpawnedWingActor)
	{
		EWingState WState = PC->SpawnedWingActor->GetWingState();
		if (WState == EWingState::Inflated)
		{
			float WingHelp = PC->SpawnedWingActor->GetDebugWeightSupport();
			EffectiveMaxRun = MaxRunSpeed / FMath::Sqrt(FMath::Max(0.1f, 1.0f - WingHelp));
		}
	}

	FVector HorizVel = FVector(NewVel.X, NewVel.Y, 0.0f);
	if (HorizVel.Size() > EffectiveMaxRun)
	{
		FVector Clamped = HorizVel.GetSafeNormal() * EffectiveMaxRun;
		NewVel = FVector(Clamped.X, Clamped.Y, CurrentVel.Z);
	}

	if (!(PC && PC->SpawnedWingActor && PC->SpawnedWingActor->GetWingState() == EWingState::Inflated))
	{
		NewVel.Z = CurrentVel.Z;
	}
	CMC->Velocity = NewVel;
}

void AParamotorCharacter::UpdateThrustAirPhysics(float DeltaTime, UCharacterMovementComponent* CMC, bool bThrustActive, bool bOnGround, bool bWingFlying, AParamotorPlayerController* PC)
{
	if (!CMC) return;

	bool bIsAirborne = !bOnGround || bWingFlying;
	if (bThrustActive && bIsAirborne && !bIsFallen && !bWingFlying)
	{
		float ThrustForceN = GetEquippedThrustN();
		float ThrustForceUE = ThrustForceN * CM_PER_M;
		float CharMass = CMC->Mass;
		if (CharMass <= 0.0f) CharMass = 100.0f;
		float Accel = ThrustForceUE / CharMass;

		FVector Forward = GetActorForwardVector();
		Forward.Z = 0.0f;
		Forward.Normalize();

		FVector ThrustVel = Forward * Accel * DeltaTime;
		FVector CurrentVel = GetVelocity();
		FVector NewVel = CurrentVel + ThrustVel;
		NewVel.Z = CurrentVel.Z;
		CMC->Velocity = NewVel;
	}
}

void AParamotorCharacter::UpdatePostureLean(float DeltaTime, UCharacterMovementComponent* CMC, AParamotorPlayerController* PC)
{
	if (!CMC) return;

	// Real forward-lean from thrust will come from animation/physics later.
	// For now, just ensure the mesh stays at base rotation when not fallen/weight-shifting.
	if (!bIsFallen && !bWeightShiftActive)
	{
		FQuat BaseQuat(FVector::UpVector, FMath::DegreesToRadians(-90.0f));
		GetMesh()->SetRelativeRotation(BaseQuat);
	}
}

void AParamotorCharacter::UpdateAnimationState(float DeltaTime, UCharacterMovementComponent* CMC, AParamotorPlayerController* PC)
{
	const float Speed = GetVelocity().Size();
	SmoothedAnimSpeed = FMath::FInterpTo(SmoothedAnimSpeed, Speed, DeltaTime, 6.0f);

	// Seated blend — the "butt slide" into the harness when the wing carries.
	const bool bFlying = PC && PC->SpawnedWingActor && PC->SpawnedWingActor->IsFlying();
	SitAmount = FMath::FInterpTo(SitAmount, bFlying ? 1.0f : 0.0f, DeltaTime, 2.5f);

	// Kiting blend — connected with the wing pressurized overhead on the
	// ground: brace into the lines' pull. Slower in than out (pilots settle
	// into the load, but drop the brace instantly when the wing drops).
	const bool bKiting = !bFlying && PC && PC->SpawnedWingActor
		&& PC->SpawnedWingActor->IsConnected()
		&& PC->SpawnedWingActor->GetWingState() == EWingState::Inflated;
	KiteAmount = FMath::FInterpTo(KiteAmount, bKiting ? 1.0f : 0.0f, DeltaTime, bKiting ? 1.8f : 5.0f);

	// Airborne (jump/fall, no wing lift) — tuck on the way up, extend on
	// the way down to absorb the landing.
	const bool bInAir = CMC && CMC->MovementMode == MOVE_Falling && !bFlying && !bIsFallen;
	AirAmount = FMath::FInterpTo(AirAmount, bInAir ? 1.0f : 0.0f, DeltaTime, 8.0f);
	const float VZ = GetVelocity().Z;
	AirTuck = FMath::FInterpTo(AirTuck, bInAir ? FMath::Clamp(VZ / 250.0f, -0.5f, 1.0f) : 0.0f, DeltaTime, 10.0f);

	// Footprints are owned by AParamotorSandSystem — it reads foot_l/foot_r
	// bone contacts itself and stamps the shared deform field.

	UParamotorAnimInstance* ParamotorAnim = Cast<UParamotorAnimInstance>(GetMesh()->GetAnimInstance());
	if (!ParamotorAnim)
		return;

	float DesiredPlayRate = 1.0f;

	// Direction of travel in body space — pick the strafe/back clip that
	// matches instead of always playing the forward walk.
	UAnimSequence* Desired = WalkIdleAnim;
	if (SitAmount > 0.3f || bInAir)
	{
		Desired = WalkIdleAnim;  // Neutral base under the sit/air poses
	}
	else if (SmoothedAnimSpeed >= 10.0f)
	{
		const FVector LocalVel = GetActorRotation().UnrotateVector(GetVelocity());
		const float F = LocalVel.X, R = LocalVel.Y;
		const bool bBack = F < 0.0f && FMath::Abs(F) >= FMath::Abs(R);
		const bool bSide = FMath::Abs(R) > FMath::Abs(F) * 0.5f;
		if (bSprintRequested)
		{
			Desired = bBack ? (JogBackAnimAsset ? JogBackAnimAsset : JogAnimAsset)
				: bSide ? (R < 0.0f ? JogLeftAnimAsset : JogRightAnimAsset)
				: JogAnimAsset;
			if (!Desired) Desired = JogAnimAsset;
			DesiredPlayRate = 0.85f;
		}
		else
		{
			Desired = bBack ? (WalkBackAnimAsset ? WalkBackAnimAsset : WalkAnimAsset)
				: bSide ? (R < 0.0f ? WalkLeftAnimAsset : WalkRightAnimAsset)
				: WalkAnimAsset;
			if (!Desired) Desired = WalkAnimAsset;
			DesiredPlayRate = FMath::Clamp(SmoothedAnimSpeed / 140.0f, 0.85f, 1.15f);
		}
	}

	if (Desired && CurrentAnim != Desired)
	{
		CurrentAnim = Desired;
		ParamotorAnim->PlaySequence(Desired, true);
	}
	ParamotorAnim->SetPlayRate(DesiredPlayRate);
}


void AParamotorCharacter::UpdateTripRisk(float DeltaTime, UCharacterMovementComponent* CMC, AParamotorPlayerController* PC)
{
	if (!CMC) return;

	const float BalanceRecoveryRate = 1.5f;
	if (CurrentThrust > 0.1f && CMC->IsMovingOnGround())
	{
		// === Wing weight support reduces trip risk ===
		float WeightSupportFraction = 0.0f;
		if (PC && PC->SpawnedWingActor)
		{
			EWingState WState = PC->SpawnedWingActor->GetWingState();
			if (WState == EWingState::Inflated)
			{
				WeightSupportFraction = FMath::Clamp(PC->SpawnedWingActor->GetDebugWeightSupport(), 0.0f, 1.0f);
			}
		}
		float TripRiskMultiplier = FMath::Lerp(1.0f, 0.0f, WeightSupportFraction);

		// === TRIP RISK (physical moment balance) ===
		float ThrustHeightM = 1.5f;
		float FootOffsetM = 0.15f;
		float PilotMassKg = FMath::Max(50.0f, CMC->Mass);
		float ThrustForceN = GetEquippedThrustN();
		float ThrustMoment = ThrustForceN * ThrustHeightM;
		float RestoringMoment = PilotMassKg * GRAVITY_MS2 * FootOffsetM;
		float NetMoment = (ThrustMoment - RestoringMoment) * TripRiskMultiplier;
		if (NetMoment > 0.0f)
		{
			float RefMoment = PilotMassKg * GRAVITY_MS2 * FootOffsetM;
			TripRiskAccumulator += (NetMoment / RefMoment) * DeltaTime;
		}
		else
		{
			TripRiskAccumulator = FMath::Max(0.0f, TripRiskAccumulator - DeltaTime * BalanceRecoveryRate);
		}

		float TripThreshold = 1.3f;
		if (TripRiskAccumulator >= TripThreshold)
		{
			Trip();
		}
	}
	else
	{
		TripRiskAccumulator = FMath::Max(0.0f, TripRiskAccumulator - DeltaTime * BalanceRecoveryRate);
	}
}

void AParamotorCharacter::UpdateRotationHandling(float DeltaTime)
{
	if (bIsFallen || !GetController()) return;

	if (bWeightShiftActive)
	{
		// FLYING: WingActor controls full rotation
		GetMesh()->SetRelativeLocation(FVector(0.0f, 0.0f, -90.0f));

		if (SpringArm)
		{
			SpringArm->bUsePawnControlRotation = false;
			FRotator ControlRot = GetControlRotation();
			FRotator ActorRot = GetActorRotation();
			SpringArm->SetWorldRotation(FRotator(ControlRot.Pitch, ControlRot.Yaw, ActorRot.Roll));
		}
	}
	else
	{
		// GROUND: Restore spring arm to follow controller
		if (SpringArm)
		{
			SpringArm->bUsePawnControlRotation = true;
		}

		FRotator ControlRot = GetControlRotation();
		FRotator CurrentRot = GetActorRotation();
		if (FMath::Abs(CurrentRot.Pitch) > 0.1f || FMath::Abs(CurrentRot.Roll) > 0.1f)
		{
			GetCapsuleComponent()->SetWorldRotation(FRotator(0.0f, ControlRot.Yaw, 0.0f));
		}

		GetMesh()->SetRelativeLocation(FVector(0.0f, 0.0f, -90.0f));

		FQuat BaseQuat(FVector::UpVector, FMath::DegreesToRadians(-90.0f));
		GetMesh()->SetRelativeRotation(BaseQuat);
	}
}
