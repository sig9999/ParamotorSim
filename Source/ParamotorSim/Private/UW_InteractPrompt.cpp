// Copyright Epic Games, Inc. All Rights Reserved.

#include "UW_InteractPrompt.h"
#include "UIStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Components/Border.h"
#include "Components/BorderSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"

void UUW_InteractPrompt::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
	WidgetTree->RootWidget = Root;

	// Translucent chip, centered — [ F ]  EQUIP PARAMOTOR
	UBorder* Bg = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Bg"));
	Bg->SetBrushColor(UIStyle::PanelDeep);
	Bg->SetPadding(FMargin(14.0f, 8.0f, 16.0f, 8.0f));
	Root->AddChild(Bg);
	if (UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(Bg->Slot))
	{
		S->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
		S->SetPosition(FVector2D(-110.0f, -17.0f));
		S->SetSize(FVector2D(220.0f, 34.0f));
	}

	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("Row"));
	Bg->AddChild(Row);
	if (UBorderSlot* BS = Cast<UBorderSlot>(Row->Slot))
	{
		BS->SetPadding(FMargin(0.0f));
		BS->SetHorizontalAlignment(HAlign_Center);
		BS->SetVerticalAlignment(VAlign_Center);
	}

	// Keycap
	UBorder* KeyCap = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("KeyCap"));
	KeyCap->SetBrushColor(FLinearColor(0.05f, 0.09f, 0.13f, 0.95f));
	KeyCap->SetPadding(FMargin(7.0f, 1.0f, 7.0f, 2.0f));
	Row->AddChild(KeyCap);
	if (UHorizontalBoxSlot* HS = Cast<UHorizontalBoxSlot>(KeyCap->Slot))
	{
		HS->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		HS->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
		HS->SetVerticalAlignment(VAlign_Center);
	}

	UTextBlock* KeyTxt = UIStyle::MakeText(WidgetTree, TEXT("F"), UIStyle::Accent, 12, 0, true);
	KeyCap->AddChild(KeyTxt);
	if (UBorderSlot* KS = Cast<UBorderSlot>(KeyTxt->Slot))
	{
		KS->SetPadding(FMargin(0.0f));
		KS->SetHorizontalAlignment(HAlign_Center);
		KS->SetVerticalAlignment(VAlign_Center);
	}

	PromptText = UIStyle::MakeText(WidgetTree, TEXT("EQUIP PARAMOTOR"), UIStyle::Text, 12, 2, true);
	Row->AddChild(PromptText);
	if (UHorizontalBoxSlot* HS = Cast<UHorizontalBoxSlot>(PromptText->Slot))
	{
		HS->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		HS->SetVerticalAlignment(VAlign_Center);
	}
}
