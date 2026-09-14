// Copyright Epic Games, Inc. All Rights Reserved.

#include "UW_FreeFlightMenu.h"
#include "ParamotorLogging.h"
#include "ParamotorGameInstance.h"
#include "ParamotorSaveGame.h"
#include "WeatherService.h"
#include "AtmosphereModel.h"
#include "UIStyle.h"
#include "MenuWidgetHelpers.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Components/Border.h"
#include "Components/BorderSlot.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/SizeBox.h"
#include "Components/Slider.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"

using namespace MenuUtil;

// ==== WEATHER TAB ====
UTextBlock* UUW_FreeFlightMenu::CreateSlider(UCanvasPanel* RootCanvas, const TCHAR* Name, float Min, float Max, float Value, float YPos)
{
	UTextBlock* Label = MakeText(WidgetTree, *FString::Printf(TEXT("%s %.1f"), Name, Value), FLinearColor(0.9f, 0.95f, 1.0f, 1.0f), 20, 2, 0, true, 0.5f);
	RootCanvas->AddChild(Label);
	if (UCanvasPanelSlot* LabelSlot = Cast<UCanvasPanelSlot>(Label->Slot))
	{
		LabelSlot->SetAnchors(FAnchors(0.5f, YPos, 0.5f, YPos));
		LabelSlot->SetOffsets(FMargin(-300.0f, -25.0f, 600.0f, 50.0f));
	}

	USlider* Slider = WidgetTree->ConstructWidget<USlider>();
	Slider->SetMinValue(Min);
	Slider->SetMaxValue(Max);
	Slider->SetValue(Value);

	FSliderStyle SliderStyle;
	SliderStyle.NormalBarImage.TintColor = FSlateColor(FLinearColor(0.15f, 0.20f, 0.28f, 1.0f));
	SliderStyle.HoveredBarImage.TintColor = FSlateColor(FLinearColor(0.25f, 0.35f, 0.45f, 1.0f));
	SliderStyle.DisabledBarImage.TintColor = FSlateColor(FLinearColor(0.10f, 0.10f, 0.12f, 1.0f));
	SliderStyle.NormalThumbImage.TintColor = FSlateColor(FLinearColor(1.0f, 1.0f, 1.0f, 1.0f));
	SliderStyle.HoveredThumbImage.TintColor = FSlateColor(FLinearColor(0.0f, 0.8f, 1.0f, 1.0f));
	SliderStyle.DisabledThumbImage.TintColor = FSlateColor(FLinearColor(0.4f, 0.4f, 0.4f, 1.0f));
	Slider->SetWidgetStyle(SliderStyle);

	RootCanvas->AddChild(Slider);
	if (UCanvasPanelSlot* SliderSlot = Cast<UCanvasPanelSlot>(Slider->Slot))
	{
		SliderSlot->SetAnchors(FAnchors(0.5f, YPos + 0.06f, 0.5f, YPos + 0.06f));
		SliderSlot->SetOffsets(FMargin(-300.0f, -15.0f, 600.0f, 30.0f));
	}

	if (FString(Name) == TEXT("Wind Speed")) WindSlider = Slider;
	else if (FString(Name) == TEXT("Gusts")) GustsSlider = Slider;
	else if (FString(Name) == TEXT("Cloud Cover")) CloudSlider = Slider;
	else if (FString(Name) == TEXT("Temperature")) TempSlider = Slider;
	else if (FString(Name) == TEXT("Humidity")) HumiditySlider = Slider;
	else if (FString(Name) == TEXT("Time of Day")) TimeSlider = Slider;

	return Label;
}

void UUW_FreeFlightMenu::OnWindSpeedChanged(float Value)
{
	if (IsValid(GameInstanceRef)) GameInstanceRef->WindSpeed = Value;
	if (IsValid(WindLabel)) WindLabel->SetText(FText::FromString(FString::Printf(TEXT("Wind Speed %.0f"), Value)));
}

void UUW_FreeFlightMenu::OnWindGustsChanged(float Value)
{
	if (IsValid(GameInstanceRef)) GameInstanceRef->WindGusts = Value;
	if (IsValid(GustsLabel)) GustsLabel->SetText(FText::FromString(FString::Printf(TEXT("Gusts %.0f"), Value)));
}

void UUW_FreeFlightMenu::OnCloudCoverChanged(float Value)
{
	if (IsValid(GameInstanceRef)) GameInstanceRef->CloudCover = Value;
	if (IsValid(CloudLabel)) CloudLabel->SetText(FText::FromString(FString::Printf(TEXT("Cloud Cover %.2f"), Value)));
}

void UUW_FreeFlightMenu::OnTemperatureChanged(float Value)
{
	if (IsValid(GameInstanceRef))
	{
		GameInstanceRef->Temperature = Value;
		GameInstanceRef->DewPoint = ComputeDewPoint(Value, GameInstanceRef->Humidity);
	}
	if (IsValid(TempLabel)) TempLabel->SetText(FText::FromString(FString::Printf(TEXT("%.0f°C"), Value)));
}

void UUW_FreeFlightMenu::OnHumidityChanged(float Value)
{
	if (IsValid(GameInstanceRef))
	{
		GameInstanceRef->Humidity = Value;
		GameInstanceRef->DewPoint = ComputeDewPoint(GameInstanceRef->Temperature, Value);
	}
	if (IsValid(HumidityLabel)) HumidityLabel->SetText(FText::FromString(FString::Printf(TEXT("%.0f%%"), Value)));
}

void UUW_FreeFlightMenu::OnTimeOfDayChanged(float Value)
{
	if (IsValid(GameInstanceRef)) GameInstanceRef->TimeOfDay = Value;
	if (IsValid(TimeLabel)) TimeLabel->SetText(FText::FromString(FString::Printf(TEXT("Time of Day %.0f"), Value)));
}

// =====================================================================
// WEATHER TAB — live METAR for the focused airport + manual override
// =====================================================================

void UUW_FreeFlightMenu::BuildWeatherPanel(UCanvasPanel* Panel)
{
	const FLinearColor Txt = FLinearColor(0.92f, 0.95f, 1.0f, 1.0f);
	const FLinearColor Dim = FLinearColor(0.55f, 0.62f, 0.72f, 1.0f);
	const FLinearColor Faint = FLinearColor(0.40f, 0.46f, 0.56f, 1.0f);
	const FLinearColor Cyan = FLinearColor(0.15f, 0.75f, 0.90f, 1.0f);
	const FLinearColor CardBG = FLinearColor(0.03f, 0.045f, 0.065f, 0.92f);

	auto Place = [&](UWidget* Widget, float X, float Y, float Width, float Height)
	{
		Panel->AddChild(Widget);
		if (UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(Widget->Slot))
		{
			S->SetAnchors(FAnchors(0.0f, 0.0f, 0.0f, 0.0f));
			S->SetOffsets(FMargin(X, Y, Width, Height));
		}
	};

	auto Card = [&](float X, float Y, float W, float H) -> UVerticalBox*
	{
		UBorder* BG = WidgetTree->ConstructWidget<UBorder>();
		BG->SetBrushColor(CardBG);
		BG->SetPadding(FMargin(16.0f, 12.0f));
		Place(BG, X, Y, W, H);
		UVerticalBox* VB = WidgetTree->ConstructWidget<UVerticalBox>();
		BG->AddChild(VB);
		if (UBorderSlot* BS = Cast<UBorderSlot>(VB->Slot))
		{
			BS->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
			BS->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
		}
		return VB;
	};

	auto VText = [&](UVerticalBox* VB, const TCHAR* Str, const FLinearColor& C, int32 Sz, ETextJustify::Type J = ETextJustify::Left, bool bWrap = false, float Pad = 3.0f) -> UTextBlock*
	{
		UTextBlock* T = MakeText(WidgetTree, Str, C, Sz, 1, 0, false, 0.5f);
		T->SetJustification(J);
		if (bWrap) T->SetAutoWrapText(true);
		UVerticalBoxSlot* VS = VB->AddChildToVerticalBox(T);
		VS->SetPadding(FMargin(0.0f, Pad));
		return T;
	};

	// ---- Header ----
	Place(MakeText(WidgetTree, TEXT("WEATHER"), Txt, 26, 6, 1, true, 0.5f), 40.0f, 16.0f, 300.0f, 34.0f);
	WeatherSource = MakeText(WidgetTree, TEXT("SOURCE: —"), Faint, 12, 2, 0, false, 0.5f);
	WeatherSource->SetJustification(ETextJustify::Left);
	Place(WeatherSource, 40.0f, 52.0f, 500.0f, 18.0f);

	// LIVE toggle — a real checkbox: outlined square + label + hint.
	// Clicking anywhere in the control toggles live mode.
	LiveToggleBtn = WidgetTree->ConstructWidget<UButton>();
	{
		FButtonStyle BS;
		BS.Normal.TintColor  = FSlateColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
		BS.Hovered.TintColor = FSlateColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
		BS.Pressed.TintColor = FSlateColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
		BS.Disabled.TintColor = FSlateColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
		BS.NormalPadding = FMargin(0.0f);
		BS.PressedPadding = FMargin(0.0f);
		LiveToggleBtn->SetStyle(BS);
	}
	UHorizontalBox* ToggleHB = WidgetTree->ConstructWidget<UHorizontalBox>();
	LiveToggleBtn->AddChild(ToggleHB);
	if (UButtonSlot* TBS = Cast<UButtonSlot>(ToggleHB->Slot))
	{
		TBS->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
		TBS->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
	}

	// Checkbox square: outline border + inner fill square (fills when on)
	LiveCheckOutline = WidgetTree->ConstructWidget<UBorder>();
	LiveCheckOutline->SetBrushColor(FLinearColor(0.55f, 0.62f, 0.72f, 1.0f));
	LiveCheckOutline->SetPadding(FMargin(2.5f));
	if (UHorizontalBoxSlot* OS = ToggleHB->AddChildToHorizontalBox(LiveCheckOutline))
	{
		OS->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		OS->SetPadding(FMargin(0.0f, 8.0f, 8.0f, 0.0f));
		OS->SetVerticalAlignment(EVerticalAlignment::VAlign_Top);
	}
	{
		USizeBox* Sq = WidgetTree->ConstructWidget<USizeBox>();
		Sq->SetWidthOverride(18.0f);
		Sq->SetHeightOverride(18.0f);
		LiveCheckOutline->AddChild(Sq);
		LiveCheckFill = WidgetTree->ConstructWidget<UBorder>();
		LiveCheckFill->SetBrushColor(FLinearColor(0.05f, 0.07f, 0.10f, 1.0f));
		Sq->AddChild(LiveCheckFill);
	}

	// Label stack: "LIVE WEATHER" + hint
	UVerticalBox* ToggleVB = WidgetTree->ConstructWidget<UVerticalBox>();
	if (UHorizontalBoxSlot* LS = ToggleHB->AddChildToHorizontalBox(ToggleVB))
	{
		LS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}
	LiveToggleText = MakeText(WidgetTree, TEXT("LIVE WEATHER"), Txt, 15, 3, 1, false, 0.5f);
	LiveToggleText->SetJustification(ETextJustify::Left);
	{
		UVerticalBoxSlot* TS = ToggleVB->AddChildToVerticalBox(LiveToggleText);
		TS->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 1.0f));
	}
	UTextBlock* ToggleHint = MakeText(WidgetTree, TEXT("real METAR feed"), Faint, 11, 1, 0, false, 0.5f);
	ToggleHint->SetJustification(ETextJustify::Left);
	ToggleVB->AddChildToVerticalBox(ToggleHint);

	Place(LiveToggleBtn, 640.0f, 14.0f, 210.0f, 48.0f);
	LiveToggleBtn->OnClicked.AddDynamic(this, &UUW_FreeFlightMenu::OnLiveWeatherClicked);

	UButton* RefreshBtn = UIStyle::MakeGhostButton(WidgetTree, TEXT("REFRESH"), 14, UIStyle::TextDim, ETextJustify::Center);
	Place(RefreshBtn, 856.0f, 20.0f, 86.0f, 38.0f);
	RefreshBtn->OnClicked.AddDynamic(this, &UUW_FreeFlightMenu::OnWeatherRefreshClicked);

	WeatherStatus = MakeText(WidgetTree, TEXT(""), Faint, 12, 1, 0, false, 0.5f);
	WeatherStatus->SetJustification(ETextJustify::Right);
	Place(WeatherStatus, 480.0f, 64.0f, 412.0f, 18.0f);

	// ---- Left card: live METAR data ----
	UVerticalBox* LiveVB = Card(36.0f, 92.0f, 420.0f, 416.0f);
	LiveStationText = VText(LiveVB, TEXT("NO STATION"), Txt, 20, ETextJustify::Left, false, 2.0f);
	LiveMetarText = VText(LiveVB, TEXT(""), Faint, 12, ETextJustify::Left, true, 4.0f);
	LiveDataText = VText(LiveVB, TEXT("Turn on LIVE to pull real weather\nfor the focused airport."), Dim, 14, ETextJustify::Left, true, 8.0f);

	// ---- Right top card: conditions analysis ----
	UVerticalBox* AnVB = Card(466.0f, 92.0f, 420.0f, 168.0f);
	VText(AnVB, TEXT("CONDITIONS ANALYSIS"), Cyan, 12, ETextJustify::Left, false, 2.0f);
	WeatherAnalysis = VText(AnVB, TEXT(""), Dim, 13, ETextJustify::Left, true, 4.0f);

	// ---- Right bottom card: manual controls ----
	UVerticalBox* ManVB = Card(466.0f, 270.0f, 420.0f, 238.0f);
	ManualHint = VText(ManVB, TEXT("CUSTOM WEATHER — turn LIVE off to edit"), Dim, 12, ETextJustify::Left, false, 4.0f);

	WeatherSliders.SetNum(7);
	WeatherSliderLabels.SetNum(7);
	auto Row = [&](int32 Idx, const TCHAR* Name, float Min, float Max, USlider*& OutS, UTextBlock*& OutL, TFunction<void(UUW_FreeFlightMenu*, float)> Handler, const TCHAR* Mangled)
	{
		UHorizontalBox* HB = WidgetTree->ConstructWidget<UHorizontalBox>();
		UVerticalBoxSlot* HVS = ManVB->AddChildToVerticalBox(HB);
		HVS->SetPadding(FMargin(0.0f, 1.0f));

		UTextBlock* L = MakeText(WidgetTree, Name, Dim, 12, 1, 0, false, 0.5f);
		L->SetJustification(ETextJustify::Left);
		if (UHorizontalBoxSlot* LS = HB->AddChildToHorizontalBox(L))
		{
			LS->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
			LS->SetPadding(FMargin(0.0f, 4.0f, 6.0f, 0.0f));
		}
		USlider* S = WidgetTree->ConstructWidget<USlider>();
		S->SetMinValue(Min); S->SetMaxValue(Max); S->SetStepSize(0.01f);
		if (UHorizontalBoxSlot* SS = HB->AddChildToHorizontalBox(S))
		{
			SS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			SS->SetPadding(FMargin(0.0f, 2.0f));
		}
		UTextBlock* V = MakeText(WidgetTree, TEXT(""), Txt, 12, 1, 0, false, 0.5f);
		if (UHorizontalBoxSlot* VS = HB->AddChildToHorizontalBox(V))
		{
			VS->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
			VS->SetPadding(FMargin(8.0f, 4.0f, 0.0f, 0.0f));
		}
		OutS = S; OutL = V;
		WeatherSliders[Idx] = S; WeatherSliderLabels[Idx] = V;
		(void)Handler; (void)Mangled;
	};

	const float WS = IsValid(GameInstanceRef) ? GameInstanceRef->WindSpeed : 6.0f;
	const float WG = IsValid(GameInstanceRef) ? GameInstanceRef->WindGusts : 2.0f;
	const float WD = IsValid(GameInstanceRef) ? GameInstanceRef->WindDirection : 180.0f;
	const float CC = IsValid(GameInstanceRef) ? GameInstanceRef->CloudCover : 0.25f;
	const float TP = IsValid(GameInstanceRef) ? GameInstanceRef->Temperature : 20.0f;
	const float HM = IsValid(GameInstanceRef) ? GameInstanceRef->Humidity : 60.0f;
	const float TD = IsValid(GameInstanceRef) ? GameInstanceRef->TimeOfDay : 12.0f;

	Row(0, TEXT("Wind"), 0.0f, 60.0f, WindSlider, WindLabel, nullptr, TEXT("wind"));
	Row(1, TEXT("Gusts"), 0.0f, 30.0f, GustsSlider, GustsLabel, nullptr, TEXT("gusts"));
	Row(2, TEXT("Dir"), 0.0f, 360.0f, WindDirSlider, WindDirLabel, nullptr, TEXT("dir"));
	Row(3, TEXT("Cloud"), 0.0f, 1.0f, CloudSlider, CloudLabel, nullptr, TEXT("cloud"));
	Row(4, TEXT("Temp"), -20.0f, 45.0f, TempSlider, TempLabel, nullptr, TEXT("temp"));
	Row(5, TEXT("Humid"), 0.0f, 100.0f, HumiditySlider, HumidityLabel, nullptr, TEXT("humid"));
	Row(6, TEXT("Time"), 0.0f, 24.0f, TimeSlider, TimeLabel, nullptr, TEXT("time"));

	WindSlider->SetValue(WS);      WindSlider->OnValueChanged.AddDynamic(this, &UUW_FreeFlightMenu::OnWindSpeedChanged);
	GustsSlider->SetValue(WG);     GustsSlider->OnValueChanged.AddDynamic(this, &UUW_FreeFlightMenu::OnWindGustsChanged);
	WindDirSlider->SetValue(WD);   WindDirSlider->OnValueChanged.AddDynamic(this, &UUW_FreeFlightMenu::OnWindDirectionChanged);
	CloudSlider->SetValue(CC);     CloudSlider->OnValueChanged.AddDynamic(this, &UUW_FreeFlightMenu::OnCloudCoverChanged);
	TempSlider->SetValue(TP);      TempSlider->OnValueChanged.AddDynamic(this, &UUW_FreeFlightMenu::OnTemperatureChanged);
	HumiditySlider->SetValue(HM);  HumiditySlider->OnValueChanged.AddDynamic(this, &UUW_FreeFlightMenu::OnHumidityChanged);
	TimeSlider->SetValue(TD);      TimeSlider->OnValueChanged.AddDynamic(this, &UUW_FreeFlightMenu::OnTimeOfDayChanged);

	RefreshWeatherPanel();
	if (IsValid(GameInstanceRef) && GameInstanceRef->bLiveWeather)
	{
		FetchLiveWeather();
	}
}

void UUW_FreeFlightMenu::OnLiveWeatherClicked()
{
	if (!IsValid(GameInstanceRef)) return;
	GameInstanceRef->bLiveWeather = !GameInstanceRef->bLiveWeather;
	if (GameInstanceRef->bLiveWeather)
	{
		GameInstanceRef->LiveStatus = TEXT("fetching…");
		FetchLiveWeather();
	}
	RefreshWeatherPanel();
}

void UUW_FreeFlightMenu::OnWeatherRefreshClicked()
{
	if (!IsValid(GameInstanceRef)) return;
	if (GameInstanceRef->bLiveWeather)
	{
		GameInstanceRef->LiveStatus = TEXT("fetching…");
		FetchLiveWeather();
		RefreshWeatherPanel();
	}
}

void UUW_FreeFlightMenu::OnWindDirectionChanged(float Value)
{
	if (IsValid(GameInstanceRef) && !GameInstanceRef->bLiveWeather)
	{
		GameInstanceRef->WindDirection = Value;
	}
	if (IsValid(WindDirLabel))
	{
		WindDirLabel->SetText(FText::FromString(FString::Printf(TEXT("%.0f°"), Value)));
	}
}

void UUW_FreeFlightMenu::FetchLiveWeather()
{
	if (!IsValid(GameInstanceRef)) return;
	if (!IsValid(WeatherSvc))
	{
		WeatherSvc = NewObject<UWeatherService>(this);
		WeatherSvc->OnWeatherUpdated.AddDynamic(this, &UUW_FreeFlightMenu::RefreshWeatherPanel);
	}
	GameInstanceRef->LiveStatus = TEXT("fetching…");
	WeatherSvc->Fetch(GameInstanceRef);
}
void UUW_FreeFlightMenu::RefreshWeatherPanel()
{
	if (!IsValid(GameInstanceRef)) return;
	const UParamotorGameInstance* GI = GameInstanceRef;
	const bool bLive = GI->bLiveWeather;

	// Source line — the airport the world map is focused on
	if (IsValid(WeatherSource))
	{
		FString Src = GI->bAirportSelected
			? FString::Printf(TEXT("SOURCE: %s (%s)  ·  %.3f, %.3f"), *GI->SelectedAirportName.ToUpper(), *GI->SelectedAirportICAO, GI->SelectedAirportLat, GI->SelectedAirportLon)
			: FString::Printf(TEXT("SOURCE: %.3f, %.3f  ·  focus an airport on the WORLD tab"), GI->SelectedAirportLat, GI->SelectedAirportLon);
		WeatherSource->SetText(FText::FromString(Src));
	}

	// Checkbox visual — filled accent when live, empty dark when custom
	if (IsValid(LiveCheckFill))
	{
		LiveCheckFill->SetBrushColor(bLive
			? FLinearColor(0.2f, 0.9f, 0.6f, 1.0f)
			: FLinearColor(0.05f, 0.07f, 0.10f, 1.0f));
	}
	if (IsValid(LiveCheckOutline))
	{
		LiveCheckOutline->SetBrushColor(bLive
			? FLinearColor(0.2f, 0.9f, 0.6f, 1.0f)
			: FLinearColor(0.55f, 0.62f, 0.72f, 1.0f));
	}
	if (IsValid(LiveToggleText))
	{
		LiveToggleText->SetText(FText::FromString(bLive ? TEXT("LIVE WEATHER") : TEXT("LIVE WEATHER")));
		LiveToggleText->SetColorAndOpacity(FSlateColor(bLive
			? FLinearColor(0.2f, 0.9f, 0.6f, 1.0f) : UIStyle::TextDim));
	}

	// Status
	if (IsValid(WeatherStatus))
	{
		FString S = GI->LiveStatus;
		if (!bLive && (S == TEXT("live") || S == TEXT("fetching…")))
		{
			S = TEXT("");
		}
		if (bLive && GI->LiveFetchUTC > 0.0)
		{
			const int64 Age = FDateTime::UtcNow().ToUnixTimestamp() - (int64)GI->LiveFetchUTC;
			S += FString::Printf(TEXT(" — updated %llds ago"), Age);
		}
		if (!bLive && S.IsEmpty())
		{
			S = TEXT("custom mode — sliders unlocked");
		}
		WeatherStatus->SetText(FText::FromString(S));
	}

	// Live card
	if (IsValid(LiveStationText))
	{
		FString Head = bLive && !GI->LiveStation.IsEmpty()
			? FString::Printf(TEXT("%s  %s"), *GI->LiveStation, *GI->LiveFlightCat)
			: bLive ? TEXT("FETCHING…") : TEXT("LIVE OFF — custom weather");
		LiveStationText->SetText(FText::FromString(Head));
		LiveStationText->SetColorAndOpacity(FSlateColor(
			GI->LiveFlightCat == TEXT("VFR") ? FLinearColor(0.2f, 0.9f, 0.6f, 1.0f) :
			GI->LiveFlightCat.IsEmpty() ? FLinearColor(0.92f, 0.95f, 1.0f, 1.0f) : FLinearColor(0.95f, 0.7f, 0.2f, 1.0f)));
	}
	if (IsValid(LiveMetarText))
	{
		LiveMetarText->SetText(FText::FromString(GI->LiveMetarRaw));
	}
	if (IsValid(LiveDataText))
	{
		FString D;
		if (bLive && !GI->LiveStation.IsEmpty())
		{
			D = FString::Printf(TEXT(
				"WIND        %03.0f° / %.1f kt%s\n"
				"TEMP        %.1f °C   ·   dew %.1f °C\n"
				"HUMIDITY    %.0f %%\n"
				"ALTIMETER   %.2f inHg\n"
				"VISIBILITY  %.0f mi\n"
				"CLOUDS      %s\n"
				"OBSERVED    %s"),
				GI->WindDirection, GI->WindSpeed / 0.51444f,
				GI->WindGusts > 0.5f ? *FString::Printf(TEXT(" gusting %.0f kt"), GI->WindGusts / 0.51444f) : TEXT(""),
				GI->Temperature, GI->DewPoint, GI->Humidity,
				GI->Altimeter_inHg, GI->Visibility_mi,
				*GI->LiveCloudLayers, *GI->LiveObsTime);
		}
		else if (bLive)
		{
			D = TEXT("contacting aviationweather.gov…");
		}
		else
		{
			D = FString::Printf(TEXT(
				"Custom weather active.\n\n"
				"WIND        %03.0f° / %.1f kt%s\n"
				"TEMP        %.1f °C\n"
				"HUMIDITY    %.0f %%\n"
				"CLOUDS      %.0f %%"),
				GI->WindDirection, GI->WindSpeed / 0.51444f,
				GI->WindGusts > 0.5f ? *FString::Printf(TEXT(" gusting %.0f kt"), GI->WindGusts / 0.51444f) : TEXT(""),
				GI->Temperature, GI->Humidity, GI->CloudCover * 100.0f);
		}
		LiveDataText->SetText(FText::FromString(D));
	}

	// Analysis — Pasquill stability, cloudbase, thermal strength, flyability
	if (IsValid(WeatherAnalysis))
	{
		const EPasquillStability Stab = ComputePasquillClass(GI->Temperature, GI->WindSpeed, GI->TimeOfDay, GI->CloudCover);
		const TCHAR* StabName = Stab == EPasquillStability::A_VeryUnstable ? TEXT("A — very unstable")
			: Stab == EPasquillStability::B_ModeratelyUnstable ? TEXT("B — unstable")
			: Stab == EPasquillStability::C_SlightlyUnstable ? TEXT("C — slight unstable")
			: Stab == EPasquillStability::D_Neutral ? TEXT("D — neutral")
			: Stab == EPasquillStability::E_SlightlyStable ? TEXT("E — stable") : TEXT("F — very stable");

		const float CloudBaseFt = ComputeCloudbase(GI->Temperature, GI->DewPoint) * 3.28084f;
		const float BLH = ComputeBoundaryLayerHeight(Stab, GI->TimeOfDay);
		const float Climb = ComputeThermalClimbRate(GI->Temperature, BLH, GI->CloudCover, GI->TimeOfDay, GI->Humidity);
		const EFlyability Fly = AssessFlyability(GI->WindSpeed, GI->WindGusts, GI->Temperature, GI->Humidity, GI->TimeOfDay, GI->CloudCover, Stab);
		const TCHAR* FlyName = Fly == EFlyability::Excellent ? TEXT("EXCELLENT")
			: Fly == EFlyability::Good ? TEXT("GOOD")
			: Fly == EFlyability::Challenging ? TEXT("CHALLENGING")
			: Fly == EFlyability::Dangerous ? TEXT("DANGEROUS") : TEXT("UNFLYABLE");
		const FLinearColor FlyCol = Fly == EFlyability::Excellent || Fly == EFlyability::Good
			? FLinearColor(0.2f, 0.9f, 0.6f, 1.0f) : Fly == EFlyability::Challenging
			? FLinearColor(0.95f, 0.7f, 0.2f, 1.0f) : FLinearColor(0.95f, 0.3f, 0.25f, 1.0f);

		FString A = FString::Printf(TEXT(
			"STABILITY   %s\n"
			"CLOUDBASE   ~%.0f ft AGL\n"
			"MIXING LAYER ~%.0f ft\n"
			"THERMALS    %s\n"
			"FLYABILITY  "),
			StabName, FMath::Max(0.0f, CloudBaseFt), BLH * 3.28084f,
			Climb > 0.05f ? *FString::Printf(TEXT("%.1f m/s climbs"), Climb) : TEXT("none"));
		(void)FlyCol;
		WeatherAnalysis->SetText(FText::FromString(A + FlyName));
		WeatherAnalysis->SetColorAndOpacity(FSlateColor(FLinearColor(0.75f, 0.82f, 0.92f, 1.0f)));
	}

	// Lock/unlock manual controls
	const bool bEditable = !bLive;
	for (USlider* S : WeatherSliders)
	{
		if (IsValid(S)) S->SetIsEnabled(bEditable);
	}
	if (IsValid(ManualHint))
	{
		ManualHint->SetText(FText::FromString(bLive
			? TEXT("CUSTOM WEATHER — live mode on, sliders locked")
			: TEXT("CUSTOM WEATHER — edit to set your own conditions")));
	}

	// Sync slider positions + readouts to the (possibly live-updated) values
	if (IsValid(WindSlider))      { WindSlider->SetValue(GI->WindSpeed); }
	if (IsValid(GustsSlider))     { GustsSlider->SetValue(GI->WindGusts); }
	if (IsValid(WindDirSlider))   { WindDirSlider->SetValue(GI->WindDirection); }
	if (IsValid(CloudSlider))     { CloudSlider->SetValue(GI->CloudCover); }
	if (IsValid(TempSlider))      { TempSlider->SetValue(GI->Temperature); }
	if (IsValid(HumiditySlider))  { HumiditySlider->SetValue(GI->Humidity); }
	if (IsValid(TimeSlider))      { TimeSlider->SetValue(GI->TimeOfDay); }

	if (IsValid(WindLabel))      WindLabel->SetText(FText::FromString(FString::Printf(TEXT("%.0f kt"), GI->WindSpeed / 0.51444f)));
	if (IsValid(GustsLabel))     GustsLabel->SetText(FText::FromString(FString::Printf(TEXT("%.0f kt"), GI->WindGusts / 0.51444f)));
	if (IsValid(WindDirLabel))   WindDirLabel->SetText(FText::FromString(FString::Printf(TEXT("%.0f°"), GI->WindDirection)));
	if (IsValid(CloudLabel))     CloudLabel->SetText(FText::FromString(FString::Printf(TEXT("%.0f%%"), GI->CloudCover * 100.0f)));
	if (IsValid(TempLabel))      TempLabel->SetText(FText::FromString(FString::Printf(TEXT("%.0f°C"), GI->Temperature)));
	if (IsValid(HumidityLabel))  HumidityLabel->SetText(FText::FromString(FString::Printf(TEXT("%.0f%%"), GI->Humidity)));
	if (IsValid(TimeLabel))      TimeLabel->SetText(FText::FromString(FString::Printf(TEXT("%02.0f:00"), GI->TimeOfDay)));
}
