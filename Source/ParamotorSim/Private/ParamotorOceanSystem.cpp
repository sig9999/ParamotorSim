// Copyright Epic Games, Inc. All Rights Reserved.

#include "ParamotorOceanSystem.h"
#include "ParamotorLogging.h"
#include "CesiumWorldHelpers.h"
#include "ProceduralMeshComponent.h"
#include "WaterZoneActor.h"
#include "WaterBodyOceanActor.h"
#include "WaterBodyComponent.h"
#include "WaterBodyOceanComponent.h"
#include "WaterMeshComponent.h"
#include "GerstnerWaterWaves.h"
#include "WaterWaves.h"
#include "WaterSplineComponent.h"
#include "Landscape.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Materials/MaterialInterface.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"

AParamotorOceanSystem::AParamotorOceanSystem()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

// Sea-state presets — fetch-limited coastal spectrum, not a linear amplitude
// scale: as the sea builds, the dominant wavelength grows and the steepness
// distribution shifts, which is what a real building sea looks like.
FOceanParams AParamotorOceanSystem::Preset(EOceanState State)
{
	FOceanParams P;
	switch (State)
	{
	case EOceanState::Calm:    // glassy + faint long swell (Hs ~0.05m)
		P.MaxWavelengthCm = 10000; P.MinWavelengthCm = 150;
		P.MaxAmplitudeCm = 3.0f;   P.MinAmplitudeCm = 0.2f;
		P.NumWaves = 12;
		P.LargeWaveSteepness = 0.10f; P.SmallWaveSteepness = 0.15f;
		P.DirectionSpreadDeg = 22.0f;
		break;
	case EOceanState::LightWind:  // cat's paws, small chop (Hs ~0.15m)
		P.MaxWavelengthCm = 11000; P.MinWavelengthCm = 200;
		P.MaxAmplitudeCm = 10.f;   P.MinAmplitudeCm = 0.6f;
		P.NumWaves = 14;
		P.LargeWaveSteepness = 0.15f; P.SmallWaveSteepness = 0.30f;
		P.DirectionSpreadDeg = 26.0f;
		break;
	case EOceanState::Normal:  // Hs ~0.5m — living ocean, occasional whitecaps
		P.MaxWavelengthCm = 14000; P.MinWavelengthCm = 250;
		P.MaxAmplitudeCm = 30.f;   P.MinAmplitudeCm = 1.5f;
		P.NumWaves = 16;
		P.LargeWaveSteepness = 0.22f; P.SmallWaveSteepness = 0.45f;
		P.DirectionSpreadDeg = 30.0f;
		break;
	case EOceanState::Rough:   // Hs ~2m — whitecaps everywhere
		P.MaxWavelengthCm = 30000; P.MinWavelengthCm = 400;
		P.MaxAmplitudeCm = 110.f;  P.MinAmplitudeCm = 6.f;
		P.NumWaves = 18;
		P.LargeWaveSteepness = 0.32f; P.SmallWaveSteepness = 0.55f;
		P.DirectionSpreadDeg = 38.0f;
		break;
	case EOceanState::Storm:   // Hs ~4m, short-fetch coastal storm sea
		P.MaxWavelengthCm = 45000; P.MinWavelengthCm = 500;
		P.MaxAmplitudeCm = 180.f;  P.MinAmplitudeCm = 12.f;
		P.NumWaves = 18;
		P.LargeWaveSteepness = 0.38f; P.SmallWaveSteepness = 0.65f;
		P.DirectionSpreadDeg = 45.0f;
		break;
	}
	return P;
}

void AParamotorOceanSystem::Setup()
{
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// Flat maps use the Landscape + WaterZone path. The Cesium globe has
	// neither — it gets a geoid-curved water disc at true MSL instead.
	TArray<AActor*> Existing;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), ALandscapeProxy::StaticClass(), Existing);
	if (Existing.Num() == 0)
	{
		if (CesiumWorld::DetectGlobe(GetWorld())) { SetupGlobe(); return; }
		return;
	}

	// WaterZone — where water info (depth/waves/foam) gets rendered.
	// Cover well beyond the map so the ocean reaches the horizon.
	Zone = GetWorld()->SpawnActor<AWaterZone>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
	if (Zone)
	{
		Zone->SetZoneExtent(FVector2D(4000000.0, 3000000.0));  // 40km x 30km
		// No local-tessellation sliding window: with it on, the mesh exists
		// only in a ~1km window around the camera and the far mesh beyond is
		// a normal-less mirror — the seam reads as a hard wedge from the air.
		// Letting the quadtree LOD the whole 40km zone gives one material path
		// (waves + normals) everywhere; far mesh only covers past the zone.
		if (FBoolProperty* LT = FindFProperty<FBoolProperty>(AWaterZone::StaticClass(), TEXT("bEnableLocalOnlyTessellation")))
			LT->SetPropertyValue_InContainer(Zone, false);
		// Depth/attenuation texture covers the whole zone — max out its
		// resolution (40km / 4096px = ~10m per texel).
		Zone->SetRenderTargetResolution(FIntPoint(4096, 4096));

		// Far mesh: beyond the 1km tessellated window the quadtree hands off
		// to a cheap far-distance surface so water still reaches the horizon.
		// Use our ocean instance (not stock Water_FarMesh) so distant water
		// keeps wave normals — a normal-less far field mirrors the sky as a
		// single blown-out sheet.
		if (UWaterMeshComponent* Mesh = Zone->GetWaterMeshComponent())
		{
			UMaterialInterface* FarMat = LoadObject<UMaterialInterface>(nullptr,
				TEXT("/Game/Materials/MI_ParamotorOcean.MI_ParamotorOcean"));
			if (!FarMat)
				FarMat = LoadObject<UMaterialInterface>(nullptr,
					TEXT("/Water/Materials/WaterSurface/Water_FarMesh.Water_FarMesh"));
			Zone->SetFarMeshMaterial(FarMat);
			Mesh->FarDistanceMeshExtent = 40000000.0f;  // 400km far field
			// Perf: a 40km zone at 24m tiles is heavy — collapse far tiles
			// aggressively and trim per-tile density. Private members →
			// reflection, same as the zone flags above.
			Mesh->ForceCollapseDensityLevel = 0;
			if (FIntProperty* TF = FindFProperty<FIntProperty>(UWaterMeshComponent::StaticClass(), TEXT("TessellationFactor")))
				TF->SetPropertyValue_InContainer(Mesh, 5);
			if (FFloatProperty* LS = FindFProperty<FFloatProperty>(UWaterMeshComponent::StaticClass(), TEXT("LODScale")))
				LS->SetPropertyValue_InContainer(Mesh, 2.0f);
			Mesh->MarkWaterMeshGridDirty();
		}
		Zone->MarkForRebuild(EWaterZoneRebuildFlags::All);
		for (UActorComponent* C : Zone->GetComponents())
			if (UPrimitiveComponent* P = Cast<UPrimitiveComponent>(C))
			{
				P->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
				P->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
				P->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
			}
	}

	Ocean = GetWorld()->SpawnActor<AWaterBodyOcean>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
	if (Ocean)
	{
		for (UActorComponent* C : Ocean->GetComponents())
			if (UPrimitiveComponent* P = Cast<UPrimitiveComponent>(C))
			{
				P->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
				P->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
				P->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
			}

		// WaterBodyOcean is island-centric: it renders water OUTSIDE a spline
		// island. The default island sits at the origin and eats the coastline —
		// move it 10km north, inside real land, so water covers the whole map.
		if (UWaterBodyComponent* WC = Ocean->GetWaterBodyComponent())
		{
			if (UWaterSplineComponent* Spline = WC->GetWaterSpline())
			{
				const float IslandY = 1000000.0f;  // 10km north = inland
				const float R = 2000.0f;           // 20m square island
				TArray<FVector> Pts;
				Pts.Add(FVector(-R, IslandY - R, 0.f));
				Pts.Add(FVector( R, IslandY - R, 0.f));
				Pts.Add(FVector( R, IslandY + R, 0.f));
				Pts.Add(FVector(-R, IslandY + R, 0.f));
				Spline->ClearSplinePoints(false);
				for (const FVector& P : Pts)
					Spline->AddSplinePoint(P, ESplineCoordinateSpace::Local, false);
				Spline->SetClosedLoop(true, false);
				Spline->UpdateSpline();
			}
		}

		// The ocean mesh covers the zone; push its visual extent well past it.
		if (UWaterBodyOceanComponent* OC = Cast<UWaterBodyOceanComponent>(Ocean->GetWaterBodyComponent()))
		{
			// CRITICAL: runtime-spawned bodies never discover a zone on their
			// own (UpdateWaterZones only runs on editor changes/cook fixups).
			// Without this the zone owns no body, the WaterInfo texture is
			// never rendered, and no water mesh is ever generated.
			if (Zone)
				OC->SetWaterZoneOverride(Zone);
			// Regenerates the render/info meshes from the relocated island spline.
#if WITH_EDITOR
			OC->SetOceanExtent(FVector2D(40000000.0, 40000000.0));  // 400km visual disc
#endif
			// Our tuned instance (foam + ocean transition + caustics on,
			// coastal colors); fall back to Epic's ocean material.
			UMaterialInterface* OM = LoadObject<UMaterialInterface>(nullptr,
				TEXT("/Game/Materials/MI_ParamotorOcean.MI_ParamotorOcean"));
			if (!OM)
				OM = LoadObject<UMaterialInterface>(nullptr,
					TEXT("/Water/Materials/WaterSurface/Water_Material_Ocean.Water_Material_Ocean"));
			if (OM)
				OC->SetWaterMaterial(OM);
		}

		Waves = Cast<UGerstnerWaterWaves>(Ocean->GetWaterWaves());
		if (!Waves)
		{
			Waves = NewObject<UGerstnerWaterWaves>(Ocean);
			Ocean->SetWaterWaves(Waves);
		}
		Gen = NewObject<UGerstnerWaterWaveGeneratorSimple>(Waves);
		Waves->GerstnerWaveGenerator = Gen;

		// Default state, applied immediately.
		Target = Preset(EOceanState::Normal);
		Current = Target;
		ApplyToGenerator(Current);
		Waves->RecomputeWaves(false);

		UE_LOG(LogParamotor, Log, TEXT("Ocean: system up — zone 40x30km, Gerstner spectrum live"));
		return;
	}

	UE_LOG(LogParamotor, Log, TEXT("Ocean: WaterBodyOcean failed — flat fallback plane"));
	UStaticMesh* PlaneMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (!PlaneMesh) return;
	FallbackPlane = GetWorld()->SpawnActor<AStaticMeshActor>(FVector(0, 0, -15.f), FRotator::ZeroRotator, SpawnParams);
	if (FallbackPlane)
	{
		FallbackPlane->GetStaticMeshComponent()->SetStaticMesh(PlaneMesh);
		FallbackPlane->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		FallbackPlane->SetActorScale3D(FVector(8000.f, 8000.f, 1.f));
		FallbackPlane->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
}

// ---- globe path ---------------------------------------------------------
// The sea-level sheet is a radial mesh whose vertices are placed by
// GeodeticToUnreal at EGM96 geoid height — i.e. it follows mean sea level
// including Earth curvature (a flat plane deviates ~d²/2R: 49 m at 25 km).
// Measured at Narragansett Bay: PRT bakes its water skin ~4 m below MSL,
// so this sheet floats just above it with no z-fighting.
void AParamotorOceanSystem::SetupGlobe()
{
	bGlobeMode = true;

	// CPU Gerstner field still runs — GetWaterHeightAt (sand wetness, float
	// contact) needs wave height on top of the MSL sheet.
	Waves = NewObject<UGerstnerWaterWaves>(this);
	Gen = NewObject<UGerstnerWaterWaveGeneratorSimple>(Waves);
	Waves->GerstnerWaveGenerator = Gen;
	Target = Preset(EOceanState::Normal);
	Current = Target;
	ApplyToGenerator(Current);
	Waves->RecomputeWaves(false);

	GlobeDisc = NewObject<UProceduralMeshComponent>(this, TEXT("GlobeWaterDisc"));
	GlobeDisc->SetupAttachment(RootComponent);
	// Collision so the settle loop lands ON the water instead of
	// tunneling to the PRT seabed below it.
	GlobeDisc->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	GlobeDisc->RegisterComponent();

	UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Game/Materials/M_GlobeWater.M_GlobeWater"));
	if (!M)
		M = LoadObject<UMaterialInterface>(nullptr,
			TEXT("/Game/Materials/MI_ParamotorOcean.MI_ParamotorOcean"));
	if (M) GlobeDisc->SetMaterial(0, M);

	FVector OLLH;
	if (CesiumWorld::GetGeorefOrigin(GetWorld(), OLLH))
		RebuildGlobeDisc(OLLH.Y, OLLH.X);
	UE_LOG(LogParamotor, Log, TEXT("Ocean: globe mode — geoid disc spawned"));
}

void AParamotorOceanSystem::RebuildGlobeDisc(double CenLat, double CenLon)
{
	if (!GlobeDisc) return;
	DiscCenterLat = CenLat;
	DiscCenterLon = CenLon;

	constexpr double RADIUS_M = 45000.0;   // 45 km — covers RI + approaches
	constexpr int32 RINGS = 24, SEGS = 48;

	const FVector CenterW = CesiumWorld::GeodeticToUnreal(GetWorld(), CenLon, CenLat,
		CesiumWorld::GeoidUndulationM(CenLat, CenLon));
	DiscWaterZ = CenterW.Z;
	GlobeDisc->SetWorldLocation(CenterW);

	TArray<FVector> Verts, Normals;
	TArray<FVector2D> UVs;
	TArray<int32> Tris;
	TArray<FProcMeshTangent> Tangents;
	Verts.Add(FVector::ZeroVector);
	Normals.Add(FVector::UpVector);
	UVs.Add(FVector2D(0.5f, 0.5f));
	for (int32 r = 1; r <= RINGS; ++r)
	{
		const double d = RADIUS_M * r / RINGS;
		for (int32 s = 0; s < SEGS; ++s)
		{
			const double a = 2.0 * PI * s / SEGS;
			const double Lat = CenLat + (d * FMath::Sin(a)) / 110540.0;
			const double Lon = CenLon + (d * FMath::Cos(a))
				/ (111320.0 * FMath::Cos(FMath::DegreesToRadians(CenLat)));
			const FVector W = CesiumWorld::GeodeticToUnreal(GetWorld(), Lon, Lat,
				CesiumWorld::GeoidUndulationM(Lat, Lon));
			Verts.Add(W - CenterW);
			Normals.Add(FVector::UpVector);
			UVs.Add(FVector2D(0.5 + 0.5 * FMath::Cos(a), 0.5 + 0.5 * FMath::Sin(a)));
		}
	}
	for (int32 s = 0; s < SEGS; ++s)
		Tris.Append({0, 1 + s, 1 + (s + 1) % SEGS});
	for (int32 r = 1; r < RINGS; ++r)
		for (int32 s = 0; s < SEGS; ++s)
		{
			const int32 a0 = 1 + (r - 1) * SEGS + s, a1 = 1 + (r - 1) * SEGS + (s + 1) % SEGS;
			const int32 b0 = 1 + r * SEGS + s,       b1 = 1 + r * SEGS + (s + 1) % SEGS;
			Tris.Append({a0, b1, b0, a0, a1, b1});
		}
	GlobeDisc->CreateMeshSection(0, Verts, Tris, Normals, UVs, {}, Tangents, true);
}

void AParamotorOceanSystem::SetSeaState(EOceanState State)
{
	CurrentState = State;
	Target = Preset(State);
	bDirty = true;
}

void AParamotorOceanSystem::ForceSeaState(int32 State)
{
	if (State < 0 || State > (int32)EOceanState::Storm)
	{
		bManualOverride = false;
		return;
	}
	bManualOverride = true;
	SetSeaState((EOceanState)State);
	UE_LOG(LogParamotor, Log, TEXT("Ocean: forced state %d"), State);
}

void AParamotorOceanSystem::UpdateFromWeather(float WindSpeedMS, float WindDirDeg, float GustMS)
{
	if (bManualOverride) return;
	if (!bContinuousWeather)
	{
		// Map wind bands to the nearest named state.
		EOceanState S =
			WindSpeedMS < 2.5f ? EOceanState::Calm :
			WindSpeedMS < 5.5f ? EOceanState::LightWind :
			WindSpeedMS < 10.f ? EOceanState::Normal :
			WindSpeedMS < 17.f ? EOceanState::Rough : EOceanState::Storm;
		if (S != CurrentState) SetSeaState(S);
		return;
	}

	// Continuous: fetch-limited coastal growth from the 10m wind.
	//   Hs = 0.016 * U10^2 (m, saturated ~4m), T = 0.55*U10 + 1.2 (s),
	//   L  = 1.56 * T^2 (m deep-water dispersion).
	const float U10 = FMath::Max(0.5f, WindSpeedMS);
	const float Hs  = FMath::Clamp(0.016f * U10 * U10, 0.05f, 4.5f);
	const float T   = FMath::Clamp(0.55f * U10 + 1.2f, 2.5f, 9.5f);
	const float L   = 1.56f * T * T;
	CurrentHsMeters = Hs;

	// Only rebuild the spectrum when the weather actually moved it —
	// otherwise this re-dirties every weather tick and never settles.
	const float OldAmp = Target.MaxAmplitudeCm;
	const float OldLen = Target.MaxWavelengthCm;
	const float OldAng = Target.WindAngleDeg;

	FOceanParams& P = Target;
	P.MaxWavelengthCm = L * 100.0f;
	P.MinWavelengthCm = FMath::Max(120.0f, L * 9.0f);   // chop floor ~1/10th
	P.NumWaves = 16;
	P.MaxAmplitudeCm = Hs * 50.0f;                      // amp = Hs/2
	P.MinAmplitudeCm = Hs * 4.0f;
	P.AmplitudeFalloff = 2.0f;
	P.WavelengthFalloff = 2.0f;
	P.SmallWaveSteepness = FMath::Clamp(0.2f + 0.04f * U10, 0.2f, 0.6f);
	P.LargeWaveSteepness = FMath::Clamp(0.05f + 0.025f * U10, 0.1f, 0.4f);
	P.SteepnessFalloff = 1.0f;
	// Waves travel WITH the wind: METAR is FROM — convert to UE yaw.
	P.WindAngleDeg = 90.0f - FMath::Fmod(WindDirDeg + 180.0f, 360.0f);
	P.DirectionSpreadDeg = 26.0f + 0.8f * U10;
	if (FMath::Abs(P.MaxAmplitudeCm - OldAmp) > 0.5f
		|| FMath::Abs(P.MaxWavelengthCm - OldLen) > 50.0f
		|| FMath::Abs(FMath::FindDeltaAngleDegrees(P.WindAngleDeg, OldAng)) > 5.0f)
		bDirty = true;
}

void AParamotorOceanSystem::ApplyToGenerator(const FOceanParams& P)
{
	if (!Gen) return;
	Gen->NumWaves = P.NumWaves;
	Gen->MaxWavelength = P.MaxWavelengthCm;
	Gen->MinWavelength = P.MinWavelengthCm;
	Gen->MaxAmplitude = P.MaxAmplitudeCm;
	Gen->MinAmplitude = P.MinAmplitudeCm;
	Gen->AmplitudeFalloff = P.AmplitudeFalloff;
	Gen->WavelengthFalloff = P.WavelengthFalloff;
	Gen->LargeWaveSteepness = P.LargeWaveSteepness;
	Gen->SmallWaveSteepness = P.SmallWaveSteepness;
	Gen->SteepnessFalloff = P.SteepnessFalloff;
	Gen->WindAngleDeg = P.WindAngleDeg;
	Gen->DirectionAngularSpreadDeg = P.DirectionSpreadDeg;
}

void AParamotorOceanSystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Globe mode: recenter the water disc when the pawn travels >12 km.
	if (bGlobeMode && GlobeDisc)
	{
		APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
		APawn* P = PC ? PC->GetPawn() : nullptr;
		if (P)
		{
			const FVector PLLH = CesiumWorld::UnrealToGeodetic(GetWorld(), P->GetActorLocation());
			const double dN = (PLLH.Y - DiscCenterLat) * 110540.0;
			const double dE = (PLLH.X - DiscCenterLon) * 111320.0
				* FMath::Cos(FMath::DegreesToRadians(PLLH.Y));
			if (dN * dN + dE * dE > 12000.0 * 12000.0)
				RebuildGlobeDisc(PLLH.Y, PLLH.X);
		}
	}

	if (!Gen || !Waves) return;

#if !UE_BUILD_SHIPPING
	// Debug: sample the CPU wave field — if this never varies, the
	// generator produced nothing; if it varies but the surface looks flat,
	// the break is in the GPU/material path.
	DebugSampleTimer -= DeltaTime;
	if (DebugSampleTimer <= 0.0f)
	{
		DebugSampleTimer = 3.0f;
		const float T = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
		const float H = Waves->GetSimpleWaveHeightAtPosition(FVector(0, 0, 0), 10000.0f, T);
		UE_LOG(LogParamotor, Log, TEXT("Ocean dbg: waves=%d maxH=%.1fcm sample=%.1fcm"),
			Gen->NumWaves, Waves->GetMaxWaveHeight(), H);
	}
#endif

	RecomputeTimer -= DeltaTime;
	if (RecomputeTimer <= 0.0f)
	{
		RecomputeTimer = 1.5f;
		if (bDirty)
		{
			// Ease toward the target spectrum — the sea builds/falls over
			// seconds, it doesn't snap.
			const float A = FMath::Min(1.0f, DeltaTime * 0.5f + 0.12f);
			Current.MaxWavelengthCm = FMath::Lerp(Current.MaxWavelengthCm, Target.MaxWavelengthCm, A);
			Current.MinWavelengthCm = FMath::Lerp(Current.MinWavelengthCm, Target.MinWavelengthCm, A);
			Current.MaxAmplitudeCm  = FMath::Lerp(Current.MaxAmplitudeCm,  Target.MaxAmplitudeCm,  A);
			Current.MinAmplitudeCm  = FMath::Lerp(Current.MinAmplitudeCm,  Target.MinAmplitudeCm,  A);
			Current.LargeWaveSteepness = FMath::Lerp(Current.LargeWaveSteepness, Target.LargeWaveSteepness, A);
			Current.SmallWaveSteepness = FMath::Lerp(Current.SmallWaveSteepness, Target.SmallWaveSteepness, A);
			Current.DirectionSpreadDeg = FMath::Lerp(Current.DirectionSpreadDeg, Target.DirectionSpreadDeg, A);
			Current.WindAngleDeg += FMath::FindDeltaAngleDegrees(Current.WindAngleDeg, Target.WindAngleDeg) * A;
			ApplyToGenerator(Current);
			Waves->RecomputeWaves(false);

			// Converged? stop recomputing — and only log on real changes.
			const float Remaining = FMath::Abs(Target.MaxAmplitudeCm - Current.MaxAmplitudeCm)
				+ FMath::Abs(Target.MaxWavelengthCm - Current.MaxWavelengthCm)
				+ FMath::Abs(Target.MinAmplitudeCm - Current.MinAmplitudeCm);
			if (Remaining < 0.5f)
			{
				bDirty = false;
				UE_LOG(LogParamotor, Log, TEXT("Ocean: sea settled -> Hs %.2fm, L %.0fm, dir %.0f"),
					CurrentHsMeters, Current.MaxWavelengthCm / 100.0f, Current.WindAngleDeg);
			}
		}
	}
}

float AParamotorOceanSystem::GetWaterHeightAt(const FVector& WorldPos) const
{
	// Base sheet height: flat maps sit at Z≈0; the globe disc follows the
	// geoid — sample the exact geodetic surface height under the query point
	// (the old d²/2R parabola was 100x too weak and wrong far from center).
	float Base = 0.0f;
	if (bGlobeMode && GlobeDisc)
	{
		const FVector G = CesiumWorld::UnrealToGeodetic(GetWorld(), WorldPos);
		Base = (float)CesiumWorld::GeodeticToUnreal(GetWorld(), G.X, G.Y,
			CesiumWorld::GeoidUndulationM(G.Y, G.X)).Z;
	}
	if (!Waves) return Base;
	const float Time = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	// Simple height eval — depth attenuation applied for shore realism.
	return Base + Waves->GetSimpleWaveHeightAtPosition(WorldPos, 10000.0f, Time);
}
