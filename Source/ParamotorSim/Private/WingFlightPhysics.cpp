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

void AWingActor::UpdateFlight(float DeltaTime)
{
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
	if (ConnectedParamotor)
		Pawn = Cast<APawn>(ConnectedParamotor->GetAttachParentActor());
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
	TrimmerPosition = FMath::FInterpTo(TrimmerPosition, TrimmerTarget, DeltaTime, TrimmerSpeed);

	// Maintain canopy shape
	ApplyInflatedShape(DeltaTime);

	// Current velocity from the movement component (authoritative when flying)
	FVector Vel = Char->GetCharacterMovement()->Velocity;
	// Safety: NaN check
	if (!FMath::IsFinite(Vel.X) || !FMath::IsFinite(Vel.Y) || !FMath::IsFinite(Vel.Z))
		Vel = FVector::ZeroVector;
	if (Vel.Size() > 2000.0f)
		Vel = Vel.GetSafeNormal() * 2000.0f;
	Airspeed = Vel.Size();
	ClimbRate = Vel.Z;

	// === PER-FRAME CONTEXT ===
	// FFlightContext holds all locals shared between helper sections.
	// Helpers are called in the exact same order as the original code
	// to preserve data flow (later sections read values from earlier ones).
	FFlightContext C;
	C.Pawn = Pawn;
	C.Char = Char;
	C.Vel = Vel;
	C.DeltaTime = DeltaTime;

	UpdateFlightEnvironment(C);
	UpdateAerodynamicCoefficients(C);
	UpdateCollapseDetection(C);
	UpdateLineTensionAndStretch(C);
	UpdateFlightForces(C);
	UpdatePendulumAndPitch(C);
	UpdateYawRollDynamics(C);
	UpdatePilotOrientation(C);
	ApplyFlightForces(C);
}

// ============================================================
// UpdateFlightEnvironment — wind, altitude, density, turbulence,
// thermals, cell pressure, turbulence AoA
// ============================================================
void AWingActor::UpdateFlightEnvironment(FFlightContext& C)
{
	// === WIND (dynamic — gusts continue in flight) ===
	float FlightGustAmp = 0.0f;
	if (CachedGameInstance)
		FlightGustAmp = CachedGameInstance->WindGusts;
	UpdateDynamicWind(C.DeltaTime, FlightGustAmp);
	C.ShiftedWindDir = WindDirection.RotateAngleAxis(WindDirShift * RAD_TO_DEG, FVector(0, 0, 1));
	C.Wind = C.ShiftedWindDir * CurrentWindSpeed * CM_PER_M;
	// Apparent wind = true wind - velocity (wind the wing feels)
	C.ApparentWind = C.Wind - C.Vel;
	// === HORIZONTAL AIRSPEED (for lift) ===
	C.HorizApparent = FVector(C.ApparentWind.X, C.ApparentWind.Y, 0.0f);
	C.HorizSpeed = C.HorizApparent.Size();
	C.Vms = C.HorizSpeed / CM_PER_M;
	// === AERODYNAMIC COEFFICIENTS — PER-SIDE STALL MODEL ===
	C.Rho = AIR_DENSITY_SEA_LEVEL;  // Will be updated after altitude is computed
	C.HalfArea = WingModel.WingArea * 0.5f;
	C.StallAoA = WingModel.StallAoA;
	// === BRAKE RESPONSE LAG ===
	float BrakeLagSpeed = 1.0f / (LINE_LENGTH_CM / CM_PER_M / 160.0f + 0.15f);
	EffectiveLeftBrake = FMath::FInterpTo(EffectiveLeftBrake, LeftBrakeAmount, C.DeltaTime, BrakeLagSpeed);
	EffectiveRightBrake = FMath::FInterpTo(EffectiveRightBrake, RightBrakeAmount, C.DeltaTime, BrakeLagSpeed);
	// === WIND GRADIENT WITH ALTITUDE ===
	const FVector EarlyPawnLoc = C.Pawn->GetActorLocation();
	float EarlyPawnZ = EarlyPawnLoc.Z;
	float EarlyGZ = GetCachedGroundZ(EarlyPawnLoc);
	C.AltitudeM = FMath::Max(0.0f, (EarlyPawnZ - EarlyGZ) / CM_PER_M);
	C.Rho = AirDensityAtAltitude(C.AltitudeM);
	float WindGradientAlpha = 0.12f;  // Coastal terrain
	float WindRefHeight = 10.0f;
	float HeightRatio = FMath::Max(0.01f, C.AltitudeM / WindRefHeight);
	float WindGradientFactor = FMath::Pow(HeightRatio, WindGradientAlpha);
	float AltitudeWindSpeed = CurrentWindSpeed * WindGradientFactor;
	C.AltitudeWind = C.ShiftedWindDir * AltitudeWindSpeed * CM_PER_M;
	// === TURBULENCE — add to freestream velocity, not as fake acceleration ===
	C.TurbStrength = FMath::Abs(TurbLongState) + FMath::Abs(TurbLatState) + FMath::Abs(TurbVertState);
	C.TurbulenceVelocity = (C.ShiftedWindDir * TurbLongState
		+ FVector::CrossProduct(FVector::UpVector, C.ShiftedWindDir).GetSafeNormal() * TurbLatState
		+ FVector::UpVector * TurbVertState)
		* CM_PER_M;
	// Add turbulent velocity to wind before computing apparent wind
	C.ApparentWind = (C.AltitudeWind + C.TurbulenceVelocity) - C.Vel;
	C.HorizApparent = FVector(C.ApparentWind.X, C.ApparentWind.Y, 0.0f);
	C.HorizSpeed = C.HorizApparent.Size();
	C.Vms = C.HorizSpeed / CM_PER_M;
	FlightTurbulence = FVector::ZeroVector;
	// === REAL CELL PRESSURE MODEL ===
	UpdateCellPressure(C.DeltaTime, C.Vms, EffectiveLeftBrake, EffectiveRightBrake);
	// === THERMALS (buoyancy-based updraft model) ===
	C.ThermalLiftMs = ComputeThermalLiftMs(C.AltitudeM);
	C.ApparentWind.Z += C.ThermalLiftMs * CM_PER_M;
	// Per-side AoA: base cruise + effective brake (with lag) + turbulence
	C.TurbulenceAoA = (C.Vms > 1.0f) ? TurbVertState / C.Vms * RAD_TO_DEG : 0.0f;
}

// ============================================================
// UpdateAerodynamicCoefficients — brake AoA, polar lookup,
// per-side CL/CD, ground effect
// ============================================================
void AWingActor::UpdateAerodynamicCoefficients(FFlightContext& C)
{
	auto BrakeToAoA = [](float Brake) -> float
	{
		return TrailingEdgeDeflectionDeg(Brake) * 0.8f;  // 0.8 = fabric flex factor
	};
	// Trim AoA. At 2° the scaled polar gives CL ≈ 0.72, which produces level
	// flight at ~10 m/s for the Charger 25M (lift ≈ weight). 3° gave CL ≈ 0.85,
	// causing excess lift / floaty climb at trim.
	C.LeftAoA = WingModel.TrimAoA + BrakeToAoA(EffectiveLeftBrake) + C.TurbulenceAoA + GetTrimmerAoAOffset();
	C.RightAoA = WingModel.TrimAoA + BrakeToAoA(EffectiveRightBrake) + C.TurbulenceAoA + GetTrimmerAoAOffset();
	// Wing pitch from surge/oscillation affects AoA (1:1 for small angles)
	float PitchAoAEffect = WingPitchAngle * 1.0f;
	C.LeftAoA += PitchAoAEffect;
	C.RightAoA += PitchAoAEffect;
	// === REAL AIRFOIL POLAR LOOKUP ===
	auto ComputeCLFromPolar = [&](float AoA, float WingCL0, float WingCLMax, float BrakeInput) -> float
	{
		float PolarCL, PolarCD;
		LookupAirfoilPolarWithCamber(AoA, BrakeInput, PolarCL, PolarCD);
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
		float BaseAirfoilCD0 = 0.0082f;  // NACA 4415 at zero-lift (XFOIL)
		float ProfileCD = CD0 + FMath::Max(0.0f, PolarCD - BaseAirfoilCD0);
		float Denom = PI * AR * e;
		float InducedCD = Denom > 0.0f ? CL * CL / Denom : 0.0f;
		return ProfileCD + InducedCD;
	};
	C.LeftCL = ComputeCLFromPolar(C.LeftAoA, WingModel.CL0 * GetTrimmerCLFactor(), WingModel.CLMax, EffectiveLeftBrake);
	C.RightCL = ComputeCLFromPolar(C.RightAoA, WingModel.CL0 * GetTrimmerCLFactor(), WingModel.CLMax, EffectiveRightBrake);
	C.LeftCD = ComputeCDFromPolar(C.LeftAoA, C.LeftCL, WingModel.CD0 * GetTrimmerDragFactor(), WingModel.AR, OswaldEfficiency, EffectiveLeftBrake);
	C.RightCD = ComputeCDFromPolar(C.RightAoA, C.RightCL, WingModel.CD0 * GetTrimmerDragFactor(), WingModel.AR, OswaldEfficiency, EffectiveRightBrake);
	// === GROUND EFFECT — reduce induced drag near ground ===
	const FVector PawnLocAGL = C.Pawn->GetActorLocation();
	float WingAltitudeAGL = PawnLocAGL.Z - GetCachedGroundZ(PawnLocAGL);
	C.WingspanM = FMath::Sqrt(WingModel.WingArea * WingModel.AR);
	C.GroundEffectFactor = 1.0f - FMath::Exp(-WingAltitudeAGL / CM_PER_M / (C.WingspanM * 0.5f));
	float BaseAirfoilCD0 = 0.0082f;
	float ProfileCD0 = WingModel.CD0 * GetTrimmerDragFactor();
	float LeftInduced = FMath::Max(0.0f, C.LeftCD - ProfileCD0);
	float RightInduced = FMath::Max(0.0f, C.RightCD - ProfileCD0);
	C.LeftCD = ProfileCD0 + LeftInduced * C.GroundEffectFactor;
	C.RightCD = ProfileCD0 + RightInduced * C.GroundEffectFactor;
}

// ============================================================
// UpdateCollapseDetection — asymmetric, frontal, parachutal stall
// ============================================================
void AWingActor::UpdateCollapseDetection(FFlightContext& C)
{
	// === COLLAPSE DETECTION (DETERMINISTIC — from AoA and pressure) ===
	float TipStallMargin = 2.0f;
	bool bLeftCollapseTrigger = (C.LeftAoA < -WingModel.StallAoA * 0.15f) || (C.LeftAoA > C.StallAoA + FMath::Abs(C.TurbulenceAoA) && EffectiveLeftBrake > 0.5f);
	bool bRightCollapseTrigger = (C.RightAoA < -WingModel.StallAoA * 0.15f) || (C.RightAoA > C.StallAoA + FMath::Abs(C.TurbulenceAoA) && EffectiveRightBrake > 0.5f);
	if (C.LeftAoA > C.StallAoA - TipStallMargin)
		bLeftCollapseTrigger = true;
	if (C.RightAoA > C.StallAoA - TipStallMargin)
		bRightCollapseTrigger = true;
	float TurbAoAVariation = (C.Vms > 1.0f) ? FMath::Abs(TurbVertState) / C.Vms * RAD_TO_DEG : 0.0f;
	float FlightWeightRatio = WingModel.MaxTakeoffWeight > 0.0f ? PilotWeight / WingModel.MaxTakeoffWeight : 1.0f;
	float FlightCollapseResistance = FMath::Max(0.1f, FlightWeightRatio);
	float LeftAoAWithTurb = C.LeftAoA + TurbAoAVariation / FlightCollapseResistance * GetTrimmerCollapseRisk();
	float RightAoAWithTurb = C.RightAoA + TurbAoAVariation / FlightCollapseResistance * GetTrimmerCollapseRisk();
	if (LeftAoAWithTurb > C.StallAoA || LeftAoAWithTurb < -WingModel.StallAoA * 0.15f)
		bLeftCollapseTrigger = true;
	if (RightAoAWithTurb > C.StallAoA || RightAoAWithTurb < -WingModel.StallAoA * 0.15f)
		bRightCollapseTrigger = true;
	// Update collapse amounts — rates from cell pressure physics
	C.CellDeflateRate = FMath::Max(0.5f, C.Vms * 0.33f);
	C.CellRefillRate = FMath::Max(0.3f, C.Vms * 0.20f);
	float MeanChordM = (C.WingspanM > 0.0f) ? WingModel.WingArea / C.WingspanM : 2.0f;
	C.RefillDelay = FMath::Max(0.1f, MeanChordM / FMath::Max(1.0f, C.Vms));
	if (bLeftCollapseTrigger) {
		LeftCollapseAmount = FMath::Min(1.0f, LeftCollapseAmount + C.DeltaTime * C.CellDeflateRate);
		CollapseRecoveryTimer = 0.0f;
	}
	else if (LeftCollapseAmount > 0.0f) {
		CollapseRecoveryTimer += C.DeltaTime;
		if (CollapseRecoveryTimer > C.RefillDelay)
			LeftCollapseAmount = FMath::Max(0.0f, LeftCollapseAmount - C.DeltaTime * C.CellRefillRate);
	}
	if (bRightCollapseTrigger) {
		RightCollapseAmount = FMath::Min(1.0f, RightCollapseAmount + C.DeltaTime * C.CellDeflateRate);
		CollapseRecoveryTimer = 0.0f;
	}
	else if (RightCollapseAmount > 0.0f) {
		CollapseRecoveryTimer += C.DeltaTime;
		if (CollapseRecoveryTimer > C.RefillDelay)
			RightCollapseAmount = FMath::Max(0.0f, RightCollapseAmount - C.DeltaTime * C.CellRefillRate);
	}
	// Frontal collapse: both sides negative AoA
	bool bFrontalTrigger = (C.LeftAoA < -WingModel.StallAoA * 0.15f && C.RightAoA < -WingModel.StallAoA * 0.15f);
	if (bFrontalTrigger) {
		FrontalCollapseAmount = FMath::Min(1.0f, FrontalCollapseAmount + C.DeltaTime * C.CellDeflateRate);
		CollapseRecoveryTimer = 0.0f;
	}
	else if (FrontalCollapseAmount > 0.0f) {
		CollapseRecoveryTimer += C.DeltaTime;
		if (CollapseRecoveryTimer > C.RefillDelay * 0.8f)
			FrontalCollapseAmount = FMath::Max(0.0f, FrontalCollapseAmount - C.DeltaTime * C.CellRefillRate);
	}
	// Apply collapse effects: collapsed section produces bluff-body drag, no lift
	if (LeftCollapseAmount > 0.0f) {
		C.LeftCL *= (1.0f - LeftCollapseAmount);
		C.LeftCD += LeftCollapseAmount * 1.0f;
	}
	if (RightCollapseAmount > 0.0f) {
		C.RightCL *= (1.0f - RightCollapseAmount);
		C.RightCD += RightCollapseAmount * 1.0f;
	}
	if (FrontalCollapseAmount > 0.0f) {
		C.LeftCL *= (1.0f - FrontalCollapseAmount);
		C.RightCL *= (1.0f - FrontalCollapseAmount);
		C.LeftCD += FrontalCollapseAmount * 1.0f;
		C.RightCD += FrontalCollapseAmount * 1.0f;
	}
	// === PARACHUTAL STALL — emerges naturally from polar ===
	auto BrakeToAoA = [](float Brake) -> float
	{
		return TrailingEdgeDeflectionDeg(Brake) * 0.8f;
	};
	C.TotalEffectiveBrake = (EffectiveLeftBrake + EffectiveRightBrake) * 0.5f;
	float BrakeAoA = BrakeToAoA(C.TotalEffectiveBrake);
	float TotalAoA = WingModel.TrimAoA + BrakeAoA + GetTrimmerAoAOffset();
	bool bStallTrigger = (TotalAoA > C.StallAoA) && (C.Vms < TrimSpeedMs * 0.7f);
	if (bStallTrigger)
	{
		ParachutalStallTimer += C.DeltaTime;
		float StallRecoveryTime = 1.0f / C.CellRefillRate;
		if (ParachutalStallTimer > StallRecoveryTime)
			bInParachutalStall = true;
	}
	else
	{
		ParachutalStallTimer = FMath::Max(0.0f, ParachutalStallTimer - C.DeltaTime);
		if (ParachutalStallTimer <= 0.0f)
			bInParachutalStall = false;
	}
	// Average CL for glide ratio reporting
	C.CL = (C.LeftCL + C.RightCL) * 0.5f;
	C.CDWing = (C.LeftCD + C.RightCD) * 0.5f;
}

// ============================================================
// UpdateLineTensionAndStretch — drag constants, per-side forces,
// cell pressure coupling, per-group line tension, line stretch
// ============================================================
void AWingActor::UpdateLineTensionAndStretch(FFlightContext& C)
{
	// Pilot + paramotor drag (pilot sitting in harness with motor on back)
	C.PilotDragArea = FMath::Lerp(0.5f, 0.3f, SeatTransition);
	C.CDPilot = 0.5f;
	C.LineDragArea = 0.06f;
	C.CDLine = 1.0f;
	// === FORCES — per side (Newtons) ===
	C.Vtot = C.ApparentWind.Size() / CM_PER_M;
	C.LeftLiftN = 0.5f * C.Rho * C.Vtot * C.Vtot * C.HalfArea * C.LeftCL;
	C.RightLiftN = 0.5f * C.Rho * C.Vtot * C.Vtot * C.HalfArea * C.RightCL;
	C.LiftN = C.LeftLiftN + C.RightLiftN;
	// === CELL PRESSURE + LINE TENSION COUPLING ===
	C.LineMassKg = 1.5f;
	float LocalTotalMassKg = PilotWeight + WingModel.WingWeight + C.LineMassKg;
	LineTensionFraction = FMath::Clamp(C.LiftN / FMath::Max(1.0f, LocalTotalMassKg * GRAVITY_MS2), 0.0f, 1.0f);
	float FillTensionCoupling = CellFillFactor * CellFillFactor * LineTensionFraction;
	C.LiftN *= FillTensionCoupling;
	C.LeftLiftN *= FillTensionCoupling;
	C.RightLiftN *= FillTensionCoupling;
	// === REAL LINE TENSION PER GROUP ===
	float TotalLoadN = C.LiftN;
	float LoadFractionA = 0.40f;
	float LoadFractionB = 0.30f;
	float LoadFractionC = 0.20f;
	float LoadFractionD = 0.10f;
	float TotalBrake = (EffectiveLeftBrake + EffectiveRightBrake) * 0.5f;
	float BrakeShiftFraction = TotalBrake * 0.25f;
	LoadFractionA -= BrakeShiftFraction * 0.50f;
	LoadFractionB -= BrakeShiftFraction * 0.25f;
	LoadFractionC += BrakeShiftFraction * 0.25f;
	LoadFractionD += BrakeShiftFraction * 0.50f;
	LoadFractionA = FMath::Max(0.05f, LoadFractionA);
	LoadFractionB = FMath::Max(0.05f, LoadFractionB);
	LoadFractionC = FMath::Max(0.01f, LoadFractionC);
	LoadFractionD = FMath::Max(0.01f, LoadFractionD);
	LineTensionA = TotalLoadN * LoadFractionA;
	LineTensionB = TotalLoadN * LoadFractionB;
	LineTensionC = TotalLoadN * LoadFractionC;
	LineTensionD = TotalLoadN * LoadFractionD;
	// === LINE STRETCH (real material physics) ===
	float LineCrossSectionArea = 1.77e-6f;
	float LineYoungsModulus = 5.0e9f;
	float LineLengthM = LINE_LENGTH_CM / CM_PER_M;
	float LineStiffnessNPerM = LineCrossSectionArea * LineYoungsModulus / LineLengthM;
	LineStretchA = (LineTensionA / LineStiffnessNPerM) * CM_PER_M;
	LineStretchD = (LineTensionD / LineStiffnessNPerM) * CM_PER_M;
	float MeanChordCm = (C.WingspanM > 0.0f) ? (WingModel.WingArea / C.WingspanM) * CM_PER_M : 200.0f;
	float ADistanceCm = MeanChordCm * 0.6f;
	float StretchAoAEffect = FMath::Atan2((LineStretchA - LineStretchD), ADistanceCm) * RAD_TO_DEG;
}

// ============================================================
// UpdateFlightForces — per-side drag, thrust, glide ratio,
// total mass, wing loading, trim speed, roll/yaw moments,
// accelerations
// ============================================================
void AWingActor::UpdateFlightForces(FFlightContext& C)
{
	// Per-side drag (for asymmetric yaw)
	C.VmsTotal = C.ApparentWind.Size() / CM_PER_M;
	C.ApparentDir = C.ApparentWind.GetSafeNormal();
	C.LeftDragN = 0.5f * C.Rho * C.VmsTotal * C.VmsTotal * C.HalfArea * C.LeftCD;
	C.RightDragN = 0.5f * C.Rho * C.VmsTotal * C.VmsTotal * C.HalfArea * C.RightCD;
	C.PilotDragN = 0.5f * C.Rho * C.VmsTotal * C.VmsTotal * C.PilotDragArea * C.CDPilot;
	C.LineDragN = 0.5f * C.Rho * C.VmsTotal * C.VmsTotal * C.LineDragArea * C.CDLine;
	C.DragN = C.LeftDragN + C.RightDragN + C.PilotDragN + C.LineDragN;
	// Thrust
	C.ThrustN = 0.0f;
	if (ConnectedParamotor)
	{
		C.ThrustN = ConnectedParamotor->GetThrustForceN(C.Vtot);
	}
	else
	{
		float EffectiveThrottle = FMath::Max(0.0f, ThrottleAmount);
		float StaticThrustN = EffectiveThrottle * MAX_THRUST_N;
		float VmaxMs = TrimSpeedMs * 1.8f;
		float ThrustEfficiency = FMath::Max(0.0f, 1.0f - C.Vms / VmaxMs);
		C.ThrustN = StaticThrustN * ThrustEfficiency;
	}
	// Glide ratio (includes wing + pilot + line drag)
	float CDTotal = C.CDWing + (C.PilotDragArea * C.CDPilot / WingModel.WingArea) + (C.LineDragArea * C.CDLine / WingModel.WingArea);
	GlideRatio = (CDTotal > 0.001f) ? C.CL / CDTotal : 99.0f;
	// === ACCELERATIONS (cm/s²) ===
	C.TotalMassKg = PilotWeight + WingModel.WingWeight + C.LineMassKg;
	// Trim speed from physics
	float TrimAoAForCL = WingModel.TrimAoA + GetTrimmerAoAOffset();
	float PolarCLTrim, PolarCDTrim;
	LookupAirfoilPolarWithCamber(TrimAoAForCL, 0.0f, PolarCLTrim, PolarCDTrim);
	float BaseCL0 = 0.41f, BaseCLMax = 1.47f;
	float CLScaleTrim = FMath::Abs(BaseCLMax - BaseCL0) > 1e-3f
		? (WingModel.CLMax - WingModel.CL0) / (BaseCLMax - BaseCL0) : 1.0f;
	float CLTrim = (PolarCLTrim - BaseCL0) * CLScaleTrim + WingModel.CL0;
	CLTrim = FMath::Max(0.01f, CLTrim);
	float TotalWeightN = C.TotalMassKg * GRAVITY_MS2;
	TrimSpeedMs = FMath::Sqrt(2.0f * TotalWeightN / (C.Rho * WingModel.WingArea * CLTrim));
	TrimSpeedMs *= GetTrimmerSpeedFactor();
	// === ASYMMETRIC LIFT → ROLL MOMENT ===
	float LiftDiff = C.RightLiftN - C.LeftLiftN;
	float RollArm = C.WingspanM * 0.5f;
	float RollMoment = LiftDiff * RollArm;
	// === ASYMMETRIC DRAG → YAW MOMENT ===
	float DragDiff = C.RightDragN - C.LeftDragN;
	// Accelerations
	C.LiftAccel = (C.LiftN / C.TotalMassKg) * CM_PER_M;
	C.DragAccel = (C.DragN / C.TotalMassKg) * CM_PER_M;
	C.ThrustAccel = (C.ThrustN / C.TotalMassKg) * CM_PER_M;
	C.GravityAccel = GRAVITY_CM_S2;
}

// ============================================================
// UpdatePendulumAndPitch — trim speed stability, pendulum physics,
// pilot pitch/seat transition
// ============================================================
void AWingActor::UpdatePendulumAndPitch(FFlightContext& C)
{
	// === TRIM SPEED STABILITY (speed polar) ===
	C.CurrentSpeedMs = C.Vel.Size() / CM_PER_M;
	C.SpeedError = C.CurrentSpeedMs - TrimSpeedMs;
	// === PENDULUM PHYSICS — REALISTIC PARAMOTOR SWING ===
	C.LineLength = LINE_LENGTH_CM;
	C.PendulumOmega = FMath::Sqrt(C.GravityAccel / C.LineLength);
	C.PendulumOmegaSq = C.PendulumOmega * C.PendulumOmega;
	C.PendulumDriveAccel = C.ThrustAccel / C.LineLength;
	float WingOnlyDragN = C.LeftDragN + C.RightDragN;
	C.PendulumBrakeAccel = (WingOnlyDragN / C.TotalMassKg) * CM_PER_M / C.LineLength;
	// Damping ratio from aerodynamic drag + lift-derived damping
	float PitchDampArea = C.PilotDragArea * C.CDPilot + WingModel.WingArea * C.CDWing
		+ C.LineDragArea * C.CDLine
		+ WingModel.WingArea * (C.WingspanM / (2.0f * C.LineLength / CM_PER_M));
	float PitchDampCoeff = 0.5f * C.Rho * FMath::Max(0.1f, C.Vms) * PitchDampArea;
	C.DampingRatio = PitchDampCoeff / (2.0f * C.TotalMassKg * C.PendulumOmega);
	C.DampingRatio = FMath::Clamp(C.DampingRatio, 0.01f, 10.0f);
	float PendulumDamping = 2.0f * C.DampingRatio * C.PendulumOmega * PendulumAngularVel;
	float PendulumAngularAccel = C.PendulumDriveAccel + C.PendulumBrakeAccel
		- C.PendulumOmegaSq * PendulumAngle - PendulumDamping;
	PendulumAngularVel += PendulumAngularAccel * C.DeltaTime;
	PendulumAngle += PendulumAngularVel * C.DeltaTime;
	// === PILOT PITCH TRANSITION (launch → upright → seated) ===
	if (bAutoSeatPending)
	{
		LaunchTimer += C.DeltaTime;
		if (LaunchTimer > 3.0f)
		{
			bSeated = true;
			bAutoSeatPending = false;
		}
	}
	float LaunchAngleFade = bAutoSeatPending ? FMath::Max(0.0f, 1.0f - LaunchTimer / 5.0f) : 0.0f;
	LaunchAngleOffset = 5.0f * LaunchAngleFade;
	SeatTransition = FMath::FInterpTo(SeatTransition, bSeated ? 1.0f : 0.0f, C.DeltaTime, 2.0f);
	PilotPitchTarget = (bSeated ? -80.0f : 0.0f) + LaunchAngleOffset;
	PilotPitch = FMath::FInterpTo(PilotPitch, PilotPitchTarget, C.DeltaTime, 3.0f);
	DragReductionFromSeat = SeatTransition * 0.40f;
}

// ============================================================
// UpdateYawRollDynamics — weight shift, wing inertia, prop torque,
// yaw, roll, spiral dive, Dutch roll
// ============================================================
void AWingActor::UpdateYawRollDynamics(FFlightContext& C)
{
	// === WEIGHT SHIFT STEERING ===
	C.WeightShiftCGOffset = WeightShift * 0.17f;
	C.WeightShiftRollMoment = PilotWeight * GRAVITY_MS2 * C.WeightShiftCGOffset;
	// === WING INERTIA ===
	float FlightApparentMassKg = C.Rho * WingModel.WingArea * (C.WingspanM * 0.5f) * CellFillFactor * 0.5f;
	C.FlightEffectiveMassKg = WingModel.WingWeight + FlightApparentMassKg;
	C.WingInertiaRoll = C.FlightEffectiveMassKg * (C.WingspanM * 0.5f) * (C.WingspanM * 0.5f);
	C.WingInertiaYaw = C.FlightEffectiveMassKg * C.WingspanM * C.WingspanM / 12.0f;
	// === PROPELLER TORQUE ===
	float EnginePowerW = (ConnectedParamotor) ? ConnectedParamotor->GetEnginePowerW() : 0.0f;
	float PropRPM = (ConnectedParamotor) ? ConnectedParamotor->GetPropRPM() : 0.0f;
	float PropOmega = PropRPM * 2.0f * PI / 60.0f;
	C.PropTorqueNM = (PropOmega > 1.0f) ? EnginePowerW / PropOmega : 0.0f;
	// === YAW DYNAMICS — TORQUE/INERTIA INTEGRATION ===
	float YawMomentArm = C.WingspanM * 0.5f;
	float DragDiff = C.RightDragN - C.LeftDragN;
	float DragYawMoment = DragDiff * YawMomentArm;
	float CollapseYawMoment = (RightCollapseAmount - LeftCollapseAmount) * C.Rho * C.Vms * C.Vms * WingModel.WingArea * 1.0f * YawMomentArm;
	float AdverseYawMoment = -WingRollRate * PI_DIV_180 * YawMomentArm * C.Rho * C.Vms * C.Vms * WingModel.WingArea * YawMomentArm * C.CL * 0.5f / FMath::Max(1.0f, C.Vms);
	float TotalYawMoment = DragYawMoment + CollapseYawMoment + AdverseYawMoment;
	float YawAccelRad = TotalYawMoment / C.WingInertiaYaw;
	WingYawRate += YawAccelRad * RAD_TO_DEG * C.DeltaTime;
	float YawDampCoeff = (C.WingInertiaYaw > 0.1f)
		? 0.5f * C.Rho * C.Vms * WingModel.WingArea * C.WingspanM * C.WingspanM / C.WingInertiaYaw
		: 0.0f;
	float YawDampMoment = -WingYawRate * PI_DIV_180 * YawDampCoeff * C.WingInertiaYaw;
	WingYawRate += (YawDampMoment / C.WingInertiaYaw) * RAD_TO_DEG * C.DeltaTime;
	C.YawRate = FMath::Clamp(WingYawRate, -360.0f, 360.0f);
	// === ROLL DYNAMICS — TORQUE/INERTIA INTEGRATION ===
	float VmsFlight = C.HorizSpeed / CM_PER_M;
	C.TargetBank = (VmsFlight > 0.5f) ? FMath::Atan(VmsFlight * CM_PER_M * C.YawRate * PI_DIV_180 / C.GravityAccel) * RAD_TO_DEG : 0.0f;
	float LiftDiff = C.RightLiftN - C.LeftLiftN;
	float RollArm = C.WingspanM * 0.5f;
	float RollMoment = LiftDiff * RollArm;
	float CollapseRollMoment = (RightCollapseAmount - LeftCollapseAmount) * C.LiftN * C.WingspanM * 0.5f;
	float PendulumOmegaRoll = FMath::Sqrt(GRAVITY_MS2 / (LINE_LENGTH_CM / CM_PER_M));
	float CoordSpringMoment = C.WingInertiaRoll * PendulumOmegaRoll * PendulumOmegaRoll
		* (C.TargetBank - CurrentRoll) * PI_DIV_180;
	float TurbRollMoment = 0.5f * C.Rho * C.Vms * C.Vms * WingModel.WingArea * C.WingspanM
		* 0.02f * (C.Vms > 1.0f ? TurbLatState / C.Vms : 0.0f);
	float TotalRollMoment = C.WeightShiftRollMoment + RollMoment + CollapseRollMoment
		+ C.PropTorqueNM + CoordSpringMoment + TurbRollMoment;
	float RollAccelRad = TotalRollMoment / C.WingInertiaRoll;
	WingRollRate += RollAccelRad * RAD_TO_DEG * C.DeltaTime;
	float C_lp = -1.0f;
	float RollDampMoment = -WingRollRate * PI_DIV_180 * C_lp * 0.5f * C.Rho * C.Vms
		* WingModel.WingArea * (C.WingspanM * 0.5f) * (C.WingspanM * 0.5f);
	WingRollRate += (RollDampMoment / C.WingInertiaRoll) * RAD_TO_DEG * C.DeltaTime;
	CurrentRoll += WingRollRate * C.DeltaTime;
	CurrentRoll = FMath::Clamp(CurrentRoll, -180.0f, 180.0f);
	C.TargetRoll = C.TargetBank;
	// === SPIRAL DIVE INSTABILITY ===
	SpiralDiveFactor = FMath::Max(0.0f, (FMath::Abs(CurrentRoll) - 40.0f) / 20.0f);
	// === DUTCH ROLL — removed artificial sine wave ===
	DutchRollYaw = 0.0f;
	DutchRollRoll = 0.0f;
}

// ============================================================
// UpdatePilotOrientation — wing surge/pitch oscillation,
// roll pendulum, apply pilot rotation, rotate velocity vector,
// stall log
// ============================================================
void AWingActor::UpdatePilotOrientation(FFlightContext& C)
{
	// === WING SURGE / PITCH OSCILLATION ===
	C.PitchTarget = 0.0f;
	C.PitchTarget += TrailingEdgeDeflectionDeg(C.TotalEffectiveBrake) * 0.30f;
	C.PitchTarget -= FrontalCollapseAmount * FMath::Atan2(C.LiftN * 0.25f * C.WingspanM, C.TotalMassKg * GRAVITY_MS2 * (LINE_LENGTH_CM / CM_PER_M)) * RAD_TO_DEG;
	if (C.Vms > 1.0f && C.SpeedError > 0.0f)
		C.PitchTarget -= FMath::Atan(C.SpeedError / C.Vms) * RAD_TO_DEG;
	C.PitchTarget += PendulumAngle * RAD_TO_DEG;
	C.PitchTarget += GetTrimmerPitchOffset();
	C.MaxHangPointOffsetCm = C.LineLength * FMath::Tan(4.0f * PI_DIV_180);
	C.HangPointTiltDeg = FMath::Atan2(HangPointOffset * C.MaxHangPointOffsetCm, C.LineLength) * RAD_TO_DEG;
	C.PitchTarget += C.HangPointTiltDeg;
	// Wing pitch spring-mass-damper dynamics
	C.PitchOmega = FMath::Sqrt(C.GravityAccel / C.LineLength);
	float PitchDampingRatio = C.DampingRatio;
	float PitchSpring = (C.PitchTarget - WingPitchAngle) * C.PitchOmega * C.PitchOmega;
	float PitchDamp = -WingPitchRate * 2.0f * PitchDampingRatio * C.PitchOmega;
	WingPitchRate += (PitchSpring + PitchDamp) * C.DeltaTime;
	WingPitchAngle += WingPitchRate * C.DeltaTime;
	// === ROLL PENDULUM — pilot swings sideways under wing during turns ===
	float TargetPilotRoll = CurrentRoll;
	float TurbLatForce = 0.5f * C.Rho * FMath::Max(0.1f, C.Vms) * FMath::Abs(TurbLatState) * 2.0f * C.PilotDragArea * C.CDPilot;
	float TurbLatAccel = TurbLatForce / C.TotalMassKg * CM_PER_M;
	float TurbRollAccelDeg = (TurbLatAccel / C.LineLength) * RAD_TO_DEG;
	float RollPendulumOmega = FMath::Sqrt(C.GravityAccel / C.LineLength);
	float RollDampArea = C.PilotDragArea * C.CDPilot + WingModel.WingArea * C.CDWing
		+ C.LineDragArea * C.CDLine
		+ WingModel.WingArea * (C.WingspanM / (2.0f * C.LineLength / CM_PER_M));
	float RollDampCoeff = 0.5f * C.Rho * FMath::Max(0.1f, C.Vms) * RollDampArea;
	float RollDampingRatio = RollDampCoeff / (2.0f * C.TotalMassKg * RollPendulumOmega);
	RollDampingRatio = FMath::Clamp(RollDampingRatio, 0.01f, 10.0f);
	float RollErrorRad = (TargetPilotRoll - PilotRollAngle) * PI_DIV_180;
	float RollSpringRad = RollErrorRad * RollPendulumOmega * RollPendulumOmega;
	float RollDampRad = -PilotRollRate * PI_DIV_180 * 2.0f * RollDampingRatio * RollPendulumOmega;
	float RollAccelDeg = (RollSpringRad + RollDampRad) * RAD_TO_DEG + TurbRollAccelDeg;
	PilotRollRate += RollAccelDeg * C.DeltaTime;
	PilotRollAngle += PilotRollRate * C.DeltaTime;
	// === APPLY PILOT ROTATION ===
	FRotator CurrentRot = C.Pawn->GetActorRotation();
	FRotator NewRot = CurrentRot;
	NewRot.Yaw += C.YawRate * C.DeltaTime + DutchRollYaw;
	NewRot.Roll = PilotRollAngle + DutchRollRoll;
	NewRot.Pitch = PilotPitch + PendulumAngle * RAD_TO_DEG + WingPitchAngle;
	C.Pawn->SetActorRotation(NewRot);
	// Verify the rotation actually stuck
	FRotator VerifyRot = C.Pawn->GetActorRotation();
	{
		RollDebugTimer += C.DeltaTime;
		if (RollDebugTimer > 0.2f)
		{
			RollDebugTimer = 0.0f;
			UE_LOG(LogTemp, Warning, TEXT("ROT DEBUG: Set Roll=%.2f Pitch=%.2f Yaw=%.2f | Verified Roll=%.2f Pitch=%.2f Yaw=%.2f | PilotRoll=%.2f rate=%.2f | WingRoll=%.2f WingRollRate=%.2f | TargetRoll=%.2f | YawRate=%.2f WeightShift=%.2f"),
				NewRot.Roll, NewRot.Pitch, NewRot.Yaw,
				VerifyRot.Roll, VerifyRot.Pitch, VerifyRot.Yaw,
				PilotRollAngle, PilotRollRate,
				CurrentRoll, WingRollRate,
				C.TargetRoll,
				C.YawRate, WeightShift);
		}
	}
	// === ROTATE VELOCITY VECTOR TO FOLLOW YAW ===
	float YawDeltaRad = C.YawRate * C.DeltaTime * PI_DIV_180;
	float CosY = FMath::Cos(YawDeltaRad);
	float SinY = FMath::Sin(YawDeltaRad);
	float NewVx = C.Vel.X * CosY - C.Vel.Y * SinY;
	float NewVy = C.Vel.X * SinY + C.Vel.Y * CosY;
	C.Vel.X = NewVx;
	C.Vel.Y = NewVy;
	// Log stall/collapse events
	StallLogTimer += C.DeltaTime;
	if (StallLogTimer > 0.5f && (C.LeftAoA > C.StallAoA || C.RightAoA > C.StallAoA ||
		LeftCollapseAmount > 0.1f || RightCollapseAmount > 0.1f || FrontalCollapseAmount > 0.1f ||
		bInParachutalStall))
	{
		StallLogTimer = 0.0f;
		UE_LOG(LogTemp, Warning, TEXT("WingActor: L_AoA=%.1f° R_AoA=%.1f° L_CL=%.2f R_CL=%.2f roll=%.1f yaw=%.1f pitch=%.1f | Col: L=%.0f%% R=%.0f%% F=%.0f%% Stall=%d"),
			C.LeftAoA, C.RightAoA, C.LeftCL, C.RightCL, CurrentRoll, C.YawRate, WingPitchAngle,
			LeftCollapseAmount * PERCENT_FACTOR, RightCollapseAmount * PERCENT_FACTOR, FrontalCollapseAmount * PERCENT_FACTOR,
			bInParachutalStall ? 1 : 0);
	}
}

// ============================================================
// ApplyFlightForces — banked turn, thrust, drag, lift, gravity,
// centripetal, NewVel integration, safety caps, landing,
// move pilot, wing position, flight log
// ============================================================
void AWingActor::ApplyFlightForces(FFlightContext& C)
{
	FVector PawnFwd = C.Pawn->GetActorForwardVector();
	FVector VelDir = C.Vel.GetSafeNormal();
	if (VelDir.Size() < 0.01f) VelDir = PawnFwd;
	FVector DragDir = C.ApparentDir;
	// === BANKED TURN CENTRIPETAL FORCE ===
	float BankRad = CurrentRoll * PI_DIV_180;
	float VerticalLift = C.LiftAccel * FMath::Cos(BankRad);
	float CentripetalAccel = C.LiftAccel * FMath::Sin(BankRad);
	FVector RightVec = C.Pawn->GetActorRightVector();
	RightVec.Z = 0.0f;
	RightVec = RightVec.GetSafeNormal();
	FVector CentripetalDir = RightVec * (CurrentRoll > 0.0f ? 1.0f : -1.0f);
	FVector Accel = FVector::ZeroVector;
	// Thrust: applied along the pilot's forward vector, tilted by hang point angle
	float HangPointTiltRad = FMath::Atan2(HangPointOffset * C.MaxHangPointOffsetCm, C.LineLength);
	float ThrustHorizFraction = FMath::Cos(HangPointTiltRad);
	float ThrustVertFraction = FMath::Sin(HangPointTiltRad);
	Accel += PawnFwd * C.ThrustAccel * ThrustHorizFraction;
	Accel.Z += C.ThrustAccel * ThrustVertFraction;
	Accel += DragDir * C.DragAccel;
	// === LIFT PERPENDICULAR TO FLIGHT PATH ===
	float TotalPitchRad = (WingPitchAngle + PendulumAngle * RAD_TO_DEG) * PI_DIV_180;
	float LiftVertical = VerticalLift * FMath::Cos(TotalPitchRad);
	float LiftForward = -C.LiftAccel * FMath::Sin(TotalPitchRad) * FMath::Cos(BankRad);
	Accel.Z += LiftVertical;
	Accel += PawnFwd * LiftForward;
	Accel.Z -= C.GravityAccel;
	Accel += CentripetalDir * CentripetalAccel;
	Accel += FlightTurbulence;
	C.NewVel = C.Vel + Accel * C.DeltaTime;
	// Safety
	if (!FMath::IsFinite(C.NewVel.X) || !FMath::IsFinite(C.NewVel.Y) || !FMath::IsFinite(C.NewVel.Z))
	{
		UE_LOG(LogTemp, Error, TEXT("WingActor: NaN NewVel — aborting"));
		return;
	}
	ApplyVelocitySafetyCaps(C.NewVel);
	// === GROUND COLLISION / LANDING ===
	HandleLanding(C.Char, C.DeltaTime, C.NewVel, C.TotalMassKg);
	// === MOVE PILOT — direct actor movement, bypassing CMC ===
	{
		FVector CurrentLoc = C.Pawn->GetActorLocation();
		FVector NewLoc = CurrentLoc + C.NewVel * C.DeltaTime;
		float MoveGZ = GetCachedGroundZ(NewLoc);
		if (NewLoc.Z < MoveGZ)
		{
			NewLoc.Z = MoveGZ;
			C.NewVel.Z = 0;
		}
		C.Pawn->SetActorLocation(NewLoc, false);
		C.Char->GetCharacterMovement()->Velocity = C.NewVel;
	}
	// === WING POSITION (rigid line constraint) ===
	if (bRisersConnected)
	{
		UpdateWingPositionFromLines(PawnFwd, C.LiftN, C.DragN, C.LineLength, C.WeightShiftCGOffset);
	}
	// Log once per second
	FlightLogTimer += C.DeltaTime;
	if (FlightLogTimer > 1.0f)
	{
		FlightLogTimer = 0.0f;
		UE_LOG(LogTemp, Warning, TEXT("WingActor: FLIGHT speed=%.1f mph climb=%.0f fpm alt=%.0fm pend=%.1f° pitch=%.0f° wpitch=%.1f° thrust=%.0f yaw=%.1f bank=%.1f proll=%.1f L=%.2f R=%.2f brakeL=%.1f brakeR=%.1f wshift=%.2f turb=%.1f trim=%.1f spiral=%.0f%% wl=%.1f | LineTension A=%.0f B=%.0f C=%.0f D=%.0f N stretchA=%.2fcm"),
			Airspeed / CM_PER_M * MPH_PER_MS, ClimbRate / CM_PER_M * 196.85f, C.AltitudeM,
			PendulumAngle * RAD_TO_DEG, PilotPitch, WingPitchAngle, C.ThrustAccel, C.YawRate, CurrentRoll,
			PilotRollAngle,
			C.LeftCL, C.RightCL, EffectiveLeftBrake, EffectiveRightBrake, WeightShift, C.TurbStrength,
			TrimSpeedMs * MPH_PER_MS, SpiralDiveFactor * PERCENT_FACTOR, GetWingLoading(),
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

float AWingActor::GetTrimmerAoAOffset() const
{
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

float AWingActor::GetTrimmerSpeedFactor() const
{
	// Speed comes from the polar: trimmer changes AoA → CL changes →
	// V = sqrt(2W / (ρ × S × CL)). No separate multiplier needed.
	return 1.0f;
}

float AWingActor::GetTrimmerDragFactor() const
{
	// Drag comes from the polar at the adjusted AoA — no separate multiplier
	return 1.0f;
}

float AWingActor::GetTrimmerCLFactor() const
{
	// CL comes from the polar at the adjusted AoA — no separate multiplier
	return 1.0f;
}

float AWingActor::GetTrimmerPitchOffset() const
{
	// Pitch from riser geometry: wing angle relative to pilot = AoA offset
	// The wing hangs at a different angle because the A riser is longer/shorter
	return GetTrimmerAoAOffset();
}

float AWingActor::GetTrimmerCollapseRisk() const
{
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

FString AWingActor::GetTrimmerLabel() const
{
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
	float LandingSpeedMs = -NewVel.Z / CM_PER_M;

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
	float FrictionDecel = FrictionCoeff * NormalForce * DeltaTime * CM_PER_M;
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
	SpiralDiveFactor = 0.0f;
	DutchRollYaw = 0.0f;
	DutchRollRoll = 0.0f;
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

	FVector ToWing = DesiredWingPos - CarabMid;
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
