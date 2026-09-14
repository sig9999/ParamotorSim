// Copyright Epic Games, Inc. All Rights Reserved.

#include "UW_FreeFlightMenu.h"
#include "ParamotorLogging.h"
#include "ParamotorGameInstance.h"
#include "ParamotorSaveGame.h"
#include "UIStyle.h"
#include "MenuWidgetHelpers.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Components/Border.h"
#include "Components/BorderSlot.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"

using namespace MenuUtil;

// ==== HANGAR TAB ====
void UUW_FreeFlightMenu::BuildHangarData()
{
	UE_LOG(LogParamotor, Log, TEXT("BuildHangarData: GameInstanceRef=%s"), IsValid(GameInstanceRef) ? TEXT("valid") : TEXT("NULL"));
	if (!IsValid(GameInstanceRef)) return;
	if (GameInstanceRef->EngineSpecs.Num() > 0) return;

	{
		FParamotorEngineSpec E;
		E.Brand = TEXT("EOS");
		E.Model = TEXT("EOS 100");
		E.Displacement_cc = 100.0f;
		E.Power_hp = 18.0f;
		E.Torque_Nm = 26.0f;
		E.Weight_kg = 14.2f;
		E.HubSize = TEXT("6-hole 75mm");
		E.StaticThrustByProp = TEXT("130cm ~72kg");
		E.Notes = TEXT("Compact, light 2-stroke. Great for light pilots and the 130cm EOS prop.");
		GameInstanceRef->EngineSpecs.Add(E);
	}

	if (GameInstanceRef->FrameSpecs.Num() == 0)
	{
		FParamotorFrameSpec F;
		F.Brand = TEXT("Kangook");
		F.Model = TEXT("Amaruk 140");
		F.Weight_kg = 4.31f;
		F.EngineMount = TEXT("EOS 100");
		F.Notes = TEXT("Black powder-coated aluminum cage frame — 1400mm ring, orange safety netting, stainless hardware. Real mesh by Codex.");
		GameInstanceRef->FrameSpecs.Add(F);
	}

	{
		FParamotorPropSpec P;
		P.Brand = TEXT("EOS");
		P.Model = TEXT("PT130 Carbon");
		P.Diameter = TEXT("130 cm");
		P.Pitch = TEXT("12.5 deg");
		P.Weight_kg = 0.95f;
		P.HubSize = TEXT("6-hole 75mm");
		P.StaticThrust = TEXT("~72 kg");
		P.Notes = TEXT("EOS P&T carbon 2-blade — real mesh by Codex, mounts to the EOS 100 prop socket.");
		GameInstanceRef->PropSpecs.Add(P);
	}

	{
		FParamotorHarnessSpec H;
		H.Brand = TEXT("Dudek");
		H.Model = TEXT("PowerSeat DP");
		H.Weight_kg = 3.09f;
		H.Sizes = TEXT("S / M");
		H.Notes = TEXT("PowerSeat Comfort DP — low-hang seat harness, 390mm seat, real mesh by Codex.");
		GameInstanceRef->HarnessSpecs.Add(H);
	}

	{
		FParamotorWingSpec W;
		W.Brand = TEXT("Macpara");
		W.Model = TEXT("Charger 25");
		W.Size = TEXT("25");
		W.MaxWeightKg = 100;
		W.MaxWeightLb = 220;
		W.TrimSpeeds = TEXT("39 / 43 / 47 km/h");
		W.TopSpeed = TEXT("~53 km/h");
		W.Notes = TEXT("EN-B mid-B wing. Good all-rounder for newer pilots.");
		GameInstanceRef->WingSpecs.Add(W);
	}
}

void UUW_FreeFlightMenu::BuildHangarPanel(UCanvasPanel* Panel)
{
	UTextBlock* Label = MakeText(WidgetTree, TEXT("Hangar"), FLinearColor(1.0f, 1.0f, 1.0f, 1.0f), 48, 6, 2, true, 0.5f);
	Panel->AddChild(Label);
	if (UCanvasPanelSlot* LS = Cast<UCanvasPanelSlot>(Label->Slot))
	{
		LS->SetAnchors(FAnchors(0.5f, 0.05f, 0.5f, 0.05f));
		LS->SetOffsets(FMargin(-300.0f, -40.0f, 600.0f, 80.0f));
	}

	UTextBlock* SubLabel = MakeText(WidgetTree, TEXT("Build your paramotor"), FLinearColor(0.75f, 0.85f, 0.95f, 0.9f), 20, 2, 0, true, 0.5f);
	Panel->AddChild(SubLabel);
	if (UCanvasPanelSlot* SLS = Cast<UCanvasPanelSlot>(SubLabel->Slot))
	{
		SLS->SetAnchors(FAnchors(0.5f, 0.105f, 0.5f, 0.105f));
		SLS->SetOffsets(FMargin(-300.0f, -15.0f, 600.0f, 30.0f));
	}

	auto AddComboLabel = [&](const TCHAR* T, float Y) -> UTextBlock*
	{
		UTextBlock* L = MakeText(WidgetTree, T, FLinearColor(0.8f, 0.9f, 1.0f, 1.0f), 22, 2, 0, true, 0.5f);
		Panel->AddChild(L);
		if (UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(L->Slot))
		{
			S->SetAnchors(FAnchors(0.18f, Y, 0.18f, Y));
			S->SetOffsets(FMargin(-140.0f, -18.0f, 280.0f, 36.0f));
		}
		return L;
	};

	auto AddCombo = [&](float Y, UComboBoxString*& Out) -> UComboBoxString*
	{
		UComboBoxString* C = WidgetTree->ConstructWidget<UComboBoxString>();
		Panel->AddChild(C);
		if (UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(C->Slot))
		{
			S->SetAnchors(FAnchors(0.18f, Y, 0.18f, Y));
			S->SetOffsets(FMargin(-140.0f, -20.0f, 280.0f, 40.0f));
		}
		Out = C;
		return C;
	};

	UBorder* SelectorCard = WidgetTree->ConstructWidget<UBorder>();
	SelectorCard->SetBrushColor(FLinearColor(0.03f, 0.05f, 0.07f, 1.0f));
	SelectorCard->SetPadding(FMargin(16.0f));
	Panel->AddChild(SelectorCard);
	if (UCanvasPanelSlot* SCS = Cast<UCanvasPanelSlot>(SelectorCard->Slot))
	{
		SCS->SetAnchors(FAnchors(0.18f, 0.45f, 0.18f, 0.45f));
		SCS->SetOffsets(FMargin(-160.0f, -260.0f, 320.0f, 540.0f));
	}

	AddComboLabel(TEXT("SECTION"), 0.22f);
	AddCombo(0.28f, HangarSectionCombo);
	AddComboLabel(TEXT("BRAND"), 0.38f);
	AddCombo(0.44f, HangarBrandCombo);
	AddComboLabel(TEXT("MODEL"), 0.54f);
	AddCombo(0.60f, HangarModelCombo);

	HangarSectionCombo->OnSelectionChanged.AddDynamic(this, &UUW_FreeFlightMenu::OnHangarSectionChanged);
	HangarBrandCombo->OnSelectionChanged.AddDynamic(this, &UUW_FreeFlightMenu::OnHangarBrandChanged);
	HangarModelCombo->OnSelectionChanged.AddDynamic(this, &UUW_FreeFlightMenu::OnHangarModelChanged);

	HangarEquipButton = WidgetTree->ConstructWidget<UButton>();
	HangarEquipButton->SetStyle(MakePanelStyle(
		FLinearColor(0.0f, 0.55f, 0.75f, 1.0f),
		FLinearColor(0.05f, 0.70f, 0.90f, 1.0f),
		FLinearColor(0.0f, 0.40f, 0.60f, 1.0f)));
	UTextBlock* EquipText = MakeText(WidgetTree, TEXT("EQUIP"), FLinearColor::White, 22, 3, 1, true, 0.5f);
	HangarEquipButton->AddChild(EquipText);
	if (UButtonSlot* BS = Cast<UButtonSlot>(EquipText->Slot))
	{
		BS->SetPadding(FMargin(0.0f, 8.0f, 0.0f, 8.0f));
		BS->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Center);
		BS->SetVerticalAlignment(EVerticalAlignment::VAlign_Center);
	}
	HangarEquipButton->OnClicked.AddDynamic(this, &UUW_FreeFlightMenu::OnHangarEquipClicked);
	Panel->AddChild(HangarEquipButton);
	if (UCanvasPanelSlot* BS = Cast<UCanvasPanelSlot>(HangarEquipButton->Slot))
	{
		BS->SetAnchors(FAnchors(0.18f, 0.74f, 0.18f, 0.74f));
		BS->SetOffsets(FMargin(-140.0f, -28.0f, 280.0f, 56.0f));
	}

	UBorder* SpecsBG = WidgetTree->ConstructWidget<UBorder>();
	SpecsBG->SetBrushColor(FLinearColor(0.05f, 0.07f, 0.10f, 1.0f));
	SpecsBG->SetPadding(FMargin(20.0f));
	Panel->AddChild(SpecsBG);
	if (UCanvasPanelSlot* BGS = Cast<UCanvasPanelSlot>(SpecsBG->Slot))
	{
		BGS->SetAnchors(FAnchors(0.62f, 0.48f, 0.62f, 0.48f));
		BGS->SetOffsets(FMargin(-320.0f, -20.0f, 640.0f, 600.0f));
	}

	HangarSpecs = WidgetTree->ConstructWidget<UTextBlock>();
	HangarSpecs->SetText(FText::FromString(TEXT("Select an item to view specs.")));
	HangarSpecs->SetColorAndOpacity(FLinearColor(0.95f, 0.97f, 1.0f, 1.0f));
	HangarSpecs->SetJustification(ETextJustify::Left);
	HangarSpecs->SetAutoWrapText(true);
	FSlateFontInfo Font = HangarSpecs->GetFont();
	Font.Size = 22;
	HangarSpecs->SetFont(Font);
	SpecsBG->AddChild(HangarSpecs);
	if (UBorderSlot* SS = Cast<UBorderSlot>(HangarSpecs->Slot))
	{
		SS->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
		SS->SetVerticalAlignment(EVerticalAlignment::VAlign_Top);
	}

	HangarBuild = MakeText(WidgetTree, TEXT("Current build: none"), FLinearColor(1.0f, 0.95f, 0.8f, 1.0f), 24, 3, 1, true, 0.5f);
	Panel->AddChild(HangarBuild);
	if (UCanvasPanelSlot* BLS = Cast<UCanvasPanelSlot>(HangarBuild->Slot))
	{
		BLS->SetAnchors(FAnchors(0.5f, 0.92f, 0.5f, 0.92f));
		BLS->SetOffsets(FMargin(-500.0f, -28.0f, 1000.0f, 56.0f));
	}

	HangarSection = 0;
	HangarBrand = 0;
	HangarModel = 0;

	HangarSectionCombo->ClearOptions();
	HangarSectionCombo->AddOption(GetSectionName(0));
	HangarSectionCombo->AddOption(GetSectionName(1));
	HangarSectionCombo->AddOption(GetSectionName(2));
	HangarSectionCombo->AddOption(GetSectionName(3));
	HangarSectionCombo->SetSelectedOption(GetSectionName(HangarSection));
	RefreshHangarBrands();
}

FString UUW_FreeFlightMenu::GetSectionName(int32 Section) const
{
	switch (Section)
	{
		case 0: return TEXT("WING");
		case 1: return TEXT("ENGINE");
		case 2: return TEXT("FRAME");
		case 3: return TEXT("PROP");
		default: return TEXT("WING");
	}
}

void UUW_FreeFlightMenu::RefreshHangarBrands()
{
	if (!HangarBrandCombo || !IsValid(GameInstanceRef)) return;
	HangarBrandCombo->ClearOptions();

	if (HangarSection == 0)
	{
		HangarBrandCombo->AddOption(TEXT("Macpara"));
	}
	else if (HangarSection == 1)
	{
		HangarBrandCombo->AddOption(TEXT("EOS"));
	}
	else if (HangarSection == 3)
	{
		HangarBrandCombo->AddOption(TEXT("EOS"));
	}

	HangarBrand = 0;
	if (HangarBrandCombo->GetOptionCount() > 0)
	{
		HangarBrandCombo->SetSelectedOption(HangarBrandCombo->GetOptionAtIndex(0));
	}
	RefreshHangarModels();
}

void UUW_FreeFlightMenu::RefreshHangarModels()
{
	if (!HangarModelCombo || !IsValid(GameInstanceRef)) return;
	HangarModelCombo->ClearOptions();

	if (HangarSection == 0)
	{
		if (HangarBrand == 0) { HangarModelCombo->AddOption(TEXT("Charger 25")); }
	}
	else if (HangarSection == 1)
	{
		if (HangarBrand == 0) { HangarModelCombo->AddOption(TEXT("EOS 100")); }
	}
	else if (HangarSection == 3)
	{
		HangarModelCombo->AddOption(TEXT("EOS 130"));
	}

	HangarModel = 0;
	if (HangarModelCombo->GetOptionCount() > 0)
	{
		HangarModelCombo->SetSelectedOption(HangarModelCombo->GetOptionAtIndex(0));
	}
	RefreshHangarDetails();
}

void UUW_FreeFlightMenu::RefreshHangarDetails()
{
	if (!HangarSpecs || !IsValid(GameInstanceRef)) return;

	FString Text;
	if (HangarSection == 0)
	{
		int32 Index = HangarModel + HangarBrand;
		if (GameInstanceRef->WingSpecs.IsValidIndex(Index))
		{
			Text = BuildWingDetails(Index);
		}
	}
	else if (HangarSection == 1)
	{
		int32 Index = HangarModel + HangarBrand;
		if (GameInstanceRef->EngineSpecs.IsValidIndex(Index))
		{
			Text = BuildEngineDetails(Index);
		}
	}
	else if (HangarSection == 2)
	{
		if (GameInstanceRef->FrameSpecs.IsValidIndex(0))
		{
			Text = BuildFrameDetails(0);
		}
	}
	else if (HangarSection == 3)
	{
		if (GameInstanceRef->PropSpecs.IsValidIndex(HangarModel))
		{
			Text = BuildPropDetails(HangarModel);
		}
	}

	HangarSpecs->SetText(FText::FromString(Text.IsEmpty() ? TEXT("Select an item to view specs.") : Text));

	FString Wing = GameInstanceRef->WingSpecs.IsValidIndex(GameInstanceRef->CurrentBuild.WingIndex)
		? GameInstanceRef->WingSpecs[GameInstanceRef->CurrentBuild.WingIndex].Model : TEXT("None");
	FString Engine = GameInstanceRef->EngineSpecs.IsValidIndex(GameInstanceRef->CurrentBuild.EngineIndex)
		? GameInstanceRef->EngineSpecs[GameInstanceRef->CurrentBuild.EngineIndex].Model : TEXT("None");
	FString Frame = GameInstanceRef->FrameSpecs.IsValidIndex(GameInstanceRef->CurrentBuild.FrameIndex)
		? GameInstanceRef->FrameSpecs[GameInstanceRef->CurrentBuild.FrameIndex].Model : TEXT("None");
	FString Prop = GameInstanceRef->PropSpecs.IsValidIndex(GameInstanceRef->CurrentBuild.PropIndex)
		? GameInstanceRef->PropSpecs[GameInstanceRef->CurrentBuild.PropIndex].Model : TEXT("None");
	if (HangarBuild)
	{
		HangarBuild->SetText(FText::FromString(FString::Printf(TEXT("Current build: %s / %s / %s / %s"), *Wing, *Engine, *Frame, *Prop)));
	}
}

void UUW_FreeFlightMenu::OnHangarSectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	if (SelectionType == ESelectInfo::Direct) return;
	if (HangarSectionCombo)
	{
		HangarSection = HangarSectionCombo->FindOptionIndex(SelectedItem);
	}
	RefreshHangarBrands();
}

void UUW_FreeFlightMenu::OnHangarBrandChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	if (SelectionType == ESelectInfo::Direct) return;
	if (HangarBrandCombo)
	{
		HangarBrand = HangarBrandCombo->FindOptionIndex(SelectedItem);
	}
	RefreshHangarModels();
}

void UUW_FreeFlightMenu::OnHangarModelChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	if (SelectionType == ESelectInfo::Direct) return;
	if (HangarModelCombo)
	{
		HangarModel = HangarModelCombo->FindOptionIndex(SelectedItem);
	}
	RefreshHangarDetails();
}

void UUW_FreeFlightMenu::OnHangarEquipClicked()
{
	if (!IsValid(GameInstanceRef)) return;
	if (HangarSection == 0) GameInstanceRef->CurrentBuild.WingIndex = HangarModel + HangarBrand;
	else if (HangarSection == 1) GameInstanceRef->CurrentBuild.EngineIndex = HangarModel + HangarBrand;
	else if (HangarSection == 2) GameInstanceRef->CurrentBuild.FrameIndex = 0;
	else if (HangarSection == 3) GameInstanceRef->CurrentBuild.PropIndex = HangarModel;
	RefreshHangarDetails();
}

FString UUW_FreeFlightMenu::BuildWingDetails(int32 Index) const
{
	const FParamotorWingSpec& Spec = GameInstanceRef->WingSpecs[Index];
	return FString::Printf(TEXT("%s %s\nSize: %s\nMax pilot weight: %d kg / %d lb\nTrim speeds: %s\nTop speed: %s\n\n%s"),
		*Spec.Brand, *Spec.Model, *Spec.Size, Spec.MaxWeightKg, Spec.MaxWeightLb, *Spec.TrimSpeeds, *Spec.TopSpeed, *Spec.Notes);
}

FString UUW_FreeFlightMenu::BuildEngineDetails(int32 Index) const
{
	const FParamotorEngineSpec& Spec = GameInstanceRef->EngineSpecs[Index];
	return FString::Printf(TEXT("%s %s\nDisplacement: %.0f cc\nPower: %.0f hp\nTorque: %.0f Nm\nWeight: %.1f kg\nHub: %s\nStatic thrust: %s\n\n%s"),
		*Spec.Brand, *Spec.Model, Spec.Displacement_cc, Spec.Power_hp, Spec.Torque_Nm, Spec.Weight_kg, *Spec.HubSize, *Spec.StaticThrustByProp, *Spec.Notes);
}

FString UUW_FreeFlightMenu::BuildFrameDetails(int32 Index) const
{
	const FParamotorFrameSpec& Spec = GameInstanceRef->FrameSpecs[Index];
	return FString::Printf(TEXT("%s %s\nWeight: %.1f kg\nEngine mount: %s\n\n%s"),
		*Spec.Brand, *Spec.Model, Spec.Weight_kg, *Spec.EngineMount, *Spec.Notes);
}

FString UUW_FreeFlightMenu::BuildPropDetails(int32 Index) const
{
	const FParamotorPropSpec& Spec = GameInstanceRef->PropSpecs[Index];
	return FString::Printf(TEXT("%s %s\nDiameter: %s\nPitch: %s\nWeight: %.1f kg\nHub: %s\nStatic thrust: %s\n\n%s"),
		*Spec.Brand, *Spec.Model, *Spec.Diameter, *Spec.Pitch, Spec.Weight_kg, *Spec.HubSize, *Spec.StaticThrust, *Spec.Notes);
}
