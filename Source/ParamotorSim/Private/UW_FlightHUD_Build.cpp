// Copyright Epic Games, Inc. All Rights Reserved.

#include "UW_FlightHUD.h"
#include "HUDPalette.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Components/Border.h"
#include "Components/BorderSlot.h"
#include "Components/VerticalBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ProgressBar.h"
#include "GameFramework/PlayerController.h"

// ==== HUD CONSTRUCTION ====

FString UUW_FlightHUD::HeadingToCardinal(float HeadingDeg)
{
	int32 H = FMath::RoundToInt(HeadingDeg);
	while (H < 0) H += 360;
	while (H >= 360) H -= 360;
	if (H >= 337 || H < 23)  return TEXT("N");
	if (H < 67)  return TEXT("NE");
	if (H < 113) return TEXT("E");
	if (H < 157) return TEXT("SE");
	if (H < 203) return TEXT("S");
	if (H < 247) return TEXT("SW");
	if (H < 293) return TEXT("W");
	return TEXT("NW");
}

void UUW_FlightHUD::AddStatRow(UVerticalBox* Parent, const FString& Label, UTextBlock*& ValuePtr, int32 ValSize)
{
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), FName(*(Label + TEXT("Row"))));

	UTextBlock* Lbl = MakeText(Label + "Lbl", Label, 9, FSlateColor(CLR_TEXT_LABEL));
	Row->AddChild(Lbl);
	if (UHorizontalBoxSlot* HS = Cast<UHorizontalBoxSlot>(Lbl->Slot))
	{
		HS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		HS->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
		HS->SetVerticalAlignment(VAlign_Center);
	}

	UTextBlock* Val = MakeText(Label + "Val", TEXT("---"), ValSize, FSlateColor(CLR_TEXT));
	Val->SetJustification(ETextJustify::Right);
	Row->AddChild(Val);
	if (UHorizontalBoxSlot* HS = Cast<UHorizontalBoxSlot>(Val->Slot))
	{
		HS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		HS->SetVerticalAlignment(VAlign_Center);
	}

	AddToVBox(Parent, Row, 4.0f);
	ValuePtr = Val;
}

void UUW_FlightHUD::AddSeparator(UVerticalBox* Parent)
{
	UBorder* Sep = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Sep"));
	Sep->SetBrushColor(FLinearColor(CLR_ACCENT_DIM.R, CLR_ACCENT_DIM.G, CLR_ACCENT_DIM.B, 0.3f));
	Sep->SetPadding(FMargin(0.0f));
	AddToVBox(Parent, Sep, 6.0f);
}

void UUW_FlightHUD::AddGaugeRow(UVerticalBox* Parent, const FString& Label, UTextBlock*& ValuePtr, UProgressBar*& BarPtr, FLinearColor BarColor)
{
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), FName(*(Label + TEXT("GaugeRow"))));

	UTextBlock* Lbl = MakeText(Label + "Lbl", Label, 9, FSlateColor(CLR_TEXT_LABEL));
	Row->AddChild(Lbl);
	if (UHorizontalBoxSlot* HS = Cast<UHorizontalBoxSlot>(Lbl->Slot))
	{
		HS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		HS->SetPadding(FMargin(0.0f, 0.0f, 6.0f, 0.0f));
		HS->SetVerticalAlignment(VAlign_Center);
	}

	UTextBlock* Val = MakeText(Label + "Val", TEXT("---"), 13, FSlateColor(CLR_TEXT));
	Val->SetJustification(ETextJustify::Right);
	Row->AddChild(Val);
	if (UHorizontalBoxSlot* HS = Cast<UHorizontalBoxSlot>(Val->Slot))
	{
		HS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		HS->SetPadding(FMargin(0.0f, 0.0f, 6.0f, 0.0f));
		HS->SetVerticalAlignment(VAlign_Center);
	}

	UProgressBar* Bar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), FName(*(Label + TEXT("Bar"))));
	Bar->SetPercent(0.0f);
	Bar->SetFillColorAndOpacity(BarColor);
	Row->AddChild(Bar);
	if (UHorizontalBoxSlot* HS = Cast<UHorizontalBoxSlot>(Bar->Slot))
	{
		HS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		HS->SetVerticalAlignment(VAlign_Center);
	}

	AddToVBox(Parent, Row, 5.0f);
	ValuePtr = Val;
	BarPtr = Bar;
}

void UUW_FlightHUD::BuildFlightPanel(UCanvasPanel* Root)
{
	UBorder* FlightPanel = MakePanel(TEXT("FlightPanel"));
	Root->AddChild(FlightPanel);
	if (UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(FlightPanel->Slot))
	{
		S->SetAnchors(FAnchors(1.0f, 0.0f, 1.0f, 0.0f));
		S->SetPosition(FVector2D(-240.0f, 15.0f));
		S->SetSize(FVector2D(225.0f, 305.0f));
	}

	UVerticalBox* FlightVBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("FlightVBox"));
	FlightPanel->AddChild(FlightVBox);
	if (UBorderSlot* BS = Cast<UBorderSlot>(FlightVBox->Slot))
	{
		BS->SetPadding(FMargin(0.0f));
		BS->SetHorizontalAlignment(HAlign_Fill);
		BS->SetVerticalAlignment(VAlign_Fill);
	}

	// Compass section
	UTextBlock* CompassHdr = MakeHeader(TEXT("CompassHdr"), TEXT("HEADING"));
	AddToVBox(FlightVBox, CompassHdr, 4.0f);

	// Cardinal direction (big) + degrees (small) in a row
	UHorizontalBox* CompassRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("CompassRow"));
	CompassText = MakeText(TEXT("CompassCard"), TEXT("---"), 28, FSlateColor(CLR_HIGHLIGHT));
	CompassText->SetJustification(ETextJustify::Left);
	CompassRow->AddChild(CompassText);
	if (UHorizontalBoxSlot* HS = Cast<UHorizontalBoxSlot>(CompassText->Slot))
	{
		HS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		HS->SetVerticalAlignment(VAlign_Center);
	}
	HeadingText = MakeText(TEXT("CompassDeg"), TEXT("0\x00B0"), 13, FSlateColor(CLR_TEXT_DIM));
	HeadingText->SetJustification(ETextJustify::Right);
	CompassRow->AddChild(HeadingText);
	if (UHorizontalBoxSlot* HS = Cast<UHorizontalBoxSlot>(HeadingText->Slot))
	{
		HS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		HS->SetVerticalAlignment(VAlign_Center);
	}
	AddToVBox(FlightVBox, CompassRow, 3.0f);

	// Wind info
	WindInfoText = MakeText(TEXT("WindInfo"), TEXT("Wind: --- 0mph"), 11, FSlateColor(FLinearColor(0.4f, 0.75f, 1.0f, 0.9f)));
	WindInfoText->SetJustification(ETextJustify::Left);
	AddToVBox(FlightVBox, WindInfoText, 3.0f);

	// Conditions (flyability + OD risk)
	ConditionsText = MakeText(TEXT("Conditions"), TEXT("Conditions: ---"), 11, FSlateColor(CLR_TEXT));
	ConditionsText->SetJustification(ETextJustify::Left);
	AddToVBox(FlightVBox, ConditionsText, 3.0f);

	// Cloudbase
	CloudbaseText = MakeText(TEXT("Cloudbase"), TEXT("Cloudbase: ---"), 11, FSlateColor(FLinearColor(0.6f, 0.6f, 0.8f, 0.9f)));
	CloudbaseText->SetJustification(ETextJustify::Left);
	AddToVBox(FlightVBox, CloudbaseText, 8.0f);

	AddSeparator(FlightVBox);

	// Flight stats
	UTextBlock* FlightHdr = MakeHeader(TEXT("FlightHdr"), TEXT("FLIGHT DATA"));
	AddToVBox(FlightVBox, FlightHdr, 5.0f);

	AddStatRow(FlightVBox, TEXT("ALT"), AltitudeValue, 16);
	AddStatRow(FlightVBox, TEXT("SPD"), SpeedValue, 16);
	AddStatRow(FlightVBox, TEXT("AIR"), AirspeedValue, 14);
	AddStatRow(FlightVBox, TEXT("VSI"), VSpeedValue, 14);
	AddStatRow(FlightVBox, TEXT("TMP"), TempValue, 13);
	AddStatRow(FlightVBox, TEXT("WND"), WindSpeedValue, 13);
}

void UUW_FlightHUD::BuildEnginePanel(UCanvasPanel* Root)
{
	UBorder* EnginePanel = MakePanel(TEXT("EnginePanel"));
	Root->AddChild(EnginePanel);
	if (UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(EnginePanel->Slot))
	{
		S->SetAnchors(FAnchors(0.0f, 1.0f, 0.0f, 1.0f));
		S->SetPosition(FVector2D(15.0f, -280.0f));
		S->SetSize(FVector2D(260.0f, 265.0f));
	}

	UVerticalBox* EngVBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("EngVBox"));
	EnginePanel->AddChild(EngVBox);
	if (UBorderSlot* BS = Cast<UBorderSlot>(EngVBox->Slot))
	{
		BS->SetPadding(FMargin(0.0f));
		BS->SetHorizontalAlignment(HAlign_Fill);
		BS->SetVerticalAlignment(VAlign_Fill);
	}

	// Engine header + state in one row
	UHorizontalBox* EngHeaderRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("EngHeaderRow"));
	UTextBlock* EngHdr = MakeHeader(TEXT("EngHdr"), TEXT("ENGINE"));
	EngHeaderRow->AddChild(EngHdr);
	if (UHorizontalBoxSlot* HS = Cast<UHorizontalBoxSlot>(EngHdr->Slot))
	{
		HS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		HS->SetVerticalAlignment(VAlign_Center);
	}
	EngineStateText = MakeText(TEXT("EngState"), TEXT("OFF"), 10, FSlateColor(CLR_TEXT_DIM));
	EngineStateText->SetJustification(ETextJustify::Right);
	EngHeaderRow->AddChild(EngineStateText);
	if (UHorizontalBoxSlot* HS = Cast<UHorizontalBoxSlot>(EngineStateText->Slot))
	{
		HS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		HS->SetVerticalAlignment(VAlign_Center);
	}
	AddToVBox(EngVBox, EngHeaderRow, 6.0f);

	// Fuel cutoff indicator (only visible when active)
	FuelCutText = MakeText(TEXT("FuelCut"), TEXT(""), 10, FSlateColor(CLR_VALUE_BAD));
	FuelCutText->SetJustification(ETextJustify::Center);
	AddToVBox(EngVBox, FuelCutText, 4.0f);

	// Gauges
	AddGaugeRow(EngVBox, TEXT("RPM"), RPMValue, RPMBar, CLR_HIGHLIGHT);
	AddGaugeRow(EngVBox, TEXT("THR"), ThrottleValue, ThrottleBar, FLinearColor(0.3f, 0.8f, 1.0f, 1.0f));
	AddGaugeRow(EngVBox, TEXT("CHT"), EngTempValue, EngTempBar, FLinearColor(1.0f, 0.5f, 0.2f, 1.0f));
	AddGaugeRow(EngVBox, TEXT("EGT"), EGTValue, EGTBar, FLinearColor(1.0f, 0.35f, 0.15f, 1.0f));
	AddGaugeRow(EngVBox, TEXT("FUEL"), FuelValue, FuelBar, CLR_VALUE_GOOD);

	// Engine hours — small, at bottom
	UHorizontalBox* HrsRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("HrsRow"));
	UTextBlock* HrsLbl = MakeText(TEXT("HrsLbl"), TEXT("HRS"), 9, FSlateColor(CLR_TEXT_LABEL));
	HrsRow->AddChild(HrsLbl);
	if (UHorizontalBoxSlot* HS = Cast<UHorizontalBoxSlot>(HrsLbl->Slot))
	{
		HS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		HS->SetVerticalAlignment(VAlign_Center);
	}
	EngHoursValue = MakeText(TEXT("HrsVal"), TEXT("0.0"), 12, FSlateColor(CLR_TEXT_DIM));
	EngHoursValue->SetJustification(ETextJustify::Right);
	HrsRow->AddChild(EngHoursValue);
	if (UHorizontalBoxSlot* HS = Cast<UHorizontalBoxSlot>(EngHoursValue->Slot))
	{
		HS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		HS->SetVerticalAlignment(VAlign_Center);
	}
	AddToVBox(EngVBox, HrsRow, 0.0f);
}

void UUW_FlightHUD::BuildWingPanel(UCanvasPanel* Root)
{
	UBorder* WingPanel = MakePanel(TEXT("WingPanel"));
	Root->AddChild(WingPanel);
	if (UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(WingPanel->Slot))
	{
		S->SetAnchors(FAnchors(0.0f, 1.0f, 0.0f, 1.0f));
		S->SetPosition(FVector2D(15.0f, -625.0f));
		S->SetSize(FVector2D(260.0f, 335.0f));
	}

	UVerticalBox* WingVBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("WingVBox"));
	WingPanel->AddChild(WingVBox);
	if (UBorderSlot* BS = Cast<UBorderSlot>(WingVBox->Slot))
	{
		BS->SetPadding(FMargin(0.0f));
		BS->SetHorizontalAlignment(HAlign_Fill);
		BS->SetVerticalAlignment(VAlign_Fill);
	}

	// Wing header + state
	UHorizontalBox* WingHeaderRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("WingHeaderRow"));
	UTextBlock* WingHdr = MakeHeader(TEXT("WingHdr"), TEXT("WING"));
	WingHeaderRow->AddChild(WingHdr);
	if (UHorizontalBoxSlot* HS = Cast<UHorizontalBoxSlot>(WingHdr->Slot))
	{
		HS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		HS->SetVerticalAlignment(VAlign_Center);
	}
	WingStateText = MakeText(TEXT("WingState"), TEXT("---"), 10, FSlateColor(CLR_TEXT_DIM));
	WingStateText->SetJustification(ETextJustify::Right);
	WingHeaderRow->AddChild(WingStateText);
	if (UHorizontalBoxSlot* HS = Cast<UHorizontalBoxSlot>(WingStateText->Slot))
	{
		HS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		HS->SetVerticalAlignment(VAlign_Center);
	}
	AddToVBox(WingVBox, WingHeaderRow, 6.0f);

	// Wing model name (shown below header)
	WingModelText = MakeText(TEXT("WingModel"), TEXT(""), 10, FSlateColor(CLR_TEXT_DIM));
	WingModelText->SetJustification(ETextJustify::Right);
	AddToVBox(WingVBox, WingModelText, 4.0f);

	// Brake inputs (L/R) — fundamental paraglider control display
	UHorizontalBox* BrakeRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("BrakeRow"));
	UTextBlock* BrakeLbl = MakeText(TEXT("BrakeLbl"), TEXT("BRK"), 9, FSlateColor(CLR_TEXT_LABEL));
	BrakeRow->AddChild(BrakeLbl);
	if (UHorizontalBoxSlot* HS = Cast<UHorizontalBoxSlot>(BrakeLbl->Slot))
	{
		HS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		HS->SetPadding(FMargin(0.0f, 0.0f, 4.0f, 0.0f));
		HS->SetVerticalAlignment(VAlign_Center);
	}
	BrakeLeftValue = MakeText(TEXT("BrakeL"), TEXT("L:0%"), 11, FSlateColor(CLR_TEXT_DIM));
	BrakeLeftValue->SetJustification(ETextJustify::Left);
	BrakeRow->AddChild(BrakeLeftValue);
	if (UHorizontalBoxSlot* HS = Cast<UHorizontalBoxSlot>(BrakeLeftValue->Slot))
	{
		HS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		HS->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
		HS->SetVerticalAlignment(VAlign_Center);
	}
	BrakeRightValue = MakeText(TEXT("BrakeR"), TEXT("R:0%"), 11, FSlateColor(CLR_TEXT_DIM));
	BrakeRightValue->SetJustification(ETextJustify::Right);
	BrakeRow->AddChild(BrakeRightValue);
	if (UHorizontalBoxSlot* HS = Cast<UHorizontalBoxSlot>(BrakeRightValue->Slot))
	{
		HS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		HS->SetVerticalAlignment(VAlign_Center);
	}
	AddToVBox(WingVBox, BrakeRow, 4.0f);

	// Inflation progress (hidden by default, shown during inflation)
	InflationProgressValue = MakeText(TEXT("InflatProg"), TEXT(""), 10, FSlateColor(CLR_VALUE_WARN));
	InflationProgressValue->SetJustification(ETextJustify::Left);
	InflationProgressValue->SetVisibility(ESlateVisibility::Collapsed);
	AddToVBox(WingVBox, InflationProgressValue, 2.0f);

	InflationProgressBar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("InflatBar"));
	InflationProgressBar->SetPercent(0.0f);
	InflationProgressBar->SetFillColorAndOpacity(CLR_VALUE_WARN);
	InflationProgressBar->SetVisibility(ESlateVisibility::Collapsed);
	AddToVBox(WingVBox, InflationProgressBar, 6.0f);

	// Glide ratio + takeoff chance
	AddStatRow(WingVBox, TEXT("L/D"), GlideRatioValue, 14);
	AddStatRow(WingVBox, TEXT("LOAD"), PilotWeightValue, 12);
	AddStatRow(WingVBox, TEXT("T/O"), TakeoffChanceValue, 14);

	AddSeparator(WingVBox);

	// Pendulum + weight support row
	UHorizontalBox* WingInfoRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("WingInfoRow"));
	UTextBlock* PendLbl = MakeText(TEXT("PendLbl"), TEXT("PEND"), 9, FSlateColor(CLR_TEXT_LABEL));
	WingInfoRow->AddChild(PendLbl);
	if (UHorizontalBoxSlot* HS = Cast<UHorizontalBoxSlot>(PendLbl->Slot))
	{
		HS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		HS->SetPadding(FMargin(0.0f, 0.0f, 4.0f, 0.0f));
		HS->SetVerticalAlignment(VAlign_Center);
	}
	PendulumValue = MakeText(TEXT("Pendulum"), TEXT("--"), 12, FSlateColor(CLR_TEXT_DIM));
	PendulumValue->SetJustification(ETextJustify::Right);
	WingInfoRow->AddChild(PendulumValue);
	if (UHorizontalBoxSlot* HS = Cast<UHorizontalBoxSlot>(PendulumValue->Slot))
	{
		HS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		HS->SetPadding(FMargin(0.0f, 0.0f, 12.0f, 0.0f));
		HS->SetVerticalAlignment(VAlign_Center);
	}
	UTextBlock* WtLbl = MakeText(TEXT("WtLbl"), TEXT("WT"), 9, FSlateColor(CLR_TEXT_LABEL));
	WingInfoRow->AddChild(WtLbl);
	if (UHorizontalBoxSlot* HS = Cast<UHorizontalBoxSlot>(WtLbl->Slot))
	{
		HS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		HS->SetPadding(FMargin(0.0f, 0.0f, 4.0f, 0.0f));
		HS->SetVerticalAlignment(VAlign_Center);
	}
	WeightSupportValue = MakeText(TEXT("WtSupport"), TEXT("--"), 12, FSlateColor(CLR_TEXT_DIM));
	WeightSupportValue->SetJustification(ETextJustify::Right);
	WingInfoRow->AddChild(WeightSupportValue);
	if (UHorizontalBoxSlot* HS = Cast<UHorizontalBoxSlot>(WeightSupportValue->Slot))
	{
		HS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		HS->SetVerticalAlignment(VAlign_Center);
	}
	AddToVBox(WingVBox, WingInfoRow, 6.0f);

	// Trimmer status
	TrimmerStatusValue = MakeText(TEXT("Trimmer"), TEXT("TRIM: NEUTRAL"), 11, FSlateColor(CLR_TEXT_DIM));
	TrimmerStatusValue->SetJustification(ETextJustify::Left);
	AddToVBox(WingVBox, TrimmerStatusValue, 4.0f);

	// Injury status (hidden by default)
	InjuryStatusValue = MakeText(TEXT("Injury"), TEXT(""), 11, FSlateColor(CLR_VALUE_BAD));
	InjuryStatusValue->SetJustification(ETextJustify::Left);
	InjuryStatusValue->SetVisibility(ESlateVisibility::Collapsed);
	AddToVBox(WingVBox, InjuryStatusValue, 0.0f);

	// Debug takeoff (hidden by default, only shown during ground run)
	DebugTakeoffValue = MakeText(TEXT("DebugTakeoff"), TEXT(""), 9, FSlateColor(CLR_VALUE_WARN));
	DebugTakeoffValue->SetJustification(ETextJustify::Left);
	DebugTakeoffValue->SetVisibility(ESlateVisibility::Collapsed);
	AddToVBox(WingVBox, DebugTakeoffValue, 0.0f);
}

void UUW_FlightHUD::BuildInstructionBar(UCanvasPanel* Root)
{
	InstructionPanel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("InstructionPanel"));
	InstructionPanel->SetBrushColor(FLinearColor(0.02f, 0.04f, 0.08f, 0.65f));
	InstructionPanel->SetPadding(FMargin(20.0f, 8.0f, 20.0f, 10.0f));
	Root->AddChild(InstructionPanel);
	if (UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(InstructionPanel->Slot))
	{
		S->SetAnchors(FAnchors(0.5f, 1.0f, 0.5f, 1.0f));
		S->SetPosition(FVector2D(-280.0f, -50.0f));
		S->SetSize(FVector2D(560.0f, 38.0f));
	}

	InstructionText = MakeText(TEXT("InstructionTxt"), TEXT(""), 13, FSlateColor(CLR_HIGHLIGHT));
	InstructionText->SetJustification(ETextJustify::Center);
	InstructionPanel->AddChild(InstructionText);
	if (UBorderSlot* BS = Cast<UBorderSlot>(InstructionText->Slot))
	{
		BS->SetPadding(FMargin(0.0f));
		BS->SetHorizontalAlignment(HAlign_Fill);
		BS->SetVerticalAlignment(VAlign_Center);
	}
}

void UUW_FlightHUD::BuildCrosshair(UCanvasPanel* Root)
{
	// Center dot
	UBorder* CHDot = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("CHDot"));
	CHDot->SetBrushColor(FLinearColor(1.0f, 1.0f, 1.0f, 0.5f));
	CHDot->SetPadding(FMargin(0.0f));
	Root->AddChild(CHDot);
	if (UCanvasPanelSlot* CS = Cast<UCanvasPanelSlot>(CHDot->Slot))
	{
		CS->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
		CS->SetPosition(FVector2D(-1.5f, -1.5f));
		CS->SetSize(FVector2D(3.0f, 3.0f));
	}

	// Outer ring (faint)
	UBorder* CHRing = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("CHRing"));
	CHRing->SetBrushColor(FLinearColor(1.0f, 1.0f, 1.0f, 0.15f));
	CHRing->SetPadding(FMargin(0.0f));
	Root->AddChild(CHRing);
	if (UCanvasPanelSlot* CS = Cast<UCanvasPanelSlot>(CHRing->Slot))
	{
		CS->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
		CS->SetPosition(FVector2D(-8.0f, -8.0f));
		CS->SetSize(FVector2D(16.0f, 16.0f));
	}
}