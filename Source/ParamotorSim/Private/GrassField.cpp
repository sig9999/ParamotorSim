// Copyright Epic Games, Inc. All Rights Reserved.

#include "GrassField.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Engine/World.h"
#include "GameFramework/PlayerStart.h"
#include "EngineUtils.h"
#include "Math/RandomStream.h"

AGrassField::AGrassField()
{
	PrimaryActorTick.bCanEverTick = false;

	FieldCenter = FVector(0.0f, 0.0f, 0.0f);
	FieldRadius = 5000.0f;       // 50m radius
	BladeHeight = 8.0f;          // 8cm = short mowed lawn
	BladeWidth = 1.5f;           // 1.5cm wide at base
	DensityPerSqM = 60.0f;       // 60 blades per m²

	GrassMaterial = nullptr;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
}

void AGrassField::BeginPlay()
{
	Super::BeginPlay();

	// Auto-detect PlayerStart
	if (FieldCenter.IsNearlyZero() && GetWorld())
	{
		for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It)
		{
			FieldCenter = It->GetActorLocation();
			UE_LOG(LogTemp, Log, TEXT("GrassField: Centering on PlayerStart (%.0f, %.0f, %.0f)"),
				FieldCenter.X, FieldCenter.Y, FieldCenter.Z);
			break;
		}
	}

	GenerateGrass();
}

// Generate a single 3D grass blade as a tapered strip with real thickness.
// The blade has front and back surfaces, left and right edges, and tapers to a point.
void AddBladeGeometry(
	TArray<FVector>& Vertices,
	TArray<int32>& Triangles,
	TArray<FVector>& Normals,
	TArray<FVector2D>& UVs,
	TArray<FLinearColor>& Colors,
	const FVector& BaseLoc,
	float YawDeg,
	float Height,
	float Width,
	float Curve,
	FRandomStream& RNG)
{
	// Blade is built in local space (origin at base, Y up), then transformed to world
	FQuat Rot(FVector::UpVector, FMath::DegreesToRadians(YawDeg));

	int32 NumSegments = 6;
	int32 BaseVertexIndex = Vertices.Num();

	for (int32 i = 0; i <= NumSegments; i++)
	{
		float t = (float)i / NumSegments;  // 0 at base, 1 at tip

		// Height along blade
		float LocalY = t * Height;

		// Width tapers to zero at tip (sqrt for natural blade shape)
		float w = Width * FMath::Pow(1.0f - t, 0.6f);
		if (i == NumSegments) w = 0.0f;

		// Curve: blade bends forward (in X) as it gets taller
		float LocalX = Curve * t * t * Height * 0.2f;

		// Slight random flutter
		float FlutterX = RNG.FRandRange(-0.05f, 0.05f) * t;
		LocalX += FlutterX;

		// Thickness: real 3D volume, thinner toward tip
		float Thickness = 0.06f * (1.0f - t * 0.8f);  // 0.6mm at base

		if (w > 0.01f)
		{
			// 4 vertices: front-left, front-right, back-left, back-right
			FVector LocalFL(LocalX - w * 0.5f, LocalY, Thickness * 0.5f);
			FVector LocalFR(LocalX + w * 0.5f, LocalY, Thickness * 0.5f);
			FVector LocalBL(LocalX - w * 0.5f, LocalY, -Thickness * 0.5f);
			FVector LocalBR(LocalX + w * 0.5f, LocalY, -Thickness * 0.5f);

			Vertices.Add(BaseLoc + Rot.RotateVector(LocalFL));
			Vertices.Add(BaseLoc + Rot.RotateVector(LocalFR));
			Vertices.Add(BaseLoc + Rot.RotateVector(LocalBL));
			Vertices.Add(BaseLoc + Rot.RotateVector(LocalBR));

			// UVs: U across width, V along height
			UVs.Add(FVector2D(0.0f, t));
			UVs.Add(FVector2D(1.0f, t));
			UVs.Add(FVector2D(0.0f, t));
			UVs.Add(FVector2D(1.0f, t));

			// Vertex colors: slight green variation per blade
			FLinearColor BladeColor = FLinearColor(
				RNG.FRandRange(0.15f, 0.30f),  // R
				RNG.FRandRange(0.30f, 0.55f),  // G
				RNG.FRandRange(0.10f, 0.25f),  // B
				1.0f
			);
			Colors.Add(BladeColor);
			Colors.Add(BladeColor);
			Colors.Add(BladeColor);
			Colors.Add(BladeColor);
		}
		else
		{
			// Tip: 2 vertices (front and back merge to near-zero width)
			FVector LocalTipFront(LocalX, LocalY, Thickness * 0.2f);
			FVector LocalTipBack(LocalX, LocalY, -Thickness * 0.2f);

			Vertices.Add(BaseLoc + Rot.RotateVector(LocalTipFront));
			Vertices.Add(BaseLoc + Rot.RotateVector(LocalTipBack));

			UVs.Add(FVector2D(0.5f, t));
			UVs.Add(FVector2D(0.5f, t));

			FLinearColor BladeColor = FLinearColor(
				RNG.FRandRange(0.15f, 0.30f),
				RNG.FRandRange(0.30f, 0.55f),
				RNG.FRandRange(0.10f, 0.25f),
				1.0f
			);
			Colors.Add(BladeColor);
			Colors.Add(BladeColor);
		}
	}

	// Build triangles connecting segments
	for (int32 i = 0; i < NumSegments; i++)
	{
		int32 Base = BaseVertexIndex + i * 4;
		int32 Next = BaseVertexIndex + (i + 1) * 4;

		bool bIsLastSegment = (i == NumSegments - 1);

		if (bIsLastSegment)
		{
			// Next row has only 2 vertices (tip)
			// Front face: cur_fl, cur_fr, tip_front
			Triangles.Add(Base + 0); Triangles.Add(Base + 1); Triangles.Add(Next + 0);
			// Back face: cur_br, cur_bl, tip_back
			Triangles.Add(Base + 3); Triangles.Add(Base + 2); Triangles.Add(Next + 1);
			// Left edge: cur_bl, cur_fl, tip_front, tip_back
			Triangles.Add(Base + 2); Triangles.Add(Base + 0); Triangles.Add(Next + 0);
			Triangles.Add(Base + 2); Triangles.Add(Next + 0); Triangles.Add(Next + 1);
			// Right edge: cur_fr, cur_br, tip_back, tip_front
			Triangles.Add(Base + 1); Triangles.Add(Base + 3); Triangles.Add(Next + 1);
			Triangles.Add(Base + 1); Triangles.Add(Next + 1); Triangles.Add(Next + 0);
		}
		else
		{
			// Front face (normal +Z)
			Triangles.Add(Base + 0); Triangles.Add(Base + 1); Triangles.Add(Next + 1);
			Triangles.Add(Base + 0); Triangles.Add(Next + 1); Triangles.Add(Next + 0);
			// Back face (normal -Z)
			Triangles.Add(Base + 3); Triangles.Add(Base + 2); Triangles.Add(Next + 2);
			Triangles.Add(Base + 3); Triangles.Add(Next + 2); Triangles.Add(Next + 3);
			// Left edge
			Triangles.Add(Base + 2); Triangles.Add(Base + 0); Triangles.Add(Next + 0);
			Triangles.Add(Base + 2); Triangles.Add(Next + 0); Triangles.Add(Next + 2);
			// Right edge
			Triangles.Add(Base + 1); Triangles.Add(Base + 3); Triangles.Add(Next + 3);
			Triangles.Add(Base + 1); Triangles.Add(Next + 3); Triangles.Add(Next + 1);
		}
	}

	// Compute normals for this blade's vertices
	int32 NumBladeVerts = Vertices.Num() - BaseVertexIndex;
	for (int32 i = 0; i < NumBladeVerts; i++)
	{
		Normals.Add(FVector::UpVector);  // Simplified — PMC will recompute if needed
	}
}

void AGrassField::GenerateGrass()
{
	// Load grass material
	if (!GrassMaterial)
	{
		GrassMaterial = LoadObject<UMaterialInterface>(
			nullptr, TEXT("/Game/Materials/M_GrassBlade.M_GrassBlade"));
	}
	if (!GrassMaterial)
	{
		GrassMaterial = LoadObject<UMaterialInterface>(
			nullptr, TEXT("/Game/Materials/M_Grass.M_Grass"));
	}

	// We'll build the grass in chunks to avoid hitting vertex index limits
	// (ProceduralMeshComponent uses 16-bit indices = 65535 vertices max per section)
	// Each blade has ~28 vertices (7 segments × 4 verts), so ~2300 blades per section
	int32 MaxVertsPerSection = 60000;
	int32 VertsPerBlade = 28;  // approximate
	int32 MaxBladesPerSection = MaxVertsPerSection / VertsPerBlade;

	// Calculate total blades
	float AreaSqM = PI * (FieldRadius / 100.0f) * (FieldRadius / 100.0f);
	int32 TotalBlades = FMath::RoundToInt(DensityPerSqM * AreaSqM);

	UE_LOG(LogTemp, Log, TEXT("GrassField: Generating %d real 3D grass blades over %.0f m²"),
		TotalBlades, AreaSqM);

	// Grid spacing
	float SpacingM = FMath::Sqrt(1.0f / DensityPerSqM);
	float SpacingCm = SpacingM * 100.0f;
	float Jitter = SpacingCm * 0.35f;

	FRandomStream RNG(4242);

	// Arrays for current section
	TArray<FVector> SectionVerts;
	TArray<int32> SectionTris;
	TArray<FVector> SectionNormals;
	TArray<FVector2D> SectionUVs;
	TArray<FLinearColor> SectionColors;

	int32 SectionIndex = 0;
	int32 BladesInSection = 0;
	int32 TotalPlaced = 0;

	int32 GridExtent = FMath::CeilToInt(FieldRadius / SpacingCm) + 1;

	for (int32 gx = -GridExtent; gx <= GridExtent; gx++)
	{
		for (int32 gy = -GridExtent; gy <= GridExtent; gy++)
		{
			float BaseX = FieldCenter.X + gx * SpacingCm;
			float BaseY = FieldCenter.Y + gy * SpacingCm;

			float DX = BaseX - FieldCenter.X;
			float DY = BaseY - FieldCenter.Y;
			float Dist = FMath::Sqrt(DX * DX + DY * DY);
			if (Dist > FieldRadius) continue;

			float JX = RNG.FRandRange(-Jitter, Jitter);
			float JY = RNG.FRandRange(-Jitter, Jitter);
			float X = BaseX + JX;
			float Y = BaseY + JY;

			// Line trace to ground
			FVector TraceStart(X, Y, FieldCenter.Z + 10000.0f);
			FVector TraceEnd(X, Y, FieldCenter.Z - 10000.0f);
			FHitResult Hit;
			FCollisionQueryParams QueryParams;
			QueryParams.bTraceComplex = false;
			QueryParams.AddIgnoredActor(this);

			if (!GetWorld()->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd,
				ECC_Visibility, QueryParams))
			{
				continue;
			}

			float Slope = FVector::DotProduct(Hit.Normal, FVector::UpVector);
			if (Slope < 0.95f) continue;

			FVector Loc = Hit.Location;

			// Random blade properties
			float Yaw = RNG.FRandRange(0.0f, 360.0f);
			float HeightVar = RNG.FRandRange(0.8f, 1.2f);
			float WidthVar = RNG.FRandRange(0.8f, 1.2f);
			float Curve = RNG.FRandRange(0.1f, 0.6f);

			AddBladeGeometry(
				SectionVerts, SectionTris, SectionNormals, SectionUVs, SectionColors,
				Loc, Yaw,
				BladeHeight * HeightVar,
				BladeWidth * WidthVar,
				Curve,
				RNG
			);

			BladesInSection++;
			TotalPlaced++;

			// Flush section if full
			if (BladesInSection >= MaxBladesPerSection)
			{
				// Create PMC and submit geometry
				FName MeshName = FName(*FString::Printf(TEXT("GrassMesh_%d"), SectionIndex));
				UProceduralMeshComponent* PMC = NewObject<UProceduralMeshComponent>(this, MeshName);
				PMC->SetupAttachment(RootComponent);
				PMC->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				PMC->SetVisibility(true);
				PMC->SetHiddenInGame(false);
				PMC->SetMobility(EComponentMobility::Movable);
				PMC->SetCastShadow(false);
				PMC->SetCastContactShadow(false);

				PMC->CreateMeshSection_LinearColor(
					0,
					SectionVerts,
					SectionTris,
					SectionNormals,
					SectionUVs,
					SectionColors,
					TArray<FProcMeshTangent>(),
					false
				);

				if (GrassMaterial)
				{
					PMC->SetMaterial(0, GrassMaterial);
				}

				PMC->RegisterComponent();
				GrassMeshComponents.Add(PMC);

				UE_LOG(LogTemp, Log, TEXT("GrassField: Section %d — %d blades, %d verts"),
					SectionIndex, BladesInSection, SectionVerts.Num());

				// Reset for next section
				SectionVerts.Empty();
				SectionTris.Empty();
				SectionNormals.Empty();
				SectionUVs.Empty();
				SectionColors.Empty();
				BladesInSection = 0;
				SectionIndex++;
			}
		}
	}

	// Flush remaining blades
	if (BladesInSection > 0)
	{
		FName MeshName = FName(*FString::Printf(TEXT("GrassMesh_%d"), SectionIndex));
		UProceduralMeshComponent* PMC = NewObject<UProceduralMeshComponent>(this, MeshName);
		PMC->SetupAttachment(RootComponent);
		PMC->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		PMC->SetVisibility(true);
		PMC->SetHiddenInGame(false);
		PMC->SetMobility(EComponentMobility::Movable);
		PMC->SetCastShadow(false);
		PMC->SetCastContactShadow(false);

		PMC->CreateMeshSection_LinearColor(
			0, SectionVerts, SectionTris, SectionNormals,
			SectionUVs, SectionColors, TArray<FProcMeshTangent>(), false);

		if (GrassMaterial)
		{
			PMC->SetMaterial(0, GrassMaterial);
		}

		PMC->RegisterComponent();
		GrassMeshComponents.Add(PMC);

		UE_LOG(LogTemp, Log, TEXT("GrassField: Section %d — %d blades, %d verts"),
			SectionIndex, BladesInSection, SectionVerts.Num());
	}

	UE_LOG(LogTemp, Warning,
		TEXT("GrassField: Done — %d real 3D grass blades in %d sections at (%.0f,%.0f) radius %.0fm"),
		TotalPlaced, GrassMeshComponents.Num(), FieldCenter.X, FieldCenter.Y, FieldRadius / 100.0f);
}
