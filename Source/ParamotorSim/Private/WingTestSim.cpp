// ============================================================
// WingTestSim.cpp — Real MacPara Charger 25M cloth/line/riser simulation
// ============================================================
// Uses ACTUAL mesh data from charger_full.blend:
//   - MacParaChargerData.h: 6024 canopy verts, 13308 tris, 8 materials
//   - MacParaChargerLines.h: 128 real lines with Top/Bottom endpoints
//   - SM_Risers_Both: real riser mesh (imported from FBX)
//
// PERFORMANCE: Mesh sections created ONCE, only positions updated per frame.
// All mesh data stored as INSTANCE members (not static globals).
// ============================================================

#include "WingActor.h"
#include "MacParaChargerData.h"
#include "MacParaChargerLines.h"
#include "ProceduralMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

// ============================================================
// CLOTH INIT — from real MacPara Charger mesh data
// ============================================================

void AWingActor::InitTestWingCloth()
{
	Particles.Empty();
	Constraints.Empty();
	TargetPositions.Empty();

	FVector BBoxCenter = (MPC_BBoxMin + MPC_BBoxMax) * 0.5f;
	float MToCm = 100.0f;

	for (int32 i = 0; i < MPC_VertCount; i++)
	{
		FClothParticle P;
		P.Pos = (MPC_Vertices[i] - BBoxCenter) * MToCm;
		P.OldPos = P.Pos;
		P.bPinned = false;
		P.bGrabbed = false;
		Particles.Add(P);
	}

	TargetPositions.SetNumZeroed(Particles.Num());
	for (int32 i = 0; i < Particles.Num() && i < TargetPositions.Num(); i++)
		TargetPositions[i] = Particles[i].Pos;

	for (int32 i = 0; i < MPC_EdgeCount; i++)
	{
		int32 A = MPC_Edges[i * 2];
		int32 B = MPC_Edges[i * 2 + 1];
		if (A < Particles.Num() && B < Particles.Num())
		{
			FClothConstraint C;
			C.A = A;
			C.B = B;
			C.RestLength = (Particles[A].Pos - Particles[B].Pos).Size();
			C.Stiffness = 0.95f;
			Constraints.Add(C);
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("WingActor: InitTestWingCloth — %d particles, %d constraints"),
		Particles.Num(), Constraints.Num());
}

// ============================================================
// LINE INIT — from real MacPara Charger line endpoints
// ============================================================

void AWingActor::InitTestWingLines()
{
	TestLineParticles.Empty();
	TestLineSegments.Empty();
	TestLineRestLengths.Empty();
	TestLineColors.Empty();

	// Create 2 Verlet particles per line: Top (canopy) + Bottom (riser)
	for (int32 i = 0; i < MPCL_LineCount; i++)
	{
		const auto& Info = MPCL_LineInfos[i];

		int32 IdxTop = TestLineParticles.Num();
		FTestLineParticle PTop;
		PTop.Pos = Info.Top;
		PTop.OldPos = Info.Top;
		PTop.bPinned = false;
		TestLineParticles.Add(PTop);

		int32 IdxBot = TestLineParticles.Num();
		FTestLineParticle PBot;
		PBot.Pos = Info.Bottom;
		PBot.OldPos = Info.Bottom;
		PBot.bPinned = false;
		TestLineParticles.Add(PBot);

		TestLineSegments.Add(IdxTop);
		TestLineSegments.Add(IdxBot);
		TestLineRestLengths.Add(Info.Length);
		TestLineColors.Add(Info.Color);
	}

	// Pre-compute line-to-canopy attachments (which canopy particle each line top follows)
	TWL_TopCanopyIdx.Empty();
	TWL_IsMain.Empty();
	TWL_IsLeft.Empty();
	for (int32 li = 0; li < MPCL_LineCount; li++)
	{
		const auto& Info = MPCL_LineInfos[li];
		FVector TopPos = Info.Top;

		int32 NearestCanopy = -1;
		float NearestDist = 1e18f;
		for (int32 pi = 0; pi < Particles.Num(); pi++)
		{
			float D = FVector::DistSquared(Particles[pi].Pos, TopPos);
			if (D < NearestDist)
			{
				NearestDist = D;
				NearestCanopy = pi;
			}
		}
		TWL_TopCanopyIdx.Add(NearestCanopy);
		TWL_IsMain.Add(Info.Cascade && Info.Cascade[0] == 'M');
		TWL_IsLeft.Add(Info.Side && Info.Side[0] == 'L');
	}

	UE_LOG(LogTemp, Warning, TEXT("WingActor: InitTestWingLines — %d line particles, %d segments, %d attachments"),
		TestLineParticles.Num(), TestLineSegments.Num() / 2, TWL_TopCanopyIdx.Num());
}

// ============================================================
// MESH CREATION — create procedural mesh sections ONCE
// ============================================================

void AWingActor::InitTestWingMeshes()
{
	if (!CanopyMesh || Particles.Num() == 0) return;

	bTestWingCanopyCreated = false;
	bTestWingLinesCreated = false;
	CanopyMesh->ClearAllMeshSections();
	if (LineMesh) LineMesh->ClearAllMeshSections();

	InitCanopyMeshSection();
	InitLineMeshSection();
	InitRiserMeshComponent();
}

void AWingActor::InitCanopyMeshSection()
{
	// One mesh section per material
	TWC_Verts.SetNum(Particles.Num());
	for (int32 i = 0; i < Particles.Num(); i++)
		TWC_Verts[i] = Particles[i].Pos;

	TWC_UVs.SetNum(Particles.Num());
	for (int32 i = 0; i < Particles.Num() && i < MPC_VertCount; i++)
		TWC_UVs[i] = MPC_UVs[i];

	TWC_Normals.SetNum(Particles.Num());
	for (int32 i = 0; i < Particles.Num(); i++)
		TWC_Normals[i] = FVector(0, 0, 1);
	TWC_Tangents.SetNum(Particles.Num());

	TWC_MatTris.SetNum(MPC_MaterialCount);
	TWC_MatColors.SetNum(MPC_MaterialCount);
	for (int32 MatIdx = 0; MatIdx < MPC_MaterialCount; MatIdx++)
	{
		TWC_MatTris[MatIdx].Empty();
		for (int32 i = 0; i < MPC_TriCount; i++)
		{
			if (MPC_Triangles[i * 4 + 3] == MatIdx)
			{
				TWC_MatTris[MatIdx].Add(MPC_Triangles[i * 4]);
				TWC_MatTris[MatIdx].Add(MPC_Triangles[i * 4 + 1]);
				TWC_MatTris[MatIdx].Add(MPC_Triangles[i * 4 + 2]);
			}
		}
		TWC_MatColors[MatIdx] = MPC_MaterialColors[MatIdx];
	}

	for (int32 MatIdx = 0; MatIdx < MPC_MaterialCount; MatIdx++)
	{
		if (TWC_MatTris[MatIdx].Num() == 0) continue;
		TArray<FLinearColor> UniformColors;
		UniformColors.Init(TWC_MatColors[MatIdx], TWC_Verts.Num());
		CanopyMesh->CreateMeshSection_LinearColor(MatIdx, TWC_Verts, TWC_MatTris[MatIdx],
			TWC_Normals, TWC_UVs, UniformColors, TWC_Tangents, true);
		CanopyMesh->SetMeshSectionVisible(MatIdx, true);
		if (CanopyMaterial)
			CanopyMesh->SetMaterial(MatIdx, CanopyMaterial);
	}
	bTestWingCanopyCreated = true;
}

void AWingActor::InitLineMeshSection()
{
	if (!LineMesh) return;

	TArray<FVector> LineVerts;
	TWL_Tris.Empty();
	TWL_Colors.Empty();

	const float MainLineRadius = 0.15f;
	const float UpperLineRadius = 0.08f;
	const float BrakeLineRadius = 0.06f;
	const int32 TubeSides = 8;

	for (int32 li = 0; li < MPCL_LineCount; li++)
	{
		const auto& Info = MPCL_LineInfos[li];

		float LineRadius = MainLineRadius;
		if (Info.Cascade && Info.Cascade[0] == 'U')
			LineRadius = UpperLineRadius;
		else if (Info.Level && Info.Level[0] == 'B' && Info.Level[1] == 'r')
			LineRadius = BrakeLineRadius;

		FVector PA = Info.Top;
		FVector PB = Info.Bottom;
		FVector Dir = PB - PA;
		float Len = Dir.Size();
		if (Len < 0.1f) continue;
		Dir /= Len;

		FVector Up = FMath::Abs(Dir.Z) > 0.99f ? FVector(1, 0, 0) : FVector(0, 0, 1);
		FVector Right = FVector::CrossProduct(Dir, Up).GetSafeNormal();
		FVector RealUp = FVector::CrossProduct(Right, Dir).GetSafeNormal();

		int32 BaseVert = LineVerts.Num();

		// Top ring (canopy end)
		for (int32 i = 0; i < TubeSides; i++)
		{
			float Angle = (float)i / TubeSides * PI * 2.0f;
			FVector Offset = (Right * FMath::Cos(Angle) + RealUp * FMath::Sin(Angle)) * LineRadius;
			LineVerts.Add(PA + Offset);
			TWL_Colors.Add(Info.Color);
		}
		// Bottom ring (riser end)
		for (int32 i = 0; i < TubeSides; i++)
		{
			float Angle = (float)i / TubeSides * PI * 2.0f;
			FVector Offset = (Right * FMath::Cos(Angle) + RealUp * FMath::Sin(Angle)) * LineRadius;
			LineVerts.Add(PB + Offset);
			TWL_Colors.Add(Info.Color);
		}

		for (int32 i = 0; i < TubeSides; i++)
		{
			int32 NextI = (i + 1) % TubeSides;
			int32 V0 = BaseVert + i;
			int32 V1 = BaseVert + NextI;
			int32 V2 = BaseVert + TubeSides + i;
			int32 V3 = BaseVert + TubeSides + NextI;
			TWL_Tris.Add(V0); TWL_Tris.Add(V2); TWL_Tris.Add(V1);
			TWL_Tris.Add(V1); TWL_Tris.Add(V2); TWL_Tris.Add(V3);
		}
	}

	TestWingLineMeshVertCount = LineVerts.Num();
	TWL_Tangents.SetNum(TestWingLineMeshVertCount);
	TWL_UVs.SetNumZeroed(TestWingLineMeshVertCount);
	TArray<FVector> LineNormals;
	LineNormals.SetNum(TestWingLineMeshVertCount);
	for (int32 i = 0; i < TestWingLineMeshVertCount; i++)
		LineNormals[i] = FVector(0, 0, 1);

	LineMesh->CreateMeshSection_LinearColor(0, LineVerts, TWL_Tris, LineNormals,
		TWL_UVs, TWL_Colors, TWL_Tangents, false);
	LineMesh->SetMeshSectionVisible(0, true);
	if (LineMaterial)
	{
		LineMesh->SetMaterial(0, LineMaterial);
	}
	bTestWingLinesCreated = true;

	UE_LOG(LogTemp, Warning, TEXT("WingActor: InitTestWingMeshes — canopy %d verts (%d sections), lines %d verts"),
		TWC_Verts.Num(), MPC_MaterialCount, TestWingLineMeshVertCount);
}

void AWingActor::InitRiserMeshComponent()
{
	if (!RiserMeshComponent)
	{
		RiserMeshComponent = NewObject<UStaticMeshComponent>(this, TEXT("RiserMeshComponent"));
		RiserMeshComponent->SetupAttachment(Root);
		RiserMeshComponent->SetMobility(EComponentMobility::Movable);
		RiserMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		RiserMeshComponent->SetCastShadow(true);
		RiserMeshComponent->RegisterComponent();
	}

	UStaticMesh* RiserMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Meshes/SM_Risers_Both.SM_Risers_Both"));
	if (RiserMesh)
	{
		RiserMeshComponent->SetStaticMesh(RiserMesh);
		RiserMeshComponent->SetVisibility(true);

		// Cache the actual mesh bounds — center and extent.
		FBoxSphereBounds Bounds = RiserMesh->GetBounds();
		RiserMeshBoundsCenter = Bounds.Origin;
		RiserMeshBoundsExtent = Bounds.BoxExtent;
		bRiserBoundsCached = true;

		// FBX exported with bake_space_transform=True — vertices in Blender world space (cm).
		// Offset places mesh origin relative to wing center.
		FVector RiserOffset(40.7f, 0.0f, -696.8f);
		RiserMeshComponent->SetRelativeLocation(RiserOffset);
		RiserMeshComponent->SetRelativeRotation(FRotator(0.0f, 0.0f, 0.0f));

		UE_LOG(LogTemp, Warning, TEXT("WingActor: Loaded real riser mesh (%d verts) at offset %s, bounds center=%s, box extent=%s"),
			RiserMesh->GetNumVertices(0), *RiserOffset.ToString(),
			*Bounds.Origin.ToString(), *Bounds.BoxExtent.ToString());
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("WingActor: SM_Risers_Both not found at /Game/Meshes/SM_Risers_Both"));
	}
}

// ============================================================
// CLOTH SIMULATION — Verlet on real mesh
// ============================================================

void AWingActor::SimulateTestWingCloth(float DeltaTime)
{
	if (Particles.Num() == 0) return;

	const float ClothGravity = 981.0f;
	const float ClothDamp = 0.98f;
	const float GroundZ = GetCachedGroundZ(GetActorLocation());
	const float WindMs = GetCurrentWindSpeedMs();
	FVector WindDir = GetShiftedWindDirection();
	FVector ActorLoc = GetActorLocation();
	float WindForceCm = WindMs * 100.0f * 2.0f;
	// Vertical turbulence adds realistic cloth flutter in gusts
	float VertTurbCm = TurbVertState * 100.0f;  // m/s to cm/s

	float TargetInflation = (WingState == EWingState::Inflated || WingState == EWingState::Flying) ? 1.0f :
		(WingState == EWingState::Inflating) ? 1.0f :
		(WingState == EWingState::Spread || WingState == EWingState::Connected) ? 0.3f : 0.1f;
	TestWingInflation = FMath::Lerp(TestWingInflation, TargetInflation, DeltaTime * 2.0f);

	const int32 VertsPerRib = 128;
	const int32 TopCount = 64;
	float DeflateSeparation = (1.0f - TestWingInflation) * 5.0f;

	for (int32 i = 0; i < Particles.Num(); i++)
	{
		if (Particles[i].bPinned || Particles[i].bGrabbed) continue;

		// NaN safety
		if (FMath::IsNaN(Particles[i].Pos.X) || FMath::IsNaN(Particles[i].Pos.Z))
		{
			Particles[i].Pos = TargetPositions[i];
			Particles[i].OldPos = TargetPositions[i];
			continue;
		}

		FVector Vel = (Particles[i].Pos - Particles[i].OldPos) * ClothDamp;
		FVector Accel = FVector(0, 0, -ClothGravity);

		if (TestWingInflation > 0.1f)
		{
			Accel += WindDir * WindForceCm * TestWingInflation;
			// Vertical turbulence causes cloth to flutter in gusts
			Accel.Z += VertTurbCm * TestWingInflation * 0.5f;
		}

		if (TestWingInflation > 0.01f && i < TargetPositions.Num())
		{
			FVector ToTarget = TargetPositions[i] - Particles[i].Pos;
			Accel += ToTarget * TestWingInflation * 10.0f;
		}

		if (TestWingInflation < 0.5f)
		{
			float DeflateAmount = 1.0f - TestWingInflation;
			float HeightAboveGround = Particles[i].Pos.Z + ActorLoc.Z - GroundZ;
			if (HeightAboveGround > 50.0f)
				Accel.Z -= HeightAboveGround * DeflateAmount * 5.0f;
		}

		if (bRisersConnected && i < TargetPositions.Num())
		{
			FVector CarabinerMid = (LeftCarabinerPos + RightCarabinerPos) * 0.5f;
			FVector ToCarabiner = CarabinerMid - (Particles[i].Pos + ActorLoc);
			float DistToCarabiner = ToCarabiner.Size();
			if (DistToCarabiner > 700.0f)
				Accel += ToCarabiner.GetSafeNormal() * (DistToCarabiner - 700.0f) * 2.0f;
		}

		Particles[i].OldPos = Particles[i].Pos;
		Particles[i].Pos += Vel + Accel * DeltaTime * DeltaTime;
	}

	int32 Iterations = 4;
	for (int32 iter = 0; iter < Iterations; iter++)
	{
		for (int32 i = 0; i < Constraints.Num(); i++)
		{
			int32 A = Constraints[i].A;
			int32 B = Constraints[i].B;
			if (A >= Particles.Num() || B >= Particles.Num()) continue;
			if (Particles[A].bPinned && Particles[B].bPinned) continue;

			FVector Delta = Particles[B].Pos - Particles[A].Pos;
			float Dist = Delta.Size();
			if (Dist < 0.001f) continue;

			float Diff = (Dist - Constraints[i].RestLength) / Dist;
			float Stiffness = Constraints[i].Stiffness * 0.5f;

			if (Particles[A].bPinned)
				Particles[B].Pos -= Delta * Diff * Stiffness * 2.0f;
			else if (Particles[B].bPinned)
				Particles[A].Pos += Delta * Diff * Stiffness * 2.0f;
			else
			{
				Particles[A].Pos += Delta * Diff * Stiffness;
				Particles[B].Pos -= Delta * Diff * Stiffness;
			}
		}
	}

	for (int32 i = 0; i < Particles.Num(); i++)
	{
		if (Particles[i].bPinned) continue;
		float GroundZLocal = GroundZ - ActorLoc.Z;
		int32 RibVertIdx = i % VertsPerRib;
		float SurfaceOffset = (RibVertIdx >= TopCount) ? -DeflateSeparation : 0.0f;
		float MinZ = GroundZLocal + 2.0f + SurfaceOffset;
		if (Particles[i].Pos.Z < MinZ)
		{
			Particles[i].Pos.Z = MinZ;
			FVector Vel = Particles[i].Pos - Particles[i].OldPos;
			Vel.X *= 0.8f;
			Vel.Y *= 0.8f;
			Particles[i].OldPos = Particles[i].Pos - Vel;
		}
	}
}

// ============================================================
// LINE SIMULATION — Verlet with carabiner attachment
// ============================================================

void AWingActor::SimulateTestWingLines(float DeltaTime)
{
	if (TestLineParticles.Num() == 0) return;

	const float LineGravity = 981.0f;
	const float LineDamp = 0.95f;
	const float GroundZ = GetCachedGroundZ(GetActorLocation());
	FVector ActorLoc = GetActorLocation();

	if (bRisersConnected)
	{
		FVector LeftCarabinerLocal = LeftCarabinerPos - ActorLoc;
		FVector RightCarabinerLocal = RightCarabinerPos - ActorLoc;

		// Lines connect to the BOTTOM of the riser, not the carabiner.
		// The riser is ~52cm of webbing that hangs down from the carabiner.
		// The line bottoms attach to the bottom of the riser webbing.
		const float RiserLengthCm = 52.0f;
		FVector LeftRiserBottomLocal = LeftCarabinerLocal + FVector(0, 0, -RiserLengthCm);
		FVector RightRiserBottomLocal = RightCarabinerLocal + FVector(0, 0, -RiserLengthCm);

		for (int32 li = 0; li < MPCL_LineCount; li++)
		{
			int32 TopIdx = li * 2;
			int32 BotIdx = li * 2 + 1;
			if (TopIdx >= TestLineParticles.Num() || BotIdx >= TestLineParticles.Num()) continue;
			if (li >= TWL_IsMain.Num()) continue;

			// Pin top to canopy particle
			int32 CanopyIdx = TWL_TopCanopyIdx[li];
			if (CanopyIdx >= 0 && CanopyIdx < Particles.Num())
			{
				TestLineParticles[TopIdx].Pos = Particles[CanopyIdx].Pos;
				TestLineParticles[TopIdx].OldPos = Particles[CanopyIdx].Pos;
				TestLineParticles[TopIdx].bPinned = true;
			}

			// Pin ALL line bottoms to the BOTTOM of the appropriate riser.
			// In real paragliding, lines connect to the riser webbing (~52cm below carabiner),
			// not directly to the carabiner. The riser hangs from the carabiner at its top.
			if (li < TWL_IsLeft.Num())
			{
				FVector RiserBottomLocal = TWL_IsLeft[li] ? LeftRiserBottomLocal : RightRiserBottomLocal;
				TestLineParticles[BotIdx].Pos = RiserBottomLocal;
				TestLineParticles[BotIdx].OldPos = RiserBottomLocal;
				TestLineParticles[BotIdx].bPinned = true;
			}
		}
	}
	else
	{
		for (int32 i = 0; i < TestLineParticles.Num(); i++)
			TestLineParticles[i].bPinned = false;
	}

	for (int32 i = 0; i < TestLineParticles.Num(); i++)
	{
		if (TestLineParticles[i].bPinned) continue;

		// NaN safety — if a particle has gone bad, reset it to a safe position
		if (FMath::IsNaN(TestLineParticles[i].Pos.X) || FMath::IsNaN(TestLineParticles[i].Pos.Y) || FMath::IsNaN(TestLineParticles[i].Pos.Z))
		{
			TestLineParticles[i].Pos = FVector(0, 0, 0);
			TestLineParticles[i].OldPos = FVector(0, 0, 0);
			continue;
		}

		FVector Vel = (TestLineParticles[i].Pos - TestLineParticles[i].OldPos) * LineDamp;
		FVector Accel = FVector(0, 0, -LineGravity);
		TestLineParticles[i].OldPos = TestLineParticles[i].Pos;
		TestLineParticles[i].Pos += Vel + Accel * DeltaTime * DeltaTime;
	}

	// Constraint solving — ONLY when connected (lines are taut).
	// When not connected (deflated/on ground), lines are loose and piled up.
	// Applying distance constraints would slowly stretch them to their inflated
	// rest lengths (430-490cm), spreading them flat across the ground and making
	// them invisible. Real deflated lines don't maintain any particular length.
	if (bRisersConnected)
	{
		int32 Iterations = 5;
		float LineStiffness = 0.9f;
		for (int32 iter = 0; iter < Iterations; iter++)
		{
			for (int32 s = 0; s < TestLineSegments.Num() / 2; s++)
			{
				int32 A = TestLineSegments[s * 2];
				int32 B = TestLineSegments[s * 2 + 1];
				if (A >= TestLineParticles.Num() || B >= TestLineParticles.Num()) continue;

				FVector Delta = TestLineParticles[B].Pos - TestLineParticles[A].Pos;
				float Dist = Delta.Size();
				if (Dist < 0.001f) continue;

				float RestLen = TestLineRestLengths[s];
				float Diff = (Dist - RestLen) / Dist;

				if (TestLineParticles[A].bPinned && TestLineParticles[B].bPinned) continue;
				if (TestLineParticles[A].bPinned)
					TestLineParticles[B].Pos -= Delta * Diff * LineStiffness * 2.0f;
				else if (TestLineParticles[B].bPinned)
					TestLineParticles[A].Pos += Delta * Diff * LineStiffness * 2.0f;
				else
				{
					TestLineParticles[A].Pos += Delta * Diff * LineStiffness;
					TestLineParticles[B].Pos -= Delta * Diff * LineStiffness;
				}
			}
		}
	}

	for (int32 i = 0; i < TestLineParticles.Num(); i++)
	{
		if (TestLineParticles[i].bPinned) continue;
		float GroundZLocal = GroundZ - ActorLoc.Z;
		// Keep line particles above ground with margin for tube radius (0.15cm max)
		float MinZ = GroundZLocal + 2.0f;
		if (TestLineParticles[i].Pos.Z < MinZ)
		{
			TestLineParticles[i].Pos.Z = MinZ;
			// Kill downward velocity
			FVector Vel = TestLineParticles[i].Pos - TestLineParticles[i].OldPos;
			if (Vel.Z < 0) Vel.Z = 0;
			TestLineParticles[i].OldPos = TestLineParticles[i].Pos - Vel;
		}
	}
}

// ============================================================
// CANOPY MESH UPDATE — positions only
// ============================================================

void AWingActor::UpdateTestWingCanopyMesh()
{
	if (!CanopyMesh || Particles.Num() == 0) return;
	if (!bTestWingCanopyCreated) { InitTestWingMeshes(); return; }

	int32 NumVerts = FMath::Min(Particles.Num(), TWC_Verts.Num());
	for (int32 i = 0; i < NumVerts; i++)
		TWC_Verts[i] = Particles[i].Pos;

	for (int32 MatIdx = 0; MatIdx < MPC_MaterialCount; MatIdx++)
	{
		if (MatIdx >= TWC_MatTris.Num() || TWC_MatTris[MatIdx].Num() == 0) continue;
		TArray<FLinearColor> UniformColors;
		UniformColors.Init(TWC_MatColors[MatIdx], TWC_Verts.Num());
		CanopyMesh->UpdateMeshSection_LinearColor(MatIdx, TWC_Verts, TWC_Normals,
			TWC_UVs, UniformColors, TWC_Tangents);
	}
}

// ============================================================
// LINE MESH UPDATE — positions only (tubes between Top/Bottom)
// ============================================================

void AWingActor::UpdateTestWingLineMesh()
{
	if (!LineMesh || !bTestWingLinesCreated || TestWingLineMeshVertCount == 0) return;

	TArray<FVector> LineVerts;
	LineVerts.SetNum(TestWingLineMeshVertCount);

	const float MainLineRadius = 0.15f;
	const float UpperLineRadius = 0.08f;
	const float BrakeLineRadius = 0.06f;
	const int32 TubeSides = 8;

	int32 VertOffset = 0;
	for (int32 li = 0; li < MPCL_LineCount; li++)
	{
		const auto& Info = MPCL_LineInfos[li];

		float LineRadius = MainLineRadius;
		if (Info.Cascade && Info.Cascade[0] == 'U')
			LineRadius = UpperLineRadius;
		else if (Info.Level && Info.Level[0] == 'B' && Info.Level[1] == 'r')
			LineRadius = BrakeLineRadius;

		// Get simulated endpoints
		int32 LineParticleIdx = li * 2;
		FVector PA, PB;
		if (LineParticleIdx + 1 < TestLineParticles.Num())
		{
			PA = TestLineParticles[LineParticleIdx].Pos;     // Top (canopy)
			PB = TestLineParticles[LineParticleIdx + 1].Pos; // Bottom (riser)
		}
		else
		{
			PA = Info.Top;
			PB = Info.Bottom;
		}

		// NaN safety — skip bad lines, use static data
		if (FMath::IsNaN(PA.X) || FMath::IsNaN(PA.Z) || FMath::IsNaN(PB.X) || FMath::IsNaN(PB.Z))
		{
			PA = Info.Top;
			PB = Info.Bottom;
		}

		FVector Dir = PB - PA;
		float Len = Dir.Size();
		if (Len < 0.01f) { Dir = FVector(0,0,1); Len = 0.01f; }
		Dir /= Len;

		FVector Up = FMath::Abs(Dir.Z) > 0.99f ? FVector(1, 0, 0) : FVector(0, 0, 1);
		FVector Right = FVector::CrossProduct(Dir, Up).GetSafeNormal();
		FVector RealUp = FVector::CrossProduct(Right, Dir).GetSafeNormal();

		// Top ring
		for (int32 i = 0; i < TubeSides; i++)
		{
			float Angle = (float)i / TubeSides * PI * 2.0f;
			FVector Offset = (Right * FMath::Cos(Angle) + RealUp * FMath::Sin(Angle)) * LineRadius;
			if (VertOffset < TestWingLineMeshVertCount)
				LineVerts[VertOffset] = PA + Offset;
			VertOffset++;
		}
		// Bottom ring
		for (int32 i = 0; i < TubeSides; i++)
		{
			float Angle = (float)i / TubeSides * PI * 2.0f;
			FVector Offset = (Right * FMath::Cos(Angle) + RealUp * FMath::Sin(Angle)) * LineRadius;
			if (VertOffset < TestWingLineMeshVertCount)
				LineVerts[VertOffset] = PB + Offset;
			VertOffset++;
		}
	}

	TArray<FVector> LineNormals;
	LineNormals.SetNum(TestWingLineMeshVertCount);
	for (int32 i = 0; i < TestWingLineMeshVertCount; i++)
		LineNormals[i] = FVector(0, 0, 1);

	LineMesh->UpdateMeshSection_LinearColor(0, LineVerts, LineNormals, TWL_UVs, TWL_Colors, TWL_Tangents);
}

// ============================================================
// STATE TRANSITIONS
// ============================================================

void AWingActor::SetTestWingDeflated()
{
	TestWingInflation = 0.0f;
	float GroundZLocal = GetCachedGroundZ(GetActorLocation()) - GetActorLocation().Z;
	for (int32 i = 0; i < Particles.Num(); i++)
	{
		if (Particles[i].bPinned) continue;
		Particles[i].Pos.Z = GroundZLocal + 2.0f + FMath::FRand() * 10.0f;
		Particles[i].OldPos = Particles[i].Pos;
	}

	// Also deflate the lines so they pile near the canopy
	SetTestWingLinesDeflated();
}

void AWingActor::SetTestWingLinesDeflated()
{
	float GroundZLocal = GetCachedGroundZ(GetActorLocation()) - GetActorLocation().Z;

	// When deflated, lines should pile up near the canopy on the ground.
	// Don't try to maintain inflated lengths — real deflated lines are loose and piled up.
	for (int32 li = 0; li < MPCL_LineCount; li++)
	{
		int32 TopIdx = li * 2;
		int32 BotIdx = li * 2 + 1;
		if (TopIdx >= TestLineParticles.Num() || BotIdx >= TestLineParticles.Num()) continue;

		// Top follows canopy particle (if attached)
		int32 CanopyIdx = (li < TWL_TopCanopyIdx.Num()) ? TWL_TopCanopyIdx[li] : -1;
		FVector TopPos;
		if (CanopyIdx >= 0 && CanopyIdx < Particles.Num())
			TopPos = Particles[CanopyIdx].Pos;
		else
			TopPos = MPCL_LineInfos[li].Top;

		// Bottom piles near the top with some random offset (lines are loose on ground)
		FVector BotPos = TopPos + FVector(
			FMath::FRand() * 40.0f - 20.0f,
			FMath::FRand() * 40.0f - 20.0f,
			-5.0f - FMath::FRand() * 15.0f
		);
		// Clamp to ground
		BotPos.Z = FMath::Max(BotPos.Z, GroundZLocal + 1.0f);

		TestLineParticles[TopIdx].Pos = TopPos;
		TestLineParticles[TopIdx].OldPos = TopPos;
		TestLineParticles[TopIdx].bPinned = false;

		TestLineParticles[BotIdx].Pos = BotPos;
		TestLineParticles[BotIdx].OldPos = BotPos;
		TestLineParticles[BotIdx].bPinned = false;
	}
}
