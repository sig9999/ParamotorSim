// Copyright Epic Games, Inc. All Rights Reserved.

#include "UW_MainMenu.h"
#include "ParamotorCharacter.h"
#include "ParamotorGameInstance.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/PlayerController.h"
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
	if (!GameInstanceRef)
	{
		GameInstanceRef = NewObject<UParamotorGameInstance>(this);
	}

	UCanvasPanel* RootCanvas = NewObject<UCanvasPanel>(this);
	WidgetTree->RootWidget = RootCanvas;

	UTextBlock* Title = NewObject<UTextBlock>(this);
	Title->SetText(FText::FromString("PARAMOTOR SIM"));
	Title->SetJustification(ETextJustify::Center);
	Title->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.85f, 0.4f, 1.0f)));
	RootCanvas->AddChild(Title);
	if (UCanvasPanelSlot* TitleSlot = Cast<UCanvasPanelSlot>(Title->Slot))
	{
		TitleSlot->SetAnchors(FAnchors(0.5f, 0.12f, 0.5f, 0.12f));
		TitleSlot->SetOffsets(FMargin(-400.0f, -40.0f, 400.0f, 40.0f));
	}

	UButton* PlayButton = NewObject<UButton>(this);
	UTextBlock* PlayText = NewObject<UTextBlock>(this);
	PlayText->SetText(FText::FromString("PLAY"));
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

	UButton* QuitButton = NewObject<UButton>(this);
	UTextBlock* QuitText = NewObject<UTextBlock>(this);
	QuitText->SetText(FText::FromString("QUIT"));
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

	UTextBlock* WeatherTitle = NewObject<UTextBlock>(this);
	WeatherTitle->SetText(FText::FromString("WEATHER"));
	WeatherTitle->SetJustification(ETextJustify::Center);
	WeatherTitle->SetColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.9f, 1.0f, 1.0f)));
	RootCanvas->AddChild(WeatherTitle);
	if (UCanvasPanelSlot* WeatherSlot = Cast<UCanvasPanelSlot>(WeatherTitle->Slot))
	{
		WeatherSlot->SetAnchors(FAnchors(0.5f, 0.52f, 0.5f, 0.52f));
		WeatherSlot->SetOffsets(FMargin(-200.0f, -25.0f, 200.0f, 25.0f));
	}

	WindLabel = CreateSlider(RootCanvas, TEXT("Wind Speed"), 0.0f, 60.0f, GameInstanceRef->WindSpeed, 0.58f);
	GustsLabel = CreateSlider(RootCanvas, TEXT("Gusts"), 0.0f, 30.0f, GameInstanceRef->WindGusts, 0.66f);
	CloudLabel = CreateSlider(RootCanvas, TEXT("Cloud Cover"), 0.0f, 1.0f, GameInstanceRef->CloudCover, 0.74f);
	TempLabel = CreateSlider(RootCanvas, TEXT("Temperature"), -20.0f, 45.0f, GameInstanceRef->Temperature, 0.82f);
	TimeLabel = CreateSlider(RootCanvas, TEXT("Time of Day"), 0.0f, 24.0f, GameInstanceRef->TimeOfDay, 0.90f);

	WindSlider->SetValue(GameInstanceRef->WindSpeed);
	GustsSlider->SetValue(GameInstanceRef->WindGusts);
	CloudSlider->SetValue(GameInstanceRef->CloudCover);
	TempSlider->SetValue(GameInstanceRef->Temperature);
	TimeSlider->SetValue(GameInstanceRef->TimeOfDay);

	WindSlider->OnValueChanged.AddDynamic(this, &UUW_MainMenu::OnWindSpeedChanged);
	GustsSlider->OnValueChanged.AddDynamic(this, &UUW_MainMenu::OnWindGustsChanged);
	CloudSlider->OnValueChanged.AddDynamic(this, &UUW_MainMenu::OnCloudCoverChanged);
	TempSlider->OnValueChanged.AddDynamic(this, &UUW_MainMenu::OnTemperatureChanged);
	TimeSlider->OnValueChanged.AddDynamic(this, &UUW_MainMenu::OnTimeOfDayChanged);
}

UTextBlock* UUW_MainMenu::CreateSlider(UCanvasPanel* RootCanvas, const TCHAR* Name, float Min, float Max, float Value, float YPos)
{
	UTextBlock* Label = NewObject<UTextBlock>(this);
	Label->SetText(FText::FromString(FString::Printf(TEXT("%s %.1f"), Name, Value)));
	Label->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	RootCanvas->AddChild(Label);
	if (UCanvasPanelSlot* LabelSlot = Cast<UCanvasPanelSlot>(Label->Slot))
	{
		LabelSlot->SetAnchors(FAnchors(0.5f, YPos, 0.5f, YPos));
		LabelSlot->SetOffsets(FMargin(-300.0f, -25.0f, 300.0f, 25.0f));
	}

	USlider* Slider = NewObject<USlider>(this);
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
	else if (FString(Name) == TEXT("Time of Day")) TimeSlider = Slider;

	return Label;
}

void UUW_MainMenu::OnPlayClicked()
{
	UWorld* World = GetWorld();
	if (World)
	{
		UGameplayStatics::OpenLevel(World, FName("/Engine/Maps/Templates/Template_Default"));
	}
}

void UUW_MainMenu::OnQuitClicked()
{
	UKismetSystemLibrary::QuitGame(GetWorld(), nullptr, EQuitPreference::Quit, false);
}

void UUW_MainMenu::OnWindSpeedChanged(float Value)
{
	if (GameInstanceRef) GameInstanceRef->WindSpeed = Value;
	if (WindLabel) WindLabel->SetText(FText::FromString(FString::Printf(TEXT("Wind Speed %.0f"), Value)));
}

void UUW_MainMenu::OnWindGustsChanged(float Value)
{
	if (GameInstanceRef) GameInstanceRef->WindGusts = Value;
	if (GustsLabel) GustsLabel->SetText(FText::FromString(FString::Printf(TEXT("Gusts %.0f"), Value)));
}

void UUW_MainMenu::OnCloudCoverChanged(float Value)
{
	if (GameInstanceRef) GameInstanceRef->CloudCover = Value;
	if (CloudLabel) CloudLabel->SetText(FText::FromString(FString::Printf(TEXT("Cloud Cover %.2f"), Value)));
}

void UUW_MainMenu::OnTemperatureChanged(float Value)
{
	if (GameInstanceRef) GameInstanceRef->Temperature = Value;
	if (TempLabel) TempLabel->SetText(FText::FromString(FString::Printf(TEXT("Temperature %.0f"), Value)));
}

void UUW_MainMenu::OnTimeOfDayChanged(float Value)
{
	if (GameInstanceRef) GameInstanceRef->TimeOfDay = Value;
	if (TimeLabel) TimeLabel->SetText(FText::FromString(FString::Printf(TEXT("Time of Day %.0f"), Value)));
}
