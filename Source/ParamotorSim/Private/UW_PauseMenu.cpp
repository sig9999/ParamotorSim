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
static const FLinearColor Col_Accent      = FLinearColor(0.15f, 0.55f, 0.95f, 1.0f);  // MSFS blue
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

// ============================================================
// NATIVE INIT
// ============================================================

void UUW_PauseMenu::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	SetIsFocusable(true);

	// ---- Helpers ----

	auto MakeText = [this](const FString& Name, const FString& Text, int32 Size, FLinearColor Color, bool bBold = false)
	{
		UTextBlock* TB = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), FName(*Name));
		TB->SetText(FText::FromString(Text));
		TB->SetColorAndOpacity(FSlateColor(Color));
		FSlateFontInfo Font = TB->GetFont();
		Font.Size = Size;
		if (bBold) Font.TypefaceFontName = FName("Bold");
		TB->SetFont(Font);
		return TB;
	};

	auto MakeLeftText = [this](const FString& Name, const FString& Text, int32 Size, FLinearColor Color)
	{
		UTextBlock* TB = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), FName(*Name));
		TB->SetText(FText::FromString(Text));
		TB->SetJustification(ETextJustify::Left);
		TB->SetColorAndOpacity(FSlateColor(Color));
		FSlateFontInfo Font = TB->GetFont();
		Font.Size = Size;
		TB->SetFont(Font);
		return TB;
	};

	// Create a premium card-style button with left-aligned text and optional icon
	auto CreateCardButton = [this](const FString& Name, const FString& Label, int32 FontSize, FLinearColor TextColor)
	{
		UButton* Btn = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(*Name));
		Btn->WidgetStyle.Normal.TintColor = FSlateColor(Col_BtnNormal);
		Btn->WidgetStyle.Hovered.TintColor = FSlateColor(Col_BtnHover);
		Btn->WidgetStyle.Pressed.TintColor = FSlateColor(Col_BtnPressed);
		Btn->WidgetStyle.NormalPadding = FMargin(16.0f, 12.0f);
		Btn->WidgetStyle.PressedPadding = FMargin(16.0f, 14.0f);

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
	};

	// Create a compact pill button (for quality presets, etc.)
	auto CreatePillButton = [this](const FString& Name, const FString& Label, int32 FontSize)
	{
		UButton* Btn = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(*Name));
		Btn->WidgetStyle.Normal.TintColor = FSlateColor(Col_BtnNormal);
		Btn->WidgetStyle.Hovered.TintColor = FSlateColor(Col_BtnHover);
		Btn->WidgetStyle.Pressed.TintColor = FSlateColor(Col_BtnPressed);
		Btn->WidgetStyle.NormalPadding = FMargin(4.0f, 8.0f);
		Btn->WidgetStyle.PressedPadding = FMargin(4.0f, 10.0f);

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
	};

	// Create a section header with accent line
	auto CreateSectionHeader = [this, &MakeText, &MakeLeftText](UVerticalBox* Container, const FString& Title)
	{
		UHorizontalBox* Header = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), FName(*(Title + "Header")));
		Container->AddChild(Header);
		if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(Header->Slot))
		{
			VS->SetPadding(FMargin(0.0f, 16.0f, 0.0f, 8.0f));
			VS->SetHorizontalAlignment(HAlign_Fill);
		}

		// Accent bar
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
	};

	// Add a keybind row to a container
	auto AddKeybindRow = [this, &MakeLeftText](UVerticalBox* Container, const FString& Action, const FString& Key)
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

		// Key in a subtle "key cap" style box
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
	};

	// ============================================================
	// ROOT
	// ============================================================
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
	WidgetTree->RootWidget = Root;

	// ---- Full-screen dim background ----
	UBorder* Dim = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Dim"));
	Dim->SetBrushColor(Col_BgDim);
	Dim->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	Dim->SetPadding(FMargin(0.0f));
	Root->AddChild(Dim);
	if (UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(Dim->Slot))
	{
		S->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
		S->SetOffsets(FMargin(0.0f));
	}

	// ============================================================
	// MAIN PANEL - wide, centered, premium dark
	// ============================================================
	const float PanelW = 920.0f;
	const float PanelH = 640.0f;

	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Panel"));
	Panel->SetBrushColor(Col_Panel);
	Panel->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	Panel->SetPadding(FMargin(0.0f));
	Root->AddChild(Panel);
	if (UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(Panel->Slot))
	{
		S->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
		S->SetPosition(FVector2D(-PanelW / 2.0f, -PanelH / 2.0f));
		S->SetSize(FVector2D(PanelW, PanelH));
	}

	// Panel content is a horizontal box: [Sidebar | Content]
	UHorizontalBox* PanelLayout = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("PanelLayout"));
	Panel->AddChild(PanelLayout);
	if (UBorderSlot* BS = Cast<UBorderSlot>(PanelLayout->Slot))
	{
		BS->SetPadding(FMargin(0.0f));
		BS->SetHorizontalAlignment(HAlign_Fill);
		BS->SetVerticalAlignment(VAlign_Fill);
	}

	// ============================================================
	// LEFT SIDEBAR (navigation)
	// ============================================================
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

	// --- Logo / Title ---
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

	// --- Nav buttons ---
	// Each nav button is a horizontal box: [accent bar | text]
	// Active tab shows the accent bar, inactive hides it

	auto CreateNavButton = [this, &MakeLeftText](const FString& Name, const FString& Label, UButton*& OutBtn, UTextBlock*& OutTxt, UBorder*& OutAccent)
	{
		UButton* Btn = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(*Name));
		Btn->WidgetStyle.Normal.TintColor = FSlateColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
		Btn->WidgetStyle.Hovered.TintColor = FSlateColor(FLinearColor(0.04f, 0.06f, 0.1f, 0.6f));
		Btn->WidgetStyle.Pressed.TintColor = FSlateColor(FLinearColor(0.06f, 0.09f, 0.15f, 0.8f));
		Btn->WidgetStyle.NormalPadding = FMargin(0.0f, 10.0f);
		Btn->WidgetStyle.PressedPadding = FMargin(0.0f, 12.0f);

		UHorizontalBox* NavLayout = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), FName(*(Name + "Layout")));
		Btn->AddChild(NavLayout);
		if (UButtonSlot* BS = Cast<UButtonSlot>(NavLayout->Slot))
		{
			BS->SetPadding(FMargin(0.0f));
			BS->SetHorizontalAlignment(HAlign_Fill);
			BS->SetVerticalAlignment(VAlign_Fill);
		}

		// Accent bar (left edge indicator)
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
	};

	CreateNavButton("TabMain", "MAIN", TabMainBtn, TabMainTxt, TabMainAccent);
	SidebarContent->AddChild(TabMainBtn);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(TabMainBtn->Slot))
	{
		VS->SetPadding(FMargin(0.0f, 2.0f));
		VS->SetHorizontalAlignment(HAlign_Fill);
	}
	TabMainBtn->OnClicked.AddDynamic(this, &UUW_PauseMenu::OnTabMainClicked);

	CreateNavButton("TabSpawn", "SPAWN", TabSpawnBtn, TabSpawnTxt, TabSpawnAccent);
	SidebarContent->AddChild(TabSpawnBtn);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(TabSpawnBtn->Slot))
	{
		VS->SetPadding(FMargin(0.0f, 2.0f));
		VS->SetHorizontalAlignment(HAlign_Fill);
	}
	TabSpawnBtn->OnClicked.AddDynamic(this, &UUW_PauseMenu::OnTabSpawnClicked);

	CreateNavButton("TabOptions", "OPTIONS", TabOptionsBtn, TabOptionsTxt, TabOptionsAccent);
	SidebarContent->AddChild(TabOptionsBtn);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(TabOptionsBtn->Slot))
	{
		VS->SetPadding(FMargin(0.0f, 2.0f));
		VS->SetHorizontalAlignment(HAlign_Fill);
	}
	TabOptionsBtn->OnClicked.AddDynamic(this, &UUW_PauseMenu::OnTabOptionsClicked);

	// --- Spacer to push footer down ---
	USpacer* SidebarSpacer = WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass(), TEXT("SidebarSpacer"));
	SidebarContent->AddChild(SidebarSpacer);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(SidebarSpacer->Slot))
	{
		VS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}

	// --- Footer: ESC hint ---
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

	// --- Sidebar right border (separator line) ---
	// We'll use a thin border on the right edge of the sidebar
	// Actually, let's add it as a separate element in the panel layout
	UBorder* SidebarEdge = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("SidebarEdge"));
	SidebarEdge->SetBrushColor(Col_Separator);
	SidebarEdge->SetVisibility(ESlateVisibility::HitTestInvisible);
	PanelLayout->AddChild(SidebarEdge);
	if (UHorizontalBoxSlot* HS = Cast<UHorizontalBoxSlot>(SidebarEdge->Slot))
	{
		HS->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
	}

	// ============================================================
	// RIGHT CONTENT AREA
	// ============================================================
	UBorder* ContentArea = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ContentArea"));
	ContentArea->SetBrushColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.0f)); // transparent
	ContentArea->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	ContentArea->SetPadding(FMargin(0.0f));
	PanelLayout->AddChild(ContentArea);
	if (UHorizontalBoxSlot* HS = Cast<UHorizontalBoxSlot>(ContentArea->Slot))
	{
		HS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}

	// Content area uses a canvas so we can stack tab panels
	UCanvasPanel* ContentCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("ContentCanvas"));
	ContentArea->AddChild(ContentCanvas);
	if (UBorderSlot* BS = Cast<UBorderSlot>(ContentCanvas->Slot))
	{
		BS->SetPadding(FMargin(0.0f));
		BS->SetHorizontalAlignment(HAlign_Fill);
		BS->SetVerticalAlignment(VAlign_Fill);
	}

	// Helper to create a tab content panel that fills the content area
	auto CreateTabPanel = [this, &ContentCanvas](const FString& Name)
	{
		UBorder* Panel2 = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), FName(*Name));
		Panel2->SetBrushColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
		Panel2->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		Panel2->SetPadding(FMargin(0.0f));
		ContentCanvas->AddChild(Panel2);
		if (UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(Panel2->Slot))
		{
			S->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
			S->SetOffsets(FMargin(0.0f));
		}

		// ScrollBox for content that might overflow
		UScrollBox* Scroller = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), FName(*(Name + "Scroll")));
		Panel2->AddChild(Scroller);
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
	};

	// ============================================================
	// MAIN TAB
	// ============================================================
	MainTabContent = CreateTabPanel("MainTab");

	// Page title
	UTextBlock* MainTitle = MakeText("MainTitle", "Flight", 28, Col_TextBright, true);
	MainTitle->SetJustification(ETextJustify::Left);
	MainTabContent->AddChild(MainTitle);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(MainTitle->Slot))
	{
		VS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 4.0f));
		VS->SetHorizontalAlignment(HAlign_Fill);
	}

	UTextBlock* MainDesc = MakeText("MainDesc", "Resume your flight or exit the simulator", 12, Col_TextDim);
	MainDesc->SetJustification(ETextJustify::Left);
	MainTabContent->AddChild(MainDesc);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(MainDesc->Slot))
	{
		VS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 24.0f));
		VS->SetHorizontalAlignment(HAlign_Fill);
	}

	// Resume button
	UButton* ResumeBtn = CreateCardButton("ResumeBtn", "RESUME FLIGHT", 15, Col_Success);
	MainTabContent->AddChild(ResumeBtn);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(ResumeBtn->Slot))
	{
		VS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
		VS->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		VS->SetHorizontalAlignment(HAlign_Fill);
	}
	ResumeBtn->OnClicked.AddDynamic(this, &UUW_PauseMenu::OnResumeClicked);

	// Spacer
	USpacer* MainSpacer = WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass(), TEXT("MainSpacer"));
	MainTabContent->AddChild(MainSpacer);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(MainSpacer->Slot))
	{
		VS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		VS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
	}

	// Quit button
	UButton* QuitBtn = CreateCardButton("QuitBtn", "QUIT TO DESKTOP", 14, Col_Danger);
	MainTabContent->AddChild(QuitBtn);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(QuitBtn->Slot))
	{
		VS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
		VS->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		VS->SetHorizontalAlignment(HAlign_Fill);
	}
	QuitBtn->OnClicked.AddDynamic(this, &UUW_PauseMenu::OnQuitClicked);

	// ============================================================
	// SPAWN TAB
	// ============================================================
	SpawnTabContent = CreateTabPanel("SpawnTab");

	// Page title
	UTextBlock* SpawnTitle = MakeText("SpawnTitle", "Equipment", 28, Col_TextBright, true);
	SpawnTitle->SetJustification(ETextJustify::Left);
	SpawnTabContent->AddChild(SpawnTitle);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(SpawnTitle->Slot))
	{
		VS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 4.0f));
		VS->SetHorizontalAlignment(HAlign_Fill);
	}

	UTextBlock* SpawnDesc = MakeText("SpawnDesc", "Spawn your paramotor and wing to begin flying", 12, Col_TextDim);
	SpawnDesc->SetJustification(ETextJustify::Left);
	SpawnTabContent->AddChild(SpawnDesc);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(SpawnDesc->Slot))
	{
		VS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
		VS->SetHorizontalAlignment(HAlign_Fill);
	}

	// --- Paramotor section ---
	CreateSectionHeader(SpawnTabContent, "PARAMOTOR");

	UButton* SpawnParamBtn = CreateCardButton("SpawnParamBtn", "Spawn Paramotor", 15, Col_TextBright);
	SpawnTabContent->AddChild(SpawnParamBtn);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(SpawnParamBtn->Slot))
	{
		VS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 4.0f));
		VS->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		VS->SetHorizontalAlignment(HAlign_Fill);
	}
	SpawnParamBtn->OnClicked.AddDynamic(this, &UUW_PauseMenu::OnSpawnParamotorClicked);

	UTextBlock* ParamDesc = MakeLeftText("ParamDesc", "Spawns in front of you. Walk to it and press F to equip.", 11, Col_TextDim);
	SpawnTabContent->AddChild(ParamDesc);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(ParamDesc->Slot))
	{
		VS->SetPadding(FMargin(16.0f, 0.0f, 0.0f, 16.0f));
		VS->SetHorizontalAlignment(HAlign_Fill);
	}

	// --- Wing brand section: SIG (collapsible) ---
	bSigWingsExpanded = false;  // Start collapsed

	// SIG brand header button (click to expand/collapse)
	UButton* SigBrandBtn = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("SigBrandBtn"));
	SigBrandBtn->WidgetStyle.Normal.TintColor = FSlateColor(Col_CardBg);
	SigBrandBtn->WidgetStyle.Hovered.TintColor = FSlateColor(Col_BtnHover);
	SigBrandBtn->WidgetStyle.Pressed.TintColor = FSlateColor(Col_BtnPressed);
	SigBrandBtn->WidgetStyle.NormalPadding = FMargin(16.0f, 14.0f);
	SigBrandBtn->WidgetStyle.PressedPadding = FMargin(16.0f, 16.0f);

	// Brand header content: [accent bar | SIG | spacer | expand indicator]
	UHorizontalBox* SigHeaderLayout = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("SigHeaderLayout"));
	SigBrandBtn->AddChild(SigHeaderLayout);
	if (UButtonSlot* BS = Cast<UButtonSlot>(SigHeaderLayout->Slot))
	{
		BS->SetPadding(FMargin(0.0f));
		BS->SetHorizontalAlignment(HAlign_Fill);
		BS->SetVerticalAlignment(VAlign_Fill);
	}

	// Accent bar
	UBorder* SigAccent = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("SigAccentBar"));
	SigAccent->SetBrushColor(Col_Accent);
	SigHeaderLayout->AddChild(SigAccent);
	if (UHorizontalBoxSlot* HS = Cast<UHorizontalBoxSlot>(SigAccent->Slot))
	{
		HS->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		HS->SetPadding(FMargin(0.0f, 0.0f, 12.0f, 0.0f));
	}

	// SIG brand name
	UTextBlock* SigNameTxt = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("SigNameTxt"));
	SigNameTxt->SetText(FText::FromString("SIG"));
	SigNameTxt->SetJustification(ETextJustify::Left);
	SigNameTxt->SetColorAndOpacity(FSlateColor(Col_TextBright));
	FSlateFontInfo SigFont = SigNameTxt->GetFont();
	SigFont.Size = 16;
	SigFont.TypefaceFontName = FName("Bold");
	SigNameTxt->SetFont(SigFont);
	SigHeaderLayout->AddChild(SigNameTxt);
	if (UHorizontalBoxSlot* HS = Cast<UHorizontalBoxSlot>(SigNameTxt->Slot))
	{
		HS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		HS->SetVerticalAlignment(VAlign_Center);
	}

	// Wing count subtitle
	UTextBlock* SigCountTxt = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("SigCountTxt"));
	SigCountTxt->SetText(FText::FromString("6 models"));
	SigCountTxt->SetJustification(ETextJustify::Left);
	SigCountTxt->SetColorAndOpacity(FSlateColor(Col_TextDim));
	FSlateFontInfo SigCountFont = SigCountTxt->GetFont();
	SigCountFont.Size = 11;
	SigCountTxt->SetFont(SigCountFont);
	SigHeaderLayout->AddChild(SigCountTxt);
	if (UHorizontalBoxSlot* HS = Cast<UHorizontalBoxSlot>(SigCountTxt->Slot))
	{
		HS->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		HS->SetPadding(FMargin(12.0f, 0.0f, 0.0f, 0.0f));
		HS->SetVerticalAlignment(VAlign_Center);
	}

	// Expand/collapse indicator (+ / -)
	SigToggleIndicator = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("SigToggleIndicator"));
	SigToggleIndicator->SetText(FText::FromString("+"));
	SigToggleIndicator->SetJustification(ETextJustify::Right);
	SigToggleIndicator->SetColorAndOpacity(FSlateColor(Col_Accent));
	FSlateFontInfo ToggleFont = SigToggleIndicator->GetFont();
	ToggleFont.Size = 18;
	ToggleFont.TypefaceFontName = FName("Bold");
	SigToggleIndicator->SetFont(ToggleFont);
	SigHeaderLayout->AddChild(SigToggleIndicator);
	if (UHorizontalBoxSlot* HS = Cast<UHorizontalBoxSlot>(SigToggleIndicator->Slot))
	{
		HS->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		HS->SetVerticalAlignment(VAlign_Center);
	}

	SpawnTabContent->AddChild(SigBrandBtn);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(SigBrandBtn->Slot))
	{
		VS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
		VS->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		VS->SetHorizontalAlignment(HAlign_Fill);
	}
	SigBrandBtn->OnClicked.AddDynamic(this, &UUW_PauseMenu::OnSigBrandToggleClicked);

	// Container for SIG wing cards (hidden by default)
	SigWingContainer = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("SigWingContainer"));
	SigWingContainer->SetVisibility(ESlateVisibility::Collapsed);
	SpawnTabContent->AddChild(SigWingContainer);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(SigWingContainer->Slot))
	{
		VS->SetPadding(FMargin(16.0f, 0.0f, 0.0f, 8.0f));
		VS->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		VS->SetHorizontalAlignment(HAlign_Fill);
	}

	// Helper to create a wing card: button with name + specs subtext
	auto CreateWingCard = [this, &MakeLeftText](const FString& Name, const FString& WingName,
		const FString& Specs, FLinearColor AccentColor, UButton*& OutBtn)
	{
		UButton* Btn = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(*Name));
		Btn->WidgetStyle.Normal.TintColor = FSlateColor(Col_CardBg);
		Btn->WidgetStyle.Hovered.TintColor = FSlateColor(Col_BtnHover);
		Btn->WidgetStyle.Pressed.TintColor = FSlateColor(Col_BtnPressed);
		Btn->WidgetStyle.NormalPadding = FMargin(16.0f, 14.0f);
		Btn->WidgetStyle.PressedPadding = FMargin(16.0f, 16.0f);

		// Card content: vertical box with name + specs
		UVerticalBox* CardContent = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), FName(*(Name + "Content")));
		Btn->AddChild(CardContent);
		if (UButtonSlot* BS = Cast<UButtonSlot>(CardContent->Slot))
		{
			BS->SetPadding(FMargin(0.0f));
			BS->SetHorizontalAlignment(HAlign_Fill);
			BS->SetVerticalAlignment(VAlign_Fill);
		}

		// Wing name row with accent dot
		UHorizontalBox* NameRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), FName(*(Name + "NameRow")));
		CardContent->AddChild(NameRow);
		if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(NameRow->Slot))
		{
			VS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 4.0f));
			VS->SetHorizontalAlignment(HAlign_Fill);
		}

		// Accent dot
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

		// Specs line
		UTextBlock* SpecsTxt = MakeLeftText(Name + "Specs", Specs, 11, Col_TextDim);
		CardContent->AddChild(SpecsTxt);
		if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(SpecsTxt->Slot))
		{
			VS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 0.0f));
			VS->SetHorizontalAlignment(HAlign_Fill);
		}

		OutBtn = Btn;
	};

	// Sig 16 — super small practice wing (kiting only)
	UButton* Wing16Btn = nullptr;
	CreateWingCard("Wing16Btn", "Sig 16", "16 m\x00B2  |  Max 85 kg  |  KITING PRACTICE ONLY", FLinearColor(1.0f, 0.3f, 0.3f, 1.0f), Wing16Btn);
	SigWingContainer->AddChild(Wing16Btn);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(Wing16Btn->Slot))
	{
		VS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
		VS->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		VS->SetHorizontalAlignment(HAlign_Fill);
	}
	Wing16Btn->OnClicked.AddDynamic(this, &UUW_PauseMenu::OnSpawnWing16Clicked);

	// Sig 20
	UButton* Wing20Btn = nullptr;
	CreateWingCard("Wing20Btn", "Sig 20", "20 m\x00B2  |  Max 110 kg  |  Light pilot", FLinearColor(0.3f, 0.8f, 1.0f, 1.0f), Wing20Btn);
	SigWingContainer->AddChild(Wing20Btn);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(Wing20Btn->Slot))
	{
		VS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
		VS->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		VS->SetHorizontalAlignment(HAlign_Fill);
	}
	Wing20Btn->OnClicked.AddDynamic(this, &UUW_PauseMenu::OnSpawnWing20Clicked);

	// Sig 22
	UButton* Wing22Btn = nullptr;
	CreateWingCard("Wing22Btn", "Sig 22", "22 m\x00B2  |  Max 120 kg  |  Light-medium pilot", FLinearColor(0.5f, 0.85f, 0.7f, 1.0f), Wing22Btn);
	SigWingContainer->AddChild(Wing22Btn);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(Wing22Btn->Slot))
	{
		VS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
		VS->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		VS->SetHorizontalAlignment(HAlign_Fill);
	}
	Wing22Btn->OnClicked.AddDynamic(this, &UUW_PauseMenu::OnSpawnWing22Clicked);

	// Sig 25
	UButton* Wing25Btn = nullptr;
	CreateWingCard("Wing25Btn", "Sig 25", "25 m\x00B2  |  Max 130 kg  |  Standard", FLinearColor(0.3f, 0.9f, 0.5f, 1.0f), Wing25Btn);
	SigWingContainer->AddChild(Wing25Btn);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(Wing25Btn->Slot))
	{
		VS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
		VS->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		VS->SetHorizontalAlignment(HAlign_Fill);
	}
	Wing25Btn->OnClicked.AddDynamic(this, &UUW_PauseMenu::OnSpawnWing25Clicked);

	// Sig 30
	UButton* Wing30Btn = nullptr;
	CreateWingCard("Wing30Btn", "Sig 30", "30 m\x00B2  |  Max 160 kg  |  Heavy / tandem", FLinearColor(1.0f, 0.7f, 0.3f, 1.0f), Wing30Btn);
	SigWingContainer->AddChild(Wing30Btn);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(Wing30Btn->Slot))
	{
		VS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
		VS->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		VS->SetHorizontalAlignment(HAlign_Fill);
	}
	Wing30Btn->OnClicked.AddDynamic(this, &UUW_PauseMenu::OnSpawnWing30Clicked);

	// Sig 35
	UButton* Wing35Btn = nullptr;
	CreateWingCard("Wing35Btn", "Sig 35", "35 m\x00B2  |  Max 180 kg  |  Tandem / very heavy", FLinearColor(0.9f, 0.5f, 0.2f, 1.0f), Wing35Btn);
	SigWingContainer->AddChild(Wing35Btn);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(Wing35Btn->Slot))
	{
		VS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
		VS->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		VS->SetHorizontalAlignment(HAlign_Fill);
	}
	Wing35Btn->OnClicked.AddDynamic(this, &UUW_PauseMenu::OnSpawnWing35Clicked);

	// --- MacPara brand section ---
	CreateSectionHeader(SpawnTabContent, "MACPARA");

	// MacPara Charger 25M — real reflex paramotor wing
	UButton* MacParaCharger25Btn = nullptr;
	CreateWingCard("MacParaCharger25Btn", "Charger 25M", "25.4 m\x00B2  |  Max 137 kg  |  Reflex DGAC  |  46 cells  |  AR 5.14  |  Glide 8.9", FLinearColor(0.15f, 0.45f, 0.85f, 1.0f), MacParaCharger25Btn);
	SpawnTabContent->AddChild(MacParaCharger25Btn);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(MacParaCharger25Btn->Slot))
	{
		VS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
		VS->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		VS->SetHorizontalAlignment(HAlign_Fill);
	}
	MacParaCharger25Btn->OnClicked.AddDynamic(this, &UUW_PauseMenu::OnSpawnMacParaCharger25Clicked);

	// Wing help text
	UTextBlock* WingHelp = MakeLeftText("WingHelp", "Spawns behind you. Connect risers with F, then hold R to inflate.", 11, Col_TextDim);
	SpawnTabContent->AddChild(WingHelp);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(WingHelp->Slot))
	{
		VS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 16.0f));
		VS->SetHorizontalAlignment(HAlign_Fill);
	}

	// --- Cleanup section ---
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

	// ============================================================
	// OPTIONS TAB
	// ============================================================
	OptionsTabContent = CreateTabPanel("OptionsTab");

	// Page title
	UTextBlock* OptTitle = MakeText("OptTitle", "Options", 28, Col_TextBright, true);
	OptTitle->SetJustification(ETextJustify::Left);
	OptionsTabContent->AddChild(OptTitle);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(OptTitle->Slot))
	{
		VS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 4.0f));
		VS->SetHorizontalAlignment(HAlign_Fill);
	}

	UTextBlock* OptDesc = MakeText("OptDesc", "Graphics quality and control bindings", 12, Col_TextDim);
	OptDesc->SetJustification(ETextJustify::Left);
	OptionsTabContent->AddChild(OptDesc);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(OptDesc->Slot))
	{
		VS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
		VS->SetHorizontalAlignment(HAlign_Fill);
	}

	// --- Graphics Quality ---
	CreateSectionHeader(OptionsTabContent, "GRAPHICS QUALITY");

	CurrentQualityLabel = MakeLeftText("CurrentQuality", "Current: HIGH", 12, Col_Accent);
	OptionsTabContent->AddChild(CurrentQualityLabel);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(CurrentQualityLabel->Slot))
	{
		VS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 10.0f));
		VS->SetHorizontalAlignment(HAlign_Fill);
	}

	// Quality preset buttons (4 in a row)
	UHorizontalBox* QualityRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("QualityRow"));
	OptionsTabContent->AddChild(QualityRow);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(QualityRow->Slot))
	{
		VS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 16.0f));
		VS->SetHorizontalAlignment(HAlign_Fill);
	}

	QualityLowBtn = CreatePillButton("QLow", "LOW", 12);
	QualityRow->AddChild(QualityLowBtn);
	if (UHorizontalBoxSlot* HS = Cast<UHorizontalBoxSlot>(QualityLowBtn->Slot))
	{
		HS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		HS->SetPadding(FMargin(0.0f, 0.0f, 4.0f, 0.0f));
	}
	QualityLowBtn->OnClicked.AddDynamic(this, &UUW_PauseMenu::OnQualityLowClicked);

	QualityMedBtn = CreatePillButton("QMed", "MEDIUM", 12);
	QualityRow->AddChild(QualityMedBtn);
	if (UHorizontalBoxSlot* HS = Cast<UHorizontalBoxSlot>(QualityMedBtn->Slot))
	{
		HS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		HS->SetPadding(FMargin(4.0f, 0.0f, 4.0f, 0.0f));
	}
	QualityMedBtn->OnClicked.AddDynamic(this, &UUW_PauseMenu::OnQualityMediumClicked);

	QualityHighBtn = CreatePillButton("QHigh", "HIGH", 12);
	QualityRow->AddChild(QualityHighBtn);
	if (UHorizontalBoxSlot* HS = Cast<UHorizontalBoxSlot>(QualityHighBtn->Slot))
	{
		HS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		HS->SetPadding(FMargin(4.0f, 0.0f, 4.0f, 0.0f));
	}
	QualityHighBtn->OnClicked.AddDynamic(this, &UUW_PauseMenu::OnQualityHighClicked);

	QualityEpicBtn = CreatePillButton("QEpic", "EPIC", 12);
	QualityRow->AddChild(QualityEpicBtn);
	if (UHorizontalBoxSlot* HS = Cast<UHorizontalBoxSlot>(QualityEpicBtn->Slot))
	{
		HS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		HS->SetPadding(FMargin(4.0f, 0.0f, 0.0f, 0.0f));
	}
	QualityEpicBtn->OnClicked.AddDynamic(this, &UUW_PauseMenu::OnQualityEpicClicked);

	// --- Resolution Scale ---
	CreateSectionHeader(OptionsTabContent, "RESOLUTION SCALE");

	UHorizontalBox* ResRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("ResRow"));
	OptionsTabContent->AddChild(ResRow);
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
	OptionsTabContent->AddChild(ResScaleSlider);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(ResScaleSlider->Slot))
	{
		VS->SetPadding(FMargin(0.0f, 8.0f, 0.0f, 16.0f));
		VS->SetHorizontalAlignment(HAlign_Fill);
	}
	ResScaleSlider->OnValueChanged.AddDynamic(this, &UUW_PauseMenu::OnResolutionScaleChanged);

	// --- Display Toggles ---
	CreateSectionHeader(OptionsTabContent, "DISPLAY");

	UHorizontalBox* ToggleRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("ToggleRow"));
	OptionsTabContent->AddChild(ToggleRow);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(ToggleRow->Slot))
	{
		VS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 16.0f));
		VS->SetHorizontalAlignment(HAlign_Fill);
	}

	// VSync
	UVerticalBox* VSyncBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("VSyncBox"));
	ToggleRow->AddChild(VSyncBox);
	if (UHorizontalBoxSlot* HS = Cast<UHorizontalBoxSlot>(VSyncBox->Slot))
	{
		HS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		HS->SetPadding(FMargin(0.0f, 0.0f, 16.0f, 0.0f));
	}

	UTextBlock* VSyncLabel = MakeLeftText("VSyncLabel", "V-Sync", 13, Col_TextBright);
	VSyncBox->AddChild(VSyncLabel);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(VSyncLabel->Slot))
	{
		VS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 6.0f));
		VS->SetHorizontalAlignment(HAlign_Fill);
	}

	VSyncCheckBox = WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass(), TEXT("VSyncCheck"));
	VSyncCheckBox->SetCheckedState(ECheckBoxState::Checked);
	{
		FCheckBoxStyle Style = VSyncCheckBox->GetWidgetStyle();
		Style.CheckedImage.TintColor = FSlateColor(Col_Accent);
		Style.UncheckedImage.TintColor = FSlateColor(FLinearColor(0.15f, 0.18f, 0.25f, 1.0f));
		VSyncCheckBox->SetWidgetStyle(Style);
	}
	VSyncBox->AddChild(VSyncCheckBox);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(VSyncCheckBox->Slot))
	{
		VS->SetHorizontalAlignment(HAlign_Left);
	}
	VSyncCheckBox->OnCheckStateChanged.AddDynamic(this, &UUW_PauseMenu::OnVSyncToggled);

	// Shadows
	UVerticalBox* ShadowsBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ShadowsBox"));
	ToggleRow->AddChild(ShadowsBox);
	if (UHorizontalBoxSlot* HS = Cast<UHorizontalBoxSlot>(ShadowsBox->Slot))
	{
		HS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		HS->SetPadding(FMargin(16.0f, 0.0f, 0.0f, 0.0f));
	}

	UTextBlock* ShadowsLabel = MakeLeftText("ShadowsLabel", "Shadows", 13, Col_TextBright);
	ShadowsBox->AddChild(ShadowsLabel);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(ShadowsLabel->Slot))
	{
		VS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 6.0f));
		VS->SetHorizontalAlignment(HAlign_Fill);
	}

	ShadowsCheckBox = WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass(), TEXT("ShadowsCheck"));
	ShadowsCheckBox->SetCheckedState(ECheckBoxState::Checked);
	{
		FCheckBoxStyle Style = ShadowsCheckBox->GetWidgetStyle();
		Style.CheckedImage.TintColor = FSlateColor(Col_Accent);
		Style.UncheckedImage.TintColor = FSlateColor(FLinearColor(0.15f, 0.18f, 0.25f, 1.0f));
		ShadowsCheckBox->SetWidgetStyle(Style);
	}
	ShadowsBox->AddChild(ShadowsCheckBox);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(ShadowsCheckBox->Slot))
	{
		VS->SetHorizontalAlignment(HAlign_Left);
	}
	ShadowsCheckBox->OnCheckStateChanged.AddDynamic(this, &UUW_PauseMenu::OnShadowsToggled);

	// --- Key Bindings ---
	CreateSectionHeader(OptionsTabContent, "KEY BINDINGS");

	AddKeybindRow(OptionsTabContent, "Move Forward / Back", "W / S");
	AddKeybindRow(OptionsTabContent, "Move Left / Right", "A / D");
	AddKeybindRow(OptionsTabContent, "Turn / Look", "Mouse");
	AddKeybindRow(OptionsTabContent, "Run / Jog", "Shift");
	AddKeybindRow(OptionsTabContent, "Interact / Equip", "F");
	AddKeybindRow(OptionsTabContent, "Start / Stop Engine", "P");
	AddKeybindRow(OptionsTabContent, "Throttle Up / Down", "Up / Down");
	AddKeybindRow(OptionsTabContent, "Lift A-Lines", "R");
	AddKeybindRow(OptionsTabContent, "Left Brake", "Q");
	AddKeybindRow(OptionsTabContent, "Right Brake", "E");
	AddKeybindRow(OptionsTabContent, "Trimmer Fast / Slow", "= / -");
	AddKeybindRow(OptionsTabContent, "Pause", "Esc");

	// ============================================================
	// INITIALIZE FROM CURRENT SETTINGS
	// ============================================================
	UGameUserSettings* GUS = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	if (GUS)
	{
		int32 OverallQ = GUS->GetOverallScalabilityLevel();
		FString QNames[] = {"LOW", "MEDIUM", "HIGH", "EPIC", "CINEMATIC"};
		FString QName = (OverallQ >= 0 && OverallQ <= 4) ? QNames[OverallQ] : "CUSTOM";
		if (CurrentQualityLabel) CurrentQualityLabel->SetText(FText::FromString("Current: " + QName));

		switch (OverallQ)
		{
		case 0: HighlightQualityButton(QualityLowBtn); break;
		case 1: HighlightQualityButton(QualityMedBtn); break;
		case 2: HighlightQualityButton(QualityHighBtn); break;
		case 3: HighlightQualityButton(QualityEpicBtn); break;
		default: HighlightQualityButton(QualityHighBtn); break;
		}

		if (VSyncCheckBox)
		{
			VSyncCheckBox->SetCheckedState(GUS->IsVSyncEnabled() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked);
		}

		if (ShadowsCheckBox)
		{
			int32 ShadowQ = GUS->GetShadowQuality();
			ShadowsCheckBox->SetCheckedState(ShadowQ > 0 ? ECheckBoxState::Checked : ECheckBoxState::Unchecked);
		}

		float ResPct = GUS->GetResolutionScaleNormalized();
		if (ResolutionScaleLabel)
		{
			ResolutionScaleLabel->SetText(FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(ResPct * 100.0f))));
		}
	}

	// Show main tab by default
	ShowMainTab();
}

// ============================================================
// TAB SWITCHING
// ============================================================

void UUW_PauseMenu::SetTabActive(UButton* ActiveBtn, UButton* InactiveBtn1, UButton* InactiveBtn2)
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
	{
		SetActive(T.Txt, T.Accent, T.Btn == ActiveBtn);
	}
}

void UUW_PauseMenu::HighlightQualityButton(UButton* ActiveBtn)
{
	auto SetBtn = [](UButton* Btn, FLinearColor Tint, FLinearColor TextColor)
	{
		if (!Btn) return;
		Btn->WidgetStyle.Normal.TintColor = FSlateColor(Tint);
		UTextBlock* Txt = Cast<UTextBlock>(Btn->GetChildAt(0));
		if (Txt) Txt->SetColorAndOpacity(FSlateColor(TextColor));
	};

	// Reset all to inactive
	SetBtn(QualityLowBtn, Col_BtnNormal, Col_TextDim);
	SetBtn(QualityMedBtn, Col_BtnNormal, Col_TextDim);
	SetBtn(QualityHighBtn, Col_BtnNormal, Col_TextDim);
	SetBtn(QualityEpicBtn, Col_BtnNormal, Col_TextDim);

	// Highlight active
	if (ActiveBtn)
	{
		SetBtn(ActiveBtn, FLinearColor(0.08f, 0.15f, 0.28f, 0.95f), Col_Accent);
	}
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
		// The parent chain is: VBox -> ScrollBox -> Border(tab panel) -> Canvas
		// Set the tab panel border visibility to match
		if (UPanelWidget* P = T.Content->GetParent())
		{
			if (UPanelWidget* PP = P->GetParent())
			{
				PP->SetVisibility(bActive ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
			}
		}
	}
}

void UUW_PauseMenu::ShowMainTab()    { ShowTab(MainTabContent);    SetTabActive(TabMainBtn,    TabSpawnBtn, TabOptionsBtn); }
void UUW_PauseMenu::ShowSpawnTab()   { ShowTab(SpawnTabContent);   SetTabActive(TabSpawnBtn,   TabMainBtn,  TabOptionsBtn); }
void UUW_PauseMenu::ShowOptionsTab() { ShowTab(OptionsTabContent); SetTabActive(TabOptionsBtn, TabMainBtn,  TabSpawnBtn); }

void UUW_PauseMenu::OnTabMainClicked() { ShowMainTab(); }
void UUW_PauseMenu::OnTabSpawnClicked() { ShowSpawnTab(); }
void UUW_PauseMenu::OnTabOptionsClicked() { ShowOptionsTab(); }

// ============================================================
// GRAPHICS HANDLERS
// ============================================================

void UUW_PauseMenu::OnQualityLowClicked()
{
	UGameUserSettings* GUS = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	if (GUS)
	{
		GUS->SetOverallScalabilityLevel(0);
		GUS->ApplySettings(true);
		HighlightQualityButton(QualityLowBtn);
		if (CurrentQualityLabel) CurrentQualityLabel->SetText(FText::FromString("Current: LOW"));
	}
}

void UUW_PauseMenu::OnQualityMediumClicked()
{
	UGameUserSettings* GUS = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	if (GUS)
	{
		GUS->SetOverallScalabilityLevel(1);
		GUS->ApplySettings(true);
		HighlightQualityButton(QualityMedBtn);
		if (CurrentQualityLabel) CurrentQualityLabel->SetText(FText::FromString("Current: MEDIUM"));
	}
}

void UUW_PauseMenu::OnQualityHighClicked()
{
	UGameUserSettings* GUS = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	if (GUS)
	{
		GUS->SetOverallScalabilityLevel(2);
		GUS->ApplySettings(true);
		HighlightQualityButton(QualityHighBtn);
		if (CurrentQualityLabel) CurrentQualityLabel->SetText(FText::FromString("Current: HIGH"));
	}
}

void UUW_PauseMenu::OnQualityEpicClicked()
{
	UGameUserSettings* GUS = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	if (GUS)
	{
		GUS->SetOverallScalabilityLevel(3);
		GUS->ApplySettings(true);
		HighlightQualityButton(QualityEpicBtn);
		if (CurrentQualityLabel) CurrentQualityLabel->SetText(FText::FromString("Current: EPIC"));
	}
}

void UUW_PauseMenu::OnVSyncToggled(bool bChecked)
{
	UGameUserSettings* GUS = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	if (GUS)
	{
		GUS->SetVSyncEnabled(bChecked);
		GUS->ApplySettings(true);
	}
}

void UUW_PauseMenu::OnShadowsToggled(bool bChecked)
{
	UGameUserSettings* GUS = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	if (GUS)
	{
		GUS->SetShadowQuality(bChecked ? 2 : 0);
		GUS->ApplySettings(true);
	}
}

void UUW_PauseMenu::OnResolutionScaleChanged(float Value)
{
	UGameUserSettings* GUS = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	if (GUS)
	{
		GUS->SetResolutionScaleNormalized(Value);
		GUS->ApplySettings(true);
	}
	if (ResolutionScaleLabel)
	{
		ResolutionScaleLabel->SetText(FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Value * 100.0f))));
	}
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
	AParamotorPlayerController* PC = Cast<AParamotorPlayerController>(GetOwningPlayer());
	if (PC)
	{
		PC->TogglePauseMenu();
	}
}

void UUW_PauseMenu::OnQuitClicked()
{
	UKismetSystemLibrary::QuitGame(GetWorld(), nullptr, EQuitPreference::Quit, false);
}

void UUW_PauseMenu::OnSpawnParamotorClicked()
{
	AParamotorPlayerController* PC = Cast<AParamotorPlayerController>(GetOwningPlayer());
	if (PC)
	{
		PC->SpawnParamotor();
	}
}

void UUW_PauseMenu::OnSpawnWing16Clicked()
{
	AParamotorPlayerController* PC = Cast<AParamotorPlayerController>(GetOwningPlayer());
	if (PC) PC->SpawnWingByModel(0);
}

void UUW_PauseMenu::OnSpawnWing20Clicked()
{
	AParamotorPlayerController* PC = Cast<AParamotorPlayerController>(GetOwningPlayer());
	if (PC) PC->SpawnWingByModel(1);
}

void UUW_PauseMenu::OnSpawnWing22Clicked()
{
	AParamotorPlayerController* PC = Cast<AParamotorPlayerController>(GetOwningPlayer());
	if (PC) PC->SpawnWingByModel(2);
}

void UUW_PauseMenu::OnSpawnWing25Clicked()
{
	AParamotorPlayerController* PC = Cast<AParamotorPlayerController>(GetOwningPlayer());
	if (PC) PC->SpawnWingByModel(3);
}

void UUW_PauseMenu::OnSpawnWing30Clicked()
{
	AParamotorPlayerController* PC = Cast<AParamotorPlayerController>(GetOwningPlayer());
	if (PC) PC->SpawnWingByModel(4);
}

void UUW_PauseMenu::OnSpawnWing35Clicked()
{
	AParamotorPlayerController* PC = Cast<AParamotorPlayerController>(GetOwningPlayer());
	if (PC) PC->SpawnWingByModel(5);
}

void UUW_PauseMenu::OnSpawnMacParaCharger25Clicked()
{
	AParamotorPlayerController* PC = Cast<AParamotorPlayerController>(GetOwningPlayer());
	if (PC) PC->SpawnWingByModel(6);
}

void UUW_PauseMenu::OnSigBrandToggleClicked()
{
	bSigWingsExpanded = !bSigWingsExpanded;
	if (SigWingContainer)
	{
		SigWingContainer->SetVisibility(bSigWingsExpanded ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (SigToggleIndicator)
	{
		SigToggleIndicator->SetText(FText::FromString(bSigWingsExpanded ? "-" : "+"));
	}
}

void UUW_PauseMenu::OnClearSpawnedClicked()
{
	AParamotorPlayerController* PC = Cast<AParamotorPlayerController>(GetOwningPlayer());
	if (PC)
	{
		PC->ClearSpawnedActors();
	}
}
