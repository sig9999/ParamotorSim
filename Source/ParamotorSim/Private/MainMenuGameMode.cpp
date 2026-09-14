// Copyright Epic Games, Inc. All Rights Reserved.

#include "MainMenuGameMode.h"
#include "ParamotorLogging.h"
#include "UW_MainMenu.h"
#include "Blueprint/WidgetBlueprintLibrary.h"

AMainMenuGameMode::AMainMenuGameMode()
{
	DefaultPawnClass = nullptr;
	PlayerControllerClass = APlayerController::StaticClass();
}

void AMainMenuGameMode::BeginPlay()
{
	Super::BeginPlay();
	FTimerHandle MenuTimer;
	GetWorldTimerManager().SetTimer(MenuTimer, this, &AMainMenuGameMode::ShowMainMenu, 0.5f, false);
}

void AMainMenuGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);
}

void AMainMenuGameMode::ShowMainMenu()
{
	UE_LOG(LogParamotor, Log, TEXT("MainMenuGameMode: ShowMainMenu"));
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!PC)
	{
		UE_LOG(LogParamotor, Log, TEXT("MainMenuGameMode: no PC, retrying"));
		FTimerHandle Timer;
		GetWorldTimerManager().SetTimer(Timer, this, &AMainMenuGameMode::ShowMainMenu, 0.2f, false);
		return;
	}

	// Prevent double menu — check if a main menu widget already exists in the viewport
	TArray<UUserWidget*> Widgets;
	UWidgetBlueprintLibrary::GetAllWidgetsOfClass(PC, Widgets, UUW_MainMenu::StaticClass(), false);
	if (Widgets.Num() > 0) return;  // Menu already exists

	PC->SetShowMouseCursor(true);
	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(nullptr);
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	PC->SetInputMode(InputMode);

	if (UUW_MainMenu* Menu = CreateWidget<UUW_MainMenu>(PC, UUW_MainMenu::StaticClass()))
	{
		UE_LOG(LogParamotor, Log, TEXT("MainMenuGameMode: adding menu %s"), *Menu->GetName());
		Menu->SetOwningPlayer(PC);
		Menu->AddToViewport(10);
		Menu->SetVisibility(ESlateVisibility::Visible);
		UE_LOG(LogParamotor, Log, TEXT("MainMenuGameMode: menu in viewport=%s"), Menu->IsInViewport() ? TEXT("true") : TEXT("false"));
	}
	else
	{
		UE_LOG(LogParamotor, Log, TEXT("MainMenuGameMode: CreateWidget failed"));
	}
}
