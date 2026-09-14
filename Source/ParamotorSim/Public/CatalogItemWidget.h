// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CatalogItemWidget.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCatalogItemClicked, int32, ItemIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCatalogItemHovered, int32, ItemIndex);

UCLASS()
class PARAMOTORSIM_API UCatalogItemWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeOnInitialized() override;

	UFUNCTION(BlueprintCallable, Category = "Catalog")
	void SetItem(int32 InIndex, const FString& InLabel, bool bInSelected, bool bInCenter = false, int32 InFontSize = 0);

	/** Optional secondary line under the label (e.g. equipped item name). */
	UFUNCTION(BlueprintCallable, Category = "Catalog")
	void SetSubtitle(const FString& InSub);

	UFUNCTION(BlueprintCallable, Category = "Catalog")
	void SetItemEnabled(bool bEnabled);

	UFUNCTION(BlueprintCallable, Category = "Catalog")
	void SetSelected(bool bInSelected);

	/** Override the base/selected background colors (e.g. translucent for menus over photos). */
	UFUNCTION(BlueprintCallable, Category = "Catalog")
	void SetItemColors(const FLinearColor& InNormal, const FLinearColor& InSelected);

	UPROPERTY(BlueprintAssignable, Category = "Catalog")
	FOnCatalogItemClicked OnItemClicked;

	/** Fires when the cursor enters the item — used for preview/detail panels. */
	UPROPERTY(BlueprintAssignable, Category = "Catalog")
	FOnCatalogItemHovered OnItemHovered;

	UPROPERTY(BlueprintReadOnly, Category = "Catalog")
	int32 ItemIndex = -1;

protected:
	UFUNCTION()
	void OnButtonClicked();

	UFUNCTION()
	void OnHover();

	UFUNCTION()
	void OnUnhover();

	UPROPERTY()
	class UButton* ItemButton = nullptr;

	UPROPERTY()
	class UTextBlock* ItemText = nullptr;

	UPROPERTY()
	class UTextBlock* SubText = nullptr;

	UPROPERTY()
	class UImage* Accent = nullptr;

	bool bSelected = false;
	bool bCenter = false;

	FLinearColor NormalColor = FLinearColor(0.035f, 0.045f, 0.06f, 0.82f);
	FLinearColor HoverColor = FLinearColor(0.08f, 0.10f, 0.13f, 0.92f);
	FLinearColor SelectedColor = FLinearColor(0.025f, 0.18f, 0.24f, 0.96f);
	FLinearColor AccentActive = FLinearColor(0.0f, 0.78f, 1.0f, 1.0f);
	FLinearColor AccentInactive = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);
};
