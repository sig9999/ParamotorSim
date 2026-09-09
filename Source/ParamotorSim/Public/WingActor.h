#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/StaticMeshComponent.h"
#include "ProceduralMeshComponent.h"
#include "AtmosphereModel.h"
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
static constexpr float PI_DIV_180 = 0.01745329251994329576f; // PI/180 as float
static constexpr float RAD_TO_DEG = 180.0f / PI;       // Radians to degrees
static constexpr float PERCENT_FACTOR = 100.0f;       // Fraction to percent (0-1 → 0-100)

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

// Inflation constants
static constexpr float MIN_TAKEOFF_AIRSPEED_MS = 4.0f;      // Min airspeed for takeoff (was 3.0)

// Aerodynamic defaults
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

class USceneComponent;
class UMaterialInterface;
class AParamotorActor;
class UCharacterMovementComponent;

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wing")
	float TrimAoA = 2.0f;             // Trim angle of attack (degrees)
};

// ============================================================
// Thermal — convective updraft column
// ============================================================
struct FThermal
{
	FVector Center;         // Ground-level center position (world space, cm)
	float Radius;           // Thermal core radius (cm) — strongest at center, fades to edge
	float Strength;         // Peak updraft at core (m/s)
	float BaseAltitude;     // Bottom of thermal (m above ground, usually 0)
	float TopAltitude;      // Top of thermal (m above ground — cloudbase/inversion)
	float Lifetime;         // Remaining lifetime (s) — thermals decay
	float MaxLifetime;      // Initial lifetime (s)
	float DriftVel;         // Horizontal drift speed (cm/s) — thermals drift with wind
	bool bActive;           // Whether this thermal is currently generating lift
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
	virtual float GetTakeoffChance() const;  // 0-100% based on weight, wind, airspeed

	// === TRIMMER PUBLIC API ===
	float GetTrimmerPosition() const { return TrimmerPosition; }
	void SetTrimmerTarget(float NewTarget) { TrimmerTarget = FMath::Clamp(NewTarget, -1.0f, 1.0f); }
	float GetTrimmerTargetPos() const { return TrimmerTarget; }
	FString GetTrimmerLabel() const;

	// === Public API ===
	virtual bool UnpackFromBag();
	virtual bool GrabNearestEdge(FVector PlayerHandPos);
	virtual void ReleaseEdge();
	virtual void UpdateGrabPosition(FVector NewHandPos);
	bool IsGrabbing() const { return GrabbedParticleIndex >= 0; }

	virtual void ConnectRisers(AParamotorActor* Paramotor, FVector LeftCarabiner, FVector RightCarabiner);
	virtual void DisconnectRisers();
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

	// Unified wind access — single source of truth for all consumers
	// Returns wind direction with turbulence shift applied (normalized, world space)
	FVector GetShiftedWindDirection() const;
	// Returns full wind vector in cm/s (Unreal units), including turbulence shift + gusts
	FVector GetWindVector() const;
	// Returns wind compass heading in degrees (0=N=+X, 90=E=+Y), with turbulence shift
	float GetWindHeadingDeg() const;
	float GetWingLoading() const;  // kg/m²
	bool IsOverloaded() const { return PilotWeight > WingModel.MaxTakeoffWeight; }

	// Inflation — hold R to lift A-lines
	virtual void LiftALines();
	virtual void ReleaseALines();
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

	// Force takeoff for debugging
	virtual void ForceTakeoff();

	// Brake input — Q = left, E = right
	void SetLeftBrake(float Amount);
	void SetRightBrake(float Amount);
	void SetWeightShift(float WS) { WeightShift = FMath::Clamp(WS, -1.0f, 1.0f); }
	float GetLeftBrakeAmount() const { return LeftBrakeAmount; }
	float GetRightBrakeAmount() const { return RightBrakeAmount; }

	// Get brake handle world positions (for character arm IK)
	virtual FVector GetLeftBrakeHandleWorldPos() const;
	virtual FVector GetRightBrakeHandleWorldPos() const;

	bool IsInBag() const { return WingState == EWingState::InBag; }
	bool IsCrinkled() const { return WingState == EWingState::Crinkled; }
	bool IsSpread() const { return WingState == EWingState::Spread; }
	EWingState GetWingState() const { return WingState; }

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wing")
	USceneComponent* Root = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wing")
	UProceduralMeshComponent* CanopyMesh = nullptr;

	// Real riser mesh (loaded from SM_Risers_Both.uasset, exported from charger_full.blend)
	UPROPERTY()
	UStaticMeshComponent* RiserMeshComponent = nullptr;

	// Cached riser mesh bounds (computed once at init from actual mesh geometry)
	FVector RiserMeshBoundsCenter = FVector::ZeroVector;
	FVector RiserMeshBoundsExtent = FVector::ZeroVector;
	bool bRiserBoundsCached = false;

	// Test wing state: 0=deflated, 1=inflated (smooth transition)
	float TestWingInflation = 0.0f;
	// Whether the procedural mesh sections have been created (avoid recreating every frame)
	bool bTestWingCanopyCreated = false;
	bool bTestWingLinesCreated = false;
	// Cached line mesh vertex count (for UpdateMeshSection)
	int32 TestWingLineMeshVertCount = 0;
	// Test wing line particles (simplified Verlet for real lines)
	struct FTestLineParticle { FVector Pos; FVector OldPos; bool bPinned; };
	TArray<FTestLineParticle> TestLineParticles;
	// Test wing line segments (pairs of particle indices)
	TArray<int32> TestLineSegments;
	// Test wing line rest lengths
	TArray<float> TestLineRestLengths;
	// Test wing line colors
	TArray<FLinearColor> TestLineColors;

	// Pre-computed canopy mesh data (created once, reused every frame — INSTANCE members, not static)
	TArray<FVector> TWC_Verts;
	TArray<FVector> TWC_Normals;
	TArray<FVector2D> TWC_UVs;
	TArray<FProcMeshTangent> TWC_Tangents;
	TArray<TArray<int32>> TWC_MatTris;
	TArray<FLinearColor> TWC_MatColors;

	// Pre-computed line mesh data (created once, reused every frame)
	TArray<int32> TWL_Tris;
	TArray<FLinearColor> TWL_Colors;
	TArray<FProcMeshTangent> TWL_Tangents;
	TArray<FVector2D> TWL_UVs;

	// Pre-computed line-to-canopy attachments (which canopy particle each line top follows)
	TArray<int32> TWL_TopCanopyIdx;
	TArray<bool> TWL_IsMain;
	TArray<bool> TWL_IsLeft;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wing")
	UStaticMeshComponent* BagMesh = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wing")
	UProceduralMeshComponent* LineMesh = nullptr;

	// LineMID removed (was unused)

	// === State ===
	EWingState WingState = EWingState::InBag;
	int32 GrabbedParticleIndex = -1;
	FVector GrabHandPos = FVector::ZeroVector;

	// Inflation state
	bool bLiftingALines = false;
	float InflationProgress = 0.0f;

	// === LOG TIMERS ===
	float FlightLogTimer = 0.0f;
	float CurrentRoll = 0.0f;  // Smoothed roll for flight steering

	// Flight state
	bool bIsAirborne = false;          // True once pilot leaves ground
	float Airspeed = 0.0f;             // Current airspeed (cm/s)
	float ClimbRate = 0.0f;            // Vertical speed (cm/s)
	float GlideRatio = 0.0f;           // Current L/D glide ratio
	float ThrottleAmount = 0.0f;       // 0-1, set by controller
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
	bool bAutoSeatPending = false;     // True after takeoff — auto-seat after delay
	float LaunchTimer = 0.0f;          // Time since takeoff (for launch-to-seat transition)

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
	float GetTrimmerPitchOffset() const; // Wing pitch offset (deg): + = hangs back (slow)
	float GetTrimmerCollapseRisk() const;// Multiplier for collapse risk

	// Brake state
	float LeftBrakeAmount = 0.0f;
	float RightBrakeAmount = 0.0f;
	float LeftBrakeTarget = 0.0f;
	float RightBrakeTarget = 0.0f;

	// Wind direction — captured when risers connect, blows FROM player's facing direction (headwind)
	FVector WindDirection = FVector(1, 0, 0);  // default +X

	// === DYNAMIC WIND (gusts, turbulence, direction shifts) ===
	float CurrentWindSpeed = 6.0f;       // m/s, varies with gusts + altitude gradient
	float WindTurbulence = 0.0f;         // Turbulence component (Dryden filtered)
	float WindDirShift = 0.0f;           // Direction shift angle (radians, from lateral turbulence)
	float EkmanShift = 0.0f;             // Direction shift from Ekman spiral (radians, altitude-dependent)

	// === ATMOSPHERIC STATE (computed each frame from weather + altitude) ===
	EPasquillStability CurrentStability = EPasquillStability::D_Neutral;
	float BoundaryLayerHeightM = 1000.0f;  // Current BL height (m)
	float FrictionVelocityUStar = 0.3f;    // u* (m/s), from log law
	float MoninObukhovLength = 1e6f;        // L (m), from stability
	float InversionAltitudeM = 2000.0f;     // Inversion layer altitude (m)
	float DewPointC = 10.0f;                // Dew point (°C), from temp + humidity
	float SeaBreezeMs = 0.0f;               // Sea breeze contribution (m/s, + = onshore)
	bool bOverdevelopmentRisk = false;      // True when OD conditions are present
	EFlyability CurrentFlyability = EFlyability::Good;

	// === DRYDEN TURBULENCE MODEL (MIL-HDBK-1797B) ===
	// Three axes: longitudinal (along wind), lateral (crosswind), vertical
	// Each axis has its own filter state and white noise input.
	// Lateral and vertical use 2nd-order filters (per MIL-HDBK-1797B).
	float TurbLongState = 0.0f;          // Longitudinal turbulence filter state (m/s)
	float TurbLatState = 0.0f;           // Lateral turbulence filter state (m/s)
	float TurbLatState2 = 0.0f;          // Lateral 2nd-order filter state
	float TurbVertState = 0.0f;          // Vertical turbulence filter state (m/s)
	float TurbVertState2 = 0.0f;         // Vertical 2nd-order filter state
	float TurbIntensity = 1.5f;          // Turbulence intensity σ (m/s)
	float TurbScaleLength = 200.0f;      // Scale length L (m) — altitude-dependent (MIL-HDBK)
	float TurbSigmaU = 1.5f;             // Longitudinal σ (m/s)
	float TurbSigmaV = 1.0f;             // Lateral σ (m/s)
	float TurbSigmaW = 1.0f;             // Vertical σ (m/s)

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
	TArray<FVector> TargetPositions;  // Spring-coupled target shape for cloth particles
	TArray<FClothConstraint> Constraints;

	// === Materials ===
	UPROPERTY()
	UMaterialInterface* CanopyMaterial = nullptr;
	UPROPERTY()
	UMaterialInterface* LineMaterial = nullptr;  // Cached line mesh material (was per-frame LoadObject)
	// CanopyMID removed (was unused)
	// WingLight removed (was unused)

	// === Functions ===
	// === MacPara Charger (real data from MacParaChargerData.h) ===
	void InitTestWingCloth();            // Create particles from real MPC vertices
	void InitTestWingLines();            // Create line particles from real MPCL line endpoints
	void InitTestWingMeshes();           // Create procedural mesh sections ONCE (performance)
	void InitCanopyMeshSection();        // Create canopy procedural mesh from MPC data
	void InitLineMeshSection();          // Create line tube procedural mesh from MPCL data
	void InitRiserMeshComponent();       // Load and position real riser static mesh
	void SimulateTestWingCloth(float DeltaTime);  // Verlet cloth on real mesh
	void SimulateTestWingLines(float DeltaTime);  // Line sag/tension on real lines
	void UpdateTestWingCanopyMesh();     // Update canopy vertex positions (no recreation)
	void UpdateTestWingLineMesh();       // Update line vertex positions (no recreation)
	void SetTestWingDeflated();          // Collapse cloth to ground (deflated state)
	void SetTestWingLinesDeflated();     // Collapse line particles to pile near canopy
	void SetSpreadState();

	float GetGroundZ(FVector Pos) const;
	// Cached ground Z — call once per tick, then use the cached value
	float GetCachedGroundZ(FVector Pos);
	void InvalidateGroundZCache() { bGroundZCachedThisTick = false; }

	// Update dynamic wind (gusts, turbulence, direction shift) — called from both ground and flight
	void UpdateDynamicWind(float DeltaTime, float GustAmplitude);

	// === PER-TICK CACHED VALUES (avoid redundant line traces per frame) ===
	float CachedWingGroundZ = 0.0f;     // Ground Z at wing position (cached per tick)
	float CachedPilotGroundZ = 0.0f;    // Ground Z at pilot position (cached per tick)
	bool bGroundZCachedThisTick = false;

	// === CACHED GAME INSTANCE ===
	UPROPERTY()
	class UParamotorGameInstance* CachedGameInstance = nullptr;

	// === THERMAL SYSTEM ===
	TArray<FThermal> Thermals;
	float ThermalSpawnTimer = 0.0f;        // Timer for spawning new thermals
	float ThermalStrengthMultiplier = 1.0f; // From temperature, cloud cover, time of day
	float CloudbaseAltitudeM = 1500.0f;    // Thermal top altitude (m above ground) — from dew point
	float ThermalSinkMs = 0.0f;            // Compensating subsidence between thermals (m/s)
	float CurrentThermalClimbRate = 0.0f;  // Current peak thermal climb rate (m/s)

	// Thermal system methods
	void UpdateThermals(float DeltaTime);
	void SpawnThermal();
	float GetThermalLiftAtPosition(FVector WorldPos, float AltitudeM);  // Returns updraft m/s
	float GetThermalSinkAtPosition(FVector WorldPos, float AltitudeM);  // Returns subsidence m/s
	float ComputeThermalStrengthMultiplier() const;  // From weather conditions

	// Public accessors for atmospheric state (used by HUD, windsock, etc.)
public:
	EFlyability GetFlyability() const { return CurrentFlyability; }
	bool IsOverdevelopmentRisk() const { return bOverdevelopmentRisk; }
	float GetBoundaryLayerHeight() const { return BoundaryLayerHeightM; }
	float GetCloudbase() const { return CloudbaseAltitudeM; }
	float GetDewPoint() const { return DewPointC; }
protected:

	// === TICK HELPER ===
	void TickTestWing(float DeltaTime);

	// === TICK SUB-HELPERS (split from TickTestWing) ===
	void UpdateConnectedState(float DeltaTime);  // Line tension drag, yaw, inflation transition
	void UpdateRiserMesh();                      // Position riser mesh at carabiners or line bottoms

	// === INIT HELPERS (split from BeginPlay) ===
	void LoadMaterials();                        // Load canopy + line materials

	// === FLIGHT MODE HELPER ===
	void EnterFlightMode(ACharacter* Char);      // Disable CMC, set up flight rotation

	// === FLIGHT PHYSICS ===
	void UpdateFlight(float DeltaTime);
	void CheckTakeoff(float DeltaTime);

	// === FLIGHT PHYSICS HELPERS (split from UpdateFlight) ===
	// Intermediate flight computation state shared between helpers.
	struct FFlightCtx
	{
		float Vms = 0.0f;             // Horizontal airspeed (m/s)
		float AoA = 0.0f;             // Angle of attack (deg)
		float CL = 0.0f;             // Lift coefficient
		float CDtotal = 0.0f;        // Total drag coefficient
		float TotalBrake = 0.0f;     // Average brake 0-1
		float Rho = 1.225f;          // Air density at altitude
		float AltitudeM = 0.0f;      // Altitude above ground (m)
		float TotalLiftN = 0.0f;     // Total lift force (N)
		float TotalDragN = 0.0f;     // Total drag force (N)
		float ThrustN = 0.0f;        // Engine thrust (N)
		float TotalMassKg = 0.0f;    // Pilot + wing mass (kg)
		FVector ApparentHoriz = FVector::ZeroVector;  // Horizontal apparent wind (cm/s)
		FVector ApparentDir = FVector::ForwardVector;  // Normalized apparent wind dir
		FVector ShiftedWindDir = FVector::ForwardVector; // Wind dir with turbulence shift
		FVector NewVel = FVector::ZeroVector;  // Integrated velocity (cm/s)
	};
	void ComputeAeroState(FFlightCtx& Ctx, FVector Vel, float DeltaTime, FVector PawnLoc);
	void ComputeForces(FFlightCtx& Ctx);
	void IntegrateForces(FFlightCtx& Ctx, FVector Vel, float DeltaTime, FVector PawnLoc);
	void UpdatePendulumAndSteering(float DeltaTime);
	void UpdatePilotOrientation(APawn* Pawn, ACharacter* Char, float DeltaTime);
	void HandleLanding(APawn* Pawn, ACharacter* Char, UCharacterMovementComponent* CMC, FFlightCtx& Ctx, FVector& NewLoc, float DeltaTime);
	void UpdateWingTransform(FFlightCtx& Ctx, FVector PilotLoc, float DeltaTime);
};
