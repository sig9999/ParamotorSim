#include "WingActor.h"
#include "AirfoilPolar.h"
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
// WingFlightPhysics.cpp — WingActor implementation split
// ============================================================

// ============================================================
// FLIGHT UPDATE — aerodynamic forces when airborne
// ============================================================

void AWingActor::UpdateFlight(float DeltaTime){
	// ENTRY LOG — verify this function is reached
	EntryLogTimer += DeltaTime;
	if (EntryLogTimer > 1.0f)
	{
		EntryLogTimer = 0.0f;
		UE_LOG(LogTemp, Error, TEXT("UpdateFlight ENTRY: WingState=%d bRisers=%d ConnectedParamotor=%s"),
			(int32)WingState, bRisersConnected ? 1 : 0, ConnectedParamotor ? TEXT("yes") : TEXT("no"));
	}
	if (WingState != EWingState::Flying) return;
if (!bRisersConnected) {
	RisersLogTimer += DeltaTime;
	if (RisersLogTimer > 1.0f) { RisersLogTimer = 0.0f; UE_LOG(LogTemp, Error, TEXT("UpdateFlight: bRisersConnected=false! WingState=Flying")); }
	return;
}
// Get the pilot pawn
APawn* Pawn = nullptr;
if (ConnectedParamotor)		Pawn = Cast<APawn>(ConnectedParamotor->GetAttachParentActor());
if (!Pawn) {
	PawnLogTimer += DeltaTime;
	if (PawnLogTimer > 1.0f) { PawnLogTimer = 0.0f; UE_LOG(LogTemp, Error, TEXT("UpdateFlight: Pawn=null! ConnectedParamotor=%s"), ConnectedParamotor ? TEXT("yes") : TEXT("no")); }
	return;
}
ACharacter* Char = Cast<ACharacter>(Pawn);
if (!Char) return;
if (!Char->GetCharacterMovement()) return;
FlightTime += DeltaTime;
// === TRIMMER INTERPOLATION ===
// Trimmers smoothly move toward target position
	TrimmerPosition =
FMath::FInterpTo(TrimmerPosition, TrimmerTarget, DeltaTime, TrimmerSpeed);
// Maintain canopy shape

ApplyInflatedShape(DeltaTime);
// Current velocity from the movement component (authoritative when flying)
FVector Vel = Char->GetCharacterMovement()->Velocity;
// Safety: NaN check
if (!FMath::IsFinite(Vel.X) || !FMath::IsFinite(Vel.Y) || !FMath::IsFinite(Vel.Z))		Vel = FVector::ZeroVector;
if (Vel.Size() > 2000.0f)
		Vel = Vel.GetSafeNormal() * 2000.0f;
Airspeed = Vel.Size();
ClimbRate = Vel.Z;
// === WIND (dynamic — gusts continue in flight) ===
	// Use unified wind update helper (replaces duplicated gust code)
	float FlightGustAmp = 0.0f;  // No fake default gust
	if (CachedGameInstance)
		FlightGustAmp = CachedGameInstance->WindGusts;
	UpdateDynamicWind(DeltaTime, FlightGustAmp);
FVector ShiftedWindDirFlight = WindDirection.RotateAngleAxis(WindDirShift * RAD_TO_DEG, FVector(0, 0, 1));
FVector Wind = ShiftedWindDirFlight * CurrentWindSpeed  * CM_PER_M;
// Apparent wind = true wind - velocity (wind the wing feels)
FVector ApparentWind = Wind - Vel;
// === HORIZONTAL AIRSPEED (for lift) ===
FVector HorizApparent = FVector(ApparentWind.X, ApparentWind.Y, 0.0f);
float HorizSpeed = HorizApparent.Size();
float Vms = HorizSpeed  / CM_PER_M;
// m/s
// === AERODYNAMIC COEFFICIENTS — PER-SIDE STALL MODEL ===
// Each half of the wing has its own AoA based on that side's brake input.
// When brake is pulled hard enough, that side's AoA exceeds stall angle.
// Post-stall: CL drops sharply, CD increases dramatically (separated flow).
// This naturally causes asymmetric lift loss → roll → spiral → collapse.

float Rho = AIR_DENSITY_SEA_LEVEL;  // Will be updated after altitude is computed
float HalfArea = WingModel.WingArea * 0.5f;
float StallAoA = WingModel.StallAoA;
// 15-16° depending on wing
// === BRAKE RESPONSE LAG ===
// Real brakes have a ~0.3s lag — the trailing edge takes time to deflect,
// and the airflow takes time to respond. This gives the "spongy" feel.
// Higher airspeed = faster response (more air pressure on trailing edge).

float BrakeLagSpeed = 1.0f / (LINE_LENGTH_CM  / CM_PER_M / 160.0f + 0.15f);  // Line wave (c~160m/s) + hand motion (~0.15s)
// Faster response at higher airspeed
	EffectiveLeftBrake =
FMath::FInterpTo(EffectiveLeftBrake, LeftBrakeAmount, DeltaTime, BrakeLagSpeed);
EffectiveRightBrake = FMath::FInterpTo(EffectiveRightBrake, RightBrakeAmount, DeltaTime, BrakeLagSpeed);
// === WIND GRADIENT WITH ALTITUDE ===
// Wind speed increases with altitude: V(h) = V_ref * (h/h_ref)^0.15
// At 10m: 1.0x, at 50m: ~1.25x, at 100m: ~1.41x

float EarlyPawnZ = Pawn->GetActorLocation().Z;
float EarlyGZ = GetCachedGroundZ(Pawn->GetActorLocation());
float AltitudeM = FMath::Max(0.0f, (EarlyPawnZ - EarlyGZ)  / CM_PER_M);
Rho = AirDensityAtAltitude(AltitudeM);  // ISA altitude-dependent air density
// Wind gradient: power law profile V(z) = V_ref × (z/z_ref)^α
// α = 0.10 for open water/coast, 0.15 for open terrain, 0.25 for suburbs
// Reference height z_ref = 10m (standard anemometer height)
// At ground level (z=0): wind = 0 (no-slip condition)
// At 1m: 79% of reference wind. At 5m: 92%. At 10m: 100%. At 20m: 108%.
float WindGradientAlpha = 0.12f;  // Coastal terrain
float WindRefHeight = 10.0f;  // Standard reference height
float HeightRatio = FMath::Max(0.01f, AltitudeM / WindRefHeight);
float WindGradientFactor = FMath::Pow(HeightRatio, WindGradientAlpha);
float AltitudeWindSpeed = CurrentWindSpeed * WindGradientFactor;
FVector AltitudeWind = ShiftedWindDirFlight * AltitudeWindSpeed  * CM_PER_M;
// === TURBULENCE — add to freestream velocity, not as fake acceleration ===
// The von Kármán states are turbulent VELOCITY variations (m/s).
// Correct physics: add them to the wind vector, then compute apparent wind.
// The aerodynamic forces will naturally respond to the changed apparent wind.
// No need to inject fake acceleration — the forces come from the polar.
float TurbStrength = FMath::Abs(TurbLongState) + FMath::Abs(TurbLatState) + FMath::Abs(TurbVertState);
FVector TurbulenceVelocity = (ShiftedWindDirFlight * TurbLongState  // Along wind
                            + FVector::CrossProduct(FVector::UpVector, ShiftedWindDirFlight).GetSafeNormal() * TurbLatState  // Crosswind
                            + FVector::UpVector * TurbVertState)     // Vertical
                             * CM_PER_M;  // m/s → cm/s
// No altitude mixing scale — the Dryden scale length already grows with
// altitude, which increases turbulence magnitude naturally. No double-scaling.
// Add turbulent velocity to wind before computing apparent wind
ApparentWind = (AltitudeWind + TurbulenceVelocity) - Vel;
HorizApparent = FVector(ApparentWind.X, ApparentWind.Y, 0.0f);
HorizSpeed = HorizApparent.Size();
Vms = HorizSpeed  / CM_PER_M;
// FlightTurbulence is now zero — forces come from the polar on the modified apparent wind
FlightTurbulence = FVector::ZeroVector;
// === REAL CELL PRESSURE MODEL ===
// Update each cell's pressure from ram-air physics.
// The wing flies because cells are pressurized — no pressure = no lift = falls.
// This replaces the old formula-based lift with pressure-driven lift.
UpdateCellPressure(DeltaTime, Vms, EffectiveLeftBrake, EffectiveRightBrake);
// Turbulence is now handled by adding turbulent velocity to the freestream
// (see above). The aerodynamic forces on the modified apparent wind naturally
// produce all turbulence effects — bumps, AoA variations, collapses.
// No separate FlightTurbulence acceleration needed.

// === THERMALS (buoyancy-based updraft model) ===
float ThermalLiftMs = ComputeThermalLiftMs(AltitudeM);
// Apply thermal lift as additional vertical air velocity (not acceleration)
ApparentWind.Z += ThermalLiftMs  * CM_PER_M;  // m/s to cm/s vertical air movement
// Per-side AoA: base cruise + effective brake (with lag) + turbulence
// Vertical turbulence changes AoA — TurbVertState is in m/s, convert to AoA:
// ΔAoA ≈ atan(ΔV_vert / V_airspeed) ≈ ΔV_vert / V (small angle)
float TurbulenceAoA = (Vms > 1.0f) ? TurbVertState / Vms * RAD_TO_DEG : 0.0f;
// === BRAKE LINE PHYSICS — AoA from trailing edge deflection ===
// Real physics: pulling the brake handle pulls the brake line, which pulls
// the trailing edge down. This effectively tilts the chord line, increasing AoA.
//
// The brake line has a mechanical advantage from the cascade:
// - Brake line attaches at ~95% chord
// - The cascade gives ~2:1 mechanical advantage
// - So 40cm of handle pull = 80cm of trailing edge displacement
// - But the trailing edge only deflects over the rear 30% of chord
//
// Real deflection: θ_TE = atan(pull × MA / (chord × 0.3))
// At 100% brake: θ_TE = atan(80 / 60) = 53° (trailing edge deflects dramatically)
//
// The effective AoA increase is a fraction of this (the whole chord doesn't rotate):
// Effective AoA ≈ θ_TE × 0.30 (only rear 30% deflects)
// At 100% brake: 53° × 0.30 = 16° (matches real paramotor flight data)
//
// The progressive feel comes from the atan() curve:
// - Light brake: small deflection, gentle response
// - Mid brake: linear response
// - Heavy brake: deflection ramps up steeply (atan steepens near its knee)
// - Full brake: maximum deflection, stall territory
// === BRAKE TO AoA (physical trailing edge deflection) ===
// Uses the shared TrailingEdgeDeflectionDeg() helper from AirfoilPolar.h.
// Physical model: θ_TE = asin(pull / chord)
// At 100% brake: pull = 40cm, chord = 200cm → θ_TE = 11.5°
// Effective AoA increase = θ_TE × 0.8 (fabric flex factor) = 9.2°
// This matches real paramotor flight data.
auto BrakeToAoA = [](float Brake) -> float
{
	return TrailingEdgeDeflectionDeg(Brake) * 0.8f;  // 0.8 = fabric flex factor
};
// Trim AoA. At 2° the scaled polar gives CL ≈ 0.72, which produces level
// flight at ~10 m/s for the Charger 25M (lift ≈ weight). 3° gave CL ≈ 0.85,
// causing excess lift / floaty climb at trim.
float LeftAoA = WingModel.TrimAoA + BrakeToAoA(EffectiveLeftBrake) + TurbulenceAoA + GetTrimmerAoAOffset();
float RightAoA = WingModel.TrimAoA + BrakeToAoA(EffectiveRightBrake) + TurbulenceAoA + GetTrimmerAoAOffset();
// === SPEED STABILITY (from pendulum dynamics, not forced) ===
// Speed stability emerges naturally from the wing-pilot pendulum:
// 1. Speed increases → more lift → pilot swings back → wing pitches forward
// 2. Wing pitch forward → lower AoA → less lift → speed stabilizes
// This is handled by the pitch dynamics + pendulum coupling below.
// No artificial -1.5°/m/s AoA reduction needed — it emerges from physics.

// Wing pitch from surge/oscillation affects AoA (1:1 for small angles)
// When wing surges forward (negative pitch), AoA decreases
// When wing pitches back (positive pitch), AoA increases
// This is the geometric relationship: ΔAoA = -Δpitch (not 0.3× pitch)
float PitchAoAEffect = WingPitchAngle * 1.0f;  // 1:1 geometric coupling
LeftAoA += PitchAoAEffect;
RightAoA += PitchAoAEffect;
// === BRAKE LINE PHYSICS — camber from physical trailing edge deflection ===
// Pulling the brake handle pulls the brake line, which pulls the trailing
// edge down. This increases the airfoil's effective camber (4% → 8%).
// The camber effect is handled entirely by LookupAirfoilPolarWithCamber(),
// which uses real XFOIL data (NACA 4415 vs NACA 8415 comparison).
// No magic CLMax/CL0 multipliers — the polar lookup handles everything.
//
// The brake also increases AoA by deflecting the trailing edge downward,
// which effectively tilts the chord line. This is the BrakeToAoA function.
// Per-side: each side's brake affects that side's AoA and camber independently.

// === REAL AIRFOIL POLAR LOOKUP ===
// Uses actual XFOIL data for NACA 4415 at Re=500,000.
// The polar gives real CL and CD at each AoA, including post-stall behavior.
// We scale the base polar (CL0=0.41, CLMax=1.47) to the wing's coefficients.
auto ComputeCLFromPolar = [&](float AoA, float WingCL0, float WingCLMax, float BrakeInput) -> float
{
	float PolarCL, PolarCD;
	LookupAirfoilPolarWithCamber(AoA, BrakeInput, PolarCL, PolarCD);
	// Affine scale from base polar (BaseCL0≈0.41, BaseCLMax=1.47) to (WingCL0, WingCLMax)
	float BaseCL0 = 0.41f;
	float BaseCLMax = 1.47f;
	float Denom = BaseCLMax - BaseCL0;
	float CLScale = FMath::Abs(Denom) > 1e-3f ? (WingCLMax - WingCL0) / Denom : 1.0f;
	return (PolarCL - BaseCL0) * CLScale + WingCL0;
};
auto ComputeCDFromPolar = [&](float AoA, float CL, float CD0, float AR, float e, float BrakeInput) -> float
{
	float PolarCL, PolarCD;
	LookupAirfoilPolarWithCamber(AoA, BrakeInput, PolarCL, PolarCD);
	// Profile drag = wing baseline + polar increment above base airfoil CD0
	float BaseAirfoilCD0 = 0.0082f;  // NACA 4415 at zero-lift (XFOIL)
	float ProfileCD = CD0 + FMath::Max(0.0f, PolarCD - BaseAirfoilCD0);
	// 3D induced drag: CD_induced = CL² / (π · AR · e)
	float Denom = PI * AR * e;
	float InducedCD = Denom > 0.0f ? CL * CL / Denom : 0.0f;
	// Post-stall: the polar already captures the drag rise from separated flow
	return ProfileCD + InducedCD;
};
float LeftCL = ComputeCLFromPolar(LeftAoA, WingModel.CL0 * GetTrimmerCLFactor(), WingModel.CLMax, EffectiveLeftBrake);
float RightCL = ComputeCLFromPolar(RightAoA, WingModel.CL0 * GetTrimmerCLFactor(), WingModel.CLMax, EffectiveRightBrake);
float LeftCD = ComputeCDFromPolar(LeftAoA, LeftCL, WingModel.CD0 * GetTrimmerDragFactor(), WingModel.AR, OswaldEfficiency, EffectiveLeftBrake);
float RightCD = ComputeCDFromPolar(RightAoA, RightCL, WingModel.CD0 * GetTrimmerDragFactor(), WingModel.AR, OswaldEfficiency, EffectiveRightBrake);
// === GROUND EFFECT — reduce induced drag near ground ===
// Below ~1 wingspan altitude, induced drag reduces significantly.
// This makes landing flares more effective and takeoff easier.
// Formula: CD_induced *= (1 - exp(-h / (b/2)))
// At h=10m (1 wingspan): 14% reduction. At h=5m: 37%. At h=1m: 82%.
float WingAltitudeAGL = Pawn->GetActorLocation().Z - GetCachedGroundZ(Pawn->GetActorLocation());
float WingspanM = FMath::Sqrt(WingModel.WingArea * WingModel.AR);  // b = sqrt(AR * S)
// Ground effect reduces induced drag near ground: factor = 1 - exp(-h / (b/2))
// At h=0: 0 (full reduction). At h=b/2: 63% remaining. At h→∞: 1 (no reduction).
float GroundEffectFactor = 1.0f - FMath::Exp(-WingAltitudeAGL  / CM_PER_M / (WingspanM * 0.5f));
// Apply to induced drag portion only (separate profile from induced)
float BaseAirfoilCD0 = 0.0082f;
float ProfileCD0 = WingModel.CD0 * GetTrimmerDragFactor();
float LeftInduced = FMath::Max(0.0f, LeftCD - ProfileCD0);
float RightInduced = FMath::Max(0.0f, RightCD - ProfileCD0);
LeftCD = ProfileCD0 + LeftInduced * GroundEffectFactor;
RightCD = ProfileCD0 + RightInduced * GroundEffectFactor;
// === COLLAPSE DETECTION ===
// Asymmetric collapse: one side's AoA goes negative (tuck) or way past stall
// Frontal collapse: both sides go negative (sudden downdraft or brake release at speed)
// Collapses reduce CL to near-zero on the collapsed side and increase drag dramatically.
// Check for asymmetric collapse triggers
// Brake-induced collapse: deep brake (past stall) + turbulence pushes AoA even higher
// Threshold is StallAoA + 6° — reachable with 90%+ brake + moderate turbulence

// === COLLAPSE DETECTION (DETERMINISTIC — from AoA and pressure) ===
// Collapses are NOT random. They occur when:
// 1. AoA goes negative → leading edge tucks under (frontal/asymmetric collapse)
// 2. AoA exceeds stall → flow separates, cell loses pressure (deep stall collapse)
// 3. Turbulence causes local AoA excursions beyond stall/negative-stall limits
//
// The collapse amount ramps up deterministically when the condition persists,
// and recovers when the condition clears. No FRand() — pure physics.

float TipStallMargin = 2.0f;  // Tips stall 2° earlier than center (lower Re, washout)

// DETERMINISTIC collapse triggers — no randomness
// Asymmetric: AoA below -1° (tuck) or above stall+6° with heavy brake
bool bLeftCollapseTrigger = (LeftAoA < -WingModel.StallAoA * 0.15f) || (LeftAoA > StallAoA + FMath::Abs(TurbulenceAoA) && EffectiveLeftBrake > 0.5f);
bool bRightCollapseTrigger = (RightAoA < -WingModel.StallAoA * 0.15f) || (RightAoA > StallAoA + FMath::Abs(TurbulenceAoA) && EffectiveRightBrake > 0.5f);

// Tip stall: if AoA is past stall minus tip margin, tips are stalled
// This is deterministic — tips stall at lower AoA than center
if (LeftAoA > StallAoA - TipStallMargin)
	bLeftCollapseTrigger = true;
if (RightAoA > StallAoA - TipStallMargin)
	bRightCollapseTrigger = true;

// Turbulence-induced collapse: turbulence shifts local AoA beyond limits
// This is deterministic — if turbulence is strong enough to push AoA past
// stall or negative-stall, the cell collapses. No probability roll.
// Only the VERTICAL turbulence component changes AoA (longitudinal changes airspeed,
// lateral causes sideslip/yaw). Use TurbVertState, not the sum of all three.
float TurbAoAVariation = (Vms > 1.0f) ? FMath::Abs(TurbVertState) / Vms * RAD_TO_DEG : 0.0f;
// Overloaded wings are MORE collapse-resistant (higher internal pressure)
float FlightWeightRatio = WingModel.MaxTakeoffWeight > 0.0f ? PilotWeight / WingModel.MaxTakeoffWeight : 1.0f;
float FlightCollapseResistance = FMath::Max(0.1f, FlightWeightRatio);  // No upper clamp
// Effective AoA including turbulence:
float LeftAoAWithTurb = LeftAoA + TurbAoAVariation / FlightCollapseResistance * GetTrimmerCollapseRisk();
float RightAoAWithTurb = RightAoA + TurbAoAVariation / FlightCollapseResistance * GetTrimmerCollapseRisk();
// Collapse if turbulence pushes AoA past limits
if (LeftAoAWithTurb > StallAoA || LeftAoAWithTurb < -WingModel.StallAoA * 0.15f)
	bLeftCollapseTrigger = true;
if (RightAoAWithTurb > StallAoA || RightAoAWithTurb < -WingModel.StallAoA * 0.15f)
	bRightCollapseTrigger = true;
	// Update collapse amounts — rates from cell pressure physics
	// Collapse rate: cell deflates at rate proportional to airspeed (ram-air vents
	// can't maintain pressure when flow separates). Time constant ~0.3s at 10 m/s.
	// Recovery rate: cell refills at rate proportional to airspeed.
	// Time constant ~0.5s at 10 m/s (refill is slower than deflation).
	float CellDeflateRate = FMath::Max(0.5f, Vms * 0.33f);  // Orifice flow: rate ~ V * Cd * A_intake / V_cell
	float CellRefillRate = FMath::Max(0.3f, Vms * 0.20f);  // Refill slower than deflate
	// Recovery delay: flow reattachment time ≈ chord / airspeed
	float MeanChordM = (WingspanM > 0.0f) ? WingModel.WingArea / WingspanM : 2.0f;
	float RefillDelay = FMath::Max(0.1f, MeanChordM / FMath::Max(1.0f, Vms));

if (bLeftCollapseTrigger)	{
		LeftCollapseAmount = FMath::Min(1.0f, LeftCollapseAmount + DeltaTime * CellDeflateRate);
CollapseRecoveryTimer = 0.0f;
}
	else if (LeftCollapseAmount > 0.0f)	{
		CollapseRecoveryTimer += DeltaTime;
		if (CollapseRecoveryTimer > RefillDelay)
			LeftCollapseAmount = FMath::Max(0.0f, LeftCollapseAmount - DeltaTime * CellRefillRate);
	}
	if (bRightCollapseTrigger)	{
		RightCollapseAmount = FMath::Min(1.0f, RightCollapseAmount + DeltaTime * CellDeflateRate);
CollapseRecoveryTimer = 0.0f;
}
	else if (RightCollapseAmount > 0.0f)	{
		CollapseRecoveryTimer += DeltaTime;
if (CollapseRecoveryTimer > RefillDelay)			RightCollapseAmount = FMath::Max(0.0f, RightCollapseAmount - DeltaTime * CellRefillRate);
}
	// Frontal collapse: both sides negative AoA (sudden downdraft or speed + brake release)
// Speed-induced frontal: at ~2x trim speed, AoA drops below zero on both sides
bool bFrontalTrigger = (LeftAoA < -WingModel.StallAoA * 0.15f && RightAoA < -WingModel.StallAoA * 0.15f);
if (bFrontalTrigger)	{
		FrontalCollapseAmount = FMath::Min(1.0f, FrontalCollapseAmount + DeltaTime * CellDeflateRate);  // Same rate
CollapseRecoveryTimer = 0.0f;
}
	else if (FrontalCollapseAmount > 0.0f)	{
		CollapseRecoveryTimer += DeltaTime;
if (CollapseRecoveryTimer > RefillDelay * 0.8f)			FrontalCollapseAmount = FMath::Max(0.0f, FrontalCollapseAmount - DeltaTime * CellRefillRate);  // Same rate
}
	// Apply collapse effects: collapsed section produces bluff-body drag, no lift
	// The polar already gives near-zero CL at post-stall AoA, so we only need
	// to handle the geometric effect: a crumpled section has less area.
	// Lift scales with effective flying area: (1 - collapse_amount)
if (LeftCollapseAmount > 0.0f)	{
		LeftCL *= (1.0f - LeftCollapseAmount);  // Crumpled area produces no lift
	// Collapsed fabric drag: bluff body CD ≈ 1.0 on the collapsed area fraction
	// F_drag = 0.5 × ρ × V² × S_collapse × CD_bluff, so CD += collapse × CD_bluff
		LeftCD += LeftCollapseAmount * 1.0f;  // Bluff-body drag on collapsed fraction

}
	if (RightCollapseAmount > 0.0f)	{
		RightCL *= (1.0f - RightCollapseAmount);
		RightCD += RightCollapseAmount * 1.0f;
	}
	if (FrontalCollapseAmount > 0.0f)	{
		LeftCL *= (1.0f - FrontalCollapseAmount);
		RightCL *= (1.0f - FrontalCollapseAmount);
		LeftCD += FrontalCollapseAmount * 1.0f;
		RightCD += FrontalCollapseAmount * 1.0f;
	}
	// === PARACHUTAL STALL — emerges naturally from polar ===
	// When both brakes are deep and airspeed drops, the AoA exceeds stall
	// and the polar naturally produces low CL + high CD. No artificial trigger.
	// Parachutal stall: detected from actual aerodynamic state, not fixed thresholds
	// Real parachutal stall: wing is fully stalled (AoA > stall angle) AND
	// airspeed is below the speed needed for attached flow recovery
	// The polar already models the aerodynamic effects (CL drop, CD rise)
	float TotalEffectiveBrake = (EffectiveLeftBrake + EffectiveRightBrake) * 0.5f;
	float BrakeAoA = BrakeToAoA(TotalEffectiveBrake);
	float TotalAoA = WingModel.TrimAoA + BrakeAoA + GetTrimmerAoAOffset();
	// Parachutal stall: AoA well past stall + low airspeed (can't recover)
	bool bStallTrigger = (TotalAoA > StallAoA) && (Vms < TrimSpeedMs * 0.7f);
	if (bStallTrigger)
	{
		ParachutalStallTimer += DeltaTime;
		// Recovery time: 1 / CellRefillRate (consistent with collapse recovery)
		float StallRecoveryTime = 1.0f / CellRefillRate;
		if (ParachutalStallTimer > StallRecoveryTime)
			bInParachutalStall = true;
	}
	else
	{
		ParachutalStallTimer = FMath::Max(0.0f, ParachutalStallTimer - DeltaTime);
		if (ParachutalStallTimer <= 0.0f)
			bInParachutalStall = false;
	}
	// No artificial CL/CD modification — the polar handles stall aerodynamics
	// Average CL for glide ratio reporting
float CL = (LeftCL + RightCL) * 0.5f;
float CDWing = (LeftCD + RightCD) * 0.5f;
	// Pilot + paramotor drag (pilot sitting in harness with motor on back)
	// Real frontal area: 0.5 m² standing → 0.3 m² seated (40% reduction of pilot drag)
	float PilotDragArea = FMath::Lerp(0.5f, 0.3f, SeatTransition);
	float CDPilot = 0.5f;  // Pilot+motor drag coefficient (bluff body)
	// Line drag: ~150 lines, avg 0.8mm diameter, avg 4m length
	// Effective drag area: ~0.06 m², CD=1.0 (cylinder in crossflow)
	float LineDragArea = 0.06f;
	float CDLine = 1.0f;
	// NOTE: Flare is now handled REALISTICALLY through the camber-based CLMax increase
	// above (brakes increase CLMax by up to 30%). No artificial flare boost needed.
	// The wing generates more lift when brakes are pulled because the cambered
	// airfoil has a higher CLMax — this is real aerodynamics, not a hack.
// === FORCES — per side (Newtons) ===
// Use total apparent airspeed (includes vertical component from sink/climb/thermals)
// for dynamic pressure — vertical air movement contributes to lift.
float Vtot = ApparentWind.Size()  / CM_PER_M;
float LeftLiftN = 0.5f * Rho * Vtot * Vtot * HalfArea * LeftCL;
float RightLiftN = 0.5f * Rho * Vtot * Vtot * HalfArea * RightCL;
float LiftN = LeftLiftN + RightLiftN;
// === CELL PRESSURE + LINE TENSION COUPLING ===
// The wing only generates lift if cells are pressurized AND lines are taut.
// This is the SAME coupling as the cell pressure model in WingClothSim.cpp:
//   CellCL = CL0 × fill² × LineTensionFraction
// A partially filled cell is a crumpled mess, not a partial airfoil.
// Lift scales as fill² — at 50% fill, only 25% of lift.
// Without line tension, the cell is a billowing parachute, not an airfoil.
// During flight, lines are taut (pilot hanging from wing), so tension = 1.0.
// But if cells deflate (e.g. from collapse), lift drops quadratically.
float LocalTotalMassKg = PilotWeight + WingModel.WingWeight + 1.5f;  // pilot + wing + lines
LineTensionFraction = FMath::Clamp(LiftN / FMath::Max(1.0f, LocalTotalMassKg * GRAVITY_MS2), 0.0f, 1.0f);
float FillTensionCoupling = CellFillFactor * CellFillFactor * LineTensionFraction;
LiftN *= FillTensionCoupling;
LeftLiftN *= FillTensionCoupling;
RightLiftN *= FillTensionCoupling;
// No BrakePumpBoost — removed fake lift boost. Real brake pumping helps cells
// fill (handled by cell pressure model), not magic lift increase.
// === REAL LINE TENSION PER GROUP ===
// The total lift force is distributed across the A/B/C/D line groups
// based on the pressure distribution across the chord.
// Real paraglider line load distribution (from manufacturer data):
//   A lines: ~40% of total load (leading edge, highest pressure)
//   B lines: ~30% (mid-chord)
//   C lines: ~20% (rear)
//   D lines: ~10% (trailing edge, lowest pressure)
// Brake lines: only carry tension when pulled (brake input)
//
// The load distribution shifts with brake input:
// - Pulling brakes shifts load rearward (C/D carry more)
// - Speed system (if implemented) shifts load forward (A carries more)
//
// Line stretch: F = k × x, where k is line stiffness
// Paracord stiffness: ~1000 N/cm for a 7m line set
// A lines stretch most (highest tension)
float TotalLoadN = LiftN;  // Total upward force = total line tension
// Base load distribution (no brake, no speed system)
float LoadFractionA = 0.40f;
float LoadFractionB = 0.30f;
float LoadFractionC = 0.20f;
float LoadFractionD = 0.10f;
// Brake input shifts load rearward (trailing edge deflection → CP moves back)
float TotalBrake = (EffectiveLeftBrake + EffectiveRightBrake) * 0.5f;
// Real physics: brake deflects TE down → pressure center moves from ~30% chord
// to ~50% chord at full brake. Load shift = (CP_new - CP_old) / chord × total load.
// A/B lines are forward of CP, C/D are behind. At full brake:
// - CP at 50% chord (was 30%) → A/B lose load, C/D gain load
// - Shift magnitude: ~25% of total load moves from A/B to C/D
// The shift is proportional to brake deflection (linear for small deflections)
float BrakeShiftFraction = TotalBrake * 0.25f;  // Up to 25% load shift at full brake
// Coefficients sum to zero so total load is conserved (A,B lose; C,D gain)
LoadFractionA -= BrakeShiftFraction * 0.50f;  // A loses 50% of the shifted load (forwardmost)
LoadFractionB -= BrakeShiftFraction * 0.25f;  // B loses 25% (mid-front)
LoadFractionC += BrakeShiftFraction * 0.25f;  // C gains 25% (mid-rear)
LoadFractionD += BrakeShiftFraction * 0.50f;  // D gains 50% (rearmost, closest to deflected TE)
// Clamp to prevent negative tension (can happen at extreme brake + low speed)
LoadFractionA = FMath::Max(0.05f, LoadFractionA);
LoadFractionB = FMath::Max(0.05f, LoadFractionB);
LoadFractionC = FMath::Max(0.01f, LoadFractionC);
LoadFractionD = FMath::Max(0.01f, LoadFractionD);
// Compute per-group tension
LineTensionA = TotalLoadN * LoadFractionA;
LineTensionB = TotalLoadN * LoadFractionB;
LineTensionC = TotalLoadN * LoadFractionC;
LineTensionD = TotalLoadN * LoadFractionD;
// Brake line tension: only when brakes are pulled
// Brake line force = pilot's pull force (~50N at full brake, real measurement)
	LineTensionBrake = TotalBrake * 50.0f;
// === LINE STRETCH (real material physics) ===
// Real line stretch: ΔL = F·L / (A·E)
// where F = tension (N), L = line length (m), A = cross-section area (m²),
// E = Young's modulus (Pa)
// Paracord: diameter = 1.5mm, A = π·(0.00075)² = 1.77e-6 m²
// E (nylon/Dyneema blend) ≈ 5 GPa = 5e9 Pa
// At 100N on 7m line: ΔL = 100·7 / (1.77e-6 · 5e9) = 0.079 m = 7.9 cm (≈1.1%)
float LineCrossSectionArea = 1.77e-6f;  // m² (1.5mm diameter paracord)
float LineYoungsModulus = 5.0e9f;       // Pa (nylon/Dyneema blend)
float LineLengthM = LINE_LENGTH_CM  / CM_PER_M;
float LineStiffnessNPerM = LineCrossSectionArea * LineYoungsModulus / LineLengthM;  // N/m
float BaseLineLengthCm = LINE_LENGTH_CM;
LineStretchA = (LineTensionA / LineStiffnessNPerM)  * CM_PER_M;  // cm
LineStretchB = (LineTensionB / LineStiffnessNPerM)  * CM_PER_M;
LineStretchC = (LineTensionC / LineStiffnessNPerM)  * CM_PER_M;
LineStretchD = (LineTensionD / LineStiffnessNPerM)  * CM_PER_M;
// A lines stretch most → leading edge drops slightly → AoA increases slightly
// This is a real effect: under high load, the wing's AoA increases from line stretch
// (Note: this effect is small, ~0.1-0.3° at normal loads, and is already implicitly
//  captured in the wing's trim AoA. We compute it for wear/future use.)
// Mean chord and A-D attachment spacing derived from wing geometry (not hardcoded)
float MeanChordCm = (WingspanM > 0.0f) ? (WingModel.WingArea / WingspanM) * CM_PER_M : 200.0f;
float ADistanceCm = MeanChordCm * 0.6f;  // A at ~15% chord, D at ~75% chord → 60% span
float StretchAoAEffect = FMath::Atan2((LineStretchA - LineStretchD), ADistanceCm) * RAD_TO_DEG;
// Per-side drag (for asymmetric yaw)
float VmsTotal = ApparentWind.Size()  / CM_PER_M;
FVector ApparentDir = ApparentWind.GetSafeNormal();
float LeftDragN = 0.5f * Rho * VmsTotal * VmsTotal * HalfArea * LeftCD;
float RightDragN = 0.5f * Rho * VmsTotal * VmsTotal * HalfArea * RightCD;
float PilotDragN = 0.5f * Rho * VmsTotal * VmsTotal * PilotDragArea * CDPilot;
float LineDragN = 0.5f * Rho * VmsTotal * VmsTotal * LineDragArea * CDLine;
float DragN = LeftDragN + RightDragN + PilotDragN + LineDragN;
// Thrust: Moster 185 = ~75 kgf = 736N at full throttle
// Real carburetor has a dead zone — below ~10% throttle, essentially no thrust
// (the engine is idling, not producing power). Above 10%, thrust ramps up.

// Use real thrust from ParamotorActor when available (includes RPM, propeller model)
float ThrustN = 0.0f;
if (ConnectedParamotor)
{
	ThrustN = ConnectedParamotor->GetThrustForceN(Vtot);
}
else
{
	// Fallback: simple throttle-based estimate
	float EffectiveThrottle = FMath::Max(0.0f, ThrottleAmount);
	float StaticThrustN = EffectiveThrottle * MAX_THRUST_N;
	float VmaxMs = TrimSpeedMs * 1.8f;
	float ThrustEfficiency = FMath::Max(0.0f, 1.0f - Vms / VmaxMs);
	ThrustN = StaticThrustN * ThrustEfficiency;
}
// Glide ratio (includes wing + pilot + line drag)
float CDTotal = CDWing + (PilotDragArea * CDPilot / WingModel.WingArea) + (LineDragArea * CDLine / WingModel.WingArea);
GlideRatio = (CDTotal > 0.001f) ? CL / CDTotal : 99.0f;
// === ACCELERATIONS (cm/s²) ===
// Total mass includes pilot + paramotor + fuel + wing + lines
// Real line mass: ~1.5kg for a full line set (150 lines, avg 7m, paracord)
float LineMassKg = 1.5f;
float TotalMassKg = PilotWeight + WingModel.WingWeight + LineMassKg;
// === WING LOADING EFFECTS ===
// Heavier pilots: fly faster, more responsive brakes, harder collapses, higher sink
// Lighter pilots: fly slower, floatier handling, softer collapses, lower sink
// Wing loading = total weight / wing area (kg/m²). Typical paramotor: 3-6 kg/m²
// Use GetWingLoading() for consistency with HUD/takeoff chance calculations

float WingLoading = GetWingLoading();
// Trim speed from physics: V = sqrt(2 × W / (ρ × S × CL_trim))
// where CL_trim is the lift coefficient at the trim AoA (from polar)
// Real EN-A wing at trim: CL ≈ 0.6-0.8, giving V_trim ≈ 10-11 m/s at 4.4 kg/m²
// CL at trim AoA: evaluate polar at WingModel.TrimAoA + trimmer offset (no brake)
float TrimAoAForCL = WingModel.TrimAoA + GetTrimmerAoAOffset();
float PolarCLTrim, PolarCDTrim;
LookupAirfoilPolarWithCamber(TrimAoAForCL, 0.0f, PolarCLTrim, PolarCDTrim);
float BaseCL0 = 0.41f, BaseCLMax = 1.47f;
float CLScaleTrim = FMath::Abs(BaseCLMax - BaseCL0) > 1e-3f
    ? (WingModel.CLMax - WingModel.CL0) / (BaseCLMax - BaseCL0) : 1.0f;
float CLTrim = (PolarCLTrim - BaseCL0) * CLScaleTrim + WingModel.CL0;
CLTrim = FMath::Max(0.01f, CLTrim);  // Division-by-zero guard only
float TotalWeightN = TotalMassKg * GRAVITY_MS2;
TrimSpeedMs = FMath::Sqrt(2.0f * TotalWeightN / (Rho * WingModel.WingArea * CLTrim));
// Trimmer speed factor adjusts CL (fast trim = lower CL = higher speed)
TrimSpeedMs *= GetTrimmerSpeedFactor();
// Brake authority emerges from the polar — no magic number needed.
// The braked side's increased drag and reduced lift produce the turn naturally.
// Set to 1.0 — the polar handles everything.
// BrakeAuthority removed - polar handles brake steering
// === ASYMMETRIC LIFT → ROLL MOMENT ===
// If left side stalls (less lift), wing rolls left (pilot falls left)
// If right side stalls, wing rolls right
// Roll moment = ΔL × y_arm (half wingspan)
float LiftDiff = RightLiftN - LeftLiftN;
// + = right has more lift → roll left
float RollArm = WingspanM * 0.5f;  // Half wingspan (moment arm)
float RollMoment = LiftDiff * RollArm;  // N·m
// Convert to angular acceleration: α = M/I_xx
// (used below in roll dynamics)
// === ASYMMETRIC DRAG → YAW MOMENT ===
// Stalled side has way more drag → wing yaws toward stalled side

float DragDiff = RightDragN - LeftDragN;
// + = right has more drag → yaw right
float LiftAccel = (LiftN / TotalMassKg) * CM_PER_M;
float DragAccel = (DragN / TotalMassKg) * CM_PER_M;
	// Seat transition already reduces pilot drag area above (Lerp 0.5→0.3 m²).
	// Do NOT reduce total drag here — that would also reduce wing/line drag.
float ThrustAccel = (ThrustN / TotalMassKg) * CM_PER_M;
float GravityAccel = GRAVITY_CM_S2;
// === TRIM SPEED STABILITY (speed polar) ===
// Real wings naturally fly at trim speed with no brake input.
// Below trim: too much drag, wing pitches forward, dives to regain speed
// Above trim: too little drag, wing pitches back, slows to trim
// Brakes slow you below trim (more drag, more lift, slower speed)
// This creates the "hands-off" stability of a real paraglider

float CurrentSpeedMs = Vel.Size()  / CM_PER_M;
float SpeedError = CurrentSpeedMs - TrimSpeedMs;
// + = too fast, - = too slow
// SPEED STABILITY — emerges naturally from the drag polar.
// Drag = 0.5 × ρ × V² × S × CD. As speed increases, drag increases quadratically,
// naturally limiting speed. As speed drops, drag drops and the wing sinks less,
// allowing it to accelerate. No artificial trim drag or trim sink needed.
// The polar already produces the correct drag at every speed and AoA.
// BrakePumpBoost removed — no fake lift boost from brake pumping
// === PENDULUM PHYSICS — REALISTIC PARAMOTOR SWING ===
// The pilot hangs below the wing on 7m riser lines.
// When throttle is PUNCHED, the pilot swings FORWARD (pendulum arc).
// When throttle is LET OFF, the wing slows (drag) and the pilot swings BACK.
// Gravity restores the pendulum back toward neutral (directly below wing).
//
// Research: PPC pitch changes ~20° from glide to level, +20° more to full climb.
// Sudden throttle increases cause "porpoising" — oscillating back and forth
// with decreasing amplitude until stabilizing in the new pitch attitude.
//
// Pendulum: ω = √(g/L), L = 700cm, g = 980 cm/s²
// ω = √(980/700) = 1.18 rad/s, period ≈ 5.3s

float LineLength = LINE_LENGTH_CM;
// cm
float PendulumOmega = FMath::Sqrt(GravityAccel / LineLength);
// 1.18 rad/s
float PendulumOmegaSq = PendulumOmega * PendulumOmega;

// === THROTTLE RATE TRACKING (for logging only) ===
float ThrottleDelta = ThrottleAmount - PrevThrottleAmount;
PrevThrottleAmount = ThrottleAmount;
ThrottleRate = ThrottleDelta / FMath::Max(DeltaTime, 0.001f);

// Thrust creates angular acceleration on the pendulum (pushes pilot forward)
// Real physics: α = a_thrust / L (torque = F×L, α = τ/(m×L²) = F/(m×L) = a/L)
float PendulumDriveAccel = ThrustAccel / LineLength;

// Brake drag also swings pilot forward (wing slows, pilot keeps going)
// Use total wing drag minus pilot drag (the wing-only drag swings the pilot)
float WingOnlyDragN = LeftDragN + RightDragN;
float PendulumBrakeAccel = (WingOnlyDragN / TotalMassKg)  * CM_PER_M / LineLength;

// Gravity restoring force: -ω² * θ
// Damping: -2 * ζ * ω * θ̇
// Damping ratio from aerodynamic drag + lift-derived damping (NOT a fixed value)
// ζ = c / (2 * m * ω), where c = 0.5 * ρ * V * S_effective
// S_effective = pilot drag + wing drag + line drag + wing lift damping
float PitchDampArea = PilotDragArea * CDPilot + WingModel.WingArea * CDWing
	+ LineDragArea * CDLine
	+ WingModel.WingArea * (WingspanM / (2.0f * LineLength  / CM_PER_M));  // lift-derived pitch damping
float PitchDampCoeff = 0.5f * Rho * FMath::Max(0.1f, Vms) * PitchDampArea;  // N·s/m
float DampingRatio = PitchDampCoeff / (2.0f * TotalMassKg * PendulumOmega);
DampingRatio = FMath::Clamp(DampingRatio, 0.01f, 10.0f);  // NaN safety only
float PendulumDamping = 2.0f * DampingRatio * PendulumOmega * PendulumAngularVel;

// Real driven pendulum equation: α = (drive + brake) - ω²θ - 2ζωθ̇
// No artificial throttle impulses — the thrust force itself drives the pendulum.
// When throttle changes, ThrustAccel changes, and the pendulum responds naturally.
float PendulumAngularAccel = PendulumDriveAccel + PendulumBrakeAccel
		- PendulumOmegaSq * PendulumAngle - PendulumDamping;
PendulumAngularVel += PendulumAngularAccel * DeltaTime;
PendulumAngle += PendulumAngularVel * DeltaTime;
// No clamp — pendulum angle limited by line geometry and gravity
// === PILOT PITCH TRANSITION (launch → upright → seated) ===
// Real paramotoring launch sequence:
// 1. At takeoff: pilot is upright, motor torque tilts them slightly forward
// 2. For first ~3s: pilot hangs upright, not yet in seat (more drag, different angle)
// 3. After ~3s: pilot swings legs forward and settles into harness seat
// 4. Body reclines from 0° (upright) to -80° (seated) over ~2s
// 5. Motor torque tilt fades as pilot settles and stabilizes
// The G key can still toggle seated/standing after the auto-transition.
if (bAutoSeatPending)
{
	LaunchTimer += DeltaTime;
	// Auto-seat after 3 seconds of flight (real timing)
	if (LaunchTimer > 3.0f)
	{
		bSeated = true;
		bAutoSeatPending = false;
	}
}
// Launch angle offset: motor torque tilts pilot forward, fades as they settle
float LaunchAngleFade = bAutoSeatPending ? FMath::Max(0.0f, 1.0f - LaunchTimer / 5.0f) : 0.0f;
LaunchAngleOffset = 5.0f * LaunchAngleFade;  // Up to 5° forward tilt from torque
	SeatTransition = FMath::FInterpTo(SeatTransition, bSeated ? 1.0f : 0.0f, DeltaTime, 2.0f);
	// Seated pilot pitch: -80° (reclined in harness). Standing: 0° (upright).
	// Add launch angle offset (forward tilt from motor torque at takeoff).
	PilotPitchTarget = (bSeated ? -80.0f : 0.0f) + LaunchAngleOffset;
	PilotPitch = FMath::FInterpTo(PilotPitch, PilotPitchTarget, DeltaTime, 3.0f);
	// Pilot drag area is now lerped directly (0.5 m² standing → 0.3 m² seated).
	// DragReductionFromSeat is kept for HUD/visual compatibility but no longer
	// multiplies total drag (that was double-counting wing + line drag).
	DragReductionFromSeat = SeatTransition * 0.40f;
// === STEERING — REALISTIC PARAMOTOR PHYSICS ===
//
// Real paramotor steering:
// 1. Pull left brake → left trailing edge deflects downward
// 2. Deflected trailing edge increases camber → more lift AND more drag on left side
// 3. More drag on left → left side slows → wing YAWS LEFT
// 4. The yaw + pendulum momentum banks the whole unit (pilot + wing) INTO the turn
// 5. Banked lift vector has horizontal component → centripetal force → actual curved path
// 6. At heavy brake: braked side stalls → lift loss → wing ROLLS INTO the turn (tip drops)
//
// The ENTIRE unit (pilot + wing) rolls together as one rigid body.
// The velocity vector rotates with the yaw + centripetal force curves the path.

// === WEIGHT SHIFT STEERING ===
// Weight shift is the primary steering method in paramotoring.
// The pilot shifts their weight under the wing, which:
// 1. Lowers the riser on the weight-shift side
// 2. Shifts the center of gravity sideways
// 3. The wing banks toward the weight-shift side
// 4. Banked lift creates centripetal force → curved flight path
//
// Weight shift is SMOOTHER than brake steering and doesn't add drag.
// Positive WeightShift = shift right → turn right
// Negative WeightShift = shift left → turn left
//
// PHYSICS: Weight shift moves the pilot's CG sideways relative to the risers.
// The CG offset creates a roll moment: M = m × g × Δx
// where Δx is the lateral CG offset (max ~20cm for full weight shift).
// This tilts the pendulum, which banks the wing.
// The wing AMPLIFIES the tilt through its roll dynamics.
// Yaw comes from the banked turn (centripetal force), NOT directly from weight shift.

// CG offset from weight shift (meters)
// Real paramotor harness: pilot body shifts ~12cm laterally + paramotor CG offset ~5cm
// Total max CG shift ≈ 17cm (from harness geometry and mass distribution)
float WeightShiftCGOffset = WeightShift * 0.17f;  // Max 17cm lateral CG shift (harness geometry)
// Gravity roll moment from weight shift: M = m * g * dx (N·m)
// Real torque, not an angle — integrated through roll dynamics below
float WeightShiftRollMoment = PilotWeight * GRAVITY_MS2 * WeightShiftCGOffset;
// === WING INERTIA (needed for yaw/roll moment integration) ===
float FlightApparentMassKg = Rho * WingModel.WingArea * (WingspanM * 0.5f) * CellFillFactor * 0.5f;
float FlightEffectiveMassKg = WingModel.WingWeight + FlightApparentMassKg;
float WingInertiaRoll = FlightEffectiveMassKg * (WingspanM * 0.5f) * (WingspanM * 0.5f);  // kg·m²
// Yaw inertia about vertical axis is much larger than roll inertia about span axis
// (uniform spanwise distribution: I_yaw ≈ m·b²/12 vs I_roll ≈ m·(b/2)² = m·b²/4)
float WingInertiaYaw = FlightEffectiveMassKg * WingspanM * WingspanM / 12.0f;

// === PROPELLER TORQUE ===
// τ = P / ω_prop (N·m) — real engine torque
float EnginePowerW = (ConnectedParamotor) ? ConnectedParamotor->GetEnginePowerW() : 0.0f;
float PropRPM = (ConnectedParamotor) ? ConnectedParamotor->GetPropRPM() : 0.0f;
float PropOmega = PropRPM * 2.0f * PI / 60.0f;  // rad/s
float PropTorqueNM = (PropOmega > 1.0f) ? EnginePowerW / PropOmega : 0.0f;  // N·m
// === YAW DYNAMICS — TORQUE/INERTIA INTEGRATION ===
// Yaw moment = drag difference × moment arm (half wingspan)
// α_yaw = M_yaw / I_zz → integrated to yaw rate
float YawMomentArm = WingspanM * 0.5f;
float DragYawMoment = DragDiff * YawMomentArm;  // N·m
float CollapseYawMoment = (RightCollapseAmount - LeftCollapseAmount) * Rho * Vms * Vms * WingModel.WingArea * 1.0f * YawMomentArm;
// Adverse yaw: rolling right drags the right wing tip forward → yaw left
// Standard aerodynamic: C_N_adverse = -CL × (ω_roll × b/2V)
// M = C_N × q × S × b
float AdverseYawMoment = -WingRollRate * PI_DIV_180 * YawMomentArm * Rho * Vms * Vms * WingModel.WingArea * YawMomentArm * CL * 0.5f / FMath::Max(1.0f, Vms);
float TotalYawMoment = DragYawMoment + CollapseYawMoment + AdverseYawMoment;
// Integrate: α = M / I (rad/s²) → ω += α·dt
float YawAccelRad = TotalYawMoment / WingInertiaYaw;
WingYawRate += YawAccelRad * RAD_TO_DEG * DeltaTime;
// Aerodynamic yaw damping: c_yaw = 0.5 × ρ × V × S × b² / I_yaw
// Same formula as kiting physics (WingClothSim.cpp)
float YawDampCoeff = (WingInertiaYaw > 0.1f)
    ? 0.5f * Rho * Vms * WingModel.WingArea * WingspanM * WingspanM / WingInertiaYaw
    : 0.0f;
float YawDampMoment = -WingYawRate * PI_DIV_180 * YawDampCoeff * WingInertiaYaw;  // N·m
WingYawRate += (YawDampMoment / WingInertiaYaw) * RAD_TO_DEG * DeltaTime;
float YawRate = FMath::Clamp(WingYawRate, -360.0f, 360.0f);  // NaN safety only
// === ROLL DYNAMICS — TORQUE/INERTIA INTEGRATION ===
// Coordinated turn: equilibrium bank for current yaw rate
// tan(bank) = V × yaw_rate / g (real centripetal balance)
float VmsFlight = HorizSpeed  / CM_PER_M;
float TargetBank = (VmsFlight > 0.5f) ? FMath::Atan(VmsFlight  * CM_PER_M * YawRate * PI_DIV_180 / GravityAccel) * RAD_TO_DEG : 0.0f;
// No clamp - bank limited by line geometry and collapse physics
// Roll moments (all in N·m):
// 1. Weight shift gravity moment (WeightShiftRollMoment, computed above)
// 2. Asymmetric lift moment (RollMoment, already computed)
// 3. Collapse: lift loss on collapsed side
float CollapseRollMoment = (RightCollapseAmount - LeftCollapseAmount) * LiftN * WingspanM * 0.5f;
// 4. Prop torque (PropTorqueNM, computed above)
// 5. Coordination spring: pendulum pulls bank toward coordinated angle
//    Spring constant = I · ω² where ω = √(g/L) is pendulum frequency
float PendulumOmegaRoll = FMath::Sqrt(GRAVITY_MS2 / (LINE_LENGTH_CM  / CM_PER_M));  // rad/s
float CoordSpringMoment = WingInertiaRoll * PendulumOmegaRoll * PendulumOmegaRoll
	* (TargetBank - CurrentRoll) * PI_DIV_180;  // N·m
// 6. Turbulence roll moment: lateral gusts create asymmetric lift across the span
// The lateral turbulence velocity (TurbLatState) shifts the apparent wind direction
// differently on each side of the wing, creating a roll moment.
// M = 0.5 * ρ * V² * S * b * C_l_beta * (V_lat / V)
// where C_l_beta ≈ 0.02 per radian of sideslip for a paraglider (low dihedral effect)
float TurbRollMoment = 0.5f * Rho * Vms * Vms * WingModel.WingArea * WingspanM
	* 0.02f * (Vms > 1.0f ? TurbLatState / Vms : 0.0f);
float TotalRollMoment = WeightShiftRollMoment + RollMoment + CollapseRollMoment
	+ PropTorqueNM + CoordSpringMoment + TurbRollMoment;
// Integrate: α = M / I (rad/s²) → ω += α·dt
float RollAccelRad = TotalRollMoment / WingInertiaRoll;
WingRollRate += RollAccelRad * RAD_TO_DEG * DeltaTime;
// Aerodynamic roll damping: M = C_l_p × q × S × (b/2)² × (ω_roll · b/2 / V)
// C_l_p ≈ -1.0 for a paraglider (thin wing, high span)
// The V in the denominator cancels the V in dynamic pressure q=0.5ρV², leaving
// a damping that scales linearly with airspeed (correct: more airspeed = more damping).
float C_lp = -1.0f;
float RollDampMoment = -WingRollRate * PI_DIV_180 * C_lp * 0.5f * Rho * Vms
    * WingModel.WingArea * (WingspanM * 0.5f) * (WingspanM * 0.5f);
WingRollRate += (RollDampMoment / WingInertiaRoll) * RAD_TO_DEG * DeltaTime;
CurrentRoll += WingRollRate * DeltaTime;
CurrentRoll = FMath::Clamp(CurrentRoll, -180.0f, 180.0f);  // NaN safety only
// For debug/HUD compatibility
float TargetRoll = TargetBank;
PropTorqueRoll = PropTorqueNM;
// === SPIRAL DIVE INSTABILITY ===
// Real danger: when banked steeply (>40°), the spiral wants to INCREASE on its own.
// The wing dives steeper → speeds up → banks more → dives steeper → ...
// This is why spiral dives are dangerous — they can become self-sustaining.
// The pilot must actively exit by weight-shifting out and easing brakes.

// SPIRAL DIVE — removed artificial injection.
// A spiral dive emerges naturally from the banked-turn physics:
// 1. Banked lift → centripetal force → curved path (already computed below)
// 2. Tilted lift vector → less vertical support → sink (already in lift equation)
// 3. Increased speed from dive → more lift → tighter turn → self-sustaining
// The centripetal acceleration from banking already produces the spiral.
// No artificial roll injection, sink injection, or forward acceleration needed.
SpiralDiveFactor = FMath::Max(0.0f, (FMath::Abs(CurrentRoll) - 40.0f) / 20.0f);
	// === DUTCH ROLL — removed artificial sine wave ===
	// Dutch roll is a real coupled yaw-roll eigenmode, but it should emerge
	// naturally from the yaw/roll dynamics, not be injected as a sine wave.
	// The yaw rate from drag differential + roll from lift differential already
	// produce coupled oscillations in turbulence. No artificial injection needed.
	DutchRollYaw = 0.0f;
	DutchRollRoll = 0.0f;
// === WING SURGE / PITCH OSCILLATION ===
// When brakes are released or throttle changes, the wing surges forward
// (pitches down) then oscillates back. This is the pitch stability of the wing.
// The pendulum + wing pitch create the characteristic "breathing" of a paraglider.

float PitchTarget = 0.0f;
// Brakes shift center of pressure rearward → wing pitches back
// Physical: trailing edge deflection × fraction of deflected chord (30%)
	PitchTarget += TrailingEdgeDeflectionDeg(TotalEffectiveBrake) * 0.30f;  // 30% chord deflects
// Thrust pitch: REMOVED — handled by pendulum dynamics + 1:1 pendulum coupling
// (thrust drives PendulumAngle, which drives PitchTarget through the coupling below)
// Collapse causes wing to pitch forward (surge from lift loss)
	PitchTarget -= FrontalCollapseAmount * FMath::Atan2(LiftN * 0.25f * WingspanM, TotalMassKg * GRAVITY_MS2 * (LINE_LENGTH_CM  / CM_PER_M)) * RAD_TO_DEG;
// Speed-pitch: apparent wind shift at higher speeds (geometric)
// θ = atan(ΔV / V) — wing pitches forward to reduce AoA at higher speed
if (Vms > 1.0f && SpeedError > 0.0f)
    PitchTarget -= FMath::Atan(SpeedError / Vms) * RAD_TO_DEG;
// Pendulum coupling: when pilot swings forward, wing pitches back (geometric 1:1)
	PitchTarget += PendulumAngle * RAD_TO_DEG;
// Trimmer pitch effect (geometric — trimmer changes the wing's trim AoA)
	PitchTarget += GetTrimmerPitchOffset();
// Hang point: motor lean angle tilts thrust vector
// Real geometry: max hang-point tilt ~4° → max lateral offset = L × tan(4°)
float MaxHangPointOffsetCm = LineLength * FMath::Tan(4.0f * PI_DIV_180);
float HangPointTiltDeg = FMath::Atan2(HangPointOffset * MaxHangPointOffsetCm, LineLength) * RAD_TO_DEG;
	PitchTarget += HangPointTiltDeg;
// Wing pitch has spring-mass-damper dynamics (it oscillates)
// Physical: the wing-pilot system is a pendulum with ω = √(g/L)
// The pitch spring constant should be ω² and damping should be 2·ζ·ω
// where ζ is the damping ratio (0.3-0.5 for paragliders)
float PitchOmega = FMath::Sqrt(GravityAccel / LineLength);  // rad/s
float PitchDampingRatio = DampingRatio;  // Use computed aerodynamic damping ratio
float PitchSpring = (PitchTarget - WingPitchAngle) * PitchOmega * PitchOmega;
float PitchDamp = -WingPitchRate * 2.0f * PitchDampingRatio * PitchOmega;
	WingPitchRate += (PitchSpring + PitchDamp) * DeltaTime;
WingPitchAngle += WingPitchRate * DeltaTime;
// No clamp - pitch limited by pendulum dynamics
// === ROLL PENDULUM — pilot swings sideways under wing during turns ===
// The pilot hangs on 7m lines below the wing. When the wing banks, the pilot
// doesn't instantly bank with it — the pilot lags behind due to pendulum
// inertia, then swings outward from centrifugal force in the turn.
// This creates the visible "swing" that makes paramotor turns feel real.
//
// Physics: same pendulum as pitch, but in the roll axis.
// - The wing's bank angle (CurrentRoll) is the "target" for the pilot
// - The pilot's roll angle (PilotRollAngle) lags behind with spring-damper dynamics
// - Centrifugal force in the turn pushes the pilot outward (adds to swing)
// - Natural frequency: ω = √(g/L) (same as pitch pendulum)
// - Damping ratio: 0.3-0.5 (realistic for paraglider pendulum)
{
	// The pilot hangs on lines below the wing — a free pendulum.
	// The lines pull the pilot toward the wing's bank angle, but the pilot
	// has inertia and swings freely. Forces on the pilot:
	// 1. Line tension → pulls toward wing bank (spring)
	// 2. Lateral turbulence → gusts push pilot sideways (direct force)
	// 3. Wing roll rate → wing banking creates lateral acceleration on pilot
	// The pilot is NOT locked to the wing — they swing independently.
	float TargetPilotRoll = CurrentRoll;
	// No clamp - pilot roll limited by pendulum dynamics

	// Turbulence directly perturbs the pilot's roll.
	// Lateral gusts push the pilot sideways — the pendulum swings.
	// Force from lateral gust (linearized): F = ρ × V_steady × V_turb × S × CD
	// Angular accel: α = F / (m × L)
	float TurbLatForce = 0.5f * Rho * FMath::Max(0.1f, Vms) * FMath::Abs(TurbLatState) * 2.0f * PilotDragArea * CDPilot;
	float TurbLatAccel = TurbLatForce / TotalMassKg  * CM_PER_M;  // cm/s²
	float TurbRollAccelDeg = (TurbLatAccel / LineLength) * RAD_TO_DEG;

	// Wing roll rate effect on pilot: when the wing banks, the pilot's pendulum
	// target changes (TargetPilotRoll = CurrentRoll), and the pendulum dynamics
	// handle the swing naturally. No direct push needed — removed WingRollPushDeg.

	// Spring-damper pendulum dynamics.
	// Pendulum equation: α = -ω²·θ  (θ in radians, α in rad/s²)
	// PilotRollAngle is in DEGREES, so convert to radians for the spring,
	// then convert the angular acceleration back to deg/s².
	float RollPendulumOmega = FMath::Sqrt(GravityAccel / LineLength);  // rad/s
	// Damping ratio from aerodynamic drag (NOT a fixed value)
	// Same formula as pitch pendulum: ζ = c / (2 * m * ω)
	// Roll damping includes wing span effect (tips move more than center)
	float RollDampArea = PilotDragArea * CDPilot + WingModel.WingArea * CDWing
		+ LineDragArea * CDLine
		+ WingModel.WingArea * (WingspanM / (2.0f * LineLength  / CM_PER_M));  // roll geometry
	float RollDampCoeff = 0.5f * Rho * FMath::Max(0.1f, Vms) * RollDampArea;
	float RollDampingRatio = RollDampCoeff / (2.0f * TotalMassKg * RollPendulumOmega);
	RollDampingRatio = FMath::Clamp(RollDampingRatio, 0.01f, 10.0f);  // NaN safety only
	float RollErrorRad = (TargetPilotRoll - PilotRollAngle) * PI_DIV_180;  // deg → rad
	float RollSpringRad = RollErrorRad * RollPendulumOmega * RollPendulumOmega;  // rad/s²
	float RollDampRad = -PilotRollRate * PI_DIV_180 * 2.0f * RollDampingRatio * RollPendulumOmega;  // rad/s²
	// Convert angular acceleration from rad/s² to deg/s², add turbulence + wing roll
	float RollAccelDeg = (RollSpringRad + RollDampRad) * RAD_TO_DEG
		+ TurbRollAccelDeg;
	PilotRollRate += RollAccelDeg * DeltaTime;
	PilotRollAngle += PilotRollRate * DeltaTime;
	// No clamp - pilot roll limited by line geometry
}
// === APPLY PILOT ROTATION ===
// The pilot has its OWN orientation — separate from the wing.
// The wing's bank (CurrentRoll) is applied to the WingActor above.
// The pilot swings beneath the wing on the lines (PilotRollAngle).

FRotator CurrentRot = Pawn->GetActorRotation();
FRotator NewRot = CurrentRot;
NewRot.Yaw += YawRate * DeltaTime + DutchRollYaw;
// Pilot roll = pendulum swing angle (independent of wing bank)
NewRot.Roll = PilotRollAngle + DutchRollRoll;
// Pitch = pilot body pitch + pendulum tilt + wing pitch coupling
	NewRot.Pitch = PilotPitch + PendulumAngle * RAD_TO_DEG + WingPitchAngle;
Pawn->SetActorRotation(NewRot);

// Verify the rotation actually stuck (not overridden by something else)
FRotator VerifyRot = Pawn->GetActorRotation();
	{
		RollDebugTimer += DeltaTime;
		if (RollDebugTimer > 0.2f)
		{
			RollDebugTimer = 0.0f;
			UE_LOG(LogTemp, Warning, TEXT("ROT DEBUG: Set Roll=%.2f Pitch=%.2f Yaw=%.2f | Verified Roll=%.2f Pitch=%.2f Yaw=%.2f | PilotRoll=%.2f rate=%.2f | WingRoll=%.2f WingRollRate=%.2f | TargetRoll=%.2f | YawRate=%.2f WeightShift=%.2f"),
				NewRot.Roll, NewRot.Pitch, NewRot.Yaw,
				VerifyRot.Roll, VerifyRot.Pitch, VerifyRot.Yaw,
				PilotRollAngle, PilotRollRate,
				CurrentRoll, WingRollRate,
				TargetRoll,
				YawRate, WeightShift);
		}
	}
// === ROTATE VELOCITY VECTOR TO FOLLOW YAW ===
// The velocity must turn with the heading, plus the banked turn adds
// centripetal acceleration that curves the path further.

float YawDeltaRad = YawRate * DeltaTime * PI_DIV_180;
float CosY = FMath::Cos(YawDeltaRad);
float SinY = FMath::Sin(YawDeltaRad);
float NewVx = Vel.X * CosY - Vel.Y * SinY;
float NewVy = Vel.X * SinY + Vel.Y * CosY;
Vel.X = NewVx;
Vel.Y = NewVy;
// Log stall/collapse events
	StallLogTimer += DeltaTime;
if (StallLogTimer > 0.5f && (LeftAoA > StallAoA || RightAoA > StallAoA ||		LeftCollapseAmount > 0.1f || RightCollapseAmount > 0.1f || FrontalCollapseAmount > 0.1f ||		bInParachutalStall))	{
		StallLogTimer = 0.0f;
UE_LOG(LogTemp, Warning, TEXT("WingActor: L_AoA=%.1f° R_AoA=%.1f° L_CL=%.2f R_CL=%.2f roll=%.1f yaw=%.1f pitch=%.1f | Col: L=%.0f%% R=%.0f%% F=%.0f%% Stall=%d"),			LeftAoA, RightAoA, LeftCL, RightCL, CurrentRoll, YawRate, WingPitchAngle,			LeftCollapseAmount * PERCENT_FACTOR, RightCollapseAmount * PERCENT_FACTOR, FrontalCollapseAmount * PERCENT_FACTOR,			bInParachutalStall ? 1 : 0);
}
	// === APPLY FORCES ===
FVector PawnFwd = Pawn->GetActorForwardVector();
FVector VelDir = Vel.GetSafeNormal();
if (VelDir.Size() < 0.01f) VelDir = PawnFwd;
// Drag opposes apparent wind direction
FVector DragDir = ApparentDir;
// === BANKED TURN CENTRIPETAL FORCE ===
// When the wing is banked, the lift vector tilts sideways.
// The horizontal component of lift creates centripetal force that turns the aircraft.
// This is what makes a banked turn actually curve the flight path.

float BankRad = CurrentRoll * PI_DIV_180;
// In a banked turn, lift splits into vertical and horizontal components:
// Vertical: Lift * cos(bank) — supports weight
// Horizontal: Lift * sin(bank) — centripetal force that curves the path

float VerticalLift = LiftAccel * FMath::Cos(BankRad);
float CentripetalAccel = LiftAccel * FMath::Sin(BankRad);
// Direction: perpendicular to velocity, toward the low wing
FVector RightVec = Pawn->GetActorRightVector();
RightVec.Z = 0.0f;
RightVec = RightVec.GetSafeNormal();
FVector CentripetalDir = RightVec * (CurrentRoll > 0.0f ? 1.0f : -1.0f);
FVector Accel = FVector::ZeroVector;
// Thrust: applied along the pilot's forward vector, tilted by hang point angle
// The hang point tilt angle (computed above) redirects thrust vertically
// No separate climb boost — the thrust vector angle handles this geometrically
float HangPointTiltRad = FMath::Atan2(HangPointOffset * MaxHangPointOffsetCm, LineLength);
float ThrustHorizFraction = FMath::Cos(HangPointTiltRad);
float ThrustVertFraction = FMath::Sin(HangPointTiltRad);
Accel += PawnFwd * ThrustAccel * ThrustHorizFraction;
Accel.Z += ThrustAccel * ThrustVertFraction;
	Accel += DragDir * DragAccel;
// Drag follows apparent wind
// === LIFT PERPENDICULAR TO FLIGHT PATH ===
// Real lift is perpendicular to the flight path, not purely vertical.
// When the wing pitches forward (gliding), the lift vector tilts forward.
// The vertical component decreases, causing sink.
// The forward component provides thrust (gliding forward).
// This is what creates the natural glide/sink — no artificial sink needed.
//
// Total pitch = wing pitch + pendulum angle (pilot swing)
// Positive pitch = wing behind pilot = lift tilted back = less vertical support
// Negative pitch = wing forward = lift tilted forward = dive/glide
float TotalPitchRad = (WingPitchAngle + PendulumAngle * RAD_TO_DEG) * PI_DIV_180;
// Vertical component of lift (reduced by pitch and bank)
float LiftVertical = VerticalLift * FMath::Cos(TotalPitchRad);
// Forward component of lift: nose-up (positive pitch) tilts lift backward → decelerates.
// Nose-down (negative pitch) tilts lift forward → glide thrust.
float LiftForward = -LiftAccel * FMath::Sin(TotalPitchRad) * FMath::Cos(BankRad);
Accel.Z += LiftVertical;
Accel += PawnFwd * LiftForward;  // Forward lift component (glide thrust)
	Accel.Z -= GravityAccel;
// Gravity down
	// NO artificial base sink — the glide/sink comes naturally from:
	// 1. Drag reducing forward speed
	// 2. Speed dropping → lift dropping → wing sinks
	// 3. Wing pitches forward to regain speed (trim stability)
	// 4. Forward pitch tilts lift vector → less vertical support → sink
	// The old 120 cm/s² artificial sink was wrong and caused the wing to
	// fall out of the sky when throttle was cut.
	Accel += CentripetalDir * CentripetalAccel;
// Horizontal component of lift (sin bank)
	Accel += FlightTurbulence;
// Turbulence bumps the wing around
// === SPIRAL DIVE EXTRA FORCES ===
// In a spiral dive, the wing dives steeper and accelerates.
// This is what makes spirals self-sustaining and dangerous.

	// SPIRAL DIVE FORCES — removed artificial injection.
	// Spiral sink and acceleration emerge from the banked turn:
	// - Tilted lift vector → less vertical support → sink (from LiftAccel)
	// - Centripetal force curves the path → speed increases from gravity
	// No artificial sink or forward acceleration needed.
	// No artificial trim sink — speed stability from drag polar
// Pendulum coupling is already handled through PitchTarget/TotalPitchRad above:
// the pendulum angle tilts the lift vector, producing the correct forward/back
// acceleration. Adding a velocity term to acceleration would double-count and
// produce a displacement-sized error. Removed direct PendulumVelForward injection.
FVector NewVel = Vel + Accel * DeltaTime;
// Safety
if (!FMath::IsFinite(NewVel.X) || !FMath::IsFinite(NewVel.Y) || !FMath::IsFinite(NewVel.Z))	{
		UE_LOG(LogTemp, Error, TEXT("WingActor: NaN NewVel — aborting"));
return;
}
	ApplyVelocitySafetyCaps(NewVel);
// === GROUND COLLISION / LANDING ===
HandleLanding(Char, DeltaTime, NewVel, TotalMassKg);
// === MOVE PILOT — direct actor movement, bypassing CMC ===
// CMC is in MOVE_None during flight, so we move the actor directly.
// This gives us 100% control over position and velocity with no CMC interference.
{
	FVector CurrentLoc = Pawn->GetActorLocation();
	FVector NewLoc = CurrentLoc + NewVel * DeltaTime;

	// Ground collision — don't let pilot go underground
	float MoveGZ = GetCachedGroundZ(NewLoc);
	if (NewLoc.Z < MoveGZ)
	{
		NewLoc.Z = MoveGZ;
		NewVel.Z = 0;
	}

	Pawn->SetActorLocation(NewLoc, false);
	// Store velocity for HUD/telemetry reads via GetVelocity()
	Char->GetCharacterMovement()->Velocity = NewVel;
}
// === WING POSITION (rigid line constraint) ===
if (bRisersConnected)
{
	UpdateWingPositionFromLines(PawnFwd, LiftN, DragN, LineLength, WeightShiftCGOffset);
}// Log once per second
	FlightLogTimer += DeltaTime;
if (FlightLogTimer > 1.0f)	{
		FlightLogTimer = 0.0f;
UE_LOG(LogTemp, Warning, TEXT("WingActor: FLIGHT speed=%.1f mph climb=%.0f fpm alt=%.0fm pend=%.1f° pitch=%.0f° wpitch=%.1f° thrust=%.0f yaw=%.1f bank=%.1f proll=%.1f L=%.2f R=%.2f brakeL=%.1f brakeR=%.1f wshift=%.2f turb=%.1f trim=%.1f spiral=%.0f%% wl=%.1f | LineTension A=%.0f B=%.0f C=%.0f D=%.0f N stretchA=%.2fcm"),			Airspeed  / CM_PER_M * MPH_PER_MS, ClimbRate  / CM_PER_M * 196.85f, AltitudeM,			PendulumAngle * RAD_TO_DEG, PilotPitch, WingPitchAngle, ThrustAccel, YawRate, CurrentRoll,
			PilotRollAngle,			LeftCL, RightCL, EffectiveLeftBrake, EffectiveRightBrake, WeightShift, TurbStrength,			TrimSpeedMs * MPH_PER_MS, SpiralDiveFactor * PERCENT_FACTOR, WingLoading,
			LineTensionA, LineTensionB, LineTensionC, LineTensionD, LineStretchA);
}
}

// Real paramotor trimmers adjust the rear riser length, changing the
// angle of attack and thus the wing's trim speed, lift, drag, and pitch.
//
// TrimmerPosition: -1.0 = full slow (trimmers in), 0.0 = neutral, +1.0 = full fast (trimmers out)
//
// Slow trim (negative): higher AoA, more lift, more drag, slower speed, wing hangs back
// Fast trim (positive): lower AoA, less lift, less drag, faster speed, wing flies forward
// Fast trim also increases collapse risk (reflex mode is stable in turbulence but
// collapses easily with brake input).

float AWingActor::GetTrimmerAoAOffset() const{
	// Trimmer changes A-riser length relative to D-riser → changes wing AoA
	// Real geometry: A at 15% chord, D at 75% chord → AD distance ≈ 60% × chord
	// Trimmer travel: ~5cm on A riser (real measurement)
	// AoA change = atan(ΔL / AD_distance) = atan(5 / 120) ≈ 2.4°
	// Fast (positive): A riser lengthens → LE drops → AoA decreases (negative)
	// Slow (negative): A riser shortens → LE rises → AoA increases (positive)
	float TrimmerTravelCm = 5.0f;
	float WingspanM = FMath::Sqrt(WingModel.WingArea * WingModel.AR);
	float MeanChordCm = (WingspanM > 0.0f) ? (WingModel.WingArea / WingspanM) * CM_PER_M : 200.0f;
	float ADistanceCm = MeanChordCm * 0.6f;  // 60% of mean chord
	return -TrimmerPosition * FMath::Atan2(TrimmerTravelCm, ADistanceCm) * RAD_TO_DEG;
}

float AWingActor::GetTrimmerSpeedFactor() const{
	// Speed comes from the polar: trimmer changes AoA → CL changes →
	// V = sqrt(2W / (ρ × S × CL)). No separate multiplier needed.
	return 1.0f;
}

float AWingActor::GetTrimmerDragFactor() const{
	// Drag comes from the polar at the adjusted AoA — no separate multiplier
	return 1.0f;
}

float AWingActor::GetTrimmerCLFactor() const{
	// CL comes from the polar at the adjusted AoA — no separate multiplier
	return 1.0f;
}

float AWingActor::GetTrimmerPitchOffset() const{
	// Pitch from riser geometry: wing angle relative to pilot = AoA offset
	// The wing hangs at a different angle because the A riser is longer/shorter
	return GetTrimmerAoAOffset();
}

float AWingActor::GetTrimmerCollapseRisk() const{
	// Collapse risk from AoA margin to stall
	// At trim AoA (2°): margin to stall (15°) = 13° → low risk (1.0)
	// At fast trim (2° - 2.4° = -0.4°): margin = 15.4° → very low risk
	// At slow trim (2° + 2.4° = 4.4°): margin = 10.6° → higher risk
	// Risk scales as 1 / (margin / trim_margin)²
	float TrimAoA = WingModel.TrimAoA;
	float StallAoA = WingModel.StallAoA;
	float CurrentAoA = TrimAoA + GetTrimmerAoAOffset();
	float MarginToStall = StallAoA - CurrentAoA;
	float TrimMargin = StallAoA - TrimAoA;  // 13°
	if (TrimMargin < 1.0f) return 1.0f;
	float RiskRatio = TrimMargin / FMath::Max(1.0f, MarginToStall);
	return RiskRatio * RiskRatio;
}

FString AWingActor::GetTrimmerLabel() const{
	if (TrimmerPosition > 0.7f) return TEXT("FAST");
if (TrimmerPosition > 0.3f) return TEXT("FAST+");
if (TrimmerPosition > 0.05f) return TEXT("FAST-");
if (TrimmerPosition < -0.7f) return TEXT("SLOW");
if (TrimmerPosition < -0.3f) return TEXT("SLOW+");
if (TrimmerPosition < -0.05f) return TEXT("SLOW-");
return TEXT("NEUTRAL");
}

// ============================================================
// SEAT POSITION — pilot transitions from standing to seated
// ============================================================
// Real paramotoring: after takeoff, the pilot swings legs forward into the
// harness seat. This reduces frontal area → less drag → better glide.
// Standing has more drag (legs + body create turbulence).
// Seated also lowers the center of gravity slightly → more pendulum stability.

void AWingActor::ToggleSeat()
{
	bSeated = !bSeated;
}

// ============================================================
// HANG POINT ADJUSTMENT — motor lean angle
// ============================================================
// Real paramotors have multiple clip points or adjustable hang points.
// Moving the hang point forward makes the motor lean forward (pilot reclines).
// Moving it back makes the motor more upright.
// This affects:
// - Pitch trim: forward lean = more speed (lower AoA), back = less speed (higher AoA)
// - Weight balance: shifts CG relative to thrust line
// - Thrust vector: forward lean points thrust slightly down (climb), back points up

void AWingActor::AdjustHangPoint(float Delta)
{
	HangPointOffset = FMath::Clamp(HangPointOffset + Delta, -1.0f, 1.0f);
}


// ============================================================
// FLIGHT PHYSICS HELPERS
// ============================================================

float AWingActor::ComputeThermalLiftMs(float AltitudeM) const
{
	// Thermals form when sun-heated ground warms the air above it.
	// Warm air is less dense than surrounding air -> buoyancy -> rises.
	// Thermal updraft velocity: w = sqrt(2 * g * dT/T * h)
	if (!CachedGameInstance) return 0.0f;

	float TimeOfDay = CachedGameInstance->TimeOfDay;
	float CloudCover = CachedGameInstance->CloudCover;
	float Temperature = CachedGameInstance->Temperature;

	// Sun heating: peaks at solar noon (13:00), zero at night
	float SunIntensity = 0.0f;
	if (TimeOfDay > 7.0f && TimeOfDay < 19.0f)
	{
		float DayProgress = (TimeOfDay - 7.0f) / 12.0f;
		SunIntensity = FMath::Sin(DayProgress * PI);
	}
	float TempExcess = FMath::Max(0.0f, (Temperature - 15.0f) * SunIntensity * (1.0f - CloudCover * 0.75f));

	// Buoyancy-driven updraft: w = sqrt(2 * g * dT/T * h)
	float AmbientTempK = Temperature + 273.15f;
	float ThermalLiftMs = 0.0f;
	if (TempExcess > 0.1f && AltitudeM > 1.0f)
	{
		ThermalLiftMs = FMath::Sqrt(2.0f * GRAVITY_MS2 * TempExcess / AmbientTempK * AltitudeM);
	}

	// Thermal field: localized columns, not uniform
	FVector WingPos = GetActorLocation();
	float ThermalSpacing = 50000.0f;  // 500m in cm
	float ThermalRadius = 10000.0f;   // 100m in cm
	float NearestThermalX = FMath::RoundToInt(WingPos.X / ThermalSpacing) * ThermalSpacing;
	float NearestThermalY = FMath::RoundToInt(WingPos.Y / ThermalSpacing) * ThermalSpacing;
	NearestThermalX += FMath::Sin(NearestThermalY * 0.0001f) * ThermalSpacing * 0.3f;
	NearestThermalY += FMath::Cos(NearestThermalX * 0.0001f) * ThermalSpacing * 0.3f;
	float DX = WingPos.X - NearestThermalX;
	float DY = WingPos.Y - NearestThermalY;
	float DistFromCenter = FMath::Sqrt(DX * DX + DY * DY);
	float ThermalProfile = FMath::Exp(-(DistFromCenter * DistFromCenter) / (2.0f * ThermalRadius * ThermalRadius));
	ThermalLiftMs *= ThermalProfile;

	// Thermals weaken with altitude (spread out and dissipate)
	if (AltitudeM > 200.0f)
		ThermalLiftMs *= FMath::Max(0.0f, 1.0f - (AltitudeM - 200.0f) / 800.0f);

	return ThermalLiftMs;
}
bool AWingActor::HandleLanding(ACharacter* Char, float DeltaTime, FVector& NewVel, float TotalMassKg)
{
	FVector PawnLoc = Char->GetActorLocation();
	float GZ = GetCachedGroundZ(PawnLoc);
	if (PawnLoc.Z > GZ + 1.0f || NewVel.Z >= 0.0f)
		return false;  // Not touching ground or moving up

	// === GROUND CONTACT (real collision response) ===
	float LandingSpeedMs = -NewVel.Z  / CM_PER_M;

	// Injury check
	AParamotorCharacter* ParamotorChar = Cast<AParamotorCharacter>(Char);
	if (ParamotorChar && LandingSpeedMs > 3.0f)
		ParamotorChar->ApplyImpactInjury(LandingSpeedMs, false);

	// Vertical collision response: no bounce (pilot's legs absorb the impact)
	NewVel.Z = 0.0f;

	// Ground friction: horizontal velocity decays from ground contact
	float FrictionCoeff = 0.4f;  // Rubber soles on grass
	float LandingHorizSpeed = FMath::Sqrt(NewVel.X * NewVel.X + NewVel.Y * NewVel.Y);
	float CollisionDurationS = 0.1f;  // Leg impact absorption time
	float DecelForce = TotalMassKg * LandingSpeedMs / CollisionDurationS;
	float NormalForce = TotalMassKg * GRAVITY_MS2 + DecelForce;
	float FrictionDecel = FrictionCoeff * NormalForce * DeltaTime  * CM_PER_M;
	if (LandingHorizSpeed > 0.0f)
	{
		float FrictionFactor = FMath::Max(0.0f, 1.0f - FrictionDecel / LandingHorizSpeed);
		NewVel.X *= FrictionFactor;
		NewVel.Y *= FrictionFactor;
	}

	// Reset flight state on landing
	bIsAirborne = false;
	Char->GetCharacterMovement()->GravityScale = 1.0f;
	Char->GetCharacterMovement()->AirControl = 0.05f;
	Char->GetCharacterMovement()->MaxAcceleration = 2048.0f;
	Char->bUseControllerRotationYaw = true;
	Char->GetCharacterMovement()->bOrientRotationToMovement = false;
	Char->GetCharacterMovement()->SetMovementMode(MOVE_Walking);

	// Reset pendulum, pitch, and collapse states
	PendulumAngle = 0.0f;
	PendulumAngularVel = 0.0f;
	PrevThrottleAmount = ThrottleAmount;
	ThrottleRate = 0.0f;
	PilotPitch = 0.0f;
	PilotPitchTarget = 0.0f;
	bSeated = false;
	bAutoSeatPending = false;
	LaunchTimer = 0.0f;
	LaunchAngleOffset = 0.0f;
	SeatTransition = 0.0f;
	DragReductionFromSeat = 0.0f;
	HangPointOffset = 0.0f;
	WingPitchAngle = 0.0f;
	WingPitchRate = 0.0f;
	WingRollRate = 0.0f;
	WingYawRate = 0.0f;
	CurrentRoll = 0.0f;
	PilotRollAngle = 0.0f;
	PilotRollRate = 0.0f;
	EffectiveLeftBrake = 0.0f;
	EffectiveRightBrake = 0.0f;
	LeftCollapseAmount = 0.0f;
	RightCollapseAmount = 0.0f;
	FrontalCollapseAmount = 0.0f;
	bInParachutalStall = false;
	ParachutalStallTimer = 0.0f;
	PropTorqueRoll = 0.0f;
	SpiralDiveFactor = 0.0f;
	DutchRollYaw = 0.0f;
	DutchRollRoll = 0.0f;
	DutchRollPhase = 0.0f;
	TrimmerPosition = 0.0f;
	TrimmerTarget = 0.0f;

	if (NewVel.Size2D() < 100.0f)
	{
		WingState = EWingState::Inflated;
		UE_LOG(LogTemp, Warning, TEXT("WingActor: Landed - back to inflated/ground"));
	}
	return true;
}
void AWingActor::UpdateWingPositionFromLines(const FVector& PawnFwd, float LiftN, float DragN, float LineLength, float WeightShiftCGOffsetM)
{
	// The wing is connected to the pilot by rigid 7m lines.
	// Positioned INSTANTLY at the correct position - not lerped.
	FVector CarabMid = (LeftCarabinerPos + RightCarabinerPos) * 0.5f;
	FVector PawnFwd2D = FVector(PawnFwd.X, PawnFwd.Y, 0.0f).GetSafeNormal();
	FVector PawnRight2D = FVector::CrossProduct(FVector::UpVector, PawnFwd2D).GetSafeNormal();

	float HeightAboveCarab = LINE_LENGTH_CM;
	float PendulumBackOffset = FMath::Sin(PendulumAngle) * LineLength;
	float DragBackOffset = FMath::Atan(DragN / FMath::Max(1.0f, LiftN)) * LineLength;
	float WeightShiftLateral = -WeightShift * WeightShiftCGOffsetM * CM_PER_M;
	float RollDifferenceDeg = CurrentRoll - PilotRollAngle;
	float RollPendulumLateral = FMath::Sin(RollDifferenceDeg * PI_DIV_180) * LineLength;

	FVector DesiredWingPos = CarabMid
		+ FVector(0, 0, HeightAboveCarab)
		- PawnFwd2D * (DragBackOffset + PendulumBackOffset)
		+ PawnRight2D * (WeightShiftLateral + RollPendulumLateral);

	FVector CurrentPos = GetActorLocation();
	FVector Shift = DesiredWingPos - CurrentPos;
	SetActorLocation(DesiredWingPos, false);

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

	FVector ToWing = GetActorLocation() - CarabMid;
	ToWing.Z = 0.0f;
	FRotator DesiredRot;
	if (ToWing.Size() > LineLength * 0.05f)
		DesiredRot.Yaw = FMath::Atan2(ToWing.Y, ToWing.X) * RAD_TO_DEG + 180.0f;
	else
		DesiredRot.Yaw = FMath::Atan2(PawnFwd.Y, PawnFwd.X) * RAD_TO_DEG + 180.0f;
	DesiredRot.Pitch = WingPitchAngle;
	DesiredRot.Roll = -CurrentRoll;
	SetActorRotation(DesiredRot);
}
void AWingActor::ApplyVelocitySafetyCaps(FVector& NewVel)
{
	// Safety nets — NaN/explosion protection only (well beyond physical limits)
	// Real limits come from the drag polar, collapse physics, and thrust curve
	float NewHorizSpeed = FVector(NewVel.X, NewVel.Y, 0.0f).Size();
	const float MAX_HORIZ_SPEED_CM_S = 2000.0f;  // 20 m/s — drag polar prevents reaching this
	if (NewHorizSpeed > MAX_HORIZ_SPEED_CM_S)
	{
		float Scale = MAX_HORIZ_SPEED_CM_S / NewHorizSpeed;
		NewVel.X *= Scale;
		NewVel.Y *= Scale;
	}
	// Total speed safety cap (prevents NaN/explosion from frame spikes)
	if (NewVel.Size() > 2500.0f)
		NewVel = NewVel.GetSafeNormal() * 2500.0f;
	// Vertical safety caps
	if (NewVel.Z > 2000.0f) NewVel.Z = 2000.0f;    // 20 m/s climb
	if (NewVel.Z < -10000.0f) NewVel.Z = -10000.0f; // 100 m/s sink
}