// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CodexParts.h"
#include "ParamotorBuilder.generated.h"

UCLASS()
class PARAMOTORSIM_API AParamotorBuilder : public AActor
{
	GENERATED_BODY()

public:
	AParamotorBuilder();

	virtual void BeginPlay() override;

	// Rebuild the preview from the current GameInstance build.
	UFUNCTION(BlueprintCallable, Category = "Hangar")
	void BuildFromCurrentConfig();

	// Load/refresh one part category (Frame, Engine, Prop, Harness, Wing).
	UFUNCTION(BlueprintCallable, Category = "Hangar")
	void SetPart(EHangarPart Part);

	// Toggle visibility of a part group (useful for step-by-step assembly).
	UFUNCTION(BlueprintCallable, Category = "Hangar")
	void SetPartVisible(EHangarPart Part, bool bVisible);

	// Show/hide the seated pilot figure on the harness.
	UFUNCTION(BlueprintCallable, Category = "Hangar")
	void SetPilotVisible(bool bVisible);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hangar")
	USceneComponent* RootScene = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hangar")
	USceneComponent* FrameSlot = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hangar")
	USceneComponent* EngineSlot = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hangar")
	USceneComponent* PropSlot = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hangar")
	USceneComponent* HarnessSlot = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hangar")
	USceneComponent* WingSlot = nullptr;

	// Real assembled part BPs (Kangook frame, EOS engine, EOS prop, Dudek
	// harness) — indexed by (int32)EHangarPart. Wing stays a static mesh.
	UPROPERTY()
	TArray<AActor*> PartActors;
	UPROPERTY()
	TArray<UStaticMeshComponent*> WingMeshes;

	// Seated pilot mannequin + the harness's loose dangling straps — toggled by
	// the hangar menu (PILOT: SEATED hides the straps, EMPTY shows them).
	UPROPERTY()
	TArray<UPrimitiveComponent*> HarnessStrapComps;
	// The harness's seat-board component + its authored transform — the board
	// folds up against the backrest when the harness is displayed empty.
	UPROPERTY()
	USceneComponent* HarnessSeatComp = nullptr;
	FTransform HarnessSeatTransform;
	// No pilot mannequin — the showroom harness is always displayed empty
	// (straps out, seat folded).
	bool bPilotSeated = false;

	USceneComponent* GetSlot(EHangarPart Part) const;
	AActor* GetPartActor(EHangarPart Part) const;
	void ClearPart(EHangarPart Part);
};
