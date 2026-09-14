// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "IndexedButton.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnIndexedClicked, int32, Index);

/**
 * Minimal UButton wrapper that carries an index — Slate's OnClicked is
 * zero-arg, so any indexed list (cards, tabs) needs a wrapper like this.
 * Content is caller-supplied (set via SetContent / SetButtonStyle).
 */
UCLASS()
class PARAMOTORSIM_API UIndexedButton : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeOnInitialized() override;

	UFUNCTION(BlueprintCallable, Category = "Indexed")
	void SetIndex(int32 InIndex) { Index = InIndex; }

	UFUNCTION(BlueprintCallable, Category = "Indexed")
	void SetButtonStyle(const FButtonStyle& Style);

	UFUNCTION(BlueprintCallable, Category = "Indexed")
	void SetContent(class UWidget* Content);

	UFUNCTION(BlueprintCallable, Category = "Indexed")
	void SetButtonEnabled(bool bEnabled);

	UPROPERTY(BlueprintAssignable, Category = "Indexed")
	FOnIndexedClicked OnIndexedClicked;

	UPROPERTY(BlueprintAssignable, Category = "Indexed")
	FOnIndexedClicked OnIndexedHovered;

	int32 Index = -1;

protected:
	UFUNCTION()
	void HandleClicked();

	UFUNCTION()
	void HandleHovered();

	UPROPERTY()
	class UButton* Button = nullptr;
};
