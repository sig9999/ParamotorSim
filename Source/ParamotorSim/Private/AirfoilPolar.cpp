// ============================================================
// AirfoilPolar.cpp — NACA 4415 polar data definition
// Defined here (not in the header) to avoid duplicating the
// data array in every translation unit that includes AirfoilPolar.h
// ============================================================

#include "AirfoilPolar.h"

// NACA 4415 polar at Re=500,000, Ncrit=9
const FAirfoilPolarPoint NACA4415_Polar[] = {
	// Negative AoA (below zero lift)
	{-15.0f, -0.7828f, 0.0665f},
	{-12.0f, -0.8444f, 0.0276f},
	{-10.0f, -0.6443f, 0.0194f},
	{-8.0f,  -0.3899f, 0.0138f},
	{-6.0f,  -0.1569f, 0.0111f},
	{-5.0f,  -0.0509f, 0.0104f},
	{-4.0f,   0.0518f, 0.0098f},
	{-3.0f,   0.1491f, 0.0094f},
	{-2.0f,   0.2427f, 0.0091f},
	{-1.0f,   0.3342f, 0.0088f},
	// Zero and positive AoA (normal flight range)
	{0.0f,    0.4127f, 0.0082f},   // CL0 = 0.41 at zero AoA
	{1.0f,    0.5376f, 0.0078f},   // Note: XFOIL jump at Re transition
	{2.0f,    0.7343f, 0.0088f},
	{3.0f,    0.8663f, 0.0093f},
	{4.0f,    0.9353f, 0.0098f},
	{5.0f,    1.0049f, 0.0104f},   // Best L/D region (CL/CD ~ 97)
	{6.0f,    1.0667f, 0.0110f},
	{7.0f,    1.1318f, 0.0118f},   // Trim AoA region
	{8.0f,    1.1969f, 0.0129f},
	{9.0f,    1.2562f, 0.0144f},
	{10.0f,   1.3049f, 0.0168f},
	{11.0f,   1.3480f, 0.0198f},   // Note: interpolated
	{12.0f,   1.3888f, 0.0237f},
	{13.0f,   1.4218f, 0.0285f},   // Near CLMax
	{14.0f,   1.4517f, 0.0341f},
	{15.0f,   1.4688f, 0.0417f},   // CLMax ~ 1.47 at 15 deg
	{15.5f,   1.4688f, 0.0466f},   // Stall begins
	{16.0f,   1.4636f, 0.0523f},   // Post-stall: CL drops, CD rises
	{17.0f,   1.4474f, 0.0653f},
	{18.0f,   1.4234f, 0.0801f},
	{19.0f,   1.4011f, 0.0955f},
};

const int32 NACA4415_PolarCount = sizeof(NACA4415_Polar) / sizeof(FAirfoilPolarPoint);
