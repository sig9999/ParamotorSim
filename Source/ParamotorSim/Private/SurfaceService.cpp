// Copyright Epic Games, Inc. All Rights Reserved.

#include "SurfaceService.h"
#include "LandCoverService.h"
#include "VegetationField.h"
#include "ParamotorOceanSystem.h"
#include "ParamotorGameInstance.h"
#include "CesiumWorldHelpers.h"
#include "ParamotorLogging.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Pawn.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "ProceduralMeshComponent.h"
#include "DrawDebugHelpers.h"

ASurfaceService::ASurfaceService()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.15f;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	DetailPatch = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("DetailPatch"));
	DetailPatch->SetupAttachment(RootComponent);
	DetailPatch->SetVisibility(false);
	DetailPatch->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DetailPatch->bReceivesDecals = false;
	DetailPatch->SetCastShadow(false);
}

ASurfaceService* ASurfaceService::Get(UWorld* World)
{
	if (!World) return nullptr;
	TArray<AActor*> Found;
	UGameplayStatics::GetAllActorsOfClass(World, ASurfaceService::StaticClass(), Found);
	return Found.Num() ? Cast<ASurfaceService>(Found[0]) : nullptr;
}

void ASurfaceService::BeginPlay()
{
	Super::BeginPlay();

	TArray<AActor*> Found;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), ALandCoverService::StaticClass(), Found);
	LandCover = Found.Num() ? Cast<ALandCoverService>(Found[0]) : nullptr;
	Found.Reset();
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AVegetationField::StaticClass(), Found);
	VegField = Found.Num() ? Cast<AVegetationField>(Found[0]) : nullptr;
	Found.Reset();
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AParamotorOceanSystem::StaticClass(), Found);
	Ocean = Found.Num() ? Cast<AParamotorOceanSystem>(Found[0]) : nullptr;

	Library = USurfaceLibrary::Get();
}

// ---- resolution -----------------------------------------------------------

float ASurfaceService::WeatherWetness() const
{
	const UParamotorGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance<UParamotorGameInstance>() : nullptr;
	if (!GI || GI->LiveMetarRaw.IsEmpty()) return 0.0f;
	// METAR precipitation descriptors — cheap substring scan; good enough for
	// a wetness bias until a real precipitation channel exists.
	const FString& M = GI->LiveMetarRaw;
	if (M.Contains(TEXT("+RA")) || M.Contains(TEXT("SHRA")) || M.Contains(TEXT("TS"))) return 0.9f;
	if (M.Contains(TEXT("RA")) || M.Contains(TEXT("SN")) || M.Contains(TEXT("FZRA"))) return 0.7f;
	if (M.Contains(TEXT("DZ")) || M.Contains(TEXT("-RA"))) return 0.4f;
	return 0.0f;
}

float ASurfaceService::WetnessAt(const FVector& Pos, float GroundZ) const
{
	float Wet = WeatherWetness();
	if (Ocean)
	{
		// Same waterline curve the sand overlay uses: <15cm soaked,
		// 15-60 wet, 60-160 damp, >160 dry.
		const float Above = GroundZ - Ocean->GetWaterHeightAt(Pos);
		Wet = FMath::Max(Wet, 1.0f - FMath::SmoothStep(15.0f, 160.0f, Above));
	}
	return Wet;
}

bool ASurfaceService::ResolveCore(const FLandCoverPixel& Px, double Lat, double Lon,
	double GroundZM, const FVector& Pos, float GroundWorldZ, FResolvedSurface& Out) const
{
	Out = FResolvedSurface();
	if (!VegField || !Library) return false;

	FVegContext Ctx;
	if (!VegField->BuildContextAt(Lat, Lon, GroundZM, Ctx)) return false;

	ESurfaceSemantic Sem = VegSem::ClassifySurface(Px, Ctx);

	// Waterline refinement — land-cover is ~22m/px, so real water edges are
	// sharpened against the live water datum:
	//   submerged ground        → ShallowWater (depth < ~1.5 m) / OpenWater
	//   sand within the swash zone → WetSand
	float AboveWaterCm = TNumericLimits<float>::Max();
	if (Ocean && GroundWorldZ != TNumericLimits<float>::Max())
	{
		const float WaterZ = Ocean->GetWaterHeightAt(Pos);
		AboveWaterCm = GroundWorldZ - WaterZ;
		if (AboveWaterCm < -150.0f)
			Sem = ESurfaceSemantic::OpenWater;
		else if (AboveWaterCm < 0.0f)
			Sem = ESurfaceSemantic::ShallowWater;
		else if (VegSem::IsSandLike(Sem) && AboveWaterCm < 60.0f)
			Sem = ESurfaceSemantic::WetSand;
	}
	Out.AboveWaterCm = AboveWaterCm;
	Out.Semantic = Sem;

	// Wetness: waterline + weather. Wetness above the counterpart threshold
	// re-labels the effective surface (DrySand→WetSand, Dirt→Mud).
	float Wet = WeatherWetness();
	if (AboveWaterCm != TNumericLimits<float>::Max())
		Wet = FMath::Max(Wet, 1.0f - FMath::SmoothStep(15.0f, 160.0f, AboveWaterCm));
	Out.Wetness01 = Wet;

	const FSurfaceAssetSet* Set = Library->Find(Sem);
	ESurfaceSemantic Effective = Sem;
	if (Set && Set->WetCounterpart != ESurfaceSemantic::Unknown && Wet > 0.6f)
		Effective = Set->WetCounterpart;
	if (Effective != Sem)
		Set = Library->Find(Effective);

	Out.EffectiveSemantic = Effective;
	Out.Set = Set;
	Out.Phys = Set ? Set->Phys : Library->PhysTypeFor(Effective);
	Out.PhysProfile = Library->FindPhys(Out.Phys);
	Out.bResolved = true;
	return true;
}

bool ASurfaceService::ResolveAtLL(double Lat, double Lon, double GroundZM, FResolvedSurface& Out) const
{
	Out = FResolvedSurface();
	if (!LandCover) return false;
	FLandCoverPixel Px;
	if (!LandCover->GetPixelAt(Lat, Lon, Px)) return false;
	const FVector Pos = CesiumWorld::GeodeticToUnreal(GetWorld(), Lon, Lat, GroundZM);
	return ResolveCore(Px, Lat, Lon, GroundZM, Pos, TNumericLimits<float>::Max(), Out);
}

bool ASurfaceService::ResolveAtWorldPos(const FVector& Pos, FResolvedSurface& Out, float KnownGroundZ) const
{
	Out = FResolvedSurface();
	if (!LandCover) return false;

	const FVector LLH = CesiumWorld::UnrealToGeodetic(GetWorld(), Pos);
	if (LLH.IsNearlyZero()) return false;

	FLandCoverPixel Px;
	if (!LandCover->GetPixelAt(LLH.Y, LLH.X, Px)) return false;

	// Ground Z for waterline tests — use the caller's trace when provided,
	// else the position itself (pawn-standing case), else trace once.
	float GroundZ = KnownGroundZ;
	if (GroundZ == TNumericLimits<float>::Max())
	{
		FHitResult Hit;
		FCollisionQueryParams Q;
		APawn* Pawn = GetWorld()->GetFirstPlayerController() ? GetWorld()->GetFirstPlayerController()->GetPawn() : nullptr;
		if (Pawn) Q.AddIgnoredActor(Pawn);
		GroundZ = GetWorld()->LineTraceSingleByChannel(Hit,
			Pos + FVector(0, 0, 400), Pos - FVector(0, 0, 800), ECC_Visibility, Q)
			? Hit.ImpactPoint.Z : Pos.Z;
	}
	return ResolveCore(Px, LLH.Y, LLH.X, LLH.Z, Pos, GroundZ, Out);
}

// ---- tick -----------------------------------------------------------------

void ASurfaceService::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!LandCover || !VegField) return;

	// The ocean system can spawn after us — retry the lookup until found.
	if (!Ocean)
	{
		TArray<AActor*> Found;
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), AParamotorOceanSystem::StaticClass(), Found);
		if (Found.Num()) Ocean = Cast<AParamotorOceanSystem>(Found[0]);
	}

	APawn* Pawn = GetWorld()->GetFirstPlayerController() ? GetWorld()->GetFirstPlayerController()->GetPawn() : nullptr;
	if (!Pawn) { PawnSurface = FResolvedSurface(); DetailPatch->SetVisibility(false); return; }

	const FVector P = Pawn->GetActorLocation();
	ResolveTimer -= DeltaTime;
	if (ResolveTimer <= 0.0f || FVector::DistSquared(P, LastResolvePos) > 100.0f * 100.0f)
	{
		ResolveTimer = 0.15f;
		LastResolvePos = P;
		ResolveAtWorldPos(P, PawnSurface);
	}

	UpdateDetailPatch(Pawn);
	if (DebugViz > 0) DrawDebugSurface();
}

// ---- near-field detail patch ----------------------------------------------
// A flat translucent disc that follows the pawn and renders the semantic's
// DetailPatchMaterial — close-range material fidelity layered over real
// photogrammetry. Hidden when the semantic binds no material (the default)
// or when the pawn is airborne.

void ASurfaceService::BuildDetailPatch(float RadiusM)
{
	// Radial fan: 1 center + 6 rings × 24 segments. UV = normalized plan space.
	const int32 Segs = 24, Rings = 6;
	TArray<FVector> Verts;
	TArray<int32> Tris;
	TArray<FVector2D> UVs;
	Verts.Add(FVector::ZeroVector);
	UVs.Add(FVector2D(0.5f, 0.5f));
	for (int32 r = 1; r <= Rings; ++r)
	{
		const float Fr = (float)r / Rings;
		for (int32 s = 0; s < Segs; ++s)
		{
			const float A = 2.0f * PI * s / Segs;
			Verts.Add(FVector(FMath::Cos(A) * Fr * 100.0f, FMath::Sin(A) * Fr * 100.0f, 0.0f));
			UVs.Add(FVector2D(0.5f + 0.5f * FMath::Cos(A) * Fr, 0.5f + 0.5f * FMath::Sin(A) * Fr));
		}
	}
	for (int32 s = 0; s < Segs; ++s) // center fan (wound to face +Z)
		Tris.Add(0), Tris.Add(1 + (s + 1) % Segs), Tris.Add(1 + s);
	for (int32 r = 0; r < Rings - 1; ++r)
		for (int32 s = 0; s < Segs; ++s)
		{
			const int32 A0 = 1 + r * Segs + s, A1 = 1 + r * Segs + (s + 1) % Segs;
			const int32 B0 = A0 + Segs, B1 = A1 + Segs;
			Tris.Add(A0); Tris.Add(B1); Tris.Add(B0);
			Tris.Add(A0); Tris.Add(A1); Tris.Add(B1);
		}
	TArray<FVector> Normals; Normals.Init(FVector::UpVector, Verts.Num());
	TArray<FLinearColor> Colors; Colors.Init(FLinearColor::White, Verts.Num());
	DetailPatch->CreateMeshSection_LinearColor(0, Verts, Tris, Normals, UVs, Colors,
		TArray<FProcMeshTangent>(), false);
	DetailPatch->SetWorldScale3D(FVector(RadiusM));
}

void ASurfaceService::UpdateDetailPatch(APawn* Pawn)
{
	const FResolvedSurface& S = PawnSurface;
	const FSurfaceAssetSet* Set = S.Set;
	UMaterialInterface* Mat = Set ? Set->DetailPatchMaterial.Get() : nullptr;

	// Ground-fidelity layer: only when a material is bound AND a surface is
	// within 15 m under the pawn (dormant in flight).
	bool bWant = Mat != nullptr;
	if (bWant)
	{
		FHitResult Hit;
		FCollisionQueryParams Q; Q.AddIgnoredActor(Pawn);
		const FVector L = Pawn->GetActorLocation();
		bWant = GetWorld()->LineTraceSingleByChannel(Hit, L + FVector(0, 0, 100), L - FVector(0, 0, 1500), ECC_Visibility, Q);
		if (bWant)
		{
			// +30cm: Cesium/PRT render surfaces can sit well above their
			// collision hull — the disc must clear the rendered tile, not
			// just the trace hit, or it is buried under the photogrammetry.
			DetailPatch->SetWorldLocation(FVector(L.X, L.Y, Hit.ImpactPoint.Z + 30.0f));
			DetailPatch->SetWorldScale3D(FVector(FMath::Max(4.0f, Set->DetailRadiusM)));
			DetailPatch->BoundsScale = 4.0f;
		}
	}
	if (bWant && Mat)
	{
		// Section build is deferred to first use — a PMC section created in
		// the ctor (pre-registration) never reaches the scene proxy.
		if (DetailPatch->GetNumSections() == 0) BuildDetailPatch(1.0f);
		if (DetailSemantic != S.EffectiveSemantic || !DetailMID || DetailMID->GetMaterial() != Mat)
		{
			DetailMID = UMaterialInstanceDynamic::Create(Mat, this);
			DetailPatch->SetMaterial(0, DetailMID);
			DetailSemantic = S.EffectiveSemantic;
		}
		if (DetailMID)
		{
			DetailMID->SetScalarParameterValue(TEXT("Wetness"), S.Wetness01);
			DetailMID->SetScalarParameterValue(TEXT("SurfTime"), GetWorld()->GetTimeSeconds());
		}
	}
	if (DetailPatch->IsVisible() != bWant)
		DetailPatch->SetVisibility(bWant);
}

// ---- dev diagnostics --------------------------------------------------------

FColor ASurfaceService::DebugColorFor(ESurfaceSemantic S)
{
	switch (S)
	{
	case ESurfaceSemantic::OpenWater:
	case ESurfaceSemantic::ShallowWater:   return FColor(30, 90, 255);
	case ESurfaceSemantic::WetSand:        return FColor(160, 140, 80);
	case ESurfaceSemantic::DrySand:
	case ESurfaceSemantic::Dune:           return FColor(235, 215, 140);
	case ESurfaceSemantic::Mud:            return FColor(90, 60, 40);
	case ESurfaceSemantic::BareSoil:
	case ESurfaceSemantic::Dirt:           return FColor(140, 90, 50);
	case ESurfaceSemantic::Gravel:         return FColor(150, 150, 150);
	case ESurfaceSemantic::Rock:           return FColor(90, 90, 100);
	case ESurfaceSemantic::IceSnow:        return FColor(220, 240, 255);
	default: break;
	}
	if (VegSem::IsPaved(S))      return FColor(60, 60, 65);
	if (VegSem::IsVegetated(S))  return S == ESurfaceSemantic::Marsh || S == ESurfaceSemantic::Wetland
		? FColor(60, 160, 120) : FColor(80, 180, 80);
	return FColor(120, 120, 120);
}

void ASurfaceService::DrawDebugSurface()
{
	APawn* Pawn = GetWorld()->GetFirstPlayerController() ? GetWorld()->GetFirstPlayerController()->GetPawn() : nullptr;
	if (!Pawn) return;
	const FVector L = Pawn->GetActorLocation();
	const float Spacing = 1500.0f; // 15 m grid
	for (int32 Y = -4; Y <= 4; ++Y)
		for (int32 X = -4; X <= 4; ++X)
		{
			const FVector P(L.X + X * Spacing, L.Y + Y * Spacing, L.Z);
			FHitResult Hit;
			FCollisionQueryParams Q; Q.AddIgnoredActor(Pawn);
			if (!GetWorld()->LineTraceSingleByChannel(Hit, P + FVector(0, 0, 3000), P - FVector(0, 0, 3000), ECC_Visibility, Q))
				continue;
			FResolvedSurface R;
			if (!ResolveAtWorldPos(P, R, Hit.ImpactPoint.Z)) continue;
			DrawDebugCylinder(GetWorld(), Hit.ImpactPoint + FVector(0, 0, 60),
				Hit.ImpactPoint + FVector(0, 0, 90), Spacing * 0.45f, 12,
				DebugColorFor(R.EffectiveSemantic), false, 0.2f);
		}
}

void ASurfaceService::DumpAt(const FVector& Pos)
{
	FResolvedSurface R;
	if (!ResolveAtWorldPos(Pos, R))
	{
		UE_LOG(LogParamotor, Warning, TEXT("PsSurfGeo: unresolved (land-cover tile pending or no services)"));
		return;
	}
	const FVector LLH = CesiumWorld::UnrealToGeodetic(GetWorld(), Pos);
	FLandCoverPixel Px; Px.Cover = 0;
	if (LandCover) LandCover->GetPixelAt(LLH.Y, LLH.X, Px);

	auto SoftName = [](const TSoftObjectPtr<UMaterialInterface>& P) -> FString
	{ return P.IsNull() ? TEXT("none") : P.GetAssetName(); };
	auto SoftPhys = [](const TSoftObjectPtr<UPhysicalMaterial>& P) -> FString
	{ return P.IsNull() ? TEXT("none") : P.GetAssetName(); };

	UE_LOG(LogParamotor, Warning, TEXT(
		"PsSurfGeo @ %.5f,%.5f | NLCD=%d imperv=%d road=%d canopy=%d\n"
		"  semantic=%s -> effective=%s | phys=%s\n"
		"  wet=%.2f aboveWater=%s\n"
		"  detailMat=%s | physMat=%s | footstep=%s | wash grain=%.2f dust=%.2f\n"
		"  deform=%.2f track=%.2f friction=%.2f | patch: vis=%d loc=%s mid=%s"),
		LLH.Y, LLH.X, Px.Cover, Px.Impervious, Px.Road, Px.Canopy,
		VegSem::ToName(R.Semantic), VegSem::ToName(R.EffectiveSemantic), SurfPhysTypeName(R.Phys),
		R.Wetness01, R.AboveWaterCm == TNumericLimits<float>::Max() ? TEXT("n/a") : *FString::Printf(TEXT("%.0fcm"), R.AboveWaterCm),
		R.Set ? *SoftName(R.Set->DetailPatchMaterial) : TEXT("none"),
		R.PhysProfile ? *SoftPhys(R.PhysProfile->PhysMat) : TEXT("none"),
		R.PhysProfile ? (R.PhysProfile->FootstepFallback == EFootstepFallback::Grass ? TEXT("Grass*") : TEXT("Dirt*")) : TEXT("none"),
		R.PhysProfile ? R.PhysProfile->WashGrainMul : 0.0f,
		R.PhysProfile ? R.PhysProfile->WashDustMul : 0.0f,
		R.Set ? R.Set->Deformability01 : 0.0f,
		R.Set ? R.Set->Trackability01 : 0.0f,
		R.PhysProfile ? R.PhysProfile->FrictionScalar : 0.0f,
		DetailPatch->IsVisible() ? 1 : 0,
		*DetailPatch->GetComponentLocation().ToCompactString(),
		DetailMID ? *DetailMID->GetMaterial()->GetName() : TEXT("none"));
	APawn* Pawn = GetWorld()->GetFirstPlayerController() ? GetWorld()->GetFirstPlayerController()->GetPawn() : nullptr;
	UE_LOG(LogParamotor, Warning, TEXT(
		"  patchdiag: secs=%d scale=%.1f bounds=%.0f pawn=%s registered=%d"),
		DetailPatch->GetNumSections(),
		DetailPatch->GetComponentScale().X,
		DetailPatch->Bounds.GetSphere().W,
		Pawn ? *Pawn->GetActorLocation().ToCompactString() : TEXT("none"),
		DetailPatch->IsRegistered() ? 1 : 0);
}

void ASurfaceService::DumpLibrary() const
{
	if (!Library) return;
	UE_LOG(LogParamotor, Warning, TEXT("PsSurfLib: %s (%d semantics, %d phys profiles)"),
		Library->GetName().Contains(TEXT("DA_SurfaceLibrary")) ? TEXT("source=/Game/Data/DA_SurfaceLibrary") : TEXT("source=code defaults"),
		Library->Surfaces.Num(), Library->PhysProfiles.Num());
	for (uint8 i = 1; i <= (uint8)ESurfaceSemantic::IceSnow; ++i)
	{
		const ESurfaceSemantic S = (ESurfaceSemantic)i;
		const FSurfaceAssetSet* Set = Library->Find(S);
		const FSurfacePhysProfile* PP = Set ? Library->FindPhys(Set->Phys) : nullptr;
		UE_LOG(LogParamotor, Warning, TEXT("  %-22s phys=%-11s detail=%s pm=%s deform=%.2f"),
			VegSem::ToName(S), Set ? SurfPhysTypeName(Set->Phys) : TEXT("?"),
			Set && !Set->DetailPatchMaterial.IsNull() ? *Set->DetailPatchMaterial.GetAssetName() : TEXT("-"),
			PP && !PP->PhysMat.IsNull() ? *PP->PhysMat.GetAssetName() : TEXT("-"),
			Set ? Set->Deformability01 : 0.0f);
	}
}

void ASurfaceService::DebugSwap(ESurfaceSemantic Sem, const FString& MatPath)
{
	if (!Library) return;
	UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, *MatPath);
	if (!M)
	{
		UE_LOG(LogParamotor, Warning, TEXT("PsSurfSwap: material not found: %s"), *MatPath);
		return;
	}
	// Mutate the live map entry (runtime only — the DA on disk is untouched),
	// keeping the previous binding so DebugReset restores it.
	FSurfaceAssetSet& Set = Library->Surfaces.FindOrAdd(Sem);
	if (!SwappedBackups.Contains((uint8)Sem)) SwappedBackups.Add((uint8)Sem, Set);
	Set.DetailPatchMaterial = M;
	DetailSemantic = ESurfaceSemantic::Unknown; // force rebind
	ResolveTimer = 0.0f;
	UE_LOG(LogParamotor, Warning, TEXT("PsSurfSwap: %s -> %s (classification untouched, patch live)"),
		VegSem::ToName(Sem), *M->GetName());
}

void ASurfaceService::DebugReset()
{
	if (!Library) return;
	for (auto& KV : SwappedBackups)
		if (FSurfaceAssetSet* Set = Library->Surfaces.Find((ESurfaceSemantic)KV.Key))
			*Set = KV.Value;
	SwappedBackups.Empty();
	DetailSemantic = ESurfaceSemantic::Unknown;
	ResolveTimer = 0.0f;
	UE_LOG(LogParamotor, Warning, TEXT("PsSurfReset: library re-resolved"));
}
