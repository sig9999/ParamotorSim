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

UCLASS()
class PARAMOTORSIM_API UUW_PauseMenu : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeOnInitialized() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

protected:
	UFUNCTION()
	void OnResumeClicked();

	UFUNCTION()
	void OnQuitClicked();

	UFUNCTION()
	void OnSpawnParamotorClicked();

	UFUNCTION()
	void OnSpawnWing16Clicked();

	UFUNCTION()
	void OnSpawnWing20Clicked();

	UFUNCTION()
	void OnSpawnWing22Clicked();

	UFUNCTION()
	void OnSpawnWing25Clicked();

	UFUNCTION()
	void OnSpawnWing30Clicked();

	UFUNCTION()
	void OnSpawnWing35Clicked();

	UFUNCTION()
	void OnSpawnMacParaCharger25Clicked();

	UFUNCTION()
	void OnSigBrandToggleClicked();

	UFUNCTION()
	void OnClearSpawnedClicked();

	UFUNCTION()
	void OnTabMainClicked();

	UFUNCTION()
	void OnTabSpawnClicked();

	UFUNCTION()
	void OnTabOptionsClicked();

	void ShowMainTab();
	void ShowSpawnTab();
	void ShowOptionsTab();
	void ShowTab(UVerticalBox* ActiveContent);

	// Graphics option handlers
	UFUNCTION()
	void OnQualityLowClicked();

	UFUNCTION()
	void OnQualityMediumClicked();

	UFUNCTION()
	void OnQualityHighClicked();

	UFUNCTION()
	void OnQualityEpicClicked();

	UFUNCTION()
	void OnVSyncToggled(bool bChecked);

	UFUNCTION()
	void OnShadowsToggled(bool bChecked);

	UFUNCTION()
	void OnResolutionScaleChanged(float Value);

	// Tab content panels
	UPROPERTY()
	UVerticalBox* MainTabContent = nullptr;

	UPROPERTY()
	UVerticalBox* SpawnTabContent = nullptr;

	UPROPERTY()
	UVerticalBox* OptionsTabContent = nullptr;

	// Tab buttons (for highlighting)
	UPROPERTY()
	UButton* TabMainBtn = nullptr;

	UPROPERTY()
	UButton* TabSpawnBtn = nullptr;

	UPROPERTY()
	UButton* TabOptionsBtn = nullptr;

	// Tab button text (for color changes)
	UPROPERTY()
	UTextBlock* TabMainTxt = nullptr;

	UPROPERTY()
	UTextBlock* TabSpawnTxt = nullptr;

	UPROPERTY()
	UTextBlock* TabOptionsTxt = nullptr;

	// Tab button borders (for accent bar)
	UPROPERTY()
	UBorder* TabMainAccent = nullptr;

	UPROPERTY()
	UBorder* TabSpawnAccent = nullptr;

	UPROPERTY()
	UBorder* TabOptionsAccent = nullptr;

	// Graphics widgets for live updates
	UPROPERTY()
	UTextBlock* ResolutionScaleLabel = nullptr;

	UPROPERTY()
	UTextBlock* CurrentQualityLabel = nullptr;

	UPROPERTY()
	UCheckBox* VSyncCheckBox = nullptr;

	UPROPERTY()
	UCheckBox* ShadowsCheckBox = nullptr;

	// SIG brand collapsible section
	UPROPERTY()
	UVerticalBox* SigWingContainer = nullptr;

	UPROPERTY()
	UTextBlock* SigToggleIndicator = nullptr;

	bool bSigWingsExpanded = false;

	// Helper to style a tab button as active/inactive
	void SetTabActive(UButton* ActiveBtn, UButton* InactiveBtn1, UButton* InactiveBtn2);

	// Helper to highlight a quality button
	void HighlightQualityButton(UButton* ActiveBtn);
	UPROPERTY()
	UButton* QualityLowBtn = nullptr;
	UPROPERTY()
	UButton* QualityMedBtn = nullptr;
	UPROPERTY()
	UButton* QualityHighBtn = nullptr;
	UPROPERTY()
	UButton* QualityEpicBtn = nullptr;
};
