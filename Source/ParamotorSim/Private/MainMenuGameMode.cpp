// Copyright Epic Games, Inc. All Rights Reserved.

#include "MainMenuGameMode.h"
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
	FTimerHandle Timer;
	GetWorldTimerManager().SetTimer(Timer, this, &AMainMenuGameMode::ShowMainMenu, 0.1f, false);
}

void AMainMenuGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);
	ShowMainMenu();
}

void AMainMenuGameMode::ShowMainMenu()
{
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!PC)
	{
		FTimerHandle Timer;
		GetWorldTimerManager().SetTimer(Timer, this, &AMainMenuGameMode::ShowMainMenu, 0.2f, false);
		return;
	}

	// Prevent double menu — check if a main menu widget already exists in the viewport
	if (PC->GetHUD())
	{
		TArray<UUserWidget*> Widgets;
		UWidgetBlueprintLibrary::GetAllWidgetsOfClass(PC, Widgets, UUW_MainMenu::StaticClass(), false);
		if (Widgets.Num() > 0) return;  // Menu already exists
	}

	PC->SetShowMouseCursor(true);
	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(nullptr);
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	PC->SetInputMode(InputMode);

	if (UUW_MainMenu* Menu = CreateWidget<UUW_MainMenu>(GetWorld(), UUW_MainMenu::StaticClass()))
	{
		Menu->AddToViewport();
	}
}
