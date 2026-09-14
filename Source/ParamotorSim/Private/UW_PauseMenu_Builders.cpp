// Copyright Epic Games, Inc. All Rights Reserved.

#include "UW_PauseMenu.h"
#include "PauseMenuPalette.h"
#include "ParamotorGameInstance.h"
#include "Kismet/GameplayStatics.h"
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
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "HAL/IConsoleManager.h"

// ==== MENU CONSTRUCTION ====

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

UButton* UUW_PauseMenu::CreateTabButton(const FString& Name, const FString& Label, UImage*& OutUnderline)
{
	UButton* Btn = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(*Name));
	ApplyButtonColors(Btn, FLinearColor(0.0f, 0.0f, 0.0f, 0.0f), FLinearColor(0.10f, 0.14f, 0.22f, 0.7f),
		FLinearColor(0.14f, 0.19f, 0.29f, 0.9f), FMargin(20.0f, 4.0f), FMargin(20.0f, 6.0f));

	UVerticalBox* Stack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), FName(*(Name + "Stack")));
	Btn->AddChild(Stack);
	if (UButtonSlot* BS = Cast<UButtonSlot>(Stack->Slot))
	{
		BS->SetPadding(FMargin(0.0f));
		BS->SetHorizontalAlignment(HAlign_Fill);
		BS->SetVerticalAlignment(VAlign_Fill);
	}

	UTextBlock* Txt = MakeText(Name + "Txt", Label, 14, Col_TextDim, false);
	Stack->AddChild(Txt);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(Txt->Slot))
	{
		VS->SetHorizontalAlignment(HAlign_Center);
		VS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 6.0f));
	}

	USizeBox* LineBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), FName(*(Name + "LineBox")));
	LineBox->SetHeightOverride(2.0f);
	Stack->AddChild(LineBox);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(LineBox->Slot))
	{
		VS->SetHorizontalAlignment(HAlign_Fill);
		VS->SetPadding(FMargin(16.0f, 0.0f));
	}

	UImage* Underline = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), FName(*(Name + "Underline")));
	Underline->SetColorAndOpacity(Col_Accent);
	Underline->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	LineBox->AddChild(Underline);
	LineBox->SetVisibility(ESlateVisibility::Collapsed);

	OutUnderline = Underline;
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

void UUW_PauseMenu::BuildRootLayout()
{
	RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
	WidgetTree->RootWidget = RootCanvas;

	// Full-screen scrim — light enough that the world stays readable behind it
	UBorder* Dim = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Dim"));
	Dim->SetBrushColor(Col_BgDim);
	Dim->SetPadding(FMargin(0.0f));
	RootCanvas->AddChild(Dim);
	if (UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(Dim->Slot))
	{
		S->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
		S->SetOffsets(FMargin(0.0f));
	}

	// 1px edge ring around the panel (outer border = edge color)
	const float PanelW = 960.0f;
	const float PanelH = 680.0f;

	// Soft drop shadow — a slightly larger dark border behind the panel
	UBorder* Shadow = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("PanelShadow"));
	Shadow->SetBrushColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.4f));
	Shadow->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	Shadow->SetPadding(FMargin(0.0f));
	RootCanvas->AddChild(Shadow);
	if (UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(Shadow->Slot))
	{
		S->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
		S->SetPosition(FVector2D(-PanelW / 2.0f - 10.0f, -PanelH / 2.0f - 2.0f));
		S->SetSize(FVector2D(PanelW + 28.0f, PanelH + 28.0f));
	}

	UBorder* Edge = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("PanelEdge"));
	Edge->SetBrushColor(Col_PanelEdge);
	Edge->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	Edge->SetPadding(FMargin(1.0f));
	RootCanvas->AddChild(Edge);
	if (UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(Edge->Slot))
	{
		S->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
		S->SetPosition(FVector2D(-PanelW / 2.0f - 1.0f, -PanelH / 2.0f - 1.0f));
		S->SetSize(FVector2D(PanelW + 2.0f, PanelH + 2.0f));
	}

	// Panel body: [accent strip] [header+tabs] [content] [footer]
	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Panel"));
	Panel->SetBrushColor(Col_Panel);
	Panel->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	Panel->SetPadding(FMargin(0.0f));
	Edge->AddChild(Panel);
	if (UBorderSlot* BS = Cast<UBorderSlot>(Panel->Slot))
	{
		BS->SetPadding(FMargin(0.0f));
		BS->SetHorizontalAlignment(HAlign_Fill);
		BS->SetVerticalAlignment(VAlign_Fill);
	}

	UVerticalBox* Stack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("PanelStack"));
	Panel->AddChild(Stack);
	if (UBorderSlot* BS = Cast<UBorderSlot>(Stack->Slot))
	{
		BS->SetPadding(FMargin(0.0f));
		BS->SetHorizontalAlignment(HAlign_Fill);
		BS->SetVerticalAlignment(VAlign_Fill);
	}

	// Accent strip across the top
	UImage* Strip = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("AccentStrip"));
	Strip->SetColorAndOpacity(Col_Accent);
	Strip->SetDesiredSizeOverride(FVector2D(1.0f, 3.0f));
	Stack->AddChild(Strip);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(Strip->Slot))
	{
		VS->SetHorizontalAlignment(HAlign_Fill);
	}

	// Header band (title + tab row) — built by BuildHeader
	UBorder* Header = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("HeaderBand"));
	Header->SetBrushColor(Col_Header);
	Header->SetPadding(FMargin(0.0f));
	Stack->AddChild(Header);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(Header->Slot))
	{
		VS->SetHorizontalAlignment(HAlign_Fill);
	}
	HeaderBandBorder = Header;

	// Content area — tab panels swap inside here
	UBorder* ContentArea = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ContentArea"));
	ContentArea->SetBrushColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
	ContentArea->SetPadding(FMargin(0.0f));
	Stack->AddChild(ContentArea);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(ContentArea->Slot))
	{
		VS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		VS->SetHorizontalAlignment(HAlign_Fill);
	}

	ContentCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("ContentCanvas"));
	ContentArea->AddChild(ContentCanvas);
	if (UBorderSlot* BS = Cast<UBorderSlot>(ContentCanvas->Slot))
	{
		BS->SetPadding(FMargin(0.0f));
		BS->SetHorizontalAlignment(HAlign_Fill);
		BS->SetVerticalAlignment(VAlign_Fill);
	}

	// Footer band — built by BuildFooter
	UBorder* Footer = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("FooterBand"));
	Footer->SetBrushColor(Col_Footer);
	Footer->SetPadding(FMargin(0.0f));
	Stack->AddChild(Footer);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(Footer->Slot))
	{
		VS->SetHorizontalAlignment(HAlign_Fill);
	}
	FooterBandBorder = Footer;
}

void UUW_PauseMenu::BuildHeader()
{
	if (!HeaderBandBorder) return;

	UVerticalBox* HeaderStack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("HeaderStack"));
	HeaderBandBorder->AddChild(HeaderStack);
	if (UBorderSlot* BS = Cast<UBorderSlot>(HeaderStack->Slot))
	{
		BS->SetPadding(FMargin(0.0f));
		BS->SetHorizontalAlignment(HAlign_Fill);
		BS->SetVerticalAlignment(VAlign_Fill);
	}

	UTextBlock* Kicker = MakeText("Kicker", "PARAMOTOR SIM", 11, Col_Accent, true);
	Kicker->SetJustification(ETextJustify::Center);
	FSlateFontInfo KFont = Kicker->GetFont();
	KFont.LetterSpacing = 600;
	Kicker->SetFont(KFont);
	HeaderStack->AddChild(Kicker);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(Kicker->Slot))
	{
		VS->SetHorizontalAlignment(HAlign_Center);
		VS->SetPadding(FMargin(0.0f, 18.0f, 0.0f, 0.0f));
	}

	UTextBlock* Title = MakeText("Title", "PAUSED", 30, Col_TextBright, true);
	Title->SetJustification(ETextJustify::Center);
	HeaderStack->AddChild(Title);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(Title->Slot))
	{
		VS->SetHorizontalAlignment(HAlign_Center);
		VS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 12.0f));
	}

	// Tab row — centered pills
	UHorizontalBox* Tabs = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("TabRow"));
	HeaderStack->AddChild(Tabs);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(Tabs->Slot))
	{
		VS->SetHorizontalAlignment(HAlign_Center);
		VS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 0.0f));
	}
	TabButtonRow = Tabs;

	TabMainBtn = CreateTabButton("TabMain", "FLIGHT", TabMainUnderline);
	TabMainTxt = Cast<UTextBlock>(Cast<UVerticalBox>(TabMainBtn->GetChildAt(0))->GetChildAt(0));
	TabMainBtn->OnClicked.AddDynamic(this, &UUW_PauseMenu::OnTabMainClicked);
	Tabs->AddChild(TabMainBtn);

	TabSpawnBtn = CreateTabButton("TabSpawn", "EQUIPMENT", TabSpawnUnderline);
	TabSpawnTxt = Cast<UTextBlock>(Cast<UVerticalBox>(TabSpawnBtn->GetChildAt(0))->GetChildAt(0));
	TabSpawnBtn->OnClicked.AddDynamic(this, &UUW_PauseMenu::OnTabSpawnClicked);
	Tabs->AddChild(TabSpawnBtn);

	TabOptionsBtn = CreateTabButton("TabOptions", "OPTIONS", TabOptionsUnderline);
	TabOptionsTxt = Cast<UTextBlock>(Cast<UVerticalBox>(TabOptionsBtn->GetChildAt(0))->GetChildAt(0));
	TabOptionsBtn->OnClicked.AddDynamic(this, &UUW_PauseMenu::OnTabOptionsClicked);
	Tabs->AddChild(TabOptionsBtn);
}

void UUW_PauseMenu::BuildFooter()
{
	if (!FooterBandBorder) return;

	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("FooterRow"));
	FooterBandBorder->AddChild(Row);
	if (UBorderSlot* BS = Cast<UBorderSlot>(Row->Slot))
	{
		BS->SetPadding(FMargin(24.0f, 12.0f));
		BS->SetHorizontalAlignment(HAlign_Fill);
		BS->SetVerticalAlignment(VAlign_Center);
	}

	// Keycap-style ESC hint
	UBorder* KeyCap = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("EscCap"));
	KeyCap->SetBrushColor(Col_BtnNormal);
	KeyCap->SetPadding(FMargin(8.0f, 3.0f));
	Row->AddChild(KeyCap);
	if (UHorizontalBoxSlot* HS = Cast<UHorizontalBoxSlot>(KeyCap->Slot))
	{
		HS->SetVerticalAlignment(VAlign_Center);
	}
	UTextBlock* KeyTxt = MakeText("EscKey", "ESC", 10, Col_TextBright, true);
	KeyCap->AddChild(KeyTxt);

	UTextBlock* Hint = MakeText("EscHint", "  to resume", 11, Col_TextDim);
	Row->AddChild(Hint);
	if (UHorizontalBoxSlot* HS = Cast<UHorizontalBoxSlot>(Hint->Slot))
	{
		HS->SetVerticalAlignment(VAlign_Center);
		HS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}

	// Ghost buttons, right-aligned: main menu then quit
	auto GhostButton = [this, Row](const TCHAR* BtnName, const TCHAR* Label, FLinearColor TextCol,
		FLinearColor HoverBg, FLinearColor PressBg) -> UButton*
	{
		UButton* Btn = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(BtnName));
		ApplyButtonColors(Btn, FLinearColor(0.0f, 0.0f, 0.0f, 0.0f), HoverBg, PressBg,
			FMargin(14.0f, 6.0f), FMargin(14.0f, 8.0f));
		UTextBlock* Txt = MakeText(FString(BtnName) + TEXT("Txt"), Label, 11, TextCol, false);
		Btn->AddChild(Txt);
		Row->AddChild(Btn);
		if (UHorizontalBoxSlot* HS = Cast<UHorizontalBoxSlot>(Btn->Slot))
		{
			HS->SetVerticalAlignment(VAlign_Center);
			HS->SetPadding(FMargin(4.0f, 0.0f));
		}
		return Btn;
	};

	UButton* MenuBtn = GhostButton(TEXT("MainMenuBtn"), TEXT("RETURN TO MAIN MENU"), Col_TextLabel,
		FLinearColor(0.12f, 0.16f, 0.24f, 0.8f), FLinearColor(0.16f, 0.22f, 0.32f, 1.0f));
	MenuBtn->OnClicked.AddDynamic(this, &UUW_PauseMenu::OnMainMenuClicked);

	UButton* QuitBtn = GhostButton(TEXT("QuitBtn"), TEXT("QUIT TO DESKTOP"), Col_Danger,
		FLinearColor(0.22f, 0.10f, 0.10f, 0.8f), FLinearColor(0.30f, 0.12f, 0.12f, 1.0f));
	QuitBtn->OnClicked.AddDynamic(this, &UUW_PauseMenu::OnQuitClicked);
}

void UUW_PauseMenu::BuildMainTab()
{
	MainTabContent = CreateTabPanel("MainTab");

	// Session status card — real data from the game instance
	UBorder* Card = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("SessionCard"));
	Card->SetBrushColor(Col_CardBg);
	Card->SetPadding(FMargin(24.0f, 18.0f));
	MainTabContent->AddChild(Card);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(Card->Slot))
	{
		VS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 20.0f));
		VS->SetHorizontalAlignment(HAlign_Fill);
	}

	UVerticalBox* CardContent = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("SessionCardVBox"));
	Card->AddChild(CardContent);
	if (UBorderSlot* BS = Cast<UBorderSlot>(CardContent->Slot))
	{
		BS->SetHorizontalAlignment(HAlign_Fill);
		BS->SetVerticalAlignment(VAlign_Fill);
	}

	UTextBlock* CardTitle = MakeLeftText("SessionTitle", "CURRENT SESSION", 11, Col_Accent);
	FSlateFontInfo CTFont = CardTitle->GetFont();
	CTFont.LetterSpacing = 400;
	CardTitle->SetFont(CTFont);
	CardContent->AddChild(CardTitle);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(CardTitle->Slot))
	{
		VS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 10.0f));
	}

	// Live site + weather line
	FString SiteLine = TEXT("Westerly State Airport (KWST)");
	FString WeatherLine;
	if (UParamotorGameInstance* GI = Cast<UParamotorGameInstance>(UGameplayStatics::GetGameInstance(this)))
	{
		if (const FLaunchSite* Site = GI->GetActiveSite())
		{
			SiteLine = Site->Name;
			if (!Site->ICAO.IsEmpty())
			{
				SiteLine += FString::Printf(TEXT(" (%s)"), *Site->ICAO);
			}
		}
		WeatherLine = FString::Printf(TEXT("Wind %.0f mph from %.0f°   |   %.0f°F   |   %s"),
			GI->WindSpeed * 2.237f, GI->WindDirection, GI->Temperature * 9.0f / 5.0f + 32.0f,
			GI->bLiveWeather ? TEXT("live METAR") : TEXT("manual weather"));
	}
	UTextBlock* SiteTxt = MakeLeftText("SessionSite", SiteLine, 16, Col_TextBright);
	CardContent->AddChild(SiteTxt);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(SiteTxt->Slot))
	{
		VS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 4.0f));
	}
	UTextBlock* WxTxt = MakeLeftText("SessionWeather", WeatherLine, 12, Col_TextDim);
	CardContent->AddChild(WxTxt);

	// Primary CTA — full-width accent button
	UButton* ResumeBtn = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("ResumeBtn"));
	ResumeButton = ResumeBtn;
	ApplyButtonColors(ResumeBtn, Col_Accent, FLinearColor(0.25f, 0.85f, 1.0f, 1.0f),
		FLinearColor(0.05f, 0.55f, 0.8f, 1.0f), FMargin(16.0f, 14.0f), FMargin(16.0f, 16.0f));
	UTextBlock* ResumeTxt = MakeText("ResumeTxt", "RESUME FLIGHT", 16, Col_AccentText, true);
	ResumeBtn->AddChild(ResumeTxt);
	if (UButtonSlot* BS = Cast<UButtonSlot>(ResumeTxt->Slot))
	{
		BS->SetHorizontalAlignment(HAlign_Center);
		BS->SetVerticalAlignment(VAlign_Center);
	}
	MainTabContent->AddChild(ResumeBtn);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(ResumeBtn->Slot))
	{
		VS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 10.0f));
		VS->SetHorizontalAlignment(HAlign_Fill);
	}
	ResumeBtn->OnClicked.AddDynamic(this, &UUW_PauseMenu::OnResumeClicked);

	UTextBlock* Hint = MakeLeftText("MainHint",
		"Spawn equipment from the EQUIPMENT tab, then walk up and press F to use it.",
		12, Col_TextDim);
	Hint->SetAutoWrapText(true);
	MainTabContent->AddChild(Hint);

	// Quick-keys strip — the five controls people actually need mid-flight
	USpacer* Spacer = WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass(), TEXT("MainSpacer"));
	MainTabContent->AddChild(Spacer);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(Spacer->Slot))
	{
		VS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		VS->SetPadding(FMargin(0.0f, 16.0f));
	}

	UHorizontalBox* KeysRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("QuickKeys"));
	MainTabContent->AddChild(KeysRow);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(KeysRow->Slot))
	{
		VS->SetHorizontalAlignment(HAlign_Center);
	}

	auto KeyChip = [this, KeysRow](const TCHAR* Name, const TCHAR* Key, const TCHAR* Action)
	{
		UHorizontalBox* Chip = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), FName(Name));
		KeysRow->AddChild(Chip);
		if (UHorizontalBoxSlot* HS = Cast<UHorizontalBoxSlot>(Chip->Slot))
		{
			HS->SetPadding(FMargin(0.0f, 0.0f, 20.0f, 0.0f));
			HS->SetVerticalAlignment(VAlign_Center);
		}

		UBorder* Cap = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), FName(*(FString(Name) + TEXT("Cap"))));
		Cap->SetBrushColor(Col_BtnNormal);
		Cap->SetPadding(FMargin(7.0f, 3.0f));
		Chip->AddChild(Cap);
		if (UHorizontalBoxSlot* HS = Cast<UHorizontalBoxSlot>(Cap->Slot))
		{
			HS->SetVerticalAlignment(VAlign_Center);
		}
		UTextBlock* KeyTxt = MakeText(FString(Name) + TEXT("Key"), Key, 10, Col_TextBright, true);
		Cap->AddChild(KeyTxt);

		UTextBlock* ActTxt = MakeLeftText(FString(Name) + TEXT("Act"), FString(TEXT("  ")) + Action, 11, Col_TextDim);
		Chip->AddChild(ActTxt);
		if (UHorizontalBoxSlot* HS = Cast<UHorizontalBoxSlot>(ActTxt->Slot))
		{
			HS->SetVerticalAlignment(VAlign_Center);
		}
	};

	KeyChip(TEXT("KInteract"), TEXT("F"), TEXT("Interact"));
	KeyChip(TEXT("KEngine"), TEXT("P"), TEXT("Engine"));
	KeyChip(TEXT("KInflate"), TEXT("R"), TEXT("Inflate"));
	KeyChip(TEXT("KBrakes"), TEXT("Q/E"), TEXT("Brakes"));
	KeyChip(TEXT("KThrottle"), TEXT("↑/↓"), TEXT("Throttle"));
}

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

	UButton* SpawnCodexBtn = CreateCardButton("SpawnCodexParamBtn", "Spawn Paramotor", 15, Col_TextBright);
	SpawnTabContent->AddChild(SpawnCodexBtn);
	if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(SpawnCodexBtn->Slot))
	{
		VS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 4.0f));
		VS->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		VS->SetHorizontalAlignment(HAlign_Fill);
	}
	SpawnCodexBtn->OnClicked.AddDynamic(this, &UUW_PauseMenu::OnSpawnParamotorClicked);

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
	CreateSectionHeader(SpawnTabContent, "CLEANUP");

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
	ResScaleSlider->SetMaxValue(1.5f);
	// The game drives render scale via the r.ScreenPercentage cvar (125% at
	// Init), so read the actual rendered value rather than GameUserSettings'
	// untouched normalized default.
	float CurrentScale = 1.0f;
	if (IConsoleVariable* CV = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage")))
	{
		CurrentScale = FMath::Clamp(CV->GetFloat() / 100.0f, 0.5f, 1.5f);
	}
	ResScaleSlider->SetValue(CurrentScale);
	ResolutionScaleLabel->SetText(FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(CurrentScale * 100.0f))));
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