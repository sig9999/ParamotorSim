// Copyright Epic Games, Inc. All Rights Reserved.

#include "UW_FlightHUD.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Components/Border.h"
#include "Components/BorderSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "ParamotorPlayerController.h"
#include "ParamotorActor.h"
#include "ParamotorCharacter.h"
#include "WingActor.h"
#include "ParamotorGameInstance.h"
#include "AtmosphereModel.h"

// ============================================================
// Design constants — consistent palette and sizing
// ============================================================
static const FLinearColor CLR_BG_PANEL   = FLinearColor(0.02f, 0.04f, 0.08f, 0.55f);
static const FLinearColor CLR_BG_HEADER  = FLinearColor(0.05f, 0.10f, 0.20f, 0.70f);
static const FLinearColor CLR_ACCENT     = FLinearColor(0.15f, 0.55f, 0.95f, 1.0f);   // MSFS blue
static const FLinearColor CLR_ACCENT_DIM = FLinearColor(0.10f, 0.35f, 0.60f, 0.80f);
static const FLinearColor CLR_TEXT       = FLinearColor(0.92f, 0.94f, 0.98f, 1.0f);   // near-white
static const FLinearColor CLR_TEXT_DIM   = FLinearColor(0.45f, 0.50f, 0.58f, 1.0f);   // dim gray-blue
static const FLinearColor CLR_TEXT_LABEL = FLinearColor(0.35f, 0.42f, 0.52f, 1.0f);   // label gray
static const FLinearColor CLR_VALUE_GOOD = FLinearColor(0.25f, 0.85f, 0.45f, 1.0f);
static const FLinearColor CLR_VALUE_WARN = FLinearColor(1.0f, 0.75f, 0.25f, 1.0f);
static const FLinearColor CLR_VALUE_BAD  = FLinearColor(1.0f, 0.30f, 0.25f, 1.0f);
static const FLinearColor CLR_HIGHLIGHT  = FLinearColor(1.0f, 0.85f, 0.35f, 1.0f);

// ============================================================
// Static helpers
// ============================================================
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

// ============================================================
// UI construction helpers (extracted from NativeOnInitialized lambdas)
// ============================================================
UTextBlock* UUW_FlightHUD::MakeText(const FString& Name, const FString& Text, int32 Size, FSlateColor Color)
{
	UTextBlock* TB = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), FName(*Name));
	TB->SetText(FText::FromString(Text));
	TB->SetColorAndOpacity(Color);
	TB->SetAutoWrapText(false);
	FSlateFontInfo Font = TB->GetFont();
	Font.Size = Size;
	// Slightly bolder for better readability on dark backgrounds
	Font.OutlineSettings = FFontOutlineSettings(1, FLinearColor(0.0f, 0.0f, 0.0f, 0.6f));
	TB->SetFont(Font);
	return TB;
}

UVerticalBoxSlot* UUW_FlightHUD::AddToVBox(UVerticalBox* VBox, UWidget* Widget, float PadBottom)
{
	UVerticalBoxSlot* VBoxSlot = VBox->AddChildToVerticalBox(Widget);
	VBoxSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, PadBottom));
	VBoxSlot->SetHorizontalAlignment(HAlign_Fill);
	VBoxSlot->SetVerticalAlignment(VAlign_Top);
	return VBoxSlot;
}

UBorder* UUW_FlightHUD::MakePanel(const FString& Name)
{
	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), FName(*Name));
	Panel->SetBrushColor(CLR_BG_PANEL);
	Panel->SetPadding(FMargin(14.0f, 10.0f, 14.0f, 12.0f));
	// Corner radius via border brush — UE5 supports rounded borders via SetBrushFromAsset
	// but for C++ simplicity we use flat color; the dark semi-transparent look is clean
	return Panel;
}

UTextBlock* UUW_FlightHUD::MakeHeader(const FString& Name, const FString& Label)
{
	UTextBlock* Hdr = MakeText(Name, Label, 9, FSlateColor(CLR_ACCENT));
	Hdr->SetJustification(ETextJustify::Left);
	return Hdr;
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

// ============================================================
// UI builders (split from NativeOnInitialized)
// ============================================================
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

// ============================================================
// NativeOnInitialized — slim dispatcher
// ============================================================
void UUW_FlightHUD::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
	WidgetTree->RootWidget = Root;

	BuildFlightPanel(Root);
	BuildEnginePanel(Root);
	BuildWingPanel(Root);
	BuildInstructionBar(Root);
	BuildCrosshair(Root);
}

// ============================================================
// Tick update methods (split from NativeTick)
// ============================================================
void UUW_FlightHUD::UpdateCompass(APlayerController* PC)
{
	FRotator ControlRot = PC->GetControlRotation();
	float Heading = ControlRot.Yaw;
	while (Heading < 0.0f) Heading += 360.0f;
	while (Heading >= 360.0f) Heading -= 360.0f;

	FString Cardinal = HeadingToCardinal(Heading);

	if (CompassText)
		CompassText->SetText(FText::FromString(Cardinal));
	if (HeadingText)
		HeadingText->SetText(FText::FromString(FString::Printf(TEXT("%.0f\x00B0"), Heading)));
}

void UUW_FlightHUD::UpdateWindInfo(APlayerController* PC, AParamotorPlayerController* ParamotorPC)
{
	float LiveWindDirDeg = 0.0f;   // degrees, 0=N=+X
	float CompassWindMs = 6.0f;
	AWingActor* Wing = ParamotorPC ? ParamotorPC->SpawnedWingActor : nullptr;
	if (Wing)
	{
		CompassWindMs = Wing->GetCurrentWindSpeedMs();
		LiveWindDirDeg = Wing->GetWindHeadingDeg();
	}
	else if (UParamotorGameInstance* GI = GetWorld()->GetGameInstance<UParamotorGameInstance>())
	{
		CompassWindMs = GI->WindSpeed;
	}

	FString WindCard = HeadingToCardinal(LiveWindDirDeg);

	if (WindInfoText)
		WindInfoText->SetText(FText::FromString(FString::Printf(TEXT("Wind: %s %.0fmph"), *WindCard, CompassWindMs * MPH_PER_MS)));

	// Show flyability + cloudbase + OD risk from the wing's atmospheric state
	if (Wing)
	{
		// Build a conditions summary line
		FString ConditionsStr;
		FLinearColor CondColor = CLR_TEXT;

		EFlyability Fly = Wing->GetFlyability();
		bool bOD = Wing->IsOverdevelopmentRisk();

		switch (Fly)
		{
		case EFlyability::Excellent:   ConditionsStr = TEXT("EXCELLENT"); CondColor = CLR_VALUE_GOOD; break;
		case EFlyability::Good:        ConditionsStr = TEXT("GOOD"); CondColor = CLR_VALUE_GOOD; break;
		case EFlyability::Challenging: ConditionsStr = TEXT("CHALLENGING"); CondColor = CLR_VALUE_WARN; break;
		case EFlyability::Dangerous:   ConditionsStr = TEXT("DANGEROUS"); CondColor = CLR_VALUE_BAD; break;
		case EFlyability::Unflyable:   ConditionsStr = TEXT("UNFLYABLE"); CondColor = CLR_VALUE_BAD; break;
		}

		if (bOD)
		{
			ConditionsStr += TEXT(" - OD RISK!");
			CondColor = CLR_VALUE_BAD;
		}

		if (ConditionsText)
		{
			ConditionsText->SetText(FText::FromString(ConditionsStr));
			ConditionsText->SetColorAndOpacity(FSlateColor(CondColor));
		}

		// Cloudbase display
		float CloudbaseM = Wing->GetCloudbase();
		float CloudbaseFt = CloudbaseM * 3.28084f;
		if (CloudbaseText)
			CloudbaseText->SetText(FText::FromString(FString::Printf(TEXT("Cloudbase: %.0fft"), CloudbaseFt)));
	}
}

void UUW_FlightHUD::UpdateFlightStats(APawn* Pawn, AParamotorPlayerController* ParamotorPC)
{
	// Altitude (cm -> ft)
	float AltM = Pawn->GetActorLocation().Z / CM_PER_M;
	float AltFt = AltM * 3.28084f;
	if (AltitudeValue)
		AltitudeValue->SetText(FText::FromString(FString::Printf(TEXT("%.0f ft"), AltFt)));

	// Speed (cm/s -> mph) — ground speed
	float SpdMph = Pawn->GetVelocity().Size() / CM_PER_M * MPH_PER_MS;
	if (SpeedValue)
		SpeedValue->SetText(FText::FromString(FString::Printf(TEXT("%.0f mph"), SpdMph)));

	// Airspeed (through-air speed, from wing actor) — critical for stall awareness
	if (AirspeedValue)
	{
		AWingActor* AirWing = ParamotorPC ? ParamotorPC->SpawnedWingActor : nullptr;
		if (AirWing && (AirWing->IsFlying() || AirWing->IsInflated()))
		{
			float AirMs = AirWing->GetAirspeed() / CM_PER_M;
			float AirMph = AirMs * MPH_PER_MS;
			AirspeedValue->SetText(FText::FromString(FString::Printf(TEXT("%.0f mph"), AirMph)));
			// Color: green at safe speed, yellow near stall, red at stall
			FLinearColor AirColor = CLR_TEXT;
			if (AirMs < 4.0f) AirColor = CLR_VALUE_BAD;       // Below min takeoff airspeed
			else if (AirMs < 6.0f) AirColor = CLR_VALUE_WARN;  // Marginal
			else AirColor = CLR_VALUE_GOOD;
			AirspeedValue->SetColorAndOpacity(FSlateColor(AirColor));
		}
		else
		{
			AirspeedValue->SetText(FText::FromString(TEXT("--")));
			AirspeedValue->SetColorAndOpacity(FSlateColor(CLR_TEXT_DIM));
		}
	}

	// Vertical speed (m/s -> fpm)
	float VSpd = Pawn->GetVelocity().Z / CM_PER_M;
	float VSpdFpm = VSpd * 196.8504f;  // m/s to feet/min
	if (VSpeedValue)
	{
		FLinearColor VSiColor = CLR_TEXT;
		if (VSpdFpm > 200.0f) VSiColor = CLR_VALUE_GOOD;
		else if (VSpdFpm < -800.0f) VSiColor = CLR_VALUE_BAD;
		else if (VSpdFpm < -300.0f) VSiColor = CLR_VALUE_WARN;

		if (VSpd >= 0.0f)
			VSpeedValue->SetText(FText::FromString(FString::Printf(TEXT("+%.0f fpm"), VSpdFpm)));
		else
			VSpeedValue->SetText(FText::FromString(FString::Printf(TEXT("%.0f fpm"), VSpdFpm)));
		VSpeedValue->SetColorAndOpacity(FSlateColor(VSiColor));
	}

	// Temperature: use GameInstance Temperature as sea-level base, lapse rate with altitude
	// ISA lapse rate: -6.5°C per 1000m. Ground temp from user-set Temperature.
	float GroundTempC = 15.0f;  // ISA default
	if (UParamotorGameInstance* GI = GetWorld()->GetGameInstance<UParamotorGameInstance>())
		GroundTempC = GI->Temperature;
	float Temp = GroundTempC - (AltM * 0.0065f);
	float TempF = Temp * 9.0f / 5.0f + 32.0f;
	if (TempValue)
		TempValue->SetText(FText::FromString(FString::Printf(TEXT("%.0f\x00B0F"), TempF)));

	// Wind speed display (live from GameInstance) + headwind/tailwind indicator
	if (WindSpeedValue)
	{
		// Read live wind speed from GameInstance (was using stale compile-time default)
		float LiveWindMs = 6.0f;
		if (UParamotorGameInstance* GI = GetWorld()->GetGameInstance<UParamotorGameInstance>())
			LiveWindMs = GI->WindSpeed;
		// If wing exists, use its dynamic wind speed (includes gusts/turbulence)
		if (ParamotorPC && ParamotorPC->SpawnedWingActor)
			LiveWindMs = ParamotorPC->SpawnedWingActor->GetCurrentWindSpeedMs();

		// Check if pilot is facing into the wind or away
		FString WindDirLabel = TEXT("");
		FLinearColor WindColor = CLR_TEXT;
		if (Pawn)
		{
			FVector PilotFwd = Pawn->GetActorForwardVector();
			PilotFwd.Z = 0.0f;
			PilotFwd.Normalize();
			// Use shifted wind direction (includes turbulence shift)
			FVector WindDir = FVector(1, 0, 0);
			if (ParamotorPC && ParamotorPC->SpawnedWingActor)
				WindDir = ParamotorPC->SpawnedWingActor->GetShiftedWindDirection();
			float Dot = FVector::DotProduct(WindDir, PilotFwd);
			if (Dot < -0.3f)
			{
				WindDirLabel = TEXT(" (HEADWIND)");
				WindColor = CLR_VALUE_GOOD;
			}
			else if (Dot > 0.3f)
			{
				WindDirLabel = TEXT(" (TAILWIND!)");
				WindColor = CLR_VALUE_BAD;
			}
			else
			{
				WindDirLabel = TEXT(" (CROSSWIND)");
				WindColor = CLR_VALUE_WARN;
			}
		}
		WindSpeedValue->SetText(FText::FromString(FString::Printf(TEXT("%.0f mph%s"), LiveWindMs * MPH_PER_MS, *WindDirLabel)));
		WindSpeedValue->SetColorAndOpacity(FSlateColor(WindColor));
	}
}

void UUW_FlightHUD::UpdateEngineData(AParamotorActor* Paramotor)
{
	if (Paramotor && Paramotor->IsEquipped())
	{
		EEngineState State = Paramotor->GetEngineState();
		float RPM = Paramotor->GetEngineRPM();
		float Thr = Paramotor->GetThrottle();
		float ETemp = Paramotor->GetEngineTemp();
		float EGT = Paramotor->GetEGT();
		float Fuel = Paramotor->GetFuelLevel();
		float RunTime = Paramotor->GetTotalRunTime();
		bool bFuelCut = Paramotor->IsFuelCutoffOn();

		// Engine state text
		if (EngineStateText)
		{
			FString StateStr;
			FLinearColor StateColor;
			if (State == EEngineState::Off)
			{
				StateStr = TEXT("OFF");
				StateColor = CLR_TEXT_DIM;
			}
			else if (State == EEngineState::Cranking)
			{
				StateStr = TEXT("CRANKING");
				StateColor = CLR_VALUE_WARN;
			}
			else
			{
				StateStr = TEXT("RUNNING");
				StateColor = CLR_VALUE_GOOD;
			}
			EngineStateText->SetText(FText::FromString(StateStr));
			EngineStateText->SetColorAndOpacity(FSlateColor(StateColor));
		}

		// Fuel cutoff indicator
		if (FuelCutText)
		{
			if (bFuelCut)
			{
				FuelCutText->SetText(FText::FromString(TEXT("FUEL CUT: ON [L]")));
				FuelCutText->SetColorAndOpacity(FSlateColor(CLR_VALUE_BAD));
				FuelCutText->SetVisibility(ESlateVisibility::Visible);
			}
			else
			{
				FuelCutText->SetVisibility(ESlateVisibility::Collapsed);
			}
		}

		// RPM
		if (RPMValue)
			RPMValue->SetText(FText::FromString(FString::Printf(TEXT("%.0f"), RPM)));
		if (RPMBar)
			RPMBar->SetPercent(FMath::Clamp(RPM / REDLINE_RPM, 0.0f, 1.0f));

		// Throttle
		if (ThrottleValue)
			ThrottleValue->SetText(FText::FromString(FString::Printf(TEXT("%.0f%%"), Thr * PERCENT_FACTOR)));
		if (ThrottleBar)
			ThrottleBar->SetPercent(Thr);

		// Engine temp CHT (C -> F)
		float ETempF = ETemp * 9.0f / 5.0f + 32.0f;
		if (EngTempValue)
			EngTempValue->SetText(FText::FromString(FString::Printf(TEXT("%.0f\x00B0F"), ETempF)));
		if (EngTempBar)
		{
			EngTempBar->SetPercent(FMath::Clamp(ETemp / 130.0f, 0.0f, 1.0f));
			FLinearColor TempColor;
			if (ETemp < 90.0f) TempColor = CLR_VALUE_GOOD;
			else if (ETemp < 105.0f) TempColor = CLR_VALUE_WARN;
			else TempColor = CLR_VALUE_BAD;
			EngTempBar->SetFillColorAndOpacity(TempColor);
		}

		// EGT (C -> F)
		float EGTF = EGT * 9.0f / 5.0f + 32.0f;
		if (EGTValue)
			EGTValue->SetText(FText::FromString(FString::Printf(TEXT("%.0f\x00B0F"), EGTF)));
		if (EGTBar)
		{
			EGTBar->SetPercent(FMath::Clamp(EGT / 700.0f, 0.0f, 1.0f));
			FLinearColor EGTColor;
			if (EGT < 550.0f) EGTColor = CLR_VALUE_GOOD;
			else if (EGT < 620.0f) EGTColor = CLR_VALUE_WARN;
			else EGTColor = CLR_VALUE_BAD;
			EGTBar->SetFillColorAndOpacity(EGTColor);
		}

		// Fuel
		if (FuelValue)
		{
			if (Fuel > 0.0f)
				FuelValue->SetText(FText::FromString(FString::Printf(TEXT("%.0f%%"), Fuel * PERCENT_FACTOR)));
			else
				FuelValue->SetText(FText::FromString(TEXT("EMPTY")));
		}
		if (FuelBar)
		{
			FuelBar->SetPercent(Fuel);
			FLinearColor FuelColor;
			if (Fuel > 0.3f) FuelColor = CLR_VALUE_GOOD;
			else if (Fuel > 0.1f) FuelColor = CLR_VALUE_WARN;
			else FuelColor = CLR_VALUE_BAD;
			FuelBar->SetFillColorAndOpacity(FuelColor);
		}

		// Engine hours
		if (EngHoursValue)
			EngHoursValue->SetText(FText::FromString(FString::Printf(TEXT("%.1f"), RunTime / 3600.0f)));
	}
	else
	{
		// No paramotor equipped
		if (EngineStateText)
		{
			EngineStateText->SetText(FText::FromString(TEXT("NONE")));
			EngineStateText->SetColorAndOpacity(FSlateColor(CLR_TEXT_DIM));
		}
		if (FuelCutText) FuelCutText->SetVisibility(ESlateVisibility::Collapsed);
		if (RPMValue) RPMValue->SetText(FText::FromString(TEXT("---")));
		if (RPMBar) RPMBar->SetPercent(0.0f);
		if (ThrottleValue) ThrottleValue->SetText(FText::FromString(TEXT("---")));
		if (ThrottleBar) ThrottleBar->SetPercent(0.0f);
		if (EngTempValue) EngTempValue->SetText(FText::FromString(TEXT("---")));
		if (EngTempBar) EngTempBar->SetPercent(0.0f);
		if (EGTValue) EGTValue->SetText(FText::FromString(TEXT("---")));
		if (EGTBar) EGTBar->SetPercent(0.0f);
		if (FuelValue) FuelValue->SetText(FText::FromString(TEXT("---")));
		if (FuelBar) FuelBar->SetPercent(0.0f);
		if (EngHoursValue) EngHoursValue->SetText(FText::FromString(TEXT("---")));
	}
}

void UUW_FlightHUD::UpdateWingData(APlayerController* PC, AParamotorPlayerController* ParamotorPC, AWingActor* Wing)
{
	if (!Wing)
	{
		ClearWingData();
		return;
	}

	UpdateWingStateDisplay(Wing);
	UpdateWingInflationProgress(Wing);
	UpdateWingFlightMetrics(Wing, PC);
	UpdateWingTakeoffInfo(Wing);
}

void UUW_FlightHUD::UpdateWingStateDisplay(AWingActor* Wing)
{
	EWingState WState = Wing->GetWingState();
	FString WStateStr;
	FLinearColor WStateColor;
	switch (WState)
	{
	case EWingState::InBag:    WStateStr = TEXT("IN BAG"); WStateColor = CLR_TEXT_DIM; break;
	case EWingState::Spread:   WStateStr = TEXT("LAID OUT"); WStateColor = FLinearColor(0.5f, 0.7f, 1.0f, 1.0f); break;
	case EWingState::Connected: WStateStr = TEXT("CONNECTED"); WStateColor = FLinearColor(0.3f, 0.8f, 1.0f, 1.0f); break;
	case EWingState::Inflating: WStateStr = TEXT("INFLATING"); WStateColor = CLR_VALUE_WARN; break;
	case EWingState::Inflated:  WStateStr = TEXT("INFLATED"); WStateColor = CLR_VALUE_GOOD; break;
	case EWingState::Flying:    WStateStr = TEXT("FLYING"); WStateColor = CLR_VALUE_GOOD; break;
	default: WStateStr = TEXT("---"); WStateColor = CLR_TEXT_DIM; break;
	}
	if (WingStateText)
	{
		WingStateText->SetText(FText::FromString(WStateStr));
		WingStateText->SetColorAndOpacity(FSlateColor(WStateColor));
	}

	if (WingModelText)
		WingModelText->SetText(FText::FromString(TEXT("MacPara Charger 25M")));

	if (BrakeLeftValue && BrakeRightValue)
	{
		float LBrake = Wing->GetLeftBrakeAmount() * PERCENT_FACTOR;
		float RBrake = Wing->GetRightBrakeAmount() * PERCENT_FACTOR;
		BrakeLeftValue->SetText(FText::FromString(FString::Printf(TEXT("L:%.0f%%"), LBrake)));
		BrakeRightValue->SetText(FText::FromString(FString::Printf(TEXT("R:%.0f%%"), RBrake)));
		FLinearColor LColor = (LBrake > 1.0f) ? CLR_HIGHLIGHT : CLR_TEXT_DIM;
		FLinearColor RColor = (RBrake > 1.0f) ? CLR_HIGHLIGHT : CLR_TEXT_DIM;
		BrakeLeftValue->SetColorAndOpacity(FSlateColor(LColor));
		BrakeRightValue->SetColorAndOpacity(FSlateColor(RColor));
	}
}

void UUW_FlightHUD::UpdateWingInflationProgress(AWingActor* Wing)
{
	if (!InflationProgressValue || !InflationProgressBar) return;
	EWingState WState = Wing->GetWingState();

	if (WState == EWingState::Inflating || WState == EWingState::Inflated)
	{
		float Progress = Wing->GetInflationProgress() * PERCENT_FACTOR;
		InflationProgressValue->SetText(FText::FromString(
			FString::Printf(TEXT("INFLATING: %.0f%%"), Progress)));
		InflationProgressValue->SetVisibility(ESlateVisibility::Visible);
		InflationProgressBar->SetPercent(FMath::Clamp(Progress / 100.0f, 0.0f, 1.0f));
		InflationProgressBar->SetVisibility(ESlateVisibility::Visible);
		FLinearColor ProgColor = (Progress > 80.0f) ? CLR_VALUE_GOOD : CLR_VALUE_WARN;
		InflationProgressValue->SetColorAndOpacity(FSlateColor(ProgColor));
		InflationProgressBar->SetFillColorAndOpacity(ProgColor);
	}
	else
	{
		InflationProgressValue->SetVisibility(ESlateVisibility::Collapsed);
		InflationProgressBar->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UUW_FlightHUD::UpdateWingFlightMetrics(AWingActor* Wing, APlayerController* PC)
{
	EWingState WState = Wing->GetWingState();
	// Pendulum angle (only meaningful in flight)
	if (PendulumValue)
	{
		if (WState == EWingState::Flying)
		{
			float PendDeg = Wing->GetPendulumAngle() * RAD_TO_DEG;
			PendulumValue->SetText(FText::FromString(FString::Printf(TEXT("%.0f\x00B0"), PendDeg)));
			FLinearColor PendColor = CLR_TEXT;
			float AbsPend = FMath::Abs(PendDeg);
			if (AbsPend > 30.0f) PendColor = CLR_VALUE_BAD;
			else if (AbsPend > 15.0f) PendColor = CLR_VALUE_WARN;
			PendulumValue->SetColorAndOpacity(FSlateColor(PendColor));
		}
		else
		{
			PendulumValue->SetText(FText::FromString(TEXT("--")));
			PendulumValue->SetColorAndOpacity(FSlateColor(CLR_TEXT_DIM));
		}
	}

	// Weight support (during ground run)
	if (WeightSupportValue)
	{
		float GravScale = 1.0f;
		APawn* P = PC->GetPawn();
		ACharacter* C = Cast<ACharacter>(P);
		if (C) GravScale = C->GetCharacterMovement()->GravityScale;
		float WtSupport = (1.0f - GravScale) * PERCENT_FACTOR;
		if (WState == EWingState::Inflated)
		{
			WeightSupportValue->SetText(FText::FromString(FString::Printf(TEXT("%.0f%%"), WtSupport)));
			FLinearColor WtColor = WtSupport > 70.0f ? CLR_VALUE_GOOD : CLR_TEXT;
			WeightSupportValue->SetColorAndOpacity(FSlateColor(WtColor));
		}
		else if (WState == EWingState::Flying)
		{
			WeightSupportValue->SetText(FText::FromString(TEXT("AIR")));
			WeightSupportValue->SetColorAndOpacity(FSlateColor(CLR_VALUE_GOOD));
		}
		else
		{
			WeightSupportValue->SetText(FText::FromString(TEXT("--")));
			WeightSupportValue->SetColorAndOpacity(FSlateColor(CLR_TEXT_DIM));
		}
	}

	// Glide ratio (L/D) — only meaningful in flight
	if (GlideRatioValue)
	{
		if (WState == EWingState::Flying)
		{
			float GR = Wing->GetGlideRatio();
			GlideRatioValue->SetText(FText::FromString(FString::Printf(TEXT("%.1f:1"), GR)));
			FLinearColor GRCOLOR;
			if (GR > 7.5f) GRCOLOR = CLR_VALUE_GOOD;
			else if (GR > 5.0f) GRCOLOR = CLR_VALUE_WARN;
			else GRCOLOR = CLR_VALUE_BAD;
			GlideRatioValue->SetColorAndOpacity(FSlateColor(GRCOLOR));
		}
		else
		{
			GlideRatioValue->SetText(FText::FromString(TEXT("--")));
			GlideRatioValue->SetColorAndOpacity(FSlateColor(CLR_TEXT_DIM));
		}
	}
}

void UUW_FlightHUD::UpdateWingTakeoffInfo(AWingActor* Wing)
{
	EWingState WState = Wing->GetWingState();
	// Pilot/gear total weight
	if (PilotWeightValue)
	{
		float TotalWt = Wing->GetPilotWeight();
		float MaxWt = Wing->GetMaxTakeoffWeight();
		bool bOver = Wing->IsOverloaded();
		FLinearColor WtColor = bOver ? CLR_VALUE_BAD : CLR_TEXT;
		PilotWeightValue->SetText(FText::FromString(FString::Printf(TEXT("%.0f / %.0f kg"), TotalWt, MaxWt)));
		PilotWeightValue->SetColorAndOpacity(FSlateColor(WtColor));
	}

	// Takeoff chance
	if (TakeoffChanceValue)
	{
		if (WState == EWingState::Flying)
		{
			TakeoffChanceValue->SetText(FText::FromString(TEXT("AIRBORNE")));
			TakeoffChanceValue->SetColorAndOpacity(FSlateColor(CLR_VALUE_GOOD));
		}
		else if (WState == EWingState::Inflated || WState == EWingState::Inflating)
		{
			float Chance = Wing->GetTakeoffChance();
			FLinearColor ChanceColor;
			if (Chance > 70.0f) ChanceColor = CLR_VALUE_GOOD;
			else if (Chance > 40.0f) ChanceColor = CLR_VALUE_WARN;
			else ChanceColor = CLR_VALUE_BAD;
			TakeoffChanceValue->SetText(FText::FromString(FString::Printf(TEXT("%.0f%%"), Chance)));
			TakeoffChanceValue->SetColorAndOpacity(FSlateColor(ChanceColor));
		}
		else
		{
			TakeoffChanceValue->SetText(FText::FromString(TEXT("--")));
			TakeoffChanceValue->SetColorAndOpacity(FSlateColor(CLR_TEXT_DIM));
		}
	}

	// Debug takeoff diagnostics — only visible during ground run
	if (DebugTakeoffValue)
	{
		if (WState == EWingState::Inflated)
		{
			float airMph = Wing->GetDebugGroundAirspeed() * MPH_PER_MS;
			float grdMph = Wing->GetDebugGroundSpeed() * MPH_PER_MS;
			float headMph = Wing->GetDebugHeadwind() * MPH_PER_MS;
			float threshMph = Wing->GetDebugTakeoffThreshold() * MPH_PER_MS;
			float thr = Wing->GetDebugThrottle();
			float support = Wing->GetDebugWeightSupport() * PERCENT_FACTOR;
			bool bCanTakeoff = (Wing->GetDebugGroundAirspeed() > Wing->GetDebugTakeoffThreshold() && thr > 0.15f)
				|| (Wing->GetDebugWeightSupport() > 0.75f && thr > 0.15f && Wing->GetDebugGroundAirspeed() > 2.0f);
			FString DbgStr = FString::Printf(TEXT("AIR:%.0f GRD:%.0f WND:%.0f NEED:%.0f THR:%.2f WT:%.0f%% %s"),
				airMph, grdMph, headMph, threshMph, thr, support,
				bCanTakeoff ? TEXT("READY!") : TEXT(""));
			DebugTakeoffValue->SetText(FText::FromString(DbgStr));
			DebugTakeoffValue->SetColorAndOpacity(FSlateColor(bCanTakeoff ? CLR_VALUE_GOOD : CLR_VALUE_WARN));
			DebugTakeoffValue->SetVisibility(ESlateVisibility::Visible);
		}
		else
		{
			DebugTakeoffValue->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}

void UUW_FlightHUD::ClearWingData()
{
	if (WingStateText) WingStateText->SetText(FText::FromString(TEXT("NONE")));
	if (WingModelText) WingModelText->SetText(FText::FromString(TEXT("")));
	if (BrakeLeftValue) BrakeLeftValue->SetText(FText::FromString(TEXT("L:0%")));
	if (BrakeRightValue) BrakeRightValue->SetText(FText::FromString(TEXT("R:0%")));
	if (InflationProgressValue) InflationProgressValue->SetVisibility(ESlateVisibility::Collapsed);
	if (InflationProgressBar) InflationProgressBar->SetVisibility(ESlateVisibility::Collapsed);
	if (PendulumValue) PendulumValue->SetText(FText::FromString(TEXT("--")));
	if (WeightSupportValue) WeightSupportValue->SetText(FText::FromString(TEXT("--")));
	if (PilotWeightValue) PilotWeightValue->SetText(FText::FromString(TEXT("--")));
	if (TakeoffChanceValue) TakeoffChanceValue->SetText(FText::FromString(TEXT("--")));
	if (DebugTakeoffValue) DebugTakeoffValue->SetVisibility(ESlateVisibility::Collapsed);
}

void UUW_FlightHUD::UpdateInjuryStatus()
{
	if (!InjuryStatusValue) return;

	APawn* MyPawn = GetOwningPlayerPawn();
	AParamotorCharacter* Char = Cast<AParamotorCharacter>(MyPawn);
	if (Char && Char->IsInjured())
	{
		FString InjuryName = Char->GetInjuryName();
		float Severity = Char->GetInjurySeverity();
		FLinearColor InjuryColor;
		if (Severity > 0.7f) InjuryColor = CLR_VALUE_BAD;
		else if (Severity > 0.4f) InjuryColor = FLinearColor(1.0f, 0.5f, 0.2f, 1.0f);
		else InjuryColor = CLR_VALUE_WARN;

		InjuryStatusValue->SetText(FText::FromString(
			FString::Printf(TEXT("INJURY: %s (%d%%)"), *InjuryName, FMath::RoundToInt(Severity * PERCENT_FACTOR))));
		InjuryStatusValue->SetColorAndOpacity(FSlateColor(InjuryColor));
		InjuryStatusValue->SetVisibility(ESlateVisibility::Visible);
	}
	else
	{
		InjuryStatusValue->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UUW_FlightHUD::UpdateTrimmerStatus(AWingActor* Wing)
{
	if (!TrimmerStatusValue) return;

	if (Wing)
	{
		float Pos = Wing->GetTrimmerPosition();
		FString Label = Wing->GetTrimmerLabel();

		FLinearColor TrimColor;
		if (Pos > 0.05f)
			TrimColor = FLinearColor(0.3f, 0.8f, 1.0f, 1.0f);  // Blue = fast
		else if (Pos < -0.05f)
			TrimColor = FLinearColor(1.0f, 0.7f, 0.3f, 1.0f);  // Orange = slow
		else
			TrimColor = CLR_VALUE_GOOD;  // Green = neutral

		// Build a visual bar: [---|---] with position marker
		int32 BarPos = FMath::RoundToInt((Pos + 1.0f) * 7.0f);
		BarPos = FMath::Clamp(BarPos, 0, 14);
		FString Bar;
		for (int32 i = 0; i < 15; i++)
		{
			if (i == 7) Bar += (i == BarPos) ? TEXT("*") : TEXT("|");
			else if (i == BarPos) Bar += TEXT("*");
			else Bar += TEXT("-");
		}

		TrimmerStatusValue->SetText(FText::FromString(
			FString::Printf(TEXT("TRIM: %s  [%s]  %+.0f%%"), *Label, *Bar, Pos * PERCENT_FACTOR)));
		TrimmerStatusValue->SetColorAndOpacity(FSlateColor(TrimColor));
		TrimmerStatusValue->SetVisibility(ESlateVisibility::Visible);
	}
	else
	{
		TrimmerStatusValue->SetVisibility(ESlateVisibility::Collapsed);
	}

	// Seat position and hang point status
	if (Wing && Wing->IsFlying())
	{
		FString SeatStr = Wing->IsSeated() ? TEXT("SEATED") : TEXT("STANDING");
		float HangPt = Wing->GetHangPointOffset();
		FString HangStr;
		if (HangPt > 0.1f) HangStr = FString::Printf(TEXT("LEAN FWD %+.0f%%"), HangPt * PERCENT_FACTOR);
		else if (HangPt < -0.1f) HangStr = FString::Printf(TEXT("LEAN BACK %+.0f%%"), HangPt * PERCENT_FACTOR);
		else HangStr = TEXT("NEUTRAL");

		if (TrimmerStatusValue)
		{
			FString Existing = TrimmerStatusValue->GetText().ToString();
			TrimmerStatusValue->SetText(FText::FromString(
				FString::Printf(TEXT("%s  |  POS: %s  |  HANG: %s"), *Existing, *SeatStr, *HangStr)));
		}
	}
}

void UUW_FlightHUD::UpdateInstructions(APlayerController* PC, AParamotorActor* Paramotor, AParamotorPlayerController* ParamotorPC)
{
	if (!InstructionText || !InstructionPanel) return;

	bool bHasParamotor = (Paramotor != nullptr);
	bool bParamotorEquipped = bHasParamotor && Paramotor->IsEquipped();
	AWingActor* InstrWing = ParamotorPC ? ParamotorPC->SpawnedWingActor : nullptr;
	bool bHasWing = (InstrWing != nullptr);

	FString Instruction;
	if (!bHasParamotor)
	{
		Instruction = TEXT("Press ESC > SPAWN tab > SPAWN PARAMOTOR to begin");
	}
	else if (!bParamotorEquipped)
	{
		Instruction = TEXT("Walk to the paramotor and press F to equip it");
	}
	else if (!bHasWing)
	{
		Instruction = TEXT("Press ESC > SPAWN tab to choose your wing");
	}
	else if (InstrWing->IsInBag())
	{
		Instruction = TEXT("Walk to the wing bag and press F to unpack it");
	}
	else if (InstrWing->IsCrinkled())
	{
		if (InstrWing->IsGrabbing())
			Instruction = TEXT("Walk backward to pull the wing out. Press F to release.");
		else
			Instruction = TEXT("Walk to the wing edge and press F to grab it. Pull to spread.");
	}
	else if (InstrWing->IsSpread())
	{
		if (bParamotorEquipped && !InstrWing->IsConnected())
			Instruction = TEXT("Walk to the wing and press F to connect the risers");
		else if (InstrWing->IsConnected())
			Instruction = TEXT("Risers connected! (Inflation coming in Step 6)");
		else
			Instruction = TEXT("Wing is spread out. Equip a paramotor to connect risers.");
	}
	else if (InstrWing->IsConnected())
	{
		Instruction = TEXT("Hold R to lift A-lines and inflate the wing");
	}
	else if (InstrWing->IsInflating())
	{
		Instruction = TEXT("Keep holding R! Wing is inflating...");
	}
	else if (InstrWing->IsInflated())
	{
		// Check wind direction relative to pilot
		APawn* InstrPawn = PC ? PC->GetPawn() : nullptr;
		FString WindWarn = TEXT("");
		if (InstrPawn)
		{
			FVector Pf = InstrPawn->GetActorForwardVector(); Pf.Z = 0; Pf.Normalize();
			FVector Wd2 = InstrWing->GetShiftedWindDirection();
			float DotW = FVector::DotProduct(Wd2, Pf);
			if (DotW > 0.3f)
				WindWarn = TEXT(" TURN AROUND — you're facing downwind!");
			else if (DotW > -0.3f)
				WindWarn = TEXT(" Turn more into the wind for better lift");
		}
		Instruction = FString::Printf(TEXT("Run forward into the wind + throttle up to take off!%s"), *WindWarn);
	}
	else if (InstrWing->IsFlying())
	{
		Instruction = TEXT("Q/E steer | =/- trim | G seat | [/] hang point | Up/Dn throttle | Esc pause");
	}

	InstructionText->SetText(FText::FromString(Instruction));
	InstructionPanel->SetVisibility(
		Instruction.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
}

// ============================================================
// NativeTick — slim dispatcher
// ============================================================
void UUW_FlightHUD::NativeTick(const FGeometry& MyGeometry, float DeltaTime)
{
	Super::NativeTick(MyGeometry, DeltaTime);

	APlayerController* PC = GetOwningPlayer();
	if (!PC) return;

	APawn* Pawn = PC->GetPawn();
	if (!Pawn) return;

	AParamotorPlayerController* ParamotorPC = Cast<AParamotorPlayerController>(PC);
	AParamotorActor* Paramotor = ParamotorPC ? ParamotorPC->GetSpawnedParamotorActor() : nullptr;
	AWingActor* Wing = ParamotorPC ? ParamotorPC->SpawnedWingActor : nullptr;

	UpdateCompass(PC);
	UpdateWindInfo(PC, ParamotorPC);
	UpdateFlightStats(Pawn, ParamotorPC);
	UpdateEngineData(Paramotor);
	UpdateWingData(PC, ParamotorPC, Wing);
	UpdateInjuryStatus();
	UpdateTrimmerStatus(Wing);
	UpdateInstructions(PC, Paramotor, ParamotorPC);
}
