// Copyright Epic Games, Inc. All Rights Reserved.

#include "HangarGameMode.h"
#include "ParamotorLogging.h"
#include "HangarPawn.h"
#include "EngineUtils.h"
#include "ParamotorBuilder.h"
#include "UW_HangarMenu.h"
#include "Engine/World.h"
#include "GameFramework/PlayerStart.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "GameFramework/PlayerController.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/TextureCube.h"
#include "Engine/SkyLight.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PointLight.h"
#include "Engine/RectLight.h"
#include "Components/RectLightComponent.h"
#include "HAL/IConsoleManager.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "GameFramework/RotatingMovementComponent.h"
#include "Materials/MaterialInterface.h"

AHangarGameMode::AHangarGameMode()
{
	DefaultPawnClass = AHangarPawn::StaticClass();
}

void AHangarGameMode::StartPlay()
{
	Super::StartPlay();

	UWorld* World = GetWorld();
	if (World)
	{
		APlayerStart* Start = nullptr;
		for (TActorIterator<APlayerStart> It(World); It; ++It)
		{
			Start = *It;
			break;
		}
		if (!Start)
		{
			Start = World->SpawnActor<APlayerStart>(APlayerStart::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
			UE_LOG(LogParamotor, Log, TEXT("HangarGameMode: spawned fallback PlayerStart at origin"));
		}
	}

	ApplyStudioQuality();
	SpawnEnvironment();
	SpawnBuilder();
}

void AHangarGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);

	if (NewPlayer && !NewPlayer->GetPawn())
	{
		UWorld* World = GetWorld();
		if (World)
		{
			FActorSpawnParameters SpawnInfo;
			SpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AHangarPawn* Pawn = World->SpawnActor<AHangarPawn>(AHangarPawn::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnInfo);
			if (Pawn)
			{
				NewPlayer->Possess(Pawn);
			}
		}
	}

	CreateMenu();
}

void AHangarGameMode::DoSpawns()
{
	CreateMenu();
	ApplyStudioQuality();
	SpawnEnvironment();
	SpawnBuilder();
}

void AHangarGameMode::ApplyStudioQuality()
{
	// Ultra-HD hangar: supersample to 150% and max scalability for a small
	// studio scene the GPU can easily afford.
	IConsoleManager& CM = IConsoleManager::Get();
	auto SetCVar = [&CM](const TCHAR* Name, float Value)
	{
		if (IConsoleVariable* CVar = CM.FindConsoleVariable(Name))
		{
			CVar->Set(Value);
		}
	};

	// 200% supersample — the showroom is one rig on a podium, the GPU can
	// afford to render it well past native res.
	SetCVar(TEXT("r.ScreenPercentage"), 200.0f);
	SetCVar(TEXT("r.SecondaryScreenPercentage.GameViewport"), 0.0f);
	// TSR — UE5's native temporal super-resolution; sharper than TAA on the
	// fine cage/strap geometry.
	SetCVar(TEXT("r.AntiAliasingMethod"), 4.0f);
	SetCVar(TEXT("sg.AntiAliasingQuality"), 4.0f);
	SetCVar(TEXT("sg.ShadowQuality"), 4.0f);
	SetCVar(TEXT("sg.TextureQuality"), 4.0f);
	SetCVar(TEXT("sg.ViewDistanceQuality"), 4.0f);
	SetCVar(TEXT("sg.EffectsQuality"), 4.0f);
	SetCVar(TEXT("sg.PostProcessQuality"), 4.0f);
	SetCVar(TEXT("sg.FoliageQuality"), 4.0f);
	SetCVar(TEXT("r.Shadow.MaxResolution"), 8192.0f);
	SetCVar(TEXT("r.Shadow.MaxCSMResolution"), 4096.0f);
	SetCVar(TEXT("r.ContactShadows"), 1.0f);
	SetCVar(TEXT("r.ContactShadows.NonShadowCastingIntensity"), 1.0f);
	SetCVar(TEXT("r.AmbientOcclusionLevels"), 2.0f);
	SetCVar(TEXT("r.GTAO.FallbackToAmbientOcclusion"), 1.0f);
	SetCVar(TEXT("r.DetailMode"), 3.0f);
	SetCVar(TEXT("r.MaterialQualityLevel"), 1.0f);
	// Texture fidelity: never let the parts drop a mip.
	SetCVar(TEXT("r.MaxAnisotropy"), 16.0f);
	SetCVar(TEXT("r.Streaming.PoolSize"), 8000.0f);
	SetCVar(TEXT("r.Streaming.MipBias"), -1.0f);
	SetCVar(TEXT("r.Streaming.FullyLoadUsedTextures"), 1.0f);
	SetCVar(TEXT("r.Streaming.Boost"), 2.0f);
	// Reflections + denoisers at full quality for the metal/painted parts.
	SetCVar(TEXT("r.SSR.Quality"), 4.0f);
	SetCVar(TEXT("r.ReflectionMethod"), 1.0f); // Lumen
	SetCVar(TEXT("r.Lumen.Reflections.Quality"), 4.0f);
	SetCVar(TEXT("r.Lumen.DiffuseIndirect.SSAO"), 1.0f);
}

// ---- Environment spawn helpers ------------------------------------------

static AActor* SpawnDecorMesh(UWorld* World, const TCHAR* MeshPath, UMaterialInterface* Material,
	const FVector& Location, const FRotator& Rotation, const FVector& Scale, bool bCastShadow = true)
{
	AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), Location, Rotation);
	if (!Actor) return nullptr;

	USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("Root"));
	Actor->SetRootComponent(Root);
	Root->RegisterComponent();

	UStaticMeshComponent* MeshComp = NewObject<UStaticMeshComponent>(Actor, TEXT("Mesh"));
	MeshComp->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, MeshPath));
	if (Material)
	{
		for (int32 MatIndex = 0; MatIndex < MeshComp->GetNumMaterials(); ++MatIndex)
		{
			MeshComp->SetMaterial(MatIndex, Material);
		}
	}
	MeshComp->SetRelativeScale3D(Scale);
	MeshComp->SetMobility(EComponentMobility::Movable);
	MeshComp->SetCastShadow(bCastShadow);
	MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MeshComp->SetupAttachment(Root);
	MeshComp->RegisterComponent();
	// AActor spawns rootless — the spawn transform isn't applied until the
	// root exists, so re-assert it once the root is registered.
	Actor->SetActorLocationAndRotation(Location, Rotation);
	return Actor;
}

// Softbox panel — specular damped so glossy parts get soft studio light
// instead of hot streaks.
static void SpawnRectLight(UWorld* World, const FVector& Loc, const FRotator& Rot,
	float Intensity, const FLinearColor& Color, const FVector2D& Size)
{
	ARectLight* Light = World->SpawnActor<ARectLight>(ARectLight::StaticClass(), Loc, Rot);
	if (!Light) return;
	if (URectLightComponent* Comp = Cast<URectLightComponent>(Light->GetLightComponent()))
	{
		Comp->SetMobility(EComponentMobility::Movable);
		Comp->SetIntensity(Intensity);
		Comp->SetLightColor(Color);
		Comp->SetSourceWidth(Size.X);
		Comp->SetSourceHeight(Size.Y);
		Comp->SetAttenuationRadius(2600.0f);
		Comp->SetSpecularScale(0.2f);
	}
}

static void SpawnPointLight(UWorld* World, const FVector& Loc, float Intensity,
	float Radius, const FLinearColor& Color, float SpecularScale = 0.2f)
{
	APointLight* Light = World->SpawnActor<APointLight>(APointLight::StaticClass(), Loc, FRotator::ZeroRotator);
	if (!Light) return;
	if (UPointLightComponent* Comp = Cast<UPointLightComponent>(Light->GetLightComponent()))
	{
		Comp->SetMobility(EComponentMobility::Movable);
		Comp->SetIntensity(Intensity);
		Comp->SetAttenuationRadius(Radius);
		Comp->SetUseInverseSquaredFalloff(true);
		Comp->SetSpecularScale(SpecularScale);
		Comp->SetLightColor(Color);
	}
}

// Child decor mesh — attaches a scaled primitive under an existing component
// (podium top disc, index mark, ...).
static UStaticMeshComponent* AttachDecorMesh(AActor* Owner, USceneComponent* Parent, const TCHAR* MeshPath,
	UMaterialInterface* Material, const FVector& RelLoc, const FVector& RelScale)
{
	UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(Owner);
	Comp->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, MeshPath));
	if (Material)
	{
		for (int32 MatIndex = 0; MatIndex < Comp->GetNumMaterials(); ++MatIndex)
		{
			Comp->SetMaterial(MatIndex, Material);
		}
	}
	Comp->SetRelativeLocation(RelLoc);
	Comp->SetRelativeScale3D(RelScale);
	Comp->SetMobility(EComponentMobility::Movable);
	Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Comp->SetupAttachment(Parent);
	Comp->RegisterComponent();
	return Comp;
}

void AHangarGameMode::SpawnEnvironment()
{
	UWorld* World = GetWorld();
	if (!World) return;

	UMaterialInterface* TopMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_HangarTop.M_HangarTop"));
	UMaterialInterface* AccentMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_HangarAccent.M_HangarAccent"));

	// ---- Geometry: HDRI hangar dome + rotating podium ----
	// No floor slab — the backdrop's photo floor IS the ground. The dark
	// 18m slab used to float over it as a big black square.

	// Environment dome — HDRI Backdrop plugin wrapping the PolyHaven
	// hangar_interior cubemap. Brings its own skylight + floor plane.
	// Actor Z puts the photo floor right at the podium base bottom (-24).
	UTextureCube* EnvCube = LoadObject<UTextureCube>(nullptr,
		TEXT("/Game/Textures/Env/T_HangarInterior.T_HangarInterior"));
	bool bEnvDome = false;
	if (EnvCube)
	{
		if (UClass* BackdropClass = LoadObject<UClass>(nullptr,
			TEXT("/HDRIBackdrop/Blueprints/HDRIBackdrop.HDRIBackdrop_C")))
		{
			AActor* Dome = World->SpawnActor<AActor>(BackdropClass,
				FVector(0.0f, 0.0f, -24.0f), FRotator::ZeroRotator);
			if (Dome)
			{
				if (FObjectProperty* CubeProp = FindFProperty<FObjectProperty>(BackdropClass, TEXT("Cubemap")))
				{
					CubeProp->SetObjectPropertyValue_InContainer(Dome, EnvCube);
				}
				if (FFloatProperty* SizeProp = FindFProperty<FFloatProperty>(BackdropClass, TEXT("Size")))
				{
					SizeProp->SetPropertyValue_InContainer(Dome, 120.0f);
				}
				// The dome's floor plane is a lit material — move the whole
				// backdrop to lighting channel 1 so the studio rig (channel 0)
				// can't blow out the photo floor.
				{
					TArray<UActorComponent*> DomeComps;
					Dome->GetComponents(DomeComps);
					for (UActorComponent* C : DomeComps)
					{
						if (UPrimitiveComponent* P = Cast<UPrimitiveComponent>(C))
						{
							P->SetLightingChannels(false, true, false);
						}
					}
				}
				// The BP applies the cubemap in its construction script —
				// without this the dome keeps the default sky.
				// RerunConstructionScripts is editor-gated in 5.8 — shipping
				// spawns run the construction script on spawn anyway.
#if WITH_EDITOR
				Dome->RerunConstructionScripts();
#endif
				bEnvDome = true;
			}
		}
	}

	PodiumActor = SpawnDecorMesh(World, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), TopMat,
		FVector(0.0f, 0.0f, -12.0f), FRotator::ZeroRotator, FVector(1.5f, 1.5f, 0.24f));
	if (PodiumActor)
	{
		Turntable = NewObject<URotatingMovementComponent>(PodiumActor, TEXT("Turntable"));
		Turntable->SetUpdatedComponent(PodiumActor->GetRootComponent());
		Turntable->RotationRate = FRotator(0.0f, TurntableRate, 0.0f);
		Turntable->RegisterComponent();
		Turntable->Activate(true);

		USceneComponent* PodiumRoot = PodiumActor->GetRootComponent();
		if (PodiumRoot)
		{
			AttachDecorMesh(PodiumActor, PodiumRoot, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"),
				TopMat, FVector(0.0f, 0.0f, 2.5f), FVector(1.35f, 1.35f, 0.05f));
			// Painted accent ring around the podium rim.
			AttachDecorMesh(PodiumActor, PodiumRoot, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"),
				AccentMat, FVector(0.0f, 0.0f, 2.2f), FVector(1.52f, 1.52f, 0.012f));
			// Turntable index mark — small accent bar on the podium face.
			AttachDecorMesh(PodiumActor, PodiumRoot, TEXT("/Engine/BasicShapes/Cube.Cube"),
				AccentMat, FVector(48.0f, 0.0f, 1.5f), FVector(0.5f, 0.07f, 0.02f));
		}
	}

	// ---- Lighting: the HDRI provides ambient; the rig just shapes the subject ----

	// Key softbox — front-left, elevated, cool white
	SpawnRectLight(World, FVector(320.0f, -220.0f, 330.0f), FRotator(-40.0f, 145.0f, 0.0f), 180.0f, FLinearColor(0.92f, 0.97f, 1.0f, 1.0f), FVector2D(240.0f, 180.0f));
	// Fill softbox — front-right, lower, neutral
	SpawnRectLight(World, FVector(280.0f, 260.0f, 200.0f), FRotator(-25.0f, -140.0f, 0.0f), 120.0f, FLinearColor(1.0f, 0.95f, 0.88f, 1.0f), FVector2D(300.0f, 200.0f));
	// Rim softbox — behind, cool cyan accent for edge separation
	SpawnRectLight(World, FVector(-300.0f, 60.0f, 300.0f), FRotator(-30.0f, 0.0f, 0.0f), 200.0f, FLinearColor(0.55f, 0.80f, 1.0f, 1.0f), FVector2D(280.0f, 160.0f));

	ADirectionalLight* Sun = World->SpawnActor<ADirectionalLight>(ADirectionalLight::StaticClass(), FVector(0.0f, 0.0f, 400.0f), FRotator(-35.0f, -35.0f, 0.0f));
	if (Sun)
	{
		if (UDirectionalLightComponent* Comp = Cast<UDirectionalLightComponent>(Sun->GetLightComponent()))
		{
			Comp->SetMobility(EComponentMobility::Movable);
			Comp->SetIntensity(1800.0f);
			Comp->SetSpecularScale(0.2f);
			Comp->SetLightColor(FLinearColor(1.0f, 0.96f, 0.88f, 1.0f));
		}
	}

	// Showroom spotlight — hangs above the podium, lights the rig directly.
	SpawnPointLight(World, FVector(0.0f, 0.0f, 520.0f), 260.0f, 900.0f, FLinearColor(1.0f, 0.98f, 0.94f, 1.0f), 0.2f);

	APostProcessVolume* Post = World->SpawnActor<APostProcessVolume>(APostProcessVolume::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
	if (Post)
	{
		Post->bUnbound = true;
		Post->Settings.bOverride_AutoExposureMethod = true;
		Post->Settings.AutoExposureMethod = EAutoExposureMethod::AEM_Basic;
		Post->Settings.bOverride_AutoExposureMinBrightness = true;
		Post->Settings.AutoExposureMinBrightness = 1.0f;
		Post->Settings.bOverride_AutoExposureMaxBrightness = true;
		Post->Settings.AutoExposureMaxBrightness = 1.0f;
		Post->Settings.bOverride_AutoExposureBias = true;
		Post->Settings.AutoExposureBias = 0.2f;
		Post->Settings.bOverride_BloomIntensity = true;
		Post->Settings.BloomIntensity = 0.08f;
		Post->Settings.bOverride_MotionBlurAmount = true;
		Post->Settings.MotionBlurAmount = 0.0f;
		Post->Settings.bOverride_Sharpen = true;
		Post->Settings.Sharpen = 1.4f;
		Post->Settings.bOverride_AmbientOcclusionIntensity = true;
		Post->Settings.AmbientOcclusionIntensity = 0.6f;
		Post->Settings.bOverride_AmbientOcclusionRadius = true;
		Post->Settings.AmbientOcclusionRadius = 60.0f;
	}

	// Only spawn our own skylight when the backdrop dome failed — the dome
	// brings its own HDRI-fed skylight.
	if (!bEnvDome)
	{
		ASkyLight* Sky = World->SpawnActor<ASkyLight>(ASkyLight::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
		if (Sky)
		{
			USkyLightComponent* Comp = Sky->GetLightComponent();
			if (Comp)
			{
				Comp->SetMobility(EComponentMobility::Movable);
				if (EnvCube)
				{
					Comp->SourceType = ESkyLightSourceType::SLS_SpecifiedCubemap;
					Comp->SetCubemap(EnvCube);
					Comp->SetIntensity(1.0f);
				}
				else
				{
					Comp->SetIntensity(1.4f);
					Comp->SetLightColor(FLinearColor(0.75f, 0.82f, 0.95f, 1.0f));
					Comp->bRealTimeCapture = true;
				}
			}
		}
	}
}

void AHangarGameMode::SetTurntablePaused(bool bPaused)
{
	if (Turntable)
	{
		FRotator Rate = Turntable->RotationRate;
		Rate.Yaw = bPaused ? 0.0f : TurntableRate;
		Turntable->RotationRate = Rate;
	}
}

void AHangarGameMode::SpawnBuilder()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogParamotor, Log, TEXT("HangarGameMode: no world"));
		return;
	}

	FVector SpawnLoc(0.0f, 0.0f, 2.0f);
	AParamotorBuilder* Builder = World->SpawnActor<AParamotorBuilder>(AParamotorBuilder::StaticClass(), SpawnLoc, FRotator::ZeroRotator);
	if (Builder)
	{
		Builder->SetActorScale3D(FVector::OneVector);
		if (PodiumActor)
		{
			Builder->AttachToActor(PodiumActor, FAttachmentTransformRules::KeepWorldTransform);
		}
		UE_LOG(LogParamotor, Log, TEXT("HangarGameMode: spawned ParamotorBuilder at %s"), *SpawnLoc.ToString());
		UE_LOG(LogParamotor, Log, TEXT("HangarGameMode: builder world loc=%s rot=%s scale=%s"),
			*Builder->GetActorLocation().ToString(), *Builder->GetActorRotation().ToString(), *Builder->GetActorScale3D().ToString());
	}
	else
	{
		UE_LOG(LogParamotor, Log, TEXT("HangarGameMode: failed to spawn ParamotorBuilder"));
	}
}

void AHangarGameMode::CreateMenu()
{
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!PC)
	{
		UE_LOG(LogParamotor, Log, TEXT("HangarGameMode: no player controller"));
		return;
	}

	UUW_HangarMenu* Menu = CreateWidget<UUW_HangarMenu>(PC, UUW_HangarMenu::StaticClass());
	if (Menu)
	{
		Menu->AddToViewport(0);
		// GameAndUI so LMB drags over the 3D preview reach the pawn (podium
		// rotation) while buttons still capture their own clicks.
		FInputModeGameAndUI Mode;
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		Mode.SetHideCursorDuringCapture(false);
		PC->SetInputMode(Mode);
		PC->bShowMouseCursor = true;
		UE_LOG(LogParamotor, Log, TEXT("HangarGameMode: Hangar menu added to viewport"));
	}
}
