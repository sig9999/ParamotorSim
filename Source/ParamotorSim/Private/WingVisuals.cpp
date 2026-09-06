#include "WingActor.h"
#include "ParamotorActor.h"
#include "ParamotorCharacter.h"
#include "ParamotorGameInstance.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "ProceduralMeshComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "ParagliderWingMesh.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

// ============================================================
// WingVisuals.cpp — WingActor implementation split
// ============================================================

// ============================================================
// CANOPY MESH UPDATE
// ============================================================

void AWingActor::UpdateCanopyMesh(){
	if (Particles.Num() == 0) return;
TArray<FVector> Verts;
TArray<int32> Tris;
TArray<FVector> Normals;
TArray<FVector2D> UVs;
TArray<FLinearColor> Colors;
TArray<FProcMeshTangent> Tangents;
Verts.SetNum(PGWingVertCount);
Normals.SetNum(PGWingVertCount);
UVs.SetNum(PGWingVertCount);
Colors.SetNum(PGWingVertCount);
Tangents.SetNum(PGWingVertCount);
for (int32 i = 0; i < PGWingVertCount && i < Particles.Num(); i++)	{
		Verts[i] = Particles[i].Pos;
UVs[i] = PGWingUVs[i];
// === Compute vertex color - MacPara Charger 1 blue/white design ===
// Real Charger 1 color scheme (most iconic "Blue" design):
//   Top surface: Blue leading edge band (first ~15% chord), white rest
//   Bottom surface: White with blue leading edge
//   Wingtips: Blue (last ~12% of span each side)
//   MacPara logo: "MACPARA" text on top surface at ~30% chord, center span
//   "CHARGER 25" text on top surface below logo
//   Blue is a medium-dark blue (MacPara signature blue)

int32 RibIdx = i / (2 * PGPointsPerRib);
int32 LocalIdx = i % (2 * PGPointsPerRib);
int32 ChordIdx = LocalIdx % PGPointsPerRib;
float ChordT = (float)ChordIdx / (float)(PGPointsPerRib - 1);
float SpanT = (float)RibIdx / (float)(PGNumRibs - 1);
float TipDist = FMath::Min(SpanT, 1.0f - SpanT) * 2.0f;
bool bIsTopSurface = (LocalIdx < PGPointsPerRib);

// MacPara Charger colors
FLinearColor MacParaBlue(0.12f, 0.38f, 0.78f, 1.0f);   // MacPara signature blue
FLinearColor MacParaWhite(0.93f, 0.93f, 0.93f, 1.0f);  // Off-white canopy
FLinearColor MacParaDark(0.08f, 0.08f, 0.08f, 1.0f);   // Dark text/logo
FLinearColor MacParaLightBlue(0.20f, 0.55f, 0.90f, 1.0f); // Lighter blue accent

FLinearColor VertColor;

// Leading edge band: blue (first 15% of chord, both top and bottom surface)
if (ChordT < 0.15f)
{
    float LEBandWeight = 1.0f - (ChordT / 0.15f);
    VertColor = FMath::Lerp(MacParaWhite, MacParaBlue, FMath::SmoothStep(0.0f, 1.0f, LEBandWeight));
}
// Wingtips: blue (last 12% of span each side)
else if (TipDist < 0.12f)
{
    float TipWeight = 1.0f - (TipDist / 0.12f);
    VertColor = FMath::Lerp(MacParaWhite, MacParaBlue, FMath::SmoothStep(0.0f, 1.0f, TipWeight));
}
// Main body: white
else
{
    VertColor = MacParaWhite;
}

// === MACPARA LOGO on top surface ===
// "MACPARA" text at ~30% chord, centered on span
// 5x5 bitmap font, 7 letters, scaled to mesh resolution
if (bIsTopSurface)
{
    // 5x5 bitmap font for uppercase letters
    static const uint8 LogoFont[7][5][5] = {
        {{1,1,1,1,1},{1,0,0,0,1},{1,0,1,0,1},{1,0,0,0,1},{1,0,0,0,1}}, // M
        {{0,1,1,1,0},{1,0,0,0,1},{1,1,1,1,1},{1,0,0,0,1},{1,0,0,0,1}}, // A
        {{1,1,1,1,0},{1,0,0,0,0},{1,0,0,0,0},{1,0,0,0,0},{1,1,1,1,0}}, // C
        {{1,1,1,1,0},{1,0,0,0,1},{1,1,1,1,0},{1,0,0,0,0},{1,0,0,0,0}}, // P
        {{0,1,1,1,0},{1,0,0,0,1},{1,1,1,1,1},{1,0,0,0,1},{1,0,0,0,1}}, // A
        {{1,1,1,1,0},{1,0,0,0,1},{1,1,1,1,0},{1,0,0,1,0},{1,0,0,0,1}}, // R
        {{0,1,1,1,0},{1,0,0,0,1},{1,1,1,1,1},{1,0,0,0,1},{1,0,0,0,1}}, // A
    };

    // Scale logo to mesh: each font pixel = 2 ribs wide, 2 chord points tall
    int32 PixelScale = 2;
    int32 LetterWidth = 5 * PixelScale;   // 10 ribs per letter
    int32 LetterHeight = 5 * PixelScale;  // 10 chord points per letter
    int32 LetterSpacing = 2 * PixelScale; // 4 ribs between letters
    int32 TotalLogoWidth = 7 * LetterWidth + 6 * LetterSpacing; // 7*10+6*4 = 94
    int32 LogoStartRib = (PGNumRibs - TotalLogoWidth) / 2;  // Centered
    int32 LogoStartChord = (int32)(PGPointsPerRib * 0.28);  // ~28% chord

    for (int32 charIdx = 0; charIdx < 7; charIdx++)
    {
        int32 charStartRib = LogoStartRib + charIdx * (LetterWidth + LetterSpacing);
        if (RibIdx >= charStartRib && RibIdx < charStartRib + LetterWidth &&
            ChordIdx >= LogoStartChord && ChordIdx < LogoStartChord + LetterHeight)
        {
            int32 col = (RibIdx - charStartRib) / PixelScale;
            int32 row = (ChordIdx - LogoStartChord) / PixelScale;
            if (col < 5 && row < 5 && LogoFont[charIdx][row][col])
            {
                VertColor = MacParaDark;
                break;
            }
        }
    }

    // "CHARGER 25" text below logo (smaller, 3x3 font, at ~45% chord)
    static const uint8 SmallFont[10][3][3] = {
        {{1,1,1},{1,0,1},{1,1,1}}, // C
        {{1,0,1},{1,1,1},{1,0,1}}, // H
        {{1,1,0},{1,0,0},{1,1,0}}, // A
        {{1,1,1},{1,1,0},{1,1,1}}, // R
        {{1,1,1},{1,0,0},{1,1,1}}, // G
        {{1,1,1},{1,0,0},{1,0,0}}, // E
        {{1,1,1},{1,0,0},{1,1,1}}, // R
        {{0,0,0},{0,0,0},{0,0,0}}, // space
        {{1,0,1},{1,0,1},{1,1,1}}, // 2
        {{1,1,1},{1,0,0},{1,1,1}}, // 5
    };

    int32 SmallPixelScale = 2;
    int32 SmallWidth = 3 * SmallPixelScale;   // 6 ribs per letter
    int32 SmallHeight = 3 * SmallPixelScale;  // 6 chord points per letter
    int32 SmallSpacing = 2 * SmallPixelScale;  // 4 ribs between letters
    int32 SmallTotalWidth = 10 * SmallWidth + 9 * SmallSpacing;
    int32 SmallStartRib = (PGNumRibs - SmallTotalWidth) / 2;
    int32 SmallStartChord = (int32)(PGPointsPerRib * 0.42);  // ~42% chord

    for (int32 charIdx = 0; charIdx < 10; charIdx++)
    {
        int32 charStartRib = SmallStartRib + charIdx * (SmallWidth + SmallSpacing);
        if (RibIdx >= charStartRib && RibIdx < charStartRib + SmallWidth &&
            ChordIdx >= SmallStartChord && ChordIdx < SmallStartChord + SmallHeight)
        {
            int32 col = (RibIdx - charStartRib) / SmallPixelScale;
            int32 row = (ChordIdx - SmallStartChord) / SmallPixelScale;
            if (col < 3 && row < 3 && SmallFont[charIdx][row][col])
            {
                VertColor = FLinearColor(0.15f, 0.15f, 0.15f, 1.0f);
                break;
            }
        }
    }
}

// Slight shading: top surface slightly brighter, bottom slightly darker
if (bIsTopSurface)
    VertColor = VertColor * 1.05f;
else
    VertColor = VertColor * 0.88f;

Colors[i] = VertColor;
}
	// === Compute smooth normals from triangle faces ===
// This fixes the flat/broken lighting artifacts when the canopy deforms

for (int32 i = 0; i < Normals.Num(); i++)		Normals[i] = FVector::ZeroVector;
// Accumulate face normals per vertex
for (int32 t = 0; t < PGWingTriCount; t++)	{
		int32 i0 = PGWingTriangles[t * 3];
int32 i1 = PGWingTriangles[t * 3 + 1];
int32 i2 = PGWingTriangles[t * 3 + 2];
if (i0 >= Verts.Num() || i1 >= Verts.Num() || i2 >= Verts.Num()) continue;
FVector Edge1 = Verts[i1] - Verts[i0];
FVector Edge2 = Verts[i2] - Verts[i0];
FVector FaceNormal = FVector::CrossProduct(Edge1, Edge2);
// Don't normalize per-face — weight by area for smoother results
		Normals[i0] += FaceNormal;
Normals[i1] += FaceNormal;
Normals[i2] += FaceNormal;
}
	// Normalize accumulated normals
for (int32 i = 0; i < Normals.Num(); i++)	{
		if (Normals[i].SizeSquared() > KINDA_SMALL_NUMBER)			Normals[i].Normalize();
else			Normals[i] = FVector(0, 0, 1);
}
	Tris.SetNum(PGWingTriCount * 3);
for (int32 i = 0; i < PGWingTriCount * 3; i++)		Tris[i] = PGWingTriangles[i];
CanopyMesh->CreateMeshSection_LinearColor(0, Verts, Tris, Normals, UVs, Colors, Tangents, false);
CanopyMesh->SetMeshSectionVisible(0, true);
if (CanopyMaterial)		CanopyMesh->SetMaterial(0, CanopyMaterial);
}

// ============================================================
// LINE MESH UPDATE
// ============================================================

void AWingActor::UpdateLineMesh(){
	if (LineParticles.Num() == 0)	{
		LineMesh->ClearAllMeshSections();
return;
}
	TArray<FVector> Verts;
TArray<int32> Tris;
TArray<FVector> Normals;
TArray<FVector2D> UVs;
TArray<FLinearColor> Colors;
TArray<FProcMeshTangent> Tangents;
auto AddCylinder = [&](FVector A, FVector B, float Radius, FLinearColor Color)	{
		int32 Segments = 8;
FVector Dir = B - A;
float Len = Dir.Size();
if (Len < 0.1f) return;
Dir /= Len;
// Find perpendicular vectors
FVector Up;
if (FMath::Abs(Dir.Z) < 0.99f)			Up = FVector(0, 0, 1);
else
	Up = FVector(1, 0, 0);
FVector Right = FVector::CrossProduct(Dir, Up).GetSafeNormal();
Up = FVector::CrossProduct(Right, Dir).GetSafeNormal();
int32 BaseVert = Verts.Num();
// Two rings of vertices
for (int32 r = 0; r < 2; r++)		{
			FVector Center = (r == 0) ? A : B;
for (int32 s = 0; s < Segments; s++)			{
				float Angle = 2.0f * PI * s / Segments;
FVector Offset = Right * FMath::Cos(Angle) * Radius + Up * FMath::Sin(Angle) * Radius;
Verts.Add(Center + Offset);
Normals.Add(Offset.GetSafeNormal());
UVs.Add(FVector2D((float)s / Segments, (float)r));
Colors.Add(Color);
Tangents.Add(FProcMeshTangent(Right, false));
}
}
		// Triangles
for (int32 s = 0; s < Segments; s++)		{
			int32 NextS = (s + 1) % Segments;
int32 V0 = BaseVert + s;
int32 V1 = BaseVert + NextS;
int32 V2 = BaseVert + Segments + NextS;
int32 V3 = BaseVert + Segments + s;
Tris.Add(V0);
Tris.Add(V1);
Tris.Add(V2);
Tris.Add(V0);
Tris.Add(V2);
Tris.Add(V3);
}
};
auto AddSphere = [&](FVector Center, float Radius, FLinearColor Color)	{
		int32 Segments = 8;
int32 Rings = 4;
int32 BaseVert = Verts.Num();
for (int32 r = 0; r <= Rings; r++)		{
			float Phi = PI * r / Rings;
for (int32 s = 0; s < Segments; s++)			{
				float Theta = 2.0f * PI * s / Segments;
FVector Pos = FVector(					Radius * FMath::Sin(Phi) * FMath::Cos(Theta),					Radius * FMath::Sin(Phi) * FMath::Sin(Theta),					Radius * FMath::Cos(Phi)				);
Verts.Add(Center + Pos);
Normals.Add(Pos.GetSafeNormal());
UVs.Add(FVector2D((float)s / Segments, (float)r / Rings));
Colors.Add(Color);
Tangents.Add(FProcMeshTangent());
}
}
		for (int32 r = 0; r < Rings; r++)		{
			for (int32 s = 0; s < Segments; s++)			{
				int32 NextS = (s + 1) % Segments;
int32 V0 = BaseVert + r * Segments + s;
int32 V1 = BaseVert + r * Segments + NextS;
int32 V2 = BaseVert + (r + 1) * Segments + NextS;
int32 V3 = BaseVert + (r + 1) * Segments + s;
Tris.Add(V0);
Tris.Add(V2);
Tris.Add(V1);
Tris.Add(V0);
Tris.Add(V3);
Tris.Add(V2);
}
}
};
// Draw all line segments
// Skip riser-to-carabiner constraints (both pinned) — real mesh risers handle that
for (const FClothConstraint& C : LineConstraints)	{
		if (C.A >= LineParticles.Num() || C.B >= LineParticles.Num()) continue;
FLineParticle& PA = LineParticles[C.A];
FLineParticle& PB = LineParticles[C.B];
// Skip riser strap (both carabiner and riser are pinned) — drawn by real mesh
if (PA.bPinned && PB.bPinned) continue;
float Radius = FMath::Max(PA.Radius, PB.Radius);
FLinearColor Color = PA.Color;
AddCylinder(PA.Pos, PB.Pos, Radius, Color);
}
	// Brake toggles and carabiners are real meshes on the paramotor — skip procedural drawing
// Carabiners and brake toggles are now real meshes on the paramotor A-arm hierarchy
// Skip drawing procedural spheres for them
LineMesh->CreateMeshSection_LinearColor(0, Verts, Tris, Normals, UVs, Colors, Tangents, false);
	LineMesh->SetMeshSectionVisible(0, true);
	// Use cached line material (loaded once in BeginPlay, not per-frame)
	if (LineMaterial)
	{
		LineMesh->SetMaterial(0, LineMaterial);
	}
}

// ============================================================
// STATE MANAGEMENT
// ============================================================

void AWingActor::SetSpreadState(){
	CanopyMesh->SetVisibility(true);
LineMesh->SetVisibility(true);
BagMesh->SetVisibility(false);
}

void AWingActor::SetInflatedShape(){
	// Wing just inflated — canopy shape comes from cell pressure, not a magic variable.
	// CanopyOpenness is driven by CellFillFactor in ApplyInflatedShape.
	CanopyOpenness = CellFillFactor;
	ApplyInflatedShape();
	// Snap particles to target positions on initial inflation
	for (int32 i = 0; i < PGWingVertCount && i < Particles.Num() && i < TargetPositions.Num(); i++)
	{
		Particles[i].Pos = TargetPositions[i];
		Particles[i].OldPos = TargetPositions[i];
	}
	UE_LOG(LogTemp, Warning, TEXT("WingActor: SetInflatedShape — cell fill drives shape (fill=%.0f%%)"), CellFillFactor * 100);
}

void AWingActor::ApplyInflatedShape(float DeltaTime){
	// Smooth brake values — asymmetric for realistic feel
	float Dt = (DeltaTime > 0.0f) ? DeltaTime : (GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.016f);
	float PullSpeed = 10.0f;
	float ReleaseSpeed = 6.0f;  // Faster release — wing recovers quickly when brake released
	float LeftSpeed = (LeftBrakeAmount > SmoothedLeftBrake) ? PullSpeed : ReleaseSpeed;
	float RightSpeed = (RightBrakeAmount > SmoothedRightBrake) ? PullSpeed : ReleaseSpeed;
	SmoothedLeftBrake = FMath::FInterpTo(SmoothedLeftBrake, LeftBrakeAmount, Dt, LeftSpeed);
	SmoothedRightBrake = FMath::FInterpTo(SmoothedRightBrake, RightBrakeAmount, Dt, RightSpeed);

	// Use cached canopy bounds
	float MinY, MaxY, MinX, MaxX;
	if (bCanopyBoundsCached)
	{
		MinY = CachedCanopyMin.Y; MaxY = CachedCanopyMax.Y;
		MinX = CachedCanopyMin.X; MaxX = CachedCanopyMax.X;
	}
	else
	{
		MinY = 1e9; MaxY = -1e9; MinX = 1e9; MaxX = -1e9;
		for (int32 i = 0; i < PGWingVertCount; i++)
		{
			MinY = FMath::Min(MinY, PGWingVertices[i].Y);
			MaxY = FMath::Max(MaxY, PGWingVertices[i].Y);
			MinX = FMath::Min(MinX, PGWingVertices[i].X);
			MaxX = FMath::Max(MaxX, PGWingVertices[i].X);
		}
		CachedCanopyMin = FVector(MinX, MinY, 0);
		CachedCanopyMax = FVector(MaxX, MaxY, 0);
		bCanopyBoundsCached = true;
	}
	float SpanRange = MaxY - MinY;
	float SpanCenter = (MinY + MaxY) * 0.5f;
	float ChordRange = MaxX - MinX;

	// === MACPARA CHARGER 1 25M ===
	// The base mesh (ParagliderWingMesh.h) now has the correct geometry:
	//   - 46 cells (47 ribs), span 11.44m, root chord 2.71m
	//   - Nearly flat top with gentle anhedral (35cm center above tips)
	//   - Wingtip stabilizer taper (50cm drop, starts at 45% span)
	//   - Reflex airfoil (NACA 4415 with reflexed TE) built into base Z
	//   - Smooth elliptical planform with swept tips
	// ApplyInflatedShape only adds INFLATION and DEFORMATION on top of the base shape.

	// === LINE TENSION DRIVES CANOPY SHAPE ===
	float Tension = FMath::Clamp(LineTensionFraction, 0.0f, 1.0f);
	CanopyOpenness = FMath::FInterpTo(CanopyOpenness, CellFillFactor, Dt, 3.0f);
	float Openness = FMath::Clamp(CanopyOpenness, 0.0f, 1.0f);
	float SpanReduction = (1.0f - Openness) * 0.10f;

	// === AIRFOIL THICKNESS (pressurization) ===
	float MaxAirfoilInflate = 8.0f;  // Gentle inflation — base mesh already has airfoil shape
	float AirfoilScale = Tension * (0.3f + Openness * 0.7f);

	// === REFLEX PROFILE (paramotor-specific) ===
	// The Charger uses a reflex airfoil: trailing edge curves UP at the rear.
	// This provides pitch stability without B-riser trim — critical for paramotor.
	// Reflex effect: last 20% of chord lifts up gently
	float ReflexAmount = 3.0f;  // cm of TE lift at full pressurization (gentle)
	float ReflexScale = Tension * Openness;

	// Brake deflection — real MacPara Charger: up to 30cm trailing edge drop at full brake
	float LeftBrakeDeflect = SmoothedLeftBrake * 30.0f;
	float RightBrakeDeflect = SmoothedRightBrake * 30.0f;
	float TotalBrake = (SmoothedLeftBrake + SmoothedRightBrake) * 0.5f;
	float KillFactor = FMath::Clamp((TotalBrake - 0.7f) / 0.3f, 0.0f, 1.0f);

	for (int32 i = 0; i < PGWingVertCount && i < Particles.Num(); i++)
	{
		FClothParticle& P = Particles[i];
		FVector BasePos = PGWingVertices[i];
		float SpanT = (BasePos.Y - SpanCenter) / (SpanRange * 0.5f);
		float SpanScale = 1.0f - SpanReduction * FMath::Abs(SpanT);
		float NewY = SpanCenter + (BasePos.Y - SpanCenter) * SpanScale;

		// Cell index — use SMOOTHED span-wise fill, not discrete per-cell
		int32 CellIdx = FMath::Clamp(
			(int32)((SpanT * 0.5f + 0.5f) * (float)NUM_WING_CELLS), 0, NUM_WING_CELLS - 1);
		// Smooth CellShape across span: interpolate between adjacent cells
		// This prevents crinkling/bumps between ribs when fill varies slightly
		float CellFrac = (SpanT * 0.5f + 0.5f) * (float)NUM_WING_CELLS - 0.5f;
		int32 CellA = FMath::Clamp((int32)CellFrac, 0, NUM_WING_CELLS - 1);
		int32 CellB = FMath::Clamp(CellA + 1, 0, NUM_WING_CELLS - 1);
		float CellLerp = FMath::Clamp(CellFrac - (float)CellA, 0.0f, 1.0f);
		float SmoothCellFill = FMath::Lerp(CellFill[CellA], CellFill[CellB], CellLerp);
		float CellShape = SmoothCellFill;

		// Top or bottom surface
		int32 LocalIdx = i % (2 * PGPointsPerRib);
		bool bIsTopSurface = (LocalIdx < PGPointsPerRib);

		// Chord position: 0 = leading edge, 1 = trailing edge
		float ChordT = ChordRange > 0.0f ? (BasePos.X - MinX) / ChordRange : 0.0f;

		// Use base mesh X directly (mesh already has correct Charger planform)
		float NewX = BasePos.X;

		// === AIRFOIL INFLATION (cell pressure adds thickness) ===
		float AirfoilInflate = 0.0f;
		if (bIsTopSurface)
		{
			// Top skin: max inflation at 25% chord (max thickness point for reflex airfoil)
			float ThicknessCurve = FMath::Sin(ChordT * PI) * 0.6f + FMath::Pow(1.0f - ChordT, 2.5f) * 0.4f;
			AirfoilInflate = ThicknessCurve * MaxAirfoilInflate * AirfoilScale * CellShape;
		}
		else
		{
			// Bottom skin: bulges DOWN between ribs
			float BulgeCurve = FMath::Sin(ChordT * PI) * 0.5f;
			AirfoilInflate = -BulgeCurve * MaxAirfoilInflate * 0.4f * AirfoilScale * CellShape;
		}

		// === LEADING EDGE ROUNDING ===
		float LERound = 0.0f;
		if (ChordT < 0.15f && bIsTopSurface)
		{
			float LEWeight = 1.0f - (ChordT / 0.15f);
			LERound = LEWeight * LEWeight * 4.0f * AirfoilScale * CellShape;
		}

		// === REFLEX PROFILE (TE curves up) ===
		// Paramotor-specific: last 20% of chord lifts up
		float ReflexLift = 0.0f;
		if (ChordT > 0.8f && bIsTopSurface)
		{
			float ReflexWeight = (ChordT - 0.8f) / 0.2f;
			ReflexLift = ReflexWeight * ReflexAmount * ReflexScale * CellShape;
		}

		// === PARACHUTE BILLOW (when no tension) ===
		float ParachuteBillow = 0.0f;
		if (Tension < 0.8f)
		{
			float NoTensionFactor = 1.0f - Tension;
			float TEBillow = FMath::Pow(ChordT, 2.0f) * 25.0f * NoTensionFactor * CellShape;
			float CenterBillow = FMath::Sin(ChordT * PI) * 15.0f * NoTensionFactor * CellShape;
			ParachuteBillow = -TEBillow - CenterBillow;
		}

		// === BRAKE DEFLECTION (trailing edge drops DOWN on braked side) ===
		// Real physics: pulling brake pulls the trailing edge down via the brake line.
		// This increases camber → more lift + drag on that side → yaw + roll toward it.
		// The deflection is strongest at the trailing edge and fades toward mid-chord.
		// Side assignment: +Y = left side of wing, -Y = right side (per wing local axes).
		// Left brake affects left side (+Y, SpanT > 0), right brake affects right side (-Y, SpanT < 0).
		float BrakeDrop = 0.0f;
		float BrakePullX = 0.0f;
		if (ChordT > 0.4f)
		{
			float EdgeWeight = (ChordT - 0.4f) / 0.6f;
			EdgeWeight = EdgeWeight * EdgeWeight * (3.0f - 2.0f * EdgeWeight);
			// Symmetric side weights: 0 at center, 1 at tip (smoothstep from 0 to 0.85)
			float LeftSideWeight = FMath::SmoothStep(0.0f, 0.85f, SpanT);   // Left: 0 at center → 1 at left tip
			float RightSideWeight = FMath::SmoothStep(0.0f, 0.85f, -SpanT); // Right: 0 at center → 1 at right tip
			float BrakeAmount = LeftBrakeDeflect * LeftSideWeight + RightBrakeDeflect * RightSideWeight;
			BrakeDrop = -EdgeWeight * BrakeAmount;  // Negative = trailing edge goes DOWN
			BrakePullX = -EdgeWeight * EdgeWeight * BrakeAmount * 0.3f;  // TE also pulls forward slightly
		}

		// Deep brake: canopy flattens
		float KillDrop = 0.0f;
		if (KillFactor > 0.01f)
		{
			KillDrop = -KillFactor * 15.0f;
		}

		// NOTE: Per-cell pressure variation (CellVariation) removed — it caused
		// crinkling/bumps between ribs when fill states differed slightly.
		// The smoothed CellShape above handles fill variation smoothly.

		// Target = base mesh position + inflation/deformation only
		// (arch and tip drop are already in BasePos.Z from the mesh)
		FVector Target = FVector(
			NewX + BrakePullX,
			NewY,
			BasePos.Z + AirfoilInflate + LERound + ReflexLift + ParachuteBillow + BrakeDrop + KillDrop);
		if (i < TargetPositions.Num())
			TargetPositions[i] = Target;
	}
}

