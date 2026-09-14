// Copyright Epic Games, Inc. All Rights Reserved.

#include "UW_MainMenu.h"
#include "UW_FreeFlightMenu.h"
#include "CatalogItemWidget.h"
#include "ParamotorGameInstance.h"
#include "UIStyle.h"
#include "MenuWidgetHelpers.h"
#include "Blueprint/WidgetTree.h"
#include "UObject/ConstructorHelpers.h"
#include "Components/Border.h"
#include "Components/BorderSlot.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Texture2D.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"

UUW_MainMenu::UUW_MainMenu(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	static ConstructorHelpers::FObjectFinder<UTexture2D> BG(TEXT("/Game/Textures/T_MenuBG.T_MenuBG"));
	if (BG.Succeeded())
	{
		MenuBackgroundTexture = BG.Object;
	}
}

using namespace MenuUtil;

namespace
{
	UCanvasPanelSlot* SlotToCanvas(UWidget* Widget)
	{
		return Cast<UCanvasPanelSlot>(Widget->Slot);
	}

	// A solid border widget used as one band of a fake gradient.
	UBorder* MakeFadeBand(UWidgetTree* Tree, float Alpha)
	{
		UBorder* B = Tree->ConstructWidget<UBorder>();
		B->SetBrushColor(FLinearColor(0.004f, 0.007f, 0.012f, Alpha));
		B->SetPadding(FMargin(0.0f));
		return B;
	}
}

void UUW_MainMenu::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	using namespace UIStyle;

	UOverlay* OverlayRoot = WidgetTree->ConstructWidget<UOverlay>();
	WidgetTree->RootWidget = OverlayRoot;

	if (!MenuBackgroundTexture)
	{
		MenuBackgroundTexture = LoadObject<UTexture2D>(nullptr, TEXT("/Game/Textures/T_MenuBG.T_MenuBG"));
	}

	// Paramotor background photo
	if (MenuBackgroundTexture)
	{
		UBorder* BGBorder = WidgetTree->ConstructWidget<UBorder>();
		BGBorder->SetBrushFromTexture(MenuBackgroundTexture);
		BGBorder->SetPadding(FMargin(0.0f));
		UOverlaySlot* BGSlot = OverlayRoot->AddChildToOverlay(BGBorder);
		FillSlot(BGSlot);
	}

	// Overall readability dim
	UBorder* DarkOverlay = WidgetTree->ConstructWidget<UBorder>();
	DarkOverlay->SetBrushColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.22f));
	DarkOverlay->SetPadding(FMargin(0.0f));
	FillSlot(OverlayRoot->AddChildToOverlay(DarkOverlay));

	// ---- Fake gradient scrim, left edge: many thin bands of decreasing
	// alpha give a smooth photo->dark falloff behind the menu column.
	{
		const int32 Bands = 80;
		const float BandW = 10.0f;      // ~800px total
		const float MaxA  = 0.82f;
		for (int32 i = 0; i < Bands; ++i)
		{
			const float T = (float)i / (float)(Bands - 1);
			const float F = 1.0f - T;
			UBorder* Band = MakeFadeBand(WidgetTree, MaxA * F * F);
			USizeBox* Box = WidgetTree->ConstructWidget<USizeBox>();
			Box->SetWidthOverride(BandW);
			Box->AddChild(Band);
			if (USizeBoxSlot* Inner = Cast<USizeBoxSlot>(Band->Slot))
			{
				Inner->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
				Inner->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
			}
			OverlayRoot->AddChildToOverlay(Box);
			if (UOverlaySlot* S = Cast<UOverlaySlot>(Box->Slot))
			{
				S->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Left);
				S->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
				S->SetPadding(FMargin(BandW * i, 0.0f, 0.0f, 0.0f));
			}
		}
	}

	// Bottom scrim band — lifts footer/card text off the photo.
	{
		const int32 Bands = 14;
		const float BandH = 12.0f;
		for (int32 i = 0; i < Bands; ++i)
		{
			const float T = (float)i / (float)(Bands - 1);
			UBorder* Band = MakeFadeBand(WidgetTree, 0.5f * T * T); // darkest at bottom
			USizeBox* Box = WidgetTree->ConstructWidget<USizeBox>();
			Box->SetHeightOverride(BandH);
			Box->AddChild(Band);
			if (USizeBoxSlot* Inner = Cast<USizeBoxSlot>(Band->Slot))
			{
				Inner->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
				Inner->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
			}
			OverlayRoot->AddChildToOverlay(Box);
			if (UOverlaySlot* S = Cast<UOverlaySlot>(Box->Slot))
			{
				S->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
				S->SetVerticalAlignment(EVerticalAlignment::VAlign_Bottom);
				S->SetPadding(FMargin(0.0f, 0.0f, 0.0f, BandH * (Bands - 1 - i)));
			}
		}
	}

	RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>();
	FillSlot(OverlayRoot->AddChildToOverlay(RootCanvas));

	// ---- Header block, top-left ----
	const float LX = 64.0f;

	UTextBlock* Kicker = MakeText(WidgetTree, TEXT("POWERED PARAGLIDER FLIGHT SIMULATOR"), Accent, 13, 5, 0, true);
	RootCanvas->AddChild(Kicker);
	if (UCanvasPanelSlot* KS = SlotToCanvas(Kicker))
	{
		KS->SetAnchors(FAnchors(0.0f, 0.0f, 0.0f, 0.0f));
		KS->SetOffsets(FMargin(LX, 56.0f, 500.0f, 20.0f));
	}

	UTextBlock* Title = MakeText(WidgetTree, TEXT("PARAMOTOR SIM"), FLinearColor(1.0f, 0.97f, 0.9f, 1.0f), 62, 6, 2, true, 0.6f);
	Title->SetJustification(ETextJustify::Left);
	RootCanvas->AddChild(Title);
	if (UCanvasPanelSlot* TitleSlot = SlotToCanvas(Title))
	{
		TitleSlot->SetAnchors(FAnchors(0.0f, 0.0f, 0.0f, 0.0f));
		TitleSlot->SetOffsets(FMargin(LX - 2.0f, 82.0f, 900.0f, 80.0f));
	}

	UImage* TitleRule = MakeLine(WidgetTree, AccentSoft);
	RootCanvas->AddChild(TitleRule);
	if (UCanvasPanelSlot* UnderlineSlot = SlotToCanvas(TitleRule))
	{
		UnderlineSlot->SetAnchors(FAnchors(0.0f, 0.0f, 0.0f, 0.0f));
		UnderlineSlot->SetOffsets(FMargin(LX + 2.0f, 172.0f, 170.0f, 2.0f));
	}

	UTextBlock* Tagline = MakeText(WidgetTree, TEXT("Real terrain  ·  Real rigs  ·  Global map"), TextDim, 14, 1, 0, true);
	RootCanvas->AddChild(Tagline);
	if (UCanvasPanelSlot* TagS = SlotToCanvas(Tagline))
	{
		TagS->SetAnchors(FAnchors(0.0f, 0.0f, 0.0f, 0.0f));
		TagS->SetOffsets(FMargin(LX, 186.0f, 700.0f, 20.0f));
	}

	// ---- Menu column: numbered entries, hover drives the detail panel ----
	struct FMenuEntry
	{
		const TCHAR* Label; const TCHAR* Sub;
		const TCHAR* Kicker; const TCHAR* Desc; const TCHAR* Feat;
		int32 Id; bool bEnabled;
	};
	const FMenuEntry Entries[] = {
		{ TEXT("FREE FLIGHT"), TEXT("Choose a launch site — fly anywhere"),
		  TEXT("FREE ROAM"),   TEXT("Pick any launch site on the globe and take to the air. Real-world terrain, your callsign, your sky."),
		  TEXT("GLOBAL MAP  ·  REAL TERRAIN  ·  OPEN SKY"), 0, true  },
		{ TEXT("HANGAR"),      TEXT("Assemble your rig, piece by piece"),
		  TEXT("GARAGE"),      TEXT("Build your paramotor on the podium — frame, engine, propeller, harness, wing. Every part is the real article."),
		  TEXT("REAL PARTS  ·  LIVE PREVIEW  ·  SAVED BUILDS"), 1, true  },
		{ TEXT("CAREER"),      TEXT("Structured training — coming soon"),
		  TEXT("PROGRESSION"), TEXT("Lessons, checkrides and missions that take you from first inflation to cross-country."),
		  TEXT("IN DEVELOPMENT"), 2, false },
		{ TEXT("QUIT"),        TEXT("Exit to desktop"),
		  TEXT("EXIT"),        TEXT("Close the simulator and return to your desktop."),
		  TEXT(""), 3, true  },
	};

	float MY = 0.40f;
	for (const FMenuEntry& E : Entries)
	{
		UCatalogItemWidget* Item = CreateWidget<UCatalogItemWidget>(this, UCatalogItemWidget::StaticClass());
		Item->SetItemColors(
			FLinearColor(0.004f, 0.008f, 0.014f, 0.55f),
			FLinearColor(0.010f, 0.15f, 0.20f, 0.82f));
		FString NumberedLabel = FString::Printf(TEXT("%02d   %s"), E.Id + 1, E.Label);
		Item->SetItem(E.Id, NumberedLabel, false, false, 24);
		Item->SetSubtitle(E.Sub);
		Item->SetItemEnabled(E.bEnabled);
		Item->OnItemClicked.AddDynamic(this, &UUW_MainMenu::OnMenuItemClicked);
		Item->OnItemHovered.AddDynamic(this, &UUW_MainMenu::OnMenuItemHovered);
		RootCanvas->AddChild(Item);
		if (UCanvasPanelSlot* IS = SlotToCanvas(Item))
		{
			IS->SetAnchors(FAnchors(0.0f, MY, 0.0f, MY));
			IS->SetOffsets(FMargin(LX - 14.0f, 0.0f, 500.0f, 74.0f));
		}
		MY += 0.082f;
	}

	// ---- Right detail panel — reflects the hovered entry ----
	{
		DetailPanel = MakePanel(WidgetTree, PanelDeep, 0.0f);
		RootCanvas->AddChild(DetailPanel);
		if (UCanvasPanelSlot* DP = SlotToCanvas(DetailPanel))
		{
			DP->SetAnchors(FAnchors(1.0f, 0.30f, 1.0f, 0.30f));
			DP->SetOffsets(FMargin(-400.0f, 0.0f, 360.0f, 220.0f));
		}
		DetailPanel->SetPadding(FMargin(0.0f));

		UImage* PanelRule = MakeLine(WidgetTree, AccentSoft);
		RootCanvas->AddChild(PanelRule);
		if (UCanvasPanelSlot* PR = SlotToCanvas(PanelRule))
		{
			PR->SetAnchors(FAnchors(1.0f, 0.30f, 1.0f, 0.30f));
			PR->SetOffsets(FMargin(-400.0f, 0.0f, 360.0f, 2.0f));
		}

		const float PX = -400.0f + 24.0f;
		const float PY = 0.30f;
		DetailKicker = MakeText(WidgetTree, TEXT("MODE"), Accent, 11, 4, 0, true);
		RootCanvas->AddChild(DetailKicker);
		if (UCanvasPanelSlot* S = SlotToCanvas(DetailKicker))
		{
			S->SetAnchors(FAnchors(1.0f, PY, 1.0f, PY));
			S->SetOffsets(FMargin(PX, 16.0f, 300.0f, 18.0f));
		}

		DetailTitle = MakeText(WidgetTree, TEXT("FREE FLIGHT"), Text, 26, 2, 0, true);
		RootCanvas->AddChild(DetailTitle);
		if (UCanvasPanelSlot* S = SlotToCanvas(DetailTitle))
		{
			S->SetAnchors(FAnchors(1.0f, PY, 1.0f, PY));
			S->SetOffsets(FMargin(PX, 38.0f, 320.0f, 34.0f));
		}

		DetailDesc = MakeText(WidgetTree, TEXT(""), TextDim, 13, 0, 0, true);
		DetailDesc->SetAutoWrapText(true);
		RootCanvas->AddChild(DetailDesc);
		if (UCanvasPanelSlot* S = SlotToCanvas(DetailDesc))
		{
			S->SetAnchors(FAnchors(1.0f, PY, 1.0f, PY));
			S->SetOffsets(FMargin(PX, 80.0f, 312.0f, 96.0f));
		}

		DetailFeat = MakeText(WidgetTree, TEXT(""), Accent, 10, 3, 0, true);
		RootCanvas->AddChild(DetailFeat);
		if (UCanvasPanelSlot* S = SlotToCanvas(DetailFeat))
		{
			S->SetAnchors(FAnchors(1.0f, PY, 1.0f, PY));
			S->SetOffsets(FMargin(PX, 186.0f, 312.0f, 18.0f));
		}

		UpdateDetailPanel(0);
	}

	// ---- Launch site card, bottom-left ----
	{
		UParamotorGameInstance* GI = Cast<UParamotorGameInstance>(UGameplayStatics::GetGameInstance(this));
		const float Temp = GI ? GI->Temperature : 20.0f;
		const float Wind = GI ? GI->WindSpeed : 6.0f;
		const float TOD  = GI ? GI->TimeOfDay : 12.0f;
		const FString LocName = GI ? GI->SelectedAirportName : TEXT("Westerly State Airport");
		const FString LocCode = GI ? GI->SelectedAirportICAO : TEXT("WST");

		UBorder* SiteCard = MakePanel(WidgetTree, PanelDeep, 0.0f);
		RootCanvas->AddChild(SiteCard);
		if (UCanvasPanelSlot* CS = SlotToCanvas(SiteCard))
		{
			CS->SetAnchors(FAnchors(0.0f, 1.0f, 0.0f, 1.0f));
			CS->SetOffsets(FMargin(LX - 14.0f, -150.0f, 400.0f, 96.0f));
		}

		UVerticalBox* CardCol = WidgetTree->ConstructWidget<UVerticalBox>();
		SiteCard->AddChild(CardCol);
		if (UBorderSlot* CardSlot = Cast<UBorderSlot>(CardCol->Slot))
		{
			CardSlot->SetPadding(FMargin(18.0f, 12.0f, 18.0f, 12.0f));
			CardSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Center);
		}

		UTextBlock* CardKicker = MakeText(WidgetTree, TEXT("LAUNCH SITE"), Accent, 11, 4, 0, true);
		CardCol->AddChild(CardKicker);

		FString LocLine = FString::Printf(TEXT("%s  ·  %s"), *LocName.ToUpper(), *LocCode);
		UTextBlock* CardLoc = MakeText(WidgetTree, *LocLine, Text, 15, 1, 0, true);
		CardCol->AddChild(CardLoc);

		int32 HH = (int32)TOD;
		int32 MM = (int32)((TOD - HH) * 60.0f);
		FString CondLine = FString::Printf(TEXT("%.0f°C   WIND %.0f M/S   %02d:%02d LOCAL"), Temp, Wind, HH, MM);
		UTextBlock* CardCond = MakeText(WidgetTree, *CondLine, TextDim, 12, 1, 0, true);
		CardCol->AddChild(CardCond);
		if (UVerticalBoxSlot* CondSlot = Cast<UVerticalBoxSlot>(CardCond->Slot))
		{
			CondSlot->SetPadding(FMargin(0.0f, 6.0f, 0.0f, 0.0f));
		}
	}

	// ---- Footer strip ----
	UImage* FooterLine = MakeLine(WidgetTree, Edge);
	RootCanvas->AddChild(FooterLine);
	if (UCanvasPanelSlot* FL = SlotToCanvas(FooterLine))
	{
		FL->SetAnchors(FAnchors(0.0f, 1.0f, 1.0f, 1.0f));
		FL->SetOffsets(FMargin(LX, -46.0f, -LX, 1.0f));
	}

	UTextBlock* FooterL = MakeText(WidgetTree, TEXT("UNREAL ENGINE 5  ·  CESIUM GLOBE"), TextFaint, 11, 3, false);
	RootCanvas->AddChild(FooterL);
	if (UCanvasPanelSlot* FS = SlotToCanvas(FooterL))
	{
		FS->SetAnchors(FAnchors(0.0f, 1.0f, 0.0f, 1.0f));
		FS->SetOffsets(FMargin(LX, -32.0f, 500.0f, 16.0f));
	}

	UTextBlock* FooterR = MakeText(WidgetTree, TEXT("PRE-ALPHA 0.1"), TextFaint, 11, 3, false);
	FooterR->SetJustification(ETextJustify::Right);
	RootCanvas->AddChild(FooterR);
	if (UCanvasPanelSlot* FS = SlotToCanvas(FooterR))
	{
		FS->SetAnchors(FAnchors(1.0f, 1.0f, 1.0f, 1.0f));
		FS->SetOffsets(FMargin(-560.0f, -32.0f, 500.0f, 16.0f));
	}

	// Fade the UI in on entry.
	SetRenderOpacity(0.0f);
	FadeElapsed = 0.0f;
}

void UUW_MainMenu::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (FadeElapsed < 0.55f)
	{
		FadeElapsed += InDeltaTime;
		SetRenderOpacity(FMath::Clamp(FadeElapsed / 0.45f, 0.0f, 1.0f));
	}
}

void UUW_MainMenu::UpdateDetailPanel(int32 Index)
{
	struct FDetail { const TCHAR* Kicker; const TCHAR* Title; const TCHAR* Desc; const TCHAR* Feat; };
	static const FDetail Details[] = {
		{ TEXT("MODE 01 · FREE ROAM"),
		  TEXT("FREE FLIGHT"),
		  TEXT("Pick any launch site on the globe and take to the air. Real-world terrain, your callsign, your sky."),
		  TEXT("GLOBAL MAP  ·  REAL TERRAIN  ·  OPEN SKY") },
		{ TEXT("MODE 02 · GARAGE"),
		  TEXT("HANGAR"),
		  TEXT("Build your paramotor on the podium — frame, engine, propeller, harness, wing. Every part is the real article."),
		  TEXT("REAL PARTS  ·  LIVE PREVIEW  ·  SAVED BUILDS") },
		{ TEXT("MODE 03 · PROGRESSION"),
		  TEXT("CAREER"),
		  TEXT("Lessons, checkrides and missions that take you from first inflation to cross-country."),
		  TEXT("IN DEVELOPMENT") },
		{ TEXT("EXIT"),
		  TEXT("QUIT"),
		  TEXT("Close the simulator and return to your desktop."),
		  TEXT("") },
	};
	if (Index < 0 || Index >= (int32)UE_ARRAY_COUNT(Details)) return;
	const FDetail& D = Details[Index];
	if (DetailKicker) DetailKicker->SetText(FText::FromString(D.Kicker));
	if (DetailTitle)  DetailTitle->SetText(FText::FromString(D.Title));
	if (DetailDesc)   DetailDesc->SetText(FText::FromString(D.Desc));
	if (DetailFeat)   DetailFeat->SetText(FText::FromString(D.Feat));
}

void UUW_MainMenu::OnMenuItemHovered(int32 Index)
{
	UpdateDetailPanel(Index);
}

void UUW_MainMenu::OnMenuItemClicked(int32 Index)
{
	switch (Index)
	{
		case 0:
		{
			if (UWorld* World = GetWorld())
			{
				UGameplayStatics::OpenLevel(World, FName(TEXT("LV_World")));
			}
			break;
		}
		case 1:
		{
			if (UWorld* World = GetWorld())
			{
				UGameplayStatics::OpenLevel(World, FName(TEXT("L_Hangar")));
			}
			break;
		}
		case 3: OnQuitClicked(); break;
	}
}

void UUW_MainMenu::OnQuitClicked()
{
	UWorld* World = GetWorld();
	if (IsValid(World))
	{
		UKismetSystemLibrary::QuitGame(World, nullptr, EQuitPreference::Quit, false);
	}
}
