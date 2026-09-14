// Copyright Epic Games, Inc. All Rights Reserved.

#include "ForestScatter.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "ParamotorLogging.h"

AForestScatter::AForestScatter()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.5f;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	// Placeholder foliage — the user's real tree pack replaces these.
	static const TCHAR* Defs[] = {
		TEXT("/Game/Meshes/trees/fir_tree_01_a_LOD0.fir_tree_01_a_LOD0"),
		TEXT("/Game/Meshes/trees/fir_tree_01_b_LOD0.fir_tree_01_b_LOD0"),
		TEXT("/Game/Meshes/trees/pine_tree_01_a_LOD0.pine_tree_01_a_LOD0"),
		TEXT("/Game/Meshes/trees/pine_tree_01_b_LOD0.pine_tree_01_b_LOD0"),
	};
	for (const TCHAR* P : Defs)
		if (UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, P))
			TreeMeshes.Add(M);
}

bool AForestScatter::LoadGrid(const FString& Path, double MapOriginUtmE_, double MapOriginUtmN_)
{
	TArray<uint8> Raw;
	if (!FFileHelper::LoadFileToArray(Raw, *Path))
	{
		UE_LOG(LogParamotor, Warning, TEXT("ForestScatter: no grid at %s"), *Path);
		return false;
	}
	// header: magic u32 | ver u32 | originE f64 | originN f64 | cell f32 | cols u32 | rows u32
	if (Raw.Num() < 4 + 4 + 8 + 8 + 4 + 4 + 4) return false;
	const uint8* P = Raw.GetData();
	uint32 Magic; FMemory::Memcpy(&Magic, P, 4);
	if (Magic != 0x44524746 /*'FGRD'*/) return false;
	P += 4; P += 4;  // magic + version
	double OE; FMemory::Memcpy(&OE, P, 8); P += 8;
	double ON; FMemory::Memcpy(&ON, P, 8); P += 8;
	float CellM; FMemory::Memcpy(&CellM, P, 4); P += 4;
	uint32 Cols; FMemory::Memcpy(&Cols, P, 4); P += 4;
	uint32 Rows; FMemory::Memcpy(&Rows, P, 4); P += 4;
	if (Raw.Num() < (int32)(P - Raw.GetData()) + (int32)(Cols * Rows)) return false;

	GridOriginUtmE = OE; GridOriginUtmN = ON;
	GridCols = (int32)Cols; GridRows = (int32)Rows;
	GridCellM = CellM;
	MapOriginUtmE = MapOriginUtmE_; MapOriginUtmN = MapOriginUtmN_;
	Density.SetNumUninitialized(Cols * Rows);
	FMemory::Memcpy(Density.GetData(), P, Cols * Rows);

	UE_LOG(LogParamotor, Log, TEXT("ForestScatter: grid %dx%d @ %.0fm, %dKB"), GridCols, GridRows, CellM, Density.Num() / 1024);
	return true;
}

uint8 AForestScatter::DensityAt(int32 C, int32 R) const
{
	if (C < 0 || R < 0 || C >= GridCols || R >= GridRows) return 0;
	return Density[R * GridCols + C];
}

void AForestScatter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (TreeMeshes.Num() == 0 || Density.Num() == 0) return;

	RepopulateTimer -= DeltaTime;
	if (RepopulateTimer > 0.0f) return;
	RepopulateTimer = 0.5f;

	if (!CachedPawn)
		CachedPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!CachedPawn) return;

	Repopulate(CachedPawn);
}

void AForestScatter::Repopulate(APawn* Pawn)
{
	const FVector P = Pawn->GetActorLocation();
	const float R = ScatterRadiusM * 100.0f;
	const float R2 = R * R;

	// Cells inside the radius
	TSet<FCellKey> Wanted;
	const int32 MinC = FMath::FloorToInt((P.X - R) / CellSizeCm);
	const int32 MaxC = FMath::CeilToInt((P.X + R) / CellSizeCm);
	const int32 MinR = FMath::FloorToInt((P.Y - R) / CellSizeCm);
	const int32 MaxR = FMath::CeilToInt((P.Y + R) / CellSizeCm);
	for (int32 CY = MinR; CY <= MaxR; ++CY)
		for (int32 CX = MinC; CX <= MaxC; ++CX)
		{
			const FVector Cc((CX + 0.5f) * CellSizeCm, (CY + 0.5f) * CellSizeCm, 0.0f);
			if (FVector::DistSquaredXY(Cc, P) <= R2)
				Wanted.Add({CX, CY});
		}

	// Despawn out-of-range cells
	for (auto It = LiveCells.CreateIterator(); It; ++It)
	{
		if (!Wanted.Contains(It->Key))
		{
			if (It->Value) It->Value->DestroyComponent();
			It.RemoveCurrent();
		}
	}
	// Spawn new cells
	for (const FCellKey& K : Wanted)
		if (!LiveCells.Contains(K))
			SpawnCell(K);
}

void AForestScatter::SpawnCell(const FCellKey& K)
{
	// Live cells are CellSizeCm (480m); each candidate position samples the
	// 60m density grid — a tree lands only where OSM says forest.
	const FRandomStream RS(HashCombine(GetTypeHash(K), 0x9E3779B9));

	UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(this);
	ISM->SetupAttachment(RootComponent);
	ISM->SetMobility(EComponentMobility::Static);
	ISM->bCastDynamicShadow = true;
	ISM->bCastContactShadow = false;
	// One species per cell — forests are monoculture patches anyway.
	// Mesh must be set BEFORE RegisterComponent or the static-mobility
	// render proxy never builds.
	ISM->SetStaticMesh(TreeMeshes[RS.RandRange(0, TreeMeshes.Num() - 1)]);
	ISM->RegisterComponent();

	bool bAny = false;
	const int32 Candidates = TreesPerCell * 8;   // density-filtered below
	for (int32 I = 0; I < Candidates; ++I)
	{
		const float LX = (K.X + RS.FRand()) * CellSizeCm;
		const float LY = (K.Y + RS.FRand()) * CellSizeCm;

		const double UtmE = MapOriginUtmE + LX / 100.0;
		const double UtmN = MapOriginUtmN + LY / 100.0;
		const int32 GC = FMath::FloorToInt((UtmE - GridOriginUtmE) / GridCellM);
		const int32 GR = FMath::FloorToInt((UtmN - GridOriginUtmN) / GridCellM);
		const uint8 D = DensityAt(GC, GR);
		if (D == 0 || RS.FRand() * 255.0f > (float)D) continue;

		// Ground trace — landscape or Cesium tiles, whatever's under.
		FHitResult Hit;
		const FVector A(LX, LY, 200000.0f), B(LX, LY, -50000.0f);
		if (!GetWorld()->LineTraceSingleByChannel(Hit, A, B, ECC_Visibility)) continue;
		const float GroundZ = Hit.ImpactPoint.Z;

		const float Scale = 0.85f + RS.FRand() * 0.5f;
		const FRotator Yaw(0.0f, RS.FRand() * 360.0f, 0.0f);
		ISM->AddInstance(FTransform(Yaw, FVector(LX, LY, GroundZ), FVector(Scale)));
		bAny = true;
	}
	if (!bAny) { ISM->DestroyComponent(); ISM = nullptr; }
	LiveCells.Add(K, ISM);
}

void AForestScatter::DespawnCell(UInstancedStaticMeshComponent* ISM)
{
	if (ISM) ISM->DestroyComponent();
}
