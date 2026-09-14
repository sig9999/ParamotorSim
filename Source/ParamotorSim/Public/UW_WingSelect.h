// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UW_WingSelect.generated.h"

/**
 * Full-screen wing catalog — light theme, opens from the hangar after the
 * paramotor build is saved. Brand tabs across the top, wing cards with
 * planform art on the left, full spec sheet on the right.
 */
UCLASS()
class PARAMOTORSIM_API UUW_WingSelect : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeOnInitialized() override;

protected:
	UFUNCTION()
	void OnWingCardClicked(int32 Index);

	UFUNCTION()
	void OnBrandTabClicked(int32 Index);

	UFUNCTION()
	void OnBackClicked(int32 Index);

	UFUNCTION()
	void OnConfirmClicked(int32 Index);

	void RebuildUI();
	void UpdateDetail();

	UPROPERTY()
	class UParamotorGameInstance* GI = nullptr;

	UPROPERTY()
	class UCanvasPanel* RootCanvas = nullptr;

	UPROPERTY()
	class UTextBlock* ConfirmLabel = nullptr;

	// Detail panel text targets
	UPROPERTY()
	class UTextBlock* DetailBrand = nullptr;
	UPROPERTY()
	class UTextBlock* DetailTitle = nullptr;
	UPROPERTY()
	class UTextBlock* DetailSub = nullptr;
	UPROPERTY()
	class UImage*     DetailImage = nullptr;
	UPROPERTY()
	class UTextBlock* DetailSpecs = nullptr;
	UPROPERTY()
	class UTextBlock* DetailNotes = nullptr;

	FString ActiveBrand;      // empty = ALL
	int32 SelectedWingIndex = -1;
};
