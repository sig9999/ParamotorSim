// Copyright Epic Games, Inc. All Rights Reserved.

#include "ParamotorCharacter.h"
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

	// === CLOTHING: Hightop shoes ===
	// The Hightop shoes are standard MetaHuman skeletal meshes (not Chaos Outfit).
	// They're skinned to metahuman_base_skel and come in male/female body variants.
	// Try male medium narrow first, then female, then overweight variants.
	ShoesMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("ShoesMesh"));
	// Try male medium narrow first (most common paramotor pilot build)
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> ShoesFinder(TEXT("/Game/Hightop/meshes/m_med_nrw_shs_hightop.m_med_nrw_shs_hightop"));
	if (ShoesFinder.Succeeded())
	{
		ShoesMesh->SetSkeletalMesh(ShoesFinder.Object);
	}
	else
	{
		// Try female medium narrow
		static ConstructorHelpers::FObjectFinder<USkeletalMesh> ShoesFinderF(TEXT("/Game/Hightop/meshes/f_med_nrw_shs_hightop.f_med_nrw_shs_hightop"));
		if (ShoesFinderF.Succeeded())
		{
			ShoesMesh->SetSkeletalMesh(ShoesFinderF.Object);
		}
	}
	ShoesMesh->SetupAttachment(GetMesh());
	ShoesMesh->SetRelativeTransform(FTransform::Identity);
	ShoesMesh->SetLeaderPoseComponent(GetMesh());
	ShoesMesh->SetVisibility(true);
	ShoesMesh->SetHiddenInGame(false);
	ShoesMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ShoesMesh->SetSimulatePhysics(false);

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
		UE_LOG(LogTemp, Warning, TEXT("ParamotorCharacter: Replaced body mesh with techwear CombinedSkelMesh: %s"), *ClothedBody->GetName());
		if (OutfitMesh) OutfitMesh->SetVisibility(false);
		if (ShoesMesh) ShoesMesh->SetVisibility(false);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("ParamotorCharacter: Techwear CombinedSkelMesh not found — trying Hightop for shoes only"));
		if (ShoesMesh && !ShoesMesh->GetSkeletalMeshAsset())
		{
			USkeletalMesh* ShoeSkel = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Hightop/meshes/m_med_nrw_CombinedSkelMesh.m_med_nrw_CombinedSkelMesh"));
			if (ShoeSkel)
			{
				ShoesMesh->SetSkeletalMesh(ShoeSkel);
				ShoesMesh->SetLeaderPoseComponent(GetMesh());
				UE_LOG(LogTemp, Warning, TEXT("ParamotorCharacter: Hightop CombinedSkelMesh loaded for shoes: %s"), *ShoeSkel->GetName());
			}
		}
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

	UE_LOG(LogTemp, Warning, TEXT("BeginPlay: Idle=%p Walk=%p Jog=%p"), (void*)WalkIdleAnim, (void*)WalkAnimAsset, (void*)JogAnimAsset);

	GetMesh()->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	GetMesh()->SetAnimInstanceClass(UParamotorAnimInstance::StaticClass());

	if (UParamotorAnimInstance* ParamotorAnim = Cast<UParamotorAnimInstance>(GetMesh()->GetAnimInstance()))
	{
		if (WalkAnimAsset)
		{
			ParamotorAnim->PlaySequence(WalkAnimAsset, true);
			CurrentAnim = WalkAnimAsset;
			UE_LOG(LogTemp, Warning, TEXT("BeginPlay: ParamotorAnim playing walk"));
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("BeginPlay: Failed to get ParamotorAnimInstance"));
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
			UE_LOG(LogTemp, Warning, TEXT("ParamotorCharacter: Using clothed body — only overriding slot 0 (skin)"));
		}
		for (int32 i = 0; i < SkinSlotCount; ++i)
		{
			GetMesh()->SetMaterial(i, BodySkin);
		}
	}
}

void AParamotorCharacter::LoadFootstepSounds()
{
	// Grass (normal walk)
	for (int32 i = 1; i <= 6; i++)
	{
		FString Path = FString::Printf(TEXT("/Game/Sounds/footstep_grass_%d.footstep_grass_%d"), i, i);
		USoundBase* Snd = LoadObject<USoundBase>(nullptr, *Path);
		if (Snd) GrassFootstepSounds.Add(Snd);
	}
	// Grass (heavy - jogging/running)
	for (int32 i = 1; i <= 3; i++)
	{
		FString Path = FString::Printf(TEXT("/Game/Sounds/footstep_grass_heavy_%d.footstep_grass_heavy_%d"), i, i);
		USoundBase* Snd = LoadObject<USoundBase>(nullptr, *Path);
		if (Snd) GrassFootstepHeavySounds.Add(Snd);
	}
	// Dirt (normal walk)
	for (int32 i = 1; i <= 6; i++)
	{
		FString Path = FString::Printf(TEXT("/Game/Sounds/footstep_dirt_%d.footstep_dirt_%d"), i, i);
		USoundBase* Snd = LoadObject<USoundBase>(nullptr, *Path);
		if (Snd) DirtFootstepSounds.Add(Snd);
	}
	// Dirt (heavy - jogging/running)
	for (int32 i = 1; i <= 3; i++)
	{
		FString Path = FString::Printf(TEXT("/Game/Sounds/footstep_dirt_heavy_%d.footstep_dirt_heavy_%d"), i, i);
		USoundBase* Snd = LoadObject<USoundBase>(nullptr, *Path);
		if (Snd) DirtFootstepHeavySounds.Add(Snd);
	}
	// Fallback: load old single sounds if new ones weren't found
	if (GrassFootstepSounds.Num() == 0)
	{
		USoundBase* OldGrass = LoadObject<USoundBase>(nullptr, TEXT("/Game/Sounds/footstep_grass.footstep_grass"));
		if (OldGrass) GrassFootstepSounds.Add(OldGrass);
	}
	if (DirtFootstepSounds.Num() == 0)
	{
		USoundBase* OldDirt = LoadObject<USoundBase>(nullptr, TEXT("/Game/Sounds/footstep_dirt.footstep_dirt"));
		if (OldDirt) DirtFootstepSounds.Add(OldDirt);
	}
	UE_LOG(LogTemp, Warning, TEXT("ParamotorCharacter: Loaded %d grass + %d grass-heavy + %d dirt + %d dirt-heavy footstep sounds"),
		GrassFootstepSounds.Num(), GrassFootstepHeavySounds.Num(),
		DirtFootstepSounds.Num(), DirtFootstepHeavySounds.Num());
}

void AParamotorCharacter::LoadAmbientAudio()
{
	USoundBase* WindSound = LoadObject<USoundBase>(nullptr, TEXT("/Game/Sounds/wind_ambient.wind_ambient"));
	if (WindSound && WindAmbientAudio)
	{
		WindAmbientAudio->SetSound(WindSound);
		WindAmbientAudio->SetVolumeMultiplier(0.3f);
		WindAmbientAudio->Play();
		UE_LOG(LogTemp, Warning, TEXT("ParamotorCharacter: Wind ambient started"));
	}

	USoundBase* FallSound = LoadObject<USoundBase>(nullptr, TEXT("/Game/Sounds/fall_sound.fall_sound"));
	if (FallSound && FallAudio)
	{
		FallAudio->SetSound(FallSound);
	}
	UE_LOG(LogTemp, Warning, TEXT("ParamotorCharacter: BeginPlay at %s possessed=%d"),
		*GetActorLocation().ToString(), IsPlayerControlled());
}

void AParamotorCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	static float DBG_T = 0.f; DBG_T += DeltaTime;
	if (DBG_T > 4.f && DBG_T < 4.2f)
		UE_LOG(LogTemp, Warning, TEXT("ParamotorCharacter: Tick loc=%s movemode=%d possessed=%d"),
			*GetActorLocation().ToString(), (int)GetCharacterMovement()->MovementMode, IsPlayerControlled());

	UCharacterMovementComponent* CMC = GetCharacterMovement();

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
			UE_LOG(LogTemp, Warning, TEXT("ParamotorCharacter: Landed after air-thrust — TRIPPED!"));
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
	UpdateAnimationState(DeltaTime, CMC);

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

bool AParamotorCharacter::UpdateFallState(float DeltaTime, UCharacterMovementComponent* CMC, float EffectiveMaxSpeed)
{
	if (!bIsFallen) return false;

	if (bIsRagdolling)
	{
		FallTimer += DeltaTime;
		FVector RagVel = GetMesh() ? GetMesh()->GetPhysicsLinearVelocity() : FVector::ZeroVector;
		float RagSpeed = RagVel.Size();
		if (FallTimer >= FallRecoveryTime && RagSpeed < 50.0f)
		{
			SavedRagdollLocation = GetMesh()->GetComponentLocation();
			SavedRagdollQuat = GetMesh()->GetComponentQuat();
			GetMesh()->SetSimulatePhysics(false);
			GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			bIsRagdolling = false;
			bIsGettingUp = true;
			GetUpTimer = 0.0f;
			FVector RagLoc = SavedRagdollLocation;
			SetActorLocation(FVector(RagLoc.X, RagLoc.Y, GetActorLocation().Z));
			UE_LOG(LogTemp, Warning, TEXT("ParamotorCharacter: Starting get-up blend"));
		}
	}
	else if (bIsGettingUp)
	{
		GetUpTimer += DeltaTime;
		float BlendAlpha = FMath::Clamp(GetUpTimer / GetUpBlendTime, 0.0f, 1.0f);
		FVector StandLoc(0.0f, 0.0f, -90.0f);
		FQuat BaseQuat(FVector::UpVector, FMath::DegreesToRadians(-90.0f));
		FQuat StandQuat = BaseQuat;
		FVector CurrentMeshLoc = GetMesh()->GetRelativeLocation();
		FQuat CurrentMeshQuat = GetMesh()->GetRelativeRotation().Quaternion();
		FVector NewLoc = FMath::Lerp(CurrentMeshLoc, StandLoc, BlendAlpha);
		FQuat NewQuat = FQuat::Slerp(CurrentMeshQuat, StandQuat, BlendAlpha);
		GetMesh()->SetRelativeLocation(NewLoc);
		GetMesh()->SetRelativeRotation(NewQuat);
		if (BlendAlpha >= 1.0f)
		{
			bIsFallen = false;
			bIsGettingUp = false;
			GetUpTimer = 0.0f;
			FallTimer = 0.0f;
			TripRiskAccumulator = 0.0f;
			if (CMC)
			{
				CMC->SetMovementMode(MOVE_Walking);
				CMC->MaxWalkSpeed = EffectiveMaxSpeed;
			}
			GetMesh()->SetRelativeLocation(FVector(0.0f, 0.0f, -90.0f));
			FQuat FinalBaseQuat(FVector::UpVector, FMath::DegreesToRadians(-90.0f));
			GetMesh()->SetRelativeRotation(FinalBaseQuat);
			if (WalkIdleAnim)
			{
				CurrentAnim = WalkIdleAnim;
				if (UParamotorAnimInstance* PA = Cast<UParamotorAnimInstance>(GetMesh()->GetAnimInstance()))
					PA->PlaySequence(WalkIdleAnim, true);
			}
			UE_LOG(LogTemp, Warning, TEXT("ParamotorCharacter: Recovered from fall"));
		}
	}
	return true;  // Skip all normal movement/animation/audio code while fallen
}

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

void AParamotorCharacter::UpdateAnimationState(float DeltaTime, UCharacterMovementComponent* CMC)
{
	const float Speed = GetVelocity().Size();
	SmoothedAnimSpeed = FMath::FInterpTo(SmoothedAnimSpeed, Speed, DeltaTime, 6.0f);

	UParamotorAnimInstance* ParamotorAnim = Cast<UParamotorAnimInstance>(GetMesh()->GetAnimInstance());
	if (!ParamotorAnim)
		return;

	float DesiredPlayRate = 1.0f;

	if (SmoothedAnimSpeed < 10.0f)
	{
		if (WalkIdleAnim && CurrentAnim != WalkIdleAnim)
		{
			CurrentAnim = WalkIdleAnim;
			ParamotorAnim->PlaySequence(WalkIdleAnim, true);
		}
		DesiredPlayRate = 1.0f;
	}
	else if (bSprintRequested)
	{
		UAnimSequence* Jog = JogAnimAsset ? JogAnimAsset : WalkAnimAsset;
		if (Jog && CurrentAnim != Jog)
		{
			CurrentAnim = Jog;
			ParamotorAnim->PlaySequence(Jog, true);
		}
		DesiredPlayRate = 0.85f;
	}
	else
	{
		if (WalkAnimAsset && CurrentAnim != WalkAnimAsset)
		{
			CurrentAnim = WalkAnimAsset;
			ParamotorAnim->PlaySequence(WalkAnimAsset, true);
		}
		DesiredPlayRate = FMath::Clamp(SmoothedAnimSpeed / 140.0f, 0.85f, 1.15f);
	}

	ParamotorAnim->SetPlayRate(DesiredPlayRate);
}

void AParamotorCharacter::UpdateFootstepAudio(float DeltaTime, UCharacterMovementComponent* CMC)
{
	if (!CMC) return;

	bool bIsMoving = SmoothedAnimSpeed > 20.0f && CMC->IsMovingOnGround();

	if (bIsMoving)
	{
		float SpeedRatio = FMath::Clamp(SmoothedAnimSpeed / 270.0f, 0.0f, 1.0f);
		float StepInterval = FMath::Lerp(0.55f, 0.32f, SpeedRatio);

		FootstepTimer += DeltaTime;

		if (FootstepTimer >= StepInterval)
		{
			PlayFootstep();
			FootstepTimer = 0.0f;
		}
	}
	else
	{
		FootstepTimer = 0.0f;
	}
}

void AParamotorCharacter::UpdateWindAudio()
{
	if (!WindAmbientAudio) return;

	// Real wind noise: SPL ∝ U^5 (50 dB/decade), so amplitude ∝ U^2.5
	// The sound source is apparent wind at the pilot/ear:
	//   U_apparent = U_true_wind - U_vehicle (vector difference)
	// We use the wing's live dynamic wind (includes gusts, turbulence, altitude gradient)
	// for the true wind, and the pilot's velocity for the vehicle motion.

	float WindMs = 0.0f;
	FVector WindVec = FVector::ZeroVector;

	// Try to get live wind from the wing actor (includes gusts, turbulence, gradient)
	AParamotorPlayerController* PC = Cast<AParamotorPlayerController>(GetController());
	if (PC && PC->SpawnedWingActor)
	{
		WindVec = PC->SpawnedWingActor->GetWindVector() / 100.0f;  // cm/s to m/s
		WindMs = WindVec.Size();
	}
	else if (UParamotorGameInstance* GI = GetWorld()->GetGameInstance<UParamotorGameInstance>())
	{
		WindMs = GI->WindSpeed;
	}

	// Apparent wind = true wind - vehicle velocity (vector)
	FVector PilotVel = GetVelocity() / 100.0f;  // cm/s to m/s
	FVector ApparentWindVec = WindVec - PilotVel;
	float ApparentWindMs = ApparentWindVec.Size();

	// Use the AtmosphereModel wind noise amplitude (U^2.5 scaling)
	float NoiseAmplitude = ComputeWindNoiseAmplitude(ApparentWindMs);

	// Map to volume: 0.05 (silence) to 0.7 (strong wind)
	// The U^2.5 curve gives a realistic progression:
	//   2 m/s → 0.06, 5 m/s → 0.15, 10 m/s → 0.35, 20 m/s → 0.65, 30 m/s → 0.70
	float WindVol = 0.05f + NoiseAmplitude * 0.65f;
	WindAmbientAudio->SetVolumeMultiplier(WindVol);

	// Also modulate pitch slightly with wind speed (higher wind = slightly higher pitch)
	// Real wind noise shifts toward higher frequencies at higher speeds
	float PitchRatio = 0.9f + FMath::Clamp(ApparentWindMs / 30.0f, 0.0f, 1.0f) * 0.3f;
	WindAmbientAudio->SetPitchMultiplier(PitchRatio);
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

void AParamotorCharacter::PlayFootstep()
{
	if (!FootstepAudio) return;

	// Trace down to detect surface type
	FVector Start = GetActorLocation();
	FVector End = Start - FVector(0.0f, 0.0f, 120.0f);
	FHitResult Hit;
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);

	// Determine if jogging (heavy footsteps)
	float Speed = GetVelocity().Size();
	bool bIsJogging = Speed > 280.0f;  // Jog threshold

	TArray<USoundBase*>* SurfaceSounds = nullptr;
	TArray<USoundBase*>* HeavySounds = nullptr;

	if (GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, QueryParams))
	{
		// Check physical material of the surface
		UPhysicalMaterial* PhysMat = Hit.PhysMaterial.Get();

		// Default to grass - the map is a coastal grass environment
		SurfaceSounds = &GrassFootstepSounds;
		HeavySounds = &GrassFootstepHeavySounds;

		if (PhysMat)
		{
			FString MatName = PhysMat->GetName().ToLower();
			if (MatName.Contains(TEXT("dirt")) || MatName.Contains(TEXT("rock")) ||
				MatName.Contains(TEXT("sand")) || MatName.Contains(TEXT("stone")) ||
				MatName.Contains(TEXT("gravel")))
			{
				SurfaceSounds = &DirtFootstepSounds;
				HeavySounds = &DirtFootstepHeavySounds;
			}
		}
	}
	else
	{
		SurfaceSounds = &GrassFootstepSounds;
		HeavySounds = &GrassFootstepHeavySounds;
	}

	// Use heavy sounds when jogging, if available
	TArray<USoundBase*>* ActiveSounds = bIsJogging ? HeavySounds : SurfaceSounds;

	// Fallback: if heavy sounds empty, use normal sounds
	if (!ActiveSounds || ActiveSounds->Num() == 0)
	{
		ActiveSounds = SurfaceSounds;
	}
	if (!ActiveSounds || ActiveSounds->Num() == 0)
	{
		ActiveSounds = &GrassFootstepSounds;
	}

	if (ActiveSounds->Num() == 0) return;

	int32 Index = FMath::RandRange(0, ActiveSounds->Num() - 1);
	USoundBase* Sound = (*ActiveSounds)[Index];
	if (Sound)
	{
		// Natural variation - wider pitch range for variety, volume scales with speed
		float Pitch = FMath::FRandRange(0.92f, 1.08f);
		// Volume: walk = 0.55-0.70, jog = 0.70-0.90
		float BaseVol = bIsJogging ? 0.75f : 0.55f;
		float Vol = BaseVol + FMath::FRandRange(0.0f, 0.15f);
		FootstepAudio->SetSound(Sound);
		FootstepAudio->SetPitchMultiplier(Pitch);
		FootstepAudio->SetVolumeMultiplier(Vol);
		FootstepAudio->Play();
	}
}

void AParamotorCharacter::Trip()
{
	if (bIsFallen) return;

	bIsFallen = true;
	bIsRagdolling = true;
	bIsGettingUp = false;
	FallTimer = 0.0f;
	TripRiskAccumulator = 0.0f;

	// Capture velocity BEFORE stopping movement (StopMovementImmediately zeros it)
	FVector LaunchVel = GetVelocity();

	// Stop character movement
	if (UCharacterMovementComponent* TripCMC = GetCharacterMovement())
	{
		TripCMC->StopMovementImmediately();
		TripCMC->DisableMovement();
	}

	// === Enable ragdoll on the mesh ===
	// Re-enable physics and collision for the skeletal mesh
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	GetMesh()->SetCollisionResponseToAllChannels(ECR_Block);
	GetMesh()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	GetMesh()->SetSimulatePhysics(true);
	GetMesh()->WakeRigidBody();

	// Give the ragdoll the character's actual velocity at trip time
	// Real physics: the body inherits the momentum it had when it fell
	if (LaunchVel.Size2D() < 50.0f)
	{
		// Standing still: small forward tip from the thrust moment that caused the trip
		LaunchVel = GetActorForwardVector() * 50.0f;
	}
	// Add slight upward pop from the falling motion (CG passes outside base of support)
	LaunchVel.Z = FMath::Max(LaunchVel.Z, 50.0f);  // Small upward from tipping over

	// Apply impulse to the ragdoll bodies (bVelChange=true means cm/s velocity change)
	GetMesh()->AddImpulse(LaunchVel, NAME_None, true);

	UE_LOG(LogTemp, Warning, TEXT("ParamotorCharacter: RAGDOLL TRIP! (thrust was %.2f, launch vel=%s)"), CurrentThrust, *LaunchVel.ToString());

	// Play fall sound
	if (FallAudio)
	{
		FallAudio->SetVolumeMultiplier(0.8f);
		FallAudio->Play();
	}

	// === Trip can cause ankle injury if fast/hard ===
	// Running trips at speed can twist an ankle
	// Use LaunchVel (captured before StopMovementImmediately) not GetVelocity()
	float TripSpeed = LaunchVel.Size2D() / CM_PER_M;  // m/s
	ApplyImpactInjury(TripSpeed, true);
}

// ============================================================
// INJURY SYSTEM
// ============================================================

FString AParamotorCharacter::GetInjuryName() const
{
	switch (CurrentInjury)
	{
	case EInjuryType::SprainedAnkle: return TEXT("Sprained Ankle");
	case EInjuryType::BrokenAnkle:   return TEXT("Broken Ankle");
	case EInjuryType::BrokenLeg:     return TEXT("Broken Leg");
	default:                         return TEXT("None");
	}
}

void AParamotorCharacter::ApplyImpactInjury(float ImpactSpeedMs, bool bFromTrip)
{
	// Impact thresholds (vertical descent speed in m/s):
	// - < 3 m/s: safe landing, no injury
	// - 3-5 m/s: possible sprained ankle (especially from trip)
	// - 5-8 m/s: likely broken ankle
	// - > 8 m/s: broken leg (hard landing)
	//
	// For trips, ImpactSpeedMs is the horizontal speed at fall time.
	// Trips at low speed = no injury, trips at high speed = sprain risk.

	EInjuryType NewInjury = EInjuryType::None;
	float Severity = 0.0f;

	if (bFromTrip)
	{
		// Trip injuries are based on horizontal speed
		if (ImpactSpeedMs > 8.0f)
		{
			NewInjury = EInjuryType::BrokenAnkle;
			Severity = FMath::Clamp((ImpactSpeedMs - 8.0f) / 4.0f, 0.5f, 0.9f);
		}
		else if (ImpactSpeedMs > 5.0f)
		{
			NewInjury = EInjuryType::SprainedAnkle;
			Severity = FMath::Clamp((ImpactSpeedMs - 5.0f) / 3.0f, 0.3f, 0.6f);
		}
		// Low-speed trips = no injury (just a stumble)
	}
	else
	{
		// Landing injuries based on vertical impact speed
		if (ImpactSpeedMs > 8.0f)
		{
			NewInjury = EInjuryType::BrokenLeg;
			Severity = FMath::Clamp((ImpactSpeedMs - 8.0f) / 4.0f, 0.7f, 1.0f);
		}
		else if (ImpactSpeedMs > 5.0f)
		{
			NewInjury = EInjuryType::BrokenAnkle;
			Severity = FMath::Clamp((ImpactSpeedMs - 5.0f) / 3.0f, 0.4f, 0.7f);
		}
		else if (ImpactSpeedMs > 3.0f)
		{
			NewInjury = EInjuryType::SprainedAnkle;
			Severity = FMath::Clamp((ImpactSpeedMs - 3.0f) / 2.0f, 0.2f, 0.5f);
		}
	}

	// Only upgrade injury, never downgrade from a new lesser impact
	if (NewInjury != EInjuryType::None)
	{
		int32 NewRank = (int32)NewInjury;
		int32 CurRank = (int32)CurrentInjury;
		if (NewRank > CurRank)
		{
			CurrentInjury = NewInjury;
			InjurySeverity = Severity;
		}
		else if (NewRank == CurRank)
		{
			// Same type — worsen the severity
			InjurySeverity = FMath::Clamp(FMath::Max(InjurySeverity, Severity), 0.0f, 1.0f);
		}

		// Apply movement penalties based on injury
		switch (CurrentInjury)
		{
		case EInjuryType::SprainedAnkle:
			InjuryMaxSpeedMultiplier = 0.65f;  // 65% speed — noticeable limp
			InjuryJumpMultiplier = 0.5f;       // 50% jump height
			break;
		case EInjuryType::BrokenAnkle:
			InjuryMaxSpeedMultiplier = 0.35f;  // 35% speed — severe limp
			InjuryJumpMultiplier = 0.2f;       // Barely can hop
			break;
		case EInjuryType::BrokenLeg:
			InjuryMaxSpeedMultiplier = 0.15f;  // 15% speed — can barely shuffle
			InjuryJumpMultiplier = 0.0f;       // No jumping
			break;
		default:
			break;
		}

		InjuryHealTimer = 0.0f;
		UE_LOG(LogTemp, Warning, TEXT("ParamotorCharacter: INJURY — %s (severity %.2f, impact %.1f m/s, fromTrip=%d)"),
			*GetInjuryName(), InjurySeverity, ImpactSpeedMs, bFromTrip ? 1 : 0);
	}
}

void AParamotorCharacter::HealInjury(float DeltaTime)
{
	if (CurrentInjury == EInjuryType::None) return;

	InjuryHealTimer += DeltaTime;

	// Healing times (seconds for full heal at max severity):
	// - Sprained ankle: 60s
	// - Broken ankle: 180s (3 min)
	// - Broken leg: 300s (5 min)
	float HealTime = 60.0f;
	switch (CurrentInjury)
	{
	case EInjuryType::SprainedAnkle: HealTime = 60.0f;  break;
	case EInjuryType::BrokenAnkle:   HealTime = 180.0f; break;
	case EInjuryType::BrokenLeg:     HealTime = 300.0f; break;
	default: return;
	}

	// Gradual healing — severity decreases over time
	float HealRate = (1.0f / HealTime) * DeltaTime;
	InjurySeverity = FMath::Max(0.0f, InjurySeverity - HealRate);

	// Gradually restore movement as severity decreases
	float HealFraction = FMath::Clamp(1.0f - InjurySeverity, 0.0f, 1.0f);  // 0 = just injured, 1 = fully healed
	switch (CurrentInjury)
	{
	case EInjuryType::SprainedAnkle:
		InjuryMaxSpeedMultiplier = FMath::Lerp(0.65f, 1.0f, HealFraction);
		InjuryJumpMultiplier = FMath::Lerp(0.5f, 1.0f, HealFraction);
		break;
	case EInjuryType::BrokenAnkle:
		InjuryMaxSpeedMultiplier = FMath::Lerp(0.35f, 1.0f, HealFraction);
		InjuryJumpMultiplier = FMath::Lerp(0.2f, 1.0f, HealFraction);
		break;
	case EInjuryType::BrokenLeg:
		InjuryMaxSpeedMultiplier = FMath::Lerp(0.15f, 1.0f, HealFraction);
		InjuryJumpMultiplier = FMath::Lerp(0.0f, 1.0f, HealFraction);
		break;
	default:
		break;
	}

	// Fully healed
	if (InjurySeverity <= 0.0f)
	{
		InjurySeverity = 0.0f;
		CurrentInjury = EInjuryType::None;
		InjuryMaxSpeedMultiplier = 1.0f;
		InjuryJumpMultiplier = 1.0f;
		UE_LOG(LogTemp, Warning, TEXT("ParamotorCharacter: Injury healed — fully recovered"));
	}
}

void AParamotorCharacter::ClearInjury()
{
	CurrentInjury = EInjuryType::None;
	InjurySeverity = 0.0f;
	InjuryHealTimer = 0.0f;
	InjuryMaxSpeedMultiplier = 1.0f;
	InjuryJumpMultiplier = 1.0f;
	UE_LOG(LogTemp, Warning, TEXT("ParamotorCharacter: Injury cleared"));
}

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
	AddControllerYawInput(Value);

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
	AddControllerPitchInput(Value);
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
		UE_LOG(LogTemp, Warning, TEXT("ParamotorCharacter: Can't jump — broken leg!"));
		return;
	}
	// Apply injury jump penalty
	GetCharacterMovement()->JumpZVelocity = 420.0f * InjuryJumpMultiplier;
	// If thrust is active, jumping = loss of ground contact = thrown by thrust
	if (CurrentThrust > 0.1f)
	{
		UE_LOG(LogTemp, Warning, TEXT("ParamotorCharacter: Jumped with thrust=%.2f — will trip on landing!"), CurrentThrust);
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

// ============================================================
// BRAKE ARM IK — character reaches up to grab brake handles
// ============================================================

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
