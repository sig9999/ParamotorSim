// Copyright Epic Games, Inc. All Rights Reserved.

#include "UW_MainMenu.h"
#include "ParamotorGameInstance.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"

void UUW_MainMenu::NativeConstruct()
{
	Super::NativeConstruct();

	GameInstanceRef = Cast<UParamotorGameInstance>(UGameplayStatics::GetGameInstance(this));
	if (!IsValid(GameInstanceRef))
	{
		UE_LOG(LogTemp, Warning, TEXT("MainMenu: No ParamotorGameInstance found; weather values will use defaults."));
	}

	UCanvasPanel* RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>();
	WidgetTree->RootWidget = RootCanvas;

	UTextBlock* Title = WidgetTree->ConstructWidget<UTextBlock>();
	Title->SetText(FText::FromString(TEXT("PARAMOTOR SIM")));
	Title->SetJustification(ETextJustify::Center);
	Title->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.85f, 0.4f, 1.0f)));
	RootCanvas->AddChild(Title);
	if (UCanvasPanelSlot* TitleSlot = Cast<UCanvasPanelSlot>(Title->Slot))
	{
		TitleSlot->SetAnchors(FAnchors(0.5f, 0.12f, 0.5f, 0.12f));
		TitleSlot->SetOffsets(FMargin(-400.0f, -40.0f, 400.0f, 40.0f));
	}

	UButton* PlayButton = WidgetTree->ConstructWidget<UButton>();
	UTextBlock* PlayText = WidgetTree->ConstructWidget<UTextBlock>();
	PlayText->SetText(FText::FromString(TEXT("PLAY")));
	PlayText->SetJustification(ETextJustify::Center);
	PlayText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	PlayButton->AddChild(PlayText);
	if (UButtonSlot* TextSlot = Cast<UButtonSlot>(PlayText->Slot))
	{
		TextSlot->SetPadding(FMargin(0.0f));
	}
	PlayButton->OnClicked.AddDynamic(this, &UUW_MainMenu::OnPlayClicked);
	RootCanvas->AddChild(PlayButton);
	if (UCanvasPanelSlot* PlaySlot = Cast<UCanvasPanelSlot>(PlayButton->Slot))
	{
		PlaySlot->SetAnchors(FAnchors(0.5f, 0.3f, 0.5f, 0.3f));
		PlaySlot->SetOffsets(FMargin(-150.0f, -25.0f, 150.0f, 25.0f));
	}

	UButton* QuitButton = WidgetTree->ConstructWidget<UButton>();
	UTextBlock* QuitText = WidgetTree->ConstructWidget<UTextBlock>();
	QuitText->SetText(FText::FromString(TEXT("QUIT")));
	QuitText->SetJustification(ETextJustify::Center);
	QuitText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	QuitButton->AddChild(QuitText);
	if (UButtonSlot* TextSlot2 = Cast<UButtonSlot>(QuitText->Slot))
	{
		TextSlot2->SetPadding(FMargin(0.0f));
	}
	QuitButton->OnClicked.AddDynamic(this, &UUW_MainMenu::OnQuitClicked);
	RootCanvas->AddChild(QuitButton);
	if (UCanvasPanelSlot* QuitSlot = Cast<UCanvasPanelSlot>(QuitButton->Slot))
	{
		QuitSlot->SetAnchors(FAnchors(0.5f, 0.4f, 0.5f, 0.4f));
		QuitSlot->SetOffsets(FMargin(-150.0f, -25.0f, 150.0f, 25.0f));
	}

	UTextBlock* WeatherTitle = WidgetTree->ConstructWidget<UTextBlock>();
	WeatherTitle->SetText(FText::FromString(TEXT("WEATHER")));
	WeatherTitle->SetJustification(ETextJustify::Center);
	WeatherTitle->SetColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.9f, 1.0f, 1.0f)));
	RootCanvas->AddChild(WeatherTitle);
	if (UCanvasPanelSlot* WeatherSlot = Cast<UCanvasPanelSlot>(WeatherTitle->Slot))
	{
		WeatherSlot->SetAnchors(FAnchors(0.5f, 0.52f, 0.5f, 0.52f));
		WeatherSlot->SetOffsets(FMargin(-200.0f, -25.0f, 200.0f, 25.0f));
	}

	float WindSpeed   = IsValid(GameInstanceRef) ? GameInstanceRef->WindSpeed   : 0.0f;
	float WindGusts   = IsValid(GameInstanceRef) ? GameInstanceRef->WindGusts   : 0.0f;
	float CloudCover  = IsValid(GameInstanceRef) ? GameInstanceRef->CloudCover  : 0.25f;
	float Temperature = IsValid(GameInstanceRef) ? GameInstanceRef->Temperature : 20.0f;
	float Humidity    = IsValid(GameInstanceRef) ? GameInstanceRef->Humidity    : 60.0f;
	float TimeOfDay   = IsValid(GameInstanceRef) ? GameInstanceRef->TimeOfDay   : 12.0f;

	WindLabel     = CreateSlider(RootCanvas, TEXT("Wind Speed"),  0.0f, 60.0f, WindSpeed,   0.56f);
	GustsLabel    = CreateSlider(RootCanvas, TEXT("Gusts"),       0.0f, 30.0f, WindGusts,   0.62f);
	CloudLabel    = CreateSlider(RootCanvas, TEXT("Cloud Cover"), 0.0f, 1.0f,  CloudCover,  0.68f);
	TempLabel     = CreateSlider(RootCanvas, TEXT("Temperature"), -20.0f, 45.0f, Temperature, 0.74f);
	HumidityLabel = CreateSlider(RootCanvas, TEXT("Humidity"),    0.0f, 100.0f, Humidity,    0.80f);
	TimeLabel     = CreateSlider(RootCanvas, TEXT("Time of Day"), 0.0f, 24.0f,  TimeOfDay,   0.86f);

	WindSlider->SetValue(WindSpeed);
	GustsSlider->SetValue(WindGusts);
	CloudSlider->SetValue(CloudCover);
	TempSlider->SetValue(Temperature);
	HumiditySlider->SetValue(Humidity);
	TimeSlider->SetValue(TimeOfDay);

	WindSlider->OnValueChanged.AddDynamic(this, &UUW_MainMenu::OnWindSpeedChanged);
	GustsSlider->OnValueChanged.AddDynamic(this, &UUW_MainMenu::OnWindGustsChanged);
	CloudSlider->OnValueChanged.AddDynamic(this, &UUW_MainMenu::OnCloudCoverChanged);
	TempSlider->OnValueChanged.AddDynamic(this, &UUW_MainMenu::OnTemperatureChanged);
	HumiditySlider->OnValueChanged.AddDynamic(this, &UUW_MainMenu::OnHumidityChanged);
	TimeSlider->OnValueChanged.AddDynamic(this, &UUW_MainMenu::OnTimeOfDayChanged);
}

UTextBlock* UUW_MainMenu::CreateSlider(UCanvasPanel* RootCanvas, const TCHAR* Name, float Min, float Max, float Value, float YPos)
{
	UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>();
	Label->SetText(FText::FromString(FString::Printf(TEXT("%s %.1f"), Name, Value)));
	Label->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	RootCanvas->AddChild(Label);
	if (UCanvasPanelSlot* LabelSlot = Cast<UCanvasPanelSlot>(Label->Slot))
	{
		LabelSlot->SetAnchors(FAnchors(0.5f, YPos, 0.5f, YPos));
		LabelSlot->SetOffsets(FMargin(-300.0f, -25.0f, 300.0f, 25.0f));
	}

	USlider* Slider = WidgetTree->ConstructWidget<USlider>();
	Slider->SetMinValue(Min);
	Slider->SetMaxValue(Max);
	Slider->SetValue(Value);
	RootCanvas->AddChild(Slider);
	if (UCanvasPanelSlot* SliderSlot = Cast<UCanvasPanelSlot>(Slider->Slot))
	{
		SliderSlot->SetAnchors(FAnchors(0.5f, YPos + 0.04f, 0.5f, YPos + 0.04f));
		SliderSlot->SetOffsets(FMargin(-300.0f, -15.0f, 300.0f, 15.0f));
	}

	if (FString(Name) == TEXT("Wind Speed")) WindSlider = Slider;
	else if (FString(Name) == TEXT("Gusts")) GustsSlider = Slider;
	else if (FString(Name) == TEXT("Cloud Cover")) CloudSlider = Slider;
	else if (FString(Name) == TEXT("Temperature")) TempSlider = Slider;
	else if (FString(Name) == TEXT("Humidity")) HumiditySlider = Slider;
	else if (FString(Name) == TEXT("Time of Day")) TimeSlider = Slider;

	return Label;
}

void UUW_MainMenu::OnPlayClicked()
{
	UWorld* World = GetWorld();
	if (IsValid(World))
	{
		UGameplayStatics::OpenLevel(World, FName(TEXT("L_Westerly")));
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

void UUW_MainMenu::OnWindSpeedChanged(float Value)
{
	if (IsValid(GameInstanceRef)) GameInstanceRef->WindSpeed = Value;
	if (IsValid(WindLabel)) WindLabel->SetText(FText::FromString(FString::Printf(TEXT("Wind Speed %.0f"), Value)));
}

void UUW_MainMenu::OnWindGustsChanged(float Value)
{
	if (IsValid(GameInstanceRef)) GameInstanceRef->WindGusts = Value;
	if (IsValid(GustsLabel)) GustsLabel->SetText(FText::FromString(FString::Printf(TEXT("Gusts %.0f"), Value)));
}

void UUW_MainMenu::OnCloudCoverChanged(float Value)
{
	if (IsValid(GameInstanceRef)) GameInstanceRef->CloudCover = Value;
	if (IsValid(CloudLabel)) CloudLabel->SetText(FText::FromString(FString::Printf(TEXT("Cloud Cover %.2f"), Value)));
}

void UUW_MainMenu::OnTemperatureChanged(float Value)
{
	if (IsValid(GameInstanceRef)) GameInstanceRef->Temperature = Value;
	if (IsValid(TempLabel)) TempLabel->SetText(FText::FromString(FString::Printf(TEXT("Temperature %.0f"), Value)));
}

void UUW_MainMenu::OnHumidityChanged(float Value)
{
	if (IsValid(GameInstanceRef)) GameInstanceRef->Humidity = Value;
	if (IsValid(HumidityLabel)) HumidityLabel->SetText(FText::FromString(FString::Printf(TEXT("Humidity %.0f%%"), Value)));
}

void UUW_MainMenu::OnTimeOfDayChanged(float Value)
{
	if (IsValid(GameInstanceRef)) GameInstanceRef->TimeOfDay = Value;
	if (IsValid(TimeLabel)) TimeLabel->SetText(FText::FromString(FString::Printf(TEXT("Time of Day %.0f"), Value)));
}
