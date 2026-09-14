// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/Image.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "UW_FreeFlightMenu.generated.h"

struct FAirportInfo;

UCLASS()
class PARAMOTORSIM_API UUW_FreeFlightMenu : public UUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldMap")
	bool bWorldMapBackground = false;

	virtual void NativeOnInitialized() override;

	/** Called by WorldMapGameMode when an airport pin is clicked on the globe. */
	void UpdateAirportDisplay(const struct FAirportInfo& Info);

protected:
	UFUNCTION()
	void OnGarageTabClicked();

	UFUNCTION()
	void OnGarageTabHovered();

	UFUNCTION()
	void OnSettingsTabClicked();

	UFUNCTION()
	void OnHangarSectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

	UFUNCTION()
	void OnHangarBrandChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

	UFUNCTION()
	void OnHangarModelChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

	UFUNCTION()
	void OnHangarEquipClicked();

	UFUNCTION()
	void OnWeatherTabClicked();

	UFUNCTION()
	void OnWorldTabClicked();

	UFUNCTION()
	void OnFlyClicked();

	UFUNCTION()
	void OnFlyButtonHovered();

	UFUNCTION()
	void OnBackClicked();

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

	UFUNCTION()
	void OnMasterVolumeChanged(float Value);

	UFUNCTION()
	void OnMouseSensitivityChanged(float Value);

	UFUNCTION()
	void OnWindSpeedChanged(float Value);

	UFUNCTION()
	void OnWindGustsChanged(float Value);

	UFUNCTION()
	void OnCloudCoverChanged(float Value);

	UFUNCTION()
	void OnTemperatureChanged(float Value);

	UFUNCTION()
	void OnHumidityChanged(float Value);

	UFUNCTION()
	void OnTimeOfDayChanged(float Value);

	UFUNCTION()
	void OnWindDirectionChanged(float Value);

	UFUNCTION()
	void OnLiveWeatherClicked();

	UFUNCTION()
	void OnWeatherRefreshClicked();

	void SwitchToTab(int32 Index);

	// Weather tab — live METAR via aviationweather.gov for the focused airport
	void BuildWeatherPanel(UCanvasPanel* Panel);
	void RefreshWeatherPanel();
	void FetchLiveWeather();
	UTextBlock* CreateSlider(UCanvasPanel* RootCanvas, const TCHAR* Name, float Min, float Max, float Value, float YPos);
	void AddSettingsSlider(UCanvasPanel* Panel, const TCHAR* Name, float Min, float Max, float Value, float YPos, bool bPercent, UTextBlock*& OutLabel, USlider*& OutSlider);
	void InitFromCurrentSettings();
	void SetQualityLevel(int32 Level, UButton* ActiveBtn);
	void HighlightQualityButton(UButton* ActiveBtn);
	void BuildHangarData();
	void BuildHangarPanel(UCanvasPanel* Panel);
	void RefreshHangarBrands();
	void RefreshHangarModels();
	void RefreshHangarDetails();
	FString GetSectionName(int32 Section) const;
	FString BuildWingDetails(int32 Index) const;
	FString BuildEngineDetails(int32 Index) const;
	FString BuildFrameDetails(int32 Index) const;
	FString BuildPropDetails(int32 Index) const;

	UPROPERTY()
	class UParamotorGameInstance* GameInstanceRef = nullptr;

	UPROPERTY()
	class UWeatherService* WeatherSvc = nullptr;

	UPROPERTY()
	class UWidgetSwitcher* ContentSwitcher = nullptr;

	UPROPERTY()
	UTextBlock* WindLabel = nullptr;
	UPROPERTY()
	UTextBlock* GustsLabel = nullptr;
	UPROPERTY()
	UTextBlock* CloudLabel = nullptr;
	UPROPERTY()
	UTextBlock* TempLabel = nullptr;
	UPROPERTY()
	UTextBlock* HumidityLabel = nullptr;
	UPROPERTY()
	UTextBlock* TimeLabel = nullptr;

	UPROPERTY()
	USlider* WindSlider = nullptr;
	UPROPERTY()
	USlider* GustsSlider = nullptr;
	UPROPERTY()
	USlider* CloudSlider = nullptr;
	UPROPERTY()
	USlider* TempSlider = nullptr;
	UPROPERTY()
	USlider* HumiditySlider = nullptr;
	UPROPERTY()
	USlider* TimeSlider = nullptr;

	// Weather tab — live readouts + manual controls
	UPROPERTY()
	UButton* LiveToggleBtn = nullptr;
	UPROPERTY()
	UTextBlock* LiveToggleText = nullptr;
	UPROPERTY()
	UBorder* LiveCheckOutline = nullptr;
	UPROPERTY()
	UBorder* LiveCheckFill = nullptr;
	UPROPERTY()
	UTextBlock* WeatherSource = nullptr;
	UPROPERTY()
	UTextBlock* WeatherStatus = nullptr;
	UPROPERTY()
	UTextBlock* LiveStationText = nullptr;
	UPROPERTY()
	UTextBlock* LiveMetarText = nullptr;
	UPROPERTY()
	UTextBlock* LiveDataText = nullptr;
	UPROPERTY()
	UTextBlock* WeatherAnalysis = nullptr;
	UPROPERTY()
	UTextBlock* ManualHint = nullptr;
	UPROPERTY()
	UTextBlock* WindDirLabel = nullptr;
	UPROPERTY()
	USlider* WindDirSlider = nullptr;
	UPROPERTY()
	TArray<USlider*> WeatherSliders;
	UPROPERTY()
	TArray<UTextBlock*> WeatherSliderLabels;

	UPROPERTY()
	UButton* QualityLowBtn = nullptr;
	UPROPERTY()
	UButton* QualityMedBtn = nullptr;
	UPROPERTY()
	UButton* QualityHighBtn = nullptr;
	UPROPERTY()
	UButton* QualityEpicBtn = nullptr;
	UPROPERTY()
	UTextBlock* CurrentQualityLabel = nullptr;

	UPROPERTY()
	UCheckBox* VSyncCheckBox = nullptr;
	UPROPERTY()
	UCheckBox* ShadowsCheckBox = nullptr;

	UPROPERTY()
	UTextBlock* ResolutionScaleLabel = nullptr;
	UPROPERTY()
	UTextBlock* MasterVolumeLabel = nullptr;
	UPROPERTY()
	UTextBlock* MouseSensitivityLabel = nullptr;

	UPROPERTY()
	USlider* ResolutionScaleSlider = nullptr;
	UPROPERTY()
	USlider* MasterVolumeSlider = nullptr;
	UPROPERTY()
	USlider* MouseSensitivitySlider = nullptr;

	// World / tabs
	UPROPERTY()
	UBorder* DarkOverlay = nullptr;

	UPROPERTY()
	UBorder* ContentBG = nullptr;

	UPROPERTY()
	TArray<UImage*> TabUnderlines;

	UPROPERTY()
	UTextBlock* WorldHint = nullptr;

	// Hangar
	UPROPERTY()
	UComboBoxString* HangarSectionCombo = nullptr;
	UPROPERTY()
	UComboBoxString* HangarBrandCombo = nullptr;
	UPROPERTY()
	UComboBoxString* HangarModelCombo = nullptr;
	UPROPERTY()
	UTextBlock* HangarSpecs = nullptr;
	UPROPERTY()
	UTextBlock* HangarBuild = nullptr;
	UPROPERTY()
	UButton* HangarEquipButton = nullptr;

	int32 HangarSection = 0;
	int32 HangarBrand = 0;
	int32 HangarModel = 0;

	// World tab — airport info display (updated by WorldMapGameMode on pin click)
	UPROPERTY()
	UTextBlock* LaunchSiteDetails = nullptr;

	UPROPERTY()
	UTextBlock* LaunchSiteTitle = nullptr;

	UPROPERTY()
	UButton* FlyButton = nullptr;

	UPROPERTY()
	UTextBlock* FlyButtonText = nullptr;

	int32 SelectedLaunchSite = 0;
};
