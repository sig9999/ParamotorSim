// Copyright Epic Games, Inc. All Rights Reserved.

#include "CatalogItemWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/TextBlock.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

namespace CatalogStyle
{
	static FButtonStyle BaseStyle(const FLinearColor& Normal)
	{
		FButtonStyle S;
		FLinearColor Hover = FLinearColor(FMath::Min(Normal.R + 0.06f, 1.0f), FMath::Min(Normal.G + 0.06f, 1.0f), FMath::Min(Normal.B + 0.06f, 1.0f), Normal.A);
		FLinearColor Press = FLinearColor(FMath::Max(Normal.R - 0.04f, 0.0f), FMath::Max(Normal.G - 0.04f, 0.0f), FMath::Max(Normal.B - 0.04f, 0.0f), Normal.A);
		S.Normal.TintColor = FSlateColor(Normal);
		S.Hovered.TintColor = FSlateColor(Hover);
		S.Pressed.TintColor = FSlateColor(Press);
		S.Disabled.TintColor = FSlateColor(FLinearColor(0.04f, 0.04f, 0.04f, 0.5f));
		S.NormalPadding = FMargin(0.0f);
		S.PressedPadding = FMargin(0.0f);
		return S;
	}
}

void UCatalogItemWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetVisibility(ESlateVisibility::Visible);
	SetIsFocusable(true);
	SetClipping(EWidgetClipping::Inherit);

	Accent = WidgetTree->ConstructWidget<UImage>();
	Accent->SetColorAndOpacity(AccentInactive);
	USizeBox* AccentBox = WidgetTree->ConstructWidget<USizeBox>();
	AccentBox->SetWidthOverride(6.0f);
	if (USizeBoxSlot* ABS = Cast<USizeBoxSlot>(AccentBox->AddChild(Accent)))
	{
		ABS->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
		ABS->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
	}

	ItemText = WidgetTree->ConstructWidget<UTextBlock>();
	ItemText->SetColorAndOpacity(FLinearColor(0.9f, 0.92f, 0.96f, 1.0f));
	FSlateFontInfo Font = ItemText->GetFont();
	Font.Size = 16;
	ItemText->SetFont(Font);
	ItemText->SetJustification(ETextJustify::Left);

	SubText = WidgetTree->ConstructWidget<UTextBlock>();
	SubText->SetColorAndOpacity(FLinearColor(0.55f, 0.62f, 0.70f, 1.0f));
	SubText->SetVisibility(ESlateVisibility::Collapsed);
	{
		FSlateFontInfo SubFont = SubText->GetFont();
		SubFont.Size = 11;
		SubText->SetFont(SubFont);
		SubText->SetJustification(ETextJustify::Left);
	}

	UVerticalBox* TextCol = WidgetTree->ConstructWidget<UVerticalBox>();
	TextCol->AddChild(ItemText);
	if (UVerticalBoxSlot* TitleVS = Cast<UVerticalBoxSlot>(ItemText->Slot))
	{
		TitleVS->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
	}
	TextCol->AddChild(SubText);
	if (UVerticalBoxSlot* SubVS = Cast<UVerticalBoxSlot>(SubText->Slot))
	{
		SubVS->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
	}

	UOverlay* ContentOverlay = WidgetTree->ConstructWidget<UOverlay>();
	ContentOverlay->AddChild(AccentBox);
	if (UOverlaySlot* AS = Cast<UOverlaySlot>(AccentBox->Slot))
	{
		AS->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Left);
		AS->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
	}
	ContentOverlay->AddChild(TextCol);
	if (UOverlaySlot* TS = Cast<UOverlaySlot>(TextCol->Slot))
	{
		TS->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
		TS->SetVerticalAlignment(EVerticalAlignment::VAlign_Center);
		TS->SetPadding(FMargin(16.0f, 8.0f, 12.0f, 8.0f));
	}

	ItemButton = WidgetTree->ConstructWidget<UButton>();
	ItemButton->SetStyle(CatalogStyle::BaseStyle(NormalColor));
	ItemButton->OnClicked.AddDynamic(this, &UCatalogItemWidget::OnButtonClicked);
	ItemButton->OnHovered.AddDynamic(this, &UCatalogItemWidget::OnHover);
	ItemButton->OnUnhovered.AddDynamic(this, &UCatalogItemWidget::OnUnhover);
	ItemButton->AddChild(ContentOverlay);
	if (UButtonSlot* BS = Cast<UButtonSlot>(ContentOverlay->Slot))
	{
		BS->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
		BS->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
	}

	WidgetTree->RootWidget = ItemButton;
}

void UCatalogItemWidget::SetItem(int32 InIndex, const FString& InLabel, bool bInSelected, bool bInCenter, int32 InFontSize)
{
	ItemIndex = InIndex;
	bCenter = bInCenter;
	if (ItemText)
	{
		ItemText->SetText(FText::FromString(InLabel));
		ItemText->SetJustification(bCenter ? ETextJustify::Center : ETextJustify::Left);
		FSlateFontInfo Font = ItemText->GetFont();
		Font.Size = InFontSize > 0 ? InFontSize : (bCenter ? 18 : 16);
		ItemText->SetFont(Font);
	}
	if (ItemText && ItemText->GetParent())
	{
		if (UOverlaySlot* TS = Cast<UOverlaySlot>(ItemText->GetParent()->Slot))
		{
			TS->SetPadding(bCenter ? FMargin(12.0f, 6.0f, 12.0f, 6.0f) : FMargin(18.0f, 8.0f, 12.0f, 8.0f));
		}
	}
	SetSelected(bInSelected);
}

void UCatalogItemWidget::SetSubtitle(const FString& InSub)
{
	if (!SubText) return;
	SubText->SetText(FText::FromString(InSub));
	SubText->SetVisibility(InSub.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
}

void UCatalogItemWidget::SetItemEnabled(bool bEnabled)
{
	if (ItemButton)
	{
		ItemButton->SetIsEnabled(bEnabled);
	}
	if (ItemText)
	{
		ItemText->SetColorAndOpacity(bEnabled
			? FLinearColor(0.9f, 0.92f, 0.96f, 1.0f)
			: FLinearColor(0.45f, 0.48f, 0.54f, 1.0f));
	}
}

void UCatalogItemWidget::SetSelected(bool bInSelected)
{
	bSelected = bInSelected;
	if (ItemButton)
	{
		FLinearColor C = bSelected ? SelectedColor : NormalColor;
		ItemButton->SetStyle(CatalogStyle::BaseStyle(C));
	}
	if (Accent)
	{
		Accent->SetColorAndOpacity(bSelected ? AccentActive : AccentInactive);
	}
}

void UCatalogItemWidget::SetItemColors(const FLinearColor& InNormal, const FLinearColor& InSelected)
{
	NormalColor = InNormal;
	SelectedColor = InSelected;
	SetSelected(bSelected);
}

void UCatalogItemWidget::OnButtonClicked()
{
	OnItemClicked.Broadcast(ItemIndex);
}

void UCatalogItemWidget::OnHover()
{
	// Show a dimmed accent bar on hover for tactile feedback.
	if (Accent && !bSelected)
	{
		Accent->SetColorAndOpacity(FLinearColor(0.0f, 0.76f, 1.0f, 0.45f));
	}
	OnItemHovered.Broadcast(ItemIndex);
}

void UCatalogItemWidget::OnUnhover()
{
	if (Accent)
	{
		Accent->SetColorAndOpacity(bSelected ? AccentActive : AccentInactive);
	}
}
