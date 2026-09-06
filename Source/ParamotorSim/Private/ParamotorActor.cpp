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
		TEXT("/Game/Meshes/AArm_Left.AArm_Left"),
		TEXT("/Game/Meshes/AArm_Right.AArm_Right"),
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
		TEXT("/Game/Meshes/WingClip_Ring_Left.WingClip_Ring_Left"),
		TEXT("/Game/Meshes/WingClip_Ring_Right.WingClip_Ring_Right"),
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
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("ParamotorActor: Built %d mesh components, %d prop meshes"),
		MeshComponents.Num(), PropMeshes.Num());
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

void AParamotorActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (EngineState == EEngineState::Cranking)
	{
		StarterTimer += DeltaTime;

		// Engine catches ONLY if throttle is being given
		if (Throttle > 0.05f)
		{
			// Engine catches after cranking for at least 1.5 seconds with throttle
			// (you need to hear the starter cranking before it catches)
			if (StarterTimer >= 1.5f)
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
		if (StarterTimer >= 5.0f)
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
		float IdleRPM = 2500.0f;
		float EngineOmega = EngineRPM * 2.0f * PI / 60.0f;  // rad/s
		float PropOmega = GetPropRPM() * 2.0f * PI / 60.0f;

		// Engine torque: produces torque from throttle combustion
		// At idle: just enough to overcome internal friction
		// At full throttle: peak torque at peak RPM
		float EffectiveThrottle = FMath::Max(0.0f, (Throttle - 0.05f) / 0.95f);
		float EnginePowerW = 18500.0f * EffectiveThrottle;
		float EngineTorque = (EngineOmega > 10.0f) ? EnginePowerW / EngineOmega : 0.0f;

		// Idle torque: engine produces minimum torque from idle fuel flow
		// Real Moster 185 idle: ~2500 RPM, ~1.5 kW (typical 2-stroke idle power)
		// τ_idle = P_idle / ω_idle = 1500 / (2500 × 2π/60) ≈ 5.7 N·m
		float IdlePowerW = 1500.0f;  // Real idle power for Moster 185
		float IdleOmega = IdleRPM * 2.0f * PI / 60.0f;
		float IdleTorque = (EngineRPM < IdleRPM) ? IdlePowerW / FMath::Max(1.0f, IdleOmega) : 0.0f;

		// Propeller torque: from blade element theory (same model as thrust)
		// τ_prop = Σ (dL×sin(φ) + dD×cos(φ)) × r for each blade element
		float AirspeedForTorque = 0.0f;
		if (AActor* OwnerActor = GetOwner())
			AirspeedForTorque = OwnerActor->GetVelocity().Size() / 100.0f;
		float PropTorque = GetPropTorqueNM(AirspeedForTorque);

		// Engine rotational inertia (kg·m²) — real Moster 185 value
		// Crank + flywheel: ~0.03 kg·m². Propeller (1.3m, 1.5kg): I = m×r²/3 ≈ 0.085 kg·m²
		// Reduction gear adds ~0.005. Total ≈ 0.12 kg·m² (reflected to engine side)
		float EngineInertia = 0.12f;

		// Net torque → angular acceleration → RPM change
		float NetTorque = EngineTorque + IdleTorque - PropTorque;
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
		EngineTemp = FMath::FInterpTo(EngineTemp, TargetTemp, DeltaTime, 0.05f);  // τ ≈ 20s (gameplay compromise)

		// === EGT (exhaust gas temperature) ===
		// EGT is higher than CHT and responds faster (smaller thermal mass)
		// Idle ~450C, full throttle ~650C
		// EGT thermal mass: exhaust gas has very low mass, responds in ~1-2s
		float TargetEGT = 450.0f + (EngineRPM / REDLINE_RPM) * 200.0f;
		EGT = FMath::FInterpTo(EGT, TargetEGT, DeltaTime, 1.0f);  // τ ≈ 1s (fast response)

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

		// Rotate prop meshes around the X axis (prop shaft)
		{
			FQuat PropQuat = FQuat(FVector::ForwardVector, PropRotation);
			for (UStaticMeshComponent* PropMesh : PropMeshes)
			{
				FTransform RelTransform = PropMesh->GetRelativeTransform();
				FVector RelPos = RelTransform.GetTranslation();
				FVector PropCenter = FVector(15.0f, 0.0f, 73.0f);
				FVector Offset = RelPos - PropCenter;
				FVector NewOffset = PropQuat.RotateVector(Offset);
				FVector NewPos = PropCenter + NewOffset;
				FQuat NewRot = PropQuat * RelTransform.GetRotation();
				PropMesh->SetRelativeLocationAndRotation(NewPos, NewRot);
			}
		}

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
			EngineRPM = FMath::FInterpTo(EngineRPM, 0.0f, DeltaTime, 2.0f);
			float PropOmega = GetPropRPM() * 2.0f * PI / 60.0f;
			PropRotation += PropOmega * DeltaTime;

			FQuat PropQuat = FQuat(FVector::ForwardVector, PropRotation);
			for (UStaticMeshComponent* PropMesh : PropMeshes)
			{
				FTransform RelTransform = PropMesh->GetRelativeTransform();
				FVector RelPos = RelTransform.GetTranslation();
				FVector PropCenter = FVector(15.0f, 0.0f, 73.0f);
				FVector Offset = RelPos - PropCenter;
				FVector NewOffset = PropQuat.RotateVector(Offset);
				FVector NewPos = PropCenter + NewOffset;
				FQuat NewRot = PropQuat * RelTransform.GetRotation();
				PropMesh->SetRelativeLocationAndRotation(NewPos, NewRot);
			}
		}

		// Engine cools down when off
		if (EngineTemp > 20.0f)
		{
			EngineTemp = FMath::FInterpTo(EngineTemp, 20.0f, DeltaTime, 0.1f);
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

float AParamotorActor::GetThrustForceN(float AirspeedMs) const
{
	if (EngineState != EEngineState::Running) return 0.0f;

	// Effective throttle (dead zone below 10%)
	float EffectiveThrottle = FMath::Max(0.0f, (Throttle - 0.1f) / 0.9f);

	// === BLADE ELEMENT THEORY ===
	// Real propeller thrust computed by integrating aerodynamic forces
	// along each blade. Each blade section is a rotating airfoil.
	//
	// For each blade element at radius r:
	//   Tangential velocity: V_t = ω × r  (from rotation)
	//   Axial velocity: V_a = V_∞  (freestream/airspeed)
	//   Relative velocity: V_rel = √(V_a² + V_t²)
	//   Inflow angle: φ = atan(V_a / V_t)
	//   Blade pitch angle: β = pitch_angle(r)  (geometric pitch)
	//   Angle of attack: α = β - φ
	//   Lift: dL = 0.5 × ρ × V_rel² × c × CL(α) × dr
	//   Drag: dD = 0.5 × ρ × V_rel² × c × CD(α) × dr
	//   Thrust: dT = dL × cos(φ) - dD × sin(φ)
	//   Total thrust: T = N_blades × ∫ dT from r_hub to r_tip
	//
	// The Moster 185 prop:
	//   Diameter: 130cm, 2 blades, ~8cm chord, ~50cm pitch
	//   Max prop RPM: 7800/2.58 = 3023 RPM
	//   Pitch angle varies from ~30° at root to ~15° at tip (twist)

	float PropRPM = GetPropRPM();
	if (PropRPM < 100.0f) return 0.0f;  // Prop barely spinning

	// Altitude-dependent air density (ISA model)
	float AltitudeM = 0.0f;
	AActor* OwnerActor = GetOwner();
	if (OwnerActor)
		AltitudeM = OwnerActor->GetActorLocation().Z / 100.0f;
	float Rho = AirDensityAtAltitude(AltitudeM);
	float Omega = PropRPM * 2.0f * PI / 60.0f;  // Angular velocity (rad/s)
	float PropRadius = PROP_DIAMETER_CM / 2.0f / 100.0f;  // meters
	float HubRadius = 0.05f;  // 5cm hub radius
	float BladeChord = PROP_CHORD_CM / 100.0f;  // meters
	float PitchAngle = FMath::Atan2(PROP_PITCH_CM / 100.0f, 2.0f * PI * PropRadius);  // Pitch angle at tip (rad)

	// Throttle affects effective pitch (variable pitch approximation)
	// At low throttle, the engine can't sustain full RPM, so effective pitch is lower
	// This is modeled by scaling the RPM, not the pitch (fixed-pitch prop)
	float RPMscale = FMath::Max(0.0f, EngineRPM / REDLINE_RPM);

	// Integrate thrust along the blade
	float TotalThrustN = 0.0f;
	int32 NumElements = (int32)PROP_BLADE_ELEMENTS;
	float dr = (PropRadius - HubRadius) / NumElements;

	for (int32 i = 0; i < NumElements; i++)
	{
		float r = HubRadius + (i + 0.5f) * dr;  // Element center radius (m)
		float Vt = Omega * r;                    // Tangential velocity (m/s)
		float Va = AirspeedMs;                   // Axial velocity (airspeed, m/s)
		float Vrel = FMath::Sqrt(Va * Va + Vt * Vt);  // Relative velocity

		if (Vrel < 0.1f) continue;

		float Phi = FMath::Atan2(Va, FMath::Max(0.1f, Vt));  // Inflow angle (rad)

		// Blade pitch angle: varies from root to tip (propeller twist)
		// Root: high angle (~30°), Tip: lower angle (~15°)
		// Linear twist from root to tip
		float rNorm = (r - HubRadius) / (PropRadius - HubRadius);  // 0 at root, 1 at tip
		float BetaRoot = 30.0f * PI / 180.0f;   // 30° at root
		float BetaTip = PitchAngle;             // Computed from pitch/diameter
		float Beta = FMath::Lerp(BetaRoot, BetaTip, rNorm);

		// Angle of attack
		float Alpha = Beta - Phi;  // radians

		// Simple flat-plate airfoil model for propeller blade:
		// CL = 2π × α (thin airfoil theory, valid for small α)
		// CD = 0.02 + 0.05 × α² (profile drag + induced drag)
		// Stall at α > 15° (flow separates)
		float AlphaDeg = Alpha * 180.0f / PI;
		float CL, CD;
		if (FMath::Abs(AlphaDeg) < 15.0f)
		{
			CL = 2.0f * PI * Alpha;  // Thin airfoil theory
			CD = 0.02f + 0.05f * Alpha * Alpha;
		}
		else
		{
			// Stalled: CL drops, CD rises
			CL = 0.3f * FMath::Sign(AlphaDeg);
			CD = 0.5f + 0.1f * (FMath::Abs(AlphaDeg) - 15.0f) * PI / 180.0f;
		}

		// Forces on this blade element
		float dL = 0.5f * Rho * Vrel * Vrel * BladeChord * CL * dr;
		float dD = 0.5f * Rho * Vrel * Vrel * BladeChord * CD * dr;

		// Thrust component (axial force)
		float dT = dL * FMath::Cos(Phi) - dD * FMath::Sin(Phi);

		TotalThrustN += dT;
	}

	// Multiply by number of blades
	TotalThrustN *= PROP_NUM_BLADES;

	// Engine power limit: T ≤ P × η / V (can't exceed engine power at high speed)
	// Efficiency computed from blade element results: η = T × V / P
	// At zero airspeed, η is undefined (static thrust), so no power limit applies
	float EnginePowerW = GetEnginePowerW();
	float MaxThrustFromPower;
	if (AirspeedMs > 0.5f && EnginePowerW > 1.0f)
	{
		// Compute efficiency from the blade element thrust we just calculated
		// η = T × V / P — this is the actual propulsive efficiency
		float ComputedEfficiency = (TotalThrustN * AirspeedMs) / EnginePowerW;
		// Clamp to physical range [0, 0.85] (real props max ~85% efficient)
		ComputedEfficiency = FMath::Clamp(ComputedEfficiency, 0.0f, 0.85f);
		MaxThrustFromPower = EnginePowerW * ComputedEfficiency / AirspeedMs;
	}
	else
	{
		MaxThrustFromPower = TotalThrustN;  // Static thrust — no power limit
	}
	TotalThrustN = FMath::Min(TotalThrustN, MaxThrustFromPower);

	// RPM is already accounted for through Omega in the blade element integration.
	// No additional RPM scaling — that would double-count the RPM effect.
	return FMath::Max(0.0f, TotalThrustN);
}

float AParamotorActor::GetPropTorqueNM(float AirspeedMs) const
{
	if (EngineState != EEngineState::Running) return 0.0f;

	float PropRPM = GetPropRPM();
	if (PropRPM < 100.0f) return 0.0f;

	float AltitudeM = 0.0f;
	AActor* OwnerActor = GetOwner();
	if (OwnerActor)
		AltitudeM = OwnerActor->GetActorLocation().Z / 100.0f;
	float Rho = AirDensityAtAltitude(AltitudeM);
	float Omega = PropRPM * 2.0f * PI / 60.0f;
	float PropRadius = PROP_DIAMETER_CM / 2.0f / 100.0f;
	float HubRadius = 0.05f;
	float BladeChord = PROP_CHORD_CM / 100.0f;
	float PitchAngle = FMath::Atan2(PROP_PITCH_CM / 100.0f, 2.0f * PI * PropRadius);

	int32 NumElements = (int32)PROP_BLADE_ELEMENTS;
	float dr = (PropRadius - HubRadius) / NumElements;
	float TotalTorque = 0.0f;

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
			CD = 0.02f + 0.05f * Alpha * Alpha;
		}
		else
		{
			CL = 0.3f * FMath::Sign(AlphaDeg);
			CD = 0.5f + 0.1f * (FMath::Abs(AlphaDeg) - 15.0f) * PI / 180.0f;
		}

		float dL = 0.5f * Rho * Vrel * Vrel * BladeChord * CL * dr;
		float dD = 0.5f * Rho * Vrel * Vrel * BladeChord * CD * dr;

		// Torque component (tangential force × radius)
		// dQ = (dL × sin(φ) + dD × cos(φ)) × r
		float dQ = (dL * FMath::Sin(Phi) + dD * FMath::Cos(Phi)) * r;
		TotalTorque += dQ;
	}

	TotalTorque *= PROP_NUM_BLADES;
	return TotalTorque;  // N·m
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
	return PeakPowerW * EffectiveThrottle * RPMFactor;
}
