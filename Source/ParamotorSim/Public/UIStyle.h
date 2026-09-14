// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/BorderSlot.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

/**
 * Shared ParamotorSim UI style — Forza Horizon / MSFS 2020 hybrid.
 * Deep translucent panels, cyan accent, letter-spaced kickers, hairlines.
 * Header-only helpers so every menu builds widgets the same way.
 */
namespace UIStyle
{
	// ---- Palette ------------------------------------------------------
	static const FLinearColor Scrim      (0.004f, 0.006f, 0.010f, 0.60f);
	static const FLinearColor PanelDeep  (0.008f, 0.012f, 0.018f, 0.94f);
	static const FLinearColor Panel      (0.016f, 0.024f, 0.036f, 0.90f);
	static const FLinearColor Card       (0.026f, 0.038f, 0.056f, 0.92f);
	static const FLinearColor CardHover  (0.050f, 0.072f, 0.105f, 0.95f);
	static const FLinearColor Edge       (0.32f, 0.46f, 0.60f, 0.22f);
	static const FLinearColor EdgeBright (0.35f, 0.60f, 0.80f, 0.55f);

	static const FLinearColor Accent     (0.00f, 0.76f, 1.00f, 1.0f);
	static const FLinearColor AccentSoft (0.00f, 0.42f, 0.58f, 0.55f);
	static const FLinearColor Gold       (1.00f, 0.84f, 0.40f, 1.0f);

	static const FLinearColor Text       (0.94f, 0.96f, 0.98f, 1.0f);
	static const FLinearColor TextDim    (0.60f, 0.66f, 0.74f, 1.0f);
	static const FLinearColor TextFaint  (0.40f, 0.46f, 0.54f, 1.0f);

	static const FLinearColor Success    (0.30f, 0.80f, 0.50f, 1.0f);
	static const FLinearColor Warning    (1.00f, 0.64f, 0.20f, 1.0f);
	static const FLinearColor Danger     (0.90f, 0.32f, 0.30f, 1.0f);

	// ---- Factories ----------------------------------------------------
	static UTextBlock* MakeText(UWidgetTree* WT, const TCHAR* Str, const FLinearColor& Color,
		int32 Size, int32 LetterSpacing = 0, bool bShadow = false,
		ETextJustify::Type Justify = ETextJustify::Left)
	{
		UTextBlock* T = WT->ConstructWidget<UTextBlock>();
		T->SetText(FText::FromString(Str));
		T->SetJustification(Justify);
		T->SetColorAndOpacity(FSlateColor(Color));
		if (bShadow)
		{
			T->SetShadowOffset(FVector2D(1.0f, 1.5f));
			T->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.6f));
		}
		FSlateFontInfo Font = T->GetFont();
		Font.Size = (float)Size;
		Font.LetterSpacing = LetterSpacing * 100; // LetterSpacing is in 1/100 em
		T->SetFont(Font);
		return T;
	}

	static UBorder* MakePanel(UWidgetTree* WT, const FLinearColor& Color, float Padding = 0.0f)
	{
		UBorder* B = WT->ConstructWidget<UBorder>();
		B->SetBrushColor(Color);
		B->SetPadding(FMargin(Padding));
		return B;
	}

	static UImage* MakeLine(UWidgetTree* WT, const FLinearColor& Color)
	{
		UImage* Img = WT->ConstructWidget<UImage>();
		Img->SetColorAndOpacity(Color);
		Img->SetVisibility(ESlateVisibility::HitTestInvisible);
		return Img;
	}

	static UCanvasPanelSlot* PlaceOnCanvas(UWidget* W, UCanvasPanel* Canvas,
		const FAnchors& Anchors, const FMargin& Offsets, int32 Z = 0,
		const FVector2D& Alignment = FVector2D(0.0f, 0.0f))
	{
		if (!W || !Canvas) return nullptr;
		UCanvasPanelSlot* Slot = Canvas->AddChildToCanvas(W);
		if (Slot)
		{
			Slot->SetAnchors(Anchors);
			Slot->SetOffsets(Offsets);
			Slot->SetAlignment(Alignment);
			Slot->SetZOrder(Z);
		}
		return Slot;
	}

	static FButtonStyle FlatButtonStyle(const FLinearColor& Normal, const FLinearColor& Hovered,
		const FLinearColor& Pressed, const FLinearColor& Disabled = FLinearColor(0.04f, 0.04f, 0.05f, 0.5f))
	{
		FButtonStyle S;
		S.Normal.TintColor   = FSlateColor(Normal);
		S.Hovered.TintColor  = FSlateColor(Hovered);
		S.Pressed.TintColor  = FSlateColor(Pressed);
		S.Disabled.TintColor = FSlateColor(Disabled);
		S.NormalPadding  = FMargin(0.0f);
		S.PressedPadding = FMargin(0.0f);
		return S;
	}

	/** Ghost text button — transparent until hover. Forza-style menu entries. */
	static UButton* MakeGhostButton(UWidgetTree* WT, const TCHAR* Label, int32 Size,
		const FLinearColor& TextColor, ETextJustify::Type Justify = ETextJustify::Left,
		UTextBlock** OutText = nullptr)
	{
		UButton* Btn = WT->ConstructWidget<UButton>();
		Btn->SetStyle(FlatButtonStyle(
			FLinearColor(0, 0, 0, 0),
			FLinearColor(1, 1, 1, 0.06f),
			FLinearColor(1, 1, 1, 0.12f)));

		UTextBlock* Txt = MakeText(WT, Label, TextColor, Size, 2, true, Justify);
		Btn->AddChild(Txt);
		if (UButtonSlot* BS = Cast<UButtonSlot>(Txt->Slot))
		{
			BS->SetPadding(FMargin(14.0f, 6.0f, 14.0f, 6.0f));
			BS->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
			BS->SetVerticalAlignment(EVerticalAlignment::VAlign_Center);
		}
		if (OutText) *OutText = Txt;
		return Btn;
	}

	/** Solid accent action button (FLY / EQUIP / SAVE). */
	static UButton* MakeActionButton(UWidgetTree* WT, const TCHAR* Label, int32 Size,
		UTextBlock** OutText = nullptr)
	{
		UButton* Btn = WT->ConstructWidget<UButton>();
		Btn->SetStyle(FlatButtonStyle(
			FLinearColor(0.0f, 0.45f, 0.62f, 0.95f),
			FLinearColor(0.0f, 0.60f, 0.82f, 1.0f),
			FLinearColor(0.0f, 0.34f, 0.48f, 1.0f),
			FLinearColor(0.05f, 0.07f, 0.09f, 0.55f)));

		UTextBlock* Txt = MakeText(WT, Label, Text, Size, 3, true, ETextJustify::Center);
		Btn->AddChild(Txt);
		if (UButtonSlot* BS = Cast<UButtonSlot>(Txt->Slot))
		{
			BS->SetPadding(FMargin(0.0f));
			BS->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Center);
			BS->SetVerticalAlignment(EVerticalAlignment::VAlign_Center);
		}
		if (OutText) *OutText = Txt;
		return Btn;
	}

	/** Kicker + title + accent rule header block, anchored inside a canvas. */
	static void PageHeader(UWidgetTree* WT, UCanvasPanel* Canvas, const TCHAR* Kicker,
		const TCHAR* Title, float X, float Y, float Width, int32 Z = 5)
	{
		UTextBlock* K = MakeText(WT, Kicker, Accent, 13, 4, true);
		PlaceOnCanvas(K, Canvas, FAnchors(0.0f, 0.0f), FMargin(X, Y, Width, 20.0f), Z);

		UTextBlock* T = MakeText(WT, Title, Text, 40, 3, true);
		PlaceOnCanvas(T, Canvas, FAnchors(0.0f, 0.0f), FMargin(X - 2.0f, Y + 20.0f, Width, 50.0f), Z);

		UImage* Rule = MakeLine(WT, AccentSoft);
		PlaceOnCanvas(Rule, Canvas, FAnchors(0.0f, 0.0f), FMargin(X + 2.0f, Y + 74.0f, 120.0f, 2.0f), Z);
	}
}
