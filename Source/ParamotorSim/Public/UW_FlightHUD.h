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
	UPROPERTY() UTextBlock* CompassText;
	UPROPERTY() UTextBlock* HeadingText;
	UPROPERTY() UImage* HeadingIndicator;
	UPROPERTY() UImage* WindIndicator;
	UPROPERTY() UTextBlock* WindInfoText;

	// Stats
	UPROPERTY() UTextBlock* AltitudeValue;
	UPROPERTY() UTextBlock* SpeedValue;
	UPROPERTY() UTextBlock* AirspeedValue;     // Airspeed (through-air, not ground speed)
	UPROPERTY() UTextBlock* VSpeedValue;
	UPROPERTY() UTextBlock* TempValue;
	UPROPERTY() UTextBlock* WindSpeedValue;

	// Engine panel
	UPROPERTY() UTextBlock* EngineStateText;
	UPROPERTY() UTextBlock* RPMValue;
	UPROPERTY() UProgressBar* RPMBar;
	UPROPERTY() UTextBlock* ThrottleValue;
	UPROPERTY() UProgressBar* ThrottleBar;
	UPROPERTY() UTextBlock* EngTempValue;
	UPROPERTY() UProgressBar* EngTempBar;
	UPROPERTY() UTextBlock* EGTValue;
	UPROPERTY() UProgressBar* EGTBar;
	UPROPERTY() UTextBlock* FuelValue;
	UPROPERTY() UProgressBar* FuelBar;
	UPROPERTY() UTextBlock* FuelCutText;
	UPROPERTY() UTextBlock* EngHoursValue;

	// Wing panel
	UPROPERTY() UTextBlock* WingStateText;
	UPROPERTY() UTextBlock* WingModelText;      // Wing model name (e.g. "Sig 25")
	UPROPERTY() UTextBlock* BrakeLeftValue;     // Left brake input 0-100%
	UPROPERTY() UTextBlock* BrakeRightValue;    // Right brake input 0-100%
	UPROPERTY() UTextBlock* InflationProgressValue; // Inflation progress 0-100%
	UPROPERTY() UProgressBar* InflationProgressBar;
	UPROPERTY() UTextBlock* PendulumValue;
	UPROPERTY() UTextBlock* WeightSupportValue;
	UPROPERTY() UTextBlock* GlideRatioValue;
	UPROPERTY() UTextBlock* PilotWeightValue;    // Total pilot+gear weight
	UPROPERTY() UTextBlock* TakeoffChanceValue;  // Takeoff success chance %
	UPROPERTY() UTextBlock* DebugTakeoffValue;   // Debug takeoff diagnostics
	UPROPERTY() UTextBlock* InjuryStatusValue;   // Injury status display
	UPROPERTY() UTextBlock* TrimmerStatusValue;  // Trimmer position display

	// Wind state
	float WindDirection = 45.0f;  // degrees, 0=N
	float WindSpeed = 8.0f;       // knots

	// Instruction text (context-sensitive on-screen guidance)
	UPROPERTY() UTextBlock* InstructionText;
	UPROPERTY() UBorder* InstructionPanel;
};
