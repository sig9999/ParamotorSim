// Copyright Epic Games, Inc. All Rights Reserved.

#include "ParamotorSandSystem.h"
#include "SurfaceService.h"

#include "ParamotorActor.h"
#include "ParamotorCharacter.h"
#include "ParamotorOceanSystem.h"
#include "ParamotorPlayerController.h"
#include "WingActor.h"
#include "ForestScatter.h"
#include "WaterBodyActor.h"
#include "WaterZoneActor.h"
#include "ParamotorLogging.h"

#include "ProceduralMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/StaticMesh.h"
#include "Engine/Canvas.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"

namespace
{
	// Deform RT resolution — must match the RT_SandDeform asset size.
	constexpr int32 DeformRes = 2048;
	// Terrain capture RT resolution — created at runtime, no asset needed.
	constexpr int32 TerrainRes = 256;
	// How far above the window the ortho capture floats (cm).
	constexpr float CaptureHeightCm = 6000.0f;
	// Capture far plane reach below the plane (cm) — must cover terrain dips.
	constexpr float CaptureRangeCm = 20000.0f;
}

AParamotorSandSystem::AParamotorSandSystem()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	OverlayMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Overlay"));
	OverlayMesh->SetupAttachment(RootComponent);
	OverlayMesh->bUseAsyncCooking = true;
	OverlayMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	OverlayMesh->SetCastShadow(true);

	TerrainCapture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("TerrainCapture"));
	TerrainCapture->SetupAttachment(RootComponent);
	TerrainCapture->ProjectionType = ECameraProjectionMode::Orthographic;
	TerrainCapture->CaptureSource = ESceneCaptureSource::SCS_SceneDepth;
	TerrainCapture->bCaptureEveryFrame = false;
	TerrainCapture->bCaptureOnMovement = false;
	TerrainCapture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_RenderScenePrimitives;
	TerrainCapture->bConsiderUnrenderedOpaquePixelAsFullyTranslucent = false;

	GrainISM = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Grains"));
	GrainISM->SetupAttachment(RootComponent);
	GrainISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GrainISM->SetCastShadow(false);
	GrainISM->NumCustomDataFloats = 8;

	DustISM = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Dust"));
	DustISM->SetupAttachment(RootComponent);
	DustISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DustISM->SetCastShadow(false);
	DustISM->NumCustomDataFloats = 8;
}

void AParamotorSandSystem::BeginPlay()
{
	Super::BeginPlay();

	RT_Deform = LoadObject<UTextureRenderTarget2D>(nullptr, TEXT("/Game/Textures/RT_SandDeform.RT_SandDeform"));
	RT_Scratch = LoadObject<UTextureRenderTarget2D>(nullptr, TEXT("/Game/Textures/RT_SandDeformB.RT_SandDeformB"));
	MPC = LoadObject<UMaterialParameterCollection>(nullptr, TEXT("/Game/Materials/MPC_Sand.MPC_Sand"));

	// Terrain depth RT is runtime-created — must be true float or cm-depth
	// saturates at 1.0 and the overlay launches to the capture plane.
	RT_Terrain = NewObject<UTextureRenderTarget2D>(this);
	RT_Terrain->InitCustomFormat(TerrainRes, TerrainRes, EPixelFormat::PF_R32_FLOAT, false);
	RT_Terrain->UpdateResourceImmediate(true);
	TerrainCapture->TextureTarget = RT_Terrain;

	UMaterialInterface* OverlayMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_SandOverlay.M_SandOverlay"));
	UMaterialInterface* ShiftMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_SandShift.M_SandShift"));
	if (OverlayMat) OverlayMID = UMaterialInstanceDynamic::Create(OverlayMat, this);
	if (ShiftMat) ShiftMID = UMaterialInstanceDynamic::Create(ShiftMat, this);
	if (OverlayMID) OverlayMesh->SetMaterial(0, OverlayMID);

	UMaterialInterface* FootBase = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_SandStampFoot.M_SandStampFoot"));
	UMaterialInterface* WashBase = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_SandStampWash.M_SandStampWash"));
	UMaterialInterface* ErodeBase = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_SandErode.M_SandErode"));
	UMaterialInterface* FadeBase = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_SandFade.M_SandFade"));
	if (FootBase) FootStampMID = UMaterialInstanceDynamic::Create(FootBase, this);
	if (WashBase) WashStampMID = UMaterialInstanceDynamic::Create(WashBase, this);
	if (ErodeBase) ErodeStampMID = UMaterialInstanceDynamic::Create(ErodeBase, this);
	if (FadeBase) FadeMID = UMaterialInstanceDynamic::Create(FadeBase, this);

	if (UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")))
		GrainISM->SetStaticMesh(Cube);
	if (UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")))
		DustISM->SetStaticMesh(Sphere);
	if (UMaterialInterface* GM = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_SandGrain.M_SandGrain")))
		GrainISM->SetMaterial(0, GM);
	if (UMaterialInterface* DM = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_SandDust.M_SandDust")))
		DustISM->SetMaterial(0, DM);

	// Find the ocean for real water level (wetness band anchor).
	{
		TArray<AActor*> Found;
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), AParamotorOceanSystem::StaticClass(), Found);
		if (Found.Num() > 0) Ocean = Cast<AParamotorOceanSystem>(Found[0]);
	}
	// Semantic surface resolver (globe maps only) — deformability gating.
	SurfSvc = ASurfaceService::Get(GetWorld());

	// The capture must not see ourselves — feedback loop — or the pawn/props.
	TerrainCapture->HiddenActors.Add(this);
	TerrainCapture->HideComponent(OverlayMesh);
	TerrainCapture->HideComponent(GrainISM);
	TerrainCapture->HideComponent(DustISM);

	// Start clean — the RT assets persist between sessions.
	if (RT_Deform) UKismetRenderingLibrary::ClearRenderTarget2D(this, RT_Deform, FLinearColor::Black);
	if (RT_Scratch) UKismetRenderingLibrary::ClearRenderTarget2D(this, RT_Scratch, FLinearColor::Black);
}

void AParamotorSandSystem::EnsureInitialized()
{
	if (bInitialized) return;
	CachedPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!CachedPawn) return;

	const float Texel = RegionSizeCm / (float)DeformRes;
	RegionCenter.X = FMath::FloorToFloat(CachedPawn->GetActorLocation().X / Texel) * Texel;
	RegionCenter.Y = FMath::FloorToFloat(CachedPawn->GetActorLocation().Y / Texel) * Texel;
	RegionOriginCm = RegionCenter - RegionSizeCm * 0.5f;
	BuildOverlayMesh();
	RefreshCaptureHiddenActors();
	RecaptureTerrain();
	UpdateMPC();
	SetOverlayParams();

	// Water surface is not ground — hide every water body + the flat fallback
	// plane so the overlay conforms to the sand UNDER shallow water.
	{
		TArray<AActor*> WaterActors;
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), AWaterBody::StaticClass(), WaterActors);
		for (AActor* W : WaterActors) TerrainCapture->HiddenActors.AddUnique(W);
		// The ocean's wave surface lives on the AWaterZone, not a WaterBody —
		// if the capture sees wave crests, overlay verts lift into walls.
		TArray<AActor*> Zones;
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), AWaterZone::StaticClass(), Zones);
		for (AActor* Z : Zones) TerrainCapture->HiddenActors.AddUnique(Z);
		if (Ocean)
		{
			if (AActor* FP = Ocean->GetFallbackPlane()) TerrainCapture->HiddenActors.AddUnique(FP);
		}
	}
	// Trees are above-ground clutter — a canopy must not lift the sand field.
	{
		TArray<AActor*> Forests;
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), AForestScatter::StaticClass(), Forests);
		for (AActor* F : Forests) TerrainCapture->HiddenActors.AddUnique(F);
	}
	OverlayMesh->SetVisibility(false);   // shown by UpdateActive when on sand
	bInitialized = true;
}

// ============================================================
// WINDOW / REGION
// ============================================================

void AParamotorSandSystem::UpdateMPC()
{
	if (!MPC) return;
	UMaterialParameterCollectionInstance* Inst = GetWorld()->GetParameterCollectionInstance(MPC);
	if (!Inst) return;
	Inst->SetVectorParameterValue(TEXT("SandDeformOrigin"), FLinearColor(RegionOriginCm.X, RegionOriginCm.Y, 0.0f, 0.0f));
	Inst->SetScalarParameterValue(TEXT("SandDeformSize"), RegionSizeCm);
	Inst->SetScalarParameterValue(TEXT("SandCaptureZ"), CaptureZ);
	Inst->SetScalarParameterValue(TEXT("SandActive"), bActive ? 1.0f : 0.0f);
	float WaterZ = 0.0f;
	if (Ocean && CachedPawn) WaterZ = Ocean->GetWaterHeightAt(CachedPawn->GetActorLocation());
	Inst->SetScalarParameterValue(TEXT("SandWaterZ"), WaterZ);
	if (OverlayMID) OverlayMID->SetScalarParameterValue(TEXT("WaterZ"), WaterZ);
}

void AParamotorSandSystem::SetOverlayParams()
{
	if (!OverlayMID) return;
	OverlayMID->SetVectorParameterValue(TEXT("RegionOrigin"), FLinearColor(RegionOriginCm.X, RegionOriginCm.Y, 0.0f, 0.0f));
	OverlayMID->SetScalarParameterValue(TEXT("RegionSize"), RegionSizeCm);
	OverlayMID->SetScalarParameterValue(TEXT("CaptureZ"), CaptureZ);
	OverlayMID->SetScalarParameterValue(TEXT("Lift"), OverlayLiftCm);
	OverlayMID->SetTextureParameterValue(TEXT("DeformRT"), RT_Deform);
	OverlayMID->SetTextureParameterValue(TEXT("TerrainRT"), RT_Terrain);
}

void AParamotorSandSystem::BuildOverlayMesh()
{
	// Flat local-space grid centered on the component origin; the material
	// displaces every vertex to terrainZ + deform. UVs are window UVs.
	const int32 N = FMath::Max(32, GridVerts);
	TArray<FVector> Verts;
	TArray<int32> Tris;
	TArray<FVector2D> UVs;
	TArray<FVector> Normals;
	TArray<FProcMeshTangent> Tangents;
	Verts.SetNumUninitialized(N * N);
	UVs.SetNumUninitialized(N * N);
	Normals.Init(FVector::UpVector, N * N);
	Tangents.Init(FProcMeshTangent(1, 0, 0), N * N);

	const float Step = RegionSizeCm / (N - 1);
	const float Half = RegionSizeCm * 0.5f;
	for (int32 Y = 0; Y < N; ++Y)
		for (int32 X = 0; X < N; ++X)
		{
			const int32 I = Y * N + X;
			Verts[I] = FVector(X * Step - Half, Y * Step - Half, 0.0f);
			UVs[I] = FVector2D((float)X / (N - 1), (float)Y / (N - 1));
		}
	Tris.Reserve((N - 1) * (N - 1) * 6);
	for (int32 Y = 0; Y < N - 1; ++Y)
		for (int32 X = 0; X < N - 1; ++X)
		{
			const int32 A = Y * N + X, B = A + 1, C = A + N, D = C + 1;
			Tris.Add(A); Tris.Add(B); Tris.Add(C);
			Tris.Add(B); Tris.Add(D); Tris.Add(C);
		}

	OverlayMesh->CreateMeshSection(0, Verts, Tris, Normals, UVs, TArray<FColor>(), Tangents, false);
	const float MeshZ = CachedPawn ? CachedPawn->GetActorLocation().Z : 0.0f;
	OverlayMesh->SetWorldLocation(FVector(RegionCenter.X, RegionCenter.Y, MeshZ));
	// WPO displaces verts to terrainZ — keep culling bounds generous.
	OverlayMesh->BoundsScale = 4.0f;
}

void AParamotorSandSystem::RefreshCaptureHiddenActors()
{
	if (!TerrainCapture || !CachedPawn) return;
	TerrainCapture->HiddenActors.AddUnique(CachedPawn);
	// Spawned gear appears after init — its depth must not contaminate terrain Z.
	if (AParamotorPlayerController* PC = Cast<AParamotorPlayerController>(CachedPawn->GetController()))
	{
		if (AActor* PA = PC->GetSpawnedParamotorActor()) TerrainCapture->HiddenActors.AddUnique(PA);
		if (AActor* WA = PC->SpawnedWingActor) TerrainCapture->HiddenActors.AddUnique(WA);
	}
}

void AParamotorSandSystem::RecaptureTerrain()
{
	if (!TerrainCapture || !RT_Terrain) return;
	CaptureZ = 0.0f;
	if (CachedPawn) CaptureZ = CachedPawn->GetActorLocation().Z + CaptureHeightCm;
	TerrainCapture->SetWorldLocation(FVector(RegionCenter.X, RegionCenter.Y, CaptureZ));
	TerrainCapture->SetWorldRotation(FRotator(-90.0f, 0.0f, 0.0f));
	TerrainCapture->OrthoWidth = RegionSizeCm;
	TerrainCapture->CaptureScene();
}

void AParamotorSandSystem::UpdateRegion(bool bForce)
{
	if (!CachedPawn) return;
	const FVector L = CachedPawn->GetActorLocation();
	const float Texel = RegionSizeCm / (float)DeformRes;

	const float CX = FMath::FloorToFloat(L.X / Texel) * Texel;
	const float CY = FMath::FloorToFloat(L.Y / Texel) * Texel;
	const float DX = CX - RegionCenter.X;
	const float DY = CY - RegionCenter.Y;

	if (!bForce && FMath::Abs(DX) < RecenterStepCm && FMath::Abs(DY) < RecenterStepCm)
		return;

	// Scroll the deform field by whole texels — point-sampled copy, no smear.
	// newUV = oldUV - d/size  =>  newRT[u] = oldRT[u + d/size]
	const float ShiftU = DX / RegionSizeCm;
	const float ShiftV = DY / RegionSizeCm;
	if (ShiftMID && RT_Deform && RT_Scratch)
	{
		ShiftMID->SetTextureParameterValue(TEXT("PrevRT"), RT_Deform);
		ShiftMID->SetScalarParameterValue(TEXT("OffsetU"), ShiftU);
		ShiftMID->SetScalarParameterValue(TEXT("OffsetV"), ShiftV);
		UKismetRenderingLibrary::DrawMaterialToRenderTarget(this, RT_Scratch, ShiftMID);
		// Copy straight back into RT_Deform — it's the asset the landscape samples.
		ShiftMID->SetScalarParameterValue(TEXT("OffsetU"), 0.0f);
		ShiftMID->SetScalarParameterValue(TEXT("OffsetV"), 0.0f);
		ShiftMID->SetTextureParameterValue(TEXT("PrevRT"), RT_Scratch);
		UKismetRenderingLibrary::DrawMaterialToRenderTarget(this, RT_Deform, ShiftMID);
	}

	RegionCenter = FVector2D(CX, CY);
	RegionOriginCm = RegionCenter - RegionSizeCm * 0.5f;
	OverlayMesh->SetWorldLocation(FVector(RegionCenter.X, RegionCenter.Y, L.Z));
	RecaptureTerrain();
	UpdateMPC();
	SetOverlayParams();
}

// ============================================================
// ACTIVATION
// ============================================================

void AParamotorSandSystem::UpdateActive(APawn* Pawn)
{
		bool bWant = false;
	if (Pawn)
	{
		// Sand band: terrain under the pawn must be low (near waterline) and
		// the pawn must be close to the ground — flying means dormant.
		FHitResult Hit;
		FCollisionQueryParams Q;
		Q.AddIgnoredActor(Pawn);
		const FVector L = Pawn->GetActorLocation();
		if (GetWorld()->LineTraceSingleByChannel(Hit, L + FVector(0, 0, 200), L - FVector(0, 0, 5000), ECC_Visibility, Q))
		{
			const float GroundZ = Hit.ImpactPoint.Z;
			const float Above = L.Z - GroundZ;
			float WaterZ = 0.0f;
			if (Ocean) WaterZ = Ocean->GetWaterHeightAt(L);
			bWant = (Above < 4500.0f) && (GroundZ < WaterZ + SandMaxZCm);

			// Semantic gate: when the surface service resolves this point,
			// the semantic's Deformability01 overrides the geometric test —
			// wet sand deforms, a paved promenade at the same height doesn't.
			if (SurfSvc && PawnSurface.bResolved && PawnSurface.Set)
				bWant = (Above < 4500.0f) && PawnSurface.Set->Deformability01 >= 0.5f;
		}
	}
	if (bWant != bActive)
	{
		bActive = bWant;
		OverlayMesh->SetVisibility(bActive);
		if (bActive)
		{
			UpdateRegion(true);
		}
		else
		{
			UpdateMPC();  // SandActive=0 -> landscape sink releases
		}
	}
}

// ============================================================
// TICK
// ============================================================

void AParamotorSandSystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!bInitialized) { EnsureInitialized(); return; }
	if (!CachedPawn) return;

	// Refresh the pawn's surface semantics ~2 Hz — drives the sand-band gate.
	if (SurfSvc)
	{
		SurfQueryTimer -= DeltaTime;
		if (SurfQueryTimer <= 0.0f)
		{
			SurfQueryTimer = 0.5f;
			SurfSvc->ResolveAtWorldPos(CachedPawn->GetActorLocation(), PawnSurface);
		}
	}
	UpdateActive(CachedPawn);
	if (!bActive) return;

	// Shader Time node is engine real-time, not world seconds — feed world
	// time through the MPC every frame so grain/dust ages match T0 custom data.
	if (MPC)
		if (UMaterialParameterCollectionInstance* Inst = GetWorld()->GetParameterCollectionInstance(MPC))
			Inst->SetScalarParameterValue(TEXT("SandTime"), GetWorld()->GetTimeSeconds());

	UpdateRegion(false);

	// Water level drifts with waves — refresh MPC wetness anchor slowly.
	RecaptureTimer -= DeltaTime;
	if (RecaptureTimer <= 0.0f)
	{
		RecaptureTimer = 0.5f;
		UpdateMPC();
		RefreshCaptureHiddenActors();
	}

	// Natural smoothing: loose sand relaxes over ~4 minutes (wind, gravity).
	FadeTimer -= DeltaTime;
	if (FadeTimer <= 0.0f && FadeMID && RT_Deform)
	{
		FadeTimer = 1.0f;
		FadeMID->SetScalarParameterValue(TEXT("K"), 0.9971f);
		UKismetRenderingLibrary::DrawMaterialToRenderTarget(this, RT_Deform, FadeMID);
	}

	UpdateFootprints(CachedPawn, DeltaTime);
	UpdatePropWash(CachedPawn, DeltaTime);
}

// ============================================================
// STAMPS
// ============================================================

bool AParamotorSandSystem::WorldToUV(const FVector& WorldPos, FVector2D& OutUV) const
{
	OutUV.X = (WorldPos.X - RegionOriginCm.X) / RegionSizeCm;
	OutUV.Y = (WorldPos.Y - RegionOriginCm.Y) / RegionSizeCm;
	return OutUV.X >= 0.0f && OutUV.X <= 1.0f && OutUV.Y >= 0.0f && OutUV.Y <= 1.0f;
}

void AParamotorSandSystem::ApplySurfaceContact(const FVector& WorldPos, float YawDeg, float LengthCm, float WidthCm, float DepthCm, float RimCm)
{
	if (!bActive || !RT_Deform) return;
	FVector2D UV;
	if (!WorldToUV(WorldPos, UV)) return;

	if (!FootStampMID) return;
	const float Wet = GetWetnessAt(WorldPos);
	FootStampMID->SetScalarParameterValue(TEXT("Depth"), DepthCm);
	FootStampMID->SetScalarParameterValue(TEXT("Rim"), RimCm);
	FootStampMID->SetScalarParameterValue(TEXT("Wet"), Wet);
	FootStampMID->SetScalarParameterValue(TEXT("Seed"), FMath::FRand());

	UCanvas* Canvas = nullptr;
	FVector2D CanvasSize;
	FDrawToRenderTargetContext Ctx;
	UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, RT_Deform, Canvas, CanvasSize, Ctx);
	if (!Canvas) return;

	const float ResX = CanvasSize.X;
	const FVector2D Center(UV.X * ResX, UV.Y * CanvasSize.Y);
	const FVector2D ScreenSize(LengthCm / RegionSizeCm * ResX, WidthCm / RegionSizeCm * CanvasSize.Y);
	// ScreenPosition anchors the quad top-left; place so the quad's center
	// lands on the stamp point, then rotate about that center.
	Canvas->K2_DrawMaterial(FootStampMID, Center - ScreenSize * 0.5f, ScreenSize,
		FVector2D::ZeroVector, FVector2D(1.0f, 1.0f), -YawDeg, FVector2D(0.5f, 0.5f));
	UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, Ctx);
}

void AParamotorSandSystem::ApplyWashStamp(const FVector& WorldPos, const FVector2D& WashDir, float RadiusCm, float Strength01, float DeltaTime, float Wet01)
{
	if (!bActive || !RT_Deform) return;
	FVector2D UV;
	if (!WorldToUV(WorldPos, UV)) return;

	if (!WashStampMID) return;
	WashStampMID->SetScalarParameterValue(TEXT("Strength"), Strength01 * WashScourCmPerSec * DeltaTime);
	WashStampMID->SetScalarParameterValue(TEXT("Erode"), Strength01 * WashErodeMul * DeltaTime);
	WashStampMID->SetScalarParameterValue(TEXT("Seed"), FMath::FRand());
	WashStampMID->SetScalarParameterValue(TEXT("Wet"), Wet01);
	// Erode stamp (modulate) shrinks existing relief toward flat — this is
	// what actually dissolves footprints; additive alone can only dig.
	const float ErodeK = FMath::Clamp(Strength01 * WashErodeMul * DeltaTime * 0.5f, 0.0f, 0.5f);
	if (ErodeStampMID) ErodeStampMID->SetScalarParameterValue(TEXT("K"), ErodeK);

	UCanvas* Canvas = nullptr;
	FVector2D CanvasSize;
	FDrawToRenderTargetContext Ctx;
	UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, RT_Deform, Canvas, CanvasSize, Ctx);
	if (!Canvas) return;

	const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(WashDir.Y, WashDir.X));
	const float ResX = CanvasSize.X;
	const FVector2D Center(UV.X * ResX, UV.Y * CanvasSize.Y);
	// Wash stamp is elongated along the flow — 3x longer than wide.
	const FVector2D ScreenSize(RadiusCm * 3.0f / RegionSizeCm * ResX, RadiusCm * 2.0f / RegionSizeCm * CanvasSize.Y);
	const FVector2D TL = Center - ScreenSize * 0.5f;
	Canvas->K2_DrawMaterial(WashStampMID, TL, ScreenSize,
		FVector2D::ZeroVector, FVector2D(1.0f, 1.0f), -Yaw, FVector2D(0.5f, 0.5f));
	if (ErodeStampMID && ErodeK > 0.001f)
		Canvas->K2_DrawMaterial(ErodeStampMID, TL, ScreenSize,
			FVector2D::ZeroVector, FVector2D(1.0f, 1.0f), -Yaw, FVector2D(0.5f, 0.5f));
	UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, Ctx);
}

void AParamotorSandSystem::StampFootprint(const FVector& WorldPos, float YawDeg, bool bLeftFoot, float SpeedCmS)
{
	// Heavier steps sink more; wet sand takes a cleaner, slightly deeper print.
	const float SpeedT = FMath::Clamp((SpeedCmS - MinPrintSpeed) / 300.0f, 0.0f, 1.0f);
	const float Wet = GetWetnessAt(WorldPos);
	// Semantic deformability: cohesive wet sand holds a print, dry sand shifts.
	const float Deform = (PawnSurface.bResolved && PawnSurface.Set) ? PawnSurface.Set->Deformability01 : 1.0f;
	float Depth = FootDepthCm * FMath::Lerp(0.55f, 1.0f, SpeedT) * FMath::Lerp(1.0f, 1.15f, Wet) * Deform;
	float Rim = FootRimCm * FMath::Lerp(1.0f, 0.6f, Wet) * Deform;  // wet sand displaces less loose material
	ApplySurfaceContact(WorldPos, YawDeg, FootLengthCm, FootWidthCm, Depth, Rim);
}

// ============================================================
// WETNESS
// ============================================================

float AParamotorSandSystem::GetWetnessAt(const FVector& WorldPos) const
{
	FHitResult Hit;
	FCollisionQueryParams Q;
	if (CachedPawn) Q.AddIgnoredActor(CachedPawn);
	float GroundZ = WorldPos.Z;
	if (GetWorld()->LineTraceSingleByChannel(Hit, WorldPos + FVector(0, 0, 100), WorldPos - FVector(0, 0, 300), ECC_Visibility, Q))
		GroundZ = Hit.ImpactPoint.Z;
	// Service adds weather (METAR precip) on top of the waterline curve.
	if (SurfSvc) return SurfSvc->WetnessAt(WorldPos, GroundZ);
	return WetnessFromGroundZ(WorldPos, GroundZ);
}

float AParamotorSandSystem::WetnessFromGroundZ(const FVector& WorldPos, float GroundZ) const
{
	float WaterZ = 0.0f;
	if (Ocean) WaterZ = Ocean->GetWaterHeightAt(WorldPos);
	const float Above = GroundZ - WaterZ;   // cm above water surface
	// <15cm: soaked. 15-60: wet. 60-160: damp. >160: dry.
	return 1.0f - FMath::SmoothStep(15.0f, 160.0f, Above);
}

bool AParamotorSandSystem::IsSandAt(const FVector& WorldPos) const
{
	FVector2D UV;
	return WorldToUV(WorldPos, UV);
}

// ============================================================
// FOOTPRINTS — real bone contact, not a cadence timer
// ============================================================

void AParamotorSandSystem::UpdateFootprints(APawn* Pawn, float DeltaTime)
{
	ACharacter* Ch = Cast<ACharacter>(Pawn);
	if (!Ch || !Ch->GetMesh()) return;
	UCharacterMovementComponent* CMC = Ch->GetCharacterMovement();
	const float Speed2 = FVector2D(Ch->GetVelocity().X, Ch->GetVelocity().Y).Size();
	if (!CMC || !CMC->IsMovingOnGround()) { bLeftWasDown = bRightWasDown = false; return; }

	const float Speed = Speed2;
	if (Speed < MinPrintSpeed) { bLeftWasDown = bRightWasDown = false; return; }

	USkeletalMeshComponent* Mesh = Ch->GetMesh();
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const FName Bone = Side == 0 ? TEXT("foot_l") : TEXT("foot_r");
		if (Mesh->GetBoneIndex(Bone) == INDEX_NONE) continue;
		const FVector FootPos = Mesh->GetBoneLocation(Bone);

		FHitResult Hit;
		FCollisionQueryParams Q;
		Q.AddIgnoredActor(Pawn);
		bool bDown = false;
		const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, FootPos + FVector(0, 0, 40), FootPos - FVector(0, 0, 60), ECC_Visibility, Q);
		if (bHit)
		{
			// foot_l/foot_r are ankle bones riding ~10-20cm above the sole, and
			// this rig slides — the foot never holds still. A plant is the foot's
			// LOCAL MINIMUM gap: track per-foot min/max gap with fast-attack EMAs
			// and stamp when the foot enters the lower third of its motion range.
			const float Gap = FootPos.Z - Hit.ImpactPoint.Z;
			float& Lo = Side == 0 ? LoGapL : LoGapR;
			float& Hi = Side == 0 ? HiGapL : HiGapR;
			Lo += (Gap - Lo) * (Gap < Lo ? 1.0f : 0.6f * DeltaTime);
			Hi += (Gap - Hi) * (Gap > Hi ? 1.0f : 0.6f * DeltaTime);
			bDown = Gap < FMath::Max(Lo + (Hi - Lo) * 0.45f, Lo + 5.0f);
		}
		bool& WasDown = Side == 0 ? bLeftWasDown : bRightWasDown;
		if (bDown && !WasDown)
		{
			// Fresh plant — stamp at the bone, oriented by travel direction
			// (foot yaw tracks movement, not pelvis yaw).
			const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Ch->GetVelocity().Y, Ch->GetVelocity().X));
			StampFootprint(Hit.ImpactPoint, Yaw, Side == 0, Speed);
			UE_LOG(LogParamotor, Log, TEXT("Sand: %s stamp at %s"), Side == 0 ? TEXT("L") : TEXT("R"), *Hit.ImpactPoint.ToCompactString());
		}
		WasDown = bDown;
	}
}

// ============================================================
// PROP WASH
// ============================================================

void AParamotorSandSystem::UpdatePropWash(APawn* Pawn, float DeltaTime)
{
	AParamotorPlayerController* PC = Cast<AParamotorPlayerController>(Pawn ? Pawn->GetController() : nullptr);
	AParamotorActor* PA = PC ? PC->GetSpawnedParamotorActor() : nullptr;
	if (!PA) return;

	const float RPM = PA->GetPropRPM();
	if (RPM < WashMinRPM) { GrainEmitAccum = 0.0f; return; }

	// Prop world transform — real Codex prop when present, else the frame.
	// Wake direction follows the prop's own axis (pusher: wake goes out the
	// back = -prop forward), projected onto the ground plane.
	const FTransform PropT = PA->GetPropTransform();
	const FVector PropPos = PropT.GetLocation();
	FVector WashDir = -PropT.GetRotation().GetForwardVector();
	WashDir.Z = 0.0f;
	if (WashDir.IsNearlyZero()) WashDir = -PA->GetActorForwardVector();
	WashDir = WashDir.GetSafeNormal();

	const float RPMNorm = FMath::Clamp((RPM - WashMinRPM) / (7800.0f / 2.58f - WashMinRPM), 0.0f, 1.0f);
	const float PropR = 65.0f;  // 130cm prop radius
	const float TanSpread = FMath::Tan(FMath::DegreesToRadians(WashSpreadDeg));

	// March the expanding wake cone; where its lower edge reaches the ground,
	// the airflow scrubs the surface.
	const float Step = 60.0f;
	FRandomStream RS(FMath::FloorToInt(GetWorld()->GetTimeSeconds() * 61.0f));
	float GroundZ = PropPos.Z;
	float EmissionSum = 0.0f;

	for (float S = Step; S <= WashLengthCm; S += Step)
	{
		const FVector P = PropPos + WashDir * S;
		const float ConeR = PropR + S * TanSpread;
		const float Intensity = RPMNorm / (1.0f + (S / 450.0f) * (S / 450.0f));
		if (Intensity < 0.03f) break;

		// Find ground under this wake slice.
		FHitResult Hit;
		FCollisionQueryParams Q;
		Q.AddIgnoredActor(Pawn);
		Q.AddIgnoredActor(PA);
		if (!GetWorld()->LineTraceSingleByChannel(Hit, P + FVector(0, 0, 500), P - FVector(0, 0, 500), ECC_Visibility, Q))
			continue;
		GroundZ = Hit.ImpactPoint.Z;

		// Wake bottom edge must be near/below the surface to scrub it.
		const float WakeBottom = P.Z - ConeR;
		const float Overlap = WakeBottom - GroundZ;
		if (Overlap < -30.0f) continue;   // wake passes well overhead

		const float Contact = FMath::Clamp(1.0f + Overlap / ConeR, 0.0f, 1.0f);
		const float Wet = SurfSvc ? SurfSvc->WetnessAt(Hit.ImpactPoint, Hit.ImpactPoint.Z)
			: WetnessFromGroundZ(Hit.ImpactPoint, Hit.ImpactPoint.Z);
		const float Dry = 1.0f - Wet;
		// Surface response: the resolved semantic's phys profile scales how
		// much loose material the wake can lift (dry sand 1.0, wet sand ~0.15).
		float SurfMul = 1.0f;
		if (SurfSvc)
		{
			FResolvedSurface Surf;
			if (SurfSvc->ResolveAtWorldPos(Hit.ImpactPoint, Surf, Hit.ImpactPoint.Z) && Surf.PhysProfile)
				SurfMul = Surf.PhysProfile->WashGrainMul;
		}
		const float Eff = Intensity * Contact * FMath::Lerp(0.25f, 1.0f, Dry) * SurfMul;

		// Jittered scour stamp inside the cone's ground footprint.
		const FVector Jit(Hit.ImpactPoint.X + (RS.FRand() - 0.5f) * ConeR,
		                  Hit.ImpactPoint.Y + (RS.FRand() - 0.5f) * ConeR, GroundZ);
		ApplyWashStamp(Jit, FVector2D(WashDir), FMath::Min(ConeR, 160.0f), Eff, DeltaTime, Wet);

		EmissionSum += Eff;
		if (DebugMode >= 1 && Eff > 0.05f)
			DrawDebugSphere(GetWorld(), Jit, 20.0f, 8, FColor::Orange, false, 0.05f);
	}

	// Airborne grains — rate tracks how hard the wake scrubs dry ground.
	GrainEmitAccum += EmissionSum * GrainRate * DeltaTime * 0.25f;
	while (GrainEmitAccum >= 1.0f)
	{
		GrainEmitAccum -= 1.0f;
		const float S = Step * (1.0f + RS.FRand() * (WashLengthCm / Step - 1.0f) * 0.5f);
		const FVector Base = PropPos + WashDir * S;
		FHitResult Hit;
		FCollisionQueryParams Q;
		Q.AddIgnoredActor(Pawn);
		Q.AddIgnoredActor(PA);
		if (!GetWorld()->LineTraceSingleByChannel(Hit, Base + FVector(0, 0, 300), Base - FVector(0, 0, 300), ECC_Visibility, Q))
			continue;
		if (SurfSvc)
		{
			if (SurfSvc->WetnessAt(Hit.ImpactPoint, Hit.ImpactPoint.Z) > 0.7f) continue;  // wet sand barely lifts
		}
		else if (WetnessFromGroundZ(Hit.ImpactPoint, Hit.ImpactPoint.Z) > 0.7f) continue;

		const FVector Vel = WashDir * (600.0f + RS.FRand() * 1400.0f)
			+ FVector(0, 0, 80.0f + RS.FRand() * 260.0f)
			+ FVector(RS.FRand() - 0.5f, RS.FRand() - 0.5f, 0.0f) * 400.0f;
		EmitGrain(Hit.ImpactPoint + FVector(0, 0, 4.0f), Vel, 0.7f + RS.FRand() * 0.9f, RS.FRand());

		if (RS.FRand() < 0.22f)
		{
			const FVector DV = WashDir * (150.0f + RS.FRand() * 300.0f)
				+ FVector(0, 0, 30.0f + RS.FRand() * 60.0f);
			EmitDust(Hit.ImpactPoint + FVector(0, 0, 12.0f), DV, 1.6f + RS.FRand() * 1.6f, RS.FRand());
		}
	}

	DebugDraw(Pawn, PropPos, WashDir, RPMNorm);
}

// ============================================================
// AIRBORNE GRAINS — ISM pool, ballistic path evaluated in the material
// ============================================================

void AParamotorSandSystem::EmitGrain(const FVector& Pos, const FVector& Vel, float Life, float Size01)
{
	const float Size = FMath::Lerp(0.012f, 0.03f, Size01);  // 1.2-3cm visible grains
	const FTransform T(FRotator(FMath::FRand() * 360.0f, FMath::FRand() * 360.0f, 0.0f), Pos, FVector(Size));
	const float T0 = GetWorld()->GetTimeSeconds();

	int32 Idx;
	if (GrainISM->GetInstanceCount() < MaxGrains)
		Idx = GrainISM->AddInstance(T, true);
	else
	{
		Idx = GrainCursor;
		GrainISM->UpdateInstanceTransform(GrainCursor, T, true, true, false);
		GrainCursor = (GrainCursor + 1) % FMath::Max(1, GrainISM->GetInstanceCount());
	}
	if (Idx != INDEX_NONE)
	{
		GrainISM->SetCustomDataValue(Idx, 0, T0);
		GrainISM->SetCustomDataValue(Idx, 1, Life);
		GrainISM->SetCustomDataValue(Idx, 2, Vel.X);
		GrainISM->SetCustomDataValue(Idx, 3, Vel.Y);
		GrainISM->SetCustomDataValue(Idx, 4, Vel.Z);
	}
}

void AParamotorSandSystem::EmitDust(const FVector& Pos, const FVector& Vel, float Life, float Size01)
{
	const float Size = FMath::Lerp(0.5f, 1.4f, Size01);  // 50-140cm puffs
	const FTransform T(FRotator::ZeroRotator, Pos, FVector(Size, Size, Size * 0.45f));
	const float T0 = GetWorld()->GetTimeSeconds();

	int32 Idx;
	if (DustISM->GetInstanceCount() < MaxDust)
		Idx = DustISM->AddInstance(T, true);
	else
	{
		Idx = DustCursor;
		DustISM->UpdateInstanceTransform(DustCursor, T, true, true, false);
		DustCursor = (DustCursor + 1) % FMath::Max(1, DustISM->GetInstanceCount());
	}
	if (Idx != INDEX_NONE)
	{
		DustISM->SetCustomDataValue(Idx, 0, T0);
		DustISM->SetCustomDataValue(Idx, 1, Life);
		DustISM->SetCustomDataValue(Idx, 2, Vel.X);
		DustISM->SetCustomDataValue(Idx, 3, Vel.Y);
		DustISM->SetCustomDataValue(Idx, 4, Vel.Z);
	}
}

// ============================================================
// DEBUG
// ============================================================

void AParamotorSandSystem::DebugTestStamp()
{
	if (!CachedPawn) return;
	const FVector P = CachedPawn->GetActorLocation();
	// Big unmissable scoop — verifies the deform->WPO pipeline end to end.
	ApplySurfaceContact(P + FVector(0, 0, -90.0f), CachedPawn->GetActorRotation().Yaw,
		80.0f, 40.0f, 20.0f, 5.0f);
	UE_LOG(LogParamotor, Log, TEXT("Sand: test stamp at %s"), *P.ToCompactString());
}

void AParamotorSandSystem::DebugDump()
{
	UE_LOG(LogParamotor, Log, TEXT("Sand dump: init=%d active=%d center=(%.0f,%.0f) origin=(%.0f,%.0f) size=%.0f capZ=%.0f"),
		bInitialized, bActive, RegionCenter.X, RegionCenter.Y, RegionOriginCm.X, RegionOriginCm.Y, RegionSizeCm, CaptureZ);
	UE_LOG(LogParamotor, Log, TEXT("  RTs: deform=%s scratch=%s terrain=%s  MIDs: ov=%d foot=%d wash=%d erode=%d fade=%d"),
		RT_Deform ? TEXT("ok") : TEXT("null"), RT_Scratch ? TEXT("ok") : TEXT("null"), RT_Terrain ? TEXT("ok") : TEXT("null"),
		OverlayMID != nullptr, FootStampMID != nullptr, WashStampMID != nullptr, ErodeStampMID != nullptr, FadeMID != nullptr);
	UE_LOG(LogParamotor, Log, TEXT("  overlay vis=%d loc=%s verts=%d grains=%d dust=%d"),
		OverlayMesh->IsVisible(), *OverlayMesh->GetComponentLocation().ToCompactString(),
		OverlayMesh->GetNumSections() > 0 ? 1 : 0, GrainISM->GetInstanceCount(), DustISM->GetInstanceCount());
	if (CachedPawn)
	{
		float Wet = GetWetnessAt(CachedPawn->GetActorLocation());
		UE_LOG(LogParamotor, Log, TEXT("  pawn=%s wet=%.2f"), *CachedPawn->GetActorLocation().ToCompactString(), Wet);
	}
	// Read back the terrain-depth RT center — verifies the capture pipeline.
	if (RT_Terrain)
	{
		FLinearColor C0 = UKismetRenderingLibrary::ReadRenderTargetUV(this, RT_Terrain, 0.5f, 0.5f);
		FLinearColor C1 = UKismetRenderingLibrary::ReadRenderTargetUV(this, RT_Terrain, 0.25f, 0.25f);
		UE_LOG(LogParamotor, Log, TEXT("  terrainRT center=%.1f corner=%.1f  -> Z %.0f / %.0f"),
			C0.R, C1.R, CaptureZ - C0.R, CaptureZ - C1.R);
	}
	if (RT_Deform)
	{
		// Sample a 5x5 grid — a center-only read can sit on a lone clean pixel.
		for (int32 Y = 1; Y <= 3; ++Y)
		{
			FString Row;
			for (int32 X = 1; X <= 3; ++X)
			{
				FLinearColor D = UKismetRenderingLibrary::ReadRenderTargetUV(this, RT_Deform, X * 0.25f, Y * 0.25f);
				Row += FString::Printf(TEXT("(%.2f,%.2f) "), D.R, D.G);
			}
			UE_LOG(LogParamotor, Log, TEXT("  deformRT r%d: %s"), Y, *Row);
		}
	}
}

void AParamotorSandSystem::DebugSetView(int32 V)
{
	if (OverlayMID)
	{
		OverlayMID->SetScalarParameterValue(TEXT("DbgView"), (float)V);
		UE_LOG(LogParamotor, Log, TEXT("Sand: DbgView=%d"), V);
	}
}



void AParamotorSandSystem::DebugDraw(APawn* Pawn, const FVector& PropPos, const FVector& WashDir, float Strength) const
{
	if (DebugMode < 1) return;
	// Window border
	const FVector C(RegionCenter.X, RegionCenter.Y, Pawn->GetActorLocation().Z);
	DrawDebugBox(GetWorld(), C, FVector(RegionSizeCm * 0.5f, RegionSizeCm * 0.5f, 20.0f), FColor::Green, false, 0.05f);
	// Prop axis + wake cone
	DrawDebugLine(GetWorld(), PropPos, PropPos + WashDir * 400.0f, FColor::Red, false, 0.05f, 0, 2.0f);
	DrawDebugCircle(GetWorld(), PropPos, 65.0f, 24, FColor::Yellow, false, 0.05f, 0, 1.5f,
		FVector::RightVector, FVector::UpVector, false);
	if (DebugMode >= 2)
		UE_LOG(LogParamotor, Log, TEXT("Sand: wash strength %.2f, grains %d"), Strength, GrainISM->GetInstanceCount());
}
