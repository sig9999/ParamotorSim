#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/StaticMeshComponent.h"
#include "ProceduralMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "WingActor.generated.h"

// ============================================================
// PHYSICS CONSTANTS — all in Unreal units (cm, deg, s)
// ============================================================
// Conversions
static constexpr float CM_PER_M = 100.0f;
static constexpr float GRAVITY_MS2 = 9.81f;           // m/s²
static constexpr float GRAVITY_CM_S2 = 981.0f;         // cm/s² (Unreal)
static constexpr float AIR_DENSITY_SEA_LEVEL = 1.225f; // kg/m³
static constexpr float MPH_PER_MS = 2.23694f;
static constexpr float KMH_PER_MS = 3.6f;
static constexpr float KNOTS_PER_MS = 1.94384f;
static constexpr float PI_DIV_180 = 0.01745329251994329576f; // PI/180 as float

// Altitude-dependent air density (ISA model)
// ρ(h) = ρ₀ × (1 - 0.0065×h/288.15)^5.255
// At 0m: 1.225 kg/m³. At 1000m: 1.112. At 2000m: 1.006. At 3000m: 0.909.
inline float AirDensityAtAltitude(float AltitudeM)
{
    return AIR_DENSITY_SEA_LEVEL * FMath::Pow(1.0f - 0.0065f * AltitudeM / 288.15f, 5.255f);
}

// Wing geometry constants (cm) — MacPara Charger 25M
static constexpr float LINE_LENGTH_CM = 750.0f;        // 7.5m avg carabiner-to-canopy (real Charger 25M)
static constexpr float RISER_LENGTH_CM = 52.5f;        // 0.525m riser (real Charger neutral position)
static constexpr float CARABINER_SPACING_CM = 42.0f;   // 0.42m between carabiners
static constexpr float BRAKE_SLACK_CM = 7.0f;          // 7cm slack before brake engages
static constexpr float MAX_KITE_OFFSET_CM = 350.0f;    // Max kiting drift before collapse
static constexpr float MAX_KITE_VELOCITY_CM_S = 500.0f; // Max kite drift speed

// Inflation constants
static constexpr float INFLATION_TARGET_HEIGHT_CM = 200.0f; // Fully inflated height
static constexpr float INFLATION_OVERSHOOT_BASE_CM = 40.0f; // Base overshoot past zenith
static constexpr float TAKEOFF_HEIGHT_THRESHOLD_CM = 50.0f; // Min skip height for takeoff
static constexpr float MIN_TAKEOFF_AIRSPEED_MS = 4.0f;      // Min airspeed for takeoff (was 3.0)

// Aerodynamic defaults
static constexpr float DEFAULT_TRIM_AOA_DEG = 8.0f;    // Base AoA at trim
static constexpr float DEFAULT_STALL_AOA_DEG = 15.0f;  // Stall angle
static constexpr float OSWALD_EFFICIENCY = 0.90f;       // Modern paraglider
static constexpr float MAX_THRUST_N = 736.0f;           // Full throttle thrust (~75 kgf)
static constexpr float REDLINE_RPM = 7800.0f;           // Moster 185 redline (was 9000)
static constexpr float IDLE_RPM = 2500.0f;              // Moster 185 idle
static constexpr float PROP_REDUCTION_RATIO = 2.58f;    // Moster 185 reduction ratio

// === PROPELLER SPECIFICATIONS (Moster 185) ===
// For blade element theory computation
static constexpr float PROP_DIAMETER_CM = 130.0f;       // 130cm (51") — Moster 185 standard prop
static constexpr float PROP_PITCH_CM = 50.0f;           // ~50cm pitch (typical paramotor prop)
static constexpr float PROP_NUM_BLADES = 2.0f;          // 2-blade wooden prop
static constexpr float PROP_CHORD_CM = 8.0f;            // ~8cm average blade chord
static constexpr float PROP_BLADE_ELEMENTS = 20;        // Number of blade elements for integration

// Pendulum constants
static constexpr float PENDULUM_LENGTH_CM = 750.0f;    // 7.5m effective pendulum (real Charger 25M)
static constexpr float PENDULUM_PERIOD_S = 5.5f;       // ~5.5s for 7.5m pendulum

class USceneComponent;
class UStaticMeshComponent;
class UProceduralMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UPointLightComponent;
class AParamotorActor;

// ============================================================
// Wing states
// ============================================================
UENUM(BlueprintType)
enum class EWingState : uint8
{
	InBag,
	Crinkled,
	Spread,
	Connected,
	Inflating,
	Inflated,
	Flying,
	Collapsed,
};

// ============================================================
// Verlet cloth particle
// ============================================================
struct FClothParticle
{
	FVector Pos;
	FVector OldPos;
	bool bPinned;
	bool bGrabbed;
	// GrabTarget removed (was unused)
};

// ============================================================
// Distance constraint
// ============================================================
struct FClothConstraint
{
	int32 A;
	int32 B;
	float RestLength;
	float Stiffness;
};

// ============================================================
// Line levels (A, B, C, D, Brake)
// ============================================================
enum class ELineLevel : uint8
{
	A = 0,
	B = 1,
	C = 2,
	D = 3,
	E = 4,        // MacPara Charger E lines (behind D, near trailing edge)
	Brake = 5,
};

// ============================================================
// Wing model specs — defines a wing type (size, weight, performance)
// ============================================================
USTRUCT(BlueprintType)
struct FWingModel
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wing")
	FString Name = TEXT("Sig1");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wing")
	float WingArea = 25.0f;            // m²

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wing")
	float MaxTakeoffWeight = 130.0f;   // kg

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wing")
	float WingWeight = 6.0f;           // kg

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wing")
	float CL0 = 0.3f;              // Cambered airfoil lift at 0° AoA

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wing")
	float CLMax = 1.5f;           // Realistic paraglider max CL

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wing")
	float CD0 = 0.045f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wing")
	float AR = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wing")
	float StallAoA = 15.0f;
};

// ============================================================
// AWingActor — Realistic paramaglider wing with cells, ribs,
// proper line cascade, and real riser geometry
// ============================================================

UCLASS()
class PARAMOTORSIM_API AWingActor : public AActor
{
	GENERATED_BODY()

public:
	AWingActor();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	// Wing model — defines size/performance specs
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wing")
	FWingModel WingModel;

	// Get wing info for HUD
	FString GetWingName() const { return WingModel.Name; }
	float GetTakeoffChance() const;  // 0-100% based on weight, wind, airspeed

	// === TRIMMER PUBLIC API ===
	float GetTrimmerPosition() const { return TrimmerPosition; }
	void SetTrimmerTarget(float NewTarget) { TrimmerTarget = FMath::Clamp(NewTarget, -1.0f, 1.0f); }
	float GetTrimmerTargetPos() const { return TrimmerTarget; }
	FString GetTrimmerLabel() const;

	// === Public API ===
	bool UnpackFromBag();
	bool GrabNearestEdge(FVector PlayerHandPos);
	void ReleaseEdge();
	void UpdateGrabPosition(FVector NewHandPos);
	bool IsGrabbing() const { return GrabbedParticleIndex >= 0; }

	void ConnectRisers(AParamotorActor* Paramotor, FVector LeftCarabiner, FVector RightCarabiner);
	void DisconnectRisers();
	void UpdateCarabinerPositions(FVector LeftCarabiner, FVector RightCarabiner);
	bool IsConnected() const { return bRisersConnected; }

	// Weight/lift — set pilot+equipment total weight (kg)
	void SetPilotWeight(float TotalWeightKg);
	float GetPilotWeight() const { return PilotWeight; }
	float GetMaxTakeoffWeight() const { return WingModel.MaxTakeoffWeight; }
	float GetWingArea() const { return WingModel.WingArea; }
	FVector GetWindDirection() const { return WindDirection; }
	float GetCurrentWindSpeedMs() const { return CurrentWindSpeed; }  // Live wind including gusts (m/s)
	float GetWindTurbulence() const { return WindTurbulence; }        // Current turbulence level
	float GetWingLoading() const;  // kg/m²
	bool IsOverloaded() const { return PilotWeight > WingModel.MaxTakeoffWeight; }

	// Inflation — hold R to lift A-lines
	void LiftALines();
	void ReleaseALines();
	bool IsInflating() const { return WingState == EWingState::Inflating; }
	bool IsInflated() const { return WingState == EWingState::Inflated; }
	bool IsFlying() const { return WingState == EWingState::Flying; }
	bool IsAirborne() const { return bIsAirborne; }
	float GetInflationProgress() const { return InflationProgress; }

	// Seat position — pilot transitions from standing to seated in flight
	bool IsSeated() const { return bSeated; }
	void ToggleSeat();
	// Hang point adjustment — changes motor lean angle (weight balance)
	void AdjustHangPoint(float Delta);
	float GetHangPointOffset() const { return HangPointOffset; }

	// Flight — set by controller each frame
	void SetThrottle(float T) { ThrottleAmount = FMath::Clamp(T, 0.0f, 1.0f); }
	void SetPilotVelocity(FVector V) { PilotVelocity = V; }
	void PumpBrakes();  // Quick brake pump to help open canopy
	float GetAirspeed() const { return Airspeed; }
	float GetClimbRate() const { return ClimbRate; }
	float GetPendulumAngle() const { return PendulumAngle; }
	float GetGlideRatio() const { return GlideRatio; }

	// Debug — ground run diagnostics
	float GetDebugGroundAirspeed() const { return DebugGroundAirspeed; }
	float GetDebugGroundSpeed() const { return DebugGroundSpeed; }
	float GetDebugHeadwind() const { return DebugHeadwind; }
	float GetDebugThrottle() const { return ThrottleAmount; }
	float GetDebugTakeoffThreshold() const { return DebugTakeoffThreshold; }
	float GetDebugWeightSupport() const { return DebugWeightSupport; }
	bool IsGroundRunActive() const { return WingState == EWingState::Inflated; }

	// Force takeoff for debugging
	void ForceTakeoff();

	// Brake input — Q = left, E = right
	void SetLeftBrake(float Amount);
	void SetRightBrake(float Amount);
	void SetWeightShift(float WS) { WeightShift = FMath::Clamp(WS, -1.0f, 1.0f); }
	float GetLeftBrakeAmount() const { return LeftBrakeAmount; }
	float GetRightBrakeAmount() const { return RightBrakeAmount; }

	// Get brake handle world positions (for character arm IK)
	FVector GetLeftBrakeHandleWorldPos() const;
	FVector GetRightBrakeHandleWorldPos() const;

	bool IsMostlySpread() const;
	float GetSpreadPercent() const;

	bool IsInBag() const { return WingState == EWingState::InBag; }
	bool IsCrinkled() const { return WingState == EWingState::Crinkled; }
	bool IsSpread() const { return WingState == EWingState::Spread; }
	EWingState GetWingState() const { return WingState; }

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wing")
	USceneComponent* Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wing")
	UProceduralMeshComponent* CanopyMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wing")
	UStaticMeshComponent* BagMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wing")
	UProceduralMeshComponent* LineMesh;

	// LineMID removed (was unused)

	// === State ===
	EWingState WingState = EWingState::InBag;
	int32 GrabbedParticleIndex = -1;
	FVector GrabHandPos = FVector::ZeroVector;

	// Inflation state
	bool bLiftingALines = false;
	float InflationProgress = 0.0f;
	float InflationMomentum = 0.0f;
	float InflationHeight = 0.0f;  // Current height of canopy during inflation (cm)
	float InflationVelocity = 0.0f;  // Vertical velocity during inflation rise (cm/s)
	float CellFillFactor = 0.0f;   // 0=empty cells, 1=fully inflated (ram-air pressure)
	float ALineFadeFactor = 0.0f;  // Fades A-line pull after inflation (1=full pull, 0=none)

	// === REAL CELL PRESSURE MODEL ===
	// A real paraglider has 30+ cells that fill with air through the open
	// leading edge. Each cell is a volume bounded by ribs (top and bottom
	// surfaces). Air enters at the leading edge from ram-air pressure.
	//
	// The internal pressure gives the canopy its airfoil shape.
	// Without pressure, the canopy is just fabric — it collapses.
	//
	// Physics:
	// - Ram-air pressure: P_ram = 0.5 × ρ × V² (dynamic pressure at leading edge)
	// - Cell fill rate: proportional to apparent wind speed
	// - Porosity: fabric leaks air slowly (cells lose pressure over time)
	// - Each cell has independent pressure (one cell can collapse while others fly)
	// - Pressure determines cell shape (inflated = airfoil, deflated = flat fabric)
	// - Lift comes from the pressurized airfoil shape, not a formula
	static constexpr int32 NUM_WING_CELLS = 92;  // 92 mesh cells (2x real 46 for smoothness)
	float CellPressure[NUM_WING_CELLS];          // Internal pressure per cell (Pa, 0=deflated, ~50=fully pressurized at 6m/s)
	float CellFill[NUM_WING_CELLS];              // Fill state per cell (0=empty, 1=full)
	float CellLift[NUM_WING_CELLS];              // Lift per cell (Newtons)
	float CellDrag[NUM_WING_CELLS];              // Drag per cell (Newtons)
	// Porosity: how fast cells leak air (lower = tighter fabric)
	float WingPorosity = 0.02f;                  // Air leak rate (per second at full pressure)

	// === KITING DYNAMICS (ground handling) ===
	// When inflated, the wing is a kite — wind pushes it around.
	// The pilot must actively manage it with brakes and body positioning.
	FVector KiteOffset = FVector::ZeroVector;      // Offset from directly above carabiner
	FVector KiteVelocity = FVector::ZeroVector;     // Kite drift velocity (cm/s)
	float KitePitchOvershoot = 0.0f;                // Pitch overshoot during inflation
	float KiteSinkHeight = 0.0f;                   // Height reduction from insufficient wind (cm)
	float KiteVerticalVel = 0.0f;                  // Vertical velocity of wing during kiting (cm/s)
	float KiteYawDrift = 0.0f;                      // Yaw drift from wind/turbulence (offset from wind-direction base)
	float KiteYawVel = 0.0f;                        // Yaw drift velocity
	float KiteRollSmoothed = 0.0f;                  // Smoothed roll for kiting (emerges from yaw, not direct brake)
	float PrevSymBrake = 0.0f;                      // Previous frame symmetric brake (for surge detection)
	float WingGroundYaw = 0.0f;                     // Absolute wing yaw during ground handling (wind-driven, NOT pilot-coupled)
	float WingGroundYawRate = 0.0f;                 // Yaw rate for inertia (deg/s)
	FVector WingGroundVel = FVector::ZeroVector;    // Wing velocity during Connected state (cm/s) — for inertia + air resistance

	// === REAL WING PHYSICS BODY ===
	// The wing is a real physical object with 3D velocity.
	// It moves from forces: lift from cell pressure, gravity, line tension, wind drag.
	// It is NOT teleported or Lerped — it integrates forces like a real object.
	FVector WingVel = FVector::ZeroVector;          // 3D wing velocity (cm/s) — real physics
	float WingPitchVel = 0.0f;                      // Pitch angular velocity (deg/s)
	float WingRollVel = 0.0f;                       // Roll angular velocity (deg/s)
	float WingYawVel = 0.0f;                        // Yaw angular velocity (deg/s)

	// === PER-SIDE KITING STATE (3D wing model) ===
	// The wing is modeled as two independent halves (left and right).
	// Each half has its own stall state, lift, and drag.
	// This creates realistic dynamics:
	// - Pulling one brake stalls that half → it loses lift, gains drag → wing yaws
	// - The stalled half takes time to recover (refill with air)
	// - Switching brakes doesn't instantly reverse — the stalled side must recover first
	// - Wind affects each half independently (causes wandering)
	float KiteLeftStall = 0.0f;      // 0=flying, 1=fully stalled (left half)
	float KiteRightStall = 0.0f;     // 0=flying, 1=fully stalled (right half)
	float KiteLeftLift = 0.0f;       // Current lift on left half (Newtons)
	float KiteRightLift = 0.0f;      // Current lift on right half (Newtons)
	float KiteLeftDrag = 0.0f;       // Current drag on left half (Newtons)
	float KiteRightDrag = 0.0f;      // Current drag on right half (Newtons)
	// Per-side wind perturbation (each half sees slightly different wind)
	float KiteLeftWindPerturb = 0.0f;
	float KiteRightWindPerturb = 0.0f;

	// === REAL LINE TENSION PER GROUP ===
	// In a real paraglider, the A/B/C/D lines carry different loads.
	// The load distribution is determined by the pressure distribution
	// across the chord. A lines carry the most load (~40%), D lines the least (~10%).
	// This affects line stretch, wear, and the wing's pitch moment.
	float LineTensionA = 0.0f;    // Tension in A lines (Newtons)
	float LineTensionB = 0.0f;    // Tension in B lines (Newtons)
	float LineTensionC = 0.0f;    // Tension in C lines (Newtons)
	float LineTensionD = 0.0f;    // Tension in D lines (Newtons)
	float LineTensionBrake = 0.0f; // Tension in brake lines (Newtons, only when pulled)
	// Overall line tension fraction (0 = slack, 1 = taut at line length)
	// Drives canopy shape: no tension = parachute billow, full tension = airfoil
	float LineTensionFraction = 0.0f;
	// Line stretch from tension (cm) — A lines stretch most
	float LineStretchA = 0.0f;
	float LineStretchB = 0.0f;
	float LineStretchC = 0.0f;
	float LineStretchD = 0.0f;
	FVector PrevPilotVel = FVector::ZeroVector;     // Previous frame pilot velocity (for inertial lag)

	// === LOG TIMERS (member variables, not statics) ===
	float KiteLogTimer = 0.0f;
	float GroundLogTimer = 0.0f;
	float SustainedAirTime = 0.0f;
	float FlightLogTimer = 0.0f;
	float StallLogTimer = 0.0f;
	float CurrentRoll = 0.0f;  // Smoothed roll for flight steering
	float CanopyOpenness = 0.0f;  // 0=crinkled, 1=fully open. Increased by brake pumping.
	float BrakePumpBoost = 0.0f;  // Temporary lift boost from brake pumping (decays over time)

	// Flight state
	bool bIsAirborne = false;          // True once pilot leaves ground
	float Airspeed = 0.0f;             // Current airspeed (cm/s)
	float ClimbRate = 0.0f;            // Vertical speed (cm/s)
	float GlideRatio = 0.0f;           // Current L/D glide ratio
	float ThrottleAmount = 0.0f;       // 0-1, set by controller
	float PrevThrottleAmount = 0.0f;   // Previous frame throttle (for rate-of-change)
	float ThrottleRate = 0.0f;         // Throttle change rate (per second), for pendulum impulse
	FVector PilotVelocity = FVector::ZeroVector;  // Pilot velocity from controller
	float WeightShift = 0.0f;          // -1=left, 0=center, +1=right (from mouse look)

	// Pendulum physics — pilot swings under wing when feet leave ground
	float PendulumAngle = 0.0f;        // 0 = directly below wing, + = swung forward (radians)
	float PendulumAngularVel = 0.0f;   // Angular velocity (rad/s)
	float PilotPitch = 0.0f;           // Current pilot pitch (degrees, 0=upright, -80=seated)
	float PilotPitchTarget = 0.0f;     // Target pitch (transitions to -80 on takeoff)
	float FlightTime = 0.0f;           // Time since takeoff (for transition timing)

	// Roll pendulum — pilot swings sideways under wing during turns
	// The pilot hangs on 7m lines and lags behind the wing's bank angle.
	// In a turn, centrifugal force pushes the pilot outward.
	// This creates the visible "swing" during turn entry/exit.
	float PilotRollAngle = 0.0f;       // Pilot's roll angle (degrees) — lags behind wing
	float PilotRollRate = 0.0f;        // Pilot roll angular velocity (deg/s)

	// Seat position and hang point
	bool bSeated = false;              // Pilot seated in harness (reduces drag)
	float SeatTransition = 0.0f;       // 0=standing, 1=fully seated (smooth transition)
	float HangPointOffset = 0.0f;      // -1.0 = motor leans back, +1.0 = motor leans forward
	float DragReductionFromSeat = 0.0f; // Drag reduction factor from seated position (0-0.3)

	// === WING INERTIA / APPARENT MASS ===
	// A paraglider wing doesn't respond instantly to inputs — it has inertia
	// from its own mass PLUS "apparent mass" (air it has to drag along with it).
	// This creates lag in roll, yaw, and pitch response — the "feel" of the wing.
	float WingRollRate = 0.0f;         // Current roll rate (deg/s) — has inertia
	float WingYawRate = 0.0f;          // Current yaw rate (deg/s) — has inertia
	float WingPitchAngle = 0.0f;       // Wing pitch relative to pilot (deg) — surge/oscillate
	float WingPitchRate = 0.0f;        // Wing pitch rate (deg/s)

	// === BRAKE RESPONSE LAG ===
	// Brakes don't instantly change the wing's aerodynamics — the trailing edge
	// takes time to deflect, and the airflow takes time to respond.
	// This creates the "spongy" feel of real brake inputs.
	float EffectiveLeftBrake = 0.0f;   // Brake amount after lag filter
	float EffectiveRightBrake = 0.0f;  // Brake amount after lag filter

	// === COLLAPSE STATE ===
	// Real wings collapse from turbulence, heavy brake at high AoA, or negative AoA.
	// Asymmetric collapse = one side tucks, pilot swings toward collapsed side.
	// Frontal collapse = leading edge tucks under, wing loses all lift briefly.
	float LeftCollapseAmount = 0.0f;   // 0=intact, 1=fully collapsed
	float RightCollapseAmount = 0.0f;  // 0=intact, 1=fully collapsed
	float FrontalCollapseAmount = 0.0f; // 0=intact, 1=fully collapsed
	float CollapseRecoveryTimer = 0.0f; // Time since collapse began (for auto-recovery)

	// === PARACHUTAL STALL ===
	// Too much brake at low airspeed = wing stops flying, becomes a parachute.
	// Descent rate increases dramatically, wing feels "mushy".
	bool bInParachutalStall = false;
	float ParachutalStallTimer = 0.0f;

	// === PROPELLER TORQUE EFFECT ===
	// The spinning propeller creates torque that rolls the pilot slightly.
	// This is a constant force when the engine is running.
	float PropTorqueRoll = 0.0f;       // Smoothed torque-induced roll

	// === DUTCH ROLL ===
	// Real paragliders have a gentle side-to-side yaw-roll oscillation mode.
	// The wing yaws slightly, then rolls, then yaws back. This is part of the "feel".
	float DutchRollYaw = 0.0f;         // Current Dutch roll yaw offset (deg)
	float DutchRollRoll = 0.0f;        // Current Dutch roll roll offset (deg)
	float DutchRollPhase = 0.0f;       // Phase for Dutch roll oscillation

	// === SPIRAL DIVE ===
	// When banked steeply (>40°), the spiral wants to increase on its own.
	// The wing dives steeper, speeds up, banks more — a real danger.
	// The pilot must actively exit by weight-shifting out and easing brakes.
	float SpiralDiveFactor = 0.0f;     // 0=normal, 1=full spiral instability

	// === TRIM SPEED ===
	// Real wings have a natural hands-off trim speed (~36 km/h = 10 m/s).
	// At trim, the wing flies straight and level with no brake input.
	// Brakes slow you below trim (more lift, more drag, slower speed).
	// Speed bar (not yet implemented) would speed you above trim.
	float TrimSpeedMs = 10.0f;         // Trim speed in m/s (set from wing model)

	// === TRIMMERS ===
	// Trimmers adjust the rear riser length, changing the angle of attack.
	// Range: -1.0 (full slow / trimmers in) to +1.0 (full fast / trimmers out)
	// 0.0 = neutral (standard trim, best for takeoff/landing/climbing)
	// Slow trim: higher AoA, more lift, more drag, slower speed, wing hangs back
	// Fast trim: lower AoA, less lift, less drag, faster speed, wing flies forward
	// Fast trim also makes the wing more prone to collapses with brake input.
	float TrimmerPosition = 0.0f;       // -1 = full slow, 0 = neutral, +1 = full fast
	float TrimmerTarget = 0.0f;         // Target position for smooth interpolation
	float TrimmerSpeed = 0.5f;          // How fast trimmers move (units per second)

	// Get trimmer-adjusted values for physics
	float GetTrimmerAoAOffset() const;   // Degrees: + = higher AoA (slow), - = lower (fast)
	float GetTrimmerSpeedFactor() const; // Multiplier for trim speed
	float GetTrimmerDragFactor() const;  // Multiplier for drag
	float GetTrimmerCLFactor() const;    // Multiplier for CL0
	float GetTrimmerPitchOffset() const; // Wing pitch offset (deg): + = hangs back (slow)
	float GetTrimmerCollapseRisk() const;// Multiplier for collapse risk

	// === TURBULENCE IN FLIGHT ===
	// Real air has turbulence that bumps the wing around in flight.
	FVector FlightTurbulence = FVector::ZeroVector; // Random turbulence force
	float FlightTurbPhase = 0.0f;      // Phase for turbulence oscillation

	// Brake state
	float LeftBrakeAmount = 0.0f;
	float RightBrakeAmount = 0.0f;
	float LeftBrakeTarget = 0.0f;
	float RightBrakeTarget = 0.0f;
	// Smoothed brake values for visual canopy (prevents popping artifacts)
	float SmoothedLeftBrake = 0.0f;
	float SmoothedRightBrake = 0.0f;
	float SmoothedKillFactor = 0.0f;  // Smoothed kill-the-wing factor

	// Wind direction — captured when risers connect, blows FROM player's facing direction (headwind)
	FVector WindDirection = FVector(1, 0, 0);  // default +X

	// === DYNAMIC WIND (gusts, turbulence, direction shifts) ===
	float CurrentWindSpeed = 6.0f;       // m/s, varies with gusts
	float WindGustPhase = 0.0f;          // Phase for gust oscillation
	float WindTurbulence = 0.0f;         // Turbulence component (von Kármán filtered)
	float WindDirShift = 0.0f;           // Direction shift angle (radians)

	// === VON KÁRMÁN TURBULENCE MODEL ===
	// The von Kármán model is the FAA/USAF standard for atmospheric turbulence.
	// It uses a spectral approach with proper spatial and temporal correlation,
	// producing realistic turbulence that builds up and decays naturally.
	// Implementation: Dryden approximation (filtered white noise through
	// first-order filters shaped to match the von Kármán spectrum).
	//
	// Three axes: longitudinal (along wind), lateral (crosswind), vertical
	// Each axis has its own filter state and white noise input.
	float TurbLongState = 0.0f;          // Longitudinal turbulence filter state (m/s)
	float TurbLatState = 0.0f;           // Lateral turbulence filter state (m/s)
	float TurbVertState = 0.0f;          // Vertical turbulence filter state (m/s)
	float TurbIntensity = 1.5f;          // Turbulence intensity σ (m/s), set from gusts
	float TurbScaleLength = 200.0f;      // Scale length L (m) — large eddies, typical for low altitude

	// === TAKEOFF SKIP/HOP STATE ===
	float TakeoffSkipZ = 0.0f;           // Current skip height (cm)
	// TakeoffSkipVel removed (was unused)
	int32 TakeoffFootContacts = 0;       // Number of foot touches during takeoff run

	// Debug ground run diagnostics
	float DebugGroundAirspeed = 0.0f;
	float DebugGroundSpeed = 0.0f;
	float DebugHeadwind = 0.0f;
	float DebugTakeoffThreshold = 0.0f;
	float DebugWeightSupport = 0.0f;

	bool bRisersConnected = false;
	UPROPERTY()
	AParamotorActor* ConnectedParamotor = nullptr;
	FVector LeftCarabinerPos = FVector::ZeroVector;
	FVector RightCarabinerPos = FVector::ZeroVector;

	// === Weight & Aerodynamics ===
	float PilotWeight = 110.0f;        // Total pilot+paramotor+fuel (kg), default 110
	// Wing specs are now in WingModel struct above
	float OswaldEfficiency = 0.90f;    // Oswald efficiency factor (modern paraglider)

	// === Cloth Simulation ===
	TArray<FClothParticle> Particles;
	TArray<FVector> TargetPositions;  // Spring-coupled target shape from ApplyInflatedShape
	TArray<FClothConstraint> Constraints;

	// === Line particles ===
	struct FLineParticle
	{
		FVector Pos;
		FVector OldPos;
		bool bPinned;
		int32 CanopyParticleIdx;
		float Radius;
		FLinearColor Color;
		float Mass;
		float Damping;
		int32 Side;      // 0=left, 1=right
		int32 Level;     // 0=A, 1=B, 2=C, 3=D, 4=Brake
		int32 RibIdx;    // Which rib this line attaches to
	};
	TArray<FLineParticle> LineParticles;
	TArray<FClothConstraint> LineConstraints;

	// Line attachment: canopy particle → line particle
	struct FLineAttachment
	{
		int32 CanopyParticleIdx;
		int32 LineParticleIdx;
		int32 Side;
		int32 Level;
		int32 RibIdx;
	};
	TArray<FLineAttachment> LineAttachments;

	// Riser and brake handle particle indices
	int32 LeftRiserIdx = -1;
	int32 RightRiserIdx = -1;
	int32 LeftCarabinerIdx = -1;
	int32 RightCarabinerIdx = -1;
	int32 LeftBrakeHandleIdx = -1;
	int32 RightBrakeHandleIdx = -1;

	// Physics parameters — all in cm/s² (Unreal units)
	float Gravity = GRAVITY_CM_S2;  // Unified gravity (was 600, inconsistent with flight's 980)
	float Damping = 0.85f;
	int32 ConstraintIterations = 8;
	float GroundFriction = 0.8f;

	// === Materials ===
	UPROPERTY()
	UMaterialInterface* CanopyMaterial;
	// CanopyMID removed (was unused)
	// WingLight removed (was unused)

	// === Functions ===
	void InitCloth();                    // Create particles from procedural wing mesh
	void InitLines();                    // Create line cascade with real lengths
	void SimulateCloth(float DeltaTime);
	void SimulateLines(float DeltaTime);
	void UpdateInflation(float DeltaTime);
	void UpdateFlight(float DeltaTime);
	// Real cell pressure model
	void UpdateCellPressure(float DeltaTime, float ApparentWindMs, float BrakeLeft, float BrakeRight);
	float GetTotalCellLiftN() const;
	float GetTotalCellDragN() const;
	float GetLeftCellLiftN() const;
	float GetRightCellLiftN() const;
	float GetLeftCellDragN() const;
	float GetRightCellDragN() const;
	void UpdateCanopyMesh();
	void UpdateLineMesh();
	// SetBagState, SetCrinkledState, CreateRiserLines removed (were unused)
	void SetSpreadState();
	void SetInflatedShape();
	void ApplyInflatedShape(float DeltaTime = -1.0f);  // Pass DeltaTime for consistent smoothing

	// Wing geometry helpers
	// GetRibCount, GetPointsPerRib, GetAirfoilPoint removed (were unused)
	int32 GetRibVertexIndex(int32 RibIdx, int32 PointIdx) const;

	float GetGroundZ(FVector Pos) const;

	// === COMMON HELPERS (extracted from duplicated code) ===
	// Get pilot forward/right vectors and velocity from connected paramotor
	struct FPilotFrame
	{
		FVector Forward = FVector::ForwardVector;
		FVector Right = FVector::RightVector;
		FVector Velocity = FVector::ZeroVector;
		float SpeedMs = 0.0f;  // Horizontal speed in m/s
		bool bValid = false;
	};
	FPilotFrame GetPilotFrame() const;

	// Update dynamic wind (gusts, turbulence, direction shift) — called from both ground and flight
	void UpdateDynamicWind(float DeltaTime, float GustAmplitude);

	// Shift all particles by a world-space delta (used when moving the actor)
	void ShiftAllParticles(FVector Shift);

	// Cached canopy bounds (computed once in InitCloth, not every frame)
	FVector CachedCanopyMin = FVector::ZeroVector;
	FVector CachedCanopyMax = FVector::ZeroVector;
	bool bCanopyBoundsCached = false;
};
