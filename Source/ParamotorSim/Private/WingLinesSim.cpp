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
// WingLinesSim.cpp — WingActor implementation split
// ============================================================

// ============================================================
// LINE INITIALIZATION — Real MacPara Charger 25M line cascade
// ============================================================
// Real measurements from MacPara Charger 1 user manual (page 37):
// - 5 risers per side: A, A1, B, C, D (all 525mm in neutral)
// - 5 line levels: A, B, C, D, E + Brake
// - 12 main line attachment positions per side per level
// - Line lengths vary by span position: center longest, tips shorter
// - Line tabs: A=15% chord, B=35%, C=55%, D=75%, E=85%, Brake=95%
// - Brake handle: 26cm below riser pulley
//
// Full line lengths (riser → canopy tab) [mm] from Charger 25M manual:
//   Level    Center(1)  Tip(12)  Difference
//   A        6888       6185     703mm
//   B        6824       6189     635mm
//   C        6896       6224     672mm
//   D        7080       6276     804mm
//   E        7205       6299     906mm
//   BR       7450       7180     270mm (handle to TE, only 2 positions)
//
// Riser lengths [mm] (neutral position): A=525, A1=525, B=525, C=525, D=525
// Brake line lengths: Low attachment 3.20m, High attachment 3.35m
//
// Line colors (from MacPara manual page 6):
//   A-lines = red, Brake lines = orange, all others = yellow
//
// Line materials (from MacPara manual page 6):
//   Upper lines: Aramid/Kevlar 60kg, 80kg
//   Main lines A1,B1: 230kg, A2,A3,B2,B3: 340kg, C/D: 190kg
//   Brake lines: Dynema 80kg, 100kg, main brake: 240kg
// ============================================================

void AWingActor::InitLines(){
	LineParticles.Empty();
LineConstraints.Empty();
LineAttachments.Empty();
LeftRiserIdx = -1;
RightRiserIdx = -1;
LeftCarabinerIdx = -1;
RightCarabinerIdx = -1;
LeftBrakeHandleIdx = -1;
RightBrakeHandleIdx = -1;
// Line levels: A, B, C, D, Brake
// Chord position of each tab (0=leading edge, 1=trailing edge)
	struct FLineLevelDef

{
		ELineLevel Level;
float ChordT;
float Radius;
// visual radius (cm)
FLinearColor Color;
int32 AttachEveryNthRib;
// attach to every Nth rib (not every rib)
// Real MacPara Charger 25m line lengths [cm] (riser attachment → canopy tab)
	float CenterLineLen;  // Line length at center (rib 0)
float TipLineLen;     // Line length at outermost rib
};
FLineLevelDef Levels[] = {
		{
ELineLevel::A,     TabA_ChordT,     0.25f, FLinearColor(0.85f, 0.15f, 0.15f, 1.0f), 2, 688.8f, 618.5f
},  // A: RED, center 688.8cm, tip 618.5cm (real Charger 25M)
{
ELineLevel::B,     TabB_ChordT,     0.22f, FLinearColor(0.9f, 0.75f, 0.0f, 1.0f), 2, 682.4f, 618.9f
},  // B: YELLOW, center 682.4cm, tip 618.9cm
{
ELineLevel::C,     TabC_ChordT,     0.18f, FLinearColor(0.9f, 0.75f, 0.0f, 1.0f), 2, 689.6f, 622.4f
},  // C: YELLOW, center 689.6cm, tip 622.4cm
{
ELineLevel::D,     TabD_ChordT,     0.15f, FLinearColor(0.9f, 0.75f, 0.0f, 1.0f), 2, 708.0f, 627.6f
},  // D: YELLOW, center 708.0cm, tip 627.6cm
{
ELineLevel::E,     TabE_ChordT,     0.13f, FLinearColor(0.9f, 0.75f, 0.0f, 1.0f), 3, 720.5f, 629.9f
},  // E: YELLOW, center 720.5cm, tip 629.9cm
{
ELineLevel::Brake, TabBrake_ChordT, 0.12f, FLinearColor(0.9f, 0.45f, 0.0f, 1.0f), 2, 719.0f, 692.0f
},  // Brake: ORANGE, center 719cm (745-26 handle), tip 692cm (718-26)
};
// Riser webbing is thicker
float RiserRadius = 0.5f;
float CarabinerRadius = 0.4f;
// Real MacPara Charger riser length: 52.5cm (525mm neutral position)
float RiserLength = RISER_LENGTH_CM;
// Compute half-span from mesh geometry for span-dependent line lengths
float MaxRibY = 0.0f;
for (int32 rib = 0; rib < PGNumRibs; rib++)
{
	int32 Idx = GetRibVertexIndex(rib, 0);
	if (Idx < Particles.Num())
		MaxRibY = FMath::Max(MaxRibY, FMath::Abs(Particles[Idx].Pos.Y));
}
float HalfSpan = FMath::Max(100.0f, MaxRibY);  // Min 1m if mesh invalid
// Real MacPara Charger 25M measurements:
// - Riser length: 52.5cm (525mm neutral, all 5 risers A/A1/B/C/D)
// - Total line length carabiner→canopy: ~741-773cm (riser + main line)
// - Carabiners ~42cm apart on harness
// - Brake pulley on D riser, brake handle hangs 26cm below
// - Brake handle to pulley: 26cm (sim) / 320cm (real low attachment)
// - 5 risers: A, A1, B, C, D (sim uses single riser per side)
// - Wing tip steering handles (WTST) on C riser
// For each side (left=0, right=1)

for (int32 side = 0; side < 2; side++)	{
		float SideY = (side == 0) ? (CARABINER_SPACING_CM * 0.5f) : -(CARABINER_SPACING_CM * 0.5f);
// NOTE: side 0 (left) = +Y because wing is spawned at PilotYaw+180 (leading edge
// upwind). At that rotation, local +Y → world -Y = pilot's left. This keeps
// the left carabiner/lines/brake on the pilot's left without crossing.
// === Create carabiner particle ===
// Carabiner is at the harness connection point
// On ground (not connected): at ground level
// When connected: pinned to player's hip position
		FLineParticle CarabP;
CarabP.Pos =

FVector(0, SideY, 5.0f);
// Ground level initially
		CarabP.OldPos = CarabP.Pos;
CarabP.bPinned = false;
CarabP.CanopyParticleIdx = -1;
CarabP.Radius = CarabinerRadius;
CarabP.Color =
FLinearColor(0.8f, 0.8f, 0.8f, 1.0f);
// Metal
		CarabP.Mass = 0.07f;  // Real carabiner ~70g
CarabP.Damping = 0.99f;  // Low damping - metal
CarabP.Side = side;
CarabP.Level = -1;
CarabP.RibIdx = -1;
int32 CarabIdx = LineParticles.Add(CarabP);
// === Create riser particle ===
// Riser is 50cm above carabiner (real measurement: 49.5cm)
// The riser is PINNED relative to the carabiner — it's a rigid webbing strap.
// It does NOT drift or stretch. When carabiner moves, riser follows rigidly.
		FLineParticle RiserP;
RiserP.Pos =

FVector(0, SideY, 5.0f + RiserLength);
// 49cm above carabiner (real MacPara)
		RiserP.OldPos = RiserP.Pos;
RiserP.bPinned = true;
// Riser is always pinned to carabiner offset
		RiserP.CanopyParticleIdx = -1;
RiserP.Radius = RiserRadius;
RiserP.Color =

FLinearColor(0.25f, 0.25f, 0.25f, 1.0f);
// Dark webbing
		RiserP.Mass = 0.15f;  // Real riser webbing ~150g
RiserP.Damping = 0.99f;
RiserP.Side = side;
RiserP.Level = -1;
RiserP.RibIdx = -1;
int32 RiserIdx = LineParticles.Add(RiserP);
// Constraint: carabiner → riser (50cm, rigid)
		FClothConstraint RC;
RC.A = CarabIdx;
RC.B = RiserIdx;
RC.RestLength = RiserLength;
RC.Stiffness = 1.0f;
LineConstraints.Add(RC);
if (side == 0) {
 LeftRiserIdx = RiserIdx;
LeftCarabinerIdx = CarabIdx;
}
		else {
 RightRiserIdx = RiserIdx;
RightCarabinerIdx = CarabIdx;
}
		// === Create brake handle particle ===
// Brake pulley is on the C/D riser (at the riser level, 50cm above carabiner)
// Brake handle hangs ~26cm below the pulley (real: low hook-in = 26cm)
// When connected, handle is at carabiner+24cm = hip+24cm = natural hand height
		FLineParticle BrakeHandleP;
BrakeHandleP.Pos =

FVector(0, SideY, 5.0f + RiserLength - 26.0f);
// 26cm below riser (real MacPara low hook-in)
		BrakeHandleP.OldPos = BrakeHandleP.Pos;
BrakeHandleP.bPinned = false;
BrakeHandleP.CanopyParticleIdx = -1;
BrakeHandleP.Radius = 0.3f;
BrakeHandleP.Color =
FLinearColor(0.2f, 0.2f, 0.8f, 1.0f);
// Blue handle
		BrakeHandleP.Mass = 0.03f;  // Real brake handle ~30g
BrakeHandleP.Damping = 0.98f;
BrakeHandleP.Side = side;
BrakeHandleP.Level = (
int32)ELineLevel::Brake;
BrakeHandleP.RibIdx = -1;
int32 BrakeHandleIdx = LineParticles.Add(BrakeHandleP);
if (side == 0) LeftBrakeHandleIdx = BrakeHandleIdx;
else RightBrakeHandleIdx = BrakeHandleIdx;
// Constraint: riser → brake handle (26cm, rigid strap)
		FClothConstraint BHC;
BHC.A = RiserIdx;
BHC.B = BrakeHandleIdx;
BHC.RestLength = 26.0f;
BHC.Stiffness = 1.0f;
LineConstraints.Add(BHC);
// === Create suspension lines for each level (A, B, C, D) ===
// Real cascade: canopy tab → top line → middle line → main line → riser
// We simplify to: canopy tab → 3-segment line → riser
// Total line length from riser to canopy: ~650cm (700cm total - 50cm riser)

for (auto& Lvl : Levels)		{
			if (Lvl.Level == ELineLevel::Brake)			{
				// Brake lines go from trailing edge → riser pulley → brake handle
// We route: canopy trailing edge → brake line → riser → brake handle

for (int32 rib = 0; rib < PGNumRibs; rib += Lvl.AttachEveryNthRib)				{
					// Find canopy particle at this rib's trailing edge (bottom surface, last point)
int32 CanopyIdx = GetRibVertexIndex(rib, PGPointsPerRib + PGPointsPerRib - 1);
if (CanopyIdx >= Particles.Num()) continue;
// Only attach ribs on this side
float RibY = Particles[CanopyIdx].Pos.Y;
if (side == 0 && RibY < 0) continue;
// Left side = positive Y (pilot's left when wing at 180°)
if (side == 1 && RibY > 0) continue;
// Right side = negative Y (pilot's right when wing at 180°)
// === COMPUTE LINE LENGTH FROM SPAN POSITION ===
// Real Charger 25M brake line: center 719cm, tip 692cm (handle to TE minus 26cm handle)
// The arch curve (cosine) makes tips closer → shorter lines
float SpanPos = FMath::Abs(RibY) / HalfSpan;  // 0=center, 1=tip
float ArchFactor = 1.0f - FMath::Cos(SpanPos * PI / 2.0f);  // cosine arch
float BrakeLineTotal = Lvl.CenterLineLen - (Lvl.CenterLineLen - Lvl.TipLineLen) * ArchFactor;
// Create brake line from canopy trailing edge to riser
// 2 segments for brake line

int32 NumSegs = 2;
int32 StartIdx = LineParticles.Num();
FVector CanopyPos = Particles[CanopyIdx].Pos;
FVector RiserPos = LineParticles[RiserIdx].Pos;
// Initialize particles along real line path: from canopy tab
// going backward (-X, toward pilot) for the full line length

FVector LineDir = FVector(-1, 0, 0.15f).GetSafeNormal();
// Backward + slightly up
for (int32 seg = 0; seg <= NumSegs; seg++)					{
						FLineParticle LP;
float Dist = (float)seg * (BrakeLineTotal / (NumSegs + 1));
LP.Pos = CanopyPos + LineDir * Dist;
LP.Pos.Z = FMath::Max(LP.Pos.Z, 0.0f);
LP.OldPos = LP.Pos;
LP.bPinned = (seg == 0);
LP.CanopyParticleIdx = (seg == 0) ? CanopyIdx : -1;
LP.Radius = Lvl.Radius;
LP.Color = Lvl.Color;
LP.Mass = 0.004f;  // Dyneema 1.5mm: 2g/m x 2m
LP.Damping = 0.99f;  // Low damping - line dynamics
LP.Side = side;
LP.Level = (int32)Lvl.Level;
LP.RibIdx = rib;
LineParticles.Add(LP);
}
					// Segment constraints — real line length, rigid paracord
float BrakeSegLen = BrakeLineTotal / (NumSegs + 1);
for (int32 seg = 0; seg < NumSegs; seg++)					{
						FClothConstraint C;
C.A = StartIdx + seg;
C.B = StartIdx + seg + 1;
C.RestLength = BrakeSegLen;
C.Stiffness = 1.0f;  // Inextensible paracord (Verlet constraint)
LineConstraints.Add(C);
}
// Final constraint: last segment → riser
					FClothConstraint EndC;
EndC.A = StartIdx + NumSegs;
EndC.B = RiserIdx;
EndC.RestLength = BrakeSegLen;
EndC.Stiffness = 1.0f;  // Inextensible paracord (Verlet constraint)
LineConstraints.Add(EndC);
LineAttachments.Add(
{
CanopyIdx, StartIdx, side, (int32)Lvl.Level, rib
});
}
}
			else			{
				// A/B/C/D lines: canopy tab → 3-segment line → riser
// Real total line length from canopy tab to riser: ~650cm
// (total carabiner-to-canopy ~700cm minus riser 49cm = ~651cm)

for (int32 rib = 0; rib < PGNumRibs; rib += Lvl.AttachEveryNthRib)				{
					// Find canopy particle at this rib's tab position (bottom surface)
// Bottom surface points start at PGPointsPerRib

int32 PointIdx = PGPointsPerRib + (int32)(Lvl.ChordT * (PGPointsPerRib - 1));
int32 CanopyIdx = GetRibVertexIndex(rib, PointIdx);
if (CanopyIdx >= Particles.Num()) continue;
// Only attach ribs on this side
float RibY = Particles[CanopyIdx].Pos.Y;
if (side == 0 && RibY < 0) continue;
if (side == 1 && RibY > 0) continue;
// === COMPUTE LINE LENGTH FROM SPAN POSITION ===
// Real Charger 25M data [cm] (from manual page 37):
//   A: center 688.8, tip 618.5 (70.3cm shorter)
//   B: center 682.4, tip 618.9 (63.5cm shorter)
//   C: center 689.6, tip 622.4 (67.2cm shorter)
//   D: center 708.0, tip 627.6 (80.4cm shorter)
//   E: center 720.5, tip 629.9 (90.6cm shorter)
// The arch curve (cosine) makes tips closer → shorter lines
float SpanPos = FMath::Abs(RibY) / HalfSpan;  // 0=center, 1=tip
float ArchFactor = 1.0f - FMath::Cos(SpanPos * PI / 2.0f);  // cosine arch
float MainLineTotal = Lvl.CenterLineLen - (Lvl.CenterLineLen - Lvl.TipLineLen) * ArchFactor;
// Create line from canopy to riser with 3 segments
int32 NumSegs = 3;
int32 StartIdx = LineParticles.Num();
FVector CanopyPos = Particles[CanopyIdx].Pos;
FVector RiserPos = LineParticles[RiserIdx].Pos;
// Initialize particles along real line path: from canopy tab
// going backward (-X, toward pilot) for the full line length

FVector LineDir = FVector(-1, 0, 0.15f).GetSafeNormal();
// Backward + slightly up
for (int32 seg = 0; seg <= NumSegs; seg++)					{
						FLineParticle LP;
float Dist = (float)seg * (MainLineTotal / (NumSegs + 1));
LP.Pos = CanopyPos + LineDir * Dist;
LP.Pos.Z = FMath::Max(LP.Pos.Z, 5.0f);
LP.OldPos = LP.Pos;
LP.bPinned = (seg == 0);
LP.CanopyParticleIdx = (seg == 0) ? CanopyIdx : -1;
LP.Radius = Lvl.Radius;
LP.Color = Lvl.Color;
// Line mass for Verlet stability. Physical paracord mass (~0.0035 kg/m) is too
// small for stable Verlet integration with real line stiffness (250000 N/m) at
// 60fps. Use a higher mass that keeps the simulation stable — line mass doesn't
// affect flight physics (lines are visual-only during flight).
LP.Mass = 0.5f;
LP.Damping = 0.9f;
LP.Side = side;
LP.Level = (int32)Lvl.Level;
LP.RibIdx = rib;
LineParticles.Add(LP);
}
					// Segment constraints — real line length, rigid paracord
float MainSegLen = MainLineTotal / (NumSegs + 1);
for (int32 seg = 0; seg < NumSegs; seg++)					{
						FClothConstraint C;
C.A = StartIdx + seg;
C.B = StartIdx + seg + 1;
C.RestLength = MainSegLen;
C.Stiffness = 1.0f;  // Inextensible paracord (Verlet constraint)
LineConstraints.Add(C);
}
// Final constraint: last segment → riser
					FClothConstraint EndC;
EndC.A = StartIdx + NumSegs;
EndC.B = RiserIdx;
EndC.RestLength = MainSegLen;
EndC.Stiffness = 1.0f;  // Inextensible paracord (Verlet constraint)
LineConstraints.Add(EndC);
LineAttachments.Add(
{
CanopyIdx, StartIdx, side, (int32)Lvl.Level, rib
});
}
}
}
}
	UE_LOG(LogTemp, Warning, TEXT("WingActor: InitLines — %d line particles, %d constraints, %d attachments"),		LineParticles.Num(), LineConstraints.Num(), LineAttachments.Num());
}

// ============================================================
// LINE SIMULATION — Verlet + sag
// ============================================================

void AWingActor::SimulateLines(float DeltaTime){
	if (LineParticles.Num() == 0) return;
// Brake response: physical line dynamics
// Brake line wave speed: c = sqrt(T/μ) where T=tension, μ=linear density
// For Dyneema 1.5mm: T~50N, μ~0.002 kg/m → c ~160 m/s
// Response time: τ = L/c = 7m/160 = 0.044s → very fast
// The FInterpTo rate corresponds to 1/τ ≈ 23/s
float BrakeResponseSpeed = 1.0f / (LINE_LENGTH_CM / 100.0f / 160.0f);  // tau = L/c, c=160m/s
	LeftBrakeAmount =
FMath::FInterpTo(LeftBrakeAmount, LeftBrakeTarget, DeltaTime, BrakeResponseSpeed);
RightBrakeAmount = FMath::FInterpTo(RightBrakeAmount, RightBrakeTarget, DeltaTime, BrakeResponseSpeed);
// Update pinned line particles to follow canopy

for (const FLineAttachment& Att : LineAttachments)	{
		if (Att.CanopyParticleIdx >= 0 && Att.CanopyParticleIdx < Particles.Num())		{
			LineParticles[Att.LineParticleIdx].Pos = Particles[Att.CanopyParticleIdx].Pos;
LineParticles[Att.LineParticleIdx].OldPos = LineParticles[Att.LineParticleIdx].Pos;
}
}
	// Update carabiner and riser particles if connected
// The carabiner is at the player's hip (world space).
// The riser is 49cm of webbing FROM the carabiner TOWARD the wing.
// When the wing is on the ground behind the pilot, the riser hangs DOWN.
// When the wing is inflated overhead, the riser points UP.

if (bRisersConnected)	{
		FTransform ActorTransform = GetActorTransform();
FVector WingWorldPos = GetActorLocation();
auto UpdateCarabinerAndRiser = [&](int32 CarabIdx, int32 RiserIdx, FVector CarabWorldPos)		{
			if (CarabIdx < 0 || CarabIdx >= LineParticles.Num()) return;
// Carabiner at the harness
FVector LocalPos = ActorTransform.InverseTransformPosition(CarabWorldPos);
LineParticles[CarabIdx].Pos = LocalPos;
LineParticles[CarabIdx].OldPos = LocalPos;
LineParticles[CarabIdx].bPinned = true;
// Riser: 49cm from carabiner, pointing UP (rigid webbing strap).
// The riser is a fixed-length strap that hangs from the carabiner.
// It does NOT follow the wing direction — it always points up from
// the carabiner. The lines above the riser connect to the wing.
// When the wing is on the ground behind the pilot, the riser points
// up and the lines drape down from the riser top to the wing.
// When the wing is inflated overhead, the riser points up and the
// lines go from the riser top up to the canopy.
if (RiserIdx >= 0 && RiserIdx < LineParticles.Num())			{
				// Riser always points straight up from carabiner (rigid strap)
				FVector RiserWorldPos = CarabWorldPos + FVector(0, 0, RISER_LENGTH_CM);
				FVector RiserLocalPos = ActorTransform.InverseTransformPosition(RiserWorldPos);
				LineParticles[RiserIdx].Pos = RiserLocalPos;
				LineParticles[RiserIdx].OldPos = RiserLocalPos;
				LineParticles[RiserIdx].bPinned = true;
			}
};
UpdateCarabinerAndRiser(LeftCarabinerIdx, LeftRiserIdx, LeftCarabinerPos);
UpdateCarabinerAndRiser(RightCarabinerIdx, RightRiserIdx, RightCarabinerPos);

// Debug: log riser world positions
RiserLogTimer += DeltaTime;
if (RiserLogTimer > 1.0f && LeftRiserIdx >= 0 && RightRiserIdx >= 0)
{
	RiserLogTimer = 0.0f;
	FTransform T = GetActorTransform();
	FVector LCarabWorld = T.TransformPosition(LineParticles[LeftCarabinerIdx].Pos);
	FVector RCarabWorld = T.TransformPosition(LineParticles[RightCarabinerIdx].Pos);
	FVector LRiserWorld = T.TransformPosition(LineParticles[LeftRiserIdx].Pos);
	FVector RRiserWorld = T.TransformPosition(LineParticles[RightRiserIdx].Pos);
	UE_LOG(LogTemp, Warning, TEXT("RISER: Carab L=%s R=%s | Riser L=%s R=%s | Wing=%s"),
		*LCarabWorld.ToString(), *RCarabWorld.ToString(),
		*LRiserWorld.ToString(), *RRiserWorld.ToString(),
		*GetActorLocation().ToString());
}
}
	else	{
		if (LeftCarabinerIdx >= 0) LineParticles[LeftCarabinerIdx].bPinned = false;
if (RightCarabinerIdx >= 0) LineParticles[RightCarabinerIdx].bPinned = false;
}
// Reposition intermediate line particles — NOW REAL VERLET PHYSICS during flight
// Lines are real physical constraints: Verlet integration + constraint solving.
// Canopy attachment points are pinned to follow canopy particles.
// Carabiners are pinned to follow the harness.
// Everything else is physics: gravity, wind drag, constraint solving.
if (bRisersConnected)	{
    // Pin canopy attachment points to follow canopy particles
    for (const FLineAttachment& Att : LineAttachments)	{
        if (Att.CanopyParticleIdx >= 0 && Att.CanopyParticleIdx < Particles.Num())	{
            FVector CanopyLocalPos = Particles[Att.CanopyParticleIdx].Pos;
            LineParticles[Att.LineParticleIdx].Pos = CanopyLocalPos;
            LineParticles[Att.LineParticleIdx].OldPos = CanopyLocalPos;
        }
    }
}
	// Verlet integration for ALL line particles (including during flight)
	// Lines are real physical objects with mass, gravity, and wind drag.
	// During flight, the constraint solving keeps them taut between canopy and riser.
	{
		// Wind direction in local space
		FTransform ActorTransform = GetActorTransform();
		FVector WindDirLocal = ActorTransform.InverseTransformVector(WindDirection);
		float WindMs = CurrentWindSpeed;
		// During flight, also account for apparent wind from pilot velocity
		FVector PilotVelLocal = FVector::ZeroVector;
		if (WingState == EWingState::Flying || WingState == EWingState::Inflated)
		{
			if (ConnectedParamotor)
			{
				APawn* Pawn = Cast<APawn>(ConnectedParamotor->GetAttachParentActor());
				if (Pawn)
				{
					FVector PilotVelWorld = Pawn->GetVelocity();
					PilotVelLocal = ActorTransform.InverseTransformVector(PilotVelWorld);
				}
			}
		}
		for (int32 i = 0; i < LineParticles.Num(); i++)		{
			FLineParticle& LP = LineParticles[i];
if (LP.bPinned) continue;
// Verlet integration
FVector Vel = (LP.Pos - LP.OldPos) * LP.Damping;
LP.OldPos = LP.Pos;
LP.Pos += Vel;
// Gravity on line particles
// Real line linear density: paracord ~0.003 kg/m (3g/m for 1.5mm Dyneema)
// Line segment mass: 0.003 × 2m = 0.006 kg
// Gravity acceleration is the same for all objects (g = 9.81 m/s²)
// The 0.3× reduction was wrong — gravity doesn't change with mass.
// The visual "lightness" of lines comes from their low mass → wind drag
// dominates over gravity, not from reduced gravity.
LP.Pos.Z -= Gravity * DeltaTime * DeltaTime;
// === REAL WIND DRAG ON LINES ===
// Each line segment is a cylinder in crossflow.
// F = 0.5 * rho * V² * d * L * CD, where d=diameter, L=length, CD=1.0
float LineDiameterM = 0.0015f;  // Real 1.5mm Dyneema
float SegLenM = 2.0f;  // Default segment length (m)
// Apparent wind = wind - line velocity (line moves with wing)
// During flight, also subtract pilot velocity for apparent wind
FVector LineVel = (LP.Pos - LP.OldPos) / FMath::Max(DeltaTime, 0.001f);
FVector ApparentWindLocal = WindDirLocal * WindMs * 100.0f - PilotVelLocal - LineVel;
float ApparentWindMs = ApparentWindLocal.Size() / 100.0f;
FVector ApparentWindDir = ApparentWindLocal.GetSafeNormal();
float LineDragN = 0.5f * AIR_DENSITY_SEA_LEVEL * ApparentWindMs * ApparentWindMs * LineDiameterM * SegLenM * 1.0f;
float LineDragAccel = (LineDragN / FMath::Max(0.0001f, LP.Mass)) * CM_PER_M;
// Apply wind drag in apparent wind direction
LP.Pos += ApparentWindDir * LineDragAccel * DeltaTime * DeltaTime;
// Ground collision (in world space)
FVector WorldPos = GetActorLocation() + LP.Pos;
float GZ = GetCachedGroundZ(WorldPos);
if (WorldPos.Z < GZ)			{
				LP.Pos.Z = GZ - GetActorLocation().Z;
LP.OldPos.Z = LP.Pos.Z;
}
}
	}
	// Position brake handles hanging from the riser, toward the wing
// Brake handle hangs 26cm below the riser pulley (in the direction of gravity/wing)
// When brake is pulled, handle moves down further

if (bRisersConnected)	{
		FTransform ActorTransform = GetActorTransform();
FVector WingWorldPos = GetActorLocation();
auto UpdateBrakeHandle = [&](int32 HandleIdx, int32 RiserIdx2, FVector CarabWorldPos, float BrakeAmount)		{
			if (HandleIdx < 0 || HandleIdx >= LineParticles.Num()) return;
if (RiserIdx2 < 0 || RiserIdx2 >= LineParticles.Num()) return;
// Get riser world position
FVector RiserWorldPos = ActorTransform.TransformPosition(LineParticles[RiserIdx2].Pos);
// Brake handle hangs from riser (real Charger 25M geometry from manual):
// - 26cm below riser pulley at rest (real measurement)
// - Up to 40cm additional pull at full brake (real pilot pull distance)
// Full brake: θ_TE = asin(40/200) = 11.5° (documented in AGENTS.md)
float HangDist = 26.0f + BrakeAmount * 40.0f;
FVector HandleWorldPos = RiserWorldPos - FVector(0, 0, HangDist);
// Don't let handle go below ground
float GZ = GetCachedGroundZ(HandleWorldPos);
if (HandleWorldPos.Z < GZ)				HandleWorldPos.Z = GZ;
FVector HandleLocalPos = ActorTransform.InverseTransformPosition(HandleWorldPos);
LineParticles[HandleIdx].Pos = HandleLocalPos;
LineParticles[HandleIdx].OldPos = HandleLocalPos;
};
UpdateBrakeHandle(LeftBrakeHandleIdx, LeftRiserIdx, LeftCarabinerPos, LeftBrakeAmount);
UpdateBrakeHandle(RightBrakeHandleIdx, RightRiserIdx, RightCarabinerPos, RightBrakeAmount);
}
	// Constraint solving — ALL states including inflated/flying
	// Real line constraints keep the lines taut between canopy and riser.
	// During flight, this is what actually holds the wing up — the lines
	// transfer load from the canopy to the riser to the carabiner.
	// More iterations during flight for stiffer lines (more load).
	int32 NumConstraintIters = (WingState == EWingState::Inflated || WingState == EWingState::Flying) ? 10 : 6;  // More iterations for stiffer loaded lines
	{
		for (int32 iter = 0; iter < NumConstraintIters; iter++)		{
			for (const FClothConstraint& C : LineConstraints)			{
			if (C.A >= LineParticles.Num() || C.B >= LineParticles.Num()) continue;
FLineParticle& PA = LineParticles[C.A];
FLineParticle& PB = LineParticles[C.B];
FVector Delta = PB.Pos - PA.Pos;
float Dist = Delta.Size();
if (Dist < 0.001f) continue;
float Diff = (Dist - C.RestLength) / Dist * C.Stiffness * 0.5f;
FVector Offset = Delta * Diff;
if (!PA.bPinned) PA.Pos += Offset;
if (!PB.bPinned) PB.Pos -= Offset;
}
}
	}
	// === DIRECT CANOPY-TO-CARABINER CONSTRAINTS (safety net) ===
// These are a safety net for when the multi-segment line constraints
// aren't stiff enough to keep the canopy within line length.
// The real line constraints above handle the primary load transfer.
// This only kicks in when the canopy exceeds the total line length.
// Skip during inflation so the wing can rise freely
if (bRisersConnected && WingState != EWingState::Inflating)	{
		FTransform ActorTransform = GetActorTransform();
for (const FLineAttachment& Att : LineAttachments)		{
			if (Att.CanopyParticleIdx < 0 || Att.CanopyParticleIdx >= Particles.Num()) continue;
// Determine which carabiner and total line length for this attachment
// Real Charger 25M: total carabiner-to-canopy = riser (52.5cm) + line length
int32 CarabIdx = (Att.Side == 0) ? LeftCarabinerIdx : RightCarabinerIdx;
// Per-level total lengths (riser + main line) from real Charger 25M data
float TotalLineLen = 745.0f;  // Default from Charger 25M data
switch ((ELineLevel)Att.Level)
{
case ELineLevel::A:     TotalLineLen = RISER_LENGTH_CM + 688.8f; break;  // 741.3cm
case ELineLevel::B:     TotalLineLen = RISER_LENGTH_CM + 682.4f; break;  // 734.9cm
case ELineLevel::C:     TotalLineLen = RISER_LENGTH_CM + 689.6f; break;  // 742.1cm
case ELineLevel::D:     TotalLineLen = RISER_LENGTH_CM + 708.0f; break;  // 760.5cm
case ELineLevel::E:     TotalLineLen = RISER_LENGTH_CM + 720.5f; break;  // 773.0cm
case ELineLevel::Brake: TotalLineLen = RISER_LENGTH_CM + 719.0f; break;  // 771.5cm
}
if (CarabIdx < 0 || CarabIdx >= LineParticles.Num()) continue;
// Canopy particle position in world space
FVector CanopyWorld = ActorTransform.TransformPosition(Particles[Att.CanopyParticleIdx].Pos);
FVector CarabWorld = (Att.Side == 0) ? LeftCarabinerPos : RightCarabinerPos;
FVector ToCarab = CarabWorld - CanopyWorld;
float Dist = ToCarab.Size();
if (Dist > TotalLineLen)			{
// Safety net: pull back by the excess distance (line constraint physics)
// The line can only stretch so much before pulling the canopy back
float Excess = Dist - TotalLineLen;
// Full excess — line is inextensible (Verlet constraint handles stiffness)
float PullStrength = Excess;
FVector PullDir = ToCarab.GetSafeNormal();
FVector Pull = PullDir * PullStrength;
FVector PullLocal = ActorTransform.InverseTransformVector(Pull);
// === SMOOTH PULL DISTRIBUTION ===
// Instead of pulling just one canopy particle (which creates sharp spikes/teeth),
// distribute the pull across neighboring ribs and chord points using a Gaussian
// falloff. This simulates how real fabric distributes load across a wide area.
//
// The attachment point gets full pull, neighbors get exponentially less.
// Spread radius: 2 ribs sideways, 3 chord points forward/back.
// This creates a smooth dimple instead of a sharp tooth.

int32 CenterRib = Att.RibIdx;
int32 VertsPerRib = 2 * PGPointsPerRib;
// Find the chord index of the attachment point within its rib
int32 LocalIdx = Att.CanopyParticleIdx - CenterRib * VertsPerRib;
int32 CenterChordTop = LocalIdx % PGPointsPerRib;
// 0-41 (top surface)
bool bIsBottom = LocalIdx >= PGPointsPerRib;
int32 CenterChord = CenterChordTop;  // Same for top and bottom surfaces
// Spread across ribs and chord points
const int32 RibSpread = 2;
// ±2 ribs
const int32 ChordSpread = 4;
// ±4 chord points
const float Sigma = 1.5f;
// Gaussian width (tighter = more localized)
for (int32 dRib = -RibSpread; dRib <= RibSpread; dRib++)				{
					int32 Rib = CenterRib + dRib;
if (Rib < 0 || Rib >= PGNumRibs) continue;
for (int32 dChord = -ChordSpread; dChord <= ChordSpread; dChord++)					{
						int32 Chord = CenterChord + dChord;
if (Chord < 0 || Chord >= PGPointsPerRib) continue;
// Gaussian weight based on distance from center
float DistSq = (float)(dRib * dRib) + (float)(dChord * dChord) / 4.0f;
float Weight = FMath::Exp(-DistSq / (2.0f * Sigma * Sigma));
if (Weight < 0.01f) continue;
// Skip negligible contributions
// Apply to top surface

int32 VtxIdx = GetRibVertexIndex(Rib, Chord);
if (VtxIdx >= 0 && VtxIdx < Particles.Num())						{
							FClothParticle& CP = Particles[VtxIdx];
if (!CP.bPinned && !CP.bGrabbed)							{
								CP.Pos += PullLocal * Weight;
CP.OldPos += PullLocal * Weight;
}
}
						// Also apply to bottom surface
// Load transfer: top skin carries pressure differential, bottom skin carries
// less. Real cell: top skin ~70% of load, bottom skin ~30% (from pressure
// distribution on a ram-air airfoil where most lift is on the upper surface)
int32 VtxIdxBot = GetRibVertexIndex(Rib, PGPointsPerRib + Chord);
if (VtxIdxBot >= 0 && VtxIdxBot < Particles.Num())						{
							FClothParticle& CP = Particles[VtxIdxBot];
if (!CP.bPinned && !CP.bGrabbed)							{
								CP.Pos += PullLocal * Weight * 0.4f;  // 40% on bottom skin (pressure distribution)
CP.OldPos += PullLocal * Weight * 0.4f;
}
}
}
}
				// Move intermediate line particles proportionally
			// Loop against actual segment count (brake lines may have only 2 segments)
			int32 LineStart = Att.LineParticleIdx;
			int32 MaxSegs = 3;  // Default for main lines
			// Count actual segments for this line
			int32 SegCount = 0;
			for (int32 s = LineStart + 1; s < LineParticles.Num() && !LineParticles[s].bPinned; s++)
				SegCount++;
			MaxSegs = FMath::Min(MaxSegs, SegCount);
			for (int32 seg = 1; seg <= MaxSegs; seg++)				{
					if (LineStart + seg >= LineParticles.Num()) break;
if (!LineParticles[LineStart + seg].bPinned)					{
						LineParticles[LineStart + seg].Pos += PullLocal * (float)seg / (float)(MaxSegs + 1);
LineParticles[LineStart + seg].OldPos += PullLocal * (float)seg / (float)(MaxSegs + 1);
}
}
}
}
		// Now check if the wing actor itself needs to move
// If multiple canopy particles are being pulled, the whole wing should follow

FVector ActorLoc = GetActorLocation();
FVector CarabMid = (LeftCarabinerPos + RightCarabinerPos) * 0.5f;
FVector ToWing = ActorLoc - CarabMid;
ToWing.Z = 0.0f;
float WingDist = ToWing.Size();
// Max distance: total line length + half chord (real Charger 25M)
float MaxDist = LINE_LENGTH_CM + 145.0f;
// ~895cm (750cm lines + 145cm half chord)
if (WingDist > MaxDist)		{
			float Excess = WingDist - MaxDist;
FVector DragDir = ToWing.GetSafeNormal();
FVector Shift = -DragDir * Excess;
SetActorLocation(ActorLoc + Shift, false);
ShiftAllParticles(Shift);
}
}
}

FVector AWingActor::GetLeftBrakeHandleWorldPos() const{
	if (LeftBrakeHandleIdx >= 0 && LeftBrakeHandleIdx < LineParticles.Num())		return GetActorTransform().TransformPosition(LineParticles[LeftBrakeHandleIdx].Pos);
return FVector::ZeroVector;
}

FVector AWingActor::GetRightBrakeHandleWorldPos() const{
	if (RightBrakeHandleIdx >= 0 && RightBrakeHandleIdx < LineParticles.Num())		return GetActorTransform().TransformPosition(LineParticles[RightBrakeHandleIdx].Pos);
return FVector::ZeroVector;
}

