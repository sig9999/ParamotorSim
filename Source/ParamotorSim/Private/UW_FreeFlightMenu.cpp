// Copyright Epic Games, Inc. All Rights Reserved.

#include "UW_FreeFlightMenu.h"
#include "ParamotorLogging.h"
#include "UW_MainMenu.h"
#include "ParamotorGameInstance.h"
#include "ParamotorSaveGame.h"
#include "AirportMarker.h"
#include "UIStyle.h"
#include "MenuWidgetHelpers.h"
#include "Blueprint/WidgetTree.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/Texture2D.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Components/Border.h"
#include "Components/BorderSlot.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CheckBox.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/WidgetSwitcher.h"

using namespace MenuUtil;

void UUW_FreeFlightMenu::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true);

	if (UWorld* World = GetWorld())
	{
		GameInstanceRef = Cast<UParamotorGameInstance>(World->GetGameInstance());
		if (!GameInstanceRef)
		{
			UE_LOG(LogParamotor, Log, TEXT("FreeFlightMenu: GameInstance is base %s, creating UParamotorGameInstance"), World->GetGameInstance() ? *World->GetGameInstance()->GetClass()->GetName() : TEXT("NULL"));
			GameInstanceRef = NewObject<UParamotorGameInstance>(World);
			World->SetGameInstance(GameInstanceRef);
		}
	}
	else
	{
		GameInstanceRef = Cast<UParamotorGameInstance>(UGameplayStatics::GetGameInstance(this));
	}
	BuildHangarData();

	if (GameInstanceRef)
	{
		if (UParamotorSaveGame* Save = Cast<UParamotorSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("ParamotorBuildSlot"), 0)))
		{
			GameInstanceRef->CurrentBuild = Save->CurrentBuild;
		}
	}

	UOverlay* OverlayRoot = WidgetTree->ConstructWidget<UOverlay>();
	WidgetTree->RootWidget = OverlayRoot;

	UTexture2D* BgTex = LoadObject<UTexture2D>(nullptr, TEXT("/Game/Textures/T_MenuBG.T_MenuBG"));
	if (BgTex)
	{
		UBorder* BGBorder = WidgetTree->ConstructWidget<UBorder>();
		BGBorder->SetBrushFromTexture(BgTex);
		BGBorder->SetPadding(FMargin(0.0f));
		UOverlaySlot* BGSlot = OverlayRoot->AddChildToOverlay(BGBorder);
		FillSlot(BGSlot);
		if (bWorldMapBackground)
		{
			BGBorder->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	DarkOverlay = WidgetTree->ConstructWidget<UBorder>();
	DarkOverlay->SetBrushColor(bWorldMapBackground ? FLinearColor(0.0f, 0.0f, 0.0f, 0.0f) : FLinearColor(0.0f, 0.0f, 0.0f, 0.45f));
	DarkOverlay->SetPadding(FMargin(0.0f));
	DarkOverlay->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	UOverlaySlot* DarkSlot = OverlayRoot->AddChildToOverlay(DarkOverlay);
	FillSlot(DarkSlot);

	UCanvasPanel* RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>();
	DarkOverlay->AddChild(RootCanvas);
	if (UBorderSlot* RootSlot = Cast<UBorderSlot>(RootCanvas->Slot))
	{
		RootSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
		RootSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
	}

	// ---- Slim top strip: title left, tabs right (MSFS/Forza hybrid) ----
	{
		UBorder* Strip = UIStyle::MakePanel(WidgetTree, FLinearColor(0.006f, 0.009f, 0.015f, 0.88f));
		Strip->SetVisibility(ESlateVisibility::HitTestInvisible);
		RootCanvas->AddChild(Strip);
		if (UCanvasPanelSlot* SS = Cast<UCanvasPanelSlot>(Strip->Slot))
		{
			SS->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 0.0f));
			SS->SetOffsets(FMargin(0.0f, 0.0f, 0.0f, 64.0f));
		}

		UImage* StripLine = UIStyle::MakeLine(WidgetTree, UIStyle::Edge);
		RootCanvas->AddChild(StripLine);
		if (UCanvasPanelSlot* LS = Cast<UCanvasPanelSlot>(StripLine->Slot))
		{
			LS->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 0.0f));
			LS->SetOffsets(FMargin(0.0f, 64.0f, 0.0f, 1.0f));
		}

		UTextBlock* StripTitle = UIStyle::MakeText(WidgetTree, TEXT("FREE FLIGHT"), UIStyle::Text, 18, 4, true);
		RootCanvas->AddChild(StripTitle);
		if (UCanvasPanelSlot* TS = Cast<UCanvasPanelSlot>(StripTitle->Slot))
		{
			TS->SetAnchors(FAnchors(0.0f, 0.0f, 0.0f, 0.0f));
			TS->SetOffsets(FMargin(28.0f, 19.0f, 260.0f, 26.0f));
		}

		UImage* StripAccent = UIStyle::MakeLine(WidgetTree, UIStyle::Accent);
		RootCanvas->AddChild(StripAccent);
		if (UCanvasPanelSlot* AS = Cast<UCanvasPanelSlot>(StripAccent->Slot))
		{
			AS->SetAnchors(FAnchors(0.0f, 0.0f, 0.0f, 0.0f));
			AS->SetOffsets(FMargin(28.0f, 47.0f, 88.0f, 2.0f));
		}

		// Tabs — right-aligned text buttons with underline indicator
		const TCHAR* TabNames[] = { TEXT("HANGAR"), TEXT("SETTINGS"), TEXT("WEATHER"), TEXT("WORLD") };
		const float TabW = 148.0f;
		TabUnderlines.SetNum(4);
		for (int32 i = 0; i < 4; ++i)
		{
			UTextBlock* TabText = nullptr;
			UButton* Btn = UIStyle::MakeGhostButton(WidgetTree, TabNames[i], 15, UIStyle::TextDim, ETextJustify::Center, &TabText);
			RootCanvas->AddChild(Btn);
			if (UCanvasPanelSlot* BS = Cast<UCanvasPanelSlot>(Btn->Slot))
			{
				BS->SetAnchors(FAnchors(1.0f, 0.0f, 1.0f, 0.0f));
				BS->SetOffsets(FMargin(-28.0f - (4 - i) * TabW, 8.0f, TabW, 50.0f));
			}

			UImage* Under = UIStyle::MakeLine(WidgetTree, UIStyle::Accent);
			Under->SetVisibility(ESlateVisibility::Hidden);
			RootCanvas->AddChild(Under);
			if (UCanvasPanelSlot* US = Cast<UCanvasPanelSlot>(Under->Slot))
			{
				US->SetAnchors(FAnchors(1.0f, 0.0f, 1.0f, 0.0f));
				US->SetOffsets(FMargin(-28.0f - (4 - i) * TabW + 36.0f, 58.0f, TabW - 72.0f, 2.0f));
			}
			TabUnderlines[i] = Under;

			switch (i)
			{
				case 0: Btn->OnClicked.AddDynamic(this, &UUW_FreeFlightMenu::OnGarageTabClicked);   break;
				case 1: Btn->OnClicked.AddDynamic(this, &UUW_FreeFlightMenu::OnSettingsTabClicked); break;
				case 2: Btn->OnClicked.AddDynamic(this, &UUW_FreeFlightMenu::OnWeatherTabClicked);  break;
				case 3: Btn->OnClicked.AddDynamic(this, &UUW_FreeFlightMenu::OnWorldTabClicked);    break;
			}
		}
	}

	// Content area — centered card below the strip (settings/weather tabs);
	// the world tab leaves it transparent so the globe shows through.
	UOverlay* ContentArea = WidgetTree->ConstructWidget<UOverlay>();
	RootCanvas->AddChild(ContentArea);
	if (UCanvasPanelSlot* AreaSlot = Cast<UCanvasPanelSlot>(ContentArea->Slot))
	{
		AreaSlot->SetAnchors(FAnchors(0.5f, 0.60f, 0.5f, 0.60f));
		AreaSlot->SetOffsets(FMargin(-460.0f, -270.0f, 920.0f, 540.0f));
	}

	ContentBG = WidgetTree->ConstructWidget<UBorder>();
	ContentBG->SetBrushColor(FLinearColor(0.012f, 0.018f, 0.028f, bWorldMapBackground ? 0.0f : 0.90f));
	ContentBG->SetPadding(FMargin(0.0f));
	ContentBG->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	UOverlaySlot* BGSlot = ContentArea->AddChildToOverlay(ContentBG);
	FillSlot(BGSlot);

	ContentSwitcher = WidgetTree->ConstructWidget<UWidgetSwitcher>();
	ContentArea->AddChildToOverlay(ContentSwitcher);
	if (UOverlaySlot* SwitcherSlot = Cast<UOverlaySlot>(ContentSwitcher->Slot))
	{
		SwitcherSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
		SwitcherSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
	}

	auto AddPanelBG = [&](UCanvasPanel* Panel)
	{
		UBorder* BG = WidgetTree->ConstructWidget<UBorder>();
		BG->SetBrushColor(FLinearColor(0.02f, 0.03f, 0.05f, 1.0f));
		BG->SetPadding(FMargin(0.0f));
		BG->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		Panel->AddChild(BG);
		if (UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(BG->Slot))
		{
			S->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
			S->SetOffsets(FMargin(0.0f));
		}
	};

	// Hangar tab
	{
		UCanvasPanel* Panel = WidgetTree->ConstructWidget<UCanvasPanel>();
		AddPanelBG(Panel);
		BuildHangarPanel(Panel);
		ContentSwitcher->AddChild(Panel);
	}

	// Settings tab
	{
		UCanvasPanel* Panel = WidgetTree->ConstructWidget<UCanvasPanel>();
		AddPanelBG(Panel);

		UTextBlock* Label = MakeText(WidgetTree, TEXT("Settings"), FLinearColor(1.0f, 1.0f, 1.0f, 1.0f), 36, 4, 1, true, 0.5f);
		Panel->AddChild(Label);
		if (UCanvasPanelSlot* LS = Cast<UCanvasPanelSlot>(Label->Slot))
		{
			LS->SetAnchors(FAnchors(0.5f, 0.15f, 0.5f, 0.15f));
			LS->SetOffsets(FMargin(-300.0f, -30.0f, 600.0f, 60.0f));
		}

		CurrentQualityLabel = MakeText(WidgetTree, TEXT("Current: "), FLinearColor(0.85f, 0.9f, 1.0f, 1.0f), 22, 2, 0, true, 0.5f);
		Panel->AddChild(CurrentQualityLabel);
		if (UCanvasPanelSlot* CQLS = Cast<UCanvasPanelSlot>(CurrentQualityLabel->Slot))
		{
			CQLS->SetAnchors(FAnchors(0.5f, 0.24f, 0.5f, 0.24f));
			CQLS->SetOffsets(FMargin(-300.0f, -25.0f, 600.0f, 50.0f));
		}

		UHorizontalBox* QualityRow = WidgetTree->ConstructWidget<UHorizontalBox>();
		Panel->AddChild(QualityRow);
		if (UCanvasPanelSlot* QRS = Cast<UCanvasPanelSlot>(QualityRow->Slot))
		{
			QRS->SetAnchors(FAnchors(0.5f, 0.32f, 0.5f, 0.32f));
			QRS->SetOffsets(FMargin(-300.0f, -25.0f, 600.0f, 50.0f));
		}

		auto MakeQBtn = [&](const TCHAR* BtnLabel) -> UButton*
		{
			UButton* Button = WidgetTree->ConstructWidget<UButton>();
			Button->SetStyle(MakePanelStyle(
				FLinearColor(0.06f, 0.08f, 0.11f, 0.78f),
				FLinearColor(0.10f, 0.14f, 0.20f, 0.86f),
				FLinearColor(0.03f, 0.05f, 0.07f, 0.86f)));
			UTextBlock* Txt = MakeText(WidgetTree, BtnLabel, FLinearColor::White, 18, 2, 0, true, 0.5f);
			Button->AddChild(Txt);
			if (UButtonSlot* BS = Cast<UButtonSlot>(Txt->Slot))
			{
				BS->SetPadding(FMargin(0.0f, 8.0f, 0.0f, 8.0f));
				BS->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Center);
				BS->SetVerticalAlignment(EVerticalAlignment::VAlign_Center);
			}
			if (UHorizontalBoxSlot* HS = QualityRow->AddChildToHorizontalBox(Button))
			{
				HS->SetPadding(FMargin(8.0f, 0.0f, 8.0f, 0.0f));
				HS->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Center);
			}
			return Button;
		};

		QualityLowBtn   = MakeQBtn(TEXT("LOW"));
		QualityMedBtn   = MakeQBtn(TEXT("MEDIUM"));
		QualityHighBtn  = MakeQBtn(TEXT("HIGH"));
		QualityEpicBtn  = MakeQBtn(TEXT("EPIC"));

		QualityLowBtn->OnClicked.AddDynamic(this, &UUW_FreeFlightMenu::OnQualityLowClicked);
		QualityMedBtn->OnClicked.AddDynamic(this, &UUW_FreeFlightMenu::OnQualityMediumClicked);
		QualityHighBtn->OnClicked.AddDynamic(this, &UUW_FreeFlightMenu::OnQualityHighClicked);
		QualityEpicBtn->OnClicked.AddDynamic(this, &UUW_FreeFlightMenu::OnQualityEpicClicked);

		VSyncCheckBox = WidgetTree->ConstructWidget<UCheckBox>();
		VSyncCheckBox->AddChild(MakeText(WidgetTree, TEXT(" V-Sync"), FLinearColor(0.9f, 0.95f, 1.0f, 1.0f), 20, 2, 0, true, 0.5f));
		VSyncCheckBox->OnCheckStateChanged.AddDynamic(this, &UUW_FreeFlightMenu::OnVSyncToggled);
		Panel->AddChild(VSyncCheckBox);
		if (UCanvasPanelSlot* VSL = Cast<UCanvasPanelSlot>(VSyncCheckBox->Slot))
		{
			VSL->SetAnchors(FAnchors(0.5f, 0.45f, 0.5f, 0.45f));
			VSL->SetOffsets(FMargin(-300.0f, -25.0f, 600.0f, 50.0f));
		}

		ShadowsCheckBox = WidgetTree->ConstructWidget<UCheckBox>();
		ShadowsCheckBox->AddChild(MakeText(WidgetTree, TEXT(" Shadows"), FLinearColor(0.9f, 0.95f, 1.0f, 1.0f), 20, 2, 0, true, 0.5f));
		ShadowsCheckBox->OnCheckStateChanged.AddDynamic(this, &UUW_FreeFlightMenu::OnShadowsToggled);
		Panel->AddChild(ShadowsCheckBox);
		if (UCanvasPanelSlot* SSL = Cast<UCanvasPanelSlot>(ShadowsCheckBox->Slot))
		{
			SSL->SetAnchors(FAnchors(0.5f, 0.55f, 0.5f, 0.55f));
			SSL->SetOffsets(FMargin(-300.0f, -25.0f, 600.0f, 50.0f));
		}

		AddSettingsSlider(Panel, TEXT("Resolution Scale"), 0.5f, 1.0f, 1.0f, 0.66f, true, ResolutionScaleLabel, ResolutionScaleSlider);
		ResolutionScaleSlider->OnValueChanged.AddDynamic(this, &UUW_FreeFlightMenu::OnResolutionScaleChanged);

		AddSettingsSlider(Panel, TEXT("Master Volume"), 0.0f, 1.0f, 1.0f, 0.79f, true, MasterVolumeLabel, MasterVolumeSlider);
		MasterVolumeSlider->OnValueChanged.AddDynamic(this, &UUW_FreeFlightMenu::OnMasterVolumeChanged);

		AddSettingsSlider(Panel, TEXT("Mouse Sensitivity"), 0.2f, 3.0f, 1.0f, 0.92f, false, MouseSensitivityLabel, MouseSensitivitySlider);
		MouseSensitivitySlider->OnValueChanged.AddDynamic(this, &UUW_FreeFlightMenu::OnMouseSensitivityChanged);

		ContentSwitcher->AddChild(Panel);
	}

	// Weather tab
	{
		UCanvasPanel* Panel = WidgetTree->ConstructWidget<UCanvasPanel>();
		AddPanelBG(Panel);
		BuildWeatherPanel(Panel);
		ContentSwitcher->AddChild(Panel);
	}

	// World tab — the globe fills the screen behind; only a hint line and a
	// slim right-docked airport card sit on top. The panel itself stays empty
	// and hit-test-transparent so globe drag/zoom/clicks pass through.
	{
		UCanvasPanel* Panel = WidgetTree->ConstructWidget<UCanvasPanel>();
		ContentSwitcher->AddChild(Panel);
	}

	// Hint line — centered just under the top strip (world tab only)
	WorldHint = UIStyle::MakeText(WidgetTree, TEXT("DRAG TO ROTATE  ·  SCROLL TO ZOOM  ·  CLICK A PIN"), UIStyle::TextFaint, 12, 3, true, ETextJustify::Center);
	RootCanvas->AddChild(WorldHint);
	if (UCanvasPanelSlot* HS = Cast<UCanvasPanelSlot>(WorldHint->Slot))
	{
		HS->SetAnchors(FAnchors(0.5f, 0.0f, 0.5f, 0.0f));
		HS->SetOffsets(FMargin(-300.0f, 76.0f, 600.0f, 18.0f));
	}

	// Airport card — docked to the right screen edge, vertically centered
	UBorder* InfoBG = WidgetTree->ConstructWidget<UBorder>();
	InfoBG->SetBrushColor(UIStyle::PanelDeep);
	InfoBG->SetPadding(FMargin(0.0f));
	RootCanvas->AddChild(InfoBG);
	if (UCanvasPanelSlot* InfoBGS = Cast<UCanvasPanelSlot>(InfoBG->Slot))
	{
		InfoBGS->SetAnchors(FAnchors(1.0f, 0.5f, 1.0f, 0.5f));
		InfoBGS->SetAlignment(FVector2D(1.0f, 0.5f));
		InfoBGS->SetOffsets(FMargin(-24.0f, 0.0f, 356.0f, 400.0f));
	}

	UCanvasPanel* CardInner = WidgetTree->ConstructWidget<UCanvasPanel>();
	InfoBG->AddChild(CardInner);
	if (UBorderSlot* CIS = Cast<UBorderSlot>(CardInner->Slot))
	{
		CIS->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
		CIS->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
	}

	// accent edge on the card's left side
	UImage* CardEdge = UIStyle::MakeLine(WidgetTree, UIStyle::AccentSoft);
	CardInner->AddChild(CardEdge);
	if (UCanvasPanelSlot* CE = Cast<UCanvasPanelSlot>(CardEdge->Slot))
	{
		CE->SetAnchors(FAnchors(0.0f, 0.0f, 0.0f, 1.0f));
		CE->SetOffsets(FMargin(0.0f, 0.0f, 2.0f, 0.0f));
	}

	LaunchSiteTitle = UIStyle::MakeText(WidgetTree, TEXT("SELECT AN AIRPORT"), UIStyle::Text, 17, 3, true);
	LaunchSiteTitle->SetAutoWrapText(true);
	CardInner->AddChild(LaunchSiteTitle);
	if (UCanvasPanelSlot* TS = Cast<UCanvasPanelSlot>(LaunchSiteTitle->Slot))
	{
		TS->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 0.0f));
		TS->SetOffsets(FMargin(24.0f, 20.0f, 20.0f, 24.0f));
	}

	UImage* CardRule = UIStyle::MakeLine(WidgetTree, UIStyle::AccentSoft);
	CardInner->AddChild(CardRule);
	if (UCanvasPanelSlot* CR = Cast<UCanvasPanelSlot>(CardRule->Slot))
	{
		CR->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 0.0f));
		CR->SetOffsets(FMargin(24.0f, 50.0f, 20.0f, 1.0f));
	}

	LaunchSiteDetails = UIStyle::MakeText(WidgetTree,
		TEXT("Rotate the globe, zoom to a region, and click an airport pin to see details and spawn options."),
		UIStyle::TextDim, 13, 0, false);
	LaunchSiteDetails->SetAutoWrapText(true);
	CardInner->AddChild(LaunchSiteDetails);
	if (UCanvasPanelSlot* DS = Cast<UCanvasPanelSlot>(LaunchSiteDetails->Slot))
	{
		DS->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
		DS->SetOffsets(FMargin(24.0f, 66.0f, 20.0f, 82.0f));
	}

	// FLY button inside the card, docked to its bottom
	FlyButton = UIStyle::MakeActionButton(WidgetTree, TEXT("SELECT AIRPORT"), 15, &FlyButtonText);
	FlyButton->OnClicked.AddDynamic(this, &UUW_FreeFlightMenu::OnFlyClicked);
	FlyButton->OnHovered.AddDynamic(this, &UUW_FreeFlightMenu::OnFlyButtonHovered);
	CardInner->AddChild(FlyButton);
	if (UCanvasPanelSlot* FlySlot = Cast<UCanvasPanelSlot>(FlyButton->Slot))
	{
		FlySlot->SetAnchors(FAnchors(0.0f, 1.0f, 1.0f, 1.0f));
		FlySlot->SetOffsets(FMargin(24.0f, -68.0f, 20.0f, 48.0f));
	}
	FlyButton->SetIsEnabled(false);

	SelectedLaunchSite = 0;

	InitFromCurrentSettings();

	SwitchToTab(3); // start on World tab

	// Dev: -weathertab opens directly on the weather tab with live mode on.
	if (FParse::Param(FCommandLine::Get(), TEXT("weathertab")))
	{
		if (IsValid(GameInstanceRef)) GameInstanceRef->bLiveWeather = true;
		SwitchToTab(2);
		RefreshWeatherPanel();
		FetchLiveWeather();
	}

	// Back button — bottom left ghost chip
	UButton* BackButton = UIStyle::MakeGhostButton(WidgetTree, TEXT("< MAIN MENU"), 15, UIStyle::TextDim);
	BackButton->OnClicked.AddDynamic(this, &UUW_FreeFlightMenu::OnBackClicked);
	RootCanvas->AddChild(BackButton);
	if (UCanvasPanelSlot* BackSlot = Cast<UCanvasPanelSlot>(BackButton->Slot))
	{
		BackSlot->SetAnchors(FAnchors(0.0f, 1.0f, 0.0f, 1.0f));
		BackSlot->SetOffsets(FMargin(16.0f, -64.0f, 200.0f, 36.0f));
	}
}

void UUW_FreeFlightMenu::SwitchToTab(int32 Index)
{
	if (IsValid(ContentSwitcher))
	{
		ContentSwitcher->SetActiveWidgetIndex(Index);
	}

	const bool bWorld = (Index == 3);

	// World tab: globe stays bright and interactive. Other tabs dim it and
	// show the content card.
	if (IsValid(DarkOverlay))
	{
		DarkOverlay->SetBrushColor(bWorld ? FLinearColor(0.0f, 0.0f, 0.0f, 0.10f) : FLinearColor(0.0f, 0.0f, 0.0f, 0.72f));
	}
	if (IsValid(ContentBG))
	{
		ContentBG->SetBrushColor(FLinearColor(0.012f, 0.018f, 0.028f, bWorld ? 0.0f : 0.90f));
	}
	if (IsValid(WorldHint))
	{
		WorldHint->SetVisibility(bWorld ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	for (int32 i = 0; i < TabUnderlines.Num(); ++i)
	{
		if (IsValid(TabUnderlines[i]))
		{
			TabUnderlines[i]->SetVisibility(i == Index ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
		}
	}
}

void UUW_FreeFlightMenu::OnGarageTabClicked() { UGameplayStatics::OpenLevel(this, FName(TEXT("L_Hangar"))); }
void UUW_FreeFlightMenu::OnGarageTabHovered() { }
void UUW_FreeFlightMenu::OnSettingsTabClicked() { SwitchToTab(1); }
void UUW_FreeFlightMenu::OnWeatherTabClicked() { SwitchToTab(2); }
void UUW_FreeFlightMenu::OnWorldTabClicked() { SwitchToTab(3); }

void UUW_FreeFlightMenu::UpdateAirportDisplay(const FAirportInfo& Info)
{
	SelectedLaunchSite = 0;

	if (LaunchSiteTitle)
	{
		LaunchSiteTitle->SetText(FText::FromString(Info.Name));
	}

	if (LaunchSiteDetails)
	{
		FString Details = FString::Printf(
			TEXT("%s, %s\n\n"
			     "ICAO: %s\n"
			     "Elevation: %.0f m ASL\n"
			     "Coordinates: %.2fN  %.2fW\n\n"
			     "%s\n\n"
			     "Status: %s"),
			*Info.City, *Info.State, *Info.ICAO, Info.ElevationM,
			Info.Latitude, FMath::Abs(Info.Longitude),
			*Info.Description,
			Info.bAvailable ? TEXT("Available") : TEXT("Coming Soon"));
		LaunchSiteDetails->SetText(FText::FromString(Details));
	}

	if (FlyButton)
	{
		FlyButton->SetIsEnabled(Info.bAvailable);
	}

	if (FlyButtonText)
	{
		FlyButtonText->SetText(FText::FromString(
			FString::Printf(TEXT("FLY FROM %s"), *Info.ICAO)));
	}

	// Live weather follows the focused airport.
	if (IsValid(GameInstanceRef) && GameInstanceRef->bLiveWeather)
	{
		GameInstanceRef->LiveStatus = TEXT("fetching…");
		FetchLiveWeather();
		RefreshWeatherPanel();
	}
}

void UUW_FreeFlightMenu::OnFlyButtonHovered() { }

void UUW_FreeFlightMenu::OnFlyClicked()
{
	if (!IsValid(GameInstanceRef) || !GameInstanceRef->bAirportSelected)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!IsValid(World)) return;

	// A curated launch site (Westerly etc.) opens its real-terrain map and
	// spawns at the site. Anything else opens the Cesium globe map so the
	// player can fly anywhere on Earth.
	if (GameInstanceRef->SelectSiteByICAO(GameInstanceRef->SelectedAirportICAO))
	{
		const FLaunchSite* Site = GameInstanceRef->GetActiveSite();
		UGameplayStatics::OpenLevel(World,
			FName(Site && !Site->MapName.IsEmpty() ? *Site->MapName : TEXT("L_Westerly")));
	}
	else
	{
		UGameplayStatics::OpenLevel(World, FName(TEXT("L_Earth")));
	}
}

void UUW_FreeFlightMenu::OnBackClicked()
{
	if (bWorldMapBackground)
	{
		if (UWorld* World = GetWorld())
		{
			UGameplayStatics::OpenLevel(World, FName(TEXT("L_MainMenu")));
		}
		return;
	}

	APlayerController* PC = GetOwningPlayer<APlayerController>();
	if (PC)
	{
		if (UUW_MainMenu* Menu = CreateWidget<UUW_MainMenu>(PC, UUW_MainMenu::StaticClass()))
		{
			Menu->SetOwningPlayer(PC);
			Menu->AddToViewport(100);
			Menu->SetVisibility(ESlateVisibility::Visible);
		}
	}
	RemoveFromParent();
}
