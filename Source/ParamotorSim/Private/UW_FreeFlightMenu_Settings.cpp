// Copyright Epic Games, Inc. All Rights Reserved.

#include "UW_FreeFlightMenu.h"
#include "ParamotorLogging.h"
#include "ParamotorGameInstance.h"
#include "ParamotorSaveGame.h"
#include "UIStyle.h"
#include "MenuWidgetHelpers.h"
#include "Blueprint/WidgetTree.h"
#include "Misc/App.h"
#include "GameFramework/GameUserSettings.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/Slider.h"

using namespace MenuUtil;

// ==== SETTINGS TAB ====
void UUW_FreeFlightMenu::AddSettingsSlider(UCanvasPanel* Panel, const TCHAR* Name, float Min, float Max, float Value, float YPos, bool bPercent, UTextBlock*& OutLabel, USlider*& OutSlider)
{
	FString InitialText = bPercent
		? FString::Printf(TEXT("%s %d%%"), Name, FMath::RoundToInt(Value * 100.0f))
		: FString::Printf(TEXT("%s %.1f"), Name, Value);
	OutLabel = MakeText(WidgetTree, *InitialText, FLinearColor(0.9f, 0.95f, 1.0f, 1.0f), 20, 2, 0, true, 0.5f);
	Panel->AddChild(OutLabel);
	if (UCanvasPanelSlot* LabelSlot = Cast<UCanvasPanelSlot>(OutLabel->Slot))
	{
		LabelSlot->SetAnchors(FAnchors(0.5f, YPos, 0.5f, YPos));
		LabelSlot->SetOffsets(FMargin(-300.0f, -25.0f, 600.0f, 50.0f));
	}

	USlider* Slider = WidgetTree->ConstructWidget<USlider>();
	Slider->SetMinValue(Min);
	Slider->SetMaxValue(Max);
	Slider->SetValue(Value);

	FSliderStyle SliderStyle;
	SliderStyle.NormalBarImage.TintColor = FSlateColor(FLinearColor(0.15f, 0.20f, 0.28f, 1.0f));
	SliderStyle.HoveredBarImage.TintColor = FSlateColor(FLinearColor(0.25f, 0.35f, 0.45f, 1.0f));
	SliderStyle.DisabledBarImage.TintColor = FSlateColor(FLinearColor(0.10f, 0.10f, 0.12f, 1.0f));
	SliderStyle.NormalThumbImage.TintColor = FSlateColor(FLinearColor(1.0f, 1.0f, 1.0f, 1.0f));
	SliderStyle.HoveredThumbImage.TintColor = FSlateColor(FLinearColor(0.0f, 0.8f, 1.0f, 1.0f));
	SliderStyle.DisabledThumbImage.TintColor = FSlateColor(FLinearColor(0.4f, 0.4f, 0.4f, 1.0f));
	Slider->SetWidgetStyle(SliderStyle);

	Panel->AddChild(Slider);
	if (UCanvasPanelSlot* SliderSlot = Cast<UCanvasPanelSlot>(Slider->Slot))
	{
		SliderSlot->SetAnchors(FAnchors(0.5f, YPos + 0.05f, 0.5f, YPos + 0.05f));
		SliderSlot->SetOffsets(FMargin(-300.0f, -15.0f, 600.0f, 30.0f));
	}

	OutSlider = Slider;
}

void UUW_FreeFlightMenu::InitFromCurrentSettings()
{
	UGameUserSettings* GUS = MenuUtil::UserSettings();
	if (GUS)
	{
		const int32 OverallQ = MenuUtil::CurrentQualityLevel();
		if (IsValid(CurrentQualityLabel)) CurrentQualityLabel->SetText(FText::FromString(FString::Printf(TEXT("Current: %s"), *MenuUtil::QualityName(OverallQ))));

		UButton* QBtns[] = {QualityLowBtn, QualityMedBtn, QualityHighBtn, QualityEpicBtn};
		if (OverallQ >= 0 && OverallQ <= 3)
		{
			HighlightQualityButton(QBtns[OverallQ]);
		}
		else
		{
			HighlightQualityButton(nullptr);
		}

		if (VSyncCheckBox)
		{
			VSyncCheckBox->SetCheckedState(GUS->IsVSyncEnabled() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked);
		}

		if (ShadowsCheckBox)
		{
			ShadowsCheckBox->SetCheckedState(GUS->GetShadowQuality() > 0 ? ECheckBoxState::Checked : ECheckBoxState::Unchecked);
		}

		float ResPct = FMath::Clamp(GUS->GetResolutionScaleNormalized(), 0.5f, 1.0f);
		if (ResolutionScaleSlider)
		{
			ResolutionScaleSlider->SetValue(ResPct);
		}
		if (ResolutionScaleLabel)
		{
			ResolutionScaleLabel->SetText(FText::FromString(FString::Printf(TEXT("Resolution Scale %d%%"), FMath::RoundToInt(ResPct * 100.0f))));
		}
	}

	if (IsValid(GameInstanceRef))
	{
		if (MasterVolumeSlider) MasterVolumeSlider->SetValue(GameInstanceRef->MasterVolume);
		if (MasterVolumeLabel)
		{
			MasterVolumeLabel->SetText(FText::FromString(FString::Printf(TEXT("Master Volume %d%%"), FMath::RoundToInt(GameInstanceRef->MasterVolume * 100.0f))));
		}

		if (MouseSensitivitySlider) MouseSensitivitySlider->SetValue(GameInstanceRef->MouseSensitivity);
		if (MouseSensitivityLabel)
		{
			MouseSensitivityLabel->SetText(FText::FromString(FString::Printf(TEXT("Mouse Sensitivity %.1f"), GameInstanceRef->MouseSensitivity)));
		}
	}
}

void UUW_FreeFlightMenu::SetQualityLevel(int32 Level, UButton* ActiveBtn)
{
	MenuUtil::ApplyQualityLevel(Level);
	HighlightQualityButton(ActiveBtn);

	if (IsValid(CurrentQualityLabel))
	{
		CurrentQualityLabel->SetText(FText::FromString(FString::Printf(TEXT("Current: %s"), *MenuUtil::QualityName(Level))));
	}
}

void UUW_FreeFlightMenu::HighlightQualityButton(UButton* ActiveBtn)
{
	auto SetBtn = [](UButton* Btn, const FLinearColor& Tint, const FLinearColor& TextColor)
	{
		if (!Btn) return;
		FButtonStyle Style = Btn->GetStyle();
		Style.Normal.TintColor = FSlateColor(Tint);
		Btn->SetStyle(Style);
		if (UTextBlock* Txt = Cast<UTextBlock>(Btn->GetChildAt(0)))
		{
			Txt->SetColorAndOpacity(FSlateColor(TextColor));
		}
	};

	FLinearColor Normal(0.06f, 0.08f, 0.11f, 0.78f);
	FLinearColor Active(0.08f, 0.15f, 0.28f, 0.95f);
	FLinearColor TextDim(0.7f, 0.8f, 0.9f, 1.0f);
	FLinearColor TextBright(0.0f, 0.8f, 1.0f, 1.0f);

	SetBtn(QualityLowBtn, Normal, TextDim);
	SetBtn(QualityMedBtn, Normal, TextDim);
	SetBtn(QualityHighBtn, Normal, TextDim);
	SetBtn(QualityEpicBtn, Normal, TextDim);

	if (ActiveBtn)
	{
		SetBtn(ActiveBtn, Active, TextBright);
	}
}

void UUW_FreeFlightMenu::OnQualityLowClicked()    { SetQualityLevel(0, QualityLowBtn); }
void UUW_FreeFlightMenu::OnQualityMediumClicked() { SetQualityLevel(1, QualityMedBtn); }
void UUW_FreeFlightMenu::OnQualityHighClicked()   { SetQualityLevel(2, QualityHighBtn); }
void UUW_FreeFlightMenu::OnQualityEpicClicked()   { SetQualityLevel(3, QualityEpicBtn); }

void UUW_FreeFlightMenu::OnVSyncToggled(bool bChecked)
{
	MenuUtil::ApplyVSync(bChecked);
}

void UUW_FreeFlightMenu::OnShadowsToggled(bool bChecked)
{
	MenuUtil::ApplyShadows(bChecked);
}

void UUW_FreeFlightMenu::OnResolutionScaleChanged(float Value)
{
	MenuUtil::ApplyResolutionScale(Value);
	if (ResolutionScaleLabel)
	{
		ResolutionScaleLabel->SetText(FText::FromString(FString::Printf(TEXT("Resolution Scale %d%%"), FMath::RoundToInt(Value * 100.0f))));
	}
}

void UUW_FreeFlightMenu::OnMasterVolumeChanged(float Value)
{
	FApp::SetVolumeMultiplier(Value);
	if (IsValid(GameInstanceRef)) GameInstanceRef->MasterVolume = Value;
	if (MasterVolumeLabel)
	{
		MasterVolumeLabel->SetText(FText::FromString(FString::Printf(TEXT("Master Volume %d%%"), FMath::RoundToInt(Value * 100.0f))));
	}
}

void UUW_FreeFlightMenu::OnMouseSensitivityChanged(float Value)
{
	if (IsValid(GameInstanceRef)) GameInstanceRef->MouseSensitivity = Value;
	if (MouseSensitivityLabel)
	{
		MouseSensitivityLabel->SetText(FText::FromString(FString::Printf(TEXT("Mouse Sensitivity %.1f"), Value)));
	}
}
