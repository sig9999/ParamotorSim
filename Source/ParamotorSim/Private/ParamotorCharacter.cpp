// Copyright Epic Games, Inc. All Rights Reserved.

#include "ParamotorCharacter.h"
#include "ParamotorAnimInstance.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/AudioComponent.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Sound/SoundBase.h"
#include "Kismet/GameplayStatics.h"
#include "ParamotorGameInstance.h"
#include "ParamotorPlayerController.h"
#include "ParamotorActor.h"
#include "WingActor.h"

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

	// === Load clothing: Replace body mesh with techwear CombinedSkelMesh ===
	// The CombinedSkelMesh includes body + techwear jacket + pants + shoes as one
	// skeletal mesh. This is the cleanest approach — one mesh, no z-fighting.
	// The head mesh stays separate (attached via SetLeaderPoseComponent).
	USkeletalMesh* ClothedBody = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Techwear/techwearOutfit/Meshes/bodyShapeG_CombinedSkelMesh.bodyShapeG_CombinedSkelMesh"));
	if (!ClothedBody)
		ClothedBody = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Techwear/techwearOutfit/Meshes/bodyShapeH_CombinedSkelMesh.bodyShapeH_CombinedSkelMesh"));
	if (ClothedBody)
	{
		GetMesh()->SetSkeletalMesh(ClothedBody);
		UE_LOG(LogTemp, Warning, TEXT("ParamotorCharacter: Replaced body mesh with techwear CombinedSkelMesh: %s"), *ClothedBody->GetName());
		// Hide the outfit/shoes overlay meshes — no longer needed
		if (OutfitMesh) OutfitMesh->SetVisibility(false);
		if (ShoesMesh) ShoesMesh->SetVisibility(false);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("ParamotorCharacter: Techwear CombinedSkelMesh not found — trying Hightop for shoes only"));

		// Fall back: use Hightop CombinedSkelMesh for shoes only (as overlay)
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

	if (!WalkIdleAnim)
	{
		WalkIdleAnim = LoadObject<UAnimSequence>(nullptr, TEXT("/MetaHumanCharacter/Optional/Animation/UEFNAnimPreset/Locomotion/AS_MH_Neutral_Stand_Idle_Loop.AS_MH_Neutral_Stand_Idle_Loop"));
	}
	if (!WalkAnimAsset)
	{
		WalkAnimAsset = LoadObject<UAnimSequence>(nullptr, TEXT("/MetaHumanCharacter/Optional/Animation/UEFNAnimPreset/Locomotion/AS_MH_Neutral_Walk_Loop_F.AS_MH_Neutral_Walk_Loop_F"));
	}
	if (!JogAnimAsset)
	{
		JogAnimAsset = LoadObject<UAnimSequence>(nullptr, TEXT("/MetaHumanCharacter/Optional/Animation/UEFNAnimPreset/Locomotion/AS_MH_Neutral_Run_Loop_F.AS_MH_Neutral_Run_Loop_F"));
	}

	UE_LOG(LogTemp, Warning, TEXT("BeginPlay: Idle=%p Walk=%p Jog=%p"), (void*)WalkIdleAnim, (void*)WalkAnimAsset, (void*)JogAnimAsset);

	// Use custom anim instance for IK support
	GetMesh()->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	// Set our custom anim instance class
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

	// Darker, uniform skin tone.
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

		// Only apply skin material to slot 0 (body skin).
		// If we're using a CombinedSkelMesh with clothing, the other slots have
		// clothing materials (jacket, pants, shoes) that should NOT be overwritten.
		// For the naked body (MHC_Frey_FullBody), all slots are skin.
		int32 SkinSlotCount = GetMesh()->GetNumMaterials();
		// Check if we're using the clothed body — if so, only override slot 0
		USkeletalMesh* CurrentMesh = GetMesh()->GetSkeletalMeshAsset();
		if (CurrentMesh && CurrentMesh->GetName().Contains(TEXT("CombinedSkelMesh")))
		{
			SkinSlotCount = 1;  // Only slot 0 is skin; rest are clothing
			UE_LOG(LogTemp, Warning, TEXT("ParamotorCharacter: Using clothed body — only overriding slot 0 (skin)"));
		}
		for (int32 i = 0; i < SkinSlotCount; ++i)
		{
			GetMesh()->SetMaterial(i, BodySkin);
		}
	}

	// Load footstep sounds - multiple variations per surface for natural variety
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

	// Start wind ambient sound
	USoundBase* WindSound = LoadObject<USoundBase>(nullptr, TEXT("/Game/Sounds/wind_ambient.wind_ambient"));
	if (WindSound && WindAmbientAudio)
	{
		WindAmbientAudio->SetSound(WindSound);
		WindAmbientAudio->SetVolumeMultiplier(0.3f);
		WindAmbientAudio->Play();
		UE_LOG(LogTemp, Warning, TEXT("ParamotorCharacter: Wind ambient started"));
	}

	// Load fall sound
	USoundBase* FallSound = LoadObject<USoundBase>(nullptr, TEXT("/Game/Sounds/fall_sound.fall_sound"));
	if (FallSound && FallAudio)
	{
		FallAudio->SetSound(FallSound);
	}
}

void AParamotorCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// === INJURY HEALING ===
	// Injuries gradually heal over time. Applied before fall handling so
	// movement penalties are available in the get-up-from-fall section.
	HealInjury(DeltaTime);
	float EffectiveMaxSpeed = CurrentMaxSpeed * InjuryMaxSpeedMultiplier;

	// === Fall handling — early out when fallen ===
	// Process ragdoll/get-up logic first, then skip all normal movement code
	if (bIsFallen)
	{
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
				if (GetCharacterMovement())
				{
					GetCharacterMovement()->SetMovementMode(MOVE_Walking);
					GetCharacterMovement()->MaxWalkSpeed = EffectiveMaxSpeed;
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
		return;  // Skip all normal movement/animation/audio code while fallen
	}

	// === Force actor rotation to match controller yaw ===
	// This prevents the character from ever rotating sideways due to
	// velocity changes from thrust. The character ALWAYS faces the camera direction.
	// EXCEPT when flying with weight shift — then the character body stays on its
	// flight heading and only leans (rolls) from weight shift. The camera can still
	// look around freely.
	if (!bIsFallen && GetController())
	{
		if (bWeightShiftActive)
		{
			// Flying: WingActor controls pawn rotation (bank, pitch, yaw).
			// Do NOT override rotation here — that would fight the wing physics
			// and keep the character locked level. The wing sets the full rotation
			// including bank angle and pendulum pitch.
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

	// === Paramotor thrust physics — REALISTIC GROUND BEHAVIOR ===
	//
	// Real paramotor ground physics:
	// 1. STANDING STILL + throttle: Feet planted (friction). NO sliding.
	//    Character leans forward to brace. High throttle = trip risk.
	// 2. WALKING + throttle: Thrust boosts forward speed. Less lean.
	// 3. JUMPING + throttle: No ground friction → thrust throws you → FALL.
	//
	// Thrust: ~75kgf (736N) at full throttle (Vittorazi Moster 185)
	// Pilot + gear: ~100-120kg
	bool bOnGround = GetCharacterMovement()->IsMovingOnGround();
	bool bThrustActive = !bIsFallen && CurrentThrust > 0.01f;

	// Cache controller for wing state checks
	AParamotorPlayerController* PC = Cast<AParamotorPlayerController>(GetController());
	// Wing flight check: WingActor sets MOVE_None (not MOVE_Flying) during flight,
	// so IsFlying() on the movement component is always false. Check the wing instead.
	auto IsWingFlying = [PC]() -> bool {
		return (PC && PC->SpawnedWingActor && PC->SpawnedWingActor->IsFlying());
	};

	// Detect if player is providing movement input (pressing W/A/S/D)
	FVector CurrentAccel = GetCharacterMovement()->GetCurrentAcceleration();
	bool bHasMovementInput = CurrentAccel.Size2D() > 1.0f;

	// Track if player was genuinely airborne while thrust was active (not just bouncing)
	// During takeoff run, the wing lifts the pilot into brief hops/skips — these are NORMAL
	// and should NOT trigger a trip. Only trip if the player was genuinely high in the air.
	// Air time tracking (instance members — was static, shared between instances)
	if (bThrustActive && !bOnGround && !IsWingFlying())
	{
		AirTimeAccum += DeltaTime;
		// Track max height above ground during this air episode
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
		// Only flag as genuinely airborne if: 1+ second in air AND >2m above ground
		// (takeoff skips are <0.5s and <1m high)
		if (AirTimeAccum > 1.0f && MaxAirHeight > 200.0f)
			bWasInAirWithThrust = true;
	}
	else if (bOnGround)
	{
		AirTimeAccum = 0.0f;
		MaxAirHeight = 0.0f;
	}

	if (bThrustActive && bOnGround)
	{
		// === LANDED AFTER BEING IN AIR WITH THRUST → TRIP ===
		// Only if genuinely airborne (>2m high, >1s in air) and not in wing flight
		if (bWasInAirWithThrust)
		{
			bWasInAirWithThrust = false;
			AirTimeAccum = 0.0f;
			MaxAirHeight = 0.0f;
			// Only trip if the wing isn't flying (wing flight landing = controlled landing)
			if (!IsWingFlying())
			{
				UE_LOG(LogTemp, Warning, TEXT("ParamotorCharacter: Landed after air-thrust — TRIPPED!"));
				Trip();
			}
		}
		else if (bHasMovementInput)
		{
			// === WALKING + THRUST: boost forward speed ===
			// Max run speed from leg power vs. resistance (same formula as WingGroundHandling.cpp)
			// P_leg = F_resistance * v -> v_max = P_leg / F_resistance
			// Pilot sprint power: ~350W. Resistance: rolling friction + paramotor drag
			float PilotLegPowerW = 350.0f;
			// PilotWeight (set by PlayerController) already includes paramotor + fuel mass.
			// Only add wing + lines (not already in PilotWeight).
			float CharMassForRun = GetCharacterMovement()->Mass;
			if (CharMassForRun <= 0.0f) CharMassForRun = 100.0f;
			float TotalMass = CharMassForRun + 7.5f;  // +wing(6kg) + lines(1.5kg) approx
			float RollingMu = 0.4f;  // Rubber soles on grass (consistent with WingGroundHandling)
			float RollingResistanceN = RollingMu * TotalMass * GRAVITY_MS2;  // mu * N
			float MaxRunSpeedMs = (RollingResistanceN > 1.0f) ? PilotLegPowerW / RollingResistanceN : 8.0f;
			MaxRunSpeedMs = FMath::Clamp(MaxRunSpeedMs, 1.5f, 12.0f);  // Physical human bounds
			const float MaxRunSpeed = MaxRunSpeedMs * 100.0f;  // cm/s

			// Allow speed to build up to the max running speed
			GetCharacterMovement()->MaxWalkSpeed = MaxRunSpeed;

			// Slightly reduce braking so thrust can build speed
			GetCharacterMovement()->BrakingDecelerationWalking = 100.0f;
			GetCharacterMovement()->GroundFriction = 4.0f;

			// Apply thrust as forward acceleration (F=ma → Δv = F/m * Δt)
			// Always use real thrust from ParamotorActor (blade element theory)
			float ThrustForceN = 0.0f;
			if (PC && PC->GetSpawnedParamotorActor() && PC->GetSpawnedParamotorActor()->IsEquipped())
			{
				float AirspeedMs = GetVelocity().Size() / 100.0f;
				ThrustForceN = PC->GetSpawnedParamotorActor()->GetThrustForceN(AirspeedMs);
			}
			float ThrustForceUE = ThrustForceN * CM_PER_M;
			float CharMass = GetCharacterMovement()->Mass;
			if (CharMass <= 0.0f) CharMass = 100.0f;
			float Accel = ThrustForceUE / CharMass;

			FVector Forward = GetActorForwardVector();
			Forward.Z = 0.0f;
			Forward.Normalize();

			FVector ThrustVel = Forward * Accel * DeltaTime;
			FVector CurrentVel = GetVelocity();
			FVector NewVel = CurrentVel + ThrustVel;

			// Cap at max running speed — but allow wing lift to increase the cap
			// As the wing generates lift, the pilot is lighter and can run faster
			float EffectiveMaxRun = MaxRunSpeed;
			if (PC && PC->SpawnedWingActor)
			{
				EWingState WState = PC->SpawnedWingActor->GetWingState();
				if (WState == EWingState::Inflated)
				{
					// Wing lift reduces effective weight → pilot can run faster
					// Physical: if wing supports fraction X of weight, effective mass
					// is (1-X)×original. Max run speed scales as 1/sqrt(1-X) from
					// reduced ground reaction force (less friction, less energy cost)
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

			// DON'T overwrite Z velocity — the wing's lift force sets it
			// Only keep Z from the thrust boost if wing isn't controlling it
			if (!(PC && PC->SpawnedWingActor && PC->SpawnedWingActor->GetWingState() == EWingState::Inflated))
			{
				NewVel.Z = CurrentVel.Z;
			}
			GetCharacterMovement()->Velocity = NewVel;
		}
		else
		{
			// === STANDING STILL + THRUST: feet planted, NO movement ===
			// Ground friction holds the character in place.
			// Just lean forward to brace. Trip risk handles knockover.
			GetCharacterMovement()->MaxWalkSpeed = EffectiveMaxSpeed;
			GetCharacterMovement()->BrakingDecelerationWalking = 300.0f;
			GetCharacterMovement()->GroundFriction = 8.0f;
			// Do NOT modify velocity — feet are planted on the ground
		}
	}
	else if (!bThrustActive)
	{
		// No thrust: normal movement
		bWasInAirWithThrust = false;
		GetCharacterMovement()->MaxWalkSpeed = EffectiveMaxSpeed;
		GetCharacterMovement()->BrakingDecelerationWalking = 300.0f;
		GetCharacterMovement()->GroundFriction = 8.0f;
	}

	// === WIND DRAG ON INFLATED WING ===
	// DISABLED: The wind push on the ground was pushing the pilot backward,
	// reducing apparent wind and preventing takeoff. The WingActor already
	// calculates airspeed from wind correctly. Wind knockdown risk is kept.
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

			// Only apply knockdown risk in very strong wind — no velocity push
			if (!bThrustActive && WindMs > 12.0f)  // Real knockdown ~12 m/s
			{
				TripRiskAccumulator += (WindMs - 12.0f) * 0.5f * DeltaTime;  // From drag force on body
			}
		}
	}

	// === IN AIR WITH THRUST: thrust throws you forward ===
	// Only applies when falling (not when wing is flying — WingActor handles that)
	bool bIsAirborne = !bOnGround || IsWingFlying();
	bool bWingFlying = IsWingFlying();  // Wing sets MOVE_None (not MOVE_Flying) during flight
	if (bThrustActive && bIsAirborne && !bIsFallen && !bWingFlying)
	{
		float ThrustForceN = 0.0f;
		if (PC && PC->GetSpawnedParamotorActor() && PC->GetSpawnedParamotorActor()->IsEquipped())
		{
			float AirspeedMs = GetVelocity().Size() / 100.0f;
			ThrustForceN = PC->GetSpawnedParamotorActor()->GetThrustForceN(AirspeedMs);
		}
		float ThrustForceUE = ThrustForceN * 100.0f;
		float CharMass = GetCharacterMovement()->Mass;
		if (CharMass <= 0.0f) CharMass = 100.0f;
		float Accel = ThrustForceUE / CharMass;

		FVector Forward = GetActorForwardVector();
		Forward.Z = 0.0f;
		Forward.Normalize();

		// Apply thrust as velocity in air (unopposed — no ground friction)
		FVector ThrustVel = Forward * Accel * DeltaTime;
		FVector CurrentVel = GetVelocity();
		FVector NewVel = CurrentVel + ThrustVel;
		NewVel.Z = CurrentVel.Z; // Don't affect vertical when falling
		GetCharacterMovement()->Velocity = NewVel;
	}

	// === Posture: lean forward from thrust, straighten when moving ===
	// Standing still + throttle = lean forward (bracing against thrust)
	// Walking/running + throttle = straighten up (momentum counteracts lean)
	{
		float GroundSpeed = FVector(GetVelocity().X, GetVelocity().Y, 0.0f).Size();

		if (CurrentThrust > 0.01f && !bIsFallen)
		{
			// Lean from torque balance: M_thrust = M_gravity
			// F_thrust × h_thrust = m × g × d × sin(lean)
			// lean = atan(F_thrust × h / (m × g × d))
			// h = prop height above feet (~1.5m), d = CG offset from feet (~0.3m)
			float ThrustHeightM = 1.5f;
			float CGOffsetM = 0.3f;
			float CharMass = FMath::Max(50.0f, GetCharacterMovement()->Mass);
			float ThrustN = 0.0f;
			if (PC && PC->GetSpawnedParamotorActor() && PC->GetSpawnedParamotorActor()->IsEquipped())
			{
				// Use real propeller thrust from the paramotor actor
				ThrustN = PC->GetSpawnedParamotorActor()->GetThrustForceN(GetVelocity().Size() / 100.0f);
			}
			else
			{
				ThrustN = CurrentThrust * MAX_THRUST_N;  // Fallback: max thrust at full throttle
			}
			float GravityN = CharMass * GRAVITY_MS2;
			float ThrustLeanRad = FMath::Atan2(ThrustN * ThrustHeightM, GravityN * CGOffsetM);
			float ThrustLean = ThrustLeanRad * 180.0f / PI;
			// Forward momentum provides a restoring moment (inertia straightens pilot)
			// M_momentum = m × v × h / dt ≈ m × v² × h / stride_length
			// At running speed, this counteracts the thrust lean
			float SpeedMs = GroundSpeed / 100.0f;
			float MomentumLean = FMath::Atan2(CharMass * SpeedMs * SpeedMs * ThrustHeightM, GravityN * CGOffsetM) * 180.0f / PI;
			TargetLeanPitch = FMath::Max(0.0f, ThrustLean - MomentumLean);
		}
		else
		{
			TargetLeanPitch = 0.0f;
		}

		// Smoothly interpolate lean
		CurrentLeanPitch = FMath::FInterpTo(CurrentLeanPitch, TargetLeanPitch, DeltaTime, 5.0f);

		// Apply lean to mesh using QUATERNIONS.
		// The mesh has a base -90 yaw (MetaHuman faces +Y, we rotate to face +X).
		// FRotator(pitch, yaw, roll) applies roll→yaw→pitch which scrambles the lean.
		// We need: apply -90 yaw first, THEN lean forward around the capsule's +Y (right) axis.
		// FinalQuat = LeanQuat * BaseQuat applies BaseQuat first, then LeanQuat in capsule space.
		// During flight, skip this — the mesh follows the capsule rotation naturally
		// (which is set by WingActor with full bank/pitch/yaw).
		if (!bIsFallen && !bWeightShiftActive)
		{
			FQuat BaseQuat(FVector::UpVector, FMath::DegreesToRadians(-90.0f));
			// Lean around capsule's +Y (right axis). Positive = top goes forward.
			FQuat LeanQuat(FVector::RightVector, FMath::DegreesToRadians(CurrentLeanPitch));
			FQuat FinalQuat = LeanQuat * BaseQuat;
			GetMesh()->SetRelativeRotation(FinalQuat);
		}
	}

	const float Speed = GetVelocity().Size();
	SmoothedAnimSpeed = FMath::FInterpTo(SmoothedAnimSpeed, Speed, DeltaTime, 6.0f);

	UParamotorAnimInstance* ParamotorAnim = Cast<UParamotorAnimInstance>(GetMesh()->GetAnimInstance());
	if (!ParamotorAnim)
	{
		return;
	}

	float DesiredPlayRate = 1.0f;

	if (SmoothedAnimSpeed < 10.0f)
	{
		// Idle pause: play the idle clip at normal rate.
		if (WalkIdleAnim && CurrentAnim != WalkIdleAnim)
		{
			CurrentAnim = WalkIdleAnim;
			ParamotorAnim->PlaySequence(WalkIdleAnim, true);
		}
		DesiredPlayRate = 1.0f;
	}
	else if (bSprintRequested)
	{
		// Jog: switch to the dedicated jog/run clip and play it slightly slower for a jog cadence.
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
		// Walk: normal locomotion.
		if (WalkAnimAsset && CurrentAnim != WalkAnimAsset)
		{
			CurrentAnim = WalkAnimAsset;
			ParamotorAnim->PlaySequence(WalkAnimAsset, true);
		}
		DesiredPlayRate = FMath::Clamp(SmoothedAnimSpeed / 140.0f, 0.85f, 1.15f);
	}

	ParamotorAnim->SetPlayRate(DesiredPlayRate);

	// === Brake arm IK — update arm positions for reaching brake handles ===
	UpdateBrakeArmIK(DeltaTime);

	// === Footstep audio ===
	bool bIsMoving = SmoothedAnimSpeed > 20.0f && GetCharacterMovement()->IsMovingOnGround();

	if (bIsMoving)
	{
		// Step interval scales with actual speed - faster movement = faster steps
		// Walk (~140 cm/s) = 0.55s between steps, Jog (~270 cm/s) = 0.32s
		float SpeedRatio = FMath::Clamp(SmoothedAnimSpeed / 270.0f, 0.0f, 1.0f);
		float StepInterval = FMath::Lerp(0.55f, 0.32f, SpeedRatio);

		FootstepTimer += DeltaTime;

		if (FootstepTimer >= StepInterval)
		{
			PlayFootstep();
			FootstepTimer = 0.0f;
		}
		bWasMoving = true;
	}
	else
	{
		// Stopped - no phantom step, just reset
		bWasMoving = false;
		FootstepTimer = 0.0f;
	}

	// === Wind volume based on movement speed ===
	if (WindAmbientAudio)
	{
		float SpeedRatio = FMath::Clamp(SmoothedAnimSpeed / 270.0f, 0.0f, 1.0f);
		float WindVol = 0.25f + SpeedRatio * 0.15f;
		WindAmbientAudio->SetVolumeMultiplier(WindVol);
	}

	// Accumulate trip risk when thrusting on the ground
	// Real paramotor: tripping is mainly a risk when:
	// 1. Standing still with high throttle (knockover)
	// 2. Running with excessive throttle before the wing is inflated
	// 3. Uneven terrain at speed
	// Once the wing is inflated and supporting weight, tripping is very rare
	// Pilot balance recovery rate (physical: human recovers balance in ~0.67s)
	const float BalanceRecoveryRate = 1.5f;  // 1/τ where τ ≈ 0.67s
	if (CurrentThrust > 0.1f && GetCharacterMovement()->IsMovingOnGround())
	{
		float GroundSpeed = FVector(GetVelocity().X, GetVelocity().Y, 0.0f).Size();

		// === Wing weight support reduces trip risk ===
		// As the wing generates lift, it takes weight off the pilot's feet,
		// making them lighter and more stable. At full weight support, tripping
		// is nearly impossible. At no weight support, trip risk is normal.
		float WeightSupportFraction = 0.0f;
		if (PC && PC->SpawnedWingActor)
		{
			EWingState WState = PC->SpawnedWingActor->GetWingState();
			if (WState == EWingState::Inflated)
			{
				// Use wing's debug weight support value (GravityScale is always 1.0 in ground handling)
				WeightSupportFraction = FMath::Clamp(PC->SpawnedWingActor->GetDebugWeightSupport(), 0.0f, 1.0f);
			}
		}
		// Trip risk multiplier: 1.0 at no support, 0.05 at full support
		float TripRiskMultiplier = FMath::Lerp(1.0f, 0.0f, WeightSupportFraction);  // No residual at full support

		// === TRIP RISK (physical moment balance, not probability) ===
		// A pilot trips when the thrust moment about their feet exceeds
		// the restoring moment from their weight.
		//
		// Thrust moment: M_thrust = F_thrust × h_thrust
		//   h_thrust = height of prop above feet (~1.5m for paramotor harness)
		// Restoring moment: M_weight = m × g × d_foot
		//   d_foot = horizontal distance from CG to feet (~0.1m standing)
		//   The pilot can lean forward to increase d_foot (up to ~0.3m)
		//
		// Trip occurs when M_thrust > M_weight for sustained time.
		// The accumulator represents how long the imbalance has persisted.
		// Recovery: if M_weight > M_thrust, the pilot recovers balance.
		float ThrustHeightM = 1.5f;  // Prop height above feet
		float FootOffsetM = 0.15f;   // Base CG-to-foot horizontal distance
		float PilotMassKg = FMath::Max(50.0f, GetCharacterMovement()->Mass);  // From CMC mass
		// Use real thrust from paramotor actor
		float ThrustForceN = 0.0f;
		if (PC && PC->GetSpawnedParamotorActor() && PC->GetSpawnedParamotorActor()->IsEquipped())
		{
			float AirspeedMs = GetVelocity().Size() / 100.0f;
			ThrustForceN = PC->GetSpawnedParamotorActor()->GetThrustForceN(AirspeedMs);
		}
		float ThrustMoment = ThrustForceN * ThrustHeightM;
		float RestoringMoment = PilotMassKg * GRAVITY_MS2 * FootOffsetM;
		// Net moment: positive = tipping forward (trip risk)
		float NetMoment = (ThrustMoment - RestoringMoment) * TripRiskMultiplier;
		if (NetMoment > 0.0f)
		{
			// Accumulate trip risk proportional to moment imbalance
			// Normalize by a reference moment (pilot weight × 0.1m)
			float RefMoment = PilotMassKg * GRAVITY_MS2 * FootOffsetM;  // Use actual FootOffsetM
			TripRiskAccumulator += (NetMoment / RefMoment) * DeltaTime;
		}
		else
		{
			// Pilot can resist — recover balance at physical rate
			TripRiskAccumulator = FMath::Max(0.0f, TripRiskAccumulator - DeltaTime * BalanceRecoveryRate);
		}

		// Trip when the tipping time exceeds the physical fall time
		// A human tips over when the center of gravity passes outside the
		// base of support. The time to fall from a given imbalance is:
		// t_fall = sqrt(2 × I / (m × g × d)) where I is body inertia about feet
		// For a 110 kg pilot with 1m CG height: I ≈ 110 kg·m², t_fall ≈ 1.3s
		// The accumulator represents sustained imbalance; trip when it exceeds t_fall
		float TripThreshold = 1.3f;  // t_fall = sqrt(2I/(mgd)) from CG dynamics
		if (TripRiskAccumulator >= TripThreshold)
		{
			Trip();
		}
	}
	else
	{
		// Risk decays at physical balance recovery rate when not thrusting
		TripRiskAccumulator = FMath::Max(0.0f, TripRiskAccumulator - DeltaTime * BalanceRecoveryRate);
	}

	// === ROTATION HANDLING — last thing in Tick ===
	if (!bIsFallen && GetController())
	{
		if (bWeightShiftActive)
		{
			// FLYING: WingActor controls full rotation (bank, pitch, yaw for pendulum + turns).
			// The WingActor sets our rotation via SetActorRotation, but the controller's
			// UpdateRotation may have already run and forced us level. Re-assert the
			// WingActor's rotation here as the final word in the frame.
			AParamotorPlayerController* FlightPC = Cast<AParamotorPlayerController>(GetController());
			if (FlightPC && FlightPC->SpawnedWingActor)
			{
				// If the controller forced us level (roll near 0) but the wing is banked,
				// the WingActor's SetActorRotation will fix it next tick.
				// The real fix is the UpdateRotation override in the controller.
			}

			GetMesh()->SetRelativeLocation(FVector(0.0f, 0.0f, -90.0f));

			// CAMERA ROLL: The spring arm's bUsePawnControlRotation makes its TickComponent
			// overwrite world rotation to the control rotation (zero roll) every frame.
			// We must disable that and manually set the world rotation to include the
			// actor's roll, while keeping pitch/yaw from the controller (mouse look).
			if (SpringArm)
			{
				SpringArm->bUsePawnControlRotation = false;
				FRotator ControlRot = GetControlRotation();
				FRotator ActorRot = GetActorRotation();
				// Pitch + yaw from controller, roll from actor (wing bank + pendulum)
				SpringArm->SetWorldRotation(FRotator(ControlRot.Pitch, ControlRot.Yaw, ActorRot.Roll));
			}
		}
		else
		{
			// GROUND: Restore spring arm to follow controller (no roll on ground)
			if (SpringArm)
			{
				SpringArm->bUsePawnControlRotation = true;
			}

			// GROUND: Force capsule upright (only yaw from controller)
			FRotator ControlRot = GetControlRotation();
			FRotator CurrentRot = GetActorRotation();
			if (FMath::Abs(CurrentRot.Pitch) > 0.1f || FMath::Abs(CurrentRot.Roll) > 0.1f)
			{
				GetCapsuleComponent()->SetWorldRotation(FRotator(0.0f, ControlRot.Yaw, 0.0f));
			}

			// Force mesh to correct relative transform
			GetMesh()->SetRelativeLocation(FVector(0.0f, 0.0f, -90.0f));

			// Same quaternion approach as the lean section above
			FQuat BaseQuat(FVector::UpVector, FMath::DegreesToRadians(-90.0f));
			FQuat LeanQuat(FVector::RightVector, FMath::DegreesToRadians(CurrentLeanPitch));
			FQuat FinalQuat = LeanQuat * BaseQuat;
			GetMesh()->SetRelativeRotation(FinalQuat);
		}
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
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->DisableMovement();

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
	float TripSpeed = GetVelocity().Size2D() / 100.0f;  // m/s
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
			bInjuryLimpActive = true;
			break;
		case EInjuryType::BrokenAnkle:
			InjuryMaxSpeedMultiplier = 0.35f;  // 35% speed — severe limp
			InjuryJumpMultiplier = 0.2f;       // Barely can hop
			bInjuryLimpActive = true;
			break;
		case EInjuryType::BrokenLeg:
			InjuryMaxSpeedMultiplier = 0.15f;  // 15% speed — can barely shuffle
			InjuryJumpMultiplier = 0.0f;       // No jumping
			bInjuryLimpActive = true;
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
	InjurySeverity -= HealRate;

	// Gradually restore movement as severity decreases
	float HealFraction = 1.0f - InjurySeverity;  // 0 = just injured, 1 = fully healed
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

	// Limp goes away at 80% healed
	if (InjurySeverity < 0.2f)
		bInjuryLimpActive = false;

	// Fully healed
	if (InjurySeverity <= 0.0f)
	{
		InjurySeverity = 0.0f;
		CurrentInjury = EInjuryType::None;
		InjuryMaxSpeedMultiplier = 1.0f;
		InjuryJumpMultiplier = 1.0f;
		bInjuryLimpActive = false;
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
	bInjuryLimpActive = false;
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
