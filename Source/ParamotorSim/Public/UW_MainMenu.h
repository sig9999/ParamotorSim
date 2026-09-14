// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Engine/Texture2D.h"
#include "UW_MainMenu.generated.h"

UCLASS()
class PARAMOTORSIM_API UUW_MainMenu : public UUserWidget
{
	GENERATED_BODY()

public:
	UUW_MainMenu(const FObjectInitializer& ObjectInitializer);
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

protected:
	UPROPERTY()
	TObjectPtr<UTexture2D> MenuBackgroundTexture;

protected:
	UFUNCTION()
	void OnMenuItemClicked(int32 Index);

	UFUNCTION()
	void OnMenuItemHovered(int32 Index);

	UFUNCTION()
	void OnQuitClicked();

	void UpdateDetailPanel(int32 Index);

	UPROPERTY()
	class UCanvasPanel* RootCanvas = nullptr;

	UPROPERTY()
	class UTextBlock* DetailKicker = nullptr;

	UPROPERTY()
	class UTextBlock* DetailTitle = nullptr;

	UPROPERTY()
	class UTextBlock* DetailDesc = nullptr;

	UPROPERTY()
	class UTextBlock* DetailFeat = nullptr;

	UPROPERTY()
	class UBorder* DetailPanel = nullptr;

	// Entry-point fade-in on menu open.
	float FadeElapsed = 0.0f;
};
