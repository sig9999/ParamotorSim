// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ParamotorBuilder.h"
#include "UW_HangarMenu.generated.h"

struct FParamotorBuild;

UCLASS()
class PARAMOTORSIM_API UUW_HangarMenu : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeOnInitialized() override;

protected:
	void BuildHangarData();
	bool LoadSavedBuild();
	void SaveCurrentBuild();
	void SyncBuilderVisibility();

	void RefreshUI();
	void OpenCatalogForSection(int32 Section);
	void BuildCatalogOverlay(float ViewW);
	void BuildGarageOverlay(float ViewW);
	class UParamotorSaveGame* GetOrCreateSave();
	FString GarageSummary(const FParamotorBuild& Build) const;
	float GarageWeight(const FParamotorBuild& Build) const;

	UFUNCTION()
	void OnSectionClicked(int32 Index);

	UFUNCTION()
	void OnBrandClicked(int32 BrandIndex);

	UFUNCTION()
	void OnModelClicked(int32 ModelIndex);

	UFUNCTION()
	void OnEquipClicked();

	UFUNCTION()
	void OnUnequipClicked();

	UFUNCTION()
	void OnSaveClicked();

	UFUNCTION()
	void OpenWingSelect();

	UFUNCTION()
	void OnBackClicked();

	UFUNCTION()
	void OnCatalogCloseClicked();

	UFUNCTION()
	void OnGarageCloseClicked();

	UFUNCTION()
	void OnGarageLoadClicked(int32 Index);

	UFUNCTION()
	void OnGarageDeleteClicked(int32 Index);

	UFUNCTION()
	void OnGarageSaveNewClicked();

	FString GetSectionName(int32 Index) const;
	FString GetSelectedModelNameForSection(int32 Section) const;
	TArray<FString> GetBrandsForSection(int32 Section) const;
	TArray<int32> GetModelIndicesForBrand(int32 Section, const FString& Brand) const;
	FString GetBrandForModel(int32 Section, int32 ModelIndex) const;
	FString GetModelNameForSpec(int32 Section, int32 ModelIndex) const;
	FString GetCatalogTitle(int32 Section) const;
	FString BuildSpecText(int32 Section, int32 ModelIndex) const;

	EHangarPart SectionToPart(int32 Section) const;
	int32 GetCurrentBuildIndexForSection(int32 Section) const;
	void SetCurrentBuildIndexForSection(int32 Section, int32 Index);

	UPROPERTY()
	class AParamotorBuilder* Builder = nullptr;

	UPROPERTY()
	class UParamotorGameInstance* GI = nullptr;

	UPROPERTY()
	class UTextBlock* SpecText = nullptr;

	UPROPERTY()
	class UCanvasPanel* RootCanvas = nullptr;

	int32 SelectedSection = 0;
	FString SelectedBrand;
	int32 SelectedModelIndex = -1;
	bool bCatalogOpen = false;
	bool bGarageOpen = false;
};
