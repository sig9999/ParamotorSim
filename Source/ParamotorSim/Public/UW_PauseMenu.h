// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UW_PauseMenu.generated.h"

class UVerticalBox;
class UHorizontalBox;
class UButton;
class UTextBlock;
class UBorder;
class UCanvasPanel;
class USlider;
class UCheckBox;
class UScrollBox;
class UImage;

UCLASS()
class PARAMOTORSIM_API UUW_PauseMenu : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeOnInitialized() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

protected:
	// === CLICK HANDLERS ===
	UFUNCTION() void OnResumeClicked();
	UFUNCTION() void OnMainMenuClicked();
	UFUNCTION() void OnQuitClicked();
	UFUNCTION() void OnSpawnTestWingClicked();
	UFUNCTION() void OnSpawnParamotorClicked();
	UFUNCTION() void OnClearSpawnedClicked();

	UFUNCTION() void OnTabMainClicked();
	UFUNCTION() void OnTabSpawnClicked();
	UFUNCTION() void OnTabOptionsClicked();

	UFUNCTION() void OnQualityLowClicked();
	UFUNCTION() void OnQualityMediumClicked();
	UFUNCTION() void OnQualityHighClicked();
	UFUNCTION() void OnQualityEpicClicked();

	UFUNCTION() void OnVSyncToggled(bool bChecked);
	UFUNCTION() void OnShadowsToggled(bool bChecked);
	UFUNCTION() void OnResolutionScaleChanged(float Value);

	// === TAB SWITCHING ===
	void ShowMainTab();
	void ShowSpawnTab();
	void ShowOptionsTab();
	void ShowTab(UVerticalBox* ActiveContent);
	void SetTabActive(UButton* ActiveBtn);
	void HighlightQualityButton(UButton* ActiveBtn);

	// === BUILDER METHODS (split from NativeOnInitialized) ===
	void BuildRootLayout();
	void BuildHeader();
	void BuildFooter();
	void BuildMainTab();
	void BuildSpawnTab();
	void BuildOptionsTab();
	void InitFromCurrentSettings();

	// === BuildOptionsTab split helpers ===
	void BuildQualitySection(UVerticalBox* Container);
	void BuildResolutionSection(UVerticalBox* Container);
	void BuildDisplayTogglesSection(UVerticalBox* Container);
	void BuildKeybindSection(UVerticalBox* Container);
	UCheckBox* CreateLabeledCheckbox(UHorizontalBox* ParentRow, const FString& BoxName,
		const FString& LabelText, float SlotPadLeft, float SlotPadRight);

	// === WIDGET FACTORY HELPERS ===
	UTextBlock* MakeText(const FString& Name, const FString& Text, int32 Size, FLinearColor Color, bool bBold = false);
	UTextBlock* MakeLeftText(const FString& Name, const FString& Text, int32 Size, FLinearColor Color);
	UButton* CreateCardButton(const FString& Name, const FString& Label, int32 FontSize, FLinearColor TextColor);
	UButton* CreatePillButton(const FString& Name, const FString& Label, int32 FontSize);
	void CreateSectionHeader(UVerticalBox* Container, const FString& Title);
	void AddKeybindRow(UVerticalBox* Container, const FString& Action, const FString& Key);
	UButton* CreateTabButton(const FString& Name, const FString& Label, UImage*& OutUnderline);
	UVerticalBox* CreateTabPanel(const FString& Name);
	void CreateWingCard(const FString& Name, const FString& WingName, const FString& Specs, FLinearColor AccentColor, UButton*& OutBtn);

	// === COMMON QUALITY SETTER ===
	void SetQualityLevel(int32 Level, UButton* Btn);

	// === WIDGET POINTERS ===
	UPROPERTY() UVerticalBox* MainTabContent = nullptr;
	UPROPERTY() UVerticalBox* SpawnTabContent = nullptr;
	UPROPERTY() UVerticalBox* OptionsTabContent = nullptr;

	UPROPERTY() UButton* TabMainBtn = nullptr;
	UPROPERTY() UButton* TabSpawnBtn = nullptr;
	UPROPERTY() UButton* TabOptionsBtn = nullptr;

	UPROPERTY() UTextBlock* TabMainTxt = nullptr;
	UPROPERTY() UTextBlock* TabSpawnTxt = nullptr;
	UPROPERTY() UTextBlock* TabOptionsTxt = nullptr;

	// Active-tab underline strips under each top-nav button.
	UPROPERTY() UImage* TabMainUnderline = nullptr;
	UPROPERTY() UImage* TabSpawnUnderline = nullptr;
	UPROPERTY() UImage* TabOptionsUnderline = nullptr;

	UPROPERTY() UTextBlock* ResolutionScaleLabel = nullptr;
	UPROPERTY() UTextBlock* CurrentQualityLabel = nullptr;
	UPROPERTY() UCheckBox* VSyncCheckBox = nullptr;
	UPROPERTY() UCheckBox* ShadowsCheckBox = nullptr;

	UPROPERTY() UButton* QualityLowBtn = nullptr;
	UPROPERTY() UButton* QualityMedBtn = nullptr;
	UPROPERTY() UButton* QualityHighBtn = nullptr;
	UPROPERTY() UButton* QualityEpicBtn = nullptr;

	// === SHARED ROOT WIDGETS (created in BuildRootLayout, used by tab builders) ===
	UPROPERTY() UCanvasPanel* RootCanvas = nullptr;
	UPROPERTY() UCanvasPanel* ContentCanvas = nullptr;
	UPROPERTY() UHorizontalBox* TabButtonRow = nullptr;
	UPROPERTY() UBorder* HeaderBandBorder = nullptr;
	UPROPERTY() UBorder* FooterBandBorder = nullptr;
	UPROPERTY() UButton* ResumeButton = nullptr;
};
