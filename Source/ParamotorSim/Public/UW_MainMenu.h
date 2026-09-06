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
	void OnTimeOfDayChanged(float Value);

	UTextBlock* CreateSlider(UCanvasPanel* RootCanvas, const TCHAR* Name, float Min, float Max, float Value, float YPos);

	UPROPERTY()
	class UParamotorGameInstance* GameInstanceRef;

	UPROPERTY()
	UTextBlock* WindLabel;

	UPROPERTY()
	UTextBlock* GustsLabel;

	UPROPERTY()
	UTextBlock* CloudLabel;

	UPROPERTY()
	UTextBlock* TempLabel;

	UPROPERTY()
	UTextBlock* TimeLabel;

	UPROPERTY()
	USlider* WindSlider;

	UPROPERTY()
	USlider* GustsSlider;

	UPROPERTY()
	USlider* CloudSlider;

	UPROPERTY()
	USlider* TempSlider;

	UPROPERTY()
	USlider* TimeSlider;
};
