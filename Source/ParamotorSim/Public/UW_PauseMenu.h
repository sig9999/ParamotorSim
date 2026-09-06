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
	UVerticalBox* MainTabContent;

	UPROPERTY()
	UVerticalBox* SpawnTabContent;

	UPROPERTY()
	UVerticalBox* OptionsTabContent;

	// Tab buttons (for highlighting)
	UPROPERTY()
	UButton* TabMainBtn;

	UPROPERTY()
	UButton* TabSpawnBtn;

	UPROPERTY()
	UButton* TabOptionsBtn;

	// Tab button text (for color changes)
	UPROPERTY()
	UTextBlock* TabMainTxt;

	UPROPERTY()
	UTextBlock* TabSpawnTxt;

	UPROPERTY()
	UTextBlock* TabOptionsTxt;

	// Tab button borders (for accent bar)
	UPROPERTY()
	UBorder* TabMainAccent;

	UPROPERTY()
	UBorder* TabSpawnAccent;

	UPROPERTY()
	UBorder* TabOptionsAccent;

	// Graphics widgets for live updates
	UPROPERTY()
	UTextBlock* ResolutionScaleLabel;

	UPROPERTY()
	UTextBlock* CurrentQualityLabel;

	UPROPERTY()
	UCheckBox* VSyncCheckBox;

	UPROPERTY()
	UCheckBox* ShadowsCheckBox;

	// SIG brand collapsible section
	UPROPERTY()
	UVerticalBox* SigWingContainer;

	UPROPERTY()
	UTextBlock* SigToggleIndicator;

	bool bSigWingsExpanded;

	// Helper to style a tab button as active/inactive
	void SetTabActive(UButton* ActiveBtn, UButton* InactiveBtn1, UButton* InactiveBtn2);

	// Helper to highlight a quality button
	void HighlightQualityButton(UButton* ActiveBtn);
	UPROPERTY()
	UButton* QualityLowBtn;
	UPROPERTY()
	UButton* QualityMedBtn;
	UPROPERTY()
	UButton* QualityHighBtn;
	UPROPERTY()
	UButton* QualityEpicBtn;
};
