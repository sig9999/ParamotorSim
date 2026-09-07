#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UW_FlightHUD.generated.h"

class UTextBlock;
class UBorder;
class UCanvasPanel;
class UVerticalBox;
class UHorizontalBox;
class UImage;
class UProgressBar;

UCLASS()
class PARAMOTORSIM_API UUW_FlightHUD : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float DeltaTime) override;

protected:
	// Compass
	UPROPERTY() UTextBlock* CompassText = nullptr;
	UPROPERTY() UTextBlock* HeadingText = nullptr;
	UPROPERTY() UImage* HeadingIndicator = nullptr;
	UPROPERTY() UImage* WindIndicator = nullptr;
	UPROPERTY() UTextBlock* WindInfoText = nullptr;

	// Stats
	UPROPERTY() UTextBlock* AltitudeValue = nullptr;
	UPROPERTY() UTextBlock* SpeedValue = nullptr;
	UPROPERTY() UTextBlock* AirspeedValue = nullptr;     // Airspeed (through-air, not ground speed)
	UPROPERTY() UTextBlock* VSpeedValue = nullptr;
	UPROPERTY() UTextBlock* TempValue = nullptr;
	UPROPERTY() UTextBlock* WindSpeedValue = nullptr;

	// Engine panel
	UPROPERTY() UTextBlock* EngineStateText = nullptr;
	UPROPERTY() UTextBlock* RPMValue = nullptr;
	UPROPERTY() UProgressBar* RPMBar = nullptr;
	UPROPERTY() UTextBlock* ThrottleValue = nullptr;
	UPROPERTY() UProgressBar* ThrottleBar = nullptr;
	UPROPERTY() UTextBlock* EngTempValue = nullptr;
	UPROPERTY() UProgressBar* EngTempBar = nullptr;
	UPROPERTY() UTextBlock* EGTValue = nullptr;
	UPROPERTY() UProgressBar* EGTBar = nullptr;
	UPROPERTY() UTextBlock* FuelValue = nullptr;
	UPROPERTY() UProgressBar* FuelBar = nullptr;
	UPROPERTY() UTextBlock* FuelCutText = nullptr;
	UPROPERTY() UTextBlock* EngHoursValue = nullptr;

	// Wing panel
	UPROPERTY() UTextBlock* WingStateText = nullptr;
	UPROPERTY() UTextBlock* WingModelText = nullptr;      // Wing model name (e.g. "Sig 25")
	UPROPERTY() UTextBlock* BrakeLeftValue = nullptr;     // Left brake input 0-100%
	UPROPERTY() UTextBlock* BrakeRightValue = nullptr;    // Right brake input 0-100%
	UPROPERTY() UTextBlock* InflationProgressValue = nullptr; // Inflation progress 0-100%
	UPROPERTY() UProgressBar* InflationProgressBar = nullptr;
	UPROPERTY() UTextBlock* PendulumValue = nullptr;
	UPROPERTY() UTextBlock* WeightSupportValue = nullptr;
	UPROPERTY() UTextBlock* GlideRatioValue = nullptr;
	UPROPERTY() UTextBlock* PilotWeightValue = nullptr;    // Total pilot+gear weight
	UPROPERTY() UTextBlock* TakeoffChanceValue = nullptr;  // Takeoff success chance %
	UPROPERTY() UTextBlock* DebugTakeoffValue = nullptr;   // Debug takeoff diagnostics
	UPROPERTY() UTextBlock* InjuryStatusValue = nullptr;   // Injury status display
	UPROPERTY() UTextBlock* TrimmerStatusValue = nullptr;  // Trimmer position display

	// Instruction text (context-sensitive on-screen guidance)
	UPROPERTY() UTextBlock* InstructionText = nullptr;
	UPROPERTY() UBorder* InstructionPanel = nullptr;
};
