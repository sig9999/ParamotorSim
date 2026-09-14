// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Flight HUD palette - shared between UW_FlightHUD.cpp (per-tick updates)
// and UW_FlightHUD_Build.cpp (construction).
// ============================================================
// Design constants — consistent palette and sizing
// ============================================================
static const FLinearColor CLR_BG_PANEL   = FLinearColor(0.02f, 0.04f, 0.08f, 0.55f);
static const FLinearColor CLR_BG_HEADER  = FLinearColor(0.05f, 0.10f, 0.20f, 0.70f);
static const FLinearColor CLR_ACCENT     = FLinearColor(0.15f, 0.55f, 0.95f, 1.0f);   // MSFS blue
static const FLinearColor CLR_ACCENT_DIM = FLinearColor(0.10f, 0.35f, 0.60f, 0.80f);
static const FLinearColor CLR_TEXT       = FLinearColor(0.92f, 0.94f, 0.98f, 1.0f);   // near-white
static const FLinearColor CLR_TEXT_DIM   = FLinearColor(0.45f, 0.50f, 0.58f, 1.0f);   // dim gray-blue
static const FLinearColor CLR_TEXT_LABEL = FLinearColor(0.35f, 0.42f, 0.52f, 1.0f);   // label gray
static const FLinearColor CLR_VALUE_GOOD = FLinearColor(0.25f, 0.85f, 0.45f, 1.0f);
static const FLinearColor CLR_VALUE_WARN = FLinearColor(1.0f, 0.75f, 0.25f, 1.0f);
static const FLinearColor CLR_VALUE_BAD  = FLinearColor(1.0f, 0.30f, 0.25f, 1.0f);
static const FLinearColor CLR_HIGHLIGHT  = FLinearColor(1.0f, 0.85f, 0.35f, 1.0f);
