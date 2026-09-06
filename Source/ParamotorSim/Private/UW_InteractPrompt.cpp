#include "UW_InteractPrompt.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Components/Border.h"
#include "Components/BorderSlot.h"

void UUW_InteractPrompt::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
	WidgetTree->RootWidget = Root;

	// Background border — match HUD dark navy style
	UBorder* Bg = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Bg"));
	Bg->SetBrushColor(FLinearColor(0.02f, 0.04f, 0.08f, 0.65f));
	Bg->SetPadding(FMargin(16.0f, 8.0f, 16.0f, 10.0f));
	Root->AddChild(Bg);
	if (UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(Bg->Slot))
	{
		S->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
		S->SetPosition(FVector2D(-120.0f, -20.0f));
		S->SetSize(FVector2D(240.0f, 40.0f));
	}

	// Prompt text — match HUD highlight color and outlined font
	PromptText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("PromptText"));
	PromptText->SetText(FText::FromString("Press F to Equip"));
	PromptText->SetJustification(ETextJustify::Center);
	PromptText->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.85f, 0.35f, 1.0f)));
	FSlateFontInfo Font = PromptText->GetFont();
	Font.Size = 14;
	Font.OutlineSettings = FFontOutlineSettings(1, FLinearColor(0.0f, 0.0f, 0.0f, 0.6f));
	PromptText->SetFont(Font);
	Bg->AddChild(PromptText);
	if (UBorderSlot* BS = Cast<UBorderSlot>(PromptText->Slot))
	{
		BS->SetPadding(FMargin(0.0f));
		BS->SetHorizontalAlignment(HAlign_Fill);
		BS->SetVerticalAlignment(VAlign_Center);
	}
}
