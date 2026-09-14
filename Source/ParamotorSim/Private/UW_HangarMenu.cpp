// Copyright Epic Games, Inc. All Rights Reserved.

#include "UW_HangarMenu.h"
#include "UW_WingSelect.h"
#include "CatalogItemWidget.h"
#include "ParamotorBuilder.h"
#include "ParamotorGameInstance.h"
#include "ParamotorSaveGame.h"
#include "UIStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Components/CanvasPanel.h"
#include "Components/TextBlock.h"
#include "Components/Border.h"
#include "Components/Image.h"


void UUW_HangarMenu::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	UWorld* World = GetWorld();
	if (World)
	{
		for (TActorIterator<AParamotorBuilder> It(World); It; ++It)
		{
			Builder = *It;
			break;
		}
	}

	GI = Cast<UParamotorGameInstance>(UGameplayStatics::GetGameInstance(this));
	if (!GI)
	{
		if (World)
		{
			UGameInstance* BaseGI = UGameplayStatics::GetGameInstance(this);
			if (!BaseGI || !BaseGI->IsA(UParamotorGameInstance::StaticClass()))
			{
				GI = NewObject<UParamotorGameInstance>(World);
				World->SetGameInstance(GI);
			}
			else
			{
				GI = Cast<UParamotorGameInstance>(BaseGI);
			}
		}
	}
	BuildHangarData();
	// The podium starts empty — nothing renders until the user equips a part.
	// Saved builds still restore; FParamotorBuild defaults every index to -1.
	LoadSavedBuild();
	// -emptypodium launch flag: force every part off — used to verify the
	// bare-podium state without clicking through the catalog.
	if (GI && FParse::Param(FCommandLine::Get(), TEXT("emptypodium")))
	{
		GI->CurrentBuild.FrameIndex   = -1;
		GI->CurrentBuild.EngineIndex  = -1;
		GI->CurrentBuild.PropIndex    = -1;
		GI->CurrentBuild.HarnessIndex = -1;
		GI->CurrentBuild.WingIndex    = -1;
	}
	// -fullbuild: equip the first model of every category — quick way to
	// verify the complete assembly without a save file.
	if (GI && FParse::Param(FCommandLine::Get(), TEXT("fullbuild")))
	{
		GI->CurrentBuild.FrameIndex   = GI->FrameSpecs.Num()   > 0 ? 0 : -1;
		GI->CurrentBuild.EngineIndex  = GI->EngineSpecs.Num()  > 0 ? 0 : -1;
		GI->CurrentBuild.PropIndex    = GI->PropSpecs.Num()    > 0 ? 0 : -1;
		GI->CurrentBuild.HarnessIndex = GI->HarnessSpecs.Num() > 0 ? 0 : -1;
		GI->CurrentBuild.WingIndex    = GI->WingSpecs.Num()    > 0 ? 0 : -1;
	}

	// -wings: open the wing catalog immediately — verifies the wing screen
	// without clicking SAVE BUILD. Deferred past the viewport's
	// InvalidateAllWidgets pass that runs during map-load settle (~0.6s),
	// which destroys widgets added inside NativeOnInitialized.
	if (FParse::Param(FCommandLine::Get(), TEXT("wings")))
	{
		FTimerHandle WingTimer;
		GetWorld()->GetTimerManager().SetTimer(WingTimer, this, &UUW_HangarMenu::OpenWingSelect, 1.0f, false);
	}

	RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>();
	WidgetTree->RootWidget = RootCanvas;

	SyncBuilderVisibility();
	RefreshUI();

	FTimerHandle SyncHandle;
	GetWorld()->GetTimerManager().SetTimer(SyncHandle, this, &UUW_HangarMenu::SyncBuilderVisibility, 0.5f, false);
}

void UUW_HangarMenu::RefreshUI()
{
	if (!RootCanvas) return;
	RootCanvas->ClearChildren();
	SpecText = nullptr;

	using namespace UIStyle;

	const float RailW   = 0.205f;  // left equipment rail (normalized)
	const float SheetX  = 0.78f;   // right build-sheet rail start
	const float BarY    = 0.895f;  // bottom action bar top edge

	// ---------- LEFT RAIL : equipment nav ----------
	UBorder* Rail = MakePanel(WidgetTree, PanelDeep);
	Rail->SetVisibility(ESlateVisibility::HitTestInvisible);
	PlaceOnCanvas(Rail, RootCanvas, FAnchors(0.0f, 0.0f, RailW, BarY), FMargin(0.0f), 1);

	UImage* RailEdge = MakeLine(WidgetTree, Edge);
	PlaceOnCanvas(RailEdge, RootCanvas, FAnchors(RailW, 0.0f, RailW, BarY), FMargin(0.0f, 0.0f, 1.0f, 0.0f), 2);

	UTextBlock* Kicker = MakeText(WidgetTree, TEXT("PARAMOTOR"), Accent, 12, 4, true);
	PlaceOnCanvas(Kicker, RootCanvas, FAnchors(0.0f, 0.0f, RailW, 0.0f), FMargin(26.0f, 30.0f, 20.0f, 18.0f), 3);

	UTextBlock* Title = MakeText(WidgetTree, TEXT("HANGAR"), Text, 38, 3, true);
	PlaceOnCanvas(Title, RootCanvas, FAnchors(0.0f, 0.0f, RailW, 0.0f), FMargin(24.0f, 46.0f, 20.0f, 48.0f), 3);

	UImage* TitleRule = MakeLine(WidgetTree, AccentSoft);
	PlaceOnCanvas(TitleRule, RootCanvas, FAnchors(0.0f, 0.0f, RailW, 0.0f), FMargin(26.0f, 100.0f, 110.0f, 2.0f), 3);

	UTextBlock* SecLabel = MakeText(WidgetTree, TEXT("EQUIPMENT"), TextFaint, 12, 4, true);
	PlaceOnCanvas(SecLabel, RootCanvas, FAnchors(0.0f, 0.0f, RailW, 0.0f), FMargin(26.0f, 130.0f, 20.0f, 18.0f), 3);

	float Y = 160.0f;
	// Paramotor build = frame/engine/prop/harness only — the wing lives on
	// its own catalog screen after SAVE BUILD.
	for (int32 i = 0; i < 4; ++i)
	{
		UCatalogItemWidget* Item = CreateWidget<UCatalogItemWidget>(this, UCatalogItemWidget::StaticClass());
		Item->SetItem(i, GetSectionName(i).ToUpper(), i == SelectedSection, false, 16);
		Item->SetSubtitle(GetSelectedModelNameForSection(i).ToUpper());
		Item->OnItemClicked.AddDynamic(this, &UUW_HangarMenu::OnSectionClicked);
		PlaceOnCanvas(Item, RootCanvas, FAnchors(0.0f, 0.0f, RailW, 0.0f), FMargin(10.0f, Y, 10.0f, 56.0f), 3);
		Y += 62.0f;
	}

	// ---------- GARAGE : saved builds ----------
	UTextBlock* GarLabel = MakeText(WidgetTree, TEXT("GARAGE"), TextFaint, 12, 4, true);
	PlaceOnCanvas(GarLabel, RootCanvas, FAnchors(0.0f, 0.0f, RailW, 0.0f), FMargin(26.0f, Y + 16.0f, 20.0f, 18.0f), 3);

	UCatalogItemWidget* GarItem = CreateWidget<UCatalogItemWidget>(this, UCatalogItemWidget::StaticClass());
	const int32 GarageCount = GetOrCreateSave() ? GetOrCreateSave()->Garage.Num() : 0;
	GarItem->SetItem(90, TEXT("SAVED BUILDS"), false, false, 16);
	GarItem->SetSubtitle(GarageCount > 0 ? *FString::Printf(TEXT("%d SAVED"), GarageCount) : TEXT("EMPTY"));
	GarItem->OnItemClicked.AddDynamic(this, &UUW_HangarMenu::OnSectionClicked);
	PlaceOnCanvas(GarItem, RootCanvas, FAnchors(0.0f, 0.0f, RailW, 0.0f), FMargin(10.0f, Y + 40.0f, 10.0f, 56.0f), 3);

	UTextBlock* RailHint = MakeText(WidgetTree, TEXT("Select a category to open the catalog"), TextFaint, 11, 0, false);
	RailHint->SetAutoWrapText(true);
	PlaceOnCanvas(RailHint, RootCanvas, FAnchors(0.0f, BarY, RailW, BarY), FMargin(26.0f, -38.0f, 20.0f, 24.0f), 3);

	// ---------- CENTER : podium preview ----------
	UTextBlock* PreviewLabel = MakeText(WidgetTree, TEXT("PODIUM PREVIEW"), TextFaint, 12, 4, true);
	PlaceOnCanvas(PreviewLabel, RootCanvas, FAnchors(RailW, 0.0f, SheetX, 0.0f), FMargin(26.0f, 30.0f, 40.0f, 18.0f), 3);

	const bool bHasFrame = GI && GI->FrameSpecs.IsValidIndex(GI->CurrentBuild.FrameIndex);
	UTextBlock* PodiumStatus = MakeText(WidgetTree,
		bHasFrame ? TEXT("LIVE ROTATING BUILD") : TEXT("SELECT A FRAME TO BEGIN"),
		bHasFrame ? Accent : Warning, 13, 2, true);
	PlaceOnCanvas(PodiumStatus, RootCanvas, FAnchors(RailW, BarY, SheetX, BarY), FMargin(26.0f, -38.0f, 40.0f, 20.0f), 3);

	// ---------- RIGHT RAIL : build sheet ----------
	UBorder* Sheet = MakePanel(WidgetTree, PanelDeep);
	Sheet->SetVisibility(ESlateVisibility::HitTestInvisible);
	PlaceOnCanvas(Sheet, RootCanvas, FAnchors(SheetX, 0.0f, 1.0f, BarY), FMargin(0.0f), 1);

	UImage* SheetEdge = MakeLine(WidgetTree, Edge);
	PlaceOnCanvas(SheetEdge, RootCanvas, FAnchors(SheetX, 0.0f, SheetX, BarY), FMargin(0.0f, 0.0f, 1.0f, 0.0f), 2);

	UTextBlock* SheetKicker = MakeText(WidgetTree, TEXT("CURRENT BUILD"), Accent, 12, 4, true);
	PlaceOnCanvas(SheetKicker, RootCanvas, FAnchors(SheetX, 0.0f, 1.0f, 0.0f), FMargin(26.0f, 30.0f, 20.0f, 18.0f), 3);

	UTextBlock* SheetTitle = MakeText(WidgetTree, TEXT("BUILD SHEET"), Text, 28, 3, true);
	PlaceOnCanvas(SheetTitle, RootCanvas, FAnchors(SheetX, 0.0f, 1.0f, 0.0f), FMargin(24.0f, 48.0f, 20.0f, 38.0f), 3);

	UImage* SheetRule = MakeLine(WidgetTree, AccentSoft);
	PlaceOnCanvas(SheetRule, RootCanvas, FAnchors(SheetX, 0.0f, 1.0f, 0.0f), FMargin(26.0f, 100.0f, 96.0f, 2.0f), 3);

	float SY = 128.0f;
	float TotalWeight = 0.0f;
	auto AddWeight = [&](const auto& Specs, int32 Idx)
	{
		if (Specs.IsValidIndex(Idx)) TotalWeight += Specs[Idx].Weight_kg;
	};
	if (GI)
	{
		AddWeight(GI->FrameSpecs,   GI->CurrentBuild.FrameIndex);
		AddWeight(GI->EngineSpecs,  GI->CurrentBuild.EngineIndex);
		AddWeight(GI->PropSpecs,    GI->CurrentBuild.PropIndex);
		AddWeight(GI->HarnessSpecs, GI->CurrentBuild.HarnessIndex);
	}

	for (int32 i = 0; i < 4; ++i)
	{
		UTextBlock* SLbl = MakeText(WidgetTree, *GetSectionName(i).ToUpper(), TextFaint, 11, 3, false);
		PlaceOnCanvas(SLbl, RootCanvas, FAnchors(SheetX, 0.0f, 1.0f, 0.0f), FMargin(26.0f, SY, 20.0f, 16.0f), 3);

		const FString Model = GetSelectedModelNameForSection(i);
		const bool bHas = !Model.IsEmpty() && Model != TEXT("None");
		UTextBlock* SVal = MakeText(WidgetTree,
			bHas ? *Model.ToUpper() : TEXT("— NOT EQUIPPED"),
			bHas ? Text : TextFaint, 15, 0, false);
		PlaceOnCanvas(SVal, RootCanvas, FAnchors(SheetX, 0.0f, 1.0f, 0.0f), FMargin(26.0f, SY + 16.0f, 20.0f, 20.0f), 3);
		SY += 48.0f;
	}

	// Divider + total weight
	UImage* Div = MakeLine(WidgetTree, Edge);
	PlaceOnCanvas(Div, RootCanvas, FAnchors(SheetX, 0.0f, 1.0f, 0.0f), FMargin(26.0f, SY + 6.0f, 26.0f, 1.0f), 3);
	SY += 20.0f;

	UTextBlock* WtLbl = MakeText(WidgetTree, TEXT("EST. DRY WEIGHT"), TextFaint, 11, 3, false);
	PlaceOnCanvas(WtLbl, RootCanvas, FAnchors(SheetX, 0.0f, 1.0f, 0.0f), FMargin(26.0f, SY, 20.0f, 16.0f), 3);

	UTextBlock* WtVal = MakeText(WidgetTree,
		TotalWeight > 0.0f ? *FString::Printf(TEXT("%.1f kg"), TotalWeight) : TEXT("—"),
		Accent, 20, 0, false);
	PlaceOnCanvas(WtVal, RootCanvas, FAnchors(SheetX, 0.0f, 1.0f, 0.0f), FMargin(26.0f, SY + 16.0f, 20.0f, 26.0f), 3);

	// Status line (save feedback etc.) — doubles as SpecText target
	SpecText = MakeText(WidgetTree, TEXT("Equipment changes preview on the podium."), TextDim, 12, 0, false);
	SpecText->SetAutoWrapText(true);
	PlaceOnCanvas(SpecText, RootCanvas, FAnchors(SheetX, BarY, 1.0f, BarY), FMargin(26.0f, -44.0f, 20.0f, 36.0f), 3);

	// ---------- BOTTOM ACTION BAR ----------
	UBorder* Bar = MakePanel(WidgetTree, PanelDeep);
	Bar->SetVisibility(ESlateVisibility::HitTestInvisible);
	PlaceOnCanvas(Bar, RootCanvas, FAnchors(0.0f, BarY, 1.0f, 1.0f), FMargin(0.0f), 4);

	UImage* BarTop = MakeLine(WidgetTree, Edge);
	PlaceOnCanvas(BarTop, RootCanvas, FAnchors(0.0f, BarY, 1.0f, BarY), FMargin(0.0f, 0.0f, 0.0f, 1.0f), 5);

	UButton* BackBtn = MakeGhostButton(WidgetTree, TEXT("< BACK TO WORLD"), 16, TextDim);
	BackBtn->OnClicked.AddDynamic(this, &UUW_HangarMenu::OnBackClicked);
	PlaceOnCanvas(BackBtn, RootCanvas, FAnchors(0.0f, BarY, 0.0f, BarY), FMargin(16.0f, 24.0f, 280.0f, 44.0f), 6);

	// Centered control hint — balances the bottom bar between BACK and SAVE.
	UTextBlock* DragHint = MakeText(WidgetTree, TEXT("DRAG TO ROTATE  ·  SCROLL TO ZOOM"), TextFaint, 12, 3, true);
	DragHint->SetVisibility(ESlateVisibility::HitTestInvisible);
	PlaceOnCanvas(DragHint, RootCanvas, FAnchors(0.5f, BarY, 0.5f, BarY), FMargin(-200.0f, 36.0f, 400.0f, 20.0f), 6);

	UButton* SaveBtn = MakeActionButton(WidgetTree, TEXT("SAVE  ·  CHOOSE WING →"), 15);
	SaveBtn->OnClicked.AddDynamic(this, &UUW_HangarMenu::OnSaveClicked);
	PlaceOnCanvas(SaveBtn, RootCanvas, FAnchors(1.0f, BarY, 1.0f, BarY), FMargin(-296.0f, 24.0f, 280.0f, 44.0f), 6);

	if (bCatalogOpen)
	{
		BuildCatalogOverlay(1600.0f);
	}
	if (bGarageOpen)
	{
		BuildGarageOverlay(1600.0f);
	}
	SyncBuilderVisibility();
}


void UUW_HangarMenu::OpenWingSelect()
{
	if (UUW_WingSelect* WingSel = CreateWidget<UUW_WingSelect>(this, UUW_WingSelect::StaticClass()))
	{
		WingSel->AddToViewport(100);
	}
}


void UUW_HangarMenu::OnBackClicked()
{
	UGameplayStatics::OpenLevel(this, FName(TEXT("LV_World")));
}


UParamotorSaveGame* UUW_HangarMenu::GetOrCreateSave()
{
	UParamotorSaveGame* Save = Cast<UParamotorSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("ParamotorBuildSlot"), 0));
	if (!Save)
	{
		Save = NewObject<UParamotorSaveGame>(this);
	}
	return Save;
}

float UUW_HangarMenu::GarageWeight(const FParamotorBuild& Build) const
{
	float W = 0.0f;
	if (!GI) return W;
	auto Add = [&](const auto& Specs, int32 Idx)
	{
		if (Specs.IsValidIndex(Idx)) W += Specs[Idx].Weight_kg;
	};
	Add(GI->FrameSpecs,   Build.FrameIndex);
	Add(GI->EngineSpecs,  Build.EngineIndex);
	Add(GI->PropSpecs,    Build.PropIndex);
	Add(GI->HarnessSpecs, Build.HarnessIndex);
	return W;
}


EHangarPart UUW_HangarMenu::SectionToPart(int32 Section) const
{
	switch (Section)
	{
		case 0: return EHangarPart::Frame;
		case 1: return EHangarPart::Engine;
		case 2: return EHangarPart::Prop;
		case 3: return EHangarPart::Harness;
		case 4: return EHangarPart::Wing;
	}
	return EHangarPart::Frame;
}

int32 UUW_HangarMenu::GetCurrentBuildIndexForSection(int32 Section) const
{
	if (!GI) return -1;
	switch (Section)
	{
		case 0: return GI->CurrentBuild.FrameIndex;
		case 1: return GI->CurrentBuild.EngineIndex;
		case 2: return GI->CurrentBuild.PropIndex;
		case 3: return GI->CurrentBuild.HarnessIndex;
		case 4: return GI->CurrentBuild.WingIndex;
	}
	return -1;
}


TArray<FString> UUW_HangarMenu::GetBrandsForSection(int32 Section) const
{
	TArray<FString> Brands;
	if (!GI) return Brands;

	auto AddBrand = [&](const FString& Brand)
	{
		if (!Brands.Contains(Brand))
		{
			Brands.Add(Brand);
		}
	};

	switch (Section)
	{
		case 0: for (const auto& S : GI->FrameSpecs)   AddBrand(S.Brand); break;
		case 1: for (const auto& S : GI->EngineSpecs)  AddBrand(S.Brand); break;
		case 2: for (const auto& S : GI->PropSpecs)    AddBrand(S.Brand); break;
		case 3: for (const auto& S : GI->HarnessSpecs) AddBrand(S.Brand); break;
		case 4: for (const auto& S : GI->WingSpecs)   AddBrand(S.Brand); break;
	}
	return Brands;
}

TArray<int32> UUW_HangarMenu::GetModelIndicesForBrand(int32 Section, const FString& Brand) const
{
	TArray<int32> Indices;
	if (!GI) return Indices;

	auto Collect = [&](const auto& Specs, const FString& B)
	{
		for (int32 i = 0; i < Specs.Num(); ++i)
		{
			if (B.IsEmpty() || Specs[i].Brand == B)
			{
				Indices.Add(i);
			}
		}
	};

	switch (Section)
	{
		case 0: Collect(GI->FrameSpecs,   Brand); break;
		case 1: Collect(GI->EngineSpecs,  Brand); break;
		case 2: Collect(GI->PropSpecs,    Brand); break;
		case 3: Collect(GI->HarnessSpecs, Brand); break;
		case 4: Collect(GI->WingSpecs,    Brand); break;
	}
	return Indices;
}


void UUW_HangarMenu::SyncBuilderVisibility()
{
	if (!Builder)
	{
		UWorld* World = GetWorld();
		if (World)
		{
			for (TActorIterator<AParamotorBuilder> It(World); It; ++It)
			{
				Builder = *It;
				break;
			}
		}
	}

	if (Builder)
	{
		auto ShouldShow = [&](int32 Section) -> bool
		{
			return GetCurrentBuildIndexForSection(Section) >= 0;
		};
		// No frame = no paramotor: every other part bolts onto the frame, so
		// removing it clears the whole assembly from the podium. The prop
		// likewise needs the engine's prop shaft to exist.
		const bool bHasFrame  = ShouldShow(0);
		const bool bHasEngine = ShouldShow(1);
		Builder->SetPartVisible(EHangarPart::Frame,   bHasFrame);
		Builder->SetPartVisible(EHangarPart::Engine,  bHasFrame && bHasEngine);
		Builder->SetPartVisible(EHangarPart::Prop,    bHasFrame && bHasEngine && ShouldShow(2));
		Builder->SetPartVisible(EHangarPart::Harness, bHasFrame && ShouldShow(3));
		// No pilot mannequin — the showroom harness always displays empty
		// (straps out, seat folded).
		Builder->SetPilotVisible(false);
		// The wing is configured on its own catalog screen — never on the
		// podium (a deployed canopy would fill the whole preview frame).
		Builder->SetPartVisible(EHangarPart::Wing,    false);
	}
}

void UUW_HangarMenu::BuildHangarData()
{
	if (!GI) return;
	// Spec tables live on the game instance now — shared by the hangar and
	// the world spawn paths.
	GI->EnsureSpecData();

	SelectedSection = 0;
	SelectedBrand.Empty();
	SelectedModelIndex = -1;
}
