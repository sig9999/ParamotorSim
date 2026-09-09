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
#include "DrawDebugHelpers.h"

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
	// If using the Rider 185, load the combined FBX mesh
	if (bUseRider185)
	{
		UStaticMesh* RiderMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Meshes/SM_Rider_Moster185.SM_Rider_Moster185"));
		if (RiderMesh)
		{
			UStaticMeshComponent* MeshComp = NewObject<UStaticMeshComponent>(this, TEXT("Rider185Mesh"));
			MeshComp->SetupAttachment(Root);
			MeshComp->SetStaticMesh(RiderMesh);
			MeshComp->SetMobility(EComponentMobility::Movable);
			MeshComp->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			MeshComp->SetCollisionResponseToAllChannels(ECR_Ignore);
			MeshComp->RegisterComponent();
			MeshComponents.Add(MeshComp);
			UE_LOG(LogTemp, Warning, TEXT("ParamotorActor: Built Rider 185 mesh (%d verts)"), RiderMesh->GetNumVertices(0));
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("ParamotorActor: Failed to load SM_Rider_Moster185 mesh!"));
		}

		// The Rider 185 mesh already contains the propeller and cage.
		// TODO: separate propeller sub-mesh for real-time spinning.
		return;
	}

	// If using the test frame, load the single combined mesh
	if (bUseTestFrame)
	{
		UStaticMesh* TestMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Meshes/ParamotorTestFrame.ParamotorTestFrame"));
		if (TestMesh)
		{
			UStaticMeshComponent* MeshComp = NewObject<UStaticMeshComponent>(this, TEXT("TestFrameMesh"));
			MeshComp->SetupAttachment(Root);
			MeshComp->SetStaticMesh(TestMesh);
			MeshComp->SetMobility(EComponentMobility::Movable);
			MeshComp->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			MeshComp->SetCollisionResponseToAllChannels(ECR_Ignore);
			MeshComp->RegisterComponent();
			MeshComponents.Add(MeshComp);
			UE_LOG(LogTemp, Warning, TEXT("ParamotorActor: Built test frame mesh (%d verts)"), TestMesh->GetNumVertices(0));
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("ParamotorActor: Failed to load ParamotorTestFrame mesh!"));
		}

		// Load the propeller as a separate rotating component
		UStaticMesh* PropMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Meshes/ParamotorPropeller.ParamotorPropeller"));
		if (PropMesh)
		{
			UStaticMeshComponent* PropComp = NewObject<UStaticMeshComponent>(this, TEXT("TestFramePropeller"));
			PropComp->SetupAttachment(Root);
			PropComp->SetStaticMesh(PropMesh);
			PropComp->SetMobility(EComponentMobility::Movable);
			PropComp->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			PropComp->SetCollisionResponseToAllChannels(ECR_Ignore);
			// Pivot location in the test frame local space (Blender: 1.65 - 1.19 = 0.46m X, 0.85 - 0.65 = 0.20m Z)
			// User adjusted: move up ~30", in 1 foot, plus 2" up
			PropComp->SetRelativeLocation(FVector(16.0f, 0.0f, 85.0f));
			PropComp->SetRelativeRotation(FQuat::Identity);
			PropComp->RegisterComponent();
			MeshComponents.Add(PropComp);
			PropMeshes.Add(PropComp);
			PropMeshBaseTransforms.Add(PropComp->GetRelativeTransform());
			PropCenter = FVector(16.0f, 0.0f, 85.0f);
			UE_LOG(LogTemp, Warning, TEXT("ParamotorActor: Built test frame propeller (%d verts)"), PropMesh->GetNumVertices(0));
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("ParamotorActor: Failed to load ParamotorPropeller mesh!"));
		}

		// Test frame has all geometry baked in — skip A-arm hierarchy
		// (those procedural components don't match the custom mesh and float in wrong positions)
		return;
	}

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
			PropCenter = FVector(15.0f, 0.0f, 73.0f);
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

	// Build both sides
	AArmPivotLeft = BuildSingleAArmSide(true, StudLeft);
	AArmPivotRight = BuildSingleAArmSide(false, StudRight);

	// Capture the ring mesh's world-space offset from the pivot BEFORE any rotation.
	if (RingMeshLeft && AArmPivotLeft)
	{
		FVector RingWorld = RingMeshLeft->Bounds.Origin;
		FVector PivotWorld = AArmPivotLeft->GetComponentLocation();
		CarabinerOffsetFromPivot = AArmPivotLeft->GetComponentTransform().InverseTransformPosition(RingWorld);
		UE_LOG(LogTemp, Warning, TEXT("ParamotorActor: Carabiner offset from pivot = %s (RingWorld=%s PivotWorld=%s)"),
			*CarabinerOffsetFromPivot.ToString(), *RingWorld.ToString(), *PivotWorld.ToString());
	}

	UE_LOG(LogTemp, Warning, TEXT("ParamotorActor: Built A-arm hierarchy (L/R pivots + meshes)"));
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

void AParamotorActor::UpdateAArmPivots()
{
	if (!AArmPivotLeft || !AArmPivotRight) return;

	// A-arms always hang straight down from the frame.
	// They do NOT track the wing position — in real life, A-arms are short
	// and pivot only a few degrees from gravity and line tension.
	// The riser webbing (49cm strap) handles the angular difference between
	// the A-arm and the wing. Rotating the A-arms to track the wing causes
	// wild swinging because the wing position oscillates from turbulence/pendulum.
	// The line physics uses the fixed carabiner position (from GetCarabinerWorldPositions),
	// not the A-arm rotation, so this is purely visual.
	AArmPivotLeft->SetRelativeRotation(FRotator::ZeroRotator);
	AArmPivotRight->SetRelativeRotation(FRotator::ZeroRotator);
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

	UE_LOG(LogTemp, Warning, TEXT("ParamotorActor: Audio initialized (starter=%d idle=%d exhaust=%d prop=%d)"),
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

void AParamotorActor::UpdatePropMeshRotation()
{
	FQuat PropQuat = FQuat(FVector::ForwardVector, PropRotation);
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
		UpdateCranking(DeltaTime);
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

		UpdateEngineRPM(DeltaTime);
		UpdateEngineTemp(DeltaTime);
		UpdateFuel(DeltaTime);

		// Prop rotation from real angular velocity
		float PropOmega = GetPropRPM() * 2.0f * PI / 60.0f;
		PropRotation += PropOmega * DeltaTime;
		UpdatePropMeshRotation();

		// Draw thrust-direction arrow from the propeller hub in the direction the pilot is pushed.
		FVector PropWorldPos = GetActorTransform().TransformPosition(PropCenter);
		FVector ThrustDir = GetActorForwardVector();
		DrawDebugDirectionalArrow(GetWorld(), PropWorldPos, PropWorldPos + ThrustDir * 300.0f, 60.0f, FColor::Green, false, 0.0f, 0, 6.0f);

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

			UE_LOG(LogTemp, Warning, TEXT("ParamotorActor: Engine caught! Running at throttle=%.2f"), Throttle);
		}
	}

	// If cranking too long without throttle, give up (starter thermal limit)
	if (StarterTimer >= 8.0f)
	{
		StopCranking();
		UE_LOG(LogTemp, Warning, TEXT("ParamotorActor: Starter gave up (no throttle)"));
	}
}

void AParamotorActor::UpdateEngineRPM(float DeltaTime)
{
	// J·dω/dt = τ_engine(ω, throttle) − τ_prop(ω, airspeed)
	float EngineOmega = EngineRPM * 2.0f * PI / 60.0f;
	float PropOmega = GetPropRPM() * 2.0f * PI / 60.0f;

	// Engine torque from throttle combustion
	float EffectiveThrottle = FMath::Max(0.0f, (Throttle - 0.1f) / 0.9f);
	float EnginePowerW = 18500.0f * EffectiveThrottle * FMath::Max(0.0f, EngineRPM / REDLINE_RPM);
	float EngineTorque = (EngineOmega > 10.0f) ? EnginePowerW / EngineOmega : 0.0f;

	// Idle torque: ~5.7 N·m at 2500 RPM (1.5 kW idle power)
	float IdlePowerW = 1500.0f;
	float IdleOmega = IDLE_RPM * 2.0f * PI / 60.0f;
	float IdleTorque = (EngineRPM < IDLE_RPM) ? IdlePowerW / FMath::Max(1.0f, IdleOmega) : 0.0f;

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
	EngineRPM = FMath::Clamp(EngineRPM, 0.0f, REDLINE_RPM);
}

void AParamotorActor::UpdateEngineTemp(float DeltaTime)
{
	// CHT: idle ~75C, full throttle ~110C, thermal tau ~100s
	float TargetTemp = 75.0f + (EngineRPM / REDLINE_RPM) * 35.0f;
	EngineTemp = FMath::FInterpTo(EngineTemp, TargetTemp, DeltaTime, 0.01f);

	// EGT: idle ~450C, full throttle ~650C, responds fast (~0.5s)
	float TargetEGT = 450.0f + (EngineRPM / REDLINE_RPM) * 200.0f;
	EGT = FMath::FInterpTo(EGT, TargetEGT, DeltaTime, 2.0f);
}

void AParamotorActor::UpdateFuel(float DeltaTime)
{
	// BSFC ≈ 0.4 kg/kW·h, gasoline density ≈ 0.75 kg/L, tank = 10L
	float BSFC = 0.4f;
	float FuelDensity = 0.75f;
	float TankVolumeL = 10.0f;
	float EffectiveThrottle = FMath::Max(0.0f, (Throttle - 0.1f) / 0.9f);
	float EnginePowerW = 18500.0f * EffectiveThrottle * FMath::Max(0.0f, EngineRPM / REDLINE_RPM);
	float PowerKW = EnginePowerW / 1000.0f;
	float FuelFlowLPerS = PowerKW * BSFC / (3600.0f * FuelDensity);
	float FuelRate = FuelFlowLPerS / TankVolumeL;
	FuelLevel -= FuelRate * DeltaTime;
	FuelLevel = FMath::Clamp(FuelLevel, 0.0f, 1.0f);

	if (FuelLevel <= 0.0f)
	{
		StopEngine();
		UE_LOG(LogTemp, Warning, TEXT("ParamotorActor: Out of fuel! Engine stopped"));
	}
}

void AParamotorActor::UpdateEngineAudio()
{
	float RPMRatio = (EngineRPM - IDLE_RPM) / (REDLINE_RPM - IDLE_RPM);
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

	USkeletalMeshComponent* CharacterMesh = PlayerCharacter->GetMesh();
	if (!CharacterMesh)
	{
		UE_LOG(LogTemp, Error, TEXT("ParamotorActor: No character mesh!"));
		return;
	}

	if (bUseTestFrame || bUseRider185)
	{
		// Bone attachment — follows the body during movement/animation
		FAttachmentTransformRules AttachRules(EAttachmentRule::SnapToTarget, EAttachmentRule::SnapToTarget, EAttachmentRule::KeepWorld, false);
		FName AttachBone = TEXT("spine_02");
		if (CharacterMesh->GetBoneIndex(AttachBone) == INDEX_NONE)
		{
			// Fall back to mesh root if bone not found
			UE_LOG(LogTemp, Warning, TEXT("ParamotorActor: spine_02 not found, falling back to mesh root"));
			AttachToComponent(CharacterMesh, AttachRules);
		}
		else
		{
			AttachToComponent(CharacterMesh, AttachRules, AttachBone);
		}

		// Offset/rotation that put the paramotor in the correct position on the back
		SetActorRelativeLocation(FVector(-60.0f, 120.0f, 0.0f));
		SetActorRelativeRotation(FRotator(0.0f, -90.0f, 90.0f));
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
	PromptWidget->SetVisibility(false);
	InteractionSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	UE_LOG(LogTemp, Warning, TEXT("ParamotorActor: Equipped by player!"));
}

bool AParamotorActor::GetCarabinerWorldPositions(FVector& OutLeft, FVector& OutRight) const
{
	// Use the A-arm PIVOT position + fixed offset (captured at setup before rotation).
	// This breaks the feedback loop: A-arm rotation is purely visual and does NOT
	// change the carabiner position that drives line physics.
	// Using RingMesh->Bounds.Origin would feed A-arm rotation back into line physics,
	// causing oscillation (wing moves → A-arm rotates → carabiner moves → lines move → wing moves).
	if (AArmPivotLeft && AArmPivotRight && CarabinerOffsetFromPivot != FVector::ZeroVector)
	{
		FVector PivotLeftPos = AArmPivotLeft->GetComponentLocation();
		FVector PivotRightPos = AArmPivotRight->GetComponentLocation();
		// Apply the fixed offset in the pivot's local space (before rotation)
		// Since the offset was captured before rotation, we apply it in the pivot's
		// local space WITHOUT the current rotation — use the parent's transform.
		FTransform PivotLeftTM = AArmPivotLeft->GetAttachParent() ? AArmPivotLeft->GetAttachParent()->GetComponentTransform() : FTransform::Identity;
		FTransform PivotRightTM = AArmPivotRight->GetAttachParent() ? AArmPivotRight->GetAttachParent()->GetComponentTransform() : FTransform::Identity;
		OutLeft = PivotLeftTM.TransformPosition(CarabinerOffsetFromPivot);
		OutRight = PivotRightTM.TransformPosition(CarabinerOffsetFromPivot);
		return true;
	}

	// Fallback: use ring mesh bounds directly (legacy, may cause feedback oscillation)
	if (RingMeshLeft && RingMeshRight)
	{
		OutLeft = RingMeshLeft->Bounds.Origin;
		OutRight = RingMeshRight->Bounds.Origin;
		return true;
	}

	// Test frame / Rider 185 fallback: compute carabiner positions from the paramotor actor's
	// own world position. These are single meshes with no A-arm hierarchy,
	// so we derive carabiner positions from the actor location + character right vector.
	if (bUseTestFrame || bUseRider185)
	{
		FVector ActorLoc = GetActorLocation();
		// Get the parent actor (character) to determine facing direction
		FVector Right = FVector::RightVector;
		AActor* ParentActor = GetAttachParentActor();
		if (ParentActor)
		{
			Right = ParentActor->GetActorRightVector();
		}
		float CarabHalfSpacing = 21.0f;  // ~42cm total spacing
		OutLeft = ActorLoc + Right * -CarabHalfSpacing;
		OutRight = ActorLoc + Right * CarabHalfSpacing;
		return true;
	}

	UE_LOG(LogTemp, Warning, TEXT("ParamotorActor: Could not compute carabiner positions"));
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

	UE_LOG(LogTemp, Warning, TEXT("ParamotorActor: Engine stopped (temp=%.1fC fuel=%.0f%%)"), EngineTemp, FuelLevel * PERCENT_FACTOR);
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

// ============================================================
// REAL PROPULSION API
// Based on propeller momentum theory and Moster 185 specifications
// ============================================================

AParamotorActor::FBladeElementResult AParamotorActor::ComputeBladeElement(float AirspeedMs) const
{
	FBladeElementResult Result = {0.0f, 0.0f};

	float PropRPM = GetPropRPM();
	float AltitudeM = GetActorLocation().Z  / CM_PER_M;
	float Rho = AirDensityAtAltitude(AltitudeM);
	float Omega = PropRPM * 2.0f * PI / 60.0f;
	float PropRadius = PROP_DIAMETER_CM / 2.0f  / CM_PER_M;
	float HubRadius = 0.05f;
	float BladeChord = PROP_CHORD_CM  / CM_PER_M;
	float PitchAngle = FMath::Atan2(PROP_PITCH_CM  / CM_PER_M, 2.0f * PI * PropRadius);

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
		float BetaRoot = 30.0f * PI_DIV_180;
		float BetaTip = PitchAngle;
		float Beta = FMath::Lerp(BetaRoot, BetaTip, rNorm);
		float Alpha = Beta - Phi;
		float AlphaDeg = Alpha * RAD_TO_DEG;

		float CL, CD;
		if (FMath::Abs(AlphaDeg) < 15.0f)
		{
			CL = 2.0f * PI * Alpha;
			CD = 0.015f + 0.04f * Alpha * Alpha;
		}
		else
		{
			CL = 0.5f * FMath::Sign(AlphaDeg);
			CD = 1.0f + 0.3f * (FMath::Abs(AlphaDeg) - 15.0f) * PI_DIV_180;
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
