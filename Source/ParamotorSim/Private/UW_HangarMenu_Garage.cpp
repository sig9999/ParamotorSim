// Copyright Epic Games, Inc. All Rights Reserved.

#include "UW_HangarMenu.h"
#include "UW_WingSelect.h"
#include "IndexedButton.h"
#include "ParamotorGameInstance.h"
#include "ParamotorSaveGame.h"
#include "UIStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Components/CanvasPanel.h"
#include "Components/TextBlock.h"
#include "Components/Border.h"
#include "Components/Image.h"

// ==== SAVED-BUILDS GARAGE ====

FString UUW_HangarMenu::GarageSummary(const FParamotorBuild& Build) const
{
	auto Name = [&](int32 Section, int32 Idx) -> FString
	{
		return Idx >= 0 ? GetModelNameForSpec(Section, Idx).ToUpper() : TEXT("—");
	};
	return FString::Printf(TEXT("%s · %s · %s · %s   |   WING: %s"),
		*Name(0, Build.FrameIndex), *Name(1, Build.EngineIndex),
		*Name(2, Build.PropIndex), *Name(3, Build.HarnessIndex),
		*Name(4, Build.WingIndex));
}
void UUW_HangarMenu::BuildGarageOverlay(float ViewW)
{
	using namespace UIStyle;
	UParamotorSaveGame* Save = GetOrCreateSave();

	UBorder* Overlay = MakePanel(WidgetTree, PanelDeep);
	PlaceOnCanvas(Overlay, RootCanvas, FAnchors(0.0f, 0.0f, 1.0f, 1.0f), FMargin(0.0f), 8);

	UTextBlock* Kicker = MakeText(WidgetTree, TEXT("GARAGE"), Accent, 12, 4, true);
	PlaceOnCanvas(Kicker, RootCanvas, FAnchors(0.03f, 0.0f, 0.6f, 0.0f), FMargin(0.0f, 30.0f, 0.0f, 18.0f), 9);

	UTextBlock* Title = MakeText(WidgetTree, TEXT("SAVED BUILDS"), Text, 30, 3, true);
	PlaceOnCanvas(Title, RootCanvas, FAnchors(0.03f, 0.0f, 0.6f, 0.0f), FMargin(0.0f, 48.0f, 0.0f, 42.0f), 9);

	UImage* Rule = MakeLine(WidgetTree, AccentSoft);
	PlaceOnCanvas(Rule, RootCanvas, FAnchors(0.03f, 0.0f, 0.6f, 0.0f), FMargin(0.0f, 100.0f, 110.0f, 2.0f), 9);

	// Close + save-as-new controls
	UButton* CloseBtn = MakeGhostButton(WidgetTree, TEXT("×  CLOSE"), 14, TextDim);
	CloseBtn->OnClicked.AddDynamic(this, &UUW_HangarMenu::OnGarageCloseClicked);
	PlaceOnCanvas(CloseBtn, RootCanvas, FAnchors(0.97f, 0.0f, 0.97f, 0.0f), FMargin(-140.0f, 30.0f, 140.0f, 40.0f), 9);

	UButton* SaveNewBtn = MakeActionButton(WidgetTree, TEXT("+ SAVE CURRENT BUILD"), 14);
	SaveNewBtn->OnClicked.AddDynamic(this, &UUW_HangarMenu::OnGarageSaveNewClicked);
	PlaceOnCanvas(SaveNewBtn, RootCanvas, FAnchors(0.97f, 0.0f, 0.97f, 0.0f), FMargin(-400.0f, 30.0f, 240.0f, 40.0f), 9);

	if (!Save || Save->Garage.Num() == 0)
	{
		UTextBlock* Empty = MakeText(WidgetTree,
			TEXT("NO SAVED BUILDS YET — CONFIGURE THE RIG, THEN HIT SAVE CURRENT BUILD."),
			TextDim, 14, 3, true);
		PlaceOnCanvas(Empty, RootCanvas, FAnchors(0.03f, 0.45f, 0.97f, 0.45f), FMargin(0.0f), 9);
		return;
	}

	// One card per saved build — name/date left, parts + weight, LOAD/DELETE.
	float RowY = 124.0f;
	for (int32 i = 0; i < Save->Garage.Num(); ++i)
	{
		const FSavedBuildEntry& E = Save->Garage[i];
		if (RowY + 84.0f > 620.0f) break; // simple clip — plenty of rows fit

		UBorder* RowCard = MakePanel(WidgetTree, Panel);
		RowCard->SetVisibility(ESlateVisibility::HitTestInvisible);
		PlaceOnCanvas(RowCard, RootCanvas, FAnchors(0.03f, 0.0f, 0.97f, 0.0f), FMargin(0.0f, RowY, 0.0f, 84.0f), 9);

		UTextBlock* NameTxt = MakeText(WidgetTree, *E.Name.ToUpper(), Text, 16, 2, true);
		PlaceOnCanvas(NameTxt, RootCanvas, FAnchors(0.03f, 0.0f, 0.97f, 0.0f), FMargin(18.0f, RowY + 10.0f, 400.0f, 22.0f), 10);

		UTextBlock* WhenTxt = MakeText(WidgetTree, *E.SavedWhen, TextFaint, 11, 1, false);
		PlaceOnCanvas(WhenTxt, RootCanvas, FAnchors(0.03f, 0.0f, 0.97f, 0.0f), FMargin(18.0f, RowY + 34.0f, 400.0f, 16.0f), 10);

		UTextBlock* PartsTxt = MakeText(WidgetTree, *GarageSummary(E.Build), TextDim, 12, 0, false);
		PlaceOnCanvas(PartsTxt, RootCanvas, FAnchors(0.03f, 0.0f, 0.97f, 0.0f), FMargin(18.0f, RowY + 54.0f, 400.0f, 18.0f), 10);

		UTextBlock* WtTxt = MakeText(WidgetTree,
			*FString::Printf(TEXT("%.1f kg"), E.TotalWeightKg), Accent, 15, 0, true);
		PlaceOnCanvas(WtTxt, RootCanvas, FAnchors(0.97f, 0.0f, 0.97f, 0.0f), FMargin(-360.0f, RowY + 10.0f, 100.0f, 22.0f), 10);

		const FButtonStyle Ghost = FlatButtonStyle(
			FLinearColor(0, 0, 0, 0), FLinearColor(1, 1, 1, 0.06f), FLinearColor(1, 1, 1, 0.12f));
		UIndexedButton* LoadBtn = CreateWidget<UIndexedButton>(this, UIndexedButton::StaticClass());
		LoadBtn->SetIndex(i);
		LoadBtn->SetButtonStyle(Ghost);
		UIndexedButton* DelBtn = CreateWidget<UIndexedButton>(this, UIndexedButton::StaticClass());
		DelBtn->SetIndex(i);
		DelBtn->SetButtonStyle(Ghost);

		UTextBlock* LoadTxt = MakeText(WidgetTree, TEXT("LOAD"), TextDim, 13, 2, true);
		LoadTxt->SetJustification(ETextJustify::Center);
		LoadBtn->SetContent(LoadTxt);
		LoadBtn->OnIndexedClicked.AddDynamic(this, &UUW_HangarMenu::OnGarageLoadClicked);
		PlaceOnCanvas(LoadBtn, RootCanvas, FAnchors(0.97f, 0.0f, 0.97f, 0.0f), FMargin(-250.0f, RowY + 22.0f, 110.0f, 40.0f), 10);

		UTextBlock* DelTxt = MakeText(WidgetTree, TEXT("DELETE"), Warning, 13, 2, true);
		DelTxt->SetJustification(ETextJustify::Center);
		DelBtn->SetContent(DelTxt);
		DelBtn->OnIndexedClicked.AddDynamic(this, &UUW_HangarMenu::OnGarageDeleteClicked);
		PlaceOnCanvas(DelBtn, RootCanvas, FAnchors(0.97f, 0.0f, 0.97f, 0.0f), FMargin(-130.0f, RowY + 22.0f, 110.0f, 40.0f), 10);

		RowY += 94.0f;
	}
}
void UUW_HangarMenu::OnGarageCloseClicked()
{
	bGarageOpen = false;
	RefreshUI();
}
void UUW_HangarMenu::OnGarageSaveNewClicked()
{
	if (!GI) return;
	UParamotorSaveGame* Save = GetOrCreateSave();
	if (!Save) return;

	FSavedBuildEntry E;
	E.Name = FString::Printf(TEXT("BUILD %02d"), Save->Garage.Num() + 1);
	E.SavedWhen = FDateTime::Now().ToString(TEXT("%b %d, %Y  %H:%M"));
	E.Build = GI->CurrentBuild;
	E.TotalWeightKg = GarageWeight(GI->CurrentBuild);
	Save->Garage.Add(E);
	UGameplayStatics::SaveGameToSlot(Save, TEXT("ParamotorBuildSlot"), 0);
	RefreshUI();
}
void UUW_HangarMenu::OnGarageLoadClicked(int32 Index)
{
	if (!GI) return;
	UParamotorSaveGame* Save = GetOrCreateSave();
	if (!Save || !Save->Garage.IsValidIndex(Index)) return;

	GI->CurrentBuild = Save->Garage[Index].Build;
	bGarageOpen = false;
	// Re-spawn every part so the loaded build replaces whatever is on the podium.
	if (Builder)
	{
		for (int32 i = 0; i < 4; ++i)
		{
			Builder->SetPart(SectionToPart(i));
		}
	}
	SyncBuilderVisibility();
	RefreshUI();
}
void UUW_HangarMenu::OnGarageDeleteClicked(int32 Index)
{
	UParamotorSaveGame* Save = GetOrCreateSave();
	if (!Save || !Save->Garage.IsValidIndex(Index)) return;
	Save->Garage.RemoveAt(Index);
	UGameplayStatics::SaveGameToSlot(Save, TEXT("ParamotorBuildSlot"), 0);
	RefreshUI();
}
bool UUW_HangarMenu::LoadSavedBuild()
{
	if (!GI) return false;
	UParamotorSaveGame* Save = Cast<UParamotorSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("ParamotorBuildSlot"), 0));
	if (Save)
	{
		GI->CurrentBuild = Save->CurrentBuild;
		return true;
	}
	return false;
}
void UUW_HangarMenu::SaveCurrentBuild()
{
	if (!GI) return;
	UParamotorSaveGame* Save = Cast<UParamotorSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("ParamotorBuildSlot"), 0));
	if (!Save)
	{
		Save = NewObject<UParamotorSaveGame>();
	}
	Save->CurrentBuild = GI->CurrentBuild;
	if (UGameplayStatics::SaveGameToSlot(Save, TEXT("ParamotorBuildSlot"), 0))
	{
		if (SpecText)
		{
			SpecText->SetText(FText::FromString(TEXT("Configuration saved.")));
		}
	}
	else
	{
		if (SpecText)
		{
			SpecText->SetText(FText::FromString(TEXT("Save failed.")));
		}
	}
}
void UUW_HangarMenu::OnSaveClicked()
{
	SaveCurrentBuild();
	// Paramotor build saved — the wing is chosen on its own catalog screen.
	OpenWingSelect();
}