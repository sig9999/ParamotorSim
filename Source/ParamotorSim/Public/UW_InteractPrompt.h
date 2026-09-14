// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UW_InteractPrompt.generated.h"

UCLASS()
class PARAMOTORSIM_API UUW_InteractPrompt : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeOnInitialized() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Prompt")
	class UTextBlock* PromptText = nullptr;
};
