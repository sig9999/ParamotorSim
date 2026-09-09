// Copyright Epic Games, Inc. All Rights Reserved.

#include "UW_PauseMenu.h"
#include "ParamotorPlayerController.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/TextBlock.h"
#include "Components/Border.h"
#include "Components/BorderSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Spacer.h"
#include "Components/Slider.h"
#include "Components/CheckBox.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Engine/Engine.h"
#include "GameFramework/GameUserSettings.h"
#include "Engine/GameViewportClient.h"

// ============================================================
// COLOR PALETTE - MSFS-inspired premium dark theme
// ============================================================

static const FLinearColor Col_BgDim       = FLinearColor(0.005f, 0.008f, 0.015f, 0.88f);
static const FLinearColor Col_Panel       = FLinearColor(0.015f, 0.02f, 0.035f, 0.97f);
static const FLinearColor Col_Sidebar     = FLinearColor(0.01f, 0.015f, 0.025f, 1.0f);
static const FLinearColor Col_Separator   = FLinearColor(0.08f, 0.1f, 0.15f, 1.0f);
static const FLinearColor Col_Accent      = FLinearColor(0.15f, 0.55f, 0.95f, 1.0f);
static const FLinearColor Col_AccentDim   = FLinearColor(0.08f, 0.3f, 0.5f, 0.6f);
static const FLinearColor Col_TextBright  = FLinearColor(0.95f, 0.96f, 0.98f, 1.0f);
static const FLinearColor Col_TextDim     = FLinearColor(0.45f, 0.48f, 0.55f, 1.0f);
static const FLinearColor Col_TextLabel   = FLinearColor(0.6f, 0.63f, 0.7f, 1.0f);
static const FLinearColor Col_BtnNormal   = FLinearColor(0.03f, 0.04f, 0.07f, 0.8f);
static const FLinearColor Col_BtnHover    = FLinearColor(0.06f, 0.09f, 0.16f, 0.9f);
static const FLinearColor Col_BtnPressed  = FLinearColor(0.1f, 0.14f, 0.24f, 1.0f);
static const FLinearColor Col_CardBg      = FLinearColor(0.025f, 0.035f, 0.06f, 0.9f);
static const FLinearColor Col_CardBorder  = FLinearColor(0.1f, 0.12f, 0.18f, 0.8f);
static const FLinearColor Col_Warning     = FLinearColor(0.9f, 0.5f, 0.25f, 1.0f);
static const FLinearColor Col_Danger      = FLinearColor(0.85f, 0.3f, 0.3f, 1.0f);
static const FLinearColor Col_Success     = FLinearColor(0.3f, 0.75f, 0.45f, 1.0f);

// Helper: apply button colors without deprecated WidgetStyle direct access
static void ApplyButtonColors(UButton* Btn, FLinearColor Normal, FLinearColor Hovered, FLinearColor Pressed,
	FMargin NormalPad, FMargin PressedPad)
{
	if (!Btn) return;
	FButtonStyle Style = Btn->GetStyle();
	Style.Normal.TintColor = FSlateColor(Normal);
	Style.Hovered.TintColor = FSlateColor(Hovered);
	Style.Pressed.TintColor = FSlateColor(Pressed);
	Style.NormalPadding = NormalPad;
	Style.PressedPadding = PressedPad;
	Btn->SetStyle(Style);
}

// ============================================================
// WIDGET FACTORY HELPERS
// ============================================================

UTextBlock* UUW_PauseMenu::MakeText(const FString& Name, const FString& Text, int32 Size, FLinearColor Color, bool bBold)
{
	UTextBlock* TB = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), FName(*Name));
	TB->SetText(FText::FromString(Text));
	TB->SetColorAndOpacity(FSlateColor(Color));
	FSlateFontInfo Font = TB->GetFont();
	Font.Size = Size;
	if (bBold) Font.TypefaceFontName = FName("Bold");
	TB->SetFont(Font);
	return TB;
}

UTextBlock* UUW_PauseMenu::MakeLeftText(const FString& Name, const FString& Text, int32 Size, FLinearColor Color)
{
	UTextBlock* TB = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), FName(*Name));
	TB->SetText(FText::FromString(Text));
	TB->SetJustification(ETextJustify::Left);
	TB->SetColorAndOpacity(FSlateColor(Color));
	FSlateFontInfo Font = TB->GetFont();
	Font.Size = Size;
	TB->SetFont(Font);
	return TB;
}

UButton* UUW_PauseMenu::CreateCardButton(const FString& Name, const FString& Label, int32 FontSize, FLinearColor TextColor)
{
	UButton* Btn = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(*Name));
	ApplyButtonColors(Btn, Col_BtnNormal, Col_BtnHover, Col_BtnPressed, FMargin(16.0f, 12.0f), FMargin(16.0f, 14.0f));

	UTextBlock* Txt = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), FName(*(Name + "Txt")));
	Txt->SetText(FText::FromString(Label));
	Txt->SetJustification(ETextJustify::Left);
	Txt->SetColorAndOpacity(FSlateColor(TextColor));
	FSlateFontInfo Font = Txt->GetFont();
	Font.Size = FontSize;
	Txt->SetFont(Font);
	Btn->AddChild(Txt);
	if (UButtonSlot* BS = Cast<UButtonSlot>(Txt->Slot))
	{
		BS->SetPadding(FMargin(0.0f));
		BS->SetHorizontalAlignment(HAlign_Fill);
		BS->SetVerticalAlignment(VAlign_Center);
	}
	return Btn;
}

UButton* UUW_PauseMenu::CreatePillButton(const FString& Name, const FString& Label, int32 FontSize)
{
	UButton* Btn = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(*Name));
	ApplyButtonColors(Btn, Col_BtnNormal, Col_BtnHover, Col_BtnPressed, FMargin(4.0f, 8.0f), FMargin(4.0f, 10.0f));

	UTextBlock* Txt = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), FName(*(Name + "Txt")));
	Txt->SetText(FText::FromString(Label));
	Txt->SetJustification(ETextJustify::Center);
	Txt->SetColorAndOpacity(FSlateColor(Col_TextDim));
	FSlateFontInfo Font = Txt->GetFont();
	Font.Size = FontSize;
	Txt->SetFont(Font);
	Btn->AddChild(Txt);
	if (UButtonSlot* BS = Cast<UButtonSlot>(Txt->Slot))
	{
		BS->SetPadding(FMargin(0.0f));
		BS->SetHorizontalAlignment(HAlign_Fill);
		BS->SetVerticalAlignment(VAlign_Center);
	}
	return Btn;
}

void UUW_PauseMenu::CreateSectionHeader(UVerticalBox* Container, const FString& Title)
{
	UHorizontalBox* Header = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), FName(*(Title + "Header")));
	Container->AddChild(Header);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(Header->Slot))
	{
		VS->SetPadding(FMargin(0.0f, 16.0f, 0.0f, 8.0f));
		VS->SetHorizontalAlignment(HAlign_Fill);
	}

	UBorder* AccentBar = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), FName(*(Title + "Accent")));
	AccentBar->SetBrushColor(Col_Accent);
	Header->AddChild(AccentBar);
	if (UHorizontalBoxSlot* HS = Cast<UHorizontalBoxSlot>(AccentBar->Slot))
	{
		HS->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		HS->SetPadding(FMargin(0.0f, 0.0f, 12.0f, 0.0f));
	}

	UTextBlock* Label = MakeLeftText(Title + "Label", Title, 11, Col_TextLabel);
	Header->AddChild(Label);
	if (UHorizontalBoxSlot* HS = Cast<UHorizontalBoxSlot>(Label->Slot))
	{
		HS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		HS->SetVerticalAlignment(VAlign_Center);
	}
}

void UUW_PauseMenu::AddKeybindRow(UVerticalBox* Container, const FString& Action, const FString& Key)
{
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), FName(*(Action + "Row")));
	Container->AddChild(Row);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(Row->Slot))
	{
		VS->SetPadding(FMargin(0.0f, 4.0f));
		VS->SetHorizontalAlignment(HAlign_Fill);
	}

	UTextBlock* ActionTxt = MakeLeftText(Action + "Label", Action, 12, Col_TextLabel);
	Row->AddChild(ActionTxt);
	if (UHorizontalBoxSlot* HS = Cast<UHorizontalBoxSlot>(ActionTxt->Slot))
	{
		HS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}

	UBorder* KeyCap = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), FName(*(Action + "KeyCap")));
	KeyCap->SetBrushColor(FLinearColor(0.06f, 0.08f, 0.13f, 0.9f));
	KeyCap->SetPadding(FMargin(8.0f, 2.0f, 8.0f, 2.0f));
	Row->AddChild(KeyCap);
	if (UHorizontalBoxSlot* HS = Cast<UHorizontalBoxSlot>(KeyCap->Slot))
	{
		HS->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		HS->SetPadding(FMargin(8.0f, 0.0f, 0.0f, 0.0f));
	}

	UTextBlock* KeyTxt = MakeLeftText(Action + "Key", Key, 11, Col_Accent);
	KeyTxt->SetJustification(ETextJustify::Center);
	KeyCap->AddChild(KeyTxt);
	if (UBorderSlot* BS = Cast<UBorderSlot>(KeyTxt->Slot))
	{
		BS->SetPadding(FMargin(0.0f));
		BS->SetHorizontalAlignment(HAlign_Center);
		BS->SetVerticalAlignment(VAlign_Center);
	}
}

void UUW_PauseMenu::CreateNavButton(const FString& Name, const FString& Label, UButton*& OutBtn, UTextBlock*& OutTxt, UBorder*& OutAccent)
{
	UButton* Btn = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(*Name));
	ApplyButtonColors(Btn, FLinearColor(0.0f, 0.0f, 0.0f, 0.0f), FLinearColor(0.04f, 0.06f, 0.1f, 0.6f),
		FLinearColor(0.06f, 0.09f, 0.15f, 0.8f), FMargin(0.0f, 10.0f), FMargin(0.0f, 12.0f));

	UHorizontalBox* NavLayout = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), FName(*(Name + "Layout")));
	Btn->AddChild(NavLayout);
	if (UButtonSlot* BS = Cast<UButtonSlot>(NavLayout->Slot))
	{
		BS->SetPadding(FMargin(0.0f));
		BS->SetHorizontalAlignment(HAlign_Fill);
		BS->SetVerticalAlignment(VAlign_Fill);
	}

	UBorder* Accent = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), FName(*(Name + "AccentBar")));
	Accent->SetBrushColor(Col_Accent);
	NavLayout->AddChild(Accent);
	if (UHorizontalBoxSlot* HS = Cast<UHorizontalBoxSlot>(Accent->Slot))
	{
		HS->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		HS->SetPadding(FMargin(0.0f, 0.0f, 14.0f, 0.0f));
	}

	UTextBlock* Txt = MakeLeftText(Name + "Txt", Label, 14, Col_TextDim);
	NavLayout->AddChild(Txt);
	if (UHorizontalBoxSlot* HS = Cast<UHorizontalBoxSlot>(Txt->Slot))
	{
		HS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		HS->SetVerticalAlignment(VAlign_Center);
	}

	OutBtn = Btn;
	OutTxt = Txt;
	OutAccent = Accent;
}

UVerticalBox* UUW_PauseMenu::CreateTabPanel(const FString& Name)
{
	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), FName(*Name));
	Panel->SetBrushColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
	Panel->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	Panel->SetPadding(FMargin(0.0f));
	ContentCanvas->AddChild(Panel);
	if (UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(Panel->Slot))
	{
		S->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
		S->SetOffsets(FMargin(0.0f));
	}

	UScrollBox* Scroller = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), FName(*(Name + "Scroll")));
	Panel->AddChild(Scroller);
	if (UBorderSlot* BS = Cast<UBorderSlot>(Scroller->Slot))
	{
		BS->SetPadding(FMargin(32.0f, 28.0f));
		BS->SetHorizontalAlignment(HAlign_Fill);
		BS->SetVerticalAlignment(VAlign_Fill);
	}

	UVerticalBox* VBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), FName(*(Name + "VBox")));
	Scroller->AddChild(VBox);
	if (UScrollBoxSlot* SS = Cast<UScrollBoxSlot>(VBox->Slot))
	{
		SS->SetHorizontalAlignment(HAlign_Fill);
	}

	return VBox;
}

void UUW_PauseMenu::CreateWingCard(const FString& Name, const FString& WingName,
	const FString& Specs, FLinearColor AccentColor, UButton*& OutBtn)
{
	UButton* Btn = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(*Name));
	ApplyButtonColors(Btn, Col_CardBg, Col_BtnHover, Col_BtnPressed, FMargin(16.0f, 14.0f), FMargin(16.0f, 16.0f));

	UVerticalBox* CardContent = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), FName(*(Name + "Content")));
	Btn->AddChild(CardContent);
	if (UButtonSlot* BS = Cast<UButtonSlot>(CardContent->Slot))
	{
		BS->SetPadding(FMargin(0.0f));
		BS->SetHorizontalAlignment(HAlign_Fill);
		BS->SetVerticalAlignment(VAlign_Fill);
	}

	UHorizontalBox* NameRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), FName(*(Name + "NameRow")));
	CardContent->AddChild(NameRow);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(NameRow->Slot))
	{
		VS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 4.0f));
		VS->SetHorizontalAlignment(HAlign_Fill);
	}

	UBorder* Dot = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), FName(*(Name + "Dot")));
	Dot->SetBrushColor(AccentColor);
	Dot->SetPadding(FMargin(0.0f));
	NameRow->AddChild(Dot);
	if (UHorizontalBoxSlot* HS = Cast<UHorizontalBoxSlot>(Dot->Slot))
	{
		HS->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		HS->SetPadding(FMargin(0.0f, 0.0f, 10.0f, 0.0f));
	}

	UTextBlock* NameTxt = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), FName(*(Name + "NameTxt")));
	NameTxt->SetText(FText::FromString(WingName));
	NameTxt->SetJustification(ETextJustify::Left);
	NameTxt->SetColorAndOpacity(FSlateColor(Col_TextBright));
	FSlateFontInfo NameFont = NameTxt->GetFont();
	NameFont.Size = 16;
	NameFont.TypefaceFontName = FName("Bold");
	NameTxt->SetFont(NameFont);
	NameRow->AddChild(NameTxt);
	if (UHorizontalBoxSlot* HS = Cast<UHorizontalBoxSlot>(NameTxt->Slot))
	{
		HS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		HS->SetVerticalAlignment(VAlign_Center);
	}

	UTextBlock* SpecsTxt = MakeLeftText(Name + "Specs", Specs, 11, Col_TextDim);
	CardContent->AddChild(SpecsTxt);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(SpecsTxt->Slot))
	{
		VS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 0.0f));
		VS->SetHorizontalAlignment(HAlign_Fill);
	}

	OutBtn = Btn;
}

// ============================================================
// BUILDER: ROOT LAYOUT
// ============================================================

void UUW_PauseMenu::BuildRootLayout()
{
	RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
	WidgetTree->RootWidget = RootCanvas;

	// Full-screen dim background
	UBorder* Dim = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Dim"));
	Dim->SetBrushColor(Col_BgDim);
	Dim->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	Dim->SetPadding(FMargin(0.0f));
	RootCanvas->AddChild(Dim);
	if (UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(Dim->Slot))
	{
		S->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
		S->SetOffsets(FMargin(0.0f));
	}

	// Main panel
	const float PanelW = 920.0f;
	const float PanelH = 640.0f;

	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Panel"));
	Panel->SetBrushColor(Col_Panel);
	Panel->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	Panel->SetPadding(FMargin(0.0f));
	RootCanvas->AddChild(Panel);
	if (UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(Panel->Slot))
	{
		S->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
		S->SetPosition(FVector2D(-PanelW / 2.0f, -PanelH / 2.0f));
		S->SetSize(FVector2D(PanelW, PanelH));
	}

	// Panel layout: [Sidebar | Edge | Content]
	UHorizontalBox* PanelLayout = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("PanelLayout"));
	Panel->AddChild(PanelLayout);
	if (UBorderSlot* BS = Cast<UBorderSlot>(PanelLayout->Slot))
	{
		BS->SetPadding(FMargin(0.0f));
		BS->SetHorizontalAlignment(HAlign_Fill);
		BS->SetVerticalAlignment(VAlign_Fill);
	}

	// Sidebar container
	UBorder* Sidebar = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Sidebar"));
	Sidebar->SetBrushColor(Col_Sidebar);
	Sidebar->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	Sidebar->SetPadding(FMargin(0.0f));
	PanelLayout->AddChild(Sidebar);
	if (UHorizontalBoxSlot* HS = Cast<UHorizontalBoxSlot>(Sidebar->Slot))
	{
		HS->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
	}

	UVerticalBox* SidebarContent = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("SidebarContent"));
	Sidebar->AddChild(SidebarContent);
	if (UBorderSlot* BS = Cast<UBorderSlot>(SidebarContent->Slot))
	{
		BS->SetPadding(FMargin(24.0f, 28.0f, 20.0f, 20.0f));
		BS->SetHorizontalAlignment(HAlign_Fill);
		BS->SetVerticalAlignment(VAlign_Fill);
	}

	// Sidebar edge separator
	UBorder* SidebarEdge = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("SidebarEdge"));
	SidebarEdge->SetBrushColor(Col_Separator);
	SidebarEdge->SetVisibility(ESlateVisibility::HitTestInvisible);
	PanelLayout->AddChild(SidebarEdge);
	if (UHorizontalBoxSlot* HS = Cast<UHorizontalBoxSlot>(SidebarEdge->Slot))
	{
		HS->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
	}

	// Content area
	UBorder* ContentArea = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ContentArea"));
	ContentArea->SetBrushColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
	ContentArea->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	ContentArea->SetPadding(FMargin(0.0f));
	PanelLayout->AddChild(ContentArea);
	if (UHorizontalBoxSlot* HS = Cast<UHorizontalBoxSlot>(ContentArea->Slot))
	{
		HS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}

	ContentCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("ContentCanvas"));
	ContentArea->AddChild(ContentCanvas);
	if (UBorderSlot* BS = Cast<UBorderSlot>(ContentCanvas->Slot))
	{
		BS->SetPadding(FMargin(0.0f));
		BS->SetHorizontalAlignment(HAlign_Fill);
		BS->SetVerticalAlignment(VAlign_Fill);
	}

	// Store sidebar content for BuildSidebar
	SidebarContentBox = SidebarContent;
}

// ============================================================
// BUILDER: SIDEBAR
// ============================================================

void UUW_PauseMenu::BuildSidebar()
{
	UVerticalBox* SidebarContent = SidebarContentBox;

	// Logo / Title
	UTextBlock* Logo = MakeText("Logo", "PARAMOTOR", 18, Col_TextBright, true);
	Logo->SetJustification(ETextJustify::Left);
	SidebarContent->AddChild(Logo);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(Logo->Slot))
	{
		VS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 2.0f));
		VS->SetHorizontalAlignment(HAlign_Fill);
	}

	UTextBlock* Subtitle = MakeText("Subtitle", "SIMULATOR", 10, Col_Accent);
	Subtitle->SetJustification(ETextJustify::Left);
	SidebarContent->AddChild(Subtitle);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(Subtitle->Slot))
	{
		VS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 24.0f));
		VS->SetHorizontalAlignment(HAlign_Fill);
	}

	// Nav buttons
	auto AddNavButton = [this, SidebarContent](const FString& Name, const FString& Label, UButton*& OutBtn)
	{
		UTextBlock* Txt;
		UBorder* Accent;
		CreateNavButton(Name, Label, OutBtn, Txt, Accent);
		SidebarContent->AddChild(OutBtn);
		if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(OutBtn->Slot))
		{
			VS->SetPadding(FMargin(0.0f, 2.0f));
			VS->SetHorizontalAlignment(HAlign_Fill);
		}
	};

	AddNavButton("TabMain", "MAIN", TabMainBtn);
	TabMainBtn->OnClicked.AddDynamic(this, &UUW_PauseMenu::OnTabMainClicked);

	AddNavButton("TabSpawn", "SPAWN", TabSpawnBtn);
	TabSpawnBtn->OnClicked.AddDynamic(this, &UUW_PauseMenu::OnTabSpawnClicked);

	AddNavButton("TabOptions", "OPTIONS", TabOptionsBtn);
	TabOptionsBtn->OnClicked.AddDynamic(this, &UUW_PauseMenu::OnTabOptionsClicked);

	// Spacer
	USpacer* SidebarSpacer = WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass(), TEXT("SidebarSpacer"));
	SidebarContent->AddChild(SidebarSpacer);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(SidebarSpacer->Slot))
	{
		VS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}

	// Footer separator + ESC hint
	UBorder* FooterSeparator = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("FooterSep"));
	FooterSeparator->SetBrushColor(Col_Separator);
	FooterSeparator->SetVisibility(ESlateVisibility::HitTestInvisible);
	SidebarContent->AddChild(FooterSeparator);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(FooterSeparator->Slot))
	{
		VS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 12.0f));
		VS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		VS->SetHorizontalAlignment(HAlign_Fill);
	}

	UTextBlock* EscHint = MakeText("EscHint", "ESC to resume", 10, Col_TextDim);
	EscHint->SetJustification(ETextJustify::Left);
	SidebarContent->AddChild(EscHint);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(EscHint->Slot))
	{
		VS->SetHorizontalAlignment(HAlign_Fill);
	}
}

// ============================================================
// BUILDER: MAIN TAB
// ============================================================

void UUW_PauseMenu::BuildMainTab()
{
	MainTabContent = CreateTabPanel("MainTab");

	UTextBlock* Title = MakeText("MainTitle", "Flight", 28, Col_TextBright, true);
	Title->SetJustification(ETextJustify::Left);
	MainTabContent->AddChild(Title);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(Title->Slot))
	{
		VS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 4.0f));
		VS->SetHorizontalAlignment(HAlign_Fill);
	}

	UTextBlock* Desc = MakeText("MainDesc", "Resume your flight or exit the simulator", 12, Col_TextDim);
	Desc->SetJustification(ETextJustify::Left);
	MainTabContent->AddChild(Desc);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(Desc->Slot))
	{
		VS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 24.0f));
		VS->SetHorizontalAlignment(HAlign_Fill);
	}

	UButton* ResumeBtn = CreateCardButton("ResumeBtn", "RESUME FLIGHT", 15, Col_Success);
	MainTabContent->AddChild(ResumeBtn);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(ResumeBtn->Slot))
	{
		VS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
		VS->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		VS->SetHorizontalAlignment(HAlign_Fill);
	}
	ResumeBtn->OnClicked.AddDynamic(this, &UUW_PauseMenu::OnResumeClicked);

	USpacer* Spacer = WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass(), TEXT("MainSpacer"));
	MainTabContent->AddChild(Spacer);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(Spacer->Slot))
	{
		VS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		VS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
	}

	UButton* QuitBtn = CreateCardButton("QuitBtn", "QUIT TO DESKTOP", 14, Col_Danger);
	MainTabContent->AddChild(QuitBtn);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(QuitBtn->Slot))
	{
		VS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
		VS->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		VS->SetHorizontalAlignment(HAlign_Fill);
	}
	QuitBtn->OnClicked.AddDynamic(this, &UUW_PauseMenu::OnQuitClicked);
}

// ============================================================
// BUILDER: SPAWN TAB
// ============================================================

void UUW_PauseMenu::BuildSpawnTab()
{
	SpawnTabContent = CreateTabPanel("SpawnTab");

	UTextBlock* Title = MakeText("SpawnTitle", "Equipment", 28, Col_TextBright, true);
	Title->SetJustification(ETextJustify::Left);
	SpawnTabContent->AddChild(Title);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(Title->Slot))
	{
		VS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 4.0f));
		VS->SetHorizontalAlignment(HAlign_Fill);
	}

	UTextBlock* Desc = MakeText("SpawnDesc", "Spawn your paramotor and wing to begin flying", 12, Col_TextDim);
	Desc->SetJustification(ETextJustify::Left);
	SpawnTabContent->AddChild(Desc);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(Desc->Slot))
	{
		VS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
		VS->SetHorizontalAlignment(HAlign_Fill);
	}

	// Paramotor section
	CreateSectionHeader(SpawnTabContent, "PARAMOTOR");

	UButton* SpawnParamBtn = CreateCardButton("SpawnTestParamBtn", "Spawn Test Paramotor", 15, Col_TextBright);
	SpawnTabContent->AddChild(SpawnParamBtn);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(SpawnParamBtn->Slot))
	{
		VS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 4.0f));
		VS->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		VS->SetHorizontalAlignment(HAlign_Fill);
	}
	SpawnParamBtn->OnClicked.AddDynamic(this, &UUW_PauseMenu::OnSpawnTestParamotorClicked);

	UButton* SpawnRiderBtn = CreateCardButton("SpawnRider185Btn", "Spawn Rider 185", 15, Col_TextBright);
	SpawnTabContent->AddChild(SpawnRiderBtn);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(SpawnRiderBtn->Slot))
	{
		VS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 4.0f));
		VS->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		VS->SetHorizontalAlignment(HAlign_Fill);
	}
	SpawnRiderBtn->OnClicked.AddDynamic(this, &UUW_PauseMenu::OnSpawnRider185Clicked);

	UTextBlock* ParamDesc = MakeLeftText("TestParamDesc", "Spawns in front of you. Walk to it and press F to equip.", 11, Col_TextDim);
	SpawnTabContent->AddChild(ParamDesc);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(ParamDesc->Slot))
	{
		VS->SetPadding(FMargin(16.0f, 0.0f, 0.0f, 8.0f));
		VS->SetHorizontalAlignment(HAlign_Fill);
	}

	// Wing section
	CreateSectionHeader(SpawnTabContent, "WING");

	UButton* MacParaBtn = nullptr;
	CreateWingCard("MacParaTestWingBtn", "MacPara Charger 25M",
		"Physical canopy with cell openings  |  46 cells  |  Internal ribs + cross-ports  |  6,024 verts",
		FLinearColor(0.15f, 0.45f, 0.85f, 1.0f), MacParaBtn);
	SpawnTabContent->AddChild(MacParaBtn);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(MacParaBtn->Slot))
	{
		VS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
		VS->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		VS->SetHorizontalAlignment(HAlign_Fill);
	}
	MacParaBtn->OnClicked.AddDynamic(this, &UUW_PauseMenu::OnSpawnTestWingClicked);

	UTextBlock* WingHelp = MakeLeftText("WingHelp", "Spawns behind you. Connect risers with F, then hold R to inflate.", 11, Col_TextDim);
	SpawnTabContent->AddChild(WingHelp);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(WingHelp->Slot))
	{
		VS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 16.0f));
		VS->SetHorizontalAlignment(HAlign_Fill);
	}

	// Cleanup section
	CreateSectionHeader(SpawnTabContent, "RANGE MANAGEMENT");

	UButton* ClearBtn = CreateCardButton("ClearBtn", "Clear All Spawned Equipment", 14, Col_Warning);
	SpawnTabContent->AddChild(ClearBtn);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(ClearBtn->Slot))
	{
		VS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
		VS->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		VS->SetHorizontalAlignment(HAlign_Fill);
	}
	ClearBtn->OnClicked.AddDynamic(this, &UUW_PauseMenu::OnClearSpawnedClicked);
}

// ============================================================
// BUILDER: OPTIONS TAB
// ============================================================

void UUW_PauseMenu::BuildOptionsTab()
{
	OptionsTabContent = CreateTabPanel("OptionsTab");

	UTextBlock* Title = MakeText("OptTitle", "Options", 28, Col_TextBright, true);
	Title->SetJustification(ETextJustify::Left);
	OptionsTabContent->AddChild(Title);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(Title->Slot))
	{
		VS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 4.0f));
		VS->SetHorizontalAlignment(HAlign_Fill);
	}

	UTextBlock* Desc = MakeText("OptDesc", "Graphics quality and control bindings", 12, Col_TextDim);
	Desc->SetJustification(ETextJustify::Left);
	OptionsTabContent->AddChild(Desc);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(Desc->Slot))
	{
		VS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
		VS->SetHorizontalAlignment(HAlign_Fill);
	}

	BuildQualitySection(OptionsTabContent);
	BuildResolutionSection(OptionsTabContent);
	BuildDisplayTogglesSection(OptionsTabContent);
	BuildKeybindSection(OptionsTabContent);
}

void UUW_PauseMenu::BuildQualitySection(UVerticalBox* Container)
{
	CreateSectionHeader(Container, "GRAPHICS QUALITY");

	CurrentQualityLabel = MakeLeftText("CurrentQuality", "Current: HIGH", 12, Col_Accent);
	Container->AddChild(CurrentQualityLabel);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(CurrentQualityLabel->Slot))
	{
		VS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 10.0f));
		VS->SetHorizontalAlignment(HAlign_Fill);
	}

	UHorizontalBox* QualityRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("QualityRow"));
	Container->AddChild(QualityRow);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(QualityRow->Slot))
	{
		VS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 16.0f));
		VS->SetHorizontalAlignment(HAlign_Fill);
	}

	auto AddQualityBtn = [this, QualityRow](UButton*& Btn, const FString& Name, const FString& Label, float RightPad)
	{
		Btn = CreatePillButton(Name, Label, 12);
		QualityRow->AddChild(Btn);
		if (UHorizontalBoxSlot* HS = Cast<UHorizontalBoxSlot>(Btn->Slot))
		{
			HS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			HS->SetPadding(FMargin(0.0f, 0.0f, RightPad, 0.0f));
		}
	};

	AddQualityBtn(QualityLowBtn, "QLow", "LOW", 4.0f);
	QualityLowBtn->OnClicked.AddDynamic(this, &UUW_PauseMenu::OnQualityLowClicked);
	AddQualityBtn(QualityMedBtn, "QMed", "MEDIUM", 4.0f);
	QualityMedBtn->OnClicked.AddDynamic(this, &UUW_PauseMenu::OnQualityMediumClicked);
	AddQualityBtn(QualityHighBtn, "QHigh", "HIGH", 4.0f);
	QualityHighBtn->OnClicked.AddDynamic(this, &UUW_PauseMenu::OnQualityHighClicked);
	AddQualityBtn(QualityEpicBtn, "QEpic", "EPIC", 0.0f);
	QualityEpicBtn->OnClicked.AddDynamic(this, &UUW_PauseMenu::OnQualityEpicClicked);
}

void UUW_PauseMenu::BuildResolutionSection(UVerticalBox* Container)
{
	CreateSectionHeader(Container, "RESOLUTION SCALE");

	UHorizontalBox* ResRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("ResRow"));
	Container->AddChild(ResRow);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(ResRow->Slot))
	{
		VS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 4.0f));
		VS->SetHorizontalAlignment(HAlign_Fill);
	}

	UTextBlock* ResLabel = MakeLeftText("ResLabel", "Render Scale", 12, Col_TextLabel);
	ResRow->AddChild(ResLabel);
	if (UHorizontalBoxSlot* HS = Cast<UHorizontalBoxSlot>(ResLabel->Slot))
	{
		HS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		HS->SetVerticalAlignment(VAlign_Center);
	}

	ResolutionScaleLabel = MakeText("ResScaleValue", "100%", 13, Col_Accent);
	ResolutionScaleLabel->SetJustification(ETextJustify::Right);
	ResRow->AddChild(ResolutionScaleLabel);
	if (UHorizontalBoxSlot* HS = Cast<UHorizontalBoxSlot>(ResolutionScaleLabel->Slot))
	{
		HS->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		HS->SetVerticalAlignment(VAlign_Center);
	}

	USlider* ResScaleSlider = WidgetTree->ConstructWidget<USlider>(USlider::StaticClass(), TEXT("ResScaleSlider"));
	ResScaleSlider->SetMinValue(0.5f);
	ResScaleSlider->SetMaxValue(1.0f);
	ResScaleSlider->SetValue(1.0f);
	ResScaleSlider->SetSliderBarColor(FLinearColor(0.08f, 0.1f, 0.15f, 1.0f));
	ResScaleSlider->SetSliderHandleColor(Col_Accent);
	Container->AddChild(ResScaleSlider);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(ResScaleSlider->Slot))
	{
		VS->SetPadding(FMargin(0.0f, 8.0f, 0.0f, 16.0f));
		VS->SetHorizontalAlignment(HAlign_Fill);
	}
	ResScaleSlider->OnValueChanged.AddDynamic(this, &UUW_PauseMenu::OnResolutionScaleChanged);
}

UCheckBox* UUW_PauseMenu::CreateLabeledCheckbox(UHorizontalBox* ParentRow, const FString& BoxName,
	const FString& LabelText, float SlotPadLeft, float SlotPadRight)
{
	UVerticalBox* CheckBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), FName(*BoxName));
	ParentRow->AddChild(CheckBox);
	if (UHorizontalBoxSlot* HS = Cast<UHorizontalBoxSlot>(CheckBox->Slot))
	{
		HS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		HS->SetPadding(FMargin(SlotPadLeft, 0.0f, SlotPadRight, 0.0f));
	}

	UTextBlock* Label = MakeLeftText(BoxName + TEXT("Label"), LabelText, 13, Col_TextBright);
	CheckBox->AddChild(Label);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(Label->Slot))
	{
		VS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 6.0f));
		VS->SetHorizontalAlignment(HAlign_Fill);
	}

	UCheckBox* Check = WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass(), FName(*(BoxName + TEXT("Check"))));
	Check->SetCheckedState(ECheckBoxState::Checked);
	FCheckBoxStyle Style = Check->GetWidgetStyle();
	Style.CheckedImage.TintColor = FSlateColor(Col_Accent);
	Style.UncheckedImage.TintColor = FSlateColor(FLinearColor(0.15f, 0.18f, 0.25f, 1.0f));
	Check->SetWidgetStyle(Style);
	CheckBox->AddChild(Check);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(Check->Slot))
		VS->SetHorizontalAlignment(HAlign_Left);

	return Check;
}

void UUW_PauseMenu::BuildDisplayTogglesSection(UVerticalBox* Container)
{
	CreateSectionHeader(Container, "DISPLAY");

	UHorizontalBox* ToggleRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("ToggleRow"));
	Container->AddChild(ToggleRow);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(ToggleRow->Slot))
	{
		VS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 16.0f));
		VS->SetHorizontalAlignment(HAlign_Fill);
	}

	VSyncCheckBox = CreateLabeledCheckbox(ToggleRow, "VSync", "V-Sync", 0.0f, 16.0f);
	if (VSyncCheckBox)
		VSyncCheckBox->OnCheckStateChanged.AddDynamic(this, &UUW_PauseMenu::OnVSyncToggled);

	ShadowsCheckBox = CreateLabeledCheckbox(ToggleRow, "Shadows", "Shadows", 16.0f, 0.0f);
	if (ShadowsCheckBox)
		ShadowsCheckBox->OnCheckStateChanged.AddDynamic(this, &UUW_PauseMenu::OnShadowsToggled);
}

void UUW_PauseMenu::BuildKeybindSection(UVerticalBox* Container)
{
	CreateSectionHeader(Container, "KEY BINDINGS");

	AddKeybindRow(Container, "Move Forward / Back", "W / S");
	AddKeybindRow(Container, "Move Left / Right", "A / D");
	AddKeybindRow(Container, "Turn / Look", "Mouse");
	AddKeybindRow(Container, "Run / Jog", "Shift");
	AddKeybindRow(Container, "Interact / Equip", "F");
	AddKeybindRow(Container, "Start / Stop Engine", "P");
	AddKeybindRow(Container, "Throttle Up / Down", "Up / Down");
	AddKeybindRow(Container, "Lift A-Lines", "R");
	AddKeybindRow(Container, "Left Brake", "Q");
	AddKeybindRow(Container, "Right Brake", "E");
	AddKeybindRow(Container, "Trimmer Fast / Slow", "= / -");
	AddKeybindRow(Container, "Pause", "Esc");
}

// ============================================================
// INIT FROM CURRENT SETTINGS
// ============================================================

void UUW_PauseMenu::InitFromCurrentSettings()
{
	UGameUserSettings* GUS = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	if (!GUS) return;

	int32 OverallQ = GUS->GetOverallScalabilityLevel();
	FString QNames[] = {TEXT("LOW"), TEXT("MEDIUM"), TEXT("HIGH"), TEXT("EPIC"), TEXT("CINEMATIC")};
	FString QName = (OverallQ >= 0 && OverallQ <= 4) ? QNames[OverallQ] : TEXT("CUSTOM");
	if (IsValid(CurrentQualityLabel)) CurrentQualityLabel->SetText(FText::FromString(FString::Printf(TEXT("Current: %s"), *QName)));

	UButton* QBtns[] = {QualityLowBtn, QualityMedBtn, QualityHighBtn, QualityEpicBtn};
	int32 QIdx = FMath::Clamp(OverallQ, 0, 3);
	HighlightQualityButton(QBtns[QIdx]);

	if (VSyncCheckBox)
		VSyncCheckBox->SetCheckedState(GUS->IsVSyncEnabled() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked);

	if (ShadowsCheckBox)
		ShadowsCheckBox->SetCheckedState(GUS->GetShadowQuality() > 0 ? ECheckBoxState::Checked : ECheckBoxState::Unchecked);

	float ResPct = GUS->GetResolutionScaleNormalized();
	if (ResolutionScaleLabel)
		ResolutionScaleLabel->SetText(FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(ResPct * 100.0f))));
}

// ============================================================
// NATIVE INIT — slim dispatcher
// ============================================================

void UUW_PauseMenu::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true);

	BuildRootLayout();
	BuildSidebar();
	BuildMainTab();
	BuildSpawnTab();
	BuildOptionsTab();
	InitFromCurrentSettings();
	ShowMainTab();
}

// ============================================================
// TAB SWITCHING
// ============================================================

void UUW_PauseMenu::SetTabActive(UButton* ActiveBtn)
{
	auto SetActive = [](UTextBlock* Txt, UBorder* Accent, bool bActive)
	{
		if (Txt) Txt->SetColorAndOpacity(FSlateColor(bActive ? Col_TextBright : Col_TextDim));
		if (Accent) Accent->SetVisibility(bActive ? ESlateVisibility::Visible : ESlateVisibility::Hidden);
	};

	struct TabInfo { UButton* Btn; UTextBlock* Txt; UBorder* Accent; };
	TabInfo Tabs[] = {
		{TabMainBtn, TabMainTxt, TabMainAccent},
		{TabSpawnBtn, TabSpawnTxt, TabSpawnAccent},
		{TabOptionsBtn, TabOptionsTxt, TabOptionsAccent}
	};

	for (const auto& T : Tabs)
		SetActive(T.Txt, T.Accent, T.Btn == ActiveBtn);
}

void UUW_PauseMenu::HighlightQualityButton(UButton* ActiveBtn)
{
	auto SetBtn = [](UButton* Btn, FLinearColor Tint, FLinearColor TextColor)
	{
		if (!Btn) return;
		FButtonStyle Style = Btn->GetStyle();
		Style.Normal.TintColor = FSlateColor(Tint);
		Btn->SetStyle(Style);
		UTextBlock* Txt = Cast<UTextBlock>(Btn->GetChildAt(0));
		if (Txt) Txt->SetColorAndOpacity(FSlateColor(TextColor));
	};

	SetBtn(QualityLowBtn, Col_BtnNormal, Col_TextDim);
	SetBtn(QualityMedBtn, Col_BtnNormal, Col_TextDim);
	SetBtn(QualityHighBtn, Col_BtnNormal, Col_TextDim);
	SetBtn(QualityEpicBtn, Col_BtnNormal, Col_TextDim);

	if (ActiveBtn)
		SetBtn(ActiveBtn, FLinearColor(0.08f, 0.15f, 0.28f, 0.95f), Col_Accent);
}

void UUW_PauseMenu::ShowTab(UVerticalBox* ActiveContent)
{
	struct TabEntry { UVerticalBox* Content; };
	TabEntry Tabs[] = { {MainTabContent}, {SpawnTabContent}, {OptionsTabContent} };

	for (const auto& T : Tabs)
	{
		if (!T.Content) continue;
		bool bActive = (T.Content == ActiveContent);
		T.Content->SetVisibility(bActive ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		if (UPanelWidget* P = T.Content->GetParent())
		{
			if (UPanelWidget* PP = P->GetParent())
			{
				PP->SetVisibility(bActive ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
			}
		}
	}
}

void UUW_PauseMenu::ShowMainTab()    { ShowTab(MainTabContent);    SetTabActive(TabMainBtn); }
void UUW_PauseMenu::ShowSpawnTab()   { ShowTab(SpawnTabContent);   SetTabActive(TabSpawnBtn); }
void UUW_PauseMenu::ShowOptionsTab() { ShowTab(OptionsTabContent); SetTabActive(TabOptionsBtn); }

void UUW_PauseMenu::OnTabMainClicked()    { ShowMainTab(); }
void UUW_PauseMenu::OnTabSpawnClicked()   { ShowSpawnTab(); }
void UUW_PauseMenu::OnTabOptionsClicked() { ShowOptionsTab(); }

// ============================================================
// GRAPHICS HANDLERS — common setter + thin wrappers
// ============================================================

void UUW_PauseMenu::SetQualityLevel(int32 Level, UButton* Btn)
{
	UGameUserSettings* GUS = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	if (!GUS) return;
	GUS->SetOverallScalabilityLevel(Level);
	GUS->ApplySettings(true);
	HighlightQualityButton(Btn);

	static const TCHAR* QNames[] = {TEXT("LOW"), TEXT("MEDIUM"), TEXT("HIGH"), TEXT("EPIC")};
	if (CurrentQualityLabel && Level >= 0 && Level <= 3)
		CurrentQualityLabel->SetText(FText::FromString(FString::Printf(TEXT("Current: %s"), QNames[Level])));
}

void UUW_PauseMenu::OnQualityLowClicked()    { SetQualityLevel(0, QualityLowBtn); }
void UUW_PauseMenu::OnQualityMediumClicked() { SetQualityLevel(1, QualityMedBtn); }
void UUW_PauseMenu::OnQualityHighClicked()   { SetQualityLevel(2, QualityHighBtn); }
void UUW_PauseMenu::OnQualityEpicClicked()   { SetQualityLevel(3, QualityEpicBtn); }

void UUW_PauseMenu::OnVSyncToggled(bool bChecked)
{
	UGameUserSettings* GUS = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	if (GUS) { GUS->SetVSyncEnabled(bChecked); GUS->ApplySettings(true); }
}

void UUW_PauseMenu::OnShadowsToggled(bool bChecked)
{
	UGameUserSettings* GUS = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	if (GUS) { GUS->SetShadowQuality(bChecked ? 2 : 0); GUS->ApplySettings(true); }
}

void UUW_PauseMenu::OnResolutionScaleChanged(float Value)
{
	UGameUserSettings* GUS = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	if (GUS) { GUS->SetResolutionScaleNormalized(Value); GUS->ApplySettings(true); }
	if (ResolutionScaleLabel)
		ResolutionScaleLabel->SetText(FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Value * 100.0f))));
}

// ============================================================
// INPUT / ACTIONS
// ============================================================

FReply UUW_PauseMenu::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		OnResumeClicked();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

void UUW_PauseMenu::OnResumeClicked()
{
	if (AParamotorPlayerController* PC = Cast<AParamotorPlayerController>(GetOwningPlayer()))
		PC->TogglePauseMenu();
}

void UUW_PauseMenu::OnQuitClicked()
{
	UKismetSystemLibrary::QuitGame(GetWorld(), nullptr, EQuitPreference::Quit, false);
}

void UUW_PauseMenu::OnSpawnTestWingClicked()
{
	if (AParamotorPlayerController* PC = Cast<AParamotorPlayerController>(GetOwningPlayer()))
		PC->SpawnTestWing();
}

void UUW_PauseMenu::OnSpawnTestParamotorClicked()
{
	if (AParamotorPlayerController* PC = Cast<AParamotorPlayerController>(GetOwningPlayer()))
		PC->SpawnTestParamotor();
}

void UUW_PauseMenu::OnSpawnRider185Clicked()
{
	if (AParamotorPlayerController* PC = Cast<AParamotorPlayerController>(GetOwningPlayer()))
		PC->SpawnRider185();
}

void UUW_PauseMenu::OnClearSpawnedClicked()
{
	if (AParamotorPlayerController* PC = Cast<AParamotorPlayerController>(GetOwningPlayer()))
		PC->ClearSpawnedActors();
}
