// Copyright Epic Games, Inc. All Rights Reserved.

#include "ParamotorBuilder.h"
#include "ParamotorLogging.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "CodexHelpers.h"
#include "Components/SceneComponent.h"
#include "Materials/MaterialInterface.h"
#include "ParamotorGameInstance.h"
#include "Kismet/GameplayStatics.h"

AParamotorBuilder::AParamotorBuilder()
{
	PrimaryActorTick.bCanEverTick = false;

	RootScene = CreateDefaultSubobject<USceneComponent>(TEXT("RootScene"));
	SetRootComponent(RootScene);

	FrameSlot = CreateDefaultSubobject<USceneComponent>(TEXT("FrameSlot"));
	FrameSlot->SetupAttachment(RootScene);

	EngineSlot = CreateDefaultSubobject<USceneComponent>(TEXT("EngineSlot"));
	EngineSlot->SetupAttachment(RootScene);

	PropSlot = CreateDefaultSubobject<USceneComponent>(TEXT("PropSlot"));
	PropSlot->SetupAttachment(RootScene);

	HarnessSlot = CreateDefaultSubobject<USceneComponent>(TEXT("HarnessSlot"));
	HarnessSlot->SetupAttachment(RootScene);

	WingSlot = CreateDefaultSubobject<USceneComponent>(TEXT("WingSlot"));
	WingSlot->SetupAttachment(RootScene);

	PartActors.SetNum((int32)EHangarPart::Wing + 1);
}

void AParamotorBuilder::BeginPlay()
{
	Super::BeginPlay();
	BuildFromCurrentConfig();
}

void AParamotorBuilder::BuildFromCurrentConfig()
{
	SetPart(EHangarPart::Frame);
	SetPart(EHangarPart::Engine);
	SetPart(EHangarPart::Prop);
	SetPart(EHangarPart::Harness);
	SetPart(EHangarPart::Wing);
}

void AParamotorBuilder::SetPart(EHangarPart Part)
{
	ClearPart(Part);

	USceneComponent* Slot = GetSlot(Part);
	if (!Slot) return;

	// Wing is still a plain static mesh.
	if (Part == EHangarPart::Wing)
	{
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr,
			TEXT("/Game/Meshes/SM_ParamotorWing.SM_ParamotorWing"));
		if (!Mesh)
		{
			UE_LOG(LogParamotor, Log, TEXT("ParamotorBuilder: failed to load wing mesh"));
			return;
		}
		UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(this, TEXT("WingMesh"));
		Comp->SetupAttachment(Slot);
		Comp->SetStaticMesh(Mesh);
		Comp->SetMobility(EComponentMobility::Movable);
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Comp->SetRelativeTransform(FTransform::Identity);
		Comp->RegisterComponent();
		Comp->SetVisibility(false);
		Comp->SetHiddenInGame(true);
		WingMeshes.Add(Comp);
		return;
	}

	// Real assembled part Blueprints (Codex assets).
	const TCHAR* BPPath = CodexParts::BPPath(Part);
	if (!BPPath) return;

	UClass* PartClass = LoadObject<UClass>(nullptr, BPPath);
	if (!PartClass || !GetWorld())
	{
		UE_LOG(LogParamotor, Log, TEXT("ParamotorBuilder: part BP not found: %s"), BPPath);
		return;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* PartActor = GetWorld()->SpawnActor<AActor>(PartClass, FTransform::Identity, Params);
	if (!PartActor) return;
	PartActors[(int32)Part] = PartActor;
	PartActor->SetActorEnableCollision(false);
	CodexHelpers::HideHelperVisuals(PartActor);

	// Everything mounts in frame space (the frame BP sits at FrameSlot
	// identity). The prop prefers the engine's SOCKET_Propeller helper for a
	// live attachment; the fallback transform lands on the same point.
	USceneComponent* AttachParent = Slot;
	if (Part == EHangarPart::Prop)
	{
		if (USceneComponent* PropSocket = CodexHelpers::FindHelper(GetPartActor(EHangarPart::Engine), CodexParts::PropSocket()))
		{
			AttachParent = PropSocket;
		}
	}

	PartActor->AttachToComponent(AttachParent, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	if (AttachParent == Slot)
	{
		if (USceneComponent* Root = PartActor->GetRootComponent())
		{
			Root->SetRelativeTransform(CodexParts::RelTransform(Part));
		}
	}

	// Re-spawning the engine destroyed the socket the prop was attached to —
	// re-attach it to the fresh engine's SOCKET_Propeller.
	if (Part == EHangarPart::Engine)
	{
		if (AActor* PropActor = GetPartActor(EHangarPart::Prop))
		{
			if (USceneComponent* PropSocket = CodexHelpers::FindHelper(PartActor, CodexParts::PropSocket()))
			{
				PropActor->AttachToComponent(PropSocket, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
			}
		}
	}

	// The prop BP carries its own hub — hide the engine's bare prop-mount
	// flange so the two hubs don't overlap at the prop axis. Runs for both
	// parts so re-equipping the engine after the prop still dedupes.
	if (Part == EHangarPart::Prop || Part == EHangarPart::Engine)
	{
		const bool bPropEquipped = GetPartActor(EHangarPart::Prop) != nullptr;
		if (AActor* EngineActor = GetPartActor(EHangarPart::Engine))
		{
			TArray<UActorComponent*> EngineComps;
			EngineActor->GetComponents(EngineComps);
			for (UActorComponent* C : EngineComps)
			{
				if (C && C->GetFName().ToString().Contains(CodexParts::PropHub().ToString()))
				{
					if (UPrimitiveComponent* Prim = Cast<UPrimitiveComponent>(C))
					{
						Prim->SetHiddenInGame(bPropEquipped);
						Prim->SetVisibility(!bPropEquipped);
					}
				}
			}
		}
	}

	// Seated-pilot toggle support on the harness:
	//  - collect the loose dangling strap meshes (they hang below the seat and
	//    clip the floor — a pilot would wear them, so they hide when seated)
	//  - spawn a primitive mannequin seated in the harness
	if (Part == EHangarPart::Harness)
	{
		TArray<UActorComponent*> HComps;
		PartActor->GetComponents(HComps);
		for (UActorComponent* C : HComps)
		{
			const FString N = C ? C->GetFName().ToString() : TEXT("");
			// Straps dangle loose; Hardware/Pulleys are the carabiners and
			// speed-system blocks that float at the riser hang-points when
			// nothing is connected — a seated pilot "wears" all of it.
			if (N.Contains(TEXT("Strap")) || N.Contains(TEXT("HangLoops")) ||
				N.Contains(TEXT("Hardware")) || N.Contains(TEXT("Pulleys")))
			{
				if (UPrimitiveComponent* Prim = Cast<UPrimitiveComponent>(C))
				{
					HarnessStrapComps.Add(Prim);
					Prim->SetHiddenInGame(bPilotSeated);
					Prim->SetVisibility(!bPilotSeated);
				}
			}
			// The seat board is its own component — fold it up against the
			// backrest when the harness is displayed empty.
			if (N.EndsWith(TEXT("_Seat")))
			{
				if (USceneComponent* SC = Cast<USceneComponent>(C))
				{
					HarnessSeatComp = SC;
					HarnessSeatTransform = SC->GetRelativeTransform();
				}
			}
		}

		// Harness spawned while empty — fold the seat board immediately.
		if (!bPilotSeated && HarnessSeatComp)
		{
			const FTransform Hinge(FRotator::ZeroRotator, FVector(18.0f, 0.0f, 9.0f));
			const FTransform Fold(FRotator(75.0f, 0.0f, 0.0f), FVector::ZeroVector);
			HarnessSeatComp->SetRelativeTransform(Hinge * Fold * Hinge.Inverse() * HarnessSeatTransform);
		}
	}

	PartActor->SetActorHiddenInGame(true); // shown via SetPartVisible
}

void AParamotorBuilder::SetPilotVisible(bool bVisible)
{
	bPilotSeated = bVisible;
	// Seated pilot wears the harness — hide the loose straps; empty harness
	// shows them dangling.
	for (UPrimitiveComponent* Comp : HarnessStrapComps)
	{
		if (Comp)
		{
			Comp->SetVisibility(!bVisible);
			Comp->SetHiddenInGame(bVisible);
			Comp->MarkRenderStateDirty();
		}
	}
	// Empty harness folds the seat board up against the backrest — hinged at
	// its front edge in harness space (~18cm forward, 9cm up).
	if (HarnessSeatComp)
	{
		if (bVisible)
		{
			HarnessSeatComp->SetRelativeTransform(HarnessSeatTransform);
		}
		else
		{
			const FTransform Hinge(FRotator::ZeroRotator, FVector(18.0f, 0.0f, 9.0f));
			const FTransform Fold(FRotator(75.0f, 0.0f, 0.0f), FVector::ZeroVector);
			HarnessSeatComp->SetRelativeTransform(Hinge * Fold * Hinge.Inverse() * HarnessSeatTransform);
		}
		HarnessSeatComp->MarkRenderStateDirty();
	}
}

void AParamotorBuilder::SetPartVisible(EHangarPart Part, bool bVisible)
{
	if (AActor* A = GetPartActor(Part))
	{
		A->SetActorHiddenInGame(!bVisible);
	}
	// When the prop is hidden, restore the engine's bare prop-mount flange —
	// it's only suppressed while a prop is on the shaft.
	if (Part == EHangarPart::Prop)
	{
		if (AActor* EngineActor = GetPartActor(EHangarPart::Engine))
		{
			TArray<UActorComponent*> EngineComps;
			EngineActor->GetComponents(EngineComps);
			for (UActorComponent* C : EngineComps)
			{
				if (C && C->GetFName().ToString().Contains(CodexParts::PropHub().ToString()))
				{
					if (UPrimitiveComponent* Prim = Cast<UPrimitiveComponent>(C))
					{
						Prim->SetHiddenInGame(bVisible);
						Prim->SetVisibility(!bVisible);
					}
				}
			}
		}
	}
	if (Part == EHangarPart::Wing)
	{
		for (UStaticMeshComponent* Comp : WingMeshes)
		{
			if (Comp)
			{
				Comp->SetVisibility(bVisible);
				Comp->SetHiddenInGame(!bVisible);
				Comp->MarkRenderStateDirty();
			}
		}
	}
}

USceneComponent* AParamotorBuilder::GetSlot(EHangarPart Part) const
{
	switch (Part)
	{
		case EHangarPart::Frame:   return FrameSlot;
		case EHangarPart::Engine:  return EngineSlot;
		case EHangarPart::Prop:    return PropSlot;
		case EHangarPart::Harness: return HarnessSlot;
		case EHangarPart::Wing:    return WingSlot;
	}
	return nullptr;
}

AActor* AParamotorBuilder::GetPartActor(EHangarPart Part) const
{
	const int32 i = (int32)Part;
	return PartActors.IsValidIndex(i) ? PartActors[i] : nullptr;
}

void AParamotorBuilder::ClearPart(EHangarPart Part)
{
	if (AActor* A = GetPartActor(Part))
	{
		A->Destroy();
		PartActors[(int32)Part] = nullptr;
	}
	if (Part == EHangarPart::Wing)
	{
		for (UStaticMeshComponent* Comp : WingMeshes)
		{
			if (Comp)
			{
				Comp->DestroyComponent();
			}
		}
		WingMeshes.Empty();
	}
	if (Part == EHangarPart::Harness)
	{
		HarnessStrapComps.Empty();
		HarnessSeatComp = nullptr;
	}
}
