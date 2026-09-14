// Copyright Epic Games, Inc. All Rights Reserved.

#include "VegetationField.h"
#include "LandCoverService.h"
#include "CesiumWorldHelpers.h"
#include "ParamotorLogging.h"
#include "ParamotorGameInstance.h"
#include "ParamotorGameMode.h"
#include "ParamotorPlayerController.h"
#include "ParamotorActor.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Components/SceneComponent.h"
#include "DrawDebugHelpers.h"

AVegetationField::AVegetationField()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.2f;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

// ---- lifecycle ------------------------------------------------------

void AVegetationField::BeginPlay()
{
	Super::BeginPlay();

	TArray<AActor*> Found;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), ALandCoverService::StaticClass(), Found);
	LandCover = Found.Num() ? Cast<ALandCoverService>(Found[0]) : nullptr;
	if (!LandCover)
	{
		LandCover = GetWorld()->SpawnActor<ALandCoverService>();
		UE_LOG(LogParamotor, Log, TEXT("VegetationField: spawned LandCoverService"));
	}

	Library = UVegetationLibrary::Get();
	LoadRunways();
	if (UMaterialParameterCollection* MPC = LoadVegMPC())
	{
		VegMPCAsset = MPC;
		VegMPC = GetWorld()->GetParameterCollectionInstance(MPC);
	}
}

void AVegetationField::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UpdatePawnState();

	// While the GameMode is settling the pawn onto fresh terrain, streamed
	// tiles are still at coarse LOD — their collision surface can be a phantom
	// plate kilometers above real ground. Freeze the field until the settle
	// finishes so instances never land on transient geometry.
	if (AParamotorGameMode* GM = Cast<AParamotorGameMode>(GetWorld()->GetAuthGameMode()))
		if (GM->IsAwaitingGlobeGround()) { UpdateWindParams(); return; }

	if (bHavePawnLLH)
	{
		// Georeference rebase → every world-space cell is stale; rebuild.
		FVector Origin;
		if (CesiumWorld::GetGeorefOrigin(GetWorld(), Origin) && !Origin.Equals(LastGeorefOrigin, 1.0))
		{
			LastGeorefOrigin = Origin;
			DespawnAll();
		}
		UpdateCells();
		if (DebugViz > 0) DrawDebugCells();
	}
	UpdateWindParams();
}

// ---- pawn / geodesy ---------------------------------------------------

bool AVegetationField::PawnLLH(FVector& OutLLH)
{
	APawn* P = GetWorld() ? GetWorld()->GetFirstPlayerController() ? GetWorld()->GetFirstPlayerController()->GetPawn() : nullptr : nullptr;
	if (!P) return false;
	OutLLH = CesiumWorld::UnrealToGeodetic(GetWorld(), P->GetActorLocation());
	return !OutLLH.IsNearlyZero();
}

void AVegetationField::UpdatePawnState()
{
	APawn* P = nullptr;
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController()) P = PC->GetPawn();
	if (!P) { bHavePawnLLH = false; return; }

	FVector LLH;
	if (!PawnLLH(LLH)) { bHavePawnLLH = false; return; }
	PawnLon = LLH.X; PawnLat = LLH.Y; PawnH = LLH.Z;
	LastPawnPos = P->GetActorLocation();
	bHavePawnLLH = true;

	// AGL — one downward trace per tick.
	FHitResult Hit;
	FCollisionQueryParams P2; P2.AddIgnoredActor(P);
	const FVector S = LastPawnPos + FVector(0, 0, 2000);
	const FVector E = LastPawnPos - FVector(0, 0, 500000);
	PawnAGL = GetWorld()->LineTraceSingleByChannel(Hit, S, E, ECC_Visibility, P2)
		? (LastPawnPos.Z - Hit.ImpactPoint.Z) / 100.0f : 5000.0f;
}

// Geodetic → world near the pawn via the pawn's local east/south/up frame.
// Cesium's local frame is East-South-Up: UE +X = east, +Y = south.
FVector AVegetationField::LLToUnrealNearPawn(double Lat, double Lon, double HeightM) const
{
	const double dE = (Lon - PawnLon) * 111320.0 * FMath::Cos(FMath::DegreesToRadians(PawnLat)) * 100.0;
	const double dN = (Lat - PawnLat) * 110540.0 * 100.0;
	const double dH = (HeightM - PawnH) * 100.0;
	return LastPawnPos + FVector(dE, -dN, dH);
}

void AVegetationField::UnrealToLLNearPawn(const FVector& Pos, double& OutLat, double& OutLon) const
{
	const FVector D = Pos - LastPawnPos;
	OutLon = PawnLon + (D.X / 100.0) / (111320.0 * FMath::Cos(FMath::DegreesToRadians(PawnLat)));
	OutLat = PawnLat + (-D.Y / 100.0) / 110540.0;
}

// ---- cell streaming -----------------------------------------------------

void AVegetationField::UpdateCells()
{
	if (!LandCover) return;

	// Altitude scaling: grass fades out above ~350 m AGL, tree ring shrinks.
	const float AltF = FMath::Clamp(1.0f - (PawnAGL - 350.0f) / 550.0f, 0.0f, 1.0f);      // 1 → 0 by 900 m
	const float GrassF = FMath::Clamp(1.0f - (PawnAGL - 150.0f) / 200.0f, 0.0f, 1.0f);     // 0 above ~350 m

	// Rhode Island regional fidelity mask — smooth geodetic falloff, no hard
	// border. Covers the mainland + Block Island at full strength and feathers
	// ~35 km past the state line so CT/MA transitions stay invisible.
	{
		const double dLatM = FMath::Max(FMath::Max(41.14 - PawnLat, PawnLat - 42.02), 0.0) * 110540.0;
		const double dLonM = FMath::Max(FMath::Max(-71.92 - PawnLon, PawnLon + 71.11), 0.0)
			* 111320.0 * FMath::Cos(FMath::DegreesToRadians(41.58));
		const double dM = FMath::Sqrt(dLatM * dLatM + dLonM * dLonM);
		DetailBoost = FMath::Clamp(1.0f - (float)((dM - 8000.0) / 35000.0), 0.0f, 1.0f);
	}

	const double TreeR = FMath::Lerp(350.0, 650.0, FMath::Max(AltF, 0.45f));               // m
	const double GrassR = FMath::Lerp(230.0, 310.0, DetailBoost);

	// ---- wanted sets ----------------------------------------------------
	TSet<FCellKey> WantTrees, WantGrass, KeepTrees, KeepGrass;
	auto WantDisc = [&](TSet<FCellKey>& Out, double CellD, double RadiusM)
	{
		const double RLat = RadiusM / 110540.0;
		const double RLon = RadiusM / (111320.0 * FMath::Cos(FMath::DegreesToRadians(PawnLat)));
		const int32 X0 = FMath::FloorToInt((PawnLon - RLon) / CellD), X1 = FMath::FloorToInt((PawnLon + RLon) / CellD);
		const int32 Y0 = FMath::FloorToInt((PawnLat - RLat) / CellD), Y1 = FMath::FloorToInt((PawnLat + RLat) / CellD);
		for (int32 Y = Y0; Y <= Y1; ++Y)
			for (int32 X = X0; X <= X1; ++X)
			{
				const double CenLat = (Y + 0.5) * CellD, CenLon = (X + 0.5) * CellD;
				const FVector C = LLToUnrealNearPawn(CenLat, CenLon, 0);
				FVector D = C - LastPawnPos; D.Z = 0;
				if (D.Size() < RadiusM * 100.0) Out.Add(FCellKey{ X, Y });
			}
	};
	WantDisc(WantTrees, CellDeg, TreeR);
	WantDisc(KeepTrees, CellDeg, TreeR * 1.18);              // hysteresis
	if (GrassF > 0.0f) { WantDisc(WantGrass, GrassCellDeg, GrassR); WantDisc(KeepGrass, GrassCellDeg, GrassR * 1.18); }

	// ---- despawn ----------------------------------------------------------
	auto Cull = [&](TMap<FCellKey, FCell>& Map, TSet<FCellKey>& Keep)
	{
		for (auto It = Map.CreateIterator(); It; ++It)
			if (!Keep.Contains(It->Key)) { DespawnCell(Map, It->Key); It.RemoveCurrent(); }
	};
	Cull(Cells, KeepTrees);
	Cull(GrassCells, KeepGrass);

	// ---- revalidate --------------------------------------------------------
	// Cells built on transient coarse-LOD shells go stale when the shell
	// unloads (floating veg). Re-probe a few cell centers per pass; a cell
	// whose ground vanished or moved >15 m is dropped and rebuilt next pass.
	{
		const float Now = GetWorld()->GetTimeSeconds();
		int32 Probes = 4;
		auto Reval = [&](TMap<FCellKey, FCell>& Map, double CellD)
		{
			// Probe the STALEST cells first — TMap iteration order would
			// otherwise starve late-iterated cells forever.
			TArray<FCellKey> Due;
			for (auto& KV : Map)
				if (Now - KV.Value.LastCheckT >= 4.0f) Due.Add(KV.Key);
			Due.Sort([&](const FCellKey& A, const FCellKey& B)
				{ return Map[A].LastCheckT < Map[B].LastCheckT; });
			TArray<FCellKey> Stale;
			for (const FCellKey& K : Due)
			{
				if (Probes <= 0) break;
				FCell& C = Map[K];
				C.LastCheckT = Now;
				// Center probe plus a rotating interior offset — a single
				// floater inside an otherwise-clean cell must still kill it.
				const double CenLat = (K.Y + 0.5) * CellD, CenLon = (K.X + 0.5) * CellD;
				const double Rot = (double)(FMath::FloorToInt64(Now / 4.0f) % 4) * 0.15 - 0.22;
				const double Lats[2] = { CenLat, CenLat + Rot * CellD };
				const double Lons[2] = { CenLon, CenLon + Rot * CellD };
				bool bStale = false;
				for (int32 q = 0; q < 2 && !bStale; ++q)
				{
					const FVector CC = LLToUnrealNearPawn(Lats[q], Lons[q], 0);
					FVector P0, N0;
					if (!GroundAt(CC, P0, N0) || FMath::Abs(P0.Z - C.GroundZ) > 1500.0f)
						bStale = true;
				}
				// Instance spot-check: a local shell lump can hold a few
				// instances airborne while every geo probe stays clean. Sweep a
				// rotating 4-instance window per ISM per check — over a few
				// passes the whole cell is covered; if a baked instance Z is
				// >3 m off live ground, rebuild the cell.
				if (!bStale)
				{
					for (UInstancedStaticMeshComponent* ISM : C.Comps)
					{
						if (bStale || !ISM) continue;
						const int32 InstN = ISM->GetNumInstances();
						const int32 Base = InstN > 0 ? FMath::FloorToInt(Now) % InstN : 0;
						for (int32 k = 0; k < 4 && !bStale; ++k)
						{
							const int32 Idx = InstN > 0 ? (Base + k * InstN / 4) % InstN : 0;
							if (Idx < 0 || Idx >= InstN) continue;
							FTransform T;
							if (!ISM->GetInstanceTransform(Idx, T, true)) continue;
							FVector P2, N2;
							if (!GroundAt(T.GetLocation(), P2, N2) ||
								FMath::Abs(P2.Z - T.GetLocation().Z) > 300.0f)
								bStale = true;
						}
					}
				}
				if (bStale) Stale.Add(K);
				Probes--;
			}
			for (const FCellKey& K : Stale) { DespawnCell(Map, K); Map.Remove(K); }
		};
		Reval(GrassCells, GrassCellDeg);
		Reval(Cells, CellDeg);
	}

	// ---- spawn (budgeted, nearest first) ----------------------------------
	SpawnBudget = FMath::Min(SpawnBudget + 3.0f + 3.0f * DetailBoost,
		6.0f + 4.0f * DetailBoost);
	auto SpawnPass = [&](TMap<FCellKey, FCell>& Map, TSet<FCellKey>& Want, bool bGrass)
	{
		TArray<FCellKey> Sorted = Want.Array();
		Sorted.Sort([&](const FCellKey& A, const FCellKey& B)
		{
			const double CellD = bGrass ? GrassCellDeg : CellDeg;
			auto D2 = [&](const FCellKey& K)
			{
				const FVector C = LLToUnrealNearPawn((K.Y + 0.5) * CellD, (K.X + 0.5) * CellD, 0);
				FVector D = C - LastPawnPos; D.Z = 0; return D.SizeSquared();
			};
			return D2(A) < D2(B);
		});
		for (const FCellKey& K : Sorted)
		{
			if (SpawnBudget <= 0.0f) break;
			if (Map.Contains(K)) continue;
			const double CellD = bGrass ? GrassCellDeg : CellDeg;
			const double CenLat = (K.Y + 0.5) * CellD, CenLon = (K.X + 0.5) * CellD;
			const double CellM = CellD * 110540.0;
			if (LandCover->IsAreaPending(CenLat, CenLon, CellM * 0.75)) continue;

			// one cheap probe trace — if streamed collision isn't under this
			// cell yet, skip it entirely so it retries next tick
			const FVector CC = LLToUnrealNearPawn(CenLat, CenLon, 0);
			FVector P0, N0;
			if (!GroundAt(CC, P0, N0)) { RejNoGround++; continue; }

			FCell& Cell = Map.Add(K);
			Cell.GroundZ = P0.Z;
			Cell.LastCheckT = GetWorld()->GetTimeSeconds();
			PopulateCell(K, bGrass);
			SpawnBudget -= 1.0f;
		}
	};
	SpawnPass(GrassCells, WantGrass, true);
	SpawnPass(Cells, WantTrees, false);

	NumLiveCells = Cells.Num() + GrassCells.Num();

	StatsTimer += 1.0f;
	if (StatsTimer >= 50.0f) // ~10 s at 0.2 s tick
	{
		StatsTimer = 0.0f;
		UE_LOG(LogParamotor, Log, TEXT("VegStats: cells=%d trees=%d grass=%d rejExcl=%d rejNoGround=%d AGL=%.0fm pawn=%.5f,%.5f"),
			NumLiveCells, NumTreeInst, NumGrassInst, RejExcluded, RejNoGround, PawnAGL, PawnLat, PawnLon);
	}
}

void AVegetationField::PopulateCell(const FCellKey& Key, bool bGrass)
{
	TMap<FCellKey, FCell>& Map = bGrass ? GrassCells : Cells;
	FCell* Cell = Map.Find(Key);
	if (!Cell) return;
	const double CellD = bGrass ? GrassCellDeg : CellDeg;
	const double CenLat = (Key.Y + 0.5) * CellD, CenLon = (Key.X + 0.5) * CellD;
	if (bGrass)
	{
		// a missing center pixel means the tile landed between the gate check
		// and now — drop the cell so it retries next tick
		if (!SpawnGrassCellContent(Key, *Cell, CenLat, CenLon)) Map.Remove(Key);
	}
	else
	{
		SpawnTreeCellContent(Key, *Cell, CenLat, CenLon);
	}
}

void AVegetationField::DespawnCell(TMap<FCellKey, FCell>& Map, const FCellKey& Key)
{
	if (FCell* C = Map.Find(Key))
	{
		for (UInstancedStaticMeshComponent* ISM : C->Comps)
			if (ISM) ISM->DestroyComponent();
		NumInstances -= C->Inst;
		if (&Map == &GrassCells) NumGrassInst -= C->Inst;
		else NumTreeInst -= C->Inst;
	}
}

// ---- exclusion ----------------------------------------------------------

bool AVegetationField::AllowedAt(double Lat, double Lon, bool bTree, const FLandCoverPixel& Px, float SlopeDeg)
{
	// hard exclusions — water/ice/barren/high-intensity development
	if (Px.Cover == 11 || Px.Cover == 12 || Px.Cover == 24 || Px.Cover == 31) return false;
	if (Px.Impervious > (bTree ? 45 : 70)) return false;
	if (bTree && Px.Road >= 1 && Px.Road <= 4) return false;
	if (SlopeDeg > (bTree ? 45.0f : 55.0f)) return false;

	if (bTree)
	{
		const float RW = RunwayDistM(Lat, Lon);
		if (RW >= 0.0f && RW < 45.0f) return false;
	}
	return true;
}

// ---- tree cells ---------------------------------------------------------

void AVegetationField::SpawnTreeCellContent(const FCellKey& Key, FCell& Cell, double CenLat, double CenLon)
{
	FLandCoverPixel Center;
	if (!LandCover->GetPixelAt(CenLat, CenLon, Center)) return;
	const float ForestFrac = LandCover->GetForestFractionAt(CenLat, CenLon);
	const float Canopy = Center.Canopy / 100.0f;

	// Base density by class (fraction of a "full stand" ~1 tree / 14 m grid).
	float TreeDensity = 0.0f, ShrubDensity = 0.0f;
	const uint8 C = Center.Cover;
	if (C == 41 || C == 42 || C == 43 || C == 90)
	{
		const float FF = ForestFrac >= 0.0f ? ForestFrac : 1.0f;
		TreeDensity = FMath::Clamp(Canopy * 1.4f, 0.15f, 1.0f) * FF;
		ShrubDensity = 0.5f;
	}
	else if (C == 52) { ShrubDensity = 1.0f; TreeDensity = Canopy * 0.35f; }
	else if (C == 71 || C == 81) { TreeDensity = Canopy * 0.12f; }
	else if (C == 82) { TreeDensity = Canopy * 0.08f; }
	else if (C == 21) { TreeDensity = Canopy * 0.45f; ShrubDensity = 0.15f; }
	else if (C == 22 || C == 23) { TreeDensity = Canopy * 0.25f; }
	else if (C == 95) { ShrubDensity = 0.4f; }
	else if (C == 0)  { TreeDensity = 0.0f; } // offline/no-data — grass only
	else return;

	// distance LOD — distant cells carry a sparser "canopy sample"
	const FVector CC = LLToUnrealNearPawn(CenLat, CenLon, 0);
	FVector CD = CC - LastPawnPos; CD.Z = 0;
	const float DistM = CD.Size() / 100.0f;
	const float LodFloor = FMath::Lerp(0.35f, 0.55f, DetailBoost);
	const float DistF = FMath::Clamp(1.0f - (DistM - 300.0f) / 500.0f, LodFloor, 1.0f);
	const float AltF = FMath::Clamp(1.0f - (PawnAGL - 400.0f) / 600.0f, LodFloor, 1.0f);
	TreeDensity *= DistF * AltF;
	ShrubDensity *= (DistM < FMath::Lerp(250.0f, 420.0f, DetailBoost) ? 1.0f : 0.0f);

	const double CellNS = CellDeg * 110540.0;
	const double CellEW = CellDeg * 111320.0 * FMath::Cos(FMath::DegreesToRadians(CenLat));
	const int32 MaxTrees = FMath::FloorToInt(CellNS * CellEW / (14.0 * 14.0));
	const int32 NTrees = FMath::FloorToInt(MaxTrees * TreeDensity);
	const int32 NShrubs = FMath::FloorToInt(ShrubDensity * 14.0f);

	const bool bCoastal = IsCoastal(CenLat, CenLon, 160.0);
	// Debug-viz cell color = the semantic at the cell center.
	{
		FVegContext CCtx = MakeCtx(CenLat, CenLon, 0.0); CCtx.bCoastal = bCoastal;
		Cell.DominantSem = (uint8)VegSem::ClassifyTree(Center, CCtx, 0.0f);
	}

	FRandomStream RS(HashCombine(Key.X * 73856093, Key.Y * 19349663));
	TMap<UStaticMesh*, UInstancedStaticMeshComponent*> PerMesh;

	auto Place = [&](const FVegRuntimeSet* Set, UStaticMesh* Mesh, const FVector& Pos, float Scale)
	{
		if (!Mesh || !Set) return;
		UInstancedStaticMeshComponent*& ISM = PerMesh.FindOrAdd(Mesh);
		if (!ISM)
		{
			ISM = MakeISM(Mesh, CC, Set->bCastShadow);
			if (ISM)
			{
				for (int32 s = 0; s < Set->Mats.Num() && s < Mesh->GetStaticMaterials().Num(); ++s)
					if (Set->Mats[s]) ISM->SetMaterial(s, Set->Mats[s]);
				Cell.Comps.Add(ISM);
			}
		}
		if (ISM)
		{
			FTransform T(FRotator(RS.FRandRange(-2.5f, 2.5f), RS.FRandRange(0.0f, 360.0f), RS.FRandRange(-2.5f, 2.5f)),
				Pos + FVector(0, 0, Set->GroundClearanceM * 100.0f), FVector(Scale));
			ISM->AddInstance(T, /*bWorldSpace*/true);
			Cell.Inst++;
			NumInstances++;
			NumTreeInst++;
		}
	};

	// Tier pick: near meshes close in, mid/far beyond the set's NearRadiusM.
	auto Pick = [&](const FVegRuntimeSet* Set, FRandomStream& R) -> UStaticMesh*
	{
		const TArray<UStaticMesh*>& Tier =
			(DistM > Set->NearRadiusM && Set->Mid.Num()) ? Set->Mid : Set->Near;
		return Tier.Num() ? Tier[R.RandRange(0, Tier.Num() - 1)] : nullptr;
	};

	for (int32 i = 0; i < NTrees; ++i)
	{
		const double Lat = CenLat + RS.FRandRange(-0.5, 0.5) * CellDeg;
		const double Lon = CenLon + RS.FRandRange(-0.5, 0.5) * CellDeg;
		FLandCoverPixel Px;
		if (!LandCover->GetPixelAt(Lat, Lon, Px)) { RejNoGround++; continue; }

		const FVector XY = LLToUnrealNearPawn(Lat, Lon, 0);
		FVector Pos, N;
		if (!GroundAt(XY, Pos, N)) { RejNoGround++; continue; }
		const float SlopeDeg = FMath::RadiansToDegrees(FMath::Acos(N.Z));
		if (!AllowedAt(Lat, Lon, true, Px, SlopeDeg)) { RejExcluded++; continue; }

		// treeline by elevation (geodetic height ≈ Pos relative + origin height)
		FVector LLH; PawnLLH(LLH);
		const double GroundH = LLH.Z + (Pos.Z - LastPawnPos.Z) / 100.0;
		const double Treeline = FMath::Lerp(3500.0, 2400.0, FMath::Clamp((Lat - 35.0) / 15.0, 0.0, 1.0));
		if (GroundH > Treeline) continue;

		// semantic classification → registry → variant (deterministic seed)
		FVegContext Ctx = MakeCtx(Lat, Lon, GroundH); Ctx.bCoastal = bCoastal;
		const float Roll = RS.FRand();
		EVegSemantic Sem = VegSem::ClassifyTree(Px, Ctx, Roll);
		// scattered snags inside forest
		if ((Px.Cover == 41 || Px.Cover == 42 || Px.Cover == 43) && Roll > 0.97f)
			Sem = EVegSemantic::Tree_Dead;
		const FVegRuntimeSet* Set = ResolveSet(Sem);
		if (!Set || SlopeDeg > Set->MaxSlopeDeg) { RejExcluded++; continue; }
		if (Set->MaxDrawDistanceM > 0.0f && DistM > Set->MaxDrawDistanceM) continue;
		if (UStaticMesh* Mesh = Pick(Set, RS))
			Place(Set, Mesh, Pos, RS.FRandRange(Set->ScaleRange.X, Set->ScaleRange.Y));
	}

	for (int32 i = 0; i < NShrubs; ++i)
	{
		const double Lat = CenLat + RS.FRandRange(-0.5, 0.5) * CellDeg;
		const double Lon = CenLon + RS.FRandRange(-0.5, 0.5) * CellDeg;
		FLandCoverPixel Px;
		if (!LandCover->GetPixelAt(Lat, Lon, Px)) continue;
		const FVector XY = LLToUnrealNearPawn(Lat, Lon, 0);
		FVector Pos, N;
		if (!GroundAt(XY, Pos, N)) { RejNoGround++; continue; }
		const float SlopeDeg = FMath::RadiansToDegrees(FMath::Acos(N.Z));
		if (!AllowedAt(Lat, Lon, true, Px, SlopeDeg)) { RejExcluded++; continue; }

		FVegContext Ctx = MakeCtx(Lat, Lon, 0.0); Ctx.bCoastal = bCoastal;
		const EVegSemantic Sem = VegSem::ClassifyShrub(Px, Ctx);
		const FVegRuntimeSet* Set = ResolveSet(Sem);
		if (!Set || SlopeDeg > Set->MaxSlopeDeg) { RejExcluded++; continue; }
		if (UStaticMesh* Mesh = Pick(Set, RS))
			Place(Set, Mesh, Pos, RS.FRandRange(Set->ScaleRange.X, Set->ScaleRange.Y));
	}
}

// ---- grass cells --------------------------------------------------------

bool AVegetationField::SpawnGrassCellContent(const FCellKey& Key, FCell& Cell, double CenLat, double CenLon)
{
	FLandCoverPixel Center;
	if (!LandCover->GetPixelAt(CenLat, CenLon, Center)) return false;

	float Density;
	switch (Center.Cover)
	{
	case 71: case 81: case 95: Density = 1.0f; break;
	case 21: Density = 0.8f; break;
	case 82: Density = 0.55f; break;
	case 41: case 42: case 43: case 90: Density = 0.5f; break;
	case 52: Density = 0.4f; break;
	case 22: case 23: Density = 0.3f; break;
	case 0:  Density = 0.35f; break; // no-data — modest generic cover
	default: Density = 0.0f; break;
	}
	if (Density <= 0.0f) return true; // legitimately empty (water/runway/etc.)

	const FVector CC = LLToUnrealNearPawn(CenLat, CenLon, 0);
	FVector CD = CC - LastPawnPos; CD.Z = 0;
	const float DistM = CD.Size() / 100.0f;
	Density *= FMath::Clamp(1.0f - (DistM - FMath::Lerp(90.0f, 150.0f, DetailBoost))
		/ FMath::Lerp(180.0f, 260.0f, DetailBoost), 0.3f, 1.0f);

	const bool bCoastal = IsCoastal(CenLat, CenLon, 120.0);
	{
		FVegContext CCtx = MakeCtx(CenLat, CenLon, 0.0); CCtx.bCoastal = bCoastal;
		Cell.DominantSem = (uint8)VegSem::ClassifyGrass(Center, CCtx);
	}

	FRandomStream RS(HashCombine(Key.X * 2654435761, Key.Y * 340573321));
	const int32 N = FMath::FloorToInt(420.0f * Density * FMath::Lerp(1.0f, 1.45f, DetailBoost));
	if (N <= 0) return true;

	// coarse height grid per cell — bilinear interp instead of a trace per clump
	constexpr int32 G = 6;
	FVector HG[36]; bool HitOk[36];
	for (int32 gy = 0; gy < G; ++gy)
		for (int32 gx = 0; gx < G; ++gx)
		{
			const double Lat = CenLat + (gy / (G - 1.0) - 0.5) * GrassCellDeg;
			const double Lon = CenLon + (gx / (G - 1.0) - 0.5) * GrassCellDeg;
			FVector Pos, Nrm;
			HitOk[gy * G + gx] = GroundAt(LLToUnrealNearPawn(Lat, Lon, 0), Pos, Nrm);
			HG[gy * G + gx] = Pos;
		}

	// Instances group per (semantic mesh + material) — a cell straddling a
	// marsh edge or dune line renders mixed grass types correctly.
	TMap<UStaticMesh*, UInstancedStaticMeshComponent*> PerMesh;

	for (int32 i = 0; i < N; ++i)
	{
		const double Lat = CenLat + RS.FRandRange(-0.5, 0.5) * GrassCellDeg;
		const double Lon = CenLon + RS.FRandRange(-0.5, 0.5) * GrassCellDeg;
		FLandCoverPixel Px;
		if (!LandCover->GetPixelAt(Lat, Lon, Px)) { RejNoGround++; continue; }
		if (Px.Cover == 11 || Px.Cover == 12 || Px.Cover == 24) { RejExcluded++; continue; }
		// Barren inland stays bare — on the coast it's dune sand and carries
		// sparse beachgrass.
		if (Px.Cover == 31 && !bCoastal) { RejExcluded++; continue; }
		if (Px.Impervious > 70) { RejExcluded++; continue; }
		if (Px.Road >= 1 && Px.Road <= 4) { RejExcluded++; continue; } // paved strip — keep it clear
		const float RW = RunwayDistM(Lat, Lon);
		if (RW >= 0.0f && RW < 20.0f) { RejExcluded++; continue; }

		// semantic → registry → set (density scale applied as acceptance)
		FVegContext Ctx = MakeCtx(Lat, Lon, 0.0); Ctx.RunwayDistM = RW; Ctx.bCoastal = bCoastal;
		const EVegSemantic Sem = VegSem::ClassifyGrass(Px, Ctx);
		const FVegRuntimeSet* Set = ResolveSet(Sem);
		if (!Set || !Set->Near.Num()) continue;
		if (RS.FRand() > Set->DensityScale) continue;
		if (Set->MaxDrawDistanceM > 0.0f && DistM > Set->MaxDrawDistanceM) continue;
		UStaticMesh* Mesh = Set->Near[RS.RandRange(0, Set->Near.Num() - 1)];
		if (!Mesh) continue;

		UInstancedStaticMeshComponent*& ISM = PerMesh.FindOrAdd(Mesh);
		if (!ISM)
		{
			ISM = MakeISM(Mesh, CC, Set->bCastShadow);
			if (!ISM) continue;
			for (int32 s = 0; s < Set->Mats.Num() && s < Mesh->GetStaticMaterials().Num(); ++s)
				if (Set->Mats[s]) ISM->SetMaterial(s, Set->Mats[s]);
			Cell.Comps.Add(ISM);
		}

		const FVector XY = LLToUnrealNearPawn(Lat, Lon, 0);
		// bilinear height interp inside the cell
		const float fx = FMath::Clamp((float)((Lon - (CenLon - 0.5 * GrassCellDeg)) / GrassCellDeg), 0.0f, 1.0f) * (G - 1);
		const float fy = FMath::Clamp((float)((Lat - (CenLat - 0.5 * GrassCellDeg)) / GrassCellDeg), 0.0f, 1.0f) * (G - 1);
		const int32 x0 = FMath::Min((int32)fx, G - 2), y0 = FMath::Min((int32)fy, G - 2);
		const float tx = fx - x0, ty = fy - y0;
		if (!HitOk[y0 * G + x0]) { RejNoGround++; continue; }
		const FVector Z00 = HG[y0 * G + x0], Z10 = HG[y0 * G + x0 + 1];
		const FVector Z01 = HG[(y0 + 1) * G + x0], Z11 = HG[(y0 + 1) * G + x0 + 1];
		const FVector Pos = FMath::Lerp(FMath::Lerp(Z00, Z10, tx), FMath::Lerp(Z01, Z11, tx), ty);
		FVector At = FVector(XY.X, XY.Y, Pos.Z + Set->GroundClearanceM * 100.0f);

		FTransform T(FRotator(0, RS.FRandRange(0.0f, 360.0f), 0), At,
			FVector(RS.FRandRange(Set->ScaleRange.X, Set->ScaleRange.Y)));
		const int32 Idx = ISM->AddInstance(T, /*bWorldSpace*/true);
		// custom data: [0]=dryness (semantic range), [1]=hue jitter
		ISM->SetCustomDataValue(Idx, 0, RS.FRandRange(Set->DrynessRange.X, Set->DrynessRange.Y));
		ISM->SetCustomDataValue(Idx, 1, RS.FRand());
		Cell.Inst++;
		NumInstances++;
		NumGrassInst++;
	}
	return true;
}

// ---- helpers ---------------------------------------------------------

bool AVegetationField::GroundAt(const FVector& XY, FVector& OutPos, FVector& OutNormal)
{
	FCollisionQueryParams P;
	// Trace relative to the PAWN's height, not the candidate's — candidates are
	// computed at ellipsoid height 0, which is kilometers below the surface in
	// high terrain (Denver, the Rockies). ±4 km around the pawn covers any
	// CONUS local relief.
	const float Top = LastPawnPos.Z + 400000.0f;

	// Coarse PRT ancestors are CLOSED HULLS: a trace under the top face hits
	// the shell's own interior bottom (normal -Z) and stalls. Downward-facing
	// hits are never ground — real terrain always faces up — so walk through
	// them until an up-facing surface is found.
	auto TraceDown = [&](float StartZ, FHitResult& Out) -> bool
	{
		float Z = StartZ;
		for (int32 i = 0; i < 12; ++i)
		{
			FHitResult H;
			if (!GetWorld()->LineTraceSingleByChannel(H, FVector(XY.X, XY.Y, Z),
				FVector(XY.X, XY.Y, Z - 400000.0f), ECC_Visibility, P)) return false;
			if (H.ImpactNormal.Z >= 0.2f) { Out = H; return true; }
			Z = H.ImpactPoint.Z - 100.0f;   // step under the interior face
		}
		return false;
	};

	FHitResult Hit;
	if (!TraceDown(Top, Hit)) return false;

	// Peel elevated surfaces: vegetation belongs on terrain, never on roofs,
	// decks, or shell fragments. A deeper up-facing surface >6 m below means
	// the upper layer was a structure or a phantom hull.
	for (int32 Layer = 0; Layer < 6; ++Layer)
	{
		FHitResult Sub;
		if (!TraceDown(Hit.ImpactPoint.Z - 150.0f, Sub)) break;
		if (Hit.ImpactPoint.Z - Sub.ImpactPoint.Z < 600.0f) break;
		Hit = Sub;
	}
	OutPos = Hit.ImpactPoint;
	OutNormal = Hit.ImpactNormal;
	return true;
}

UInstancedStaticMeshComponent* AVegetationField::MakeISM(UStaticMesh* Mesh, const FVector& CellOrigin, bool bCastShadow)
{
	if (!IsValid(Mesh)) return nullptr; // IsValid, not null — catches zombie assets post-GC
	UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(this);
	ISM->SetStaticMesh(Mesh);
	ISM->SetMobility(EComponentMobility::Movable);
	ISM->SetCastShadow(bCastShadow);
	ISM->bUseAsOccluder = false;
	ISM->NumCustomDataFloats = 2;
	ISM->RegisterComponent();
	return ISM;
}

// ---- semantic → asset resolution ---------------------------------------
// The ONLY path from a semantic category to renderable art. Cells store no
// mesh references — just a category; a library edit changes what every
// matching location renders without touching placement.

const FVegRuntimeSet* AVegetationField::ResolveSet(EVegSemantic Sem)
{
	if (Sem == EVegSemantic::None || !Library) return nullptr;
	if (FVegRuntimeSet* R = ResolvedSets.Find((uint8)Sem)) return R;
	FVegRuntimeSet& R = ResolvedSets.Add((uint8)Sem);
	Library->ResolveAll(Sem, R);
	for (UStaticMesh* M : R.Near) ResolvedMeshKeepAlive.AddUnique(M);
	for (UStaticMesh* M : R.Mid)  ResolvedMeshKeepAlive.AddUnique(M);
	for (UStaticMesh* M : R.Far)  ResolvedMeshKeepAlive.AddUnique(M);
	for (UMaterialInterface* M : R.Mats) ResolvedMatKeepAlive.AddUnique(M);
	return &R;
}

bool AVegetationField::BuildContextAt(double Lat, double Lon, double GroundH, FVegContext& Out) const
{
	if (!LandCover) return false;
	Out = MakeCtx(Lat, Lon, GroundH);
	Out.bCoastal = IsCoastal(Lat, Lon, 150.0);
	return true;
}

bool AVegetationField::GetCoverAt(double Lat, double Lon, FLandCoverPixel& Out) const
{
	return LandCover && LandCover->GetPixelAt(Lat, Lon, Out);
}

FVegContext AVegetationField::MakeCtx(double Lat, double Lon, double GroundH) const
{
	FVegContext Ctx;
	Ctx.RunwayDistM = RunwayDistM(Lat, Lon);
	Ctx.GroundH = GroundH;
	Ctx.Lat = Lat; Ctx.Lon = Lon;
	return Ctx;
}

bool AVegetationField::IsCoastal(double Lat, double Lon, double RadiusM) const
{
	// Any OpenWater pixel inside the radius → the cell reads as coastal.
	const double dLat = RadiusM / 110540.0;
	const double dLon = RadiusM / (111320.0 * FMath::Cos(FMath::DegreesToRadians(Lat)));
	static const double Off[9][2] = {
		{0,0},{1,0},{-1,0},{0,1},{0,-1},{0.7,0.7},{-0.7,0.7},{0.7,-0.7},{-0.7,-0.7} };
	for (auto& O : Off)
	{
		FLandCoverPixel P;
		if (LandCover->GetPixelAt(Lat + O[0] * dLat, Lon + O[1] * dLon, P) && P.Cover == 11)
			return true;
	}
	return false;
}

void AVegetationField::DrawDebugCells()
{
	// Per-semantic debug palette — grass family = greens, shrub = oranges,
	// tree = blues/reds, none = gray.
	auto ColorFor = [](uint8 Sem) -> FColor
	{
		const EVegSemantic S = (EVegSemantic)Sem;
		if (VegSem::IsGrass(S))
		{
			switch (S)
			{
			case EVegSemantic::Grass_AirportMowed: return FColor(80, 255, 80);
			case EVegSemantic::Grass_Mowed:        return FColor(120, 230, 80);
			case EVegSemantic::Grass_NaturalShort: return FColor(160, 200, 60);
			case EVegSemantic::Grass_NaturalTall:  return FColor(200, 180, 40);
			case EVegSemantic::Grass_Pasture:      return FColor(150, 190, 90);
			case EVegSemantic::Grass_Cropland:     return FColor(210, 160, 50);
			case EVegSemantic::Grass_Coastal:      return FColor(220, 220, 120);
			case EVegSemantic::Grass_Dune:         return FColor(240, 230, 160);
			case EVegSemantic::Grass_Wetland:      return FColor(60, 200, 160);
			default:                              return FColor(100, 170, 100);
			}
		}
		if (VegSem::IsShrub(S)) return FColor(255, 140, 40);
		if (VegSem::IsTree(S))
		{
			switch (S)
			{
			case EVegSemantic::Tree_Deciduous: return FColor(40, 140, 255);
			case EVegSemantic::Tree_Conifer:   return FColor(20, 80, 255);
			case EVegSemantic::Tree_Coastal:   return FColor(80, 200, 255);
			case EVegSemantic::Tree_Wetland:   return FColor(160, 80, 255);
			case EVegSemantic::Tree_Dead:      return FColor(200, 60, 60);
			default:                           return FColor(120, 120, 255);
			}
		}
		return FColor(120, 120, 120);
	};
	auto Draw = [&](TMap<FCellKey, FCell>& Map, double CellD, float R)
	{
		for (auto& KV : Map)
		{
			const double CenLat = (KV.Key.Y + 0.5) * CellD, CenLon = (KV.Key.X + 0.5) * CellD;
			const FVector C = LLToUnrealNearPawn(CenLat, CenLon, 0);
			DrawDebugSphere(GetWorld(), FVector(C.X, C.Y, KV.Value.GroundZ + 3000.0f), R, 12,
				ColorFor(KV.Value.DominantSem), false, 0.25f, 0, R * 0.06f);
		}
	};
	Draw(Cells, CellDeg, 500.0f);
	Draw(GrassCells, GrassCellDeg, 250.0f);
}

void AVegetationField::DespawnAll()
{
	for (auto& KV : Cells) DespawnCell(Cells, KV.Key);
	for (auto& KV : GrassCells) DespawnCell(GrassCells, KV.Key);
	Cells.Empty(); GrassCells.Empty();
}

// ---- dev diagnostics ---------------------------------------------------

void AVegetationField::DumpSemanticsAt(double Lat, double Lon)
{
	FLandCoverPixel Px;
	if (!LandCover || !LandCover->GetPixelAt(Lat, Lon, Px))
	{
		UE_LOG(LogParamotor, Warning, TEXT("PsSemGeo: no land-cover data at %.5f,%.5f"), Lat, Lon);
		return;
	}
	FVegContext Ctx = MakeCtx(Lat, Lon, 0.0);
	Ctx.bCoastal = IsCoastal(Lat, Lon, 150.0);
	const EVegSemantic G = VegSem::ClassifyGrass(Px, Ctx);
	const EVegSemantic T = VegSem::ClassifyTree(Px, Ctx, 0.0f);
	const EVegSemantic S = VegSem::ClassifyShrub(Px, Ctx);
	const ESurfaceSemantic Surf = VegSem::ClassifySurface(Px, Ctx);
	const FVegRuntimeSet* GS = ResolveSet(G);
	const FVegRuntimeSet* TS = ResolveSet(T);
	auto MeshNames = [](const FVegRuntimeSet* R)
	{
		if (!R || !R->Near.Num()) return FString(TEXT("none"));
		TArray<FString> N;
		for (UStaticMesh* M : R->Near) N.Add(M ? M->GetName() : TEXT("null"));
		return FString::Join(N, TEXT(","));
	};
	UE_LOG(LogParamotor, Warning, TEXT(
		"PsSemGeo @ %.5f,%.5f | NLCD=%d imperv=%d road=%d canopy=%d coastal=%d runway=%.0fm\n"
		"  surface=%s | grass=%s [%s] | tree=%s [%s] | shrub=%s"),
		Lat, Lon, Px.Cover, Px.Impervious, Px.Road, Px.Canopy, Ctx.bCoastal ? 1 : 0, Ctx.RunwayDistM,
		VegSem::ToName(Surf), VegSem::ToName(G), *MeshNames(GS), VegSem::ToName(T), *MeshNames(TS),
		VegSem::ToName(S));
}

void AVegetationField::DebugSwap(EVegSemantic Sem, const FString& MeshPath)
{
	UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, *MeshPath);
	if (!M)
	{
		UE_LOG(LogParamotor, Warning, TEXT("PsVegSwap: mesh not found: %s"), *MeshPath);
		return;
	}
	FVegRuntimeSet& R = ResolvedSets.FindOrAdd((uint8)Sem);
	R.Near = { M }; R.Mid = { M }; R.Far = { M };
	ResolvedMeshKeepAlive.AddUnique(M);
	DespawnAll();
	UE_LOG(LogParamotor, Warning, TEXT("PsVegSwap: %s -> %s (all cells rebuilding)"),
		VegSem::ToName(Sem), *M->GetName());
}

void AVegetationField::DebugReset()
{
	ResolvedSets.Empty();
	DespawnAll();
	UE_LOG(LogParamotor, Warning, TEXT("PsVegReset: library re-resolved, cells rebuilding"));
}

void AVegetationField::DumpLibrary()
{
	UE_LOG(LogParamotor, Warning, TEXT("PsVegLib: %s (%d categories resolved)"),
		Library && Library->GetName().Contains(TEXT("DA_VegetationLibrary"))
			? TEXT("source=/Game/Data/DA_VegetationLibrary") : TEXT("source=code defaults"),
		ResolvedSets.Num());
	for (uint8 i = 1; i < (uint8)EVegSemantic::MAX; ++i)
	{
		const FVegRuntimeSet* R = ResolveSet((EVegSemantic)i);
		TArray<FString> N;
		if (R) for (UStaticMesh* M : R->Near) N.Add(M ? M->GetName() : TEXT("null"));
		UE_LOG(LogParamotor, Warning, TEXT("  %-20s near=%d mid=%d far=%d : %s"),
			VegSem::ToName((EVegSemantic)i), R ? R->Near.Num() : 0, R ? R->Mid.Num() : 0,
			R ? R->Far.Num() : 0, *FString::Join(N, TEXT(",")));
	}
}

void AVegetationField::LoadRunways()
{
	TArray<uint8> Bytes;
	const FString Path = FPaths::ProjectContentDir() / TEXT("Data/runways.bin");
	if (!FFileHelper::LoadFileToArray(Bytes, *Path) || Bytes.Num() < 8) return;
	if (*(int32*)Bytes.GetData() != 0x52575931) return;
	const int32 N = *(int32*)(Bytes.GetData() + 4);
	const uint8* P = Bytes.GetData() + 8;
	for (int32 i = 0; i < N && P + 24 <= (uint8*)Bytes.GetData() + Bytes.Num(); ++i, P += 24)
	{
		FRunway R;
		FMemory::Memcpy(&R.Lat1, P, 4); FMemory::Memcpy(&R.Lon1, P + 4, 4);
		FMemory::Memcpy(&R.Lat2, P + 8, 4); FMemory::Memcpy(&R.Lon2, P + 12, 4);
		FMemory::Memcpy(&R.HalfWidthM, P + 16, 4);
		int16 Closed; FMemory::Memcpy(&Closed, P + 20, 2);
		R.Closed = Closed;
		Runways.Add(R);
	}

	// spatial index: insert each runway into every 0.1° cell its bbox touches
	// (plus a 300 m query pad so borderline lookups don't miss it).
	const double PadLat = 300.0 / 110540.0, PadLon = 300.0 / 111320.0 / 0.7; // cos(45)
	for (int32 i = 0; i < Runways.Num(); ++i)
	{
		const FRunway& R = Runways[i];
		const int32 X0 = FMath::FloorToInt((FMath::Min(R.Lon1, R.Lon2) - PadLon) / 0.1);
		const int32 X1 = FMath::FloorToInt((FMath::Max(R.Lon1, R.Lon2) + PadLon) / 0.1);
		const int32 Y0 = FMath::FloorToInt((FMath::Min(R.Lat1, R.Lat2) - PadLat) / 0.1);
		const int32 Y1 = FMath::FloorToInt((FMath::Max(R.Lat1, R.Lat2) + PadLat) / 0.1);
		for (int32 Y = Y0; Y <= Y1; ++Y)
			for (int32 X = X0; X <= X1; ++X)
				RunwayGrid.FindOrAdd(((int64)X << 32) | (uint32)Y).Add(i);
	}
	UE_LOG(LogParamotor, Log, TEXT("VegetationField: %d runway capsules, %d grid cells"), Runways.Num(), RunwayGrid.Num());
}

float AVegetationField::RunwayDistM(double Lat, double Lon) const
{
	const int64 Key = ((int64)FMath::FloorToInt(Lon / 0.1) << 32) | (uint32)FMath::FloorToInt(Lat / 0.1);
	const TArray<int32>* List = RunwayGrid.Find(Key);
	if (!List) return -10000.0f;

	float Best = -10000.0f;
	for (int32 i : *List)
	{
		const FRunway& R = Runways[i];
		if (R.Closed) continue;
		const double Bx = (R.Lon2 - R.Lon1) * 111320.0 * FMath::Cos(FMath::DegreesToRadians(R.Lat1));
		const double By = (R.Lat2 - R.Lat1) * 110540.0;
		const double Px = (Lon - R.Lon1) * 111320.0 * FMath::Cos(FMath::DegreesToRadians(R.Lat1));
		const double Py = (Lat - R.Lat1) * 110540.0;
		const double Len2 = Bx * Bx + By * By;
		const double T = Len2 > 0 ? FMath::Clamp((Px * Bx + Py * By) / Len2, 0.0, 1.0) : 0.0;
		const double Dx = Px - Bx * T, Dy = Py - By * T;
		const float D = (float)FMath::Sqrt(Dx * Dx + Dy * Dy) - R.HalfWidthM;
		if (Best < -9000.0f || D < Best) Best = D;
	}
	return Best;
}

// ---- wind + prop wash --------------------------------------------------

UMaterialParameterCollection* AVegetationField::LoadVegMPC()
{
	return LoadObject<UMaterialParameterCollection>(nullptr, TEXT("/Game/Materials/MPC_Veg"));
}

void AVegetationField::UpdateWindParams()
{
	if (!IsValid(VegMPC) || !IsValid(VegMPCAsset)) return;
	if (!bVegParamsChecked)
	{
		bVegParamsChecked = true;
		auto HasV = [&](const TCHAR* N)
		{
			for (const FCollectionVectorParameter& P : VegMPCAsset->VectorParameters)
				if (P.ParameterName == N) return true;
			return false;
		};
		auto HasS = [&](const TCHAR* N)
		{
			for (const FCollectionScalarParameter& P : VegMPCAsset->ScalarParameters)
				if (P.ParameterName == N) return true;
			return false;
		};
		bHasWindDir  = HasV(TEXT("WindDir"));
		bHasPropPos  = HasV(TEXT("PropPos"));
		bHasPropDir  = HasV(TEXT("PropDir"));
		bHasWindSpeed= HasS(TEXT("WindSpeed"));
		bHasGust     = HasS(TEXT("GustSpeed"));
		bHasTime     = HasS(TEXT("VegTime"));
		bHasPropStr  = HasS(TEXT("PropStrength"));
		UE_LOG(LogParamotor, Log, TEXT("VegetationField: MPC params wind=%d/%d/%d time=%d prop=%d/%d/%d"),
			bHasWindDir, bHasWindSpeed, bHasGust, bHasTime, bHasPropPos, bHasPropDir, bHasPropStr);
	}
	UParamotorGameInstance* GI = GetWorld()->GetGameInstance<UParamotorGameInstance>();
	if (!GI) return;

	// Wind direction is "FROM" in METAR convention → blowing direction = +180°.
	const float BlowDeg = GI->WindDirection + 180.0f;
	const float Rad = FMath::DegreesToRadians(BlowDeg);
	// ESU frame: +X east, +Y south. Heading deg: 0=N,90=E.
	const FVector WindVec(FMath::Sin(Rad) * GI->WindSpeed, -FMath::Cos(Rad) * GI->WindSpeed, 0.0f);
	if (bHasWindDir)   VegMPC->SetVectorParameterValue(TEXT("WindDir"), FLinearColor(WindVec.X, WindVec.Y, WindVec.Z, 0));
	if (bHasWindSpeed) VegMPC->SetScalarParameterValue(TEXT("WindSpeed"), GI->WindSpeed);
	if (bHasGust)      VegMPC->SetScalarParameterValue(TEXT("GustSpeed"), GI->WindGusts);
	if (bHasTime)      VegMPC->SetScalarParameterValue(TEXT("VegTime"), GetWorld()->GetTimeSeconds());

	// prop wash — same field source as the sand system
	FVector PropPos = FVector::ZeroVector, PropDir = FVector::ZeroVector;
	float Strength = 0.0f;
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
		if (AParamotorPlayerController* PPC = Cast<AParamotorPlayerController>(PC))
			if (AParamotorActor* PA = PPC->GetSpawnedParamotorActor())
			if (IsValid(PA) && !PA->IsActorBeingDestroyed())
			{
				const float RPM = PA->GetPropRPM();
				if (RPM > 800.0f)
				{
					const FTransform PT = PA->GetPropTransform();
					PropPos = PT.GetLocation();
					PropDir = -PT.GetRotation().GetForwardVector();
					PropDir.Z = 0; PropDir = PropDir.GetSafeNormal();
					Strength = FMath::Clamp((RPM - 800.0f) / 2200.0f, 0.0f, 1.0f);
				}
			}
	if (bHasPropPos) VegMPC->SetVectorParameterValue(TEXT("PropPos"), FLinearColor(PropPos.X, PropPos.Y, PropPos.Z, 0));
	if (bHasPropDir) VegMPC->SetVectorParameterValue(TEXT("PropDir"), FLinearColor(PropDir.X, PropDir.Y, PropDir.Z, 0));
	if (bHasPropStr) VegMPC->SetScalarParameterValue(TEXT("PropStrength"), Strength);
}
