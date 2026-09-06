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
// WingGroundHandling.cpp — WingActor implementation split
// ============================================================

// ============================================================
// INFLATION UPDATE
// ============================================================

void AWingActor::UpdateInflation(float DeltaTime){
	if (WingState != EWingState::Connected &&		WingState != EWingState::Inflating &&		WingState != EWingState::Inflated) return;

	// === CONNECTED STATE: waiting for A-line pull to start inflation ===
	if (WingState == EWingState::Connected)	{
		if (bLiftingALines)		{
			WingState = EWingState::Inflating;
			InflationProgress = 0.0f;
			InflationMomentum = 0.0f;
			InflationHeight = 0.0f;
			InflationVelocity = 0.0f;
			CellFillFactor = 0.0f;
			KiteOffset = FVector::ZeroVector;
			KiteVelocity = FVector::ZeroVector;
			KiteSinkHeight = 0.0f;
			KiteVerticalVel = 0.0f;
			KiteYawDrift = 0.0f;
			KiteYawVel = 0.0f;
			KiteLeftStall = 0.0f;
			KiteRightStall = 0.0f;
			WingVel = FVector::ZeroVector;
			KitePitchOvershoot = 0.0f;
			ALineFadeFactor = 0.0f;
			UE_LOG(LogTemp, Warning, TEXT("WingActor: Started inflating"));
		}
		return;
	}

	// NOTE: Inflating and Inflated state physics (forces, cell pressure, line constraint,
	// state transitions, wing position/rotation) are now handled by the UNIFIED
	// INFLATION + KITING PHYSICS section in SimulateCloth() (WingClothSim.cpp).
	// This function only handles the ground-run/takeoff physics for the Inflated state.
	if (WingState == EWingState::Inflated)	{
// === DYNAMIC WIND SYSTEM ===
// Real wind has gusts, turbulence, and direction shifts.
// No two takeoffs are the same because the wind is always changing.
// Wind direction is FIXED (compass direction) — pilot must face into it.
// Gusts vary the wind speed ±WindGusts m/s with multi-frequency oscillation.
// Turbulence adds random perturbations. Direction shifts ±15°.

float GustAmplitude = 0.0f;
if (CachedGameInstance)
		GustAmplitude = CachedGameInstance->WindGusts;
// Use unified wind update helper
	UpdateDynamicWind(DeltaTime, GustAmplitude);
// Wind direction: rotate the base WindDirection by the direction shift

FVector ShiftedWindDir = WindDirection.RotateAngleAxis(WindDirShift * 180.0f / PI, FVector(0, 0, 1));
// Ground-level wind reduction: wind is slower near the ground due to surface friction
// Wind gradient: V(h) = V_ref * (h/h_ref)^0.15, but below 2m the gradient is steeper
// At ground level (0-2m), wind is ~50-70% of the 10m reference wind
FVector WingLoc = GetActorLocation();
float WingAltitudeM = FMath::Max(0.0f, (WingLoc.Z - GetGroundZ(WingLoc)) / CM_PER_M);
float GroundWindFactor = FMath::Clamp(FMath::Pow(FMath::Max(0.5f, WingAltitudeM / 10.0f), 0.15f), 0.5f, 1.0f);
float GroundWindSpeed = CurrentWindSpeed * GroundWindFactor;
FVector Wind = ShiftedWindDir * GroundWindSpeed * CM_PER_M;
// cm/s
// === GROUND RUN PHYSICS — REALISTIC TAKEOFF ===
// In real paramotoring, takeoff is a GRADUAL process:
// 1. Pilot runs forward, building airspeed
// 2. Wing generates lift proportional to V²
// 3. As lift approaches weight, pilot gets lighter on feet
// 4. Gusts cause lift to fluctuate — pilot SKIPS/HOPS (feet touch, push off, briefly airborne)
// 5. Eventually lift consistently exceeds weight → pilot leaves ground for good
// 6. The skip/hop phase makes every takeoff unique based on wind conditions

if (bRisersConnected)	{
		APawn* Pawn = nullptr;
if (ConnectedParamotor)			Pawn = Cast<APawn>(ConnectedParamotor->GetAttachParentActor());
if (Pawn)		{
			ACharacter* Char = Cast<ACharacter>(Pawn);
if (Char && !Char->GetCharacterMovement()->IsFlying())			{
				// === AIRSPEED CALCULATION ===
FVector CharVel = Char->GetVelocity();
if (!FMath::IsFinite(CharVel.X)) CharVel = FVector::ZeroVector;
// No velocity cap - leg power model bounds speed
FVector HorizVel = FVector(CharVel.X, CharVel.Y, 0.0f);
FVector HorizWind = FVector(Wind.X, Wind.Y, 0.0f);
FVector GroundApparentWind = HorizWind - HorizVel;
// Headwind component (positive = wind in pilot's face = good for lift)
FVector PilotFwd = Char->GetActorForwardVector();
PilotFwd.Z = 0.0f;
PilotFwd.Normalize();

// === WIND DRAG ON WING PULLS PILOT (realistic) ===
// The inflated wing has significant drag: F = 0.5 * ρ * V² * S * CD
// This force is transmitted through the lines to the pilot.
// In 16mph wind with 25m² wing: F ≈ 77N = ~7.8 kgf pulling the pilot
// The pilot must lean back and brace against this force.
// The pull is in the wind direction (downwind), pushing the pilot backward.
// Drag from the cell pressure model (real computed drag, not a fixed CD)
float WingDragN = GetTotalCellDragN();  // Cell drag already scales with fill
// Convert to cm/s² acceleration on the total suspended mass
float TotalSuspendedMassKg = PilotWeight + WingModel.WingWeight + 1.5f;
float WindPullAccelCm = (WingDragN / TotalSuspendedMassKg) * CM_PER_M;
// Wind pulls pilot in the wind direction (downwind)
FVector WindPullVel = ShiftedWindDir * WindPullAccelCm * DeltaTime;
// Apply to pilot velocity (but only horizontal — wind doesn't lift pilot through drag)
FVector CurPilotVel = Char->GetCharacterMovement()->Velocity;
CurPilotVel.X += WindPullVel.X;
CurPilotVel.Y += WindPullVel.Y;
// Wind pull is naturally limited by drag equilibrium — no artificial cap needed
// The wing can only pull the pilot at the wind speed (drag = 0 when at wind speed)
Char->GetCharacterMovement()->Velocity = CurPilotVel;
float HeadwindMs = -FVector::DotProduct(GroundApparentWind, PilotFwd)  / CM_PER_M;
// Airspeed = headwind component only (tailwind = no lift)
float AirspeedMs = FMath::Max(0.0f, HeadwindMs);
// No airspeed cap — lift/weight balance naturally limits takeoff speed
float GroundSpeedMs = HorizVel.Size()  / CM_PER_M;
float AirspeedMph = AirspeedMs * MPH_PER_MS;
float GroundSpeedMph = GroundSpeedMs * MPH_PER_MS;
// === WING LIFT (from cell pressure model — real physics) ===
// Lift comes from the pressurized airfoil, not a formula.
// The cell pressure model already accounts for fill state, brake input, and stall.
// L = sum of all cell lift forces (already computed by UpdateCellPressure)
float Rho = AIR_DENSITY_SEA_LEVEL;
float LiftN = GetTotalCellLiftN();
// Ground effect: decreases induced drag near ground → more effective lift
// Formula: factor = 1 + 0.20 * exp(-h / (b/2))
// At ground: +20%, at 10m (1 wingspan): +3%, at 20m: negligible
FVector CharLoc = Char->GetActorLocation();
float GZ = GetGroundZ(CharLoc);
float HeightAboveGround = FMath::Max(0.0f, (CharLoc.Z - GZ)  / CM_PER_M);  // meters
float WingspanM = FMath::Sqrt(WingModel.WingArea * WingModel.AR);  // b = sqrt(S * AR)
// Ground effect: reduces induced drag near ground (less downwash)
// Factor goes from 0 (full reduction at ground) to 1 (no reduction at altitude)
float GroundEffectDragFactor = FMath::Exp(-HeightAboveGround / (WingspanM * 0.5f));
// Applied to wing drag below (reduces induced drag portion near ground)
// Weight — total suspended weight. PilotWeight already includes paramotor + fuel
// (set by ParamotorPlayerController). Only add wing + lines here.
float TotalMassKg = PilotWeight + WingModel.WingWeight + 1.5f;  // +1.5kg lines
float WeightN = TotalMassKg * GRAVITY_MS2;
float WeightSupport = LiftN / WeightN;
// === SKIP/HOP PHYSICS ===
// When lift is 60-100% of weight, the pilot is very light on their feet.
// Gusts cause lift to fluctuate above and below weight.
// When a gust pushes lift > weight, the pilot briefly leaves the ground (hop).
// When the gust passes, lift drops below weight and the pilot touches down again.
// This creates the realistic "skip-hop-run" takeoff pattern.
//
// We model this by applying the net vertical force (lift - weight) as acceleration.
// When net force is positive, the pilot rises. When negative, they fall back.
// The character movement component handles ground contact.

float NetVerticalForceN = LiftN - WeightN;
// Apply ONLY the lift acceleration — CMC's GravityScale=1.0 handles gravity.
// Applying (Lift - Weight) AND letting CMC apply gravity double-counts gravity.
float UpwardLiftAccelCm = (LiftN / TotalMassKg)  * CM_PER_M;
// Track skip state for logging
bool bWasOnGround = Char->GetCharacterMovement()->IsMovingOnGround();
float PrevSkipZ = TakeoffSkipZ;
TakeoffSkipZ = CharLoc.Z - GZ;
// Height above ground
// When lift > 70% of weight, allow the pilot to leave the ground briefly
// (MOVE_Falling so the movement component doesn't kill Z velocity)

if (NetVerticalForceN > 0.0f && Char->GetCharacterMovement()->IsMovingOnGround())				{
					// Only switch to falling if there's upward velocity from a gust
FVector CurVel = Char->GetCharacterMovement()->Velocity;
if (CurVel.Z > 0.0f || WeightSupport > 1.0f)					{
						Char->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
}
}
				// Apply net vertical force
FVector CurrentVel = Char->GetCharacterMovement()->Velocity;
CurrentVel.Z += UpwardLiftAccelCm * DeltaTime;
// No upward velocity cap — the net vertical force (lift - weight) naturally limits hop height
				Char->GetCharacterMovement()->Velocity = CurrentVel;
// Keep gravity at 1.0
				Char->GetCharacterMovement()->GravityScale = 1.0f;
// Detect foot contact (for logging and feel)

if (bWasOnGround && TakeoffSkipZ < PrevSkipZ - 1.0f)				{
					TakeoffFootContacts++;
}
				// === RUN SPEED: decreases as lift increases (lighter = less traction) ===
		// Real pilots slow down as the wing lifts them — less ground contact force.
		// Physics: friction = μ × N, where N = (weight - lift) × cos(θ)
		// Max run speed is limited by leg power vs. drag + wind pull:
		// P_leg = F_leg × v → v_max = P_leg / F_resistance
		// Average pilot leg power: ~350W sprint, ~200W sustained
		float PilotLegPowerW = 350.0f;  // Sprint power (W)
		// Use cell pressure drag (consistent with wind pull above), apply ground effect
	float WingDragGroundN = WingDragN * (1.0f - GroundEffectDragFactor * 0.2f);  // Up to 20% reduction near ground
		// Rolling resistance from ground friction: μ × N (normal force = weight - lift)
		float RollingMu = 0.4f;  // Rubber soles on grass
		float RollingResistanceN = RollingMu * FMath::Max(0.0f, WeightN - LiftN);
		float TotalResistanceN = WingDragGroundN + RollingResistanceN;
		float MaxRunSpeedMs = (TotalResistanceN > 1.0f) ? PilotLegPowerW / TotalResistanceN : 6.0f;
		// Physical bounds: walking ~1.5 m/s, human sprint ~12 m/s (with paramotor ~8 m/s)
		MaxRunSpeedMs = FMath::Clamp(MaxRunSpeedMs, 1.5f, 10.0f);  // Human sprint ~10 m/s
		// Traction: normal force = weight - lift. Friction = μ × N.
		// μ = 0.4 (rubber soles on grass). When N → 0, traction → 0 (can't run).
		float NormalForceN = FMath::Max(0.0f, WeightN - LiftN);
		float TractionFactor = FMath::Clamp(NormalForceN / WeightN, 0.0f, 1.0f);
		float MaxRunSpeed = MaxRunSpeedMs  * CM_PER_M * TractionFactor;
		Char->GetCharacterMovement()->MaxWalkSpeed = MaxRunSpeed;
// === STALL SPEED (for HUD display) ===
float StallDenom = Rho * WingModel.WingArea * WingModel.CLMax;
float StallSpeedMs = StallDenom > 0.0f ? FMath::Sqrt(2.0f * TotalMassKg * GRAVITY_MS2 / StallDenom) : 8.0f;
float TakeoffSpeedMs = StallSpeedMs;
// Need full stall speed to truly fly
// Store debug values
				DebugGroundAirspeed = AirspeedMs;
DebugGroundSpeed = GroundSpeedMs;
DebugHeadwind = HeadwindMs;
DebugTakeoffThreshold = TakeoffSpeedMs;
DebugWeightSupport = FMath::Clamp(WeightSupport, 0.0f, 1.0f);
// Log every 0.2 seconds
				GroundLogTimer += DeltaTime;

if (GroundLogTimer > 0.2f)				{
					GroundLogTimer = 0.0f;
UE_LOG(LogTemp, Warning, TEXT("GRUN: air=%.1fmph grd=%.1fmph wnd=%.1fmph(±%.1f) need=%.1fmph thr=%.2f support=%.0f%% skipZ=%.0f hops=%d ge=%.2f"),						AirspeedMph, GroundSpeedMph, CurrentWindSpeed * MPH_PER_MS, GustAmplitude * MPH_PER_MS,						TakeoffSpeedMs * MPH_PER_MS, ThrottleAmount,						WeightSupport * PERCENT_FACTOR, TakeoffSkipZ, TakeoffFootContacts, GroundEffectDragFactor);
}
				// === NATURAL TAKEOFF ===
		// Takeoff is a physical transition, not a timer:
		// 1. Lift consistently exceeds weight (WeightSupport > 1.0)
		// 2. Pilot is above ground (TakeoffSkipZ > 5cm = just left ground)
		// 3. Sufficient airspeed for flight (AirspeedMs > MIN_TAKEOFF_AIRSPEED_MS)
		bool bAirborne = (TakeoffSkipZ > 5.0f);  // 5cm = just left ground
		bool bLiftExceedsWeight = (WeightSupport > 1.0f);
		bool bHasAirspeed = (AirspeedMs > MIN_TAKEOFF_AIRSPEED_MS);
		if (bAirborne && bLiftExceedsWeight && bHasAirspeed)
			SustainedAirTime += DeltaTime;
		else
			SustainedAirTime = FMath::Max(0.0f, SustainedAirTime - DeltaTime);
		// Takeoff when sustained for 1 wing-period (natural stability timescale)
		// Wing period = 1/f, f = natural frequency of pendulum = √(g/L) / (2π)
		// For 7m lines: T ≈ 5.3s, but takeoff happens in the first quarter period
		// when the wing is still overhead and stable → ~1.3s
		// Using 0.3s as the minimum stable flight time (emerges from gust statistics)
		if (SustainedAirTime > 0.5f)  // Half second of sustained flight
			{
					SustainedAirTime = 0.0f;
WingState = EWingState::Flying;
bIsAirborne = true;
// BYPASS CMC ENTIRELY — use MOVE_None so CMC does zero physics processing
// WingActor has 100% control over velocity, position, and rotation
Char->GetCharacterMovement()->SetMovementMode(MOVE_None);
Char->GetCharacterMovement()->GravityScale = 0.0f;
Char->GetCharacterMovement()->AirControl = 0.0f;
Char->GetCharacterMovement()->MaxAcceleration = 0.0f;
// Disable all rotation flags — WingActor controls rotation
Char->bUseControllerRotationYaw = false;
Char->GetCharacterMovement()->bOrientRotationToMovement = false;
Char->GetCharacterMovement()->bUseControllerDesiredRotation = false;
// Set mesh to base relative rotation (MetaHuman -90° yaw) so it faces forward
// The mesh will inherit the capsule's bank/pitch/yaw from WingActor's rotation
{
	FQuat FlightBaseQuat(FVector::UpVector, FMath::DegreesToRadians(-90.0f));
	Char->GetMesh()->SetRelativeRotation(FlightBaseQuat);
}
// Initial upward velocity from the lift
FVector FlightVel = Char->GetCharacterMovement()->Velocity;
// No forced upward velocity - use actual vertical velocity from lift
Char->GetCharacterMovement()->Velocity = FlightVel;
// Initialize pendulum
					PendulumAngle = 0.0f;
PendulumAngularVel = 0.0f;  // Start at rest
PrevThrottleAmount = ThrottleAmount;
ThrottleRate = 0.0f;
FlightTime = 0.0f;
PilotPitch = 0.0f;
PilotPitchTarget = 0.0f;  // Start upright — auto-transitions to seated after launch
// Launch-to-seat transition: pilot is upright at takeoff, settles into seat after ~3s
bSeated = false;
bAutoSeatPending = true;
LaunchTimer = 0.0f;
LaunchAngleOffset = 5.0f;  // Motor torque tilts pilot slightly forward at takeoff
// Reset collapse/stall states on takeoff
					LeftCollapseAmount = 0.0f;
RightCollapseAmount = 0.0f;
FrontalCollapseAmount = 0.0f;
bInParachutalStall = false;
ParachutalStallTimer = 0.0f;
WingPitchAngle = 0.0f;
WingPitchRate = 0.0f;
EffectiveLeftBrake = 0.0f;
EffectiveRightBrake = 0.0f;
PilotRollAngle = 0.0f;
PilotRollRate = 0.0f;

UE_LOG(LogTemp, Warning, TEXT("WingActor: TAKEOFF! air=%.1fmph grd=%.1fmph thr=%.2f support=%.0f%% wind=%.1fmph hops=%d"),						AirspeedMph, GroundSpeedMph, ThrottleAmount,						WeightSupport * PERCENT_FACTOR, CurrentWindSpeed * MPH_PER_MS, TakeoffFootContacts);
}
}
}
}
		// === COLLAPSE PHYSICS DURING KITING ===
		// Get pilot vectors for collapse direction
		FVector PilotFwd2 = FVector::ForwardVector;
		FVector PilotRight2 = FVector::RightVector;
		if (ConnectedParamotor)
		{
			APawn* Pawn2 = Cast<APawn>(ConnectedParamotor->GetAttachParentActor());
			if (Pawn2)
			{
				PilotFwd2 = Pawn2->GetActorForwardVector();
				PilotFwd2.Z = 0;
				PilotFwd2.Normalize();
				PilotRight2 = Pawn2->GetActorRightVector();
				PilotRight2.Z = 0;
				PilotRight2.Normalize();
			}
		}
		UpdateKitingCollapses(DeltaTime, PilotFwd2, PilotRight2);

} // close if (WingState == Inflated)
} // close UpdateInflation

FVector AWingActor::ComputeGroundWind()
{
	// Wind direction: rotate the base WindDirection by the direction shift
	FVector ShiftedWindDir = WindDirection.RotateAngleAxis(WindDirShift * 180.0f / PI, FVector(0, 0, 1));
	// Ground-level wind reduction: wind is slower near the ground due to surface friction
	FVector WingLoc = GetActorLocation();
	float WingAltitudeM = FMath::Max(0.0f, (WingLoc.Z - GetGroundZ(WingLoc)) / CM_PER_M);
	float GroundWindFactor = FMath::Clamp(FMath::Pow(FMath::Max(0.5f, WingAltitudeM / 10.0f), 0.15f), 0.5f, 1.0f);
	float GroundWindSpeed = CurrentWindSpeed * GroundWindFactor;
	return ShiftedWindDir * GroundWindSpeed * CM_PER_M;  // cm/s
}

void AWingActor::UpdateKitingCollapses(float DeltaTime, const FVector& PilotFwd2, const FVector& PilotRight2)
{
	// Real collapses happen during ground handling when:
	// 1. The wing surges too far forward (low AoA -> leading edge folds under)
	// 2. Turbulence/gusts hit the wing asymmetrically
	// 3. Wind direction shifts suddenly
	// 4. The pilot over-brakes then releases (wing shoots forward)
	//
	// KEY: Overloaded wings are MORE collapse-resistant, not less.
	// Higher wing loading = more internal pressure = stiffer canopy.

	float KiteWeightRatio = WingModel.MaxTakeoffWeight > 0.0f ? PilotWeight / WingModel.MaxTakeoffWeight : 1.0f;
	float CollapseResistance = FMath::Max(0.1f, KiteWeightRatio);
	CollapseResistance /= GetTrimmerCollapseRisk();

	float LocalTotalBrake = (LeftBrakeAmount + RightBrakeAmount) * 0.5f;
	float BrakeDampingFactor = 1.0f / (1.0f + LocalTotalBrake * 1.5f);
	float KiteDeflateRate = FMath::Max(0.5f, CurrentWindSpeed * 0.33f);

	// === FRONTAL COLLAPSE during kiting ===
	float ForwardSurge = FVector::DotProduct(KiteOffset, PilotFwd2);
	if (ForwardSurge > LINE_LENGTH_CM * 0.5f)
	{
		float FrontalSeverity = FMath::Clamp((ForwardSurge - LINE_LENGTH_CM * 0.5f) / (LINE_LENGTH_CM * 0.3f), 0.0f, 1.0f);
		float q = 0.5f * AIR_DENSITY_SEA_LEVEL * CurrentWindSpeed * CurrentWindSpeed;
		float qRef = 0.5f * AIR_DENSITY_SEA_LEVEL * FMath::Square(MIN_TAKEOFF_AIRSPEED_MS);
		float DynamicPressureFactor = q / FMath::Max(0.1f, qRef);
		FrontalSeverity *= DynamicPressureFactor;
		FrontalSeverity *= (1.0f + FMath::Abs(WindTurbulence) * 0.5f);
		FrontalSeverity /= CollapseResistance;
		if (LocalTotalBrake < 0.01f)
			FrontalSeverity *= BrakeDampingFactor;
		FrontalCollapseAmount = FMath::Min(1.0f, FrontalCollapseAmount + FrontalSeverity * DeltaTime * KiteDeflateRate);
		if (FrontalCollapseAmount > 0.1f)
		{
			UE_LOG(LogTemp, Warning, TEXT("WingActor: KITING FRONTAL COLLAPSE! surge=%.0f wind=%.1f turb=%.2f resist=%.2f"),
				ForwardSurge, CurrentWindSpeed, WindTurbulence, CollapseResistance);
		}
	}

	// === ASYMMETRIC COLLAPSE during kiting (DETERMINISTIC) ===
	float WindSideComponent = FVector::DotProduct(
		WindDirection.RotateAngleAxis(WindDirShift * 180.0f / PI, FVector(0, 0, 1)),
		PilotRight2);

	if (FMath::Abs(WindSideComponent) > 0.1f)
	{
		float AsymSeverity = FMath::Abs(WindSideComponent);
		AsymSeverity /= CollapseResistance;
		bool bRightCollapse = (WindSideComponent > 0.0f);
		float CollapseAmt = AsymSeverity * DeltaTime * KiteDeflateRate;
		if (bRightCollapse)
		{
			RightCollapseAmount = FMath::Min(1.0f, RightCollapseAmount + CollapseAmt);
			if (CollapseAmt > 0.01f)
			{
				UE_LOG(LogTemp, Warning, TEXT("WingActor: KITING ASYMMETRIC RIGHT COLLAPSE %.0f%%! wind=%.1f turb=%.2f resist=%.2f"),
					CollapseAmt * PERCENT_FACTOR, CurrentWindSpeed, WindTurbulence, CollapseResistance);
			}
		}
		else
		{
			LeftCollapseAmount = FMath::Min(1.0f, LeftCollapseAmount + CollapseAmt);
			if (CollapseAmt > 0.01f)
			{
				UE_LOG(LogTemp, Warning, TEXT("WingActor: KITING ASYMMETRIC LEFT COLLAPSE %.0f%%! wind=%.1f turb=%.2f resist=%.2f"),
					CollapseAmt * PERCENT_FACTOR, CurrentWindSpeed, WindTurbulence, CollapseResistance);
			}
		}
	}

	// === COLLAPSE RECOVERY during kiting ===
	float RecoveryRate = FMath::Max(0.3f, CurrentWindSpeed * 0.20f) / CollapseResistance;
	RecoveryRate *= (1.0f - LocalTotalBrake);
	if (FrontalCollapseAmount > 0.0f)
		FrontalCollapseAmount = FMath::Max(0.0f, FrontalCollapseAmount - RecoveryRate * DeltaTime);
	if (LeftCollapseAmount > 0.0f)
		LeftCollapseAmount = FMath::Max(0.0f, LeftCollapseAmount - RecoveryRate * DeltaTime);
	if (RightCollapseAmount > 0.0f)
		RightCollapseAmount = FMath::Max(0.0f, RightCollapseAmount - RecoveryRate * DeltaTime);
}

float AWingActor::GetTakeoffChance() const{
	// 0-100% based on weight ratio, wind, and current airspeed
float WeightRatio = WingModel.MaxTakeoffWeight > 0.0f ? PilotWeight / WingModel.MaxTakeoffWeight : 1.0f;
if (WeightRatio >= 1.0f) return 0.0f;
// Overloaded — impossible
// Weight factor: 100% at light load, drops near 0 at max load

float WeightFactor = FMath::Clamp(1.0f - WeightRatio * 0.8f, 0.0f, 1.0f);
// Wind factor: headwind helps, no wind is neutral, tailwind hurts
float WindMs = CurrentWindSpeed;  // Use gusty live wind, not base GI->WindSpeed
// Current airspeed (from ground run or flight)
float CurrentAirspeed = 0.0f;
if (ConnectedParamotor)	{
		APawn* Pawn = Cast<APawn>(ConnectedParamotor->GetAttachParentActor());
if (Pawn)		{
			FVector Vel = Pawn->GetVelocity();
FVector ShiftedDir = WindDirection.RotateAngleAxis(WindDirShift * 180.0f / PI, FVector(0, 0, 1));
FVector Wind = ShiftedDir * WindMs  * CM_PER_M;
FVector Apparent = Wind - Vel;
FVector Fwd = Pawn->GetActorForwardVector();
Fwd.Z = 0;
Fwd.Normalize();
float Headwind = -FVector::DotProduct(Apparent, Fwd)  / CM_PER_M;
CurrentAirspeed = FMath::Max(0.0f, Headwind);
}
}
	// Takeoff speed from stall speed (derived from wing model + mass)
float StallDenom = AIR_DENSITY_SEA_LEVEL * WingModel.WingArea * WingModel.CLMax;
float TakeoffSpeed = StallDenom > 0.0f ? FMath::Sqrt(2.0f * (PilotWeight + WingModel.WingWeight + 1.5f) * GRAVITY_MS2 / StallDenom) : 5.4f;
float SpeedFactor = FMath::Clamp(CurrentAirspeed / TakeoffSpeed, 0.0f, 1.5f);
// Throttle factor: need at least 25% throttle
float ThrottleFactor = (ThrottleAmount > 0.25f) ? 1.0f : ThrottleAmount / 0.25f;
// Combined chance
float Chance = WeightFactor * SpeedFactor * ThrottleFactor * PERCENT_FACTOR;
return FMath::Clamp(Chance, 0.0f, 100.0f);
}

