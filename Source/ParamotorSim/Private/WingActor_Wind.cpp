// Copyright Epic Games, Inc. All Rights Reserved.

#include "WingActor.h"
#include "ParamotorActor.h"
#include "ParamotorGameInstance.h"
#include "ParamotorLogging.h"
#include "AtmosphereModel.h"
#include "ProceduralMeshComponent.h"
#include "Materials/Material.h"

// ==== WIND + THERMALS ====

FVector AWingActor::GetShiftedWindDirection() const
{
	// Total direction shift = turbulence shift + Ekman spiral veering
	float TotalShift = WindDirShift + EkmanShift;
	return WindDirection.RotateAngleAxis(TotalShift * RAD_TO_DEG, FVector(0, 0, 1));
}

FVector AWingActor::GetWindVector() const
{
	return GetShiftedWindDirection() * CurrentWindSpeed * CM_PER_M;
}

float AWingActor::GetWindHeadingDeg() const
{
	// Compass heading the wind comes FROM (aviation convention), for HUD.
	// Shifted is a yaw-vector (0=+X=East in this ENU world); convert yaw to
	// compass (heading = 90 - yaw) then flip to the from-direction (+180).
	FVector Shifted = GetShiftedWindDirection();
	Shifted.Z = 0.0f;
	if (Shifted.Size2D() < 0.01f) return 0.0f;
	const float Yaw = FMath::Atan2(Shifted.Y, Shifted.X) * RAD_TO_DEG;
	return FMath::Fmod(270.0f - Yaw + 360.0f, 360.0f);
}

void AWingActor::UpdateDynamicWind(float DeltaTime, float GustAmplitude)
{
	// ============================================================
	// REAL ATMOSPHERIC WIND MODEL
	// Implements: Pasquill stability, log-law wind profile with
	// Monin-Obukhov correction, Ekman spiral direction veering,
	// MIL-HDBK-1797B Dryden turbulence with proper scale lengths
	// and 2nd-order lateral/vertical filters, sea breeze, and
	// stability-dependent turbulence intensity.
	// ============================================================

	float BaseWind = 0.0f;
	float TempC = 20.0f;
	float Humidity = 60.0f;
	float CloudCover = 0.25f;
	float TimeH = 12.0f;
	if (CachedGameInstance)
	{
		BaseWind = FMath::Max(0.0f, CachedGameInstance->WindSpeed);
		TempC = CachedGameInstance->Temperature;
		Humidity = CachedGameInstance->Humidity;
		CloudCover = CachedGameInstance->CloudCover;
		TimeH = CachedGameInstance->TimeOfDay;
	}

	// Get pilot position and altitude
	APawn* Pawn = nullptr;
	if (ConnectedParamotor)
		Pawn = Cast<APawn>(ConnectedParamotor->GetAttachParentActor());
	float AltM = 10.0f;  // Default reference height
	if (Pawn)
	{
		const FVector PawnLoc = Pawn->GetActorLocation();
		float GroundZ = GetCachedGroundZ(PawnLoc);
		AltM = FMath::Max(0.5f, (PawnLoc.Z - GroundZ) / CM_PER_M);
	}

	// === 1. PASQUILL STABILITY CLASS ===
	CurrentStability = ComputePasquillClass(TempC, BaseWind, TimeH, CloudCover);

	// === 2. BOUNDARY LAYER HEIGHT ===
	BoundaryLayerHeightM = ComputeBoundaryLayerHeight(CurrentStability, TimeH);

	// === 3. DEW POINT & CLOUDBASE ===
	DewPointC = ComputeDewPoint(TempC, Humidity);
	CloudbaseAltitudeM = ComputeCloudbase(TempC, DewPointC);

	// === 4. INVERSION ALTITUDE ===
	InversionAltitudeM = ComputeInversionAltitude(CurrentStability, TimeH);

	// === 5. MONIN-OBUKHOV LENGTH ===
	MoninObukhovLength = EstimateMoninObukhovLength(CurrentStability);

	// === 6. FRICTION VELOCITY u* ===
	// From log law: u* = κ * U_ref / [ln(z_ref/z0) - ψm(z_ref/L)]
	float Z0 = AtmosConst::Z0_DEFAULT;  // Coastal grass field
	float ZetaRef = AtmosConst::REF_HEIGHT_M / MoninObukhovLength;
	FrictionVelocityUStar = ComputeFrictionVelocity(BaseWind, Z0, ZetaRef);

	// === 7. WIND SPEED PROFILE (log law with stability correction) ===
	// U(z) = (u*/κ) * [ln(z/z0) - ψm(z/L) + ψm(z0/L)]
	float ZetaZ = AltM / MoninObukhovLength;
	float ZetaZ0 = Z0 / MoninObukhovLength;
	float ProfiledWind = LogWindProfile(AltM, FrictionVelocityUStar, Z0, ZetaZ, ZetaZ0);

	// Above the boundary layer, wind transitions to geostrophic (free atmosphere)
	// The BL top has the strongest wind; above that, wind may increase or decrease
	// depending on pressure patterns. We model a gradual increase above BL.
	if (AltM > BoundaryLayerHeightM)
	{
		// Above BL: wind increases slowly (geostrophic is typically stronger than surface)
		float AboveBL = (AltM - BoundaryLayerHeightM) / 1000.0f;  // km above BL
		float GeoFactor = 1.0f + FMath::Clamp(AboveBL * 0.1f, 0.0f, 0.5f);  // +10% per km
		ProfiledWind *= GeoFactor;
	}

	// === 8. EKMAN SPIRAL — direction veering with altitude ===
	// Wind veers (turns clockwise in NH) with altitude through the BL.
	// Total veering: ~20° from surface to BL top.
	EkmanShift = EkmanDirectionShift(AltM, BoundaryLayerHeightM);

	// === 9. SEA BREEZE (coastal) ===
	// Westerly, RI is coastal. Sea breeze adds onshore flow during the day.
	// Water temperature: ocean has high heat capacity, changes slowly.
	// At night, water stays warmer than air (air cools faster).
	bool bIsNightWind = (TimeH < 6.0f || TimeH > 18.0f);
	float WaterTempC = FMath::Lerp(12.0f, 20.0f, FMath::Clamp((TempC - 10.0f) / 25.0f, 0.0f, 1.0f));
	if (bIsNightWind) WaterTempC = FMath::Max(WaterTempC, TempC + 3.0f);  // Water warmer at night
	SeaBreezeMs = ComputeSeaBreeze(TempC, TimeH, WaterTempC);
	// Sea breeze only affects the lower layers (100-500m depth)
	float SeaBreezeDepth = ComputeSeaBreezeDepth(TempC - WaterTempC);
	if (AltM > SeaBreezeDepth)
		SeaBreezeMs *= FMath::Clamp(1.0f - (AltM - SeaBreezeDepth) / 200.0f, 0.0f, 1.0f);
	// Add sea breeze to the profiled wind (positive = sea breeze, negative = land breeze)
	// CurrentWindSpeed is clamped to non-negative below, so land breeze just reduces wind
	ProfiledWind += SeaBreezeMs;

	// === 10. DRYDEN TURBULENCE (MIL-HDBK-1797B) ===
	// Scale lengths are altitude-dependent per MIL-HDBK-1797B.
	FDrydenScales Scales = ComputeDrydenScales(AltM);
	float Lu = Scales.Lu;
	float Lv = Scales.Lv;
	float Lw = Scales.Lw;

	// Turbulence intensity from stability, wind, and friction velocity
	FTurbulenceIntensity Sigma = ComputeTurbulenceIntensity(
		BaseWind, AltM, CurrentStability, FrictionVelocityUStar, GustAmplitude, BoundaryLayerHeightM);
	TurbSigmaU = Sigma.SigmaU;
	TurbSigmaV = Sigma.SigmaV;
	TurbSigmaW = Sigma.SigmaW;
	TurbIntensity = TurbSigmaU;  // For backward compat

	// Filter speed: true airspeed (how fast we traverse eddies)
	float PilotSpeedMs = 0.0f;
	if (Pawn)
		PilotSpeedMs = Pawn->GetVelocity().Size() / CM_PER_M;
	float TrueAirspeedMs = FMath::Sqrt(BaseWind * BaseWind + PilotSpeedMs * PilotSpeedMs);
	float Vfilter = FMath::Max(2.0f, TrueAirspeedMs);

	// White noise (Gaussian via CLT: 12 uniform samples → mean 0, variance 1)
	auto WhiteNoise = []() -> float {
		float W = 0.0f;
		for (int32 i = 0; i < 12; ++i) W += FMath::FRand();
		return W - 6.0f;
	};

	// --- Longitudinal: 1st-order filter ---
	// H_u(s) = σ_u * √(2L_u/(πV)) * 1/(1 + L_u*s/V)
	float TauU = Lu / Vfilter;
	float ExpU = FMath::Exp(-DeltaTime / FMath::Max(0.01f, TauU));
	float GainU = TurbSigmaU * FMath::Sqrt(2.0f / FMath::Max(0.01f, TauU)) * FMath::Sqrt(DeltaTime);
	TurbLongState = TurbLongState * ExpU + GainU * WhiteNoise();

	// --- Lateral: 2nd-order filter (MIL-HDBK-1797B) ---
	// H_v(s) = σ_v * √(2L_v/(πV)) * (1 + 2√3 * L_v/V * s) / (1 + 2L_v/V * s)²
	// Implementation: two cascaded 1st-order filters (denominator), with
	// the lead term approximated by feeding the input through both stages.
	// The cascaded filter gives the correct 2nd-order low-pass behavior.
	// The lead term (numerator) shapes high-frequency response but doesn't
	// change the DC gain, so we omit it for stability and correct variance.
	float TauV = Lv / Vfilter;
	float ExpV = FMath::Exp(-DeltaTime / FMath::Max(0.01f, TauV));
	float GainV = TurbSigmaV * FMath::Sqrt(2.0f / FMath::Max(0.01f, TauV)) * FMath::Sqrt(DeltaTime);
	// Stage 1: filter white noise
	TurbLatState2 = TurbLatState2 * ExpV + GainV * WhiteNoise();
	// Stage 2: filter stage 1 output (cascaded 1st-order = 2nd-order low-pass)
	TurbLatState = TurbLatState * ExpV + TurbLatState2 * (1.0f - ExpV);

	// --- Vertical: 2nd-order filter (same form as lateral) ---
	float TauW = Lw / Vfilter;
	float ExpW = FMath::Exp(-DeltaTime / FMath::Max(0.01f, TauW));
	float GainW = TurbSigmaW * FMath::Sqrt(2.0f / FMath::Max(0.01f, TauW)) * FMath::Sqrt(DeltaTime);
	TurbVertState2 = TurbVertState2 * ExpW + GainW * WhiteNoise();
	TurbVertState = TurbVertState * ExpW + TurbVertState2 * (1.0f - ExpW);

	// === 11. COMBINE TURBULENCE INTO WIND ===
	// Longitudinal turbulence modulates wind speed
	WindTurbulence = TurbLongState;

	// Lateral turbulence shifts wind direction (physically: crosswind gusts)
	// Clamp to ±20° to avoid unrealistic swings at low wind
	WindDirShift = FMath::Clamp(TurbLatState / FMath::Max(2.0f, BaseWind), -0.35f, 0.35f);

	// Final wind speed = profiled mean + turbulence
	CurrentWindSpeed = FMath::Max(0.0f, ProfiledWind + WindTurbulence);

	// === 12. OVERDEVELOPMENT RISK ===
	bOverdevelopmentRisk = ComputeOverdevelopmentRisk(TempC, Humidity, TimeH, CloudCover);

	// === 13. FLYABILITY ===
	CurrentFlyability = AssessFlyability(BaseWind, GustAmplitude, TempC, Humidity,
		TimeH, CloudCover, CurrentStability);

	// === 14. WIND DIRECTION DRIFT ===
	// Real wind direction slowly shifts over time (random walk).
	// Rate: ~5°/min in unstable conditions, ~1°/min in stable.
	if (BaseWind > 0.5f)
	{
		float DriftRate = (CurrentStability <= EPasquillStability::C_SlightlyUnstable) ? 0.083f : 0.017f;
		float DriftNoise = (FMath::FRand() - 0.5f) * 2.0f * DriftRate * DeltaTime;
		WindDirection = WindDirection.RotateAngleAxis(DriftNoise * RAD_TO_DEG, FVector(0, 0, 1));
		WindDirection.Z = 0.0f;
		WindDirection.Normalize();
	}
}

float AWingActor::ComputeThermalStrengthMultiplier() const
{
	if (!CachedGameInstance) return 0.0f;

	float Temp = CachedGameInstance->Temperature;
	float Clouds = CachedGameInstance->CloudCover;
	float TimeH = CachedGameInstance->TimeOfDay;
	float Humidity = CachedGameInstance->Humidity;

	// Use the real thermal climb rate computation
	float ThermalClimb = ComputeThermalClimbRate(Temp, BoundaryLayerHeightM, Clouds, TimeH, Humidity);

	// Normalize to 0-1 multiplier (5 m/s = full strength)
	return FMath::Clamp(ThermalClimb / 5.0f, 0.0f, 2.0f);
}

void AWingActor::SpawnThermal()
{
	if (!CachedGameInstance) return;

	float Temp = CachedGameInstance->Temperature;
	float Humidity = CachedGameInstance->Humidity;
	float Clouds = CachedGameInstance->CloudCover;
	float TimeH = CachedGameInstance->TimeOfDay;

	// Check if thermals can trigger at all
	if (!ThermalsCanTrigger(Temp, TimeH, Clouds)) return;

	// Compute current peak climb rate from real physics
	CurrentThermalClimbRate = ComputeThermalClimbRate(Temp, BoundaryLayerHeightM, Clouds, TimeH, Humidity);
	if (CurrentThermalClimbRate < 0.3f) return;  // Too weak to bother

	// Compute sink between thermals
	ThermalSinkMs = ComputeThermalSink(CurrentThermalClimbRate, TimeH);

	// Spawn thermal near the pilot
	APawn* Pawn = nullptr;
	if (ConnectedParamotor)
		Pawn = Cast<APawn>(ConnectedParamotor->GetAttachParentActor());
	if (!Pawn) return;

	FVector PawnLoc = Pawn->GetActorLocation();

	// Thermal spacing: 1.5-3× BL height (real atmospheric scaling)
	float Spacing = ComputeThermalSpacing(BoundaryLayerHeightM) * CM_PER_M;  // cm

	// Cloud street alignment: when wind > 5 m/s, thermals align in streets
	// parallel to the wind direction. We bias the spawn position along the wind.
	float WindSpeed = CurrentWindSpeed;
	FVector WindDir2D = GetShiftedWindDirection();
	WindDir2D.Z = 0.0f;
	if (WindDir2D.Size2D() < 0.01f) WindDir2D = FVector(1, 0, 0);
	WindDir2D.Normalize();

	float Angle = FMath::FRand() * 360.0f;
	float Dist = Spacing * (0.5f + FMath::FRand() * 0.5f);  // 0.5-1.0× spacing
	FVector Offset(FMath::Cos(FMath::DegreesToRadians(Angle)) * Dist,
		FMath::Sin(FMath::DegreesToRadians(Angle)) * Dist, 0.0f);

	// Cloud street: bias along wind direction if wind is strong enough
	if (WindSpeed > 5.0f)
	{
		float StreetBias = FMath::FRand() * Spacing * 0.8f;
		Offset += WindDir2D * StreetBias;
	}

	FThermal Thermal;
	Thermal.Center = PawnLoc + Offset;
	Thermal.Center.Z = GetCachedGroundZ(Thermal.Center);

	// Thermal radius: stronger thermals are narrower (real relationship)
	// ComputeThermalRadius returns cm directly (1500cm = 15m)
	Thermal.Radius = ComputeThermalRadius(CurrentThermalClimbRate);  // cm

	// Thermal strength: peak climb rate with some random variation
	Thermal.Strength = CurrentThermalClimbRate * (0.7f + FMath::FRand() * 0.3f);

	// Thermal base: starts at ground level
	Thermal.BaseAltitude = 0.0f;

	// Thermal top: minimum of cloudbase and inversion altitude
	// Blue thermals (low humidity, high cloudbase) stop at inversion
	// Cloud thermals stop at cloudbase
	float TopAlt = FMath::Min(CloudbaseAltitudeM, InversionAltitudeM);
	Thermal.TopAltitude = TopAlt * (0.7f + FMath::FRand() * 0.3f);  // 70-100% of top

	// Thermal lifetime: 3-10 minutes (stronger = longer)
	Thermal.MaxLifetime = ComputeThermalLifetime(CurrentThermalClimbRate);
	Thermal.Lifetime = Thermal.MaxLifetime;

	// Thermal drift: thermals drift with the wind at ~80% of wind speed
	// (thermals are buoyant columns that get advected by the mean flow)
	Thermal.DriftVel = CurrentWindSpeed * CM_PER_M * 0.8f;

	Thermal.bActive = true;

	Thermals.Add(Thermal);
	UE_LOG(LogParamotor, Log, TEXT("Thermal spawned — climb=%.1f m/s, radius=%.0f m, top=%.0f m, life=%.0f s, sink=%.1f m/s"),
		Thermal.Strength, Thermal.Radius / CM_PER_M, Thermal.TopAltitude, Thermal.MaxLifetime, ThermalSinkMs);
}

void AWingActor::UpdateThermals(float DeltaTime)
{
	// Cloudbase is already computed in UpdateDynamicWind from dew point.
	// Inversion altitude is also computed there.

	// Spawn new thermals periodically
	ThermalStrengthMultiplier = ComputeThermalStrengthMultiplier();
	if (ThermalStrengthMultiplier > 0.05f)
	{
		// Spawn rate: more thermals in strong conditions
		// Real thermal spacing: 1.5-3× BL height. With BL=1500m, spacing=3750m.
		// A pilot can typically reach 2-3 thermals, so we keep 8-15 active.
		float SpawnInterval = FMath::Lerp(30.0f, 8.0f, ThermalStrengthMultiplier);
		ThermalSpawnTimer += DeltaTime;
		if (ThermalSpawnTimer >= SpawnInterval && Thermals.Num() < 15)
		{
			ThermalSpawnTimer = 0.0f;
			SpawnThermal();
		}
	}

	// Update existing thermals
	FVector WindDir2D = GetShiftedWindDirection();
	WindDir2D.Z = 0.0f;
	if (WindDir2D.Size2D() < 0.01f) WindDir2D = FVector(1, 0, 0);
	WindDir2D.Normalize();

	for (int32 i = Thermals.Num() - 1; i >= 0; i--)
	{
		FThermal& T = Thermals[i];

		// Drift with wind
		T.Center += WindDir2D * T.DriftVel * DeltaTime;

		// Age
		T.Lifetime -= DeltaTime;

		// Remove dead thermals
		if (T.Lifetime <= 0.0f)
		{
			Thermals.RemoveAt(i);
		}
	}
}

float AWingActor::GetThermalLiftAtPosition(FVector WorldPos, float AltitudeM)
{
	float TotalLift = 0.0f;

	for (const FThermal& T : Thermals)
	{
		if (!T.bActive) continue;

		// Check altitude range (thermals exist between base and top)
		if (AltitudeM < T.BaseAltitude || AltitudeM > T.TopAltitude) continue;

		// Distance from thermal center (horizontal only)
		float Dist = FVector::Dist2D(WorldPos, T.Center);

		// Outside thermal radius
		if (Dist > T.Radius) continue;

		// Strength profile: peak at center, cosine falloff to zero at edge
		float RadiusFraction = Dist / T.Radius;
		float CoreFactor = FMath::Cos(RadiusFraction * PI * 0.5f);
		CoreFactor = FMath::Max(0.0f, CoreFactor);

		// Altitude profile: strongest at ~1/3 height (real thermal profile)
		// Thermals accelerate from ground, peak in lower-middle, weaken toward top
		float AltFraction = (AltitudeM - T.BaseAltitude) / (T.TopAltitude - T.BaseAltitude);
		AltFraction = FMath::Clamp(AltFraction, 0.0f, 1.0f);
		// Real profile: peak at ~30% height, gradual decay above
		float AltFactor = FMath::Sin(AltFraction * PI);  // Smooth rise and fall
		// Bias toward lower portion (thermals are strongest low down)
		AltFactor = FMath::Pow(AltFactor, 0.7f);

		// Lifetime strength scale (thermal lifecycle)
		// Phase 1 (0-10%): bubble/trigger — ramp up
		// Phase 2 (10-90%): column/mature — full strength
		// Phase 3 (90-100%): decay/dilution — ramp down
		float AgeFraction = 1.0f - (T.Lifetime / T.MaxLifetime);
		float StrengthScale;
		if (AgeFraction < 0.1f)
			StrengthScale = AgeFraction / 0.1f;  // Ramp up (bubble phase)
		else if (AgeFraction > 0.9f)
			StrengthScale = (1.0f - AgeFraction) / 0.1f;  // Ramp down (decay phase)
		else
			StrengthScale = 1.0f;  // Full strength (column phase)

		TotalLift += T.Strength * CoreFactor * AltFactor * StrengthScale;
	}

	return TotalLift;  // m/s upward
}

float AWingActor::GetThermalSinkAtPosition(FVector WorldPos, float AltitudeM)
{
	// Compensating subsidence: air that rises in thermals must descend elsewhere.
	// The sink rate is proportional to thermal strength and inversely proportional
	// to the area outside thermals (mass conservation).
	//
	// Real values: 1-3 m/s in moderate conditions, 2-5 m/s in strong conditions.
	// Sink is strongest at mid-altitudes and near the edges of thermals.
	//
	// We only apply sink if the pilot is NOT in a thermal (lift < 0.5 m/s).
	float Lift = GetThermalLiftAtPosition(WorldPos, AltitudeM);
	if (Lift > 0.5f) return 0.0f;  // In a thermal — no sink

	// Only apply sink within the boundary layer (thermals don't extend above BL)
	if (AltitudeM > BoundaryLayerHeightM) return 0.0f;

	// Sink rate from current conditions
	if (ThermalSinkMs < 0.1f) return 0.0f;

	// Altitude profile: sink is weakest near ground, strongest at mid-BL
	float AltFraction = AltitudeM / FMath::Max(100.0f, BoundaryLayerHeightM);
	AltFraction = FMath::Clamp(AltFraction, 0.0f, 1.0f);
	float AltFactor = FMath::Sin(AltFraction * PI);  // Peak at mid-BL

	return ThermalSinkMs * AltFactor;
}
