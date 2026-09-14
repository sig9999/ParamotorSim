// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

// ============================================================
// AtmosphereModel.h — Real atmospheric physics for wind, thermals,
// turbulence, and boundary layer modeling.
//
// All formulas from:
// - MIL-HDBK-1797B (Dryden turbulence, scale lengths)
// - Garratt, "The Atmospheric Boundary Layer" (Monin-Obukhov, log law)
// - Businger-Dyer-Paulson stability corrections
// - Holton, "An Introduction to Dynamic Meteorology" (Ekman spiral)
// - Magnus formula for dew point (over water)
// - RASP / paraglaring meteorology (thermal trigger, cloudbase)
// - WMO gust factor definitions
// - Coastal meteorology literature (sea breeze)
//
// All functions are inline (header-only) for performance.
// ============================================================

#include "CoreMinimal.h"

// ============================================================
// PHYSICAL CONSTANTS
// ============================================================
namespace AtmosConst
{
	// Von Karman constant
	static constexpr float KAPPA = 0.41f;

	// Earth rotation rate (rad/s)
	static constexpr float OMEGA_EARTH = 7.2921159e-5f;

	// Westerly, RI latitude (degrees and radians)
	static constexpr float LATITUDE_DEG = 41.3f;
	static constexpr float LATITUDE_RAD = 0.7205f;  // 41.3 * PI/180

	// Coriolis parameter for Westerly, RI: f = 2 * Omega * sin(lat)
	static constexpr float CORIOLIS_F = 2.0f * OMEGA_EARTH * 0.6566f;  // ≈ 9.58e-5 s⁻¹

	// Gravity (m/s²)
	static constexpr float G = 9.80665f;

	// Dry adiabatic lapse rate (°C/m)
	static constexpr float DALR = 0.0098f;

	// Moist adiabatic lapse rate (°C/m) — approximate, temp-dependent
	static constexpr float MALR = 0.006f;

	// Reference height for wind measurements (m)
	static constexpr float REF_HEIGHT_M = 10.0f;

	// von Karman turbulence constant
	static constexpr float VK_CONST = 1.339f;

	// Peak gust factor (3-s gust over 10-min mean)
	static constexpr float PEAK_GUST_FACTOR = 3.5f;

	// Roughness lengths (m) for different terrain types
	static constexpr float Z0_WATER = 0.0001f;      // Calm water
	static constexpr float Z0_SAND = 0.003f;        // Sand/snow
	static constexpr float Z0_GRASS = 0.03f;        // Short grass (coastal field)
	static constexpr float Z0_CROPS = 0.1f;         // Tall grass/crops
	static constexpr float Z0_BRUSH = 0.5f;         // Brush/open forest
	static constexpr float Z0_SUBURBAN = 0.3f;      // Suburban
	static constexpr float Z0_URBAN = 2.0f;         // Dense urban

	// Default roughness for coastal field (Westerly, RI)
	static constexpr float Z0_DEFAULT = Z0_GRASS;

	// Eddy viscosity for Ekman spiral (m²/s) — typical ABL value
	static constexpr float EDDY_VISCOSITY = 5.0f;  // K_M
}

// ============================================================
// PASQUILL STABILITY CLASSES
// ============================================================
enum class EPasquillStability : uint8
{
	A_VeryUnstable,
	B_ModeratelyUnstable,
	C_SlightlyUnstable,
	D_Neutral,
	E_SlightlyStable,
	F_VeryStable,
};

// Power law exponent α for each Pasquill class (rural terrain)
inline float PasquillToAlpha(EPasquillStability S)
{
	switch (S)
	{
	case EPasquillStability::A_VeryUnstable:   return 0.07f;
	case EPasquillStability::B_ModeratelyUnstable: return 0.07f;
	case EPasquillStability::C_SlightlyUnstable:   return 0.10f;
	case EPasquillStability::D_Neutral:            return 0.15f;
	case EPasquillStability::E_SlightlyStable:     return 0.35f;
	case EPasquillStability::F_VeryStable:         return 0.55f;
	default: return 0.15f;
	}
}

// Determine Pasquill stability class from weather conditions.
// Based on solar radiation (from time of day + cloud cover) and wind speed.
// This is the standard Pasquill-Turner classification.
inline EPasquillStability ComputePasquillClass(float TempC, float WindMs, float TimeH, float CloudCover)
{
	// Nighttime (before sunrise ~6:00 or after sunset ~18:00)
	bool bIsNight = (TimeH < 6.0f || TimeH > 18.0f);

	if (bIsNight)
	{
		// Night: stable classes E/F based on wind speed and cloud cover
		// More clouds = less stable (radiative cooling reduced)
		float CloudFactor = 1.0f - CloudCover * 0.5f;  // Overcast reduces stability
		if (WindMs < 2.0f * CloudFactor)
			return EPasquillStability::F_VeryStable;
		else if (WindMs < 4.0f * CloudFactor)
			return EPasquillStability::E_SlightlyStable;
		else
			return EPasquillStability::D_Neutral;  // Windy night = neutral
	}

	// Daytime: unstable classes A/B/C based on solar insolation and wind
	// Solar insolation depends on time of day and cloud cover
	float SunAngle = 0.0f;
	if (TimeH >= 6.0f && TimeH <= 18.0f)
	{
		// Solar elevation angle approximation (peak at noon)
		float HourAngle = (TimeH - 12.0f) * 15.0f;  // degrees
		SunAngle = FMath::Cos(FMath::DegreesToRadians(HourAngle));  // 1 at noon, 0 at 6/18h
		SunAngle = FMath::Max(0.0f, SunAngle);
	}

	// Solar insolation reduced by cloud cover
	float Insolation = SunAngle * (1.0f - CloudCover * 0.7f);

	// Strong insolation (>0.6): classes A/B
	// Moderate insolation (0.3-0.6): classes B/C
	// Weak insolation (<0.3): classes C/D
	if (Insolation > 0.6f)
	{
		if (WindMs < 2.0f) return EPasquillStability::A_VeryUnstable;
		if (WindMs < 3.0f) return EPasquillStability::A_VeryUnstable;
		if (WindMs < 5.0f) return EPasquillStability::B_ModeratelyUnstable;
		return EPasquillStability::C_SlightlyUnstable;
	}
	else if (Insolation > 0.3f)
	{
		if (WindMs < 2.0f) return EPasquillStability::A_VeryUnstable;
		if (WindMs < 3.0f) return EPasquillStability::B_ModeratelyUnstable;
		if (WindMs < 5.0f) return EPasquillStability::B_ModeratelyUnstable;
		if (WindMs < 6.0f) return EPasquillStability::C_SlightlyUnstable;
		return EPasquillStability::D_Neutral;
	}
	else
	{
		// Weak insolation (early morning / late afternoon)
		if (WindMs < 2.0f) return EPasquillStability::B_ModeratelyUnstable;
		if (WindMs < 3.0f) return EPasquillStability::C_SlightlyUnstable;
		if (WindMs < 5.0f) return EPasquillStability::C_SlightlyUnstable;
		return EPasquillStability::D_Neutral;
	}
}

// ============================================================
// BOUNDARY LAYER HEIGHT
// ============================================================
// BL height depends on stability and time of day.
// Convective (unstable): 1-3 km, grows from ~100m after sunrise.
// Stable (night): 100-400 m.
// Neutral: 500-1000 m.
inline float ComputeBoundaryLayerHeight(EPasquillStability Stability, float TimeH)
{
	bool bIsNight = (TimeH < 6.0f || TimeH > 18.0f);

	if (bIsNight)
	{
		// Stable nocturnal BL: 100-400m, deeper with more wind
		switch (Stability)
		{
		case EPasquillStability::F_VeryStable:     return 150.0f;
		case EPasquillStability::E_SlightlyStable: return 300.0f;
		default: return 400.0f;
		}
	}

	// Daytime: BL grows from sunrise (~6:00) to peak (~14:00-15:00), then collapses
	// Growth rate: ~100-500 m/hr after sunrise
	float HoursSinceSunrise = FMath::Max(0.0f, TimeH - 6.0f);
	float GrowthFraction = FMath::Clamp(HoursSinceSunrise / 8.0f, 0.0f, 1.0f);
	// Cosine curve: slow growth, peak at ~14:00
	float DiurnalCurve = FMath::Sin(GrowthFraction * PI * 0.5f);  // 0 at sunrise, 1 at ~14:00

	float MaxBL = 0.0f;
	switch (Stability)
	{
	case EPasquillStability::A_VeryUnstable:       MaxBL = 3000.0f; break;
	case EPasquillStability::B_ModeratelyUnstable: MaxBL = 2000.0f; break;
	case EPasquillStability::C_SlightlyUnstable:   MaxBL = 1500.0f; break;
	case EPasquillStability::D_Neutral:            MaxBL = 1000.0f; break;
	default:                                       MaxBL = 800.0f;  break;
	}

	// Start from nocturnal residual layer (~200m) and grow
	return FMath::Lerp(200.0f, MaxBL, DiurnalCurve);
}

// ============================================================
// MONIN-OBUKHOV STABILITY CORRECTION (Businger-Dyer-Paulson)
// ============================================================
// ψm(ζ) for momentum
// Unstable (ζ < 0): ψm = 2ln((1+x)/2) + ln((1+x²)/2) - 2arctan(x) + π/2, x = (1-16ζ)^(1/4)
// Stable (0 ≤ ζ < 1): ψm = -5ζ
inline float PsiM(float Zeta)
{
	if (Zeta < 0.0f)  // Unstable
	{
		float X = FMath::Pow(FMath::Max(0.001f, 1.0f - 16.0f * Zeta), 0.25f);
		return 2.0f * FMath::Loge((1.0f + X) * 0.5f)
			+ FMath::Loge((1.0f + X * X) * 0.5f)
			- 2.0f * FMath::Atan(X)
			+ PI * 0.5f;
	}
	else  // Stable
	{
		return -5.0f * FMath::Min(Zeta, 1.0f);
	}
}

// ============================================================
// LOG LAW WIND PROFILE WITH STABILITY CORRECTION
// ============================================================
// U(z) = (u*/κ) * [ln((z-d)/z0) - ψm((z-d)/L) + ψm(z0/L)]
// For simplicity, d=0 (short grass), L computed from stability.
//
// We compute u* from the reference wind at 10m:
// u* = κ * U_ref / [ln(z_ref/z0) - ψm(z_ref/L)]
inline float ComputeFrictionVelocity(float UrefMs, float Z0, float ZetaRef)
{
	float Denom = FMath::Loge(AtmosConst::REF_HEIGHT_M / Z0) - PsiM(ZetaRef);
	if (Denom < 0.1f) Denom = 0.1f;
	return AtmosConst::KAPPA * UrefMs / Denom;
}

// Wind speed at height z using log law with stability correction
inline float LogWindProfile(float Z, float Ustar, float Z0, float ZetaZ, float ZetaZ0)
{
	if (Z < Z0) return 0.0f;
	float LnTerm = FMath::Loge(Z / Z0);
	float PsiZ = PsiM(ZetaZ);
	float PsiZ0 = PsiM(ZetaZ0);
	// Clamp to non-negative — in very unstable conditions the correction
	// can make the bracket negative at low altitudes
	return FMath::Max(0.0f, (Ustar / AtmosConst::KAPPA) * (LnTerm - PsiZ + PsiZ0));
}

// Estimate Monin-Obukhov length from Pasquill class
// L < 0 = unstable, L > 0 = stable, L = ∞ = neutral
inline float EstimateMoninObukhovLength(EPasquillStability S)
{
	switch (S)
	{
	case EPasquillStability::A_VeryUnstable:       return -10.0f;    // Very unstable
	case EPasquillStability::B_ModeratelyUnstable: return -20.0f;
	case EPasquillStability::C_SlightlyUnstable:   return -50.0f;
	case EPasquillStability::D_Neutral:            return 1e6f;      // Effectively infinite
	case EPasquillStability::E_SlightlyStable:     return 50.0f;
	case EPasquillStability::F_VeryStable:         return 10.0f;
	default: return 1e6f;
	}
}

// ============================================================
// EKMAN SPIRAL — wind direction veering with altitude
// ============================================================
// In the Northern Hemisphere, wind veers (turns clockwise) with height.
// Total veering in the boundary layer: 10-30° (typical 15-20°).
// The Ekman spiral gives the angle as a function of altitude:
//   θ(z) = arctan(V/U) where U,V are Ekman components
// Above the boundary layer, wind is geostrophic (no more turning).

// Returns the direction shift (radians) at altitude z relative to surface.
// Positive = veering (clockwise in NH).
inline float EkmanDirectionShift(float AltM, float BLHeightM)
{
	if (AltM <= 0.0f || BLHeightM <= 0.0f) return 0.0f;

	// Normalize altitude to boundary layer height
	float Znorm = AltM / BLHeightM;
	if (Znorm >= 1.0f) return FMath::DegreesToRadians(20.0f);  // Full veering at BL top

	// Ekman spiral angle: θ(z) = arctan(e^(-βz) * sin(βz) / (1 - e^(-βz) * cos(βz)))
	// Simplified: use a smooth interpolation from 0 to 20° over the BL
	// The Ekman spiral gives ~45° at the surface for laminar flow, but
	// real turbulent ABL gives 10-30° total. We use 20° as typical.
	// The veering follows roughly a sine curve through the BL.
	return FMath::Sin(Znorm * PI * 0.5f) * FMath::DegreesToRadians(20.0f);
}

// ============================================================
// DRYDEN TURBULENCE SCALE LENGTHS (MIL-HDBK-1797B)
// ============================================================
// Altitude h in feet for the low-altitude formulas.
// Low (h < 1000 ft): 2Lw = h, Lu = 2Lv = h * (0.177 + 0.000823*h)^1.2
// High (h > 2000 ft): Lu = 1750 ft, Lv = Lw = 875 ft
// Between: linear interpolation

struct FDrydenScales
{
	float Lu;  // Longitudinal scale length (m)
	float Lv;  // Lateral scale length (m)
	float Lw;  // Vertical scale length (m)
};

inline FDrydenScales ComputeDrydenScales(float AltM)
{
	float AltFt = AltM * 3.28084f;  // m to ft

	if (AltFt < 1000.0f)
	{
		// Low altitude: Lw = h/2, Lu = Lv = h * (0.177 + 0.000823*h)^1.2 / 2
		// Actually MIL-HDBK says: 2Lw = h → Lw = h/2
		// Lu = 2Lv → Lv = Lu/2
		// Lu = h * (0.177 + 0.000823*h)^1.2
		float Lw_ft = AltFt * 0.5f;
		float Lu_ft = AltFt * FMath::Pow(0.177f + 0.000823f * AltFt, 1.2f);
		float Lv_ft = Lu_ft * 0.5f;
		return { Lu_ft * 0.3048f, Lv_ft * 0.3048f, Lw_ft * 0.3048f };  // ft to m
	}
	else if (AltFt > 2000.0f)
	{
		// High altitude: Lu = 1750 ft, Lv = Lw = 875 ft
		return { 1750.0f * 0.3048f, 875.0f * 0.3048f, 875.0f * 0.3048f };
	}
	else
	{
		// Interpolate between 1000 and 2000 ft
		float t = (AltFt - 1000.0f) / 1000.0f;
		float Lw_low = 500.0f;  // 1000/2
		float Lu_low = 1000.0f * FMath::Pow(0.177f + 0.823f, 1.2f);
		float Lv_low = Lu_low * 0.5f;
		float Lw_high = 875.0f;
		float Lu_high = 1750.0f;
		float Lv_high = 875.0f;
		float Lw_ft = FMath::Lerp(Lw_low, Lw_high, t);
		float Lu_ft = FMath::Lerp(Lu_low, Lu_high, t);
		float Lv_ft = FMath::Lerp(Lv_low, Lv_high, t);
		return { Lu_ft * 0.3048f, Lv_ft * 0.3048f, Lw_ft * 0.3048f };
	}
}

// ============================================================
// TURBULENCE INTENSITY
// ============================================================
// σ depends on stability, wind speed, and altitude.
// Surface layer: σu/u* ≈ 2.4, σw/u* ≈ 1.25 (neutral)
// Low altitude Dryden: σw = 0.1 * U_20ft
// Unstable conditions: higher σ (σu/U ~ 0.1-0.3)
// Stable conditions: lower σ

struct FTurbulenceIntensity
{
	float SigmaU;  // Longitudinal σ (m/s)
	float SigmaV;  // Lateral σ (m/s)
	float SigmaW;  // Vertical σ (m/s)
};

inline FTurbulenceIntensity ComputeTurbulenceIntensity(
	float WindMs, float AltM, EPasquillStability Stability, float Ustar, float GustAmp, float BLHeight)
{
	// Base turbulence from friction velocity (surface layer theory)
	float SigmaU_base = 2.4f * Ustar;
	float SigmaW_base = 1.25f * Ustar;

	// Stability multiplier
	float StabilityMult = 1.0f;
	switch (Stability)
	{
	case EPasquillStability::A_VeryUnstable:       StabilityMult = 2.0f; break;
	case EPasquillStability::B_ModeratelyUnstable: StabilityMult = 1.5f; break;
	case EPasquillStability::C_SlightlyUnstable:   StabilityMult = 1.2f; break;
	case EPasquillStability::D_Neutral:            StabilityMult = 1.0f; break;
	case EPasquillStability::E_SlightlyStable:     StabilityMult = 0.5f; break;
	case EPasquillStability::F_VeryStable:         StabilityMult = 0.3f; break;
	}

	// Gust amplitude contributes to turbulence intensity directly
	// (gust amplitude IS the turbulence σ by definition)
	float GustSigma = GustAmp;

	// Combine: use the larger of friction-velocity-based and gust-based
	float SigmaU = FMath::Max(SigmaU_base * StabilityMult, GustSigma);
	float SigmaW = FMath::Max(SigmaW_base * StabilityMult, GustSigma * 0.5f);
	float SigmaV = SigmaW;  // Lateral ≈ vertical for most conditions

	// At high altitude (above BL), turbulence decreases (free atmosphere is smooth)
	if (AltM > BLHeight)
	{
		float AboveBL = FMath::Clamp((AltM - BLHeight) / 1000.0f, 0.0f, 1.0f);
		float Decay = 1.0f - AboveBL * 0.8f;  // 80% reduction well above BL
		SigmaU *= Decay;
		SigmaV *= Decay;
		SigmaW *= Decay;
	}

	// Mechanical turbulence near ground (within 2x obstacle height)
	if (AltM < 50.0f)
	{
		float MechBoost = 1.0f + (1.0f - AltM / 50.0f) * 0.5f;
		SigmaU *= MechBoost;
		SigmaV *= MechBoost;
		SigmaW *= MechBoost;
	}

	return { SigmaU, SigmaV, SigmaW };
}

// ============================================================
// DEW POINT (Magnus formula over water)
// ============================================================
// Td = 237.3 * γ / (17.27 - γ)
// γ = 17.27 * T / (237.3 + T) + ln(RH/100)
inline float ComputeDewPoint(float TempC, float HumidityPct)
{
	float RH = FMath::Clamp(HumidityPct, 1.0f, 100.0f);
	float Gamma = (17.27f * TempC) / (237.3f + TempC) + FMath::Loge(RH / 100.0f);
	return (237.3f * Gamma) / (17.27f - Gamma);
}

// ============================================================
// CLOUDBASE (Lifting Condensation Level)
// ============================================================
// Cloudbase (m AGL) ≈ 125 × (T - Td)
inline float ComputeCloudbase(float TempC, float DewPointC)
{
	return FMath::Max(0.0f, 125.0f * (TempC - DewPointC));
}

// ============================================================
// THERMAL PHYSICS
// ============================================================

// Thermal trigger: surface temp must exceed trigger temp.
// Trigger temp ≈ puddle temp + 8-12°C. Simplified: thermals start when
// the surface temp is high enough relative to the airmass.
// We use: thermals trigger when T > 8°C AND sun angle is sufficient.
inline bool ThermalsCanTrigger(float TempC, float TimeH, float CloudCover)
{
	// Need daytime sun
	if (TimeH < 7.0f || TimeH > 17.0f) return false;
	// Need enough solar heating (reduced by clouds)
	float SunStrength = FMath::Cos((TimeH - 13.0f) * 15.0f * PI / 180.0f);
	SunStrength = FMath::Max(0.0f, SunStrength) * (1.0f - CloudCover * 0.7f);
	// Need warm enough surface
	return (TempC > 8.0f && SunStrength > 0.1f);
}

// Thermal climb rate based on conditions
// Weak: 0.5-1.5, Moderate: 1.5-3, Strong: 3-5, Very strong: 5-8, Extreme: 8+
inline float ComputeThermalClimbRate(float TempC, float BLHeight, float CloudCover, float TimeH, float Humidity)
{
	if (!ThermalsCanTrigger(TempC, TimeH, CloudCover)) return 0.0f;

	// Base strength from temperature (heat differential drives thermals)
	float TempFactor = FMath::Clamp((TempC - 10.0f) / 25.0f, 0.0f, 1.5f);

	// BL height factor: deeper BL = stronger thermals
	float BLFactor = FMath::Clamp(BLHeight / 2000.0f, 0.2f, 1.5f);

	// Cloud cover: reduces solar heating but clouds mark thermals
	// Some clouds (0.2-0.5) actually help by marking thermals; too much kills them
	float CloudFactor;
	if (CloudCover < 0.2f)
		CloudFactor = 0.8f;  // Blue thermals: weaker, less organized
	else if (CloudCover < 0.6f)
		CloudFactor = 1.0f;  // Optimal: some clouds mark thermals
	else
		CloudFactor = FMath::Clamp(1.0f - (CloudCover - 0.6f) * 2.0f, 0.1f, 1.0f);  // Overcast kills

	// Time of day: peak at 13:00-15:00, ramp up from 10:00, die by 18:00
	float TimeFactor = 0.0f;
	if (TimeH >= 9.0f && TimeH <= 18.0f)
	{
		float PeakHour = 14.0f;
		float HalfWidth = 4.5f;  // 9:30 to 18:30
		float DistFromPeak = FMath::Abs(TimeH - PeakHour) / HalfWidth;
		TimeFactor = FMath::Cos(DistFromPeak * PI * 0.5f);
		TimeFactor = FMath::Max(0.0f, TimeFactor);
	}

	// Humidity: higher humidity = more latent heat = stronger thermals once they condense
	// But also lower cloudbase, so thermals are shorter
	float HumidityFactor = FMath::Lerp(0.7f, 1.2f, FMath::Clamp(Humidity / 100.0f, 0.0f, 1.0f));

	// Combine: base 5 m/s at full strength
	float ClimbRate = 5.0f * TempFactor * BLFactor * CloudFactor * TimeFactor * HumidityFactor;

	// Overdevelopment: after 15:00 with high temp + high humidity, thermals become dangerous
	// (cloud suck, overdevelopment, strong sink between thermals)
	// We don't reduce climb rate here, but the caller can check for OD risk

	return FMath::Clamp(ClimbRate, 0.0f, 10.0f);
}

// Thermal spacing: 1.5-3 × BL height
inline float ComputeThermalSpacing(float BLHeight)
{
	return BLHeight * 2.5f;  // Middle of range
}

// Thermal radius: stronger thermals are narrower (10-30m), weaker are wider (60-250m)
inline float ComputeThermalRadius(float ClimbRate)
{
	if (ClimbRate > 5.0f) return 1500.0f + FMath::FRand() * 1500.0f;   // 15-30m (strong, narrow)
	if (ClimbRate > 2.0f) return 3000.0f + FMath::FRand() * 3000.0f;   // 30-60m
	return 6000.0f + FMath::FRand() * 19000.0f;                         // 60-250m (weak, wide)
}

// Thermal lifetime: 3-10 minutes typically
inline float ComputeThermalLifetime(float ClimbRate)
{
	// Stronger thermals last longer
	float Base = 180.0f;  // 3 min
	float Bonus = FMath::Clamp(ClimbRate * 60.0f, 0.0f, 420.0f);  // Up to 7 more min
	return Base + Bonus + FMath::FRand() * 120.0f;  // +0-2 min random
}

// Sink between thermals (compensating subsidence)
// Typically 1-3 m/s in moderate conditions, 2-5 m/s in strong conditions
inline float ComputeThermalSink(float ClimbRate, float TimeH)
{
	// Sink is proportional to thermal strength (mass conservation)
	float BaseSink = ClimbRate * 0.3f;

	// Afternoon overdevelopment increases sink
	if (TimeH > 15.0f)
	{
		float ODFactor = FMath::Clamp((TimeH - 15.0f) / 3.0f, 0.0f, 1.0f);
		BaseSink *= (1.0f + ODFactor * 0.5f);
	}

	return FMath::Clamp(BaseSink, 0.5f, 8.0f);
}

// Overdevelopment risk: high temp + high humidity + afternoon = dangerous
inline bool ComputeOverdevelopmentRisk(float TempC, float Humidity, float TimeH, float CloudCover)
{
	if (TimeH < 13.0f || TimeH > 19.0f) return false;
	if (TempC < 25.0f) return false;
	if (Humidity < 50.0f) return false;
	// Risk increases with temp, humidity, and time after 14:00
	float RiskScore = (TempC - 25.0f) / 15.0f + (Humidity - 50.0f) / 50.0f + (TimeH - 13.0f) / 6.0f;
	return RiskScore > 1.0f;
}

// ============================================================
// SEA BREEZE (coastal)
// ============================================================
// Sea breeze forms during daytime when land is warmer than water.
// Typical speed: 3-8 m/s, depth: 100-500m, inland penetration: 10-50km.
// Land breeze at night: 1-3 m/s, reverse direction.

// Returns sea breeze wind speed contribution (m/s, positive = onshore)
inline float ComputeSeaBreeze(float TempC, float TimeH, float WaterTempC)
{
	float TempDiff = TempC - WaterTempC;
	bool bIsDay = (TimeH >= 8.0f && TimeH <= 18.0f);

	if (bIsDay && TempDiff > 2.0f)
	{
		// Sea breeze: peaks in afternoon
		float TimeFactor = FMath::Sin((TimeH - 8.0f) / 10.0f * PI);
		TimeFactor = FMath::Max(0.0f, TimeFactor);
		// Speed scales with temperature difference
		return FMath::Clamp(TempDiff * 0.8f * TimeFactor, 0.0f, 8.0f);
	}
	else if (!bIsDay && TempDiff < -1.0f)
	{
		// Land breeze at night (water warmer than land)
		float NightFactor = 0.0f;
		if (TimeH > 18.0f || TimeH < 6.0f)
		{
			float NightTime = (TimeH > 18.0f) ? (TimeH - 18.0f) : (TimeH + 6.0f);
			NightFactor = FMath::Sin(NightTime / 12.0f * PI);
		}
		return FMath::Clamp(-TempDiff * 0.3f * NightFactor, -3.0f, 0.0f);
	}

	return 0.0f;
}

// Sea breeze depth (m) — typically 100-500m
inline float ComputeSeaBreezeDepth(float TempDiff)
{
	return FMath::Clamp(100.0f + TempDiff * 40.0f, 100.0f, 500.0f);
}

// ============================================================
// GUST FACTOR
// ============================================================
// Peak gust = mean + g_p × σ_u, where g_p ≈ 3.5 for 3-s gusts
inline float ComputePeakGust(float MeanWindMs, float SigmaU)
{
	return MeanWindMs + AtmosConst::PEAK_GUST_FACTOR * SigmaU;
}

// Gust factor ratio (3-s gust / 10-min mean)
inline float ComputeGustFactor(float TerrainZ0)
{
	// Typical: 1.2-1.7, higher for rougher terrain
	return FMath::Clamp(1.2f + TerrainZ0 * 2.0f, 1.2f, 1.7f);
}

// ============================================================
// INVERSION LAYER
// ============================================================
// Inversions cap thermals. Typical inversion: 800-2000m AGL at night,
// subsidence inversion at 2000-4000m during stable conditions.
inline float ComputeInversionAltitude(EPasquillStability Stability, float TimeH)
{
	bool bIsNight = (TimeH < 6.0f || TimeH > 18.0f);
	if (bIsNight)
	{
		// Nocturnal inversion: low, 100-300m
		return 200.0f;
	}
	// Daytime: subsidence inversion higher
	switch (Stability)
	{
	case EPasquillStability::A_VeryUnstable:       return 3500.0f;  // High inversion, thermals go high
	case EPasquillStability::B_ModeratelyUnstable: return 2500.0f;
	case EPasquillStability::C_SlightlyUnstable:   return 2000.0f;
	case EPasquillStability::D_Neutral:            return 1500.0f;
	default:                                       return 800.0f;   // Stable: low inversion caps thermals
	}
}

// ============================================================
// WIND NOISE (for audio scaling)
// ============================================================
// SPL ∝ U^5 (50 dB/decade), so amplitude ∝ U^2.5
// Returns normalized amplitude 0-1 for a given apparent wind speed
inline float ComputeWindNoiseAmplitude(float ApparentWindMs)
{
	// Map 0-30 m/s to 0-1 with U^2.5 scaling
	float Normalized = FMath::Pow(FMath::Clamp(ApparentWindMs / 30.0f, 0.0f, 1.0f), 2.5f);
	return FMath::Clamp(Normalized, 0.0f, 1.0f);
}

// ============================================================
// FLYABILITY ASSESSMENT (for HUD/instructions)
// ============================================================
enum class EFlyability : uint8
{
	Excellent,  // Smooth morning/evening, light wind
	Good,       // Moderate conditions, some thermals
	Challenging, // Strong thermals, turbulence
	Dangerous,  // Overdevelopment, strong wind, rotors
	Unflyable,  // Night, extreme wind, storms
};

inline EFlyability AssessFlyability(float WindMs, float GustMs, float TempC, float Humidity,
	float TimeH, float CloudCover, EPasquillStability Stability)
{
	// Night = unflyable for paramotors (no thermals, low visibility)
	if (TimeH < 6.0f || TimeH > 19.0f)
		return EFlyability::Unflyable;

	// Wind too strong
	if (WindMs > 12.0f || GustMs > 8.0f)
		return EFlyability::Unflyable;

	// Overdevelopment risk
	if (ComputeOverdevelopmentRisk(TempC, Humidity, TimeH, CloudCover))
		return EFlyability::Dangerous;

	// Strong afternoon conditions
	if (TimeH > 13.0f && TimeH < 16.0f && TempC > 28.0f && Stability == EPasquillStability::A_VeryUnstable)
		return EFlyability::Challenging;

	// Strong wind
	if (WindMs > 8.0f || GustMs > 5.0f)
		return EFlyability::Challenging;

	// Morning/evening smooth conditions
	if ((TimeH < 10.0f || TimeH > 16.0f) && WindMs < 5.0f)
		return EFlyability::Excellent;

	// Moderate conditions
	if (WindMs < 6.0f && GustMs < 3.0f)
		return EFlyability::Good;

	return EFlyability::Challenging;
}
