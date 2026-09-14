// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/OverlaySlot.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "GameFramework/GameUserSettings.h"

/**
 * Shared low-level widget helpers for the menu screens — the longer-arg
 * variants of the UIStyle kit (outlined text, panel button style, fill slot).
 * Include + `using namespace MenuUtil;` in each UW_FreeFlightMenu TU.
 */
namespace MenuUtil
{
	static UTextBlock* MakeText(UWidgetTree* Tree, const TCHAR* Str, const FLinearColor& Color,
		int32 FontSize, int32 LetterSpacing = 0, int32 OutlineSize = 0, bool bShadow = false,
		float ShadowOpacity = 0.5f)
	{
		UTextBlock* Text = Tree->ConstructWidget<UTextBlock>();
		Text->SetText(FText::FromString(Str));
		Text->SetJustification(ETextJustify::Center);
		Text->SetColorAndOpacity(FSlateColor(Color));

		if (bShadow)
		{
			Text->SetShadowOffset(FVector2D(1.0f, 1.0f));
			Text->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, ShadowOpacity));
		}

		FSlateFontInfo Font = Text->GetFont();
		Font.Size = (float)FontSize;
		Font.LetterSpacing = LetterSpacing;
		Font.OutlineSettings.OutlineSize = OutlineSize;
		Font.OutlineSettings.OutlineColor = FLinearColor(0.0f, 0.0f, 0.0f, 1.0f);
		Text->SetFont(Font);
		return Text;
	}

	static FButtonStyle MakePanelStyle(const FLinearColor& Normal, const FLinearColor& Hovered,
		const FLinearColor& Pressed)
	{
		FButtonStyle Style;
		Style.Normal.TintColor = FSlateColor(Normal);
		Style.Hovered.TintColor = FSlateColor(Hovered);
		Style.Pressed.TintColor = FSlateColor(Pressed);
		Style.NormalPadding = FMargin(0.0f);
		Style.PressedPadding = FMargin(0.0f);
		return Style;
	}

	static void FillSlot(UOverlaySlot* Slot)
	{
		if (Slot)
		{
			Slot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
			Slot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
		}
	}

	// ---- GameUserSettings apply/read helpers — shared by the free-flight
	// settings tab and the pause-menu options tab. ----

	inline UGameUserSettings* UserSettings()
	{
		return GEngine ? GEngine->GetGameUserSettings() : nullptr;
	}

	inline FString QualityName(int32 Level)
	{
		static const TCHAR* Names[] = {TEXT("LOW"), TEXT("MEDIUM"), TEXT("HIGH"), TEXT("EPIC"), TEXT("CINEMATIC")};
		return (Level >= 0 && Level <= 4) ? FString(Names[Level]) : FString(TEXT("CUSTOM"));
	}

	inline int32 CurrentQualityLevel()
	{
		UGameUserSettings* GUS = UserSettings();
		return GUS ? GUS->GetOverallScalabilityLevel() : -1;
	}

	inline void ApplyQualityLevel(int32 Level)
	{
		if (UGameUserSettings* GUS = UserSettings())
		{
			GUS->SetOverallScalabilityLevel(Level);
			GUS->ApplySettings(true);
		}
	}

	inline void ApplyVSync(bool bOn)
	{
		if (UGameUserSettings* GUS = UserSettings())
		{
			GUS->SetVSyncEnabled(bOn);
			GUS->ApplySettings(true);
		}
	}

	inline void ApplyShadows(bool bOn)
	{
		if (UGameUserSettings* GUS = UserSettings())
		{
			GUS->SetShadowQuality(bOn ? 2 : 0);
			GUS->ApplySettings(true);
		}
	}

	inline void ApplyResolutionScale(float Normalized)
	{
		if (UGameUserSettings* GUS = UserSettings())
		{
			GUS->SetResolutionScaleNormalized(Normalized);
			GUS->ApplySettings(true);
		}
	}
}
