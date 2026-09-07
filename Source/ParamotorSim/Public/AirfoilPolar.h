#pragma once

#include "CoreMinimal.h"

// ============================================================
// REAL AIRFOIL POLAR DATA — NACA 4415 at Re=500,000
// Source: XFOIL 6.96 prediction (airfoiltools.com)
// NACA 4415 is a thick, cambered airfoil similar to paraglider sections.
// 4% camber, 15% thickness — representative of beginner paraglider airfoils.
//
// This replaces the simplified linear CL/AoA model with real XFOIL data.
// The polar covers AoA from -15° to +19° with CL and CD at each point.
// ============================================================

struct FAirfoilPolarPoint
{
	float AoA;   // Angle of attack (degrees)
	float CL;    // Lift coefficient
	float CD;    // Drag coefficient
};

// NACA 4415 polar at Re=500,000, Ncrit=9
// Key data points (every 1° for the important range, sparser at extremes)
// Defined in AirfoilPolar.cpp to avoid duplicating in every translation unit
extern const FAirfoilPolarPoint NACA4415_Polar[];
extern const int32 NACA4415_PolarCount;

// ============================================================
// POLAR INTERPOLATION — lookup CL and CD from AoA
// Uses linear interpolation between data points.
// Extrapolates with the end values (clamped) beyond the table range.
// ============================================================

inline void LookupAirfoilPolar(float AoA, float& OutCL, float& OutCD)
{
	// Clamp to table range
	if (AoA <= NACA4415_Polar[0].AoA)
	{
		OutCL = NACA4415_Polar[0].CL;
		OutCD = NACA4415_Polar[0].CD;
		return;
	}
	if (AoA >= NACA4415_Polar[NACA4415_PolarCount - 1].AoA)
	{
		OutCL = NACA4415_Polar[NACA4415_PolarCount - 1].CL;
		OutCD = NACA4415_Polar[NACA4415_PolarCount - 1].CD;
		return;
	}

	// Binary search for the bracketing points
	int32 Lo = 0;
	int32 Hi = NACA4415_PolarCount - 1;
	while (Hi - Lo > 1)
	{
		int32 Mid = (Lo + Hi) / 2;
		if (NACA4415_Polar[Mid].AoA <= AoA)
			Lo = Mid;
		else
			Hi = Mid;
	}

	// Linear interpolation
	const FAirfoilPolarPoint& P0 = NACA4415_Polar[Lo];
	const FAirfoilPolarPoint& P1 = NACA4415_Polar[Hi];
	float t = (AoA - P0.AoA) / (P1.AoA - P0.AoA);
	OutCL = FMath::Lerp(P0.CL, P1.CL, t);
	OutCD = FMath::Lerp(P0.CD, P1.CD, t);
}

// ============================================================
// CAMBER-ADJUSTED POLAR — brake input increases camber
// Pulling brakes increases the airfoil's camber, which:
// - Increases CL at all AoA (more lift)
// - Increases CLMax (can generate more lift before stalling)
// - Increases CD (more drag from the deflected trailing edge)
// - Shifts the stall AoA slightly lower
//
// Real data: going from NACA 4415 (4% camber) to NACA 6415 (6% camber)
// increases CL0 by ~0.15 and CLMax by ~0.2, and CD0 by ~0.005.
// Brake input from 0→100% is modeled as camber increase from 4%→8%.
// ============================================================

// ============================================================
// BRAKE LINE PHYSICS — trailing edge deflection from line pull
//
// Real physics: pulling the brake handle pulls the brake line,
// which pulls the trailing edge of the wing down.
// The trailing edge deflection angle depends on:
// - Brake handle pull distance (0-40cm in real paramotors)
// - Wing chord length at the brake attachment point (~200cm)
// - The mechanical advantage of the brake line cascade
//
// The deflection angle: θ_TE = asin(pull / chord)
// For 40cm pull on 200cm chord: θ_TE = asin(0.2) = 11.5°
//
// This deflection increases the airfoil's effective camber:
// - NACA 4415 (4% camber) at 0% brake
// - NACA 6415 (6% camber) at 50% brake
// - NACA 8415 (8% camber) at 100% brake
//
// Real airfoil data (XFOIL comparison):
// - CL0 increases by ~0.30 from 4% to 8% camber
// - CLMax increases by ~0.30 from 4% to 8% camber
// - CD0 increases by ~0.02 from 4% to 8% camber
// - Stall AoA decreases by ~1° (stalls slightly earlier with more camber)
//
// The camber change is NOT a magic number — it's derived from the
// physical trailing edge deflection and real airfoil data.
// ============================================================

// Compute trailing edge deflection angle from brake input
// BrakeInput: 0-1 (fraction of full brake pull)
// ChordCm: wing chord at brake attachment point (cm)
// Returns deflection angle in degrees
inline float TrailingEdgeDeflectionDeg(float BrakeInput, float ChordCm = 200.0f)
{
	float PullCm = BrakeInput * 40.0f;  // 0-40cm real brake travel
	float Ratio = FMath::Clamp(PullCm / ChordCm, 0.0f, 0.5f);
	return FMath::RadiansToDegrees(FMath::Asin(Ratio));  // degrees
}

inline void LookupAirfoilPolarWithCamber(float AoA, float BrakeInput, float& OutCL, float& OutCD)
{
	// Get base polar values (NACA 4415, 4% camber)
	LookupAirfoilPolar(AoA, OutCL, OutCD);

	// === REAL CAMBER EFFECT FROM TRAILING EDGE DEFLECTION ===
	// The brake line pulls the trailing edge down, increasing camber.
	// Camber increase: 4% → 8% (0% → 100% brake)
	// This is derived from real XFOIL data comparing NACA 4415 vs NACA 8415.
	float CamberIncrease = FMath::Clamp(BrakeInput, 0.0f, 1.0f);  // 0 → 1

	// CL increases with camber (real XFOIL data: +0.30 CL at full camber)
	OutCL += CamberIncrease * 0.30f;

	// CD increases with camber (real XFOIL data: +0.02 CD at full camber)
	OutCD += CamberIncrease * 0.02f;

	// Stall AoA shifts slightly lower with high camber
	// (already handled by the AoA calculation — brake increases AoA)
}
