// Copyright Epic Games, Inc. All Rights Reserved.

#include "ParamotorActor.h"
#include "ParamotorLogging.h"
#include "WingActor.h"
#include "ParamotorCharacter.h"
#include "UW_InteractPrompt.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Components/WidgetComponent.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundWave.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "CodexParts.h"

AParamotorActor::AParamotorActor()
{
	PrimaryActorTick.bCanEverTick = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	// Hang pivot — the frame/engine/prop hang off this and can swing on the
	// pilot's back like a real paramotor hanging off the harness straps.
	HangPivot = CreateDefaultSubobject<USceneComponent>(TEXT("HangPivot"));
	HangPivot->SetupAttachment(Root);
	HangPivot->SetRelativeLocation(HangPivotLoc);

	InteractionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("InteractionSphere"));
	InteractionSphere->SetupAttachment(Root);
	InteractionSphere->SetSphereRadius(150.0f);
	InteractionSphere->SetCollisionProfileName(TEXT("OverlapAllPawn"));

	PromptWidget = CreateDefaultSubobject<UWidgetComponent>(TEXT("PromptWidget"));
	PromptWidget->SetupAttachment(Root);
	PromptWidget->SetRelativeLocation(FVector(0.0f, 0.0f, 180.0f));
	PromptWidget->SetWidgetSpace(EWidgetSpace::Screen);
	PromptWidget->SetVisibility(false);
	PromptWidget->SetDrawSize(FVector2D(300.0f, 50.0f));

	// Audio components
	StarterAudio = CreateDefaultSubobject<UAudioComponent>(TEXT("StarterAudio"));
	StarterAudio->SetupAttachment(Root);
	StarterAudio->bAutoActivate = false;
	StarterAudio->SetRelativeLocation(FVector(0.0f, 0.0f, 70.0f));
	// Starter loops while cranking (hold P)
	StarterAudio->bIsUISound = false;

	EngineAudio = CreateDefaultSubobject<UAudioComponent>(TEXT("EngineAudio"));
	EngineAudio->SetupAttachment(Root);
	EngineAudio->bAutoActivate = false;
	EngineAudio->SetRelativeLocation(FVector(0.0f, 0.0f, 70.0f));
	// Looping is set on the Sound Wave asset itself (via Python import)

	ExhaustAudio = CreateDefaultSubobject<UAudioComponent>(TEXT("ExhaustAudio"));
	ExhaustAudio->SetupAttachment(Root);
	ExhaustAudio->bAutoActivate = false;
	ExhaustAudio->SetRelativeLocation(FVector(-20.0f, 0.0f, 20.0f));

	// Prop wind audio - positioned at the prop, loud aerodynamic noise
	PropWindAudio = CreateDefaultSubobject<UAudioComponent>(TEXT("PropWindAudio"));
	PropWindAudio->SetupAttachment(Root);
	PropWindAudio->bAutoActivate = false;
	PropWindAudio->SetRelativeLocation(FVector(15.0f, 0.0f, 73.0f)); // At the prop
}

const TArray<FString> AParamotorActor::GetParamotorMeshPaths()
{
	return {
		TEXT("/Game/Meshes/Cage_FrontRing.Cage_FrontRing"),
		TEXT("/Game/Meshes/Cage_BackRing.Cage_BackRing"),
		TEXT("/Game/Meshes/Cage_Bar_0.Cage_Bar_0"),
		TEXT("/Game/Meshes/Cage_Bar_1.Cage_Bar_1"),
		TEXT("/Game/Meshes/Cage_Bar_2.Cage_Bar_2"),
		TEXT("/Game/Meshes/Cage_Bar_3.Cage_Bar_3"),
		TEXT("/Game/Meshes/Cage_Bar_4.Cage_Bar_4"),
		TEXT("/Game/Meshes/Cage_Bar_5.Cage_Bar_5"),
		TEXT("/Game/Meshes/Cage_Bar_6.Cage_Bar_6"),
		TEXT("/Game/Meshes/Cage_Bar_7.Cage_Bar_7"),
		TEXT("/Game/Meshes/Net_H_1.Net_H_1"),
		TEXT("/Game/Meshes/Net_H_2.Net_H_2"),
		TEXT("/Game/Meshes/Net_H_3.Net_H_3"),
		TEXT("/Game/Meshes/Net_H_4.Net_H_4"),
		TEXT("/Game/Meshes/Net_H_5.Net_H_5"),
		TEXT("/Game/Meshes/Net_H_6.Net_H_6"),
		TEXT("/Game/Meshes/Net_V_1.Net_V_1"),
		TEXT("/Game/Meshes/Net_V_2.Net_V_2"),
		TEXT("/Game/Meshes/Net_V_3.Net_V_3"),
		TEXT("/Game/Meshes/Net_V_4.Net_V_4"),
		TEXT("/Game/Meshes/Net_V_5.Net_V_5"),
		TEXT("/Game/Meshes/Net_V_6.Net_V_6"),
		TEXT("/Game/Meshes/Frame_TopLeft.Frame_TopLeft"),
		TEXT("/Game/Meshes/Frame_TopRight.Frame_TopRight"),
		TEXT("/Game/Meshes/Frame_BotLeft.Frame_BotLeft"),
		TEXT("/Game/Meshes/Frame_BotRight.Frame_BotRight"),
		TEXT("/Game/Meshes/Frame_SeatLeft.Frame_SeatLeft"),
		TEXT("/Game/Meshes/Frame_SeatRight.Frame_SeatRight"),
		// A-arms, carabiners, rings, risers are built as a hierarchy (not flat)
		TEXT("/Game/Meshes/Engine_Crankcase.Engine_Crankcase"),
		TEXT("/Game/Meshes/Engine_Cylinder.Engine_Cylinder"),
		TEXT("/Game/Meshes/Engine_CylHead.Engine_CylHead"),
		TEXT("/Game/Meshes/Engine_Fin_0.Engine_Fin_0"),
		TEXT("/Game/Meshes/Engine_Fin_1.Engine_Fin_1"),
		TEXT("/Game/Meshes/Engine_Fin_2.Engine_Fin_2"),
		TEXT("/Game/Meshes/Engine_Fin_3.Engine_Fin_3"),
		TEXT("/Game/Meshes/Engine_SparkPlug.Engine_SparkPlug"),
		TEXT("/Game/Meshes/Engine_ReedValve.Engine_ReedValve"),
		TEXT("/Game/Meshes/Engine_Mount.Engine_Mount"),
		TEXT("/Game/Meshes/Carburetor.Carburetor"),
		TEXT("/Game/Meshes/AirFilter.AirFilter"),
		TEXT("/Game/Meshes/FuelTank_Body.FuelTank_Body"),
		TEXT("/Game/Meshes/FuelCap.FuelCap"),
		TEXT("/Game/Meshes/FuelLine.FuelLine"),
		TEXT("/Game/Meshes/Exhaust_Pipe.Exhaust_Pipe"),
		TEXT("/Game/Meshes/Exhaust_Muffler.Exhaust_Muffler"),
		TEXT("/Game/Meshes/Prop_Hub.Prop_Hub"),
		TEXT("/Game/Meshes/Prop_HubCone.Prop_HubCone"),
		TEXT("/Game/Meshes/Prop_Shaft.Prop_Shaft"),
		TEXT("/Game/Meshes/Prop_Blade_0.Prop_Blade_0"),
		TEXT("/Game/Meshes/Prop_Blade_1.Prop_Blade_1"),
		TEXT("/Game/Meshes/PullStart_Housing.PullStart_Housing"),
		TEXT("/Game/Meshes/PullStart_RopeGuide.PullStart_RopeGuide"),
		TEXT("/Game/Meshes/PullStart_Clip.PullStart_Clip"),
		TEXT("/Game/Meshes/PullStart_Shaft.PullStart_Shaft"),
		TEXT("/Game/Meshes/PullStart_Grip.PullStart_Grip"),
		TEXT("/Game/Meshes/PullStart_GripCap1.PullStart_GripCap1"),
		TEXT("/Game/Meshes/PullStart_GripCap2.PullStart_GripCap2"),
		TEXT("/Game/Meshes/PullStart_Rope.PullStart_Rope"),
		TEXT("/Game/Meshes/Harness_Pod.Harness_Pod"),
		TEXT("/Game/Meshes/Harness_SeatPlate.Harness_SeatPlate"),
		TEXT("/Game/Meshes/LegStrap_Left.LegStrap_Left"),
		TEXT("/Game/Meshes/LegStrap_Right.LegStrap_Right"),
		TEXT("/Game/Meshes/Buckle_Leg_Left.Buckle_Leg_Left"),
		TEXT("/Game/Meshes/Buckle_Leg_Right.Buckle_Leg_Right"),
		TEXT("/Game/Meshes/Throttle_Handle.Throttle_Handle"),
		TEXT("/Game/Meshes/Throttle_Trigger.Throttle_Trigger"),
		TEXT("/Game/Meshes/Throttle_Cable.Throttle_Cable"),
		TEXT("/Game/Meshes/WingClip_Stud_Left.WingClip_Stud_Left"),
		TEXT("/Game/Meshes/WingClip_Stud_Right.WingClip_Stud_Right"),
		// Rings, carabiners, risers, quick links, brake toggles built as hierarchy
	};
}


USceneComponent* AParamotorActor::BuildSingleAArmSide(bool bLeft, USceneComponent* Stud)
{
	const FString Side = bLeft ? TEXT("Left") : TEXT("Right");
	const FString SideShort = bLeft ? TEXT("L") : TEXT("R");

	// Helper to load a mesh and create a component attached to a parent
	auto LoadMeshComp = [&](const FString& AssetPath, const FString& CompName,
		USceneComponent* Parent) -> UStaticMeshComponent*
	{
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *AssetPath);
		if (!Mesh)
		{
			UE_LOG(LogParamotor, Log, TEXT("ParamotorActor: Failed to load %s"), *AssetPath);
			return nullptr;
		}
		UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(this, FName(*CompName));
		Comp->SetupAttachment(Parent);
		Comp->SetStaticMesh(Mesh);
		Comp->SetMobility(EComponentMobility::Movable);
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Comp->SetCastShadow(true);
		Comp->RegisterComponent();
		return Comp;
	};

	// Pivot scene component attached to the stud (or root if stud not found)
	USceneComponent* Pivot = NewObject<USceneComponent>(this, FName(*FString::Printf(TEXT("AArmPivot_%s"), *SideShort)));
	Pivot->SetupAttachment(Stud ? Stud : Root);
	Pivot->SetMobility(EComponentMobility::Movable);
	Pivot->RegisterComponent();

	// A-arm mesh, ring, carabiner, riser, brake toggle, quick links
	UStaticMeshComponent* AArmMesh = LoadMeshComp(
		FString::Printf(TEXT("/Game/Meshes/AArm_%s.AArm_%s"), *Side, *Side),
		FString::Printf(TEXT("AArmMesh_%s"), *SideShort), Pivot);
	UStaticMeshComponent* RingMesh = LoadMeshComp(
		FString::Printf(TEXT("/Game/Meshes/WingClip_Ring_%s.WingClip_Ring_%s"), *Side, *Side),
		FString::Printf(TEXT("RingMesh_%s"), *SideShort), Pivot);
	UStaticMeshComponent* CarabinerMesh = LoadMeshComp(
		FString::Printf(TEXT("/Game/Meshes/Carabiner_%s.Carabiner_%s"), *Side, *Side),
		FString::Printf(TEXT("CarabinerMesh_%s"), *SideShort), Pivot);
	// SM_RiserLines is the entire line set (9m wide), not just the 49cm riser strap.
	// Hidden — procedural line mesh handles all line rendering.
	UStaticMeshComponent* RiserMesh = LoadMeshComp(
		TEXT("/Game/Meshes/SM_RiserLines.SM_RiserLines"),
		FString::Printf(TEXT("RiserMesh_%s"), *SideShort), Pivot);
	if (RiserMesh) RiserMesh->SetVisibility(false);
	UStaticMeshComponent* BrakeToggleMesh = LoadMeshComp(
		FString::Printf(TEXT("/Game/Meshes/BrakeToggle_%s.BrakeToggle_%s"), *Side, *Side),
		FString::Printf(TEXT("BrakeToggle_%s"), *SideShort), Pivot);

	// Quick links (A/B/C/D)
	for (const TCHAR* Level : { TEXT("A"), TEXT("B"), TEXT("C"), TEXT("D") })
	{
		FString Key = FString::Printf(TEXT("%s_%s"), Level, *Side);
		QuickLinkMeshes.Add(Key, LoadMeshComp(
			FString::Printf(TEXT("/Game/Meshes/QuickLink_%s_%s.QuickLink_%s_%s"), Level, *Side, Level, *Side),
			FString::Printf(TEXT("QuickLink_%s_%s"), Level, *Side), Pivot));
	}

	// Assign to the correct member variables
	if (bLeft)
	{
		AArmMeshLeft = AArmMesh;
		RingMeshLeft = RingMesh;
		CarabinerMeshLeft = CarabinerMesh;
		RiserMeshLeft = RiserMesh;
		BrakeToggleMeshLeft = BrakeToggleMesh;
	}
	else
	{
		AArmMeshRight = AArmMesh;
		RingMeshRight = RingMesh;
		CarabinerMeshRight = CarabinerMesh;
		RiserMeshRight = RiserMesh;
		BrakeToggleMeshRight = BrakeToggleMesh;
	}

	return Pivot;
}


void AParamotorActor::InitAudio()
{
	// Load sound waves
	USoundBase* StarterSound = LoadObject<USoundBase>(nullptr, TEXT("/Game/Sounds/starter.starter"));
	USoundBase* IdleSound = LoadObject<USoundBase>(nullptr, TEXT("/Game/Sounds/engine_idle.engine_idle"));
	USoundBase* EngineSound = LoadObject<USoundBase>(nullptr, TEXT("/Game/Sounds/engine_running.engine_running"));
	USoundBase* ExhaustSound = LoadObject<USoundBase>(nullptr, TEXT("/Game/Sounds/exhaust.exhaust"));
	USoundBase* PropWindSound = LoadObject<USoundBase>(nullptr, TEXT("/Game/Sounds/prop_wind.prop_wind"));

	if (StarterSound)
	{
		// Set looping on the sound wave so the starter plays continuously while cranking.
		// UE5 UAudioComponent doesn't expose a looping flag, so this is set on the asset.
		if (USoundWave* StarterWave = Cast<USoundWave>(StarterSound))
		{
			StarterWave->bLooping = true;
		}
		StarterAudio->SetSound(StarterSound);
	}
	// Use idle sound for the engine audio (will pitch shift at runtime)
	if (IdleSound) EngineAudio->SetSound(IdleSound);
	if (ExhaustSound) ExhaustAudio->SetSound(ExhaustSound);
	if (PropWindSound) PropWindAudio->SetSound(PropWindSound);

	UE_LOG(LogParamotor, Log, TEXT("ParamotorActor: Audio initialized (starter=%d idle=%d exhaust=%d prop=%d)"),
		StarterSound ? 1 : 0, IdleSound ? 1 : 0, ExhaustSound ? 1 : 0, PropWindSound ? 1 : 0);
}

void AParamotorActor::BeginPlay()
{
	Super::BeginPlay();

	// Ensure fuel starts full
	FuelLevel = 1.0f;

	BuildParamotorMeshes();
	InitAudio();

	PromptWidget->SetWidgetClass(UUW_InteractPrompt::StaticClass());

	InteractionSphere->OnComponentBeginOverlap.AddDynamic(this, &AParamotorActor::OnOverlapBegin);
	InteractionSphere->OnComponentEndOverlap.AddDynamic(this, &AParamotorActor::OnOverlapEnd);
}

void AParamotorActor::Destroyed()
{
	// Attached part actors are NOT owned by attach — they'd be orphaned in
	// the world as floating debris. Clean them up.
	for (AActor* P : CodexPartActors)
	{
		if (P) P->Destroy();
	}
	CodexPartActors.Empty();
	CodexPropActor = nullptr;
	Super::Destroyed();
}

void AParamotorActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Update A-arm pivots to follow the wing
	UpdateAArmPivots();

	// Codex rig hang-swing — the frame/engine/prop hang off the harness like
	// a real paramotor: lean-back under acceleration, sway in turns, settle
	// at rest. Driven by the pilot's acceleration as a damped pendulum.
	if (bEquipped && bUseCodexBuild)
	{
		UpdateHangSwing(DeltaTime);
	}

	if (EngineState == EEngineState::Cranking)
	{
		UpdateCranking(DeltaTime);
	}

	if (EngineState == EEngineState::Running)
	{
		TotalRunTime += DeltaTime;

		// Fuel cutoff kills engine immediately
		if (bFuelCutoff)
		{
			StopEngine();
			UE_LOG(LogParamotor, Log, TEXT("ParamotorActor: Fuel cutoff — engine stopped"));
		}

		UpdateEngineRPM(DeltaTime);
		UpdateEngineTemp(DeltaTime);
		UpdateFuel(DeltaTime);

		// Prop rotation from real angular velocity
		float PropOmega = GetPropRPM() * 2.0f * PI / 60.0f;
		PropRotation += PropOmega * DeltaTime;
		UpdatePropMeshRotation();

		UpdateEngineAudio();
	}
	else if (EngineState == EEngineState::Off)
	{
		UpdateEngineSpoolDown(DeltaTime);
	}
}

void AParamotorActor::UpdateCranking(float DeltaTime)
{
	StarterTimer += DeltaTime;

	// Engine catches ONLY if throttle is being given
	if (Throttle > 0.02f)
	{
		if (StarterTimer >= 1.0f)
		{
			EngineState = EEngineState::Running;
			if (StarterAudio) StarterAudio->Stop();

			if (EngineAudio) EngineAudio->Play();
			if (ExhaustAudio) ExhaustAudio->Play();
			if (PropWindAudio) PropWindAudio->Play();

			// Idle volume - paramotors are LOUD (~100+ dB at idle)
			if (EngineAudio) EngineAudio->SetVolumeMultiplier(1.5f);
			if (EngineAudio) EngineAudio->SetPitchMultiplier(1.0f);
			if (ExhaustAudio) ExhaustAudio->SetVolumeMultiplier(1.2f);
			if (ExhaustAudio) ExhaustAudio->SetPitchMultiplier(1.0f);
			if (PropWindAudio) PropWindAudio->SetVolumeMultiplier(0.3f);
			if (PropWindAudio) PropWindAudio->SetPitchMultiplier(0.28f);

			UE_LOG(LogParamotor, Log, TEXT("ParamotorActor: Engine caught! Running at throttle=%.2f"), Throttle);
		}
	}

	// If cranking too long without throttle, give up (starter thermal limit)
	if (StarterTimer >= 8.0f)
	{
		StopCranking();
		UE_LOG(LogParamotor, Log, TEXT("ParamotorActor: Starter gave up (no throttle)"));
	}
}

void AParamotorActor::UpdateEngineRPM(float DeltaTime)
{
	// J·dω/dt = τ_engine(ω, throttle) − τ_prop(ω, airspeed)
	float EngineOmega = EngineRPM * 2.0f * PI / 60.0f;
	float PropOmega = GetPropRPM() * 2.0f * PI / 60.0f;

	// Engine torque from throttle combustion
	float EffectiveThrottle = FMath::Max(0.0f, (Throttle - 0.1f) / 0.9f);
	float EnginePowerW = 18500.0f * EffectiveThrottle * FMath::Max(0.0f, EngineRPM / EngineRedlineRPM);
	float EngineTorque = (EngineOmega > 10.0f) ? EnginePowerW / EngineOmega : 0.0f;

	// Idle torque: ~5.7 N·m at 2500 RPM (1.5 kW idle power)
	float IdlePowerW = 1500.0f;
	float IdleOmega = EngineIdleRPM * 2.0f * PI / 60.0f;
	float IdleTorque = (EngineRPM < EngineIdleRPM) ? IdlePowerW / FMath::Max(1.0f, IdleOmega) : 0.0f;

	// Propeller torque from blade element theory
	float AirspeedForTorque = 0.0f;
	if (AActor* ParentActor = GetAttachParentActor())
		AirspeedForTorque = ParentActor->GetVelocity().Size()  / CM_PER_M;
	float PropTorque = GetPropTorqueNM(AirspeedForTorque);
	float PropTorqueAtEngine = PropTorque / PROP_REDUCTION_RATIO;

	// Net torque → angular acceleration → RPM change
	float EngineInertia = 0.12f;  // kg·m² (crank + flywheel + prop reflected)
	float NetTorque = EngineTorque + IdleTorque - PropTorqueAtEngine;
	float AngularAccel = NetTorque / EngineInertia;
	EngineOmega += AngularAccel * DeltaTime;
	EngineRPM = EngineOmega * 60.0f / (2.0f * PI);
	EngineRPM = FMath::Clamp(EngineRPM, 0.0f, EngineRedlineRPM);
}

void AParamotorActor::UpdateEngineTemp(float DeltaTime)
{
	// CHT: idle ~75C, full throttle ~110C, thermal tau ~100s
	float TargetTemp = 75.0f + (EngineRPM / EngineRedlineRPM) * 35.0f;
	EngineTemp = FMath::FInterpTo(EngineTemp, TargetTemp, DeltaTime, 0.01f);

	// EGT: idle ~450C, full throttle ~650C, responds fast (~0.5s)
	float TargetEGT = 450.0f + (EngineRPM / EngineRedlineRPM) * 200.0f;
	EGT = FMath::FInterpTo(EGT, TargetEGT, DeltaTime, 2.0f);
}

void AParamotorActor::UpdateFuel(float DeltaTime)
{
	// BSFC ≈ 0.4 kg/kW·h, gasoline density ≈ 0.75 kg/L, tank = 10L
	float BSFC = 0.4f;
	float FuelDensity = 0.75f;
	float TankVolumeL = 10.0f;
	float EffectiveThrottle = FMath::Max(0.0f, (Throttle - 0.1f) / 0.9f);
	float EnginePowerW = 18500.0f * EffectiveThrottle * FMath::Max(0.0f, EngineRPM / EngineRedlineRPM);
	float PowerKW = EnginePowerW / 1000.0f;
	float FuelFlowLPerS = PowerKW * BSFC / (3600.0f * FuelDensity);
	float FuelRate = FuelFlowLPerS / TankVolumeL;
	FuelLevel -= FuelRate * DeltaTime;
	FuelLevel = FMath::Clamp(FuelLevel, 0.0f, 1.0f);

	if (FuelLevel <= 0.0f)
	{
		StopEngine();
		UE_LOG(LogParamotor, Log, TEXT("ParamotorActor: Out of fuel! Engine stopped"));
	}
}

void AParamotorActor::UpdateEngineAudio()
{
	float RPMRatio = (EngineRPM - EngineIdleRPM) / (EngineRedlineRPM - EngineIdleRPM);
	float Pitch = 1.0f + RPMRatio * 1.5f;
	float EngineVol = 0.8f + Throttle * 1.2f;
	float ExhaustVol = 0.6f + Throttle * 1.4f;

	if (EngineAudio)
	{
		EngineAudio->SetPitchMultiplier(Pitch);
		EngineAudio->SetVolumeMultiplier(EngineVol);
	}
	if (ExhaustAudio)
	{
		ExhaustAudio->SetPitchMultiplier(Pitch * 0.85f);
		ExhaustAudio->SetVolumeMultiplier(ExhaustVol);
	}

	float PropWindVol = 0.05f + Throttle * 1.45f;
	float PropWindPitch = 0.28f + RPMRatio * 0.72f;
	if (PropWindAudio)
	{
		PropWindAudio->SetVolumeMultiplier(PropWindVol);
		PropWindAudio->SetPitchMultiplier(PropWindPitch);
	}
}

void AParamotorActor::UpdateEngineSpoolDown(float DeltaTime)
{
	// Prop slows down and stops when not running
	if (EngineRPM > 0.0f)
	{
		EngineRPM = FMath::FInterpTo(EngineRPM, 0.0f, DeltaTime, 0.5f);
		float PropOmega = GetPropRPM() * 2.0f * PI / 60.0f;
		PropRotation += PropOmega * DeltaTime;
		UpdatePropMeshRotation();
	}

	// Engine cools down when off
	if (EngineTemp > 20.0f)
	{
		EngineTemp = FMath::FInterpTo(EngineTemp, 20.0f, DeltaTime, 0.01f);
	}
}

void AParamotorActor::OnOverlapBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep,
	const FHitResult& SweepResult)
{
	if (bEquipped) return;

	AParamotorCharacter* Player = Cast<AParamotorCharacter>(OtherActor);
	if (Player)
	{
		OverlappingPlayer = Player;
		PromptWidget->SetVisibility(true);
		UE_LOG(LogParamotor, Log, TEXT("ParamotorActor: Player entered range"));
	}
}

void AParamotorActor::OnOverlapEnd(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	AParamotorCharacter* Player = Cast<AParamotorCharacter>(OtherActor);
	if (Player && Player == OverlappingPlayer)
	{
		OverlappingPlayer = nullptr;
		PromptWidget->SetVisibility(false);
		UE_LOG(LogParamotor, Log, TEXT("ParamotorActor: Player left range"));
	}
}

void AParamotorActor::Interact(AParamotorCharacter* PlayerCharacter)
{
	if (bEquipped || !PlayerCharacter) return;

	USkeletalMeshComponent* CharacterMesh = PlayerCharacter->GetMesh();
	if (!CharacterMesh)
	{
		UE_LOG(LogParamotor, Error, TEXT("ParamotorActor: No character mesh!"));
		return;
	}

	if (bUseCodexBuild)
	{
		// Bone attachment — the rig rides the torso and follows animation
		// instead of hanging on the mesh root.
		FName AttachBone = TEXT("spine_02");
		// Character-space fit (tuned via equip_offset.txt): 70cm behind the
		// spine puts the cage plane just behind the shoulder blades, 33cm
		// below capsule center puts the seat at the hips.
		EquipBaseLoc = FVector(70.0f, 0.0f, -33.0f);
		EquipBaseRot = FRotator(0.0f, 0.0f, 0.0f);

		// Dev tuning override: C:\DevinTools\equip_offset.txt
		//   line 1: "bone_name x y z pitch yaw roll"  (actor attach)
		//   line 2: "hang x y z k c maxdeg"           (swing pivot + params)
		//   line 3: "harness x y z pitch yaw roll"    (harness fit offset)
		FString Tune;
		if (FFileHelper::LoadFileToString(Tune, TEXT("C:/DevinTools/equip_offset.txt")))
		{
			TArray<FString> Lines;
			Tune.ParseIntoArrayLines(Lines);
			if (Lines.IsValidIndex(0))
			{
				TArray<FString> F;
				Lines[0].TrimStartAndEnd().ParseIntoArray(F, TEXT(" "), true);
				if (F.Num() >= 7)
				{
					AttachBone = FName(*F[0]);
					EquipBaseLoc = FVector(FCString::Atof(*F[1]), FCString::Atof(*F[2]), FCString::Atof(*F[3]));
					EquipBaseRot = FRotator(FCString::Atof(*F[4]), FCString::Atof(*F[5]), FCString::Atof(*F[6]));
					UE_LOG(LogParamotor, Log, TEXT("EquipTune: bone=%s loc=%s rot=%s"), *AttachBone.ToString(), *EquipBaseLoc.ToString(), *EquipBaseRot.ToString());
				}
			}
			if (Lines.IsValidIndex(1) && HangPivot)
			{
				TArray<FString> F;
				Lines[1].TrimStartAndEnd().ParseIntoArray(F, TEXT(" "), true);
				if (F.Num() >= 7 && F[0].Equals(TEXT("hang"), ESearchCase::IgnoreCase))
				{
					const FVector NewHang(FCString::Atof(*F[1]), FCString::Atof(*F[2]), FCString::Atof(*F[3]));
					// Relocate the pivot without moving its children — fix each
					// hanging part's relative offset by the pivot delta.
					for (AActor* P : CodexPartActors)
						if (P && P != CodexHarnessActor && P->GetRootComponent() && P->GetRootComponent()->GetAttachParent() == HangPivot)
							P->GetRootComponent()->SetRelativeLocation(P->GetRootComponent()->GetRelativeLocation() + (HangPivotLoc - NewHang));
					HangPivot->SetRelativeLocation(NewHang);
					HangPivotLoc = NewHang;
					HangSwingK = FCString::Atof(*F[4]); HangSwingC = FCString::Atof(*F[5]); HangSwingMax = FCString::Atof(*F[6]);
					UE_LOG(LogParamotor, Log, TEXT("EquipTune: hang=%s k=%.1f c=%.1f max=%.1f"), *NewHang.ToString(), HangSwingK, HangSwingC, HangSwingMax);
				}
			}
			if (Lines.IsValidIndex(2) && CodexHarnessActor)
			{
				TArray<FString> F;
				Lines[2].TrimStartAndEnd().ParseIntoArray(F, TEXT(" "), true);
				if (F.Num() >= 7 && F[0].Equals(TEXT("harness"), ESearchCase::IgnoreCase))
				{
					HarnessOffset = FVector(FCString::Atof(*F[1]), FCString::Atof(*F[2]), FCString::Atof(*F[3]));
					HarnessRotOffset = FRotator(FCString::Atof(*F[4]), FCString::Atof(*F[5]), FCString::Atof(*F[6]));
					const FTransform Rel = CodexParts::RelTransform(EHangarPart::Harness);
					CodexHarnessActor->GetRootComponent()->SetRelativeLocation(Rel.GetLocation() + HarnessOffset);
					CodexHarnessActor->GetRootComponent()->SetRelativeRotation(FQuat(HarnessRotOffset) * Rel.GetRotation());
					UE_LOG(LogParamotor, Log, TEXT("EquipTune: harness off=%s rot=%s"), *HarnessOffset.ToString(), *HarnessRotOffset.ToString());
				}
			}
		}

		FAttachmentTransformRules AttachRules(EAttachmentRule::SnapToTarget, EAttachmentRule::SnapToTarget, EAttachmentRule::KeepWorld, false);
		if (CharacterMesh->GetBoneIndex(AttachBone) == INDEX_NONE)
		{
			UE_LOG(LogParamotor, Warning, TEXT("ParamotorActor: bone %s not found — mesh-root attach"), *AttachBone.ToString());
			AttachToComponent(CharacterMesh, AttachRules);
		}
		else
		{
			AttachToComponent(CharacterMesh, AttachRules, AttachBone);
		}

		// Place in CHARACTER space — the rig still follows the bone during
		// animation, but the fit offset is expressed in body axes instead of
		// opaque bone axes: Back = cm behind the spine, Lat = lateral, Up = cm
		// above the capsule center, yaw offset from facing direction.
		const FTransform CharTM = PlayerCharacter->GetActorTransform();
		const FVector CharFwd = PlayerCharacter->GetActorForwardVector();
		const FVector CharRt  = PlayerCharacter->GetActorRightVector();
		const FVector DesiredLoc = CharTM.GetLocation()
			- CharFwd * EquipBaseLoc.X + CharRt * EquipBaseLoc.Y + FVector::UpVector * EquipBaseLoc.Z;
		const FRotator DesiredRot(0.0f, PlayerCharacter->GetActorRotation().Yaw + EquipBaseRot.Yaw, 0.0f);
		SetActorLocation(DesiredLoc);
		SetActorRotation(DesiredRot);
		UE_LOG(LogParamotor, Log, TEXT("EquipAttach: world=%s rot=%s"), *DesiredLoc.ToString(), *DesiredRot.ToString());
	}
	else
	{
		// Original procedural paramotor — attach to mesh root as before
		FAttachmentTransformRules AttachRules(EAttachmentRule::SnapToTarget, EAttachmentRule::SnapToTarget, EAttachmentRule::KeepWorld, false);
		AttachToComponent(CharacterMesh, AttachRules);
		SetActorRelativeLocation(FVector(0.0f, -25.0f, 40.0f));
		SetActorRelativeRotation(FRotator(0.0f, 270.0f, 0.0f));
	}

	bEquipped = true;
	bHasPrevPilotVel = false;  // Reset the hang-swing acceleration tracker
	PromptWidget->SetVisibility(false);
	InteractionSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	UE_LOG(LogParamotor, Log, TEXT("ParamotorActor: Equipped by player!"));
}


void AParamotorActor::StartCranking()
{
	// Only start cranking from Off state (ignore repeated key presses)
	if (EngineState != EEngineState::Off) return;

	EngineState = EEngineState::Cranking;
	StarterTimer = 0.0f;

	// Play starter sound (realistic 4-stroke cranking) - loops while cranking
	// Real paramotor starters are LOUD — grinding, whirring, clunking
	if (StarterAudio)
	{
		StarterAudio->SetVolumeMultiplier(5.0f); // VERY loud cranking
		StarterAudio->SetPitchMultiplier(1.0f);
		StarterAudio->Play();
		UE_LOG(LogParamotor, Log, TEXT("ParamotorActor: Starter audio playing (sound=%s)"),
			StarterAudio->GetSound() ? *StarterAudio->GetSound()->GetName() : TEXT("NULL"));
	}
	else
	{
		UE_LOG(LogParamotor, Error, TEXT("ParamotorActor: StarterAudio component is NULL!"));
	}

	UE_LOG(LogParamotor, Log, TEXT("ParamotorActor: Cranking starter... (give throttle to start)"));
}

void AParamotorActor::StopCranking()
{
	// Only stop if currently cranking (engine may have already caught)
	if (EngineState != EEngineState::Cranking) return;

	EngineState = EEngineState::Off;
	StarterTimer = 0.0f;

	if (StarterAudio) StarterAudio->Stop();

	UE_LOG(LogParamotor, Log, TEXT("ParamotorActor: Cranking stopped"));
}

void AParamotorActor::StopEngine()
{
	if (EngineState == EEngineState::Off) return;

	EngineState = EEngineState::Off;
	Throttle = 0.0f;
	StarterTimer = 0.0f;

	if (StarterAudio) StarterAudio->Stop();
	if (EngineAudio) EngineAudio->Stop();
	if (ExhaustAudio) ExhaustAudio->Stop();
	if (PropWindAudio) PropWindAudio->Stop();

	UE_LOG(LogParamotor, Log, TEXT("ParamotorActor: Engine stopped (temp=%.1fC fuel=%.0f%%)"), EngineTemp, FuelLevel * PERCENT_FACTOR);
}

void AParamotorActor::ToggleFuelCutoff()
{
	bFuelCutoff = !bFuelCutoff;
	UE_LOG(LogParamotor, Log, TEXT("ParamotorActor: Fuel cutoff %s"), bFuelCutoff ? TEXT("ON — engine will die") : TEXT("OFF — fuel flowing"));
}

void AParamotorActor::SetThrottle(float NewThrottle)
{
	float OldThrottle = Throttle;
	Throttle = FMath::Clamp(NewThrottle, 0.0f, 1.0f);
	// Only log when throttle changes significantly
	if (FMath::Abs(Throttle - OldThrottle) > 0.05f)
	{
		UE_LOG(LogParamotor, Log, TEXT("ParamotorActor: Throttle set to %.2f"), Throttle);
	}
}
