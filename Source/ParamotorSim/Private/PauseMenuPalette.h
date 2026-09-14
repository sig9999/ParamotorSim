// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/Button.h"

// Pause-menu palette — shared between UW_PauseMenu.cpp (handlers) and
// UW_PauseMenu_Builders.cpp (construction). static-const-in-header is fine
// here: each TU gets its own copy of a compile-time constant.
// ============================================================
// COLOR PALETTE — modern bright glass theme
// ============================================================

static const FLinearColor Col_BgDim       = FLinearColor(0.012f, 0.02f, 0.04f, 0.62f);
static const FLinearColor Col_PanelEdge   = FLinearColor(0.16f, 0.22f, 0.32f, 1.0f);
static const FLinearColor Col_Panel       = FLinearColor(0.055f, 0.07f, 0.11f, 0.97f);
static const FLinearColor Col_Header      = FLinearColor(0.075f, 0.095f, 0.15f, 1.0f);
static const FLinearColor Col_Footer      = FLinearColor(0.045f, 0.06f, 0.095f, 1.0f);
static const FLinearColor Col_Separator   = FLinearColor(0.16f, 0.21f, 0.30f, 1.0f);
static const FLinearColor Col_Accent      = FLinearColor(0.1f, 0.72f, 1.0f, 1.0f);
static const FLinearColor Col_AccentSoft  = FLinearColor(0.1f, 0.55f, 0.8f, 0.5f);
static const FLinearColor Col_AccentText  = FLinearColor(0.01f, 0.06f, 0.10f, 1.0f);
static const FLinearColor Col_TextBright  = FLinearColor(0.97f, 0.98f, 1.0f, 1.0f);
static const FLinearColor Col_TextDim     = FLinearColor(0.60f, 0.65f, 0.74f, 1.0f);
static const FLinearColor Col_TextLabel   = FLinearColor(0.72f, 0.77f, 0.86f, 1.0f);
static const FLinearColor Col_BtnNormal   = FLinearColor(0.09f, 0.115f, 0.18f, 0.85f);
static const FLinearColor Col_BtnHover    = FLinearColor(0.14f, 0.19f, 0.29f, 0.95f);
static const FLinearColor Col_BtnPressed  = FLinearColor(0.20f, 0.27f, 0.40f, 1.0f);
static const FLinearColor Col_CardBg      = FLinearColor(0.075f, 0.095f, 0.15f, 0.9f);
static const FLinearColor Col_CardBorder  = FLinearColor(0.18f, 0.24f, 0.34f, 0.9f);
static const FLinearColor Col_Warning     = FLinearColor(1.0f, 0.62f, 0.25f, 1.0f);
static const FLinearColor Col_Danger      = FLinearColor(1.0f, 0.42f, 0.42f, 1.0f);
static const FLinearColor Col_Success     = FLinearColor(0.35f, 0.85f, 0.55f, 1.0f);

// Helper: apply button colors without deprecated WidgetStyle direct access
static void ApplyButtonColors(UButton* Btn, FLinearColor Normal, FLinearColor Hovered, FLinearColor Pressed,
	FMargin NormalPad, FMargin PressedPad)
{
	if (!Btn) return;
	FButtonStyle Style = Btn->GetStyle();
	Style.Normal.TintColor = FSlateColor(Normal);
	Style.Hovered.TintColor = FSlateColor(Hovered);
	Style.Pressed.TintColor = FSlateColor(Pressed);
	Style.NormalPadding = NormalPad;
	Style.PressedPadding = PressedPad;
	Btn->SetStyle(Style);
}
