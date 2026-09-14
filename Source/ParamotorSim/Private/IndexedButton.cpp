// Copyright Epic Games, Inc. All Rights Reserved.

#include "IndexedButton.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"

void UIndexedButton::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetVisibility(ESlateVisibility::Visible);
	SetIsFocusable(true);

	Button = WidgetTree->ConstructWidget<UButton>();
	Button->OnClicked.AddDynamic(this, &UIndexedButton::HandleClicked);
	Button->OnHovered.AddDynamic(this, &UIndexedButton::HandleHovered);
	WidgetTree->RootWidget = Button;
}

void UIndexedButton::SetButtonStyle(const FButtonStyle& Style)
{
	if (Button)
	{
		Button->SetStyle(Style);
	}
}

void UIndexedButton::SetContent(UWidget* Content)
{
	if (!Button || !Content) return;
	Button->AddChild(Content);
	if (UButtonSlot* BS = Cast<UButtonSlot>(Content->Slot))
	{
		BS->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
		BS->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
	}
}

void UIndexedButton::SetButtonEnabled(bool bEnabled)
{
	if (Button)
	{
		Button->SetIsEnabled(bEnabled);
	}
}

void UIndexedButton::HandleClicked()
{
	OnIndexedClicked.Broadcast(Index);
}

void UIndexedButton::HandleHovered()
{
	OnIndexedHovered.Broadcast(Index);
}
