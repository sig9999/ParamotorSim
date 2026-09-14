// Copyright Epic Games, Inc. All Rights Reserved.

#include "UW_HangarMenu.h"
#include "UW_WingSelect.h"
#include "CatalogItemWidget.h"
#include "ParamotorBuilder.h"
#include "ParamotorGameInstance.h"
#include "UIStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Components/CanvasPanel.h"
#include "Components/TextBlock.h"
#include "Components/Border.h"
#include "Components/Image.h"

// ==== PART CATALOG OVERLAY ====

void UUW_HangarMenu::OpenCatalogForSection(int32 Section)
{
	SelectedSection = Section;
	bCatalogOpen = true;

	const int32 EquippedIndex = GetCurrentBuildIndexForSection(Section);
	SelectedBrand = GetBrandForModel(Section, EquippedIndex);
	TArray<FString> Brands = GetBrandsForSection(Section);
	if (SelectedBrand.IsEmpty() && Brands.Num() > 0)
	{
		SelectedBrand = Brands[0];
	}

	TArray<int32> ModelIndices = GetModelIndicesForBrand(Section, SelectedBrand);
	if (ModelIndices.Contains(EquippedIndex))
	{
		SelectedModelIndex = EquippedIndex;
	}
	else
	{
		SelectedModelIndex = ModelIndices.Num() > 0 ? ModelIndices[0] : -1;
	}
	RefreshUI();
}
void UUW_HangarMenu::BuildCatalogOverlay(float ViewW)
{
	using namespace UIStyle;

	const float LeftX = 0.03f;
	const float SpecX = 0.635f;
	const float RightX = 0.97f;
	const float ContentTop = 0.265f;
	const float ContentBottom = 0.855f;
	const float PanelPad = 22.0f;    // px inside panels
	const float PadN = PanelPad / ViewW; // normalized pad for anchors

	// Full-screen page — Visible so it swallows clicks meant for the rail behind.
	UBorder* Page = MakePanel(WidgetTree, FLinearColor(0.005f, 0.009f, 0.014f, 1.0f));
	Page->SetVisibility(ESlateVisibility::Visible);
	PlaceOnCanvas(Page, RootCanvas, FAnchors(0.0f, 0.0f, 1.0f, 1.0f), FMargin(0.0f), 50);

	UImage* TopGlow = MakeLine(WidgetTree, FLinearColor(0.0f, 0.20f, 0.28f, 0.28f));
	PlaceOnCanvas(TopGlow, RootCanvas, FAnchors(0.0f, 0.0f, 1.0f, 0.0f), FMargin(0.0f, 0.0f, 0.0f, 6.0f), 51);

	// Header
	UTextBlock* Kicker = MakeText(WidgetTree, TEXT("HANGAR  //  EQUIPMENT CATALOG"), Accent, 13, 4, true);
	PlaceOnCanvas(Kicker, RootCanvas, FAnchors(0.0f, 0.0f, 1.0f, 0.0f), FMargin(46.0f, 26.0f, 46.0f, 18.0f), 60);

	UTextBlock* Title = MakeText(WidgetTree, *GetCatalogTitle(SelectedSection), Text, 42, 3, true);
	PlaceOnCanvas(Title, RootCanvas, FAnchors(0.0f, 0.0f, 1.0f, 0.0f), FMargin(44.0f, 46.0f, 46.0f, 52.0f), 60);

	UImage* TitleRule = MakeLine(WidgetTree, AccentSoft);
	PlaceOnCanvas(TitleRule, RootCanvas, FAnchors(0.0f, 0.0f, 1.0f, 0.0f), FMargin(46.0f, 104.0f, 130.0f, 2.0f), 60);

	UButton* CloseBtn = MakeGhostButton(WidgetTree, TEXT("X  CLOSE"), 15, TextDim);
	CloseBtn->OnClicked.AddDynamic(this, &UUW_HangarMenu::OnCatalogCloseClicked);
	PlaceOnCanvas(CloseBtn, RootCanvas, FAnchors(1.0f, 0.0f, 1.0f, 0.0f), FMargin(-196.0f, 30.0f, 180.0f, 40.0f), 65);

	// Brand tabs
	TArray<FString> Brands = GetBrandsForSection(SelectedSection);
	TArray<int32> ModelIndices = GetModelIndicesForBrand(SelectedSection, SelectedBrand);
	const float BrandY = 140.0f;
	const float TabW = 176.0f;
	const float TabGap = 10.0f;

	UTextBlock* BrandLabel = MakeText(WidgetTree, TEXT("MANUFACTURER"), TextFaint, 12, 4, true);
	PlaceOnCanvas(BrandLabel, RootCanvas, FAnchors(0.0f, 0.0f, 1.0f, 0.0f), FMargin(46.0f, BrandY - 24.0f, 46.0f, 16.0f), 60);

	for (int32 i = 0; i < Brands.Num(); ++i)
	{
		UCatalogItemWidget* Tab = CreateWidget<UCatalogItemWidget>(this, UCatalogItemWidget::StaticClass());
		Tab->SetItem(i, Brands[i].ToUpper(), Brands[i] == SelectedBrand, true, 15);
		Tab->OnItemClicked.AddDynamic(this, &UUW_HangarMenu::OnBrandClicked);
		PlaceOnCanvas(Tab, RootCanvas, FAnchors(0.0f, 0.0f, 0.0f, 0.0f), FMargin(46.0f + i * (TabW + TabGap), BrandY, TabW, 44.0f), 62);
	}

	// Models panel
	UBorder* ModelsPanel = MakePanel(WidgetTree, Panel);
	ModelsPanel->SetVisibility(ESlateVisibility::HitTestInvisible);
	PlaceOnCanvas(ModelsPanel, RootCanvas, FAnchors(LeftX, ContentTop, SpecX, ContentBottom), FMargin(0.0f), 55);

	UTextBlock* ModelsTitle = MakeText(WidgetTree, TEXT("MODELS"), TextFaint, 12, 4, true);
	PlaceOnCanvas(ModelsTitle, RootCanvas, FAnchors(LeftX, ContentTop, SpecX, ContentTop), FMargin(PanelPad, 18.0f, 40.0f, 16.0f), 62);

	UImage* ModelsAccent = MakeLine(WidgetTree, AccentSoft);
	PlaceOnCanvas(ModelsAccent, RootCanvas, FAnchors(LeftX, ContentTop, SpecX, ContentTop), FMargin(PanelPad, 40.0f, 80.0f, 2.0f), 62);

	if (ModelIndices.Num() > 0)
	{
		const float GapN = 14.0f / ViewW;
		const int32 Columns = 2;
		const float CardW = (SpecX - LeftX - PadN * 2.0f - GapN) / Columns;
		const float CardH = 64.0f;
		const float CardsTop = 58.0f;

		for (int32 i = 0; i < ModelIndices.Num(); ++i)
		{
			const int32 Row = i / Columns;
			const int32 Col = i % Columns;
			const float X = LeftX + PadN + Col * (CardW + GapN);
			const float CY = CardsTop + Row * (CardH + 12.0f);
			const int32 SpecIndex = ModelIndices[i];

			UCatalogItemWidget* ModelCard = CreateWidget<UCatalogItemWidget>(this, UCatalogItemWidget::StaticClass());
			ModelCard->SetItem(i, GetModelNameForSpec(SelectedSection, SpecIndex), SpecIndex == SelectedModelIndex, false, 17);
			ModelCard->SetSubtitle(GetBrandForModel(SelectedSection, SpecIndex).ToUpper());
			ModelCard->OnItemClicked.AddDynamic(this, &UUW_HangarMenu::OnModelClicked);
			PlaceOnCanvas(ModelCard, RootCanvas, FAnchors(X, ContentTop, X + CardW, ContentTop), FMargin(0.0f, CY, 0.0f, CardH), 62);
		}
	}
	else
	{
		UTextBlock* EmptyTitle = MakeText(WidgetTree, TEXT("COMING SOON"), Warning, 24, 3, true, ETextJustify::Center);
		PlaceOnCanvas(EmptyTitle, RootCanvas, FAnchors(LeftX, ContentTop, SpecX, ContentTop), FMargin(0.0f, 110.0f, 0.0f, 34.0f), 63);

		UTextBlock* EmptyBody = MakeText(WidgetTree, TEXT("No manufacturer entries are installed for this category yet."), TextDim, 14, 0, false, ETextJustify::Center);
		EmptyBody->SetAutoWrapText(true);
		PlaceOnCanvas(EmptyBody, RootCanvas, FAnchors(LeftX, ContentTop, SpecX, ContentTop), FMargin(40.0f, 152.0f, 40.0f, 40.0f), 63);
	}

	// Spec panel
	UBorder* SpecPanel = MakePanel(WidgetTree, Panel);
	SpecPanel->SetVisibility(ESlateVisibility::HitTestInvisible);
	PlaceOnCanvas(SpecPanel, RootCanvas, FAnchors(SpecX + 0.012f, ContentTop, RightX, ContentBottom), FMargin(0.0f), 55);

	UTextBlock* SpecTitle = MakeText(WidgetTree, TEXT("SPECIFICATION"), Accent, 12, 4, true);
	PlaceOnCanvas(SpecTitle, RootCanvas, FAnchors(SpecX + 0.012f, ContentTop, RightX, ContentTop), FMargin(PanelPad, 18.0f, PanelPad, 16.0f), 62);

	UImage* SpecAccent = MakeLine(WidgetTree, AccentSoft);
	PlaceOnCanvas(SpecAccent, RootCanvas, FAnchors(SpecX + 0.012f, ContentTop, RightX, ContentTop), FMargin(PanelPad, 40.0f, 100.0f, 2.0f), 62);

	SpecText = MakeText(WidgetTree, *BuildSpecText(SelectedSection, SelectedModelIndex), Text, 13, 0, false);
	SpecText->SetAutoWrapText(true);
	PlaceOnCanvas(SpecText, RootCanvas, FAnchors(SpecX + 0.012f, ContentTop, RightX, ContentBottom), FMargin(PanelPad, 58.0f, PanelPad, 20.0f), 62);

	// Bottom action bar
	UImage* BottomLine = MakeLine(WidgetTree, Edge);
	PlaceOnCanvas(BottomLine, RootCanvas, FAnchors(LeftX, 0.895f, RightX, 0.895f), FMargin(0.0f, 0.0f, 0.0f, 1.0f), 62);

	UButton* BackBtn = MakeGhostButton(WidgetTree, TEXT("< BACK TO HANGAR"), 16, TextDim);
	BackBtn->OnClicked.AddDynamic(this, &UUW_HangarMenu::OnCatalogCloseClicked);
	PlaceOnCanvas(BackBtn, RootCanvas, FAnchors(LeftX, 0.895f, LeftX, 0.895f), FMargin(0.0f, 22.0f, 300.0f, 46.0f), 65);

	const bool bHasEquipped = GetCurrentBuildIndexForSection(SelectedSection) >= 0;
	UButton* UnequipBtn = MakeGhostButton(WidgetTree, TEXT("REMOVE"), 15, TextDim);
	UnequipBtn->SetIsEnabled(bHasEquipped);
	UnequipBtn->OnClicked.AddDynamic(this, &UUW_HangarMenu::OnUnequipClicked);
	PlaceOnCanvas(UnequipBtn, RootCanvas, FAnchors(RightX, 0.895f, RightX, 0.895f), FMargin(-430.0f, 22.0f, 130.0f, 46.0f), 65);

	UButton* EquipBtn = MakeActionButton(WidgetTree,
		SelectedModelIndex >= 0 ? TEXT("EQUIP SELECTED") : TEXT("NO MODEL AVAILABLE"), 16);
	EquipBtn->SetIsEnabled(SelectedModelIndex >= 0);
	EquipBtn->OnClicked.AddDynamic(this, &UUW_HangarMenu::OnEquipClicked);
	PlaceOnCanvas(EquipBtn, RootCanvas, FAnchors(RightX, 0.895f, RightX, 0.895f), FMargin(-280.0f, 22.0f, 280.0f, 46.0f), 65);
}
void UUW_HangarMenu::OnSectionClicked(int32 Index)
{
	if (Index == 90)
	{
		bGarageOpen = true;
		bCatalogOpen = false;
		RefreshUI();
		return;
	}
	bGarageOpen = false;
	OpenCatalogForSection(Index);
}
void UUW_HangarMenu::OnBrandClicked(int32 BrandIndex)
{
	TArray<FString> Brands = GetBrandsForSection(SelectedSection);
	if (Brands.IsValidIndex(BrandIndex))
	{
		SelectedBrand = Brands[BrandIndex];
	}
	TArray<int32> Indices = GetModelIndicesForBrand(SelectedSection, SelectedBrand);
	const int32 EquippedIndex = GetCurrentBuildIndexForSection(SelectedSection);
	SelectedModelIndex = Indices.Contains(EquippedIndex) ? EquippedIndex : (Indices.Num() > 0 ? Indices[0] : -1);
	RefreshUI();
}
void UUW_HangarMenu::OnModelClicked(int32 ModelIndex)
{
	TArray<int32> Indices = GetModelIndicesForBrand(SelectedSection, SelectedBrand);
	if (Indices.IsValidIndex(ModelIndex))
	{
		SelectedModelIndex = Indices[ModelIndex];
	}
	RefreshUI();
}
void UUW_HangarMenu::OnEquipClicked()
{
	if (SelectedModelIndex >= 0)
	{
		SetCurrentBuildIndexForSection(SelectedSection, SelectedModelIndex);
		if (!Builder)
		{
			if (UWorld* World = GetWorld())
			{
				for (TActorIterator<AParamotorBuilder> It(World); It; ++It) { Builder = *It; break; }
			}
		}
		if (Builder)
		{
			// Re-spawn the part so equipping a different model swaps the mesh.
			Builder->SetPart(SectionToPart(SelectedSection));
		}
		bCatalogOpen = false;
		SyncBuilderVisibility();
		RefreshUI();
	}
}
void UUW_HangarMenu::OnUnequipClicked()
{
	SetCurrentBuildIndexForSection(SelectedSection, -1);
	bCatalogOpen = false;
	SyncBuilderVisibility();
	RefreshUI();
}
void UUW_HangarMenu::OnCatalogCloseClicked()
{
	bCatalogOpen = false;
	RefreshUI();
}
FString UUW_HangarMenu::GetSectionName(int32 Index) const
{
	switch (Index)
	{
		case 0: return TEXT("Frame");
		case 1: return TEXT("Engine");
		case 2: return TEXT("Propeller");
		case 3: return TEXT("Harness");
		case 4: return TEXT("Wing");
	}
	return TEXT("");
}
void UUW_HangarMenu::SetCurrentBuildIndexForSection(int32 Section, int32 Index)
{
	if (!GI) return;
	switch (Section)
	{
		case 0: GI->CurrentBuild.FrameIndex = Index; break;
		case 1: GI->CurrentBuild.EngineIndex = Index; break;
		case 2: GI->CurrentBuild.PropIndex = Index; break;
		case 3: GI->CurrentBuild.HarnessIndex = Index; break;
		case 4: GI->CurrentBuild.WingIndex = Index; break;
	}
}
FString UUW_HangarMenu::GetSelectedModelNameForSection(int32 Section) const
{
	int32 Index = GetCurrentBuildIndexForSection(Section);
	if (!GI || Index < 0) return TEXT("None");
	switch (Section)
	{
		case 0: return GI->FrameSpecs.IsValidIndex(Index) ? GI->FrameSpecs[Index].Model : TEXT("None");
		case 1: return GI->EngineSpecs.IsValidIndex(Index) ? GI->EngineSpecs[Index].Model : TEXT("None");
		case 2: return GI->PropSpecs.IsValidIndex(Index) ? GI->PropSpecs[Index].Model : TEXT("None");
		case 3: return GI->HarnessSpecs.IsValidIndex(Index) ? GI->HarnessSpecs[Index].Model : TEXT("None");
		case 4: return GI->WingSpecs.IsValidIndex(Index) ? GI->WingSpecs[Index].Model : TEXT("None");
	}
	return TEXT("None");
}
FString UUW_HangarMenu::GetBrandForModel(int32 Section, int32 ModelIndex) const
{
	if (!GI || ModelIndex < 0) return FString();
	switch (Section)
	{
		case 0: return GI->FrameSpecs.IsValidIndex(ModelIndex) ? GI->FrameSpecs[ModelIndex].Brand : FString();
		case 1: return GI->EngineSpecs.IsValidIndex(ModelIndex) ? GI->EngineSpecs[ModelIndex].Brand : FString();
		case 2: return GI->PropSpecs.IsValidIndex(ModelIndex) ? GI->PropSpecs[ModelIndex].Brand : FString();
		case 3: return GI->HarnessSpecs.IsValidIndex(ModelIndex) ? GI->HarnessSpecs[ModelIndex].Brand : FString();
		case 4: return GI->WingSpecs.IsValidIndex(ModelIndex) ? GI->WingSpecs[ModelIndex].Brand : FString();
	}
	return FString();
}
FString UUW_HangarMenu::GetModelNameForSpec(int32 Section, int32 ModelIndex) const
{
	if (!GI || ModelIndex < 0) return TEXT("(unknown)");
	switch (Section)
	{
		case 0: return GI->FrameSpecs.IsValidIndex(ModelIndex) ? GI->FrameSpecs[ModelIndex].Model : TEXT("(unknown)");
		case 1: return GI->EngineSpecs.IsValidIndex(ModelIndex) ? GI->EngineSpecs[ModelIndex].Model : TEXT("(unknown)");
		case 2: return GI->PropSpecs.IsValidIndex(ModelIndex) ? GI->PropSpecs[ModelIndex].Model : TEXT("(unknown)");
		case 3: return GI->HarnessSpecs.IsValidIndex(ModelIndex) ? GI->HarnessSpecs[ModelIndex].Model : TEXT("(unknown)");
		case 4: return GI->WingSpecs.IsValidIndex(ModelIndex) ? GI->WingSpecs[ModelIndex].Model : TEXT("(unknown)");
	}
	return TEXT("(unknown)");
}
FString UUW_HangarMenu::GetCatalogTitle(int32 Section) const
{
	switch (Section)
	{
		case 0: return TEXT("FRAMES");
		case 1: return TEXT("MOTORS");
		case 2: return TEXT("PROPELLERS");
		case 3: return TEXT("HARNESSES");
		case 4: return TEXT("WINGS");
	}
	return TEXT("EQUIPMENT");
}
FString UUW_HangarMenu::BuildSpecText(int32 Section, int32 ModelIndex) const
{
	if (!GI) return TEXT("Select a model to view specs.");
	if (ModelIndex < 0)
	{
		if (Section == 0)
		{
			return TEXT("FRAME\n\nSelect a frame model to preview it on the podium.\n\nThe frame is the mounting point for the engine, propeller, harness, and wing.");
		}
		return FString::Printf(TEXT("%s\n\nNo model selected."), *GetSectionName(Section).ToUpper());
	}

	switch (Section)
	{
		case 0:
		{
			if (!GI->FrameSpecs.IsValidIndex(ModelIndex)) return TEXT("No frame data available.");
			const FParamotorFrameSpec& S = GI->FrameSpecs[ModelIndex];
			return FString::Printf(TEXT("%s\n%s  //  FRAME\n\nCHASSIS\nWeight       %.1f kg\nEngine mount %s\nRole         Structural base\n\n%s"),
				*S.Model, *S.Brand, S.Weight_kg, *S.EngineMount, *S.Notes);
		}
		case 1:
		{
			if (!GI->EngineSpecs.IsValidIndex(ModelIndex)) return TEXT("No engine data available.");
			const FParamotorEngineSpec& S = GI->EngineSpecs[ModelIndex];
			const float PowerDensity = S.Weight_kg > 0.0f ? S.Power_hp / S.Weight_kg : 0.0f;
			const float SpecificOutput = S.Displacement_cc > 0.0f ? S.Power_hp / (S.Displacement_cc / 1000.0f) : 0.0f;
			const float TorquePerLiter = S.Displacement_cc > 0.0f ? S.Torque_Nm / (S.Displacement_cc / 1000.0f) : 0.0f;
			return FString::Printf(TEXT("%s\n%s  //  ENGINE\n\nPOWERTRAIN\nDisplacement    %.0f cc\nOutput          %.0f hp\nTorque          %.0f Nm\n\nMASS / INSTALL\nDry weight      %.1f kg\nPower density   %.2f hp/kg\nSpecific output %.0f hp/L\nTorque density  %.0f Nm/L\nProp hub        %s\n\nPROP MATCH\nStatic thrust   %s\n\n%s"),
				*S.Model, *S.Brand, S.Displacement_cc, S.Power_hp, S.Torque_Nm, S.Weight_kg,
				PowerDensity, SpecificOutput, TorquePerLiter, *S.HubSize, *S.StaticThrustByProp, *S.Notes);
		}
		case 2:
		{
			if (!GI->PropSpecs.IsValidIndex(ModelIndex)) return TEXT("No propeller data available.");
			const FParamotorPropSpec& S = GI->PropSpecs[ModelIndex];
			const float DiameterCm = FCString::Atof(*S.Diameter);
			const float DiskArea = DiameterCm > 0.0f ? UE_PI * FMath::Square(DiameterCm * 0.005f) : 0.0f;
			FString ThrustDigits = S.StaticThrust;
			ThrustDigits.ReplaceInline(TEXT("~"), TEXT(""));
			const float StaticThrustKg = FCString::Atof(*ThrustDigits);
			const float DiskLoading = DiskArea > 0.0f ? StaticThrustKg / DiskArea : 0.0f;
			return FString::Printf(TEXT("%s\n%s  //  PROPELLER\n\nROTOR\nDiameter       %s\nPitch          %s\nSwept disk     %.2f m^2\n\nMASS / HUB\nBlade weight   %.1f kg\nHub            %s\n\nTHRUST\nStatic thrust  %s\nDisk loading   %.1f kg/m^2\n\n%s"),
				*S.Model, *S.Brand, *S.Diameter, *S.Pitch, DiskArea, S.Weight_kg, *S.HubSize,
				*S.StaticThrust, DiskLoading, *S.Notes);
		}
		case 3:
		{
			if (!GI->HarnessSpecs.IsValidIndex(ModelIndex)) return TEXT("No harness data available.");
			const FParamotorHarnessSpec& S = GI->HarnessSpecs[ModelIndex];
			return FString::Printf(TEXT("%s\n%s  //  HARNESS\n\nFIT\nAvailable sizes %s\n\nMASS\nHarness weight  %.1f kg\nBuild role      Pilot interface / load carriage\n\n%s"),
				*S.Model, *S.Brand, *S.Sizes, S.Weight_kg, *S.Notes);
		}
		case 4:
		{
			if (!GI->WingSpecs.IsValidIndex(ModelIndex)) return TEXT("No wing data available.");
			const FParamotorWingSpec& S = GI->WingSpecs[ModelIndex];
			const float WingArea = FCString::Atof(*S.Size);
			const float WingLoading = WingArea > 0.0f ? S.MaxWeightKg / WingArea : 0.0f;
			FString WingClass = TEXT("N/A");
			if (S.Notes.StartsWith(TEXT("EN-")))
			{
				WingClass = S.Notes.Left(4);
			}
			return FString::Printf(TEXT("%s\n%s  //  WING\n\nCERTIFICATION\nClass          %s\n\nSIZE / LOAD\nSize           %s\nWing loading   %.1f kg/m^2 at max\nMax pilot      %d kg / %d lb\n\nSPEED WINDOW\nTrim speeds    %s\nTop speed      %s\n\n%s"),
				*S.Model, *S.Brand, *WingClass, *S.Size, WingLoading, S.MaxWeightKg, S.MaxWeightLb,
				*S.TrimSpeeds, *S.TopSpeed, *S.Notes);
		}
	}
	return TEXT("");
}