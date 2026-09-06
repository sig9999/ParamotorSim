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
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/ProgressBar.h"
#include "Components/Spacer.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "ParamotorPlayerController.h"
#include "ParamotorActor.h"
#include "ParamotorCharacter.h"
#include "WingActor.h"
#include "ParamotorGameInstance.h"

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

void UUW_FlightHUD::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	// ============================================================
	// Helper lambdas
	// ============================================================

	// Create a styled text block with consistent font sizing
	auto MakeText = [this](const FString& Name, const FString& Text, int32 Size, FSlateColor Color)
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
	};

	auto MakeImage = [this](const FString& Name, FLinearColor Color)
	{
		UImage* Img = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), FName(*Name));
		Img->SetColorAndOpacity(Color);
		return Img;
	};

	// Add a text block to a vertical box with consistent spacing
	auto AddToVBox = [](UVerticalBox* VBox, UWidget* Widget, float PadBottom = 0.0f)
	{
		UVerticalBoxSlot* Slot = VBox->AddChildToVerticalBox(Widget);
		Slot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, PadBottom));
		Slot->SetHorizontalAlignment(HAlign_Fill);
		Slot->SetVerticalAlignment(VAlign_Top);
	};

	// Create a styled panel border with consistent look
	auto MakePanel = [this](const FString& Name) -> UBorder*
	{
		UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), FName(*Name));
		Panel->SetBrushColor(CLR_BG_PANEL);
		Panel->SetPadding(FMargin(14.0f, 10.0f, 14.0f, 12.0f));
		// Corner radius via border brush — UE5 supports rounded borders via SetBrushFromAsset
		// but for C++ simplicity we use flat color; the dark semi-transparent look is clean
		return Panel;
	};

	// Create a section header text
	auto MakeHeader = [&](const FString& Name, const FString& Label) -> UTextBlock*
	{
		UTextBlock* Hdr = MakeText(Name, Label, 9, FSlateColor(CLR_ACCENT));
		Hdr->SetJustification(ETextJustify::Left);
		return Hdr;
	};

	// Create a label-value row with consistent styling
	auto AddStatRow = [&](UVerticalBox* Parent, const FString& Label, UTextBlock*& ValuePtr, int32 ValSize = 15)
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
	};

	// Add a thin separator line
	auto AddSeparator = [&](UVerticalBox* Parent)
	{
		UBorder* Sep = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Sep"));
		Sep->SetBrushColor(FLinearColor(CLR_ACCENT_DIM.R, CLR_ACCENT_DIM.G, CLR_ACCENT_DIM.B, 0.3f));
		Sep->SetPadding(FMargin(0.0f));
		AddToVBox(Parent, Sep, 6.0f);
	};

	// Add a label + value + progress bar row (for engine gauges)
	auto AddGaugeRow = [&](UVerticalBox* Parent, const FString& Label, UTextBlock*& ValuePtr, UProgressBar*& BarPtr, FLinearColor BarColor)
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
	};

	// ============================================================
	// ROOT
	// ============================================================
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
	WidgetTree->RootWidget = Root;

	// ============================================================
	// FLIGHT DATA PANEL (top-right) — compass + stats combined
	// ============================================================
	UBorder* FlightPanel = MakePanel(TEXT("FlightPanel"));
	Root->AddChild(FlightPanel);
	if (UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(FlightPanel->Slot))
	{
		S->SetAnchors(FAnchors(1.0f, 0.0f, 1.0f, 0.0f));
		S->SetPosition(FVector2D(-240.0f, 15.0f));
		S->SetSize(FVector2D(225.0f, 280.0f));
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
	AddToVBox(FlightVBox, WindInfoText, 8.0f);

	AddSeparator(FlightVBox);

	// Flight stats
	UTextBlock* FlightHdr = MakeHeader(TEXT("FlightHdr"), TEXT("FLIGHT DATA"));
	AddToVBox(FlightVBox, FlightHdr, 5.0f);

	AddStatRow(FlightVBox, TEXT("ALT"), AltitudeValue, 16);
	AddStatRow(FlightVBox, TEXT("SPD"), SpeedValue, 16);
	AddStatRow(FlightVBox, TEXT("VSI"), VSpeedValue, 14);
	AddStatRow(FlightVBox, TEXT("TMP"), TempValue, 13);
	AddStatRow(FlightVBox, TEXT("WND"), WindSpeedValue, 13);

	// ============================================================
	// ENGINE PANEL (bottom-left) — compact gauges
	// ============================================================
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

	// ============================================================
	// WING PANEL (bottom-left, above engine) — clean, no debug
	// ============================================================
	UBorder* WingPanel = MakePanel(TEXT("WingPanel"));
	Root->AddChild(WingPanel);
	if (UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(WingPanel->Slot))
	{
		S->SetAnchors(FAnchors(0.0f, 1.0f, 0.0f, 1.0f));
		S->SetPosition(FVector2D(15.0f, -560.0f));
		S->SetSize(FVector2D(260.0f, 270.0f));
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

	// ============================================================
	// INSTRUCTION BAR (bottom-center) — clean, minimal
	// ============================================================
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

	// ============================================================
	// CROSSHAIR (center) — subtle dot + ring
	// ============================================================
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

void UUW_FlightHUD::NativeTick(const FGeometry& MyGeometry, float DeltaTime)
{
	Super::NativeTick(MyGeometry, DeltaTime);

	APlayerController* PC = GetOwningPlayer();
	if (!PC) return;

	APawn* Pawn = PC->GetPawn();
	if (!Pawn) return;

	// ============================================================
	// Heading
	// ============================================================
	FRotator ControlRot = PC->GetControlRotation();
	float Heading = ControlRot.Yaw;
	while (Heading < 0.0f) Heading += 360.0f;
	while (Heading >= 360.0f) Heading -= 360.0f;

	int32 H = FMath::RoundToInt(Heading);
	FString Cardinal;
	if (H >= 337 || H < 23) Cardinal = TEXT("N");
	else if (H < 67) Cardinal = TEXT("NE");
	else if (H < 113) Cardinal = TEXT("E");
	else if (H < 157) Cardinal = TEXT("SE");
	else if (H < 203) Cardinal = TEXT("S");
	else if (H < 247) Cardinal = TEXT("SW");
	else if (H < 293) Cardinal = TEXT("W");
	else Cardinal = TEXT("NW");

	if (CompassText)
		CompassText->SetText(FText::FromString(Cardinal));
	if (HeadingText)
		HeadingText->SetText(FText::FromString(FString::Printf(TEXT("%.0f\x00B0"), Heading)));

	// ============================================================
	// Wind direction cardinal
	// ============================================================
	FString WindCard;
	int32 WD = FMath::RoundToInt(WindDirection);
	while (WD < 0) WD += 360;
	while (WD >= 360) WD -= 360;
	if (WD >= 337 || WD < 23) WindCard = TEXT("N");
	else if (WD < 67) WindCard = TEXT("NE");
	else if (WD < 113) WindCard = TEXT("E");
	else if (WD < 157) WindCard = TEXT("SE");
	else if (WD < 203) WindCard = TEXT("S");
	else if (WD < 247) WindCard = TEXT("SW");
	else if (WD < 293) WindCard = TEXT("W");
	else WindCard = TEXT("NW");

	if (WindInfoText)
		WindInfoText->SetText(FText::FromString(FString::Printf(TEXT("Wind: %s %.0fmph"), *WindCard, WindSpeed * 1.15078f)));

	// ============================================================
	// Altitude (cm -> ft)
	// ============================================================
	float AltM = Pawn->GetActorLocation().Z / 100.0f;
	float AltFt = AltM * 3.28084f;
	if (AltitudeValue)
		AltitudeValue->SetText(FText::FromString(FString::Printf(TEXT("%.0f ft"), AltFt)));

	// ============================================================
	// Speed (cm/s -> mph)
	// ============================================================
	float SpdMph = Pawn->GetVelocity().Size() / 100.0f * 2.23694f;
	if (SpeedValue)
		SpeedValue->SetText(FText::FromString(FString::Printf(TEXT("%.0f mph"), SpdMph)));

	// ============================================================
	// Vertical speed (m/s -> fpm)
	// ============================================================
	float VSpd = Pawn->GetVelocity().Z / 100.0f;
	float VSpdFpm = VSpd * 196.8504f;  // m/s to feet/min
	if (VSpeedValue)
	{
		FLinearColor VSiColor = CLR_TEXT;
		if (VSpdFpm > 200.0f) VSiColor = CLR_VALUE_GOOD;
		else if (VSpdFpm < -300.0f) VSiColor = CLR_VALUE_WARN;
		else if (VSpdFpm < -800.0f) VSiColor = CLR_VALUE_BAD;

		if (VSpd >= 0.0f)
			VSpeedValue->SetText(FText::FromString(FString::Printf(TEXT("+%.0f fpm"), VSpdFpm)));
		else
			VSpeedValue->SetText(FText::FromString(FString::Printf(TEXT("%.0f fpm"), VSpdFpm)));
		VSpeedValue->SetColorAndOpacity(FSlateColor(VSiColor));
	}

	// ============================================================
	// Temperature (standard atmosphere: 15C at sea level, -6.5C per 1000m)
	// ============================================================
	float Temp = 15.0f - (AltM * 0.0065f);
	float TempF = Temp * 9.0f / 5.0f + 32.0f;
	if (TempValue)
		TempValue->SetText(FText::FromString(FString::Printf(TEXT("%.0f\x00B0F"), TempF)));

	// ============================================================
	// Wind speed display (live from GameInstance) + headwind/tailwind indicator
	// ============================================================
	if (WindSpeedValue)
	{
		// Read live wind speed from GameInstance (was using stale compile-time default)
		float LiveWindMs = 6.0f;
		if (UParamotorGameInstance* GI = GetWorld()->GetGameInstance<UParamotorGameInstance>())
			LiveWindMs = GI->WindSpeed;
		// If wing exists, use its dynamic wind speed (includes gusts/turbulence)
		AParamotorPlayerController* ParamotorPC = Cast<AParamotorPlayerController>(PC);
		if (ParamotorPC && ParamotorPC->SpawnedWingActor)
			LiveWindMs = ParamotorPC->SpawnedWingActor->GetCurrentWindSpeedMs();

		// Check if pilot is facing into the wind or away
		APawn* MyPawn = PC ? PC->GetPawn() : nullptr;
		FString WindDirLabel = TEXT("");
		FLinearColor WindColor = CLR_TEXT;
		if (MyPawn)
		{
			FVector PilotFwd = MyPawn->GetActorForwardVector();
			PilotFwd.Z = 0.0f;
			PilotFwd.Normalize();
			// Wind direction from wing (set at connection = headwind direction)
			FVector WindDir = FVector(1, 0, 0);
			if (ParamotorPC && ParamotorPC->SpawnedWingActor)
				WindDir = ParamotorPC->SpawnedWingActor->GetWindDirection();
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

	// ============================================================
	// Engine data (from equipped paramotor)
	// ============================================================
	AParamotorPlayerController* ParamotorPC = Cast<AParamotorPlayerController>(PC);
	AParamotorActor* Paramotor = ParamotorPC ? ParamotorPC->GetSpawnedParamotorActor() : nullptr;

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
			ThrottleValue->SetText(FText::FromString(FString::Printf(TEXT("%.0f%%"), Thr * 100.0f)));
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
				FuelValue->SetText(FText::FromString(FString::Printf(TEXT("%.0f%%"), Fuel * 100.0f)));
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

	// ============================================================
	// Wing data
	// ============================================================
	AWingActor* Wing = ParamotorPC ? ParamotorPC->SpawnedWingActor : nullptr;
	if (Wing)
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

		// Pendulum angle (only meaningful in flight)
		if (PendulumValue)
		{
			if (WState == EWingState::Flying)
			{
				float PendDeg = Wing->GetPendulumAngle() * 180.0f / PI;
				PendulumValue->SetText(FText::FromString(FString::Printf(TEXT("%.0f\x00B0"), PendDeg)));
				// Color: green near neutral, yellow at extremes, red at very extreme
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
			float WtSupport = (1.0f - GravScale) * 100.0f;
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

		// Pilot/gear total weight + wing model info
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
				float airMph = Wing->GetDebugGroundAirspeed() * 2.23694f;
				float grdMph = Wing->GetDebugGroundSpeed() * 2.23694f;
				float headMph = Wing->GetDebugHeadwind() * 2.23694f;
				float threshMph = Wing->GetDebugTakeoffThreshold() * 2.23694f;
				float thr = Wing->GetDebugThrottle();
				float support = Wing->GetDebugWeightSupport() * 100.0f;
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
	else
	{
		if (WingStateText) WingStateText->SetText(FText::FromString(TEXT("NONE")));
		if (PendulumValue) PendulumValue->SetText(FText::FromString(TEXT("--")));
		if (WeightSupportValue) WeightSupportValue->SetText(FText::FromString(TEXT("--")));
		if (PilotWeightValue) PilotWeightValue->SetText(FText::FromString(TEXT("--")));
		if (TakeoffChanceValue) TakeoffChanceValue->SetText(FText::FromString(TEXT("--")));
		if (DebugTakeoffValue) DebugTakeoffValue->SetVisibility(ESlateVisibility::Collapsed);
	}

	// ============================================================
	// INJURY STATUS
	// ============================================================
	if (InjuryStatusValue)
	{
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
				FString::Printf(TEXT("INJURY: %s (%d%%)"), *InjuryName, FMath::RoundToInt(Severity * 100.0f))));
			InjuryStatusValue->SetColorAndOpacity(FSlateColor(InjuryColor));
			InjuryStatusValue->SetVisibility(ESlateVisibility::Visible);
		}
		else
		{
			InjuryStatusValue->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	// ============================================================
	// TRIMMER STATUS
	// ============================================================
	if (TrimmerStatusValue)
	{
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
				FString::Printf(TEXT("TRIM: %s  [%s]  %+.0f%%"), *Label, *Bar, Pos * 100.0f)));
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
			if (HangPt > 0.1f) HangStr = FString::Printf(TEXT("LEAN FWD %+.0f%%"), HangPt * 100.0f);
			else if (HangPt < -0.1f) HangStr = FString::Printf(TEXT("LEAN BACK %+.0f%%"), HangPt * 100.0f);
			else HangStr = TEXT("NEUTRAL");

			if (TrimmerStatusValue)
			{
				FString Existing = TrimmerStatusValue->GetText().ToString();
				TrimmerStatusValue->SetText(FText::FromString(
					FString::Printf(TEXT("%s  |  POS: %s  |  HANG: %s"), *Existing, *SeatStr, *HangStr)));
			}
		}
	}

	// ============================================================
	// Context-sensitive on-screen instructions
	// ============================================================
	FString Instruction;
	if (InstructionText && InstructionPanel)
	{
		bool bHasParamotor = (Paramotor != nullptr);
		bool bParamotorEquipped = bHasParamotor && Paramotor->IsEquipped();
		AWingActor* InstrWing = ParamotorPC ? ParamotorPC->SpawnedWingActor : nullptr;
		bool bHasWing = (InstrWing != nullptr);

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
				FVector Wd2 = InstrWing->GetWindDirection();
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
}
