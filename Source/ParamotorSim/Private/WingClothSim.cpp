#include "WingActor.h"
#include "AirfoilPolar.h"
#include "ParamotorActor.h"
#include "ParamotorCharacter.h"
#include "ParamotorGameInstance.h"
#include "ParagliderWingMesh.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

// ============================================================
// WingClothSim.cpp — WingActor implementation split
// ============================================================

// WING GEOMETRY HELPERS
// ============================================================
// ============================================================
// WING GEOMETRY HELPERS
// ============================================================

int32 AWingActor::GetRibVertexIndex(int32 RibIdx, int32 PointIdx) const{
	// Each rib has 2*PGPointsPerRib vertices: top surface then bottom surface
return RibIdx * (2 * PGPointsPerRib) + PointIdx;
}

// ============================================================
// REAL CELL PRESSURE MODEL
// ============================================================
// The wing flies because cells fill with air from ram-air pressure.
// Each of the 46 cells has independent pressure, fill state, lift, and drag.
// The canopy shape comes from cell pressure — not a forced shape.
// Lift comes from the pressurized airfoil shape — not a formula.
// When cells lose pressure (no wind, collapse), the wing falls.
// ============================================================

void AWingActor::UpdateCellPressure(float DeltaTime, float ApparentWindMs, float BrakeLeft, float BrakeRight)
{
	// === RAM-AIR PRESSURE ===
	// Dynamic pressure at the leading edge: q = 0.5 × ρ × V²
	// This is the pressure that forces air into the cells
	float Rho = AIR_DENSITY_SEA_LEVEL;
	float RamPressure = 0.5f * Rho * ApparentWindMs * ApparentWindMs;  // Pa

	// === MINIMUM FORCE FOR INFLATION ===
	// Research (arXiv 2205.08411): there is a minimum pressure below which
	// no inflation occurs. For double-skin wings, fabric weight vs pressure.
	// A-lines pulled = wider intakes = can catch air at lower pressure.
	// Normal intakes: 3.0 Pa (~2.2 m/s). A-lines: 1.5 Pa (~1.6 m/s).
	float MinInflationPressure = bLiftingALines ? 1.5f : 3.0f;

	// === FILL RATE — EXPONENTIAL (pressure-driven) ===
	// Real cell filling: exponential approach like inflating a balloon.
	// fill(t) = 1 - exp(-t/τ) where τ = fill time constant
	// τ depends on wind speed: more wind = faster fill
	// Real inflation takes 5-8s in light wind, 3-5s in moderate wind
	// At 7 m/s (16mph): τ ≈ 3s → 95% fill in 9s
	// At 3 m/s (7mph): τ ≈ 8s → 95% fill in 24s
	// At 1 m/s: τ ≈ 20s → barely fills
	float FillTimeConstant = FMath::Max(2.0f, 12.0f / FMath::Max(0.5f, ApparentWindMs));
	float ExponentialFillRate = 1.0f / FillTimeConstant;

	// A-line pull opens the leading edge intakes wider → higher discharge coefficient
	// Real physics: Cd_open ≈ 0.3 (closed intakes) → Cd_open ≈ 0.9 (A-lines pulled)
	// Without A-lines, fill is 3x slower (intakes barely open)
	float IntakeCd = bLiftingALines ? 0.9f : 0.3f;
	float IntakeCdRatio = IntakeCd / 0.9f;  // 0.33 normal, 1.0 with A-lines

	// Fill rate scales with pressure ratio — higher pressure = faster fill
	// At threshold: slow fill. At 5x threshold: fast fill.
	float PressureRatio = (RamPressure > 0.01f) ? RamPressure / MinInflationPressure : 0.0f;
	float FillRate = ExponentialFillRate * IntakeCdRatio * PressureRatio;

	// Deflation: when no wind, cells lose air through porosity + open intakes
	// Real wings deflate in 1-2 seconds when wind stops completely
	float DeflationRate = 0.5f + WingPorosity * 10.0f;  // From porosity + exit area
	float LeakRate = WingPorosity;  // Additional porosity leak

	// Per-side brake stall: based on actual AoA, not a fixed brake percentage
	// Brake increases AoA via trailing edge deflection. Stall occurs when
	// the total AoA (base + brake + turbulence) exceeds the stall angle.
	// Real wings stall at ~15° AoA (from the polar data).
	float LeftBrakeAoA = TrailingEdgeDeflectionDeg(BrakeLeft);
	float RightBrakeAoA = TrailingEdgeDeflectionDeg(BrakeRight);
	float LeftTotalAoA = 8.0f + LeftBrakeAoA;  // Ground AoA + brake AoA
	float RightTotalAoA = 8.0f + RightBrakeAoA;
	float StallAoADeg = WingModel.StallAoA;
	// Stall factor ramps from 0 at stall AoA to 1 at stall+5° (flow separation gradient)
	float LeftBrakeStallFactor = FMath::Clamp((LeftTotalAoA - StallAoADeg) / FMath::Max(1.0f, StallAoADeg * 0.3f), 0.0f, 1.0f);
	float RightBrakeStallFactor = FMath::Clamp((RightTotalAoA - StallAoADeg) / FMath::Max(1.0f, StallAoADeg * 0.3f), 0.0f, 1.0f);

	for (int32 cell = 0; cell < NUM_WING_CELLS; cell++)
	{
		// Cell span position: -1 (left tip) to +1 (right tip)
		float SpanT = (float)cell / (float)(NUM_WING_CELLS - 1) * 2.0f - 1.0f;

		// Determine which side this cell is on
		float BrakeStallFactor = (SpanT < 0.0f) ? LeftBrakeStallFactor : RightBrakeStallFactor;
		float BrakeInput = (SpanT < 0.0f) ? BrakeLeft : BrakeRight;

		// === PROGRESSIVE FILL (center first, tips last) ===
		// Real wings inflate from the center outward because:
		// 1. Center cells have the largest intakes (highest ram pressure)
		// 2. Pressure propagates through internal cell ports at the speed of sound
		//    in the fabric structure (~5-10 m/s for soft fabric)
		// 3. Tips have smaller intakes and are further from the pressure source
		// The delay is the pressure propagation time: t = distance / c_prop
		float CenterDistance = FMath::Abs(SpanT);  // 0 at center, 1 at tips
		float PropagationSpeedMs = FMath::Sqrt(50.0f / 0.5f);  // c = sqrt(T/mu), T~50N, mu~0.5kg/m
		float HalfSpanM = FMath::Sqrt(WingModel.WingArea * WingModel.AR) * 0.5f;
		float PropagationTime = CenterDistance * HalfSpanM / PropagationSpeedMs;
		float ProgressiveFillDelay = PropagationTime * FillRate;
		float CellFillRate = FillRate * (1.0f - ProgressiveFillDelay);

		// === CELL FILL ===
		// Air enters through leading edge when ram pressure > minimum
		// A-line pull opens the intakes wider (higher Cd) but does NOT bypass
		// the wind requirement. No ram pressure = no fill, even with A-lines pulled.
		// The IntakeCdRatio above makes fill 50% faster when A-lines are pulled,
		// but only if there's enough wind to generate ram pressure.
		bool bCanFill = (RamPressure > MinInflationPressure);
		if (bCanFill)
		{
			// Exponential fill: rate proportional to unfilled portion
			// This gives the realistic fast-then-slow fill behavior
			float Unfilled = 1.0f - CellFill[cell];
			// Fill rate reduced by brake stall (disrupted airflow into leading edge)
			float EffectiveFillRate = CellFillRate * (1.0f - BrakeStallFactor);
			CellFill[cell] = FMath::Min(1.0f, CellFill[cell] + Unfilled * EffectiveFillRate * DeltaTime);
		}
		else
		{
			// No wind → cells deflate (exponential decay)
			CellFill[cell] = CellFill[cell] * FMath::Exp(-DeflationRate * DeltaTime);
		}

		// Porosity: cells slowly leak air even with wind
		CellFill[cell] = FMath::Max(0.0f, CellFill[cell] - LeakRate * DeltaTime);

		// === CELL PRESSURE ===
		// Internal pressure = ram pressure × fill state
		// Brake increases pressure slightly (blocks air exit) up to a point,
		// then stall causes pressure loss (airflow disrupted).
		// At full stall: airflow completely separated → cells can't maintain pressure.
		// Cell pressure: ram pressure × fill × brake blockage / stall disruption
		// Brake blocks the trailing edge exit → less air escapes → pressure rises
		// Physical: exit area reduces by (1 - brake_deflection_fraction)
		// Stall: flow separation blocks the intake → pressure drops sharply
		float ExitAreaFraction = 1.0f - BrakeInput * TrailingEdgeDeflectionDeg(BrakeInput) / 90.0f;  // Fraction blocked = TE deflection / 90
		float StallPressureLoss = 1.0f - BrakeStallFactor;  // Full stall = no pressure
		float BrakePressureBoost = ExitAreaFraction * StallPressureLoss;
		// No floor - pressure from physics
		CellPressure[cell] = RamPressure * CellFill[cell] * BrakePressureBoost;

		// === CELL LIFT AND DRAG ===
		// Each cell generates lift based on its pressurized shape and AoA.
		// No pressure = no lift (flat fabric generates no lift)
		// Cell area = total area / num cells
		float CellArea = WingModel.WingArea / (float)NUM_WING_CELLS;

		// === REAL AoA-BASED LIFT (not CL0 cap) ===
		// The old formula was: CL = CL0 × fill² × tension
		// This capped CL at CL0 (0.50), requiring 12+ m/s for level flight.
		// Real wings generate CL from CL0 up to CLMax depending on AoA.
		// At trim AoA (~8°): CL ≈ 0.8-1.0 for a reflex wing.
		// The cell model now uses the real polar CL at the current AoA,
		// scaled by fill (partially filled cells = less lift) and tension
		// (no tension = parachute, not airfoil).
		//
		// Kiting/ground-run AoA: base trim + brake + pendulum pitch
		// During ground run, the wing is overhead and the pilot runs forward.
		// The relative wind comes from forward+below → higher AoA than cruise.
		// Real ground-run AoA: ~12-15° (wing overhead, pilot running into wind).
		// This gives CL ≈ 1.0-1.3, enough lift at 6-7 m/s to take off.
		// Kiting/ground-run AoA: wing overhead, pilot running → higher AoA for takeoff lift
		// Flying AoA: lower trim AoA for level cruise at ~10 m/s
		// Real level-flight CL at trim ≈ 0.74, from the polar that is ~2°.
		// Real takeoff CL at ~8 m/s ≈ 1.18, from the polar that is ~8°.
		// Ground-run/inflation uses a higher AoA than trim (wing is pitched up by A-lines).
		// Flying uses trim AoA. The old hardcoded 8° is physically correct for inflation.
		float CellBaseAoA = (WingState == EWingState::Flying) ? WingModel.TrimAoA : 8.0f;
		float CellBrakeAoA = TrailingEdgeDeflectionDeg(BrakeInput);
		float CellAoA = CellBaseAoA + CellBrakeAoA;
		// Get real CL from polar at this AoA
		float PolarCL, PolarCD;
		LookupAirfoilPolarWithCamber(CellAoA, BrakeInput, PolarCL, PolarCD);
		// Affine scale from base polar (BaseCL0≈0.41, BaseCLMax=1.47) to (WingCL0, WingCLMax)
		float BaseCL0 = 0.41f, BaseCLMax = 1.47f;
		float CLDenom = BaseCLMax - BaseCL0;
		float CLScale = FMath::Abs(CLDenom) > 1e-3f ? (WingModel.CLMax - WingModel.CL0) / CLDenom : 1.0f;
		float BaseCL = (PolarCL - BaseCL0) * CLScale + WingModel.CL0;

		// Fill scaling: linear (partially filled wing still generates partial lift).
		// fill² was tested but kills lift at low fill, preventing inflation.
		// Tension gating: no tension = parachute (no airfoil lift)
		float CellCL = BaseCL * CellFill[cell] * LineTensionFraction;  // Linear fill scaling

		// Stalled cells (from brake) lose lift progressively
		// At full stall: CL drops to near zero (completely separated flow)
		CellCL *= (1.0f - BrakeStallFactor);

		// Cell CD: from the real airfoil polar at this AoA + camber
		// Scale drag similarly to CL (affine from base polar)
		float BaseAirfoilCD0 = 0.0082f;
		float CellCD = WingModel.CD0 + FMath::Max(0.0f, PolarCD - BaseAirfoilCD0);
		// Additional stalled drag from separated flow (not in the pre-stall polar)
		CellCD += BrakeStallFactor * 1.0f;  // Bluff body CD=1.0 for stalled flow
		// Bluff-body drag only for nearly-empty cells (avoid excessive drag during inflation)
		if (CellFill[cell] < 0.1f)
			CellCD += (1.0f - CellFill[cell] / 0.1f) * 0.8f;

		// Per-cell apparent wind: spanwise variation from wind gradient + turbulence
		// Real wings see slightly different wind at each cell due to:
		// 1. Wind gradient (tips are higher than center when kiting overhead)
		// 2. Turbulence spatial variation (each cell sees a different turbulent eddy)
		float SpanPositionM = SpanT * FMath::Sqrt(WingModel.WingArea * WingModel.AR) * 0.5f;
		float CellWindMs = ApparentWindMs * (1.0f + TurbLatState * SpanPositionM / FMath::Max(1.0f, FMath::Sqrt(WingModel.WingArea * WingModel.AR) * 0.5f));
		CellWindMs = FMath::Max(0.0f, CellWindMs);

		CellLift[cell] = 0.5f * Rho * CellWindMs * CellWindMs * CellArea * CellCL;
		CellDrag[cell] = 0.5f * Rho * CellWindMs * CellWindMs * CellArea * CellCD;
	}

	// Update overall CellFillFactor for compatibility with existing code
	float TotalFill = 0.0f;
	for (int32 cell = 0; cell < NUM_WING_CELLS; cell++)
		TotalFill += CellFill[cell];
	CellFillFactor = TotalFill / (float)NUM_WING_CELLS;
}

// Get total lift from all cells (Newtons)
float AWingActor::GetTotalCellLiftN() const
{
	float Total = 0.0f;
	for (int32 cell = 0; cell < NUM_WING_CELLS; cell++)
		Total += CellLift[cell];
	return Total;
}

// Get total drag from all cells (Newtons)
float AWingActor::GetTotalCellDragN() const
{
	float Total = 0.0f;
	for (int32 cell = 0; cell < NUM_WING_CELLS; cell++)
		Total += CellDrag[cell];
	return Total;
}

// Get drag on left half
float AWingActor::GetLeftCellDragN() const
{
	float Total = 0.0f;
	for (int32 cell = 0; cell < NUM_WING_CELLS / 2; cell++)
		Total += CellDrag[cell];
	return Total;
}

// Get drag on right half
float AWingActor::GetRightCellDragN() const
{
	float Total = 0.0f;
	for (int32 cell = NUM_WING_CELLS / 2; cell < NUM_WING_CELLS; cell++)
		Total += CellDrag[cell];
	return Total;
}

// ============================================================
// CLOTH INITIALIZATION — from procedural paraglider mesh
// ============================================================

void AWingActor::InitCloth(){
	Particles.Empty();
Constraints.Empty();
TargetPositions.Empty();
// Initialize cell pressure arrays
for (int32 c = 0; c < NUM_WING_CELLS; c++)
{
	CellPressure[c] = 0.0f;
	CellFill[c] = 0.0f;
	CellLift[c] = 0.0f;
	CellDrag[c] = 0.0f;
}
// Create particles from the generated paraglider mesh vertices
for (int32 i = 0; i < PGWingVertCount; i++)	{
		FClothParticle P;
P.Pos = PGWingVertices[i];
P.OldPos = P.Pos;
P.bPinned = false;
P.bGrabbed = false;
Particles.Add(P);
}
	// Initialize target positions to match particles
	TargetPositions.SetNumZeroed(Particles.Num());
	for (int32 i = 0; i < Particles.Num() && i < TargetPositions.Num(); i++)
		TargetPositions[i] = Particles[i].Pos;
	// Create constraints from triangle edges (structural + shear)
TSet<uint32> EdgeSet;
for (int32 t = 0; t < PGWingTriCount; t++)	{
		int32 A = PGWingTriangles[t * 3];
int32 B = PGWingTriangles[t * 3 + 1];
int32 C = PGWingTriangles[t * 3 + 2];
uint32 E1 = (A < B) ? (A | (B << 16)) : (B | (A << 16));
uint32 E2 = (B < C) ? (B | (C << 16)) : (C | (B << 16));
uint32 E3 = (A < C) ? (A | (C << 16)) : (C | (A << 16));
auto AddEdge = [&](uint32 Key, int32 VA, int32 VB)		{
			if (!EdgeSet.Contains(Key))			{
				EdgeSet.Add(Key);
FClothConstraint C;
C.A = VA;
C.B = VB;
C.RestLength = (Particles[VA].Pos - Particles[VB].Pos).Size();
C.Stiffness = 1.0f;  // Inextensible ripstop nylon (Verlet constraint)
Constraints.Add(C);
}
};
AddEdge(E1, A, B);
AddEdge(E2, B, C);
AddEdge(E3, A, C);
}
	// Add cross-rib constraints to maintain airfoil shape
// Connect same chord position across adjacent ribs

for (int32 rib = 0; rib < PGNumRibs - 1; rib++)	{
		for (int32 p = 0; p < PGPointsPerRib; p += 4)  // Every 4th point
{
			// Top surface
int32 VA = GetRibVertexIndex(rib, p);
int32 VB = GetRibVertexIndex(rib + 1, p);
if (VA < Particles.Num() && VB < Particles.Num())			{
				FClothConstraint C;
C.A = VA;
C.B = VB;
C.RestLength = (Particles[VA].Pos - Particles[VB].Pos).Size();
C.Stiffness = 0.9f;  // Cross-rib: high stiffness (spanwise tension)
Constraints.Add(C);
}
// Bottom surface
			VA = GetRibVertexIndex(rib, PGPointsPerRib + p);
VB = GetRibVertexIndex(rib + 1, PGPointsPerRib + p);
if (VA < Particles.Num() && VB < Particles.Num())			{
				FClothConstraint C;
C.A = VA;
C.B = VB;
C.RestLength = (Particles[VA].Pos - Particles[VB].Pos).Size();
C.Stiffness = 0.8f;  // Cross-rib: slightly flexible (spanwise tension)
Constraints.Add(C);
}
}
}
	UE_LOG(LogTemp, Warning, TEXT("WingActor: InitCloth — %d particles, %d constraints"),		Particles.Num(), Constraints.Num());
}

// ============================================================
// CLOTH SIMULATION — Verlet integration
// ============================================================

void AWingActor::SimulateCloth(float DeltaTime)
{
	if (Particles.Num() == 0) return;

	FClothContext C;
	C.DeltaTime = DeltaTime;
	C.EffectiveDamping = Damping;
	if (WingState == EWingState::Spread || WingState == EWingState::Connected)
		C.EffectiveDamping = Damping * 0.95f;  // Air resistance on fabric
	if (WingState == EWingState::Inflating || WingState == EWingState::Inflated)
		C.EffectiveDamping = 0.95f;  // Air resistance on fabric
	C.bSkipCanopyPhysics = (WingState == EWingState::Inflating && bLiftingALines) ||
		(WingState == EWingState::Inflated || WingState == EWingState::Flying);
	C.CachedActorLoc = GetActorLocation();

	SimulateClothParticles(C);
	SimulateClothConstraints(C);

	if (WingState == EWingState::Connected)
		UpdateConnectedState(C);

	bool bInflationPhysics = (WingState == EWingState::Inflating) || (WingState == EWingState::Inflated);
	if (bInflationPhysics)
	{
		UpdateKitingGeometry(C);
		UpdateKitingForces(C);
		UpdateKitingYawDynamics(C);
		UpdateKitingIntegration(C);
		UpdateKitingCanopy(C);
		UpdateKitingStateTransitions(C);
		UpdateKitingRotation(C);
	}
}

// ============================================================
// HELPER 1: SimulateClothParticles
// Verlet integration + ground collision + grabbed particles
// ============================================================

void AWingActor::SimulateClothParticles(FClothContext& C)
{
	for (int32 i = 0; i < Particles.Num(); i++)
	{
		FClothParticle& P = Particles[i];
		if (P.bPinned || P.bGrabbed) continue;
		if (C.bSkipCanopyPhysics && i < PGWingVertCount) continue;
		FVector Vel = (P.Pos - P.OldPos) * C.EffectiveDamping;
		P.OldPos = P.Pos;
		P.Pos += Vel;
		P.Pos.Z -= Gravity * C.DeltaTime * C.DeltaTime;
		// Ground collision — P.Pos is LOCAL to actor, GZ is WORLD. Convert properly.
		float ActorZ = C.CachedActorLoc.Z;
		float GZ = GetCachedGroundZ(C.CachedActorLoc + P.Pos);
		float LocalGZ = GZ - ActorZ;  // Convert world ground Z to local space
		if (P.Pos.Z < LocalGZ)
		{
			P.Pos.Z = LocalGZ;
			P.OldPos.Z = LocalGZ;
		}
	}

	// Grabbed particles follow hand
	for (int32 i = 0; i < Particles.Num(); i++)
	{
		FClothParticle& P = Particles[i];
		if (P.bGrabbed)
		{
			FVector Target = GrabHandPos - C.CachedActorLoc;
			P.OldPos = P.Pos;
			P.Pos = Target;
		}
	}
}

// ============================================================
// HELPER 2: SimulateClothConstraints
// Constraint solving loop (skipped when bSkipCanopyPhysics)
// ============================================================

void AWingActor::SimulateClothConstraints(FClothContext& C)
{
	// Constraint solving — skip during inflation AND inflated/flying
	// When inflated, the canopy shape is set by SetInflatedShape() and maintained directly.
	// Running constraints would deform it back toward the flat laid-out shape.
	float ConstraintStiffnessScale = 1.0f;
	if (!C.bSkipCanopyPhysics)
	{
		for (int32 iter = 0; iter < ConstraintIterations; iter++)
		{
			for (const FClothConstraint& Con : Constraints)
			{
				if (Con.A >= Particles.Num() || Con.B >= Particles.Num()) continue;
				FClothParticle& PA = Particles[Con.A];
				FClothParticle& PB = Particles[Con.B];
				FVector Delta = PB.Pos - PA.Pos;
				float Dist = Delta.Size();
				if (Dist < 0.001f) continue;
				float EffectiveStiffness = Con.Stiffness * ConstraintStiffnessScale;
				float Diff = (Dist - Con.RestLength) / Dist * EffectiveStiffness * 0.5f;
				FVector Offset = Delta * Diff;
				if (!PA.bPinned && !PA.bGrabbed) PA.Pos += Offset;
				if (!PB.bPinned && !PB.bGrabbed) PB.Pos -= Offset;
			}
		}
	}
}

// ============================================================
// HELPER 3: UpdateConnectedState
// Connected state cell pressure + position dynamics + line constraint
// Also handles Connected -> Inflating transition
// ============================================================

void AWingActor::UpdateConnectedState(FClothContext& C)
{
	// === CELL PRESSURE FOR CONNECTED STATE ===
	// Even when just connected (not yet inflating), cells can start filling
	// if there's enough wind. This is what triggers the natural state transition.
	float WindMs = FMath::Max(0.0f, CurrentWindSpeed);
	FVector ShiftedWindDir = WindDirection.RotateAngleAxis(WindDirShift * RAD_TO_DEG, FVector(0, 0, 1));
	FVector WindVelVec = ShiftedWindDir * WindMs * CM_PER_M;
	// Account for wing's own velocity (apparent wind = wind - wing velocity)
	FVector ApparentWindVec = WindVelVec - WingGroundVel;
	float ApparentWindMs = ApparentWindVec.Size() / CM_PER_M;
	UpdateCellPressure(C.DeltaTime, ApparentWindMs, 0.0f, 0.0f);

	// === CONNECTED STATE POSITION DYNAMICS ===
	// The wing is a physical object on the ground with inertia and air resistance.
	// It does NOT instantly follow the pilot — it resists being dragged.
	// Forces: wind drag, line tension, ground friction, air damping
	FVector CarabMid = (LeftCarabinerPos + RightCarabinerPos) * 0.5f;
	FVector WingPos = GetActorLocation();
	FVector ToWing = WingPos - CarabMid;
	ToWing.Z = 0.0f;
	float DistFromCarab = ToWing.Size();

	// Wing mass (fabric only when deflated, + air mass when partially filled)
	float WingMassKg = WingModel.WingWeight;
	float ApparentMassKg = AIR_DENSITY_SEA_LEVEL * WingModel.WingArea * (FMath::Sqrt(WingModel.WingArea * WingModel.AR) * 0.5f) * CellFillFactor * 0.5f;
	float EffectiveMassKg = WingMassKg + ApparentMassKg;

	// Wind drag: pushes the wing downwind
	float WindDragArea = WingModel.WingArea * (1.0f - CellFillFactor * 0.7f);  // Projected area varies with fill  // Less area when inflated
	float WindDragN = 0.5f * AIR_DENSITY_SEA_LEVEL * WindMs * WindMs * WindDragArea * 1.2f;
	FVector WindDragForce = ShiftedWindDir.GetSafeNormal() * WindDragN * CM_PER_M / EffectiveMassKg;

	// Line tension: gentle spring force (stable at 60fps).
	// Real paracord stiffness (250000 N/m) is too stiff for 60fps Verlet,
	// so we use a moderate spring + velocity damping. The hard position
	// clamp is applied after integration (see below).
	FVector LineTensionForce = FVector::ZeroVector;
	float MaxLineDist = LINE_LENGTH_CM * 0.7f;
	if (DistFromCarab > MaxLineDist)
	{
		float Excess = DistFromCarab - MaxLineDist;
		FVector TensionDir = -ToWing / DistFromCarab;
		// Moderate spring: 500 cm/s² per cm of stretch (stable at 60fps)
		LineTensionForce = TensionDir * Excess * 500.0f;
	}

	// Air damping: resists motion through the air
	FVector WingVel2D = FVector(WingGroundVel.X, WingGroundVel.Y, 0.0f);
	float WingSpeed = WingVel2D.Size() / CM_PER_M;
	float AirDampCoeff = 0.5f * AIR_DENSITY_SEA_LEVEL * WingModel.WingArea * 1.2f / EffectiveMassKg;  // CD=1.2
	FVector AirDamping = -WingVel2D * AirDampCoeff * (1.0f + CellFillFactor);

	// Ground friction: μ × N for fabric on grass (μ ≈ 0.4)
	float FrictionMu = 0.4f;  // Fabric on grass
	float WingNormalN = EffectiveMassKg * GRAVITY_MS2;  // Weight = normal force on ground
	FVector GroundFrictionForce = -WingVel2D.GetSafeNormal() * FrictionMu * WingNormalN / EffectiveMassKg * CM_PER_M;

	// Gravity (keeps wing on ground)
	FVector GravityForce = FVector(0, 0, -GRAVITY_CM_S2);

	// Integrate forces
	FVector TotalForce = WindDragForce + LineTensionForce + AirDamping + GroundFrictionForce + GravityForce;
	WingGroundVel += TotalForce * C.DeltaTime;

	// Ground collision: wing stays on ground
	FVector NewPos = WingPos + WingGroundVel * C.DeltaTime;
	float GZ = GetCachedGroundZ(NewPos);
	if (NewPos.Z < GZ)
	{
		NewPos.Z = GZ;
		WingGroundVel.Z = 0.0f;
	}

	// Hard line-length constraint: clamp wing to line distance from carabiner
	FVector NewToWing = NewPos - CarabMid;
	NewToWing.Z = 0.0f;
	float NewDist = NewToWing.Size();
	if (NewDist > MaxLineDist)
	{
		FVector ClampDir = NewToWing / NewDist;
		NewPos = CarabMid + ClampDir * MaxLineDist;
		NewPos.Z = FMath::Max(NewPos.Z, GZ);  // keep above ground
		// Kill outward velocity
		float OutwardVel = FVector::DotProduct(WingGroundVel, ClampDir);
		if (OutwardVel > 0.0f)
			WingGroundVel -= ClampDir * OutwardVel;
	}

	SetActorLocation(NewPos, false);

	// === NATURAL STATE TRANSITION: Connected -> Inflating ===
	// The wing only transitions to Inflating when the pilot pulls A-lines (R key).
	// Without A-lines, the leading edge intakes are closed and wind alone
	// cannot fill the cells enough to inflate. This is realistic — you must
	// actively pull the A-lines to start inflation.
	if (WingState == EWingState::Connected && bLiftingALines)
	{
		// Check if cells have started filling from A-line pull + wind
		float AvgFill = 0.0f;
		for (int32 c = 0; c < NUM_WING_CELLS; c++)
			AvgFill += CellFill[c];
		AvgFill /= NUM_WING_CELLS;
		// Transition when cells start filling from A-line pull
		if (AvgFill > 0.05f)
		{
			WingState = EWingState::Inflating;
			// Transfer ground velocity to flight velocity for continuity
			WingVel = WingGroundVel;
			WingGroundVel = FVector::ZeroVector;
			UE_LOG(LogTemp, Warning, TEXT("WingActor: Inflation started from A-line pull (fill=%.0f%%, wind=%.1f m/s)"),
				AvgFill * CM_PER_M, CurrentWindSpeed);
		}
	}
}

// ============================================================
// HELPER 4: UpdateKitingGeometry
// Pilot frame, apparent wind, geometry, line tension fraction,
// cell pressure, A-line fade, wing mass
// ============================================================

void AWingActor::UpdateKitingGeometry(FClothContext& C)
{
	// === PILOT FRAME ===
	C.PilotVel = FVector::ZeroVector;
	C.PilotFwdDir = FVector::ForwardVector;
	C.SpeedMs = 0.0f;
	if (ConnectedParamotor)
	{
		APawn* Pawn = Cast<APawn>(ConnectedParamotor->GetAttachParentActor());
		if (Pawn)
		{
			C.PilotVel = Pawn->GetVelocity() / CM_PER_M;
			C.SpeedMs = C.PilotVel.Size();
			C.PilotFwdDir = Pawn->GetActorForwardVector();
			C.PilotFwdDir.Z = 0;
			C.PilotFwdDir.Normalize();
		}
	}

	// === APPARENT WIND (true vector calculation) ===
	// Apparent wind = true wind vector - wing velocity vector
	// This is the actual air velocity at the wing's position.
	// If the wing is moving with the wind, apparent wind decreases.
	// If the wing is moving into the wind, apparent wind increases.
	C.WindMs = FMath::Max(0.0f, CurrentWindSpeed);
	C.ShiftedWindDir = WindDirection.RotateAngleAxis(WindDirShift * RAD_TO_DEG, FVector(0, 0, 1));
	C.WindVelVec = C.ShiftedWindDir * C.WindMs * CM_PER_M;  // cm/s
	C.ApparentWindVec = C.WindVelVec - WingVel;  // cm/s
	C.ApparentWindMs = C.ApparentWindVec.Size() / CM_PER_M;  // m/s

	// === GEOMETRY (needed for line tension BEFORE cell pressure) ===
	C.CarabMid = (LeftCarabinerPos + RightCarabinerPos) * 0.5f;
	C.WingPos = GetActorLocation();
	C.ToWingFromCarab = C.WingPos - C.CarabMid;
	C.HorizToWing = FVector(C.ToWingFromCarab.X, C.ToWingFromCarab.Y, 0.0f);
	C.HorizDist = C.HorizToWing.Size();
	C.HeightAboveCarab = C.WingPos.Z - C.CarabMid.Z;
	C.DistFromCarab = C.ToWingFromCarab.Size();
	C.GroundZ = GetCachedGroundZ(C.WingPos);
	C.HeightAboveGround = C.WingPos.Z - C.GroundZ;
	C.PendAngle = FMath::Atan2(C.HorizDist, FMath::Max(1.0f, FMath::Abs(C.HeightAboveCarab)));

	// === LINE TENSION FRACTION (must be computed BEFORE cell pressure) ===
	// Lines can only pull — they're taut when the wing is at line length,
	// slack when the wing is closer. This is THE key factor in canopy shape:
	// - Taut lines pull the bottom skin down -> airfoil shape
	// - Slack lines -> no tension -> wing billows like a parachute
	// - No tension at all -> wing deflates, loses all shape
	float LineSlack = FMath::Clamp((LINE_LENGTH_CM - C.DistFromCarab) / LINE_LENGTH_CM, 0.0f, 1.0f);
	// Tension also requires the wing to be above the carabiner (gravity loading)
	float GravityLoading = FMath::Clamp(C.HeightAboveCarab / CM_PER_M, 0.0f, 1.0f);
	// Target tension: 1.0 when at line length and above carabiner, 0 when slack or low
	float TargetTension = (1.0f - LineSlack) * GravityLoading;
	// Smooth the tension transition (lines don't snap taut instantly)
	LineTensionFraction = FMath::FInterpTo(LineTensionFraction, TargetTension, C.DeltaTime, 10.0f);

	// === CELL PRESSURE (fills cells, computes lift/drag per cell) ===
	// Now uses the current frame's LineTensionFraction (computed above)
	UpdateCellPressure(C.DeltaTime, C.ApparentWindMs, LeftBrakeAmount, RightBrakeAmount);

	// === A-LINE FADE ===
	// During Inflating: full A-line pull
	// After transition to Inflated: fade out over 0.5s (prevents sudden sag)
	if (WingState == EWingState::Inflating && bLiftingALines)
		ALineFadeFactor = 1.0f;
	else
		ALineFadeFactor = FMath::Max(0.0f, ALineFadeFactor - C.DeltaTime);  // Fade over 0.5s

	// === WING MASS ===
	// Research: effective canopy inertia = 3-4x wing weight for 25m² wing
	// (fabric + enclosed air + added mass of air dragged along)
	// IMPORTANT: Apparent/added mass is an INERTIAL effect (resists acceleration),
	// NOT weight. Gravity only pulls on the actual mass (fabric + air inside cells).
	C.Rho = AIR_DENSITY_SEA_LEVEL;
	C.WingMassKg = WingModel.WingWeight;
	// Apparent mass scales with fill: empty wing = just fabric, full = fabric + air
	C.ApparentMassKg = C.WingMassKg * 0.5f + C.WingMassKg * 3.0f * CellFillFactor;
	C.EffectiveMassKg = C.WingMassKg + C.ApparentMassKg;  // Used for F=ma (inertia)
	C.WingWeightN = C.WingMassKg * GRAVITY_MS2;  // Used for F=mg (gravity only on real mass)
}

// ============================================================
// HELPER 5: UpdateKitingForces
// All 9 forces + inertial lag + brake surge
// ============================================================

void AWingActor::UpdateKitingForces(FClothContext& C)
{
	// === FORCE 1: A-LINE PULL (pilot arm force on leading edge) ===
	// In real life, the pilot pulls the A-risers to lift the leading edge
	// and orient the cell intakes into the wind. This is a real force from
	// the pilot's arms, limited by human strength (~100N per arm, ~200N total).
	// The force is applied along the riser direction (toward pilot hands),
	// NOT straight up. It lifts the LE, which catches wind -> inflation.
	// If there's no wind, this force just lifts the fabric but it won't inflate.
	// The force fades as the wing rises (pilot can't reach as high).
	float PullFraction = FMath::Clamp(1.0f - C.HeightAboveGround / (LINE_LENGTH_CM * 1.5f), 0.0f, 1.0f);
	float PilotArmForceN = 200.0f;  // ~200N total (both arms pulling A-risers)
	C.ALinesPullN = PilotArmForceN * PullFraction * ALineFadeFactor;
	// Force directed from wing toward carabiner (pilot hands), plus upward component
	// This is the real riser geometry — the force pulls the LE toward the pilot
	FVector PullDir = FVector(0, 0, 1.0f);  // Default: straight up (wing on ground)
	if (C.HorizDist > 1.0f)
	{
		FVector ToCarabHoriz = -C.HorizToWing / C.HorizDist;
		// Riser direction: mostly toward pilot with some upward component
		// This lifts the LE and angles the intakes into the wind
		PullDir = (ToCarabHoriz * 0.5f + FVector(0, 0, 1.0f)).GetSafeNormal();
	}
	C.ALinesPullForce = PullDir * (C.ALinesPullN / C.EffectiveMassKg) * CM_PER_M;

	// === FORCE 2: WIND CATCH + SAIL EFFECT (lifts fabric off ground) ===
	// Two components:
	// a) Ram-air pressure on cell intakes (internal, pressurizes cells)
	// b) SAIL EFFECT: wind pushes on the entire wing surface like a parachute/sail.
	//    This is what physically lifts the fabric off the ground during inflation.
	//    A 25m² wing in 6 m/s wind catches ~150N of sail force — more than enough
	//    to lift a 5.6kg wing (55N). This is why the wing "shoots up" in real life.
	// The sail effect fades as cells fill (inflated wing becomes an airfoil, not a sail).
	float UnfilledFraction = 1.0f - CellFillFactor;
	// (a) Ram-air intake pressure (small but drives cell fill)
	float IntakeAreaPerCell = WingModel.WingArea / NUM_WING_CELLS * 0.08f;
	float IntakeArea = IntakeAreaPerCell * NUM_WING_CELLS * UnfilledFraction;
	IntakeArea *= (bLiftingALines ? 1.3f : 1.0f);
	float RamPressureN = 0.5f * C.Rho * C.ApparentWindMs * C.ApparentWindMs * IntakeArea;
	// (b) Sail effect: wind drag on the full wing surface (unfilled portion only)
	// CD_sail ≈ 1.2 (parachute/sail drag coefficient for fabric)
	// The wing is lying on the ground face-up, so wind hits the full area.
	float SailArea = WingModel.WingArea * UnfilledFraction;
	float SailCD = 1.2f;  // Fabric sail drag coefficient
	float SailForceN = 0.5f * C.Rho * C.ApparentWindMs * C.ApparentWindMs * SailArea * SailCD;
	// Total wind catch = ram pressure + sail effect
	C.WindCatchN = RamPressureN + SailForceN;
	C.WindCatchForce = FVector(0, 0, (C.WindCatchN / C.EffectiveMassKg) * CM_PER_M);

	// === FORCE 3: CELL LIFT (aerodynamic lift from pressurized airfoil) ===
	C.TotalLiftN = GetTotalCellLiftN();
	C.TotalDragN = GetTotalCellDragN();
	C.CellLiftForce = FVector(0, 0, (C.TotalLiftN / C.EffectiveMassKg) * CM_PER_M);

	// === FORCE 4: GRAVITY ===
	C.GravityForce = FVector(0, 0, -(C.WingWeightN / C.EffectiveMassKg) * CM_PER_M);

	// === FORCE 5: PENDULUM RESTORING (gravity toward zenith) ===
	// F = m * g * sin(θ) toward zenith. This is pure gravity on the wing mass.
	// The restoring acceleration = g × sin(θ) × (WingMass / EffectiveMass)
	// because only the real mass is pulled by gravity, but the effective mass
	// (including apparent air mass) resists the acceleration.
	C.PendulumForce = FVector::ZeroVector;
	if (C.HorizDist > 1.0f)
	{
		FVector ToCarabHoriz = -C.HorizToWing / C.HorizDist;
		float MassRatio = C.WingMassKg / FMath::Max(0.1f, C.EffectiveMassKg);
		float PendRestoring = FMath::Sin(C.PendAngle) * GRAVITY_CM_S2 * MassRatio;
		C.PendulumForce = ToCarabHoriz * PendRestoring;
	}

	// === FORCE 6: WIND DRAG (pushes wing downwind) ===
	// Drag from the cell pressure model, applied in the wind direction.
	// At zenith the wing is edge-on to the wind (thin profile), so effective
	// drag area is small. When the wing is lower (more surface exposed), drag is higher.
	// ShiftedWindDir already computed above for apparent wind
	float WindDragAccel = (C.TotalDragN / C.EffectiveMassKg) * CM_PER_M;
	// Wing exposure to wind: 1.0 at horizon (full face), 0.3 at zenith (edge-on)
	float WindExposure = FMath::Sin(C.PendAngle);
	C.WindDragForce = C.ShiftedWindDir * WindDragAccel * WindExposure;

	// === FORCE 7: BRAKE DAMPING ===
	// Brakes add drag which damps wing motion. Real physics: brake deflection
	// increases the wing's drag coefficient, which creates a drag force
	// opposing motion. F_drag = 0.5 × ρ × V² × S × ΔCD_brake
	// At full brake: ΔCD ≈ 2.0 (from cell pressure model), so drag is 3x base.
	C.TotalBrake = (LeftBrakeAmount + RightBrakeAmount) * 0.5f;
	float BrakeDragCD = C.TotalBrake * 1.0f;  // From cambered polar drag increase  // Brake-induced drag coefficient increase
	float BrakeVelMs = WingVel.Size() / CM_PER_M;
	float BrakeDragN = 0.5f * C.Rho * BrakeVelMs * BrakeVelMs * WingModel.WingArea * BrakeDragCD;
	FVector BrakeDragDir = (WingVel.SizeSquared() > 1.0f) ? -WingVel.GetSafeNormal() : FVector::ZeroVector;
	C.BrakeDampForce = BrakeDragDir * (BrakeDragN / C.EffectiveMassKg) * CM_PER_M;

	// === FORCE 8: AIR DAMPING (increases with apparent wind = stabilizes at speed) ===
	// Real wings have LOW damping at low speed (they oscillate, pendulum, respond to gusts)
	// and HIGH damping at high speed (stable, smooth flight).
	// Physics: this is the wing's profile drag opposing its motion through the air.
	// F_drag = 0.5 × ρ × V² × S × CD_profile
	// The linear damping term approximates the derivative of drag w.r.t. velocity.
	float VelMs = WingVel.Size() / CM_PER_M;
	float EdgeOnArea = WingModel.WingArea * 0.05f;  // Thin wing edge-on  // Edge-on profile (wing moving sideways)
	float QuadraticDragN = 0.5f * C.Rho * VelMs * VelMs * EdgeOnArea * WingModel.CD0;
	FVector DragDir = (WingVel.SizeSquared() > 1.0f) ? -WingVel.GetSafeNormal() : FVector::ZeroVector;
	C.AirDamping = DragDir * (QuadraticDragN / C.EffectiveMassKg) * CM_PER_M;
	// Linear damping: dF/dV = ρ × V × S × CD (derivative of quadratic drag)
	// This is the natural velocity-proportional damping from aerodynamic drag
	float LinearDampCoeff = C.Rho * C.ApparentWindMs * WingModel.WingArea * WingModel.CD0 / C.EffectiveMassKg;
	// No cap — the physics naturally bounds this (high speed = high damping)
	C.AirDamping += -WingVel * LinearDampCoeff;

	// === FORCE 9: TURBULENCE (from von Kármán states, not per-frame random) ===
	// Uses the 3-axis turbulence states from UpdateDynamicWind (Dryden/von Kármán).
	// These have proper temporal correlation — gusts build up and decay naturally.
	// Physics: turbulence is a wind velocity variation. The force on the wing
	// is the aerodynamic drag from that velocity variation:
	// F = 0.5 × ρ × ΔV² × S × CD
	C.PilotRight = FVector::CrossProduct(FVector::UpVector, C.PilotFwdDir).GetSafeNormal();
	float TurbLongMs = FMath::Abs(TurbLongState);
	float TurbLatMs = FMath::Abs(TurbLatState);
	// Drag from turbulence wind variation on the wing
	float TurbLongDragN = 0.5f * C.Rho * TurbLongMs * TurbLongMs * WingModel.WingArea * WingModel.CD0;
	float TurbLatDragN = 0.5f * C.Rho * TurbLatMs * TurbLatMs * WingModel.WingArea * 0.4f;
	// Turbulence force: drag from turbulent wind variations, applied in the
	// direction of each component independently (no combined sign flip)
	C.TurbulenceForce = C.ShiftedWindDir * (TurbLongDragN / C.EffectiveMassKg) * CM_PER_M * FMath::Sign(TurbLongState)
	                  + C.PilotRight * (TurbLatDragN / C.EffectiveMassKg) * CM_PER_M * FMath::Sign(TurbLatState);
	// Turbulence effect decreases with airspeed (more damping from apparent wind)
	// No extra damping - gust force already proportional to dV^2

	// === INERTIAL LAG (wing lags behind pilot acceleration) ===
	// Real physics: the wing has inertia (EffectiveMassKg) and the pilot
	// accelerates. The wing feels a pseudo-force from the pilot's acceleration.
	// F = -m_wing × a_pilot (in the pilot's frame, the wing feels -a_pilot)
	// This is what causes overshoot: when pilot stops, a_pilot is negative
	// (decelerating), so the wing feels a forward force and overshoots.
	// When pilot accelerates, wing falls behind (lags).
	// The mass ratio determines how much the wing responds — in reality
	// the wing+pilot system is a double pendulum and the wing swings freely.
	FVector PilotHorizVel = FVector(C.PilotVel.X, C.PilotVel.Y, 0.0f);
	FVector PilotAccel = (PilotHorizVel - PrevPilotVel) / FMath::Max(C.DeltaTime, 0.02f);
	// Use full effective mass ratio — the wing swings as a free pendulum
	// When pilot decelerates from 3 m/s to 0 in 0.3s, that's -10 m/s²
	// Force on wing = m_eff × 10 = ~170N → significant overshoot
	C.InertialLagForce = -PilotAccel * CM_PER_M;
	PrevPilotVel = PilotHorizVel;

	// === BRAKE SURGE (forward force when brakes released) ===
	// When brakes are released, drag drops suddenly. The wing still has momentum
	// from the brake drag pulling it back, so it surges forward.
	// This is a FORCE, not a velocity impulse — it integrates over time.
	C.BrakeSurgeForce = FVector::ZeroVector;
	if (C.TotalBrake < PrevSymBrake - 0.01f)
	{
		float ReleaseAmount = PrevSymBrake - C.TotalBrake;
		// Surge = drag reduction from brake release.
		// ΔF = 0.5 × ρ × V² × S × ΔCD_brake (the drag that disappeared)
		float SurgeDragN = 0.5f * C.Rho * C.ApparentWindMs * C.ApparentWindMs * WingModel.WingArea * ReleaseAmount;
		C.BrakeSurgeForce = C.PilotFwdDir * (SurgeDragN / C.EffectiveMassKg) * CM_PER_M;
	}
	PrevSymBrake = C.TotalBrake;
}

// ============================================================
// HELPER 6: UpdateKitingYawDynamics
// Per-side stall dynamics, yaw from drag differential + weathervane,
// yawed flight force
// ============================================================

void AWingActor::UpdateKitingYawDynamics(FClothContext& C)
{
	// === PER-SIDE STALL DYNAMICS (brake stalls one side) ===
	// Brake input increases stall on that side. Stalled side loses lift, gains drag.
	// Real wings don't start stalling until 60-70% brake — below that, brakes
	// just increase camber (more lift + more drag) without stalling.
	// Full stall occurs at 80-90% brake held for 1-2 seconds.
	// At full stall: CL drops to ~0 (completely separated flow), CD jumps 5x.
	// This is what kills the wing when you pull both brakes hard — it falls.
	// Stall onset is fast (flow separation in ~0.1s at high AoA)
	// Recovery depends on cell refill time (air must re-enter and pressurize)
	// Cell refill: τ ≈ 1/fill_rate, typically 0.8-1.5s
	float StallRate = FMath::Max(1.0f, C.ApparentWindMs * 0.5f);     // Flow separation is fast (~0.1s to full stall)
	float RecoveryRate = 1.0f / FMath::Max(0.5f, 5.0f / FMath::Max(0.5f, C.ApparentWindMs));  // Cell refill rate
	float LocalStallAoA = WingModel.StallAoA;
	float LocalLeftAoA = 8.0f + TrailingEdgeDeflectionDeg(LeftBrakeAmount);
	float LocalRightAoA = 8.0f + TrailingEdgeDeflectionDeg(RightBrakeAmount);
	if (LocalLeftAoA > LocalStallAoA)
		KiteLeftStall = FMath::Min(1.0f, KiteLeftStall + C.DeltaTime * StallRate * FMath::Clamp((LocalLeftAoA - LocalStallAoA) / 5.0f, 0.0f, 1.0f));
	else
		KiteLeftStall = FMath::Max(0.0f, KiteLeftStall - C.DeltaTime * RecoveryRate);
	if (LocalRightAoA > LocalStallAoA)
		KiteRightStall = FMath::Min(1.0f, KiteRightStall + C.DeltaTime * StallRate * FMath::Clamp((LocalRightAoA - LocalStallAoA) / 5.0f, 0.0f, 1.0f));
	else
		KiteRightStall = FMath::Max(0.0f, KiteRightStall - C.DeltaTime * RecoveryRate);

	// === KITING YAW (from per-side drag differential + weathervane) ===
	// Computed BEFORE force integration so yawed flight force can be added.
	// PilotRight already computed above for turbulence
	float CrosswindComponent = FVector::DotProduct(C.ShiftedWindDir, C.PilotRight);
	float DynPressure = 0.5f * C.Rho * C.WindMs * C.WindMs;
	float WingspanM = FMath::Sqrt(WingModel.WingArea * WingModel.AR);
	float WingInertiaYaw = C.EffectiveMassKg * (WingspanM * 0.5f) * (WingspanM * 0.5f);

	// Weathervane: wing aligns LE into wind
	// Physics: yaw moment from crosswind pressure on the wing
	// M = C_N × q × S × b × β (yawing moment coefficient × dyn pressure × area × span × sideslip)
	float WeathervaneMoment = 0.05f * DynPressure * WingModel.WingArea * WingspanM * CrosswindComponent;  // C_N_beta=0.05
	// No extra speed scaling - q already includes V^2
	float WeathervaneForce = (WingInertiaYaw > 0.1f) ? WeathervaneMoment / WingInertiaYaw * RAD_TO_DEG : 0.0f;
	WeathervaneForce *= (1.0f - FrontalCollapseAmount);
	// Yaw turbulence: crosswind turbulence creates yaw moment
	// M_turb = q × S × b × Δβ_turb (sideslip from turbulent crosswind)
	float TurbSideslip = (C.ApparentWindMs > 1.0f) ? TurbLatState / C.ApparentWindMs : 0.0f;
	float YawTurbMoment = DynPressure * WingModel.WingArea * WingspanM * TurbSideslip * 0.05f;
	float YawTurbForce = (WingInertiaYaw > 0.1f) ? YawTurbMoment / WingInertiaYaw * RAD_TO_DEG : 0.0f;
	// No extra damping - aerodynamic yaw damping handles it

	// Per-side drag from cell pressure model (brake increases drag on braked side)
	KiteLeftDrag = GetLeftCellDragN();
	KiteRightDrag = GetRightCellDragN();
	// Add stall drag: bluff-body CD on stalled area fraction (same as flight collapse)
	// F = 0.5 × ρ × V² × S_stalled × CD_bluff, CD_bluff ≈ 1.0
	KiteLeftDrag += KiteLeftStall * 0.5f * C.Rho * C.ApparentWindMs * C.ApparentWindMs * WingModel.WingArea * 0.5f * 1.0f;
	KiteRightDrag += KiteRightStall * 0.5f * C.Rho * C.ApparentWindMs * C.ApparentWindMs * WingModel.WingArea * 0.5f * 1.0f;

	float DragDiffN = KiteRightDrag - KiteLeftDrag;
	float YawMoment = DragDiffN * (WingspanM * 0.5f);
	float YawAccel = (WingInertiaYaw > 0.1f) ? YawMoment / WingInertiaYaw * RAD_TO_DEG : 0.0f;

	// Yaw restoring: from weathervane moment (already computed above as WeathervaneForce)
	// No separate spring needed — the weathervane IS the restoring force
	float YawRestoring = 0.0f;  // WeathervaneForce already includes restoring
	// Yaw damping from aerodynamic drag on wing surfaces
	// c_yaw = 0.5 * rho * V * S * b^2 / I_yaw (derivative of drag moment)
	float YawDampCoeff = (WingInertiaYaw > 0.1f)
		? 0.5f * C.Rho * C.ApparentWindMs * WingModel.WingArea * WingspanM * WingspanM / WingInertiaYaw
		: 0.0f;
	// No cap — the physical formula naturally bounds the damping coefficient
	float YawDamping = -KiteYawVel * YawDampCoeff;
	KiteYawVel += (YawAccel + WeathervaneForce + YawTurbForce + YawRestoring + YawDamping) * C.DeltaTime;
	KiteYawVel = FMath::Clamp(KiteYawVel, -360.0f, 360.0f);  // NaN safety only
	KiteYawDrift += KiteYawVel * C.DeltaTime;
	// No clamp — wing can yaw freely (reverse launch needs full 180° range)
	// Normalize to [-180, 180]
	while (KiteYawDrift > 180.0f) KiteYawDrift -= 360.0f;
	while (KiteYawDrift < -180.0f) KiteYawDrift += 360.0f;

	// === YAWED FLIGHT FORCE (yawed wing flies sideways) ===
	// When the wing yaws, its aerodynamic force vector rotates.
	// A yawed wing generates lift perpendicular to its span.
	// When yawed, the lift vector has a lateral component → wing moves sideways.
	// This is the PHYSICAL effect of brake steering: yaw → lateral force → movement.
	float YawRad = KiteYawDrift * PI_DIV_180;
	float AeroLiftN = 0.5f * C.Rho * C.ApparentWindMs * C.ApparentWindMs * WingModel.WingArea * WingModel.CL0 * CellFillFactor;
	float LateralAeroAccel = (AeroLiftN / C.EffectiveMassKg) * CM_PER_M * FMath::Sin(YawRad);
	C.YawedFlightForce = C.PilotRight * LateralAeroAccel;
}

// ============================================================
// HELPER 7: UpdateKitingIntegration
// Force integration, velocity damping, line constraint, ground collision,
// SetActorLocation, line tension pulls pilot
// ============================================================

void AWingActor::UpdateKitingIntegration(FClothContext& C)
{
	// === INTEGRATE FORCES ===
	FVector TotalForce = C.ALinesPullForce + C.WindCatchForce + C.CellLiftForce
		+ C.GravityForce + C.PendulumForce + C.WindDragForce + C.BrakeDampForce
		+ C.AirDamping + C.TurbulenceForce + C.InertialLagForce + C.YawedFlightForce + C.BrakeSurgeForce;
	WingVel += TotalForce * C.DeltaTime;

	// Velocity damping — stronger during inflated/kiting to prevent jitter.
	// Real wings have significant aerodynamic damping from their large surface area.
	// At kiting speed (6 m/s apparent wind), the wing's profile drag provides
	// substantial damping. The previous 0.5/s was too low — the wing oscillated
	// visibly from turbulence and pendulum forces.
	// 3.0/s gives ~0.33s decay — smooth but still allows pendulum swing.
	float DampRate = (WingState == EWingState::Inflated) ? 3.0f : 0.5f;
	WingVel *= FMath::Clamp(1.0f - DampRate * C.DeltaTime, 0.8f, 1.0f);

	// === LINE CONSTRAINT (always active when wing is off ground) ===
	// Lines can only pull — they stop the wing from going BEYOND line length.
	// When the wing is at line length, only tangential velocity is preserved.
	// When closer than line length, lines are slack (free movement).
	FVector NewWingPos = C.WingPos + WingVel * C.DeltaTime;
	C.ToNewPos = NewWingPos - C.CarabMid;
	float NewDist = C.ToNewPos.Size();
	bool bLineTaut = (NewDist > LINE_LENGTH_CM);
	if (bLineTaut)
	{
		FVector LineDir = C.ToNewPos / NewDist;
		NewWingPos = C.CarabMid + LineDir * LINE_LENGTH_CM;
		float VelAlongLine = FVector::DotProduct(WingVel, LineDir);
		if (VelAlongLine > 0.0f)
			WingVel -= LineDir * VelAlongLine;
	}

	// === GROUND COLLISION ===
	float GZ = GetCachedGroundZ(NewWingPos);
	if (NewWingPos.Z < GZ)
	{
		NewWingPos.Z = GZ;
		WingVel.Z = FMath::Max(0.0f, WingVel.Z);
	}

	SetActorLocation(NewWingPos, false);

	// === LINE TENSION PULLS PILOT (through the lines) ===
	// The wing pulls on the pilot through the lines. This is a real force.
	// But it should be GENTLE — it's the line tension, not a direct velocity set.
	// The force is proportional to how much the wing is pulling (drag + lift).
	// When running into wind, the wing pulls you forward (downwind) slightly,
	// but your legs + thrust should easily overcome it.
	if (ConnectedParamotor)
	{
		APawn* Pawn = Cast<APawn>(ConnectedParamotor->GetAttachParentActor());
		ACharacter* Char = Cast<ACharacter>(Pawn);
		if (Char && !Char->GetCharacterMovement()->IsFlying() && !Char->GetCharacterMovement()->IsFalling())
		{
			// Line tension from wing drag — only when wing is actually pulling
			// (inflated or significantly inflated). A flat wing on the ground
			// barely pulls the pilot.
			float PullCD = 0.06f * CellFillFactor;  // Real inflated wing drag CD  // Much lower — was 0.08
			float PullN = 0.5f * C.Rho * C.WindMs * C.WindMs * WingModel.WingArea * PullCD;
			// No WindCatchN addition — that was double-counting
			float PullAccelCm = (PullN / PilotWeight) * CM_PER_M;
			// Apply as acceleration, not velocity override
			FVector PullAccel = C.ShiftedWindDir * PullAccelCm * C.DeltaTime;
			FVector CharVel = Char->GetCharacterMovement()->Velocity;
			CharVel.X += PullAccel.X;
			CharVel.Y += PullAccel.Y;
			// Don't clamp the pilot's velocity — let the movement component handle it
			Char->GetCharacterMovement()->Velocity = CharVel;
		}
	}
}

// ============================================================
// HELPER 8: UpdateKitingCanopy
// Pitch from pendulum, ApplyInflatedShape, spring-coupled Verlet
// dynamics for canopy
// ============================================================

void AWingActor::UpdateKitingCanopy(FClothContext& C)
{
	// === PITCH (from pendulum + inflation overshoot) ===
	// Pitch from pendulum geometry: wing pitch = atan(Vz / Vh)
	// When wing rises (Vz > 0), it pitches back (positive)
	// When wing falls (Vz < 0), it pitches forward (surge)
	float Vh = FVector(WingVel.X, WingVel.Y, 0.0f).Size();
	float PitchTarget = (Vh > 1.0f) ? FMath::Atan2(-WingVel.Z, Vh) * RAD_TO_DEG : 0.0f;
	// No clamp - pitch from force/moment balance
	KitePitchOvershoot = FMath::FInterpTo(KitePitchOvershoot, PitchTarget, C.DeltaTime, 5.0f);

	// === DEFORMABLE CANOPY SHAPE ===
	// ApplyInflatedShape() computes target positions into TargetPositions[].
	// Canopy particles are spring-coupled toward targets — they oscillate,
	// flutter, and respond to gusts like real fabric, instead of being
	// a rigid mathematical shape.
	ApplyInflatedShape(C.DeltaTime);

	// === SPRING-COUPLED VERLET DYNAMICS FOR CANOPY ===
	// Each canopy particle is pulled toward its target by a spring force.
	// The spring stiffness represents fabric tension.
	// Damping represents air resistance on the fabric.
	// Additional forces: gust flutter, apparent wind pressure, cell pressure bulge
	//
	// Spring system tuning:
	//   Natural frequency: ω = sqrt(k/m) = sqrt(30/1) = 5.5 rad/s = 0.87 Hz
	//   Critical damping:  c_crit = 2*sqrt(k*m) = 2*sqrt(30) = 10.95
	//   Damping ratio:     ζ = 9.0/10.95 = 0.82 (well-damped, no oscillation)
	float FabricStiffness = 40.0f;  // Ripstop nylon: E~3GPa, t~0.1mm, L~2cm
	float FabricDamping = 5.0f;  // Air damping on fabric surface
	float FlutterAmount = FMath::Min(1.0f, C.ApparentWindMs * 0.08f);  // More wind = more flutter

	for (int32 i = 0; i < PGWingVertCount && i < Particles.Num() && i < TargetPositions.Num(); i++)
	{
		FClothParticle& P = Particles[i];
		if (P.bPinned || P.bGrabbed) continue;

		FVector Target = TargetPositions[i];

		// Verlet velocity
		// Damping from air drag on fabric: c = 1 - exp(-dt * c_air)
		// c_air = 0.5 * rho * S_particle * CD / m_particle
		float FabricAirDamp = 1.0f - FMath::Exp(-C.DeltaTime * 5.0f);  // ~8/s from fabric air drag
		FVector Vel = (P.Pos - P.OldPos) * (1.0f - FabricAirDamp);
		P.OldPos = P.Pos;
		P.Pos += Vel;

		// Spring force toward target (fabric tension)
		FVector SpringForce = (Target - P.Pos) * FabricStiffness;

		// Damping (air resistance on fabric)
		FVector DampForce = -Vel * FabricDamping;

		// === FABRIC FLUTTER ===
		// Trailing edge flutters more than leading edge (real fabric behavior)
		// Driven by vertical turbulence (TurbVertState) — already von Kármán filtered,
		// smooth, and physically represents vertical air movement on the fabric.
		// NOT using WindTurbulence (longitudinal) for vertical forces — wrong axis.
		FVector FlutterForce = FVector::ZeroVector;
		if (FlutterAmount > 0.01f)
		{
			int32 LocalIdx = i % (2 * PGPointsPerRib);
			float ChordT = (float)(LocalIdx % PGPointsPerRib) / (float)(PGPointsPerRib - 1);
			// Trailing edge (ChordT > 0.6) flutters more
			float FlutterWeight = FMath::SmoothStep(0.7f, 1.0f, ChordT);  // TE 30% flutters
			// Per-vertex phase from span position + time (slow, natural ripple)
			int32 RibIdx = i / (2 * PGPointsPerRib);
			float Phase = GetWorld()->GetTimeSeconds() * 5.0f + RibIdx * 0.5f + ChordT * 2.0f;
			float FlutterZ = FMath::Sin(Phase) * FlutterAmount * FlutterWeight;
			// Vertical turbulence drives flutter (smooth, filtered, correct axis)
			FlutterZ += TurbVertState * FlutterWeight;
			FlutterForce = FVector(0, 0, FlutterZ);
		}

		// === GUST RESPONSE ===
		// Gusts push the fabric — leading edge responds to longitudinal turbulence
		// (wind speed variation changes ram pressure on the LE)
		FVector GustForce = FVector::ZeroVector;
		if (FMath::Abs(TurbLongState) > 0.01f)
		{
			int32 LocalIdx = i % (2 * PGPointsPerRib);
			bool bIsTopSurface = (LocalIdx < PGPointsPerRib);
			float ChordT = (float)(LocalIdx % PGPointsPerRib) / (float)(PGPointsPerRib - 1);
			// Leading edge responds to gusts (pressure changes)
			if (ChordT < 0.2f && bIsTopSurface)
			{
				float LEWeight = 1.0f - (ChordT / 0.2f);
				// Use TurbLongState (smooth, filtered) — positive gust = more pressure = LE up
				GustForce = FVector(0, 0, TurbLongState * LEWeight * C.Rho * CM_PER_M);
			}
		}

		// Integrate all forces
		P.Pos += (SpringForce + DampForce + FlutterForce + GustForce) * C.DeltaTime * C.DeltaTime;
	}
}

// ============================================================
// HELPER 9: UpdateKitingStateTransitions
// Inflation progress, state transitions (Inflating->Inflated,
// Inflated->Inflating, Inflated->Connected)
// ============================================================

void AWingActor::UpdateKitingStateTransitions(FClothContext& C)
{
	// === UPDATE INFLATION PROGRESS (height AND cell fill) ===
	if (WingState == EWingState::Inflating)
	{
		InflationHeight = FMath::Max(0.0f, C.HeightAboveCarab);
		float HeightProgress = FMath::Clamp(InflationHeight / LINE_LENGTH_CM, 0.0f, 1.0f);
		InflationProgress = FMath::Min(HeightProgress, CellFillFactor);

		if (!bLiftingALines)
		{
			// R released — decay inflation only if cells are actually losing fill.
			// Hysteresis: start inflation at 10% fill, only lose at <2% fill.
			// This prevents rapid oscillation between Connected and Inflating.
			if (CellFillFactor < 0.02f && InflationHeight < 50.0f)
			{
				// Cells nearly empty AND wing is low — lose inflation
				WingState = EWingState::Connected;
				InflationHeight = 0.0f;
				CellFillFactor = 0.0f;
				ALineFadeFactor = 0.0f;
				WingGroundVel = WingVel;  // Transfer velocity for continuity
				WingVel = FVector::ZeroVector;
				if (ConnectedParamotor)
				{
					APawn* Pawn = Cast<APawn>(ConnectedParamotor->GetAttachParentActor());
					if (Pawn)
					{
						ACharacter* Char = Cast<ACharacter>(Pawn);
						if (Char) Char->GetCharacterMovement()->GravityScale = 1.0f;
					}
				}
				UE_LOG(LogTemp, Warning, TEXT("WingActor: Inflation lost"));
			}
			// If cells still have fill (wind keeping them pressurized),
			// inflation continues naturally even without A-lines held.
		}

		// Transition to Inflated: requires height, cell fill, AND line tension
		// Without tension on the lines, the wing is just a billowing parachute,
		// not an airfoil. It needs to be at line length and above the pilot.
		if (InflationProgress >= 0.8f && CellFillFactor > 0.6f && LineTensionFraction > 0.3f)
		{
			WingState = EWingState::Inflated;
			InflationProgress = 1.0f;
			SetInflatedShape();
			UE_LOG(LogTemp, Warning, TEXT("WingActor: Wing INFLATED! fill=%.0f%% h=%.0fcm"), CellFillFactor * PERCENT_FACTOR, InflationHeight);
			// Initial overshoot: wing shoots forward slightly (apply impulse to WingVel)
			WingVel += C.PilotFwdDir * 150.0f;
			KitePitchOvershoot = 10.0f;
		}
	}

	// === KITING POST-INTEGRATION (only in Inflated state) ===
	// Yaw is computed before force integration (see above).
	// Here we just update compatibility variables and cell deflation.
	if (WingState == EWingState::Inflated)
	{
		FVector PilotRightYaw = FVector::CrossProduct(FVector::UpVector, C.PilotFwdDir).GetSafeNormal();
		// Update KiteOffset for compatibility
		FVector HorizOffset = FVector(C.ToNewPos.X, C.ToNewPos.Y, 0.0f);
		KiteOffset = FVector(
			FVector::DotProduct(HorizOffset, C.PilotFwdDir),
			FVector::DotProduct(HorizOffset, PilotRightYaw),
			0.0f
		);

		// Cell deflation when apparent wind is too low
		if (C.ApparentWindMs < 2.0f)
			CellFillFactor = FMath::Max(0.0f, CellFillFactor - C.DeltaTime * 0.5f);

		// === STATE TRANSITION BACK (deflation/collapse) ===
		// If cells deflate below 30%, the wing is no longer properly inflated.
		// Also if line tension drops (lines go slack), the wing loses its airfoil
		// shape and becomes a parachute — transition back to Inflating.
		if ((CellFillFactor < 0.2f || LineTensionFraction < 0.1f) && C.HeightAboveCarab < LINE_LENGTH_CM * 0.3f)
		{
			WingState = EWingState::Inflating;
			InflationProgress = CellFillFactor;
			ALineFadeFactor = 0.0f;
			UE_LOG(LogTemp, Warning, TEXT("WingActor: Deflating — back to Inflating (fill=%.0f%% h=%.0fcm)"), CellFillFactor * PERCENT_FACTOR, C.HeightAboveCarab);
		}
		// If wing falls to near ground with low fill, transition to Connected
		if (CellFillFactor < 0.05f && C.HeightAboveGround < 50.0f)
		{
			WingState = EWingState::Connected;
			InflationHeight = 0.0f;
			CellFillFactor = 0.0f;
			ALineFadeFactor = 0.0f;
			WingGroundVel = WingVel;
			WingVel = FVector::ZeroVector;
			if (ConnectedParamotor)
			{
				APawn* Pawn = Cast<APawn>(ConnectedParamotor->GetAttachParentActor());
				if (Pawn)
				{
					ACharacter* Char = Cast<ACharacter>(Pawn);
					if (Char) Char->GetCharacterMovement()->GravityScale = 1.0f;
				}
			}
			UE_LOG(LogTemp, Warning, TEXT("WingActor: Wing deflated — back to Connected"));
		}
	}
}

// ============================================================
// HELPER 10: UpdateKitingRotation
// Wing rotation (yaw/pitch/roll), debug log
// ============================================================

void AWingActor::UpdateKitingRotation(FClothContext& C)
{
	// === WING ROTATION ===
	// Wind-driven yaw (NOT pilot-coupled). The wing weathervanes into the wind.
	// KiteYawDrift is the yaw offset from wind direction (computed above from drag diff).
	FVector WindDir2D = WindDirection;
	WindDir2D.Z = 0.0f;
	WindDir2D.Normalize();
	float WindYaw = FMath::Atan2(WindDir2D.Y, WindDir2D.X) * RAD_TO_DEG;
	WingGroundYaw = WindYaw + KiteYawDrift;  // Absolute wing yaw
	FVector ToWingRot = GetActorLocation() - C.CarabMid;
	ToWingRot.Z = 0.0f;
	float ToWingDist = ToWingRot.Size();
	FRotator DesiredRot;
	if (ToWingDist > LINE_LENGTH_CM * 0.1f)
	{
		float BlendWeight = FMath::Clamp((ToWingDist - 100.0f) / 300.0f, 0.0f, 0.4f);
		float ToWingYaw = FMath::Atan2(ToWingRot.Y, ToWingRot.X) * RAD_TO_DEG;
		float YawDiff = ToWingYaw - WingGroundYaw;
		while (YawDiff > 180.0f) YawDiff -= 360.0f;
		while (YawDiff < -180.0f) YawDiff += 360.0f;
		DesiredRot.Yaw = WingGroundYaw + YawDiff * BlendWeight;
	}
	else
	{
		DesiredRot.Yaw = WingGroundYaw;
	}
	// Pitch: base nose-up + pendulum + overshoot
	float ForwardComponent = (ToWingDist > 1.0f) ? FVector::DotProduct(ToWingRot.GetSafeNormal(), C.PilotFwdDir) * ToWingDist : 0.0f;
	float PendulumPitch = FMath::Atan2(ForwardComponent, LINE_LENGTH_CM) * RAD_TO_DEG;
	// No artificial stability - pitch from aerodynamic moment
	DesiredRot.Pitch = PendulumPitch + KitePitchOvershoot;
	// Roll: emerges from yaw dynamics, NOT direct brake input
	// In real life, brake creates drag differential → yaw → banked turn
	// The yaw is already computed above (KiteYawDrift from drag diff)
	// Roll follows from the yawed flight: wing banks into the turn
	float TargetRoll = KiteYawDrift * 0.3f;  // Bank from yaw rate  // Bank proportional to yaw angle
	KiteRollSmoothed = FMath::FInterpTo(KiteRollSmoothed, TargetRoll, C.DeltaTime, 5.0f);
	DesiredRot.Roll = KiteRollSmoothed;
	// Smooth the rotation during inflated/kiting — the wing has inertia and
	// doesn't instantaneously change orientation. Without smoothing, position
	// jitter from turbulence causes visible rotation snapping.
	if (WingState == EWingState::Inflated)
	{
		FRotator CurrentRot = GetActorRotation();
		FRotator SmoothedRot = FMath::RInterpTo(CurrentRot, DesiredRot, C.DeltaTime, 6.0f);
		SetActorRotation(SmoothedRot);
	}
	else
	{
		SetActorRotation(DesiredRot);
	}

	// Debug log
	InflLogTimer += C.DeltaTime;
	if (InflLogTimer > 0.2f)
	{
		InflLogTimer = 0.0f;
		UE_LOG(LogTemp, Warning, TEXT("INFL: state=%d pull=%.0fN catch=%.0fN lift=%.0fN weight=%.0fN fill=%.2f vel=(%.0f,%.0f,%.0f) h=%.0f dist=%.0f fade=%.2f"),
			(int32)WingState, C.ALinesPullN, C.WindCatchN, C.TotalLiftN, C.WingWeightN, CellFillFactor,
			WingVel.X, WingVel.Y, WingVel.Z, C.HeightAboveCarab, C.DistFromCarab, ALineFadeFactor);
	}
}


// Old kiting physics code removed — unified inflation+kiting model handles both states.
// See the UNIFIED INFLATION + KITING PHYSICS section above in SimulateCloth().


void AWingActor::ShiftAllParticles(FVector Shift)
{
	for (int32 i = 0; i < Particles.Num(); i++)
	{
		if (!Particles[i].bPinned && !Particles[i].bGrabbed)
		{
			Particles[i].Pos += Shift;
			Particles[i].OldPos += Shift;
		}
	}
	for (int32 i = 0; i < LineParticles.Num(); i++)
	{
		if (!LineParticles[i].bPinned)
		{
			LineParticles[i].Pos += Shift;
			LineParticles[i].OldPos += Shift;
		}
	}
}

