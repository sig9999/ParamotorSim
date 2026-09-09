#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UW_FlightHUD.generated.h"

class UTextBlock;
class UBorder;
class UCanvasPanel;
class UVerticalBox;
class UVerticalBoxSlot;
class UHorizontalBox;
class UProgressBar;
class UWidget;
class APlayerController;
class AParamotorPlayerController;
class AParamotorActor;
class AWingActor;
class APawn;
class AParamotorCharacter;

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
	UPROPERTY() UTextBlock* WindInfoText = nullptr;
	UPROPERTY() UTextBlock* ConditionsText = nullptr;   // Flyability + OD risk
	UPROPERTY() UTextBlock* CloudbaseText = nullptr;    // Cloudbase altitude

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

	// ============================================================
	// UI construction helpers (extracted from NativeOnInitialized lambdas)
	// ============================================================
	UTextBlock* MakeText(const FString& Name, const FString& Text, int32 Size, FSlateColor Color);
	UVerticalBoxSlot* AddToVBox(UVerticalBox* VBox, UWidget* Widget, float PadBottom = 0.0f);
	UBorder* MakePanel(const FString& Name);
	UTextBlock* MakeHeader(const FString& Name, const FString& Label);
	void AddStatRow(UVerticalBox* Parent, const FString& Label, UTextBlock*& ValuePtr, int32 ValSize = 15);
	void AddSeparator(UVerticalBox* Parent);
	void AddGaugeRow(UVerticalBox* Parent, const FString& Label, UTextBlock*& ValuePtr, UProgressBar*& BarPtr, FLinearColor BarColor);

	// ============================================================
	// UI builders (split from NativeOnInitialized)
	// ============================================================
	void BuildFlightPanel(UCanvasPanel* Root);
	void BuildEnginePanel(UCanvasPanel* Root);
	void BuildWingPanel(UCanvasPanel* Root);
	void BuildInstructionBar(UCanvasPanel* Root);
	void BuildCrosshair(UCanvasPanel* Root);

	// ============================================================
	// Tick update methods (split from NativeTick)
	// ============================================================
	void UpdateCompass(APlayerController* PC);
	void UpdateWindInfo(APlayerController* PC, AParamotorPlayerController* ParamotorPC);
	void UpdateFlightStats(APawn* Pawn, AParamotorPlayerController* ParamotorPC);
	void UpdateEngineData(AParamotorActor* Paramotor);
	void UpdateWingData(APlayerController* PC, AParamotorPlayerController* ParamotorPC, AWingActor* Wing);
	void UpdateInjuryStatus();
	void UpdateTrimmerStatus(AWingActor* Wing);
	void UpdateInstructions(APlayerController* PC, AParamotorActor* Paramotor, AParamotorPlayerController* ParamotorPC);

private:
	// Cached cardinal conversion helper
	static FString HeadingToCardinal(float HeadingDeg);

	// === UpdateWingData split helpers ===
	void UpdateWingStateDisplay(AWingActor* Wing);
	void UpdateWingInflationProgress(AWingActor* Wing);
	void UpdateWingFlightMetrics(AWingActor* Wing, APlayerController* PC);
	void UpdateWingTakeoffInfo(AWingActor* Wing);
	void ClearWingData();
};
