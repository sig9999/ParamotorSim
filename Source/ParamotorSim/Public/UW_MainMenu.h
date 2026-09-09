// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/CanvasPanel.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "UW_MainMenu.generated.h"

UCLASS()
class PARAMOTORSIM_API UUW_MainMenu : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;

protected:
	UFUNCTION()
	void OnPlayClicked();

	UFUNCTION()
	void OnQuitClicked();

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

	UTextBlock* CreateSlider(UCanvasPanel* RootCanvas, const TCHAR* Name, float Min, float Max, float Value, float YPos);

	UPROPERTY()
	class UParamotorGameInstance* GameInstanceRef = nullptr;

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
};
