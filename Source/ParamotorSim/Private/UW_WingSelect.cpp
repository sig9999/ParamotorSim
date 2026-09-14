// Copyright Epic Games, Inc. All Rights Reserved.

#include "UW_WingSelect.h"
#include "UIStyle.h"
#include "IndexedButton.h"
#include "ParamotorGameInstance.h"
#include "ParamotorSaveGame.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/BorderSlot.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Texture2D.h"
#include "Kismet/GameplayStatics.h"

namespace WingUI
{
	static const FLinearColor Bg        (0.800f, 0.825f, 0.855f, 1.0f);
	static const FLinearColor CardBg    (0.110f, 0.135f, 0.170f, 1.0f);   // dark slate panels
	static const FLinearColor CardHover (0.160f, 0.190f, 0.230f, 1.0f);
	static const FLinearColor Ink       (0.07f, 0.09f, 0.12f, 1.0f);      // text on light bg
	static const FLinearColor Dim       (0.34f, 0.39f, 0.46f, 1.0f);
	static const FLinearColor Faint     (0.45f, 0.50f, 0.57f, 1.0f);
	static const FLinearColor CardInk   (0.92f, 0.94f, 0.96f, 1.0f);      // text on dark panels
	static const FLinearColor CardDim   (0.70f, 0.74f, 0.79f, 1.0f);
	static const FLinearColor CardFaint (0.52f, 0.56f, 0.62f, 1.0f);
	static const FLinearColor Accent    (0.00f, 0.48f, 0.66f, 1.0f);
	static const FLinearColor AccentHi  (0.00f, 0.62f, 0.82f, 1.0f);
	static const FLinearColor AccentLt  (0.25f, 0.78f, 0.95f, 1.0f);      // accent on dark panels
	static const FLinearColor Hairline  (0.68f, 0.72f, 0.77f, 1.0f);

	// Shared factories — WingSelect keeps its own light palette but uses the
	// UIStyle widget builders.
	using UIStyle::MakeText;
	using UIStyle::MakePanel;
	using UIStyle::MakeLine;

	static UCanvasPanelSlot* Place(UWidget* W, UCanvasPanel* Canvas,
		const FAnchors& Anchors, const FMargin& Offsets, int32 Z = 0)
	{
		return UIStyle::PlaceOnCanvas(W, Canvas, Anchors, Offsets, Z);
	}

	static FButtonStyle GhostStyle()
	{
		FButtonStyle S;
		S.Normal.TintColor   = FSlateColor(FLinearColor(0, 0, 0, 0));
		S.Hovered.TintColor  = FSlateColor(FLinearColor(0, 0, 0, 0.06f));
		S.Pressed.TintColor  = FSlateColor(FLinearColor(0, 0, 0, 0.12f));
		S.Disabled.TintColor = FSlateColor(FLinearColor(0, 0, 0, 0));
		S.NormalPadding  = FMargin(0.0f);
		S.PressedPadding = FMargin(0.0f);
		return S;
	}

	static FButtonStyle ActionStyle()
	{
		FButtonStyle S;
		S.Normal.TintColor   = FSlateColor(Accent);
		S.Hovered.TintColor  = FSlateColor(AccentHi);
		S.Pressed.TintColor  = FSlateColor(FLinearColor(0.0f, 0.36f, 0.50f, 1.0f));
		S.Disabled.TintColor = FSlateColor(FLinearColor(0.75f, 0.80f, 0.85f, 1.0f));
		S.NormalPadding  = FMargin(0.0f);
		S.PressedPadding = FMargin(0.0f);
		return S;
	}

	static FButtonStyle CardStyle()
	{
		FButtonStyle S;
		S.Normal.TintColor   = FSlateColor(CardBg);
		S.Hovered.TintColor  = FSlateColor(CardHover);
		S.Pressed.TintColor  = FSlateColor(FLinearColor(0.14f, 0.16f, 0.19f, 1.0f));
		S.Disabled.TintColor = FSlateColor(CardBg);
		S.NormalPadding  = FMargin(0.0f);
		S.PressedPadding = FMargin(0.0f);
		return S;
	}
}

void UUW_WingSelect::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	GI = Cast<UParamotorGameInstance>(UGameplayStatics::GetGameInstance(this));

	RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>();
	WidgetTree->RootWidget = RootCanvas;

	// Preselect the saved/current wing so the detail sheet is never blank.
	if (GI && GI->WingSpecs.IsValidIndex(GI->CurrentBuild.WingIndex))
	{
		SelectedWingIndex = GI->CurrentBuild.WingIndex;
	}
	else if (GI && GI->WingSpecs.Num() > 0)
	{
		SelectedWingIndex = 0;
	}

	RebuildUI();
}

void UUW_WingSelect::RebuildUI()
{
	using namespace WingUI;
	if (!RootCanvas) return;
	RootCanvas->ClearChildren();

	// ---- opaque light background ----
	UBorder* BgPanel = MakePanel(WidgetTree, Bg);
	Place(BgPanel, RootCanvas, FAnchors(0.0f, 0.0f, 1.0f, 1.0f), FMargin(0.0f), 0);

	// ---- header ----
	const float LX = 56.0f;
	UTextBlock* Kicker = MakeText(WidgetTree, TEXT("BUILD  ·  STEP 02  ·  CANOPY"), Accent, 12, 4);
	Place(Kicker, RootCanvas, FAnchors(0.0f, 0.0f), FMargin(LX, 42.0f, 500.0f, 18.0f), 2);

	UTextBlock* Title = MakeText(WidgetTree, TEXT("WINGS"), Ink, 44, 3);
	Place(Title, RootCanvas, FAnchors(0.0f, 0.0f), FMargin(LX - 2.0f, 64.0f, 600.0f, 56.0f), 2);

	UImage* TitleRule = MakeLine(WidgetTree, Accent);
	Place(TitleRule, RootCanvas, FAnchors(0.0f, 0.0f), FMargin(LX + 2.0f, 128.0f, 110.0f, 2.0f), 2);

	// ---- brand tab strip across the top (Forza-style) ----
	TArray<FString> Brands;
	Brands.Add(TEXT("ALL"));
	if (GI)
	{
		for (const FParamotorWingSpec& W : GI->WingSpecs)
		{
			FString B = W.Brand.ToUpper();
			if (!Brands.Contains(B)) Brands.Add(B);
		}
	}
	float TX = LX;
	for (int32 i = 0; i < Brands.Num(); ++i)
	{
		const bool bActive = (i == 0 && ActiveBrand.IsEmpty()) || Brands[i] == ActiveBrand;
		UIndexedButton* Tab = CreateWidget<UIndexedButton>(this, UIndexedButton::StaticClass());
		Tab->SetIndex(i);
		Tab->SetButtonStyle(GhostStyle());
		UTextBlock* Txt = MakeText(WidgetTree, *Brands[i],
			bActive ? Ink : Faint, 15, 3, ETextJustify::Center);
		Tab->SetContent(Txt);
		Tab->OnIndexedClicked.AddDynamic(this, &UUW_WingSelect::OnBrandTabClicked);
		Place(Tab, RootCanvas, FAnchors(0.0f, 0.0f), FMargin(TX, 150.0f, 200.0f, 44.0f), 3);
		if (bActive)
		{
			UImage* Underline = MakeLine(WidgetTree, Accent);
			Place(Underline, RootCanvas, FAnchors(0.0f, 0.0f), FMargin(TX + 16.0f, 186.0f, 168.0f, 2.0f), 4);
		}
		TX += 210.0f;
	}
	UImage* TabRule = MakeLine(WidgetTree, Hairline);
	Place(TabRule, RootCanvas, FAnchors(0.0f, 0.0f, 1.0f, 0.0f), FMargin(LX, 188.0f, -LX, 1.0f), 3);

	// ---- wing cards (left ~60%) ----
	if (GI)
	{
		float CX = LX;
		const float CY = 226.0f;
		const float CardW = 420.0f;
		const float CardH = 252.0f;
		for (int32 i = 0; i < GI->WingSpecs.Num(); ++i)
		{
			const FParamotorWingSpec& W = GI->WingSpecs[i];
			if (!ActiveBrand.IsEmpty() && !W.Brand.ToUpper().Equals(ActiveBrand)) continue;

			const bool bSel = (i == SelectedWingIndex);

			UIndexedButton* Card = CreateWidget<UIndexedButton>(this, UIndexedButton::StaticClass());
			Card->SetIndex(i);
			Card->SetButtonStyle(CardStyle());
			Card->OnIndexedClicked.AddDynamic(this, &UUW_WingSelect::OnWingCardClicked);
			Place(Card, RootCanvas, FAnchors(0.0f, 0.0f), FMargin(CX, CY, CardW, CardH), 3);

			// card text content (image placed on the canvas below — VBox
			// auto-sizing fights UImage desired-size overrides)
			UVerticalBox* Col = WidgetTree->ConstructWidget<UVerticalBox>();
			Card->SetContent(Col);

			UTextBlock* CBrand = MakeText(WidgetTree, *W.Brand.ToUpper(), AccentLt, 10, 4);
			Col->AddChild(CBrand);
			if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(CBrand->Slot))
			{
				VS->SetPadding(FMargin(16.0f, 12.0f, 16.0f, 0.0f));
			}

			// spacer for the planform image
			USizeBox* Spacer = WidgetTree->ConstructWidget<USizeBox>();
			Spacer->SetHeightOverride(112.0f);
			Col->AddChild(Spacer);

			UTextBlock* CModel = MakeText(WidgetTree, *W.Model.ToUpper(), CardInk, 22, 2);
			Col->AddChild(CModel);
			if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(CModel->Slot))
			{
				VS->SetPadding(FMargin(16.0f, 4.0f, 16.0f, 0.0f));
			}

			FString Quick = FString::Printf(TEXT("%s  ·  %d CELLS  ·  AR %.1f  ·  %.1f kg"),
				*W.Size, W.Cells, W.AspectRatioFlat, W.WingWeight_kg);
			UTextBlock* CQuick = MakeText(WidgetTree, *Quick, CardDim, 12, 1);
			Col->AddChild(CQuick);
			if (UVerticalBoxSlot* QS = Cast<UVerticalBoxSlot>(CQuick->Slot))
			{
				QS->SetPadding(FMargin(16.0f, 6.0f, 16.0f, 0.0f));
			}

			UTextBlock* CCert = MakeText(WidgetTree, *W.Certification, CardFaint, 11, 2);
			Col->AddChild(CCert);
			if (UVerticalBoxSlot* CS2 = Cast<UVerticalBoxSlot>(CCert->Slot))
			{
				CS2->SetPadding(FMargin(16.0f, 4.0f, 16.0f, 0.0f));
			}

			// planform image — directly on the canvas so sizing is explicit
			UTexture2D* Tex = W.ImagePath.IsEmpty() ? nullptr : LoadObject<UTexture2D>(nullptr, *W.ImagePath);
			if (Tex)
			{
				UImage* Img = WidgetTree->ConstructWidget<UImage>();
				Img->SetBrushFromTexture(Tex);
				Img->SetDesiredSizeOverride(FVector2D(360.0f, 95.0f));
				Img->SetVisibility(ESlateVisibility::HitTestInvisible);
				Place(Img, RootCanvas, FAnchors(0.0f, 0.0f), FMargin(CX + 30.0f, CY + 30.0f, 360.0f, 95.0f), 4);
			}

			// selected underline
			if (bSel)
			{
				UImage* SelBar = MakeLine(WidgetTree, Accent);
				Place(SelBar, RootCanvas, FAnchors(0.0f, 0.0f), FMargin(CX, CY + CardH, CardW, 3.0f), 5);
			}

			CX += CardW + 24.0f;
		}
	}

	// ---- detail spec sheet (right panel) ----
	const float DX = 0.64f;
	UBorder* Sheet = MakePanel(WidgetTree, CardBg);
	Place(Sheet, RootCanvas, FAnchors(DX, 0.0f, 0.96f, 1.0f), FMargin(0.0f, 210.0f, 0.0f, -76.0f), 2);

	DetailBrand = MakeText(WidgetTree, TEXT(""), AccentLt, 11, 4);
	Place(DetailBrand, RootCanvas, FAnchors(DX, 0.0f, 0.96f, 0.0f), FMargin(24.0f, 232.0f, 24.0f, 16.0f), 3);

	DetailTitle = MakeText(WidgetTree, TEXT(""), CardInk, 30, 2);
	Place(DetailTitle, RootCanvas, FAnchors(DX, 0.0f, 0.96f, 0.0f), FMargin(24.0f, 252.0f, 24.0f, 40.0f), 3);

	DetailSub = MakeText(WidgetTree, TEXT(""), CardDim, 12, 1);
	Place(DetailSub, RootCanvas, FAnchors(DX, 0.0f, 0.96f, 0.0f), FMargin(24.0f, 296.0f, 24.0f, 18.0f), 3);

	DetailImage = WidgetTree->ConstructWidget<UImage>();
	DetailImage->SetDesiredSizeOverride(FVector2D(360.0f, 95.0f));
	Place(DetailImage, RootCanvas, FAnchors(DX, 0.0f, 0.96f, 0.0f), FMargin(24.0f, 326.0f, 24.0f, 110.0f), 3);

	DetailSpecs = MakeText(WidgetTree, TEXT(""), CardDim, 11, 0);
	DetailSpecs->SetAutoWrapText(true);
	Place(DetailSpecs, RootCanvas, FAnchors(DX, 0.0f, 0.96f, 0.0f), FMargin(24.0f, 450.0f, 24.0f, 190.0f), 3);

	// Notes live under the card column — brochure-style caption.
	DetailNotes = MakeText(WidgetTree, TEXT(""), Dim, 12, 0);
	DetailNotes->SetAutoWrapText(true);
	Place(DetailNotes, RootCanvas, FAnchors(0.0f, 0.0f, 0.60f, 0.0f), FMargin(LX, 586.0f, 24.0f, 80.0f), 3);

	// ---- bottom bar — kept at ~91% height: the UMG viewport is taller than
	// the windowed client area, so true bottom anchors clip ----
	UImage* BarRule = MakeLine(WidgetTree, Hairline);
	Place(BarRule, RootCanvas, FAnchors(0.0f, 0.905f, 1.0f, 0.905f), FMargin(0.0f, 0.0f, 0.0f, 1.0f), 3);

	UIndexedButton* BackBtn = CreateWidget<UIndexedButton>(this, UIndexedButton::StaticClass());
	BackBtn->SetIndex(0);
	BackBtn->SetButtonStyle(GhostStyle());
	UTextBlock* BackTxt = MakeText(WidgetTree, TEXT("< BACK TO BUILD"), Dim, 14, 2);
	BackBtn->SetContent(BackTxt);
	BackBtn->OnIndexedClicked.AddDynamic(this, &UUW_WingSelect::OnBackClicked);
	Place(BackBtn, RootCanvas, FAnchors(0.0f, 0.915f), FMargin(LX, 8.0f, 260.0f, 40.0f), 4);

	UIndexedButton* ConfirmBtn = CreateWidget<UIndexedButton>(this, UIndexedButton::StaticClass());
	ConfirmBtn->SetIndex(0);
	ConfirmBtn->SetButtonStyle(ActionStyle());
	ConfirmLabel = MakeText(WidgetTree, TEXT("SELECT WING  ·  CONTINUE"), FLinearColor::White, 15, 3, ETextJustify::Center);
	ConfirmBtn->SetContent(ConfirmLabel);
	ConfirmBtn->OnIndexedClicked.AddDynamic(this, &UUW_WingSelect::OnConfirmClicked);
	Place(ConfirmBtn, RootCanvas, FAnchors(1.0f, 0.915f), FMargin(-LX - 320.0f, 8.0f, 320.0f, 44.0f), 4);

	UpdateDetail();
}

void UUW_WingSelect::UpdateDetail()
{
	using namespace WingUI;
	if (!GI || !GI->WingSpecs.IsValidIndex(SelectedWingIndex)) return;
	const FParamotorWingSpec& W = GI->WingSpecs[SelectedWingIndex];

	if (DetailBrand) DetailBrand->SetText(FText::FromString(W.Brand.ToUpper() + TEXT("  ·  ") + W.Certification));
	if (DetailTitle) DetailTitle->SetText(FText::FromString(W.Model.ToUpper()));
	if (DetailSub)   DetailSub->SetText(FText::FromString(FString::Printf(
		TEXT("%s  ·  %d cells  ·  %.2f m flat span  ·  %.2f kg"), *W.Size, W.Cells, W.FlatSpan_m, W.WingWeight_kg)));

	if (DetailImage)
	{
		UTexture2D* Tex = W.ImagePath.IsEmpty() ? nullptr : LoadObject<UTexture2D>(nullptr, *W.ImagePath);
		if (Tex) { DetailImage->SetBrushFromTexture(Tex); DetailImage->SetVisibility(ESlateVisibility::SelfHitTestInvisible); }
		else { DetailImage->SetVisibility(ESlateVisibility::Collapsed); }
	}

	if (DetailSpecs)
	{
		const FString Specs = FString::Printf(TEXT(
			"FLAT AREA         %.2f m²\n"
			"PROJECTED AREA    %.2f m²\n"
			"FLAT SPAN         %.2f m\n"
			"PROJECTED SPAN    %.2f m\n"
			"ASPECT RATIO      %.1f flat / %.1f projected\n"
			"CELLS             %d\n"
			"WING WEIGHT       %.2f kg\n"
			"TAKE-OFF WEIGHT   %s  (max %d kg / %d lb)\n"
			"TRIM SPEEDS       %s\n"
			"TOP SPEED         %s\n"
			"RISERS            %s\n"
			"COLORS            %s"),
			W.FlatArea_m2, W.ProjectedArea_m2, W.FlatSpan_m, W.ProjectedSpan_m,
			W.AspectRatioFlat, W.AspectRatioProjected, W.Cells, W.WingWeight_kg,
			*W.TowRange, W.MaxWeightKg, W.MaxWeightLb,
			*W.TrimSpeeds, *W.TopSpeed, *W.Risers, *W.Colors);
		DetailSpecs->SetText(FText::FromString(Specs));
	}

	if (DetailNotes) DetailNotes->SetText(FText::FromString(W.Notes));
	if (ConfirmLabel) ConfirmLabel->SetText(FText::FromString(
		FString::Printf(TEXT("FLY THE %s  →"), *W.Model.ToUpper())));
}

void UUW_WingSelect::OnWingCardClicked(int32 Index)
{
	SelectedWingIndex = Index;
	RebuildUI();
}

void UUW_WingSelect::OnBrandTabClicked(int32 Index)
{
	if (!GI) return;
	TArray<FString> Brands;
	Brands.Add(TEXT("ALL"));
	for (const FParamotorWingSpec& W : GI->WingSpecs)
	{
		FString B = W.Brand.ToUpper();
		if (!Brands.Contains(B)) Brands.Add(B);
	}
	if (!Brands.IsValidIndex(Index)) return;
	ActiveBrand = (Index == 0) ? FString() : Brands[Index];
	RebuildUI();
}

void UUW_WingSelect::OnBackClicked(int32 /*Index*/)
{
	RemoveFromParent();
}

void UUW_WingSelect::OnConfirmClicked(int32 /*Index*/)
{
	if (GI && GI->WingSpecs.IsValidIndex(SelectedWingIndex))
	{
		GI->CurrentBuild.WingIndex = SelectedWingIndex;
		UParamotorSaveGame* Save = Cast<UParamotorSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("ParamotorBuildSlot"), 0));
		if (!Save)
		{
			Save = NewObject<UParamotorSaveGame>();
		}
		Save->CurrentBuild = GI->CurrentBuild;
		UGameplayStatics::SaveGameToSlot(Save, TEXT("ParamotorBuildSlot"), 0);
	}
	// Wing chosen — continue to the launch map.
	UGameplayStatics::OpenLevel(this, FName(TEXT("LV_World")));
}
