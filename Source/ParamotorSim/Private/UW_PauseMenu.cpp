// Copyright Epic Games, Inc. All Rights Reserved.

#include "UW_PauseMenu.h"
#include "MenuWidgetHelpers.h"
#include "PauseMenuPalette.h"
#include "ParamotorPlayerController.h"
#include "ParamotorGameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/TextBlock.h"
#include "Components/Border.h"
#include "Components/BorderSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Spacer.h"
#include "Components/Slider.h"
#include "Components/CheckBox.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "GameFramework/GameUserSettings.h"
#include "Engine/GameViewportClient.h"

// ============================================================
// INIT FROM CURRENT SETTINGS
// ============================================================

void UUW_PauseMenu::InitFromCurrentSettings()
{
	UGameUserSettings* GUS = MenuUtil::UserSettings();
	if (!GUS) return;

	const int32 OverallQ = MenuUtil::CurrentQualityLevel();
	if (IsValid(CurrentQualityLabel))
	{
		const bool bPreset = (OverallQ >= 0 && OverallQ <= 3);
		CurrentQualityLabel->SetText(FText::FromString(FString::Printf(TEXT("Current: %s"), *MenuUtil::QualityName(OverallQ))));
		CurrentQualityLabel->SetColorAndOpacity(FSlateColor(bPreset ? Col_Accent : Col_TextDim));
	}

	UButton* QBtns[] = {QualityLowBtn, QualityMedBtn, QualityHighBtn, QualityEpicBtn};
	HighlightQualityButton(OverallQ >= 0 && OverallQ <= 3 ? QBtns[OverallQ] : nullptr);

	if (VSyncCheckBox)
		VSyncCheckBox->SetCheckedState(GUS->IsVSyncEnabled() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked);

	if (ShadowsCheckBox)
		ShadowsCheckBox->SetCheckedState(GUS->GetShadowQuality() > 0 ? ECheckBoxState::Checked : ECheckBoxState::Unchecked);

	// Resolution scale label comes from the live cvar (see BuildResolutionSection)
}

// ============================================================
// NATIVE INIT — slim dispatcher
// ============================================================

void UUW_PauseMenu::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true);

	BuildRootLayout();
	BuildHeader();
	BuildFooter();
	BuildMainTab();
	BuildSpawnTab();
	BuildOptionsTab();
	InitFromCurrentSettings();
	ShowMainTab();

	// Enter/Space resumes immediately — standard pause-menu affordance
	if (ResumeButton)
	{
		if (APlayerController* PC = GetOwningPlayer())
		{
			ResumeButton->SetUserFocus(PC);
		}
	}
}

// ============================================================
// TAB SWITCHING
// ============================================================

void UUW_PauseMenu::SetTabActive(UButton* ActiveBtn)
{
	auto SetActive = [](UTextBlock* Txt, UImage* Underline, bool bActive)
	{
		if (Txt)
		{
			Txt->SetColorAndOpacity(FSlateColor(bActive ? Col_TextBright : Col_TextDim));
			FSlateFontInfo F = Txt->GetFont();
			F.TypefaceFontName = bActive ? FName("Bold") : FName("Regular");
			Txt->SetFont(F);
		}
		if (Underline && Underline->GetParent())
			Underline->GetParent()->SetVisibility(bActive ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	};

	struct TabInfo { UButton* Btn; UTextBlock* Txt; UImage* Underline; };
	TabInfo Tabs[] = {
		{TabMainBtn, TabMainTxt, TabMainUnderline},
		{TabSpawnBtn, TabSpawnTxt, TabSpawnUnderline},
		{TabOptionsBtn, TabOptionsTxt, TabOptionsUnderline}
	};

	for (const auto& T : Tabs)
		SetActive(T.Txt, T.Underline, T.Btn == ActiveBtn);
}

void UUW_PauseMenu::HighlightQualityButton(UButton* ActiveBtn)
{
	auto SetBtn = [](UButton* Btn, FLinearColor Tint, FLinearColor TextColor)
	{
		if (!Btn) return;
		FButtonStyle Style = Btn->GetStyle();
		Style.Normal.TintColor = FSlateColor(Tint);
		Btn->SetStyle(Style);
		UTextBlock* Txt = Cast<UTextBlock>(Btn->GetChildAt(0));
		if (Txt) Txt->SetColorAndOpacity(FSlateColor(TextColor));
	};

	SetBtn(QualityLowBtn, Col_BtnNormal, Col_TextDim);
	SetBtn(QualityMedBtn, Col_BtnNormal, Col_TextDim);
	SetBtn(QualityHighBtn, Col_BtnNormal, Col_TextDim);
	SetBtn(QualityEpicBtn, Col_BtnNormal, Col_TextDim);

	if (ActiveBtn)
		SetBtn(ActiveBtn, FLinearColor(0.08f, 0.15f, 0.28f, 0.95f), Col_Accent);
}

void UUW_PauseMenu::ShowTab(UVerticalBox* ActiveContent)
{
	struct TabEntry { UVerticalBox* Content; };
	TabEntry Tabs[] = { {MainTabContent}, {SpawnTabContent}, {OptionsTabContent} };

	for (const auto& T : Tabs)
	{
		if (!T.Content) continue;
		bool bActive = (T.Content == ActiveContent);
		T.Content->SetVisibility(bActive ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		if (UPanelWidget* P = T.Content->GetParent())
		{
			if (UPanelWidget* PP = P->GetParent())
			{
				PP->SetVisibility(bActive ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
			}
		}
	}
}

void UUW_PauseMenu::ShowMainTab()    { ShowTab(MainTabContent);    SetTabActive(TabMainBtn); }
void UUW_PauseMenu::ShowSpawnTab()   { ShowTab(SpawnTabContent);   SetTabActive(TabSpawnBtn); }
void UUW_PauseMenu::ShowOptionsTab() { ShowTab(OptionsTabContent); SetTabActive(TabOptionsBtn); }

void UUW_PauseMenu::OnTabMainClicked()    { ShowMainTab(); }
void UUW_PauseMenu::OnTabSpawnClicked()   { ShowSpawnTab(); }
void UUW_PauseMenu::OnTabOptionsClicked() { ShowOptionsTab(); }

// ============================================================
// GRAPHICS HANDLERS — common setter + thin wrappers
// ============================================================

void UUW_PauseMenu::SetQualityLevel(int32 Level, UButton* Btn)
{
	MenuUtil::ApplyQualityLevel(Level);
	HighlightQualityButton(Btn);

	if (CurrentQualityLabel)
	{
		CurrentQualityLabel->SetText(FText::FromString(FString::Printf(TEXT("Current: %s"), *MenuUtil::QualityName(Level))));
		CurrentQualityLabel->SetColorAndOpacity(FSlateColor(Col_Accent));
	}
}

void UUW_PauseMenu::OnQualityLowClicked()    { SetQualityLevel(0, QualityLowBtn); }
void UUW_PauseMenu::OnQualityMediumClicked() { SetQualityLevel(1, QualityMedBtn); }
void UUW_PauseMenu::OnQualityHighClicked()   { SetQualityLevel(2, QualityHighBtn); }
void UUW_PauseMenu::OnQualityEpicClicked()   { SetQualityLevel(3, QualityEpicBtn); }

void UUW_PauseMenu::OnVSyncToggled(bool bChecked)
{
	MenuUtil::ApplyVSync(bChecked);
}

void UUW_PauseMenu::OnShadowsToggled(bool bChecked)
{
	MenuUtil::ApplyShadows(bChecked);
}

void UUW_PauseMenu::OnResolutionScaleChanged(float Value)
{
	if (IConsoleVariable* CV = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage")))
	{
		CV->Set(Value * 100.0f, ECVF_SetByGameSetting);
	}
	MenuUtil::ApplyResolutionScale(Value);
	if (ResolutionScaleLabel)
		ResolutionScaleLabel->SetText(FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Value * 100.0f))));
}

// ============================================================
// INPUT / ACTIONS
// ============================================================

FReply UUW_PauseMenu::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		OnResumeClicked();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

void UUW_PauseMenu::OnResumeClicked()
{
	if (AParamotorPlayerController* PC = Cast<AParamotorPlayerController>(GetOwningPlayer()))
		PC->TogglePauseMenu();
}

void UUW_PauseMenu::OnMainMenuClicked()
{
	UGameplayStatics::OpenLevel(GetWorld(), FName(TEXT("L_MainMenu")));
}

void UUW_PauseMenu::OnQuitClicked()
{
	UKismetSystemLibrary::QuitGame(GetWorld(), nullptr, EQuitPreference::Quit, false);
}

void UUW_PauseMenu::OnSpawnTestWingClicked()
{
	if (AParamotorPlayerController* PC = Cast<AParamotorPlayerController>(GetOwningPlayer()))
		PC->SpawnTestWing();
}

void UUW_PauseMenu::OnSpawnParamotorClicked()
{
	if (AParamotorPlayerController* PC = Cast<AParamotorPlayerController>(GetOwningPlayer()))
		PC->SpawnParamotor();
}

void UUW_PauseMenu::OnClearSpawnedClicked()
{
	if (AParamotorPlayerController* PC = Cast<AParamotorPlayerController>(GetOwningPlayer()))
		PC->ClearSpawnedActors();
}
