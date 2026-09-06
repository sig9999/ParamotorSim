#include "ParamotorActor.h"
#include "WingActor.h"
#include "ParamotorCharacter.h"
#include "UW_InteractPrompt.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Components/WidgetComponent.h"
#include "Components/AudioComponent.h"
#include "Blueprint/UserWidget.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundWave.h"
#include "Misc/Paths.h"

AParamotorActor::AParamotorActor()
{
	PrimaryActorTick.bCanEverTick = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

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

	WingInflateAudio = CreateDefaultSubobject<UAudioComponent>(TEXT("WingInflateAudio"));
	WingInflateAudio->SetupAttachment(Root);
	WingInflateAudio->bAutoActivate = false;
	WingInflateAudio->SetRelativeLocation(FVector(0.0f, 0.0f, 200.0f));
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

void AParamotorActor::BuildParamotorMeshes()
{
	const TArray<FString> MeshPaths = GetParamotorMeshPaths();

	for (int32 i = 0; i < MeshPaths.Num(); i++)
	{
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *MeshPaths[i]);
		if (!Mesh) continue;

		FString BaseName = FPaths::GetBaseFilename(MeshPaths[i]);
		FString CompName = FString::Printf(TEXT("Mesh_%d_%s"), i, *BaseName);
		UStaticMeshComponent* MeshComp = NewObject<UStaticMeshComponent>(this, FName(*CompName));
		MeshComp->SetupAttachment(Root);
		MeshComp->SetStaticMesh(Mesh);
		MeshComp->SetMobility(EComponentMobility::Movable);
		MeshComp->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		MeshComp->SetCollisionResponseToAllChannels(ECR_Ignore);
		MeshComp->RegisterComponent();
		MeshComponents.Add(MeshComp);

		// Track prop parts for rotation
		if (BaseName.StartsWith(TEXT("Prop_Blade")) || BaseName == TEXT("Prop_Hub") ||
			BaseName == TEXT("Prop_HubCone"))
		{
			PropMeshes.Add(MeshComp);
			PropMeshBaseTransforms.Add(MeshComp->GetRelativeTransform());  // Store base for rotation
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("ParamotorActor: Built %d mesh components, %d prop meshes"),
		MeshComponents.Num(), PropMeshes.Num());

	// === BUILD A-ARM PIVOT HIERARCHY ===
	// Real paramotor connection:
	//   Frame → Stud (fixed) → AArm (PIVOTS) → Ring → Carabiner → Risers → QuickLinks → Lines
	// The A-arms pivot freely so the wing can swing independently of the frame.
	BuildAArmHierarchy();
}

void AParamotorActor::BuildAArmHierarchy()
{
	// Helper to load a mesh and create a component attached to a parent
	auto LoadMeshComp = [&](const FString& AssetPath, const FString& CompName,
		USceneComponent* Parent) -> UStaticMeshComponent*
	{
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *AssetPath);
		if (!Mesh)
		{
			UE_LOG(LogTemp, Warning, TEXT("ParamotorActor: Failed to load %s"), *AssetPath);
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

	// Find the stud mesh components (pivot attachment points on the frame)
	UStaticMeshComponent* StudLeft = nullptr;
	UStaticMeshComponent* StudRight = nullptr;
	for (UStaticMeshComponent* Comp : MeshComponents)
	{
		if (!Comp) continue;
		FString Name = Comp->GetName();
		if (Name.Contains(TEXT("WingClip_Stud_Left"))) StudLeft = Comp;
		else if (Name.Contains(TEXT("WingClip_Stud_Right"))) StudRight = Comp;
	}

	// === LEFT A-ARM HIERARCHY ===
	// Pivot scene component attached to the stud (or root if stud not found)
	AArmPivotLeft = NewObject<USceneComponent>(this, TEXT("AArmPivot_Left"));
	AArmPivotLeft->SetupAttachment(StudLeft ? (USceneComponent*)StudLeft : Root);
	AArmPivotLeft->SetMobility(EComponentMobility::Movable);
	AArmPivotLeft->RegisterComponent();

	// A-arm mesh attached to pivot
	AArmMeshLeft = LoadMeshComp(TEXT("/Game/Meshes/AArm_Left.AArm_Left"), TEXT("AArmMesh_Left"), AArmPivotLeft);
	// Ring at bottom of A-arm
	RingMeshLeft = LoadMeshComp(TEXT("/Game/Meshes/WingClip_Ring_Left.WingClip_Ring_Left"), TEXT("RingMesh_Left"), AArmPivotLeft);
	// Carabiner clips through ring
	CarabinerMeshLeft = LoadMeshComp(TEXT("/Game/Meshes/Carabiner_Left.Carabiner_Left"), TEXT("CarabinerMesh_Left"), AArmPivotLeft);
	// Riser webbing straps (from carabiner toward wing)
	RiserMeshLeft = LoadMeshComp(TEXT("/Game/Meshes/SM_RiserLines.SM_RiserLines"), TEXT("RiserMesh_Left"), AArmPivotLeft);
	// Brake toggle handle
	BrakeToggleMeshLeft = LoadMeshComp(TEXT("/Game/Meshes/BrakeToggle_Left.BrakeToggle_Left"), TEXT("BrakeToggle_Left"), AArmPivotLeft);
	// Quick links (A/B/C/D)
	QuickLinkMeshes.Add(TEXT("A_Left"), LoadMeshComp(TEXT("/Game/Meshes/QuickLink_A_Left.QuickLink_A_Left"), TEXT("QuickLink_A_Left"), AArmPivotLeft));
	QuickLinkMeshes.Add(TEXT("B_Left"), LoadMeshComp(TEXT("/Game/Meshes/QuickLink_B_Left.QuickLink_B_Left"), TEXT("QuickLink_B_Left"), AArmPivotLeft));
	QuickLinkMeshes.Add(TEXT("C_Left"), LoadMeshComp(TEXT("/Game/Meshes/QuickLink_C_Left.QuickLink_C_Left"), TEXT("QuickLink_C_Left"), AArmPivotLeft));
	QuickLinkMeshes.Add(TEXT("D_Left"), LoadMeshComp(TEXT("/Game/Meshes/QuickLink_D_Left.QuickLink_D_Left"), TEXT("QuickLink_D_Left"), AArmPivotLeft));

	// === RIGHT A-ARM HIERARCHY ===
	AArmPivotRight = NewObject<USceneComponent>(this, TEXT("AArmPivot_Right"));
	AArmPivotRight->SetupAttachment(StudRight ? (USceneComponent*)StudRight : Root);
	AArmPivotRight->SetMobility(EComponentMobility::Movable);
	AArmPivotRight->RegisterComponent();

	AArmMeshRight = LoadMeshComp(TEXT("/Game/Meshes/AArm_Right.AArm_Right"), TEXT("AArmMesh_Right"), AArmPivotRight);
	RingMeshRight = LoadMeshComp(TEXT("/Game/Meshes/WingClip_Ring_Right.WingClip_Ring_Right"), TEXT("RingMesh_Right"), AArmPivotRight);
	CarabinerMeshRight = LoadMeshComp(TEXT("/Game/Meshes/Carabiner_Right.Carabiner_Right"), TEXT("CarabinerMesh_Right"), AArmPivotRight);
	RiserMeshRight = LoadMeshComp(TEXT("/Game/Meshes/SM_RiserLines.SM_RiserLines"), TEXT("RiserMesh_Right"), AArmPivotRight);
	BrakeToggleMeshRight = LoadMeshComp(TEXT("/Game/Meshes/BrakeToggle_Right.BrakeToggle_Right"), TEXT("BrakeToggle_Right"), AArmPivotRight);
	QuickLinkMeshes.Add(TEXT("A_Right"), LoadMeshComp(TEXT("/Game/Meshes/QuickLink_A_Right.QuickLink_A_Right"), TEXT("QuickLink_A_Right"), AArmPivotRight));
	QuickLinkMeshes.Add(TEXT("B_Right"), LoadMeshComp(TEXT("/Game/Meshes/QuickLink_B_Right.QuickLink_B_Right"), TEXT("QuickLink_B_Right"), AArmPivotRight));
	QuickLinkMeshes.Add(TEXT("C_Right"), LoadMeshComp(TEXT("/Game/Meshes/QuickLink_C_Right.QuickLink_C_Right"), TEXT("QuickLink_C_Right"), AArmPivotRight));
	QuickLinkMeshes.Add(TEXT("D_Right"), LoadMeshComp(TEXT("/Game/Meshes/QuickLink_D_Right.QuickLink_D_Right"), TEXT("QuickLink_D_Right"), AArmPivotRight));

	UE_LOG(LogTemp, Warning, TEXT("ParamotorActor: Built A-arm hierarchy (L/R pivots + meshes)"));
}

void AParamotorActor::UpdateWingTarget(const FVector& WingWorldPos, bool bConnected)
{
	WingTargetPos = WingWorldPos;
	bWingConnected = bConnected;
}

void AParamotorActor::UpdateAArmPivots()
{
	if (!AArmPivotLeft || !AArmPivotRight) return;

	if (!bWingConnected)
	{
		// No wing connected — A-arms hang straight down
		AArmPivotLeft->SetRelativeRotation(FRotator(0, 0, 0));
		AArmPivotRight->SetRelativeRotation(FRotator(0, 0, 0));
		return;
	}

	// Get pivot world positions
	FVector PivotLeftPos = AArmPivotLeft->GetComponentLocation();
	FVector PivotRightPos = AArmPivotRight->GetComponentLocation();

	// Direction from each pivot to the wing
	FVector ToWingLeft = WingTargetPos - PivotLeftPos;
	FVector ToWingRight = WingTargetPos - PivotRightPos;

	// The A-arm should point toward the wing, but only the pitch (forward/back) and
	// yaw (left/right) rotation. The A-arm pivots from the stud at the top.
	// In local space: X = forward, Y = right, Z = up
	// The A-arm hangs down (-Z) and swings toward the wing.

	// Convert wing direction to local space of the paramotor
	FVector LocalToWing = GetActorTransform().InverseTransformVector(ToWingLeft.GetSafeNormal());

	// Pitch: how far forward/back the wing is (X component)
	// Yaw: how far left/right the wing is (Y component)
	float PitchDeg = FMath::Atan2(LocalToWing.X, FMath::Abs(LocalToWing.Z)) * 180.0f / PI;
	float YawDeg = FMath::Atan2(LocalToWing.Y, FMath::Abs(LocalToWing.Z)) * 180.0f / PI;

	// Clamp to reasonable A-arm swing range (±60° pitch, ±30° yaw)
	PitchDeg = FMath::Clamp(PitchDeg, -60.0f, 60.0f);
	YawDeg = FMath::Clamp(YawDeg, -30.0f, 30.0f);

	// Apply rotation (pitch around Y axis, yaw around Z axis)
	AArmPivotLeft->SetRelativeRotation(FRotator(PitchDeg, YawDeg, 0));
	AArmPivotRight->SetRelativeRotation(FRotator(PitchDeg, -YawDeg, 0));
}

void AParamotorActor::InitAudio()
{
	// Load sound waves
	USoundBase* StarterSound = LoadObject<USoundBase>(nullptr, TEXT("/Game/Sounds/starter.starter"));
	USoundBase* IdleSound = LoadObject<USoundBase>(nullptr, TEXT("/Game/Sounds/engine_idle.engine_idle"));
	USoundBase* EngineSound = LoadObject<USoundBase>(nullptr, TEXT("/Game/Sounds/engine_running.engine_running"));
	USoundBase* ExhaustSound = LoadObject<USoundBase>(nullptr, TEXT("/Game/Sounds/exhaust.exhaust"));
	USoundBase* PropWindSound = LoadObject<USoundBase>(nullptr, TEXT("/Game/Sounds/prop_wind.prop_wind"));
	USoundBase* WingInflateSound = LoadObject<USoundBase>(nullptr, TEXT("/Game/Sounds/wing_inflate.wing_inflate"));

	if (StarterSound)
	{
		// Enable looping on the starter sound wave so it plays continuously while cranking
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
	if (WingInflateSound) WingInflateAudio->SetSound(WingInflateSound);

	UE_LOG(LogTemp, Warning, TEXT("ParamotorActor: Audio initialized (starter=%d idle=%d exhaust=%d prop=%d wing=%d)"),
		StarterSound ? 1 : 0, IdleSound ? 1 : 0, ExhaustSound ? 1 : 0, PropWindSound ? 1 : 0, WingInflateSound ? 1 : 0);
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

void AParamotorActor::UpdatePropMeshRotation()
{
	FQuat PropQuat = FQuat(FVector::ForwardVector, PropRotation);
	FVector PropCenter = FVector(15.0f, 0.0f, 73.0f);
	for (int32 i = 0; i < PropMeshes.Num(); i++)
	{
		if (i >= PropMeshBaseTransforms.Num()) break;
		FTransform BaseTransform = PropMeshBaseTransforms[i];
		FVector RelPos = BaseTransform.GetTranslation();
		FVector Offset = RelPos - PropCenter;
		FVector NewOffset = PropQuat.RotateVector(Offset);
		FVector NewPos = PropCenter + NewOffset;
		FQuat NewRot = PropQuat * BaseTransform.GetRotation();
		PropMeshes[i]->SetRelativeLocationAndRotation(NewPos, NewRot);
	}
}

void AParamotorActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Update A-arm pivots to follow the wing
	UpdateAArmPivots();

	if (EngineState == EEngineState::Cranking)
	{
		StarterTimer += DeltaTime;

		// Engine catches ONLY if throttle is being given
		if (Throttle > 0.02f)  // Min throttle for combustion
		{
			// Engine catches after cranking for at least 1.5 seconds with throttle
			// (you need to hear the starter cranking before it catches)
			if (StarterTimer >= 1.0f)  // Cranking time before catch
			{
				EngineState = EEngineState::Running;
				StarterAudio->Stop();

				// Start engine idle, exhaust, and prop wind sounds
				EngineAudio->Play();
				ExhaustAudio->Play();
				PropWindAudio->Play();

				// Idle volume - paramotors are LOUD (~100+ dB at idle)
				EngineAudio->SetVolumeMultiplier(1.5f);
				EngineAudio->SetPitchMultiplier(1.0f);
				ExhaustAudio->SetVolumeMultiplier(1.2f);
				ExhaustAudio->SetPitchMultiplier(1.0f);
				// Prop wind at idle - still audible
				PropWindAudio->SetVolumeMultiplier(0.3f);
				PropWindAudio->SetPitchMultiplier(0.28f);

				UE_LOG(LogTemp, Warning, TEXT("ParamotorActor: Engine caught! Running at throttle=%.2f"), Throttle);
			}
		}

		// If cranking too long without throttle, give up after 5 seconds
		if (StarterTimer >= 8.0f)  // Starter thermal limit
		{
			StopCranking();
			UE_LOG(LogTemp, Warning, TEXT("ParamotorActor: Starter gave up (no throttle)"));
		}
	}

	if (EngineState == EEngineState::Running)
	{
		TotalRunTime += DeltaTime;

		// Fuel cutoff kills engine immediately
		if (bFuelCutoff)
		{
			StopEngine();
			UE_LOG(LogTemp, Warning, TEXT("ParamotorActor: Fuel cutoff — engine stopped"));
		}

		// === ENGINE RPM DYNAMICS (real torque balance) ===
		// J·dω/dt = τ_engine(ω, throttle) − τ_prop(ω, airspeed)
		// The engine produces torque from combustion, the propeller absorbs
		// torque from aerodynamic drag. RPM settles where they balance.
		//
		// Moster 185 specs:
		// - Peak power: 18.5 kW at 7800 RPM
		// - Idle: ~2500 RPM
		// - Reduction ratio: 2.58:1
		//
		// Engine torque: τ_engine = P / ω (power / angular velocity)
		// At peak: τ = 18500 / (7800 × 2π/60) = 22.6 N·m
		// Prop torque: τ_prop = P_prop / ω_prop (absorbed by aerodynamic drag)
		// Inertia: J ≈ 0.05 kg·m² (engine + prop + reduction)
		float IdleRPMVal = IDLE_RPM;  // Use project constant (was hardcoded 2500.0f)
		float EngineOmega = EngineRPM * 2.0f * PI / 60.0f;  // rad/s
		float PropOmega = GetPropRPM() * 2.0f * PI / 60.0f;

		// Engine torque: produces torque from throttle combustion
		// At idle: just enough to overcome internal friction
		// At full throttle: peak torque at peak RPM
		float EffectiveThrottle = FMath::Max(0.0f, (Throttle - 0.1f) / 0.9f);  // Consistent with thrust API
		float EnginePowerW = 18500.0f * EffectiveThrottle * FMath::Max(0.0f, EngineRPM / REDLINE_RPM);  // Power scales with RPM
		float EngineTorque = (EngineOmega > 10.0f) ? EnginePowerW / EngineOmega : 0.0f;

		// Idle torque: engine produces minimum torque from idle fuel flow
		// Real Moster 185 idle: ~2500 RPM, ~1.5 kW (typical 2-stroke idle power)
		// τ_idle = P_idle / ω_idle = 1500 / (2500 × 2π/60) ≈ 5.7 N·m
		float IdlePowerW = 1500.0f;  // Real idle power for Moster 185
		float IdleOmega = IdleRPMVal * 2.0f * PI / 60.0f;
		float IdleTorque = (EngineRPM < IdleRPMVal) ? IdlePowerW / FMath::Max(1.0f, IdleOmega) : 0.0f;

		// Propeller torque: from blade element theory (same model as thrust)
		// τ_prop = Σ (dL×sin(φ) + dD×cos(φ)) × r for each blade element
		float AirspeedForTorque = 0.0f;
		// Use GetAttachParentActor() (the pilot) for velocity — attachment doesn't set Owner
		if (AActor* ParentActor = GetAttachParentActor())
			AirspeedForTorque = ParentActor->GetVelocity().Size() / 100.0f;
		float PropTorque = GetPropTorqueNM(AirspeedForTorque);
		// Prop torque is at the propeller shaft — divide by reduction ratio to get
		// engine-shaft torque (the engine sees less torque through the gear reduction)
		float PropTorqueAtEngine = PropTorque / PROP_REDUCTION_RATIO;

		// Engine rotational inertia (kg·m²) — real Moster 185 value
		// Crank + flywheel: ~0.03 kg·m². Propeller (1.3m, 1.5kg): I = m×r²/3 ≈ 0.085 kg·m²
		// Reduction gear adds ~0.005. Total ≈ 0.12 kg·m² (reflected to engine side)
		float EngineInertia = 0.12f;

		// Net torque → angular acceleration → RPM change
		float NetTorque = EngineTorque + IdleTorque - PropTorqueAtEngine;
		float AngularAccel = NetTorque / EngineInertia;  // rad/s²
		EngineOmega += AngularAccel * DeltaTime;
		EngineRPM = EngineOmega * 60.0f / (2.0f * PI);
		EngineRPM = FMath::Clamp(EngineRPM, 0.0f, REDLINE_RPM);

		// === Engine temperature (CHT - cylinder head temp) ===
		// Target temp depends on RPM: idle ~75C, full throttle ~110C
		float TargetTemp = 75.0f + (EngineRPM / REDLINE_RPM) * 35.0f;
		// Thermal response: τ = m_cp / (h × A) where m=engine mass, cp=specific heat,
		// h=convective coefficient, A=surface area. For a small 2-stroke head:
		// m≈2kg, cp≈500 J/kg·K, h≈50 W/m²·K, A≈0.05 m² → τ ≈ 400s
		// FInterpTo rate = 1/τ ≈ 0.0025/s. But we want faster gameplay response,
		// so use the actual cooling rate (h×A/(m×cp)) which gives ~0.0025/s
		EngineTemp = FMath::FInterpTo(EngineTemp, TargetTemp, DeltaTime, 0.01f);  // Physical: tau~100s (m*cp/(h*A))

		// === EGT (exhaust gas temperature) ===
		// EGT is higher than CHT and responds faster (smaller thermal mass)
		// Idle ~450C, full throttle ~650C
		// EGT thermal mass: exhaust gas has very low mass, responds in ~1-2s
		float TargetEGT = 450.0f + (EngineRPM / REDLINE_RPM) * 200.0f;
		EGT = FMath::FInterpTo(EGT, TargetEGT, DeltaTime, 2.0f);  // Physical: tau~0.5s (low thermal mass)

		// === Fuel consumption (from BSFC × power) ===
		// Real Moster 185: BSFC ≈ 0.4 kg/kW·h (typical 2-stroke)
		// Gasoline density ≈ 0.75 kg/L
		// Fuel flow (L/s) = P (kW) × BSFC / (3600 × ρ_fuel)
		// At full power (18.5 kW): 18.5 × 0.4 / (3600 × 0.75) = 0.00274 L/s
		// Tank = 10L → ~3.6 hours at cruise (matches real ~3-4 L/h)
		// FuelLevel is 0-1 (fraction of tank), so divide by tank volume
		float BSFC = 0.4f;           // kg/kW·h (brake-specific fuel consumption)
		float FuelDensity = 0.75f;   // kg/L (gasoline)
		float TankVolumeL = 10.0f;   // Moster 185 tank size
		float PowerKW = EnginePowerW / 1000.0f;
		float FuelFlowLPerS = PowerKW * BSFC / (3600.0f * FuelDensity);
		float FuelRate = FuelFlowLPerS / TankVolumeL;  // Fraction per second
		FuelLevel -= FuelRate * DeltaTime;
		FuelLevel = FMath::Clamp(FuelLevel, 0.0f, 1.0f);

		// Run out of fuel -> engine stops
		if (FuelLevel <= 0.0f)
		{
			StopEngine();
			UE_LOG(LogTemp, Warning, TEXT("ParamotorActor: Out of fuel! Engine stopped"));
		}

		// === Prop rotation: realistic RPM-based ===
		// Real prop RPM = engine RPM (direct drive, no gearbox on most paramotors)
		// At 7800 RPM = 130 rev/sec = 817 rad/s
		// Visually this would be a blur, so we cap the visual rotation speed
		// but keep it proportional to actual RPM
		// At 0 throttle (idle 2500 RPM): prop spins slowly (freewheeling)
		// At full throttle (7800 RPM): prop is a blur
		// Prop rotation from real angular velocity (already computed above as PropOmega)
		PropRotation += PropOmega * DeltaTime;
		UpdatePropMeshRotation();

		// Sound: pitch and volume scale with RPM
		float RPMRatio = (EngineRPM - IDLE_RPM) / (REDLINE_RPM - IDLE_RPM);
		float Pitch = 1.0f + RPMRatio * 1.5f;
		// Engine volume: quieter at idle (0 throttle), louder under power
		// At 0 throttle: 0.8 (just idling), at full: 2.0 (screaming)
		float EngineVol = 0.8f + Throttle * 1.2f;
		// Exhaust: quieter at idle, louder under power
		float ExhaustVol = 0.6f + Throttle * 1.4f;

		EngineAudio->SetPitchMultiplier(Pitch);
		EngineAudio->SetVolumeMultiplier(EngineVol);
		ExhaustAudio->SetPitchMultiplier(Pitch * 0.85f);
		ExhaustAudio->SetVolumeMultiplier(ExhaustVol);

		// Prop wind: scales dramatically with RPM AND throttle
		// At 0 throttle (idle): prop freewheels, barely any wind noise (0.05 vol)
		// At full throttle: VERY LOUD (1.5 vol) — this is the dominant sound in flight
		float PropWindVol = 0.05f + Throttle * 1.45f;
		// Pitch: 0.28 at idle to 1.0 at full (sound was generated at max RPM)
		float PropWindPitch = 0.28f + RPMRatio * 0.72f;
		PropWindAudio->SetVolumeMultiplier(PropWindVol);
		PropWindAudio->SetPitchMultiplier(PropWindPitch);
	}
	else
	{
		// Prop slows down and stops when not running
		if (EngineRPM > 0.0f)
		{
			EngineRPM = FMath::FInterpTo(EngineRPM, 0.0f, DeltaTime, 0.5f);  // Windmilling + bearing friction
			float PropOmega = GetPropRPM() * 2.0f * PI / 60.0f;
			PropRotation += PropOmega * DeltaTime;
			UpdatePropMeshRotation();
		}

		// Engine cools down when off
		if (EngineTemp > 20.0f)
		{
			EngineTemp = FMath::FInterpTo(EngineTemp, 20.0f, DeltaTime, 0.01f);  // Same physical cooling rate
		}
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
		UE_LOG(LogTemp, Warning, TEXT("ParamotorActor: Player entered range"));
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
		UE_LOG(LogTemp, Warning, TEXT("ParamotorActor: Player left range"));
	}
}

void AParamotorActor::Interact(AParamotorCharacter* PlayerCharacter)
{
	if (bEquipped || !PlayerCharacter) return;

	USceneComponent* AttachTarget = PlayerCharacter->GetMesh();
	if (!AttachTarget) AttachTarget = PlayerCharacter->GetRootComponent();

	FAttachmentTransformRules AttachRules(EAttachmentRule::SnapToTarget, EAttachmentRule::SnapToTarget, EAttachmentRule::KeepWorld, false);

	AttachToComponent(AttachTarget, AttachRules);
	SetActorRelativeLocation(FVector(0.0f, -25.0f, 40.0f));
	SetActorRelativeRotation(FRotator(0.0f, 270.0f, 0.0f));

	bEquipped = true;
	PromptWidget->SetVisibility(false);
	InteractionSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	UE_LOG(LogTemp, Warning, TEXT("ParamotorActor: Equipped by player!"));
}

bool AParamotorActor::GetCarabinerWorldPositions(FVector& OutLeft, FVector& OutRight) const
{
	// Use the ring mesh components from the A-arm hierarchy
	if (RingMeshLeft && RingMeshRight)
	{
		// Use mesh bounds origin (world-space center of the mesh geometry)
		OutLeft = RingMeshLeft->Bounds.Origin;
		OutRight = RingMeshRight->Bounds.Origin;
		UE_LOG(LogTemp, Warning, TEXT("ParamotorActor: Carabiner rings — L=%s R=%s Dist=%.1fcm"),
			*OutLeft.ToString(), *OutRight.ToString(), FVector::Dist(OutLeft, OutRight));
		return true;
	}

	// Fallback: search MeshComponents for ring meshes (legacy path)
	UStaticMeshComponent* LeftRing = nullptr;
	UStaticMeshComponent* RightRing = nullptr;
	for (UStaticMeshComponent* Comp : MeshComponents)
	{
		if (!Comp) continue;
		FString CompName = Comp->GetName();
		if (CompName.Contains(TEXT("WingClip_Ring_Left")))
			LeftRing = Comp;
		else if (CompName.Contains(TEXT("WingClip_Ring_Right")))
			RightRing = Comp;
	}

	if (LeftRing && RightRing)
	{
		OutLeft = LeftRing->Bounds.Origin;
		OutRight = RightRing->Bounds.Origin;
		return true;
	}

	UE_LOG(LogTemp, Warning, TEXT("ParamotorActor: WingClip rings NOT found in %d mesh components"), MeshComponents.Num());
	return false;
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
		UE_LOG(LogTemp, Warning, TEXT("ParamotorActor: Starter audio playing (sound=%s)"),
			StarterAudio->GetSound() ? *StarterAudio->GetSound()->GetName() : TEXT("NULL"));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("ParamotorActor: StarterAudio component is NULL!"));
	}

	UE_LOG(LogTemp, Warning, TEXT("ParamotorActor: Cranking starter... (give throttle to start)"));
}

void AParamotorActor::StopCranking()
{
	// Only stop if currently cranking (engine may have already caught)
	if (EngineState != EEngineState::Cranking) return;

	EngineState = EEngineState::Off;
	StarterTimer = 0.0f;

	if (StarterAudio) StarterAudio->Stop();

	UE_LOG(LogTemp, Warning, TEXT("ParamotorActor: Cranking stopped"));
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

	UE_LOG(LogTemp, Warning, TEXT("ParamotorActor: Engine stopped (temp=%.1fC fuel=%.0f%%)"), EngineTemp, FuelLevel * 100.0f);
}

void AParamotorActor::ToggleFuelCutoff()
{
	bFuelCutoff = !bFuelCutoff;
	UE_LOG(LogTemp, Warning, TEXT("ParamotorActor: Fuel cutoff %s"), bFuelCutoff ? TEXT("ON — engine will die") : TEXT("OFF — fuel flowing"));
}

void AParamotorActor::SetThrottle(float NewThrottle)
{
	float OldThrottle = Throttle;
	Throttle = FMath::Clamp(NewThrottle, 0.0f, 1.0f);
	// Only log when throttle changes significantly
	if (FMath::Abs(Throttle - OldThrottle) > 0.05f)
	{
		UE_LOG(LogTemp, Warning, TEXT("ParamotorActor: Throttle set to %.2f"), Throttle);
	}
}

void AParamotorActor::PlayWingInflate()
{
	if (WingInflateAudio)
	{
		WingInflateAudio->Play();
		UE_LOG(LogTemp, Warning, TEXT("ParamotorActor: Playing wing inflation sound"));
	}
}

// ============================================================
// REAL PROPULSION API
// Based on propeller momentum theory and Moster 185 specifications
// ============================================================

AParamotorActor::FBladeElementResult AParamotorActor::ComputeBladeElement(float AirspeedMs) const
{
	FBladeElementResult Result = {0.0f, 0.0f};

	float PropRPM = GetPropRPM();
	float AltitudeM = GetActorLocation().Z / 100.0f;
	float Rho = AirDensityAtAltitude(AltitudeM);
	float Omega = PropRPM * 2.0f * PI / 60.0f;
	float PropRadius = PROP_DIAMETER_CM / 2.0f / 100.0f;
	float HubRadius = 0.05f;
	float BladeChord = PROP_CHORD_CM / 100.0f;
	float PitchAngle = FMath::Atan2(PROP_PITCH_CM / 100.0f, 2.0f * PI * PropRadius);

	int32 NumElements = (int32)PROP_BLADE_ELEMENTS;
	float dr = (PropRadius - HubRadius) / NumElements;

	for (int32 i = 0; i < NumElements; i++)
	{
		float r = HubRadius + (i + 0.5f) * dr;
		float Vt = Omega * r;
		float Va = AirspeedMs;
		float Vrel = FMath::Sqrt(Va * Va + Vt * Vt);
		if (Vrel < 0.1f) continue;

		float Phi = FMath::Atan2(Va, FMath::Max(0.1f, Vt));
		float rNorm = (r - HubRadius) / (PropRadius - HubRadius);
		float BetaRoot = 30.0f * PI / 180.0f;
		float BetaTip = PitchAngle;
		float Beta = FMath::Lerp(BetaRoot, BetaTip, rNorm);
		float Alpha = Beta - Phi;
		float AlphaDeg = Alpha * 180.0f / PI;

		float CL, CD;
		if (FMath::Abs(AlphaDeg) < 15.0f)
		{
			CL = 2.0f * PI * Alpha;
			CD = 0.015f + 0.04f * Alpha * Alpha;
		}
		else
		{
			CL = 0.5f * FMath::Sign(AlphaDeg);
			CD = 1.0f + 0.3f * (FMath::Abs(AlphaDeg) - 15.0f) * PI / 180.0f;
		}

		float dL = 0.5f * Rho * Vrel * Vrel * BladeChord * CL * dr;
		float dD = 0.5f * Rho * Vrel * Vrel * BladeChord * CD * dr;

		// Thrust (axial) and torque (tangential × radius)
		Result.ThrustN += dL * FMath::Cos(Phi) - dD * FMath::Sin(Phi);
		Result.TorqueNM += (dL * FMath::Sin(Phi) + dD * FMath::Cos(Phi)) * r;
	}

	Result.ThrustN *= PROP_NUM_BLADES;
	Result.TorqueNM *= PROP_NUM_BLADES;
	return Result;
}

float AParamotorActor::GetThrustForceN(float AirspeedMs) const
{
	if (EngineState != EEngineState::Running) return 0.0f;

	FBladeElementResult BE = ComputeBladeElement(AirspeedMs);
	float TotalThrustN = BE.ThrustN;

	// Engine power limit: T ≤ P × η / V (can't exceed engine power at high speed)
	float EnginePowerW = GetEnginePowerW();
	if (AirspeedMs > 0.5f && EnginePowerW > 1.0f)
	{
		float ComputedEfficiency = (TotalThrustN * AirspeedMs) / EnginePowerW;
		ComputedEfficiency = FMath::Clamp(ComputedEfficiency, 0.0f, 0.85f);
		float MaxThrustFromPower = EnginePowerW * ComputedEfficiency / AirspeedMs;
		TotalThrustN = FMath::Min(TotalThrustN, MaxThrustFromPower);
	}

	return FMath::Max(0.0f, TotalThrustN);
}

float AParamotorActor::GetPropTorqueNM(float AirspeedMs) const
{
	if (EngineState != EEngineState::Running) return 0.0f;
	FBladeElementResult BE = ComputeBladeElement(AirspeedMs);
	return BE.TorqueNM;
}

float AParamotorActor::GetPropRPM() const
{
	// Moster 185 has a 2.58:1 reduction ratio
	return (EngineRPM > 0.0f) ? EngineRPM / PROP_REDUCTION_RATIO : 0.0f;
}

float AParamotorActor::GetEnginePowerW() const
{
	if (EngineState != EEngineState::Running) return 0.0f;
	// Moster 185: ~18.5 kW (25 HP) at peak RPM
	// Power scales with throttle and RPM
	float EffectiveThrottle = FMath::Max(0.0f, (Throttle - 0.1f) / 0.9f);
	float RPMFactor = FMath::Max(0.0f, EngineRPM / REDLINE_RPM);
	const float PeakPowerW = 18500.0f;  // 18.5 kW
	// Real power curve: P = P_peak * throttle * (RPM/redline) for 2-stroke
	return PeakPowerW * EffectiveThrottle * RPMFactor;
}
