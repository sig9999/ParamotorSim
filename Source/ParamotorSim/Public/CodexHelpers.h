// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/SceneComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/BillboardComponent.h"

// Shared helpers for the Codex real-part Blueprints (Amaruk frame, EOS engine,
// PT130 prop, PowerSeat harness). Used by both the hangar preview builder and
// the in-world paramotor assembly.
namespace CodexHelpers
{
	/** Finds a named helper SceneComponent (SOCKET_*, AXIS_*) inside a
	 *  spawned Codex assembly Blueprint. */
	inline USceneComponent* FindHelper(AActor* Actor, FName Name)
	{
		if (!Actor) return nullptr;
		TArray<USceneComponent*> Comps;
		Actor->GetComponents(Comps);
		for (USceneComponent* C : Comps)
		{
			if (C && C->GetFName() == Name) return C;
		}
		return nullptr;
	}

	/** Helper components are reference points, not visuals — hides renderable
	 *  socket markers, axis gizmos, debug cubes, billboards. */
	inline void HideHelperVisuals(AActor* Actor)
	{
		if (!Actor) return;
		TArray<UActorComponent*> Comps;
		Actor->GetComponents(Comps);
		for (UActorComponent* C : Comps)
		{
			if (!C) continue;
			const FString N = C->GetFName().ToString();
			const bool bHelperName =
				N.StartsWith(TEXT("SOCKET_")) || N.StartsWith(TEXT("AXIS_")) ||
				N.StartsWith(TEXT("MOUNT_")) || N.Contains(TEXT("Helper")) ||
				N.Contains(TEXT("Marker")) || N.Contains(TEXT("Gizmo")) ||
				N.Contains(TEXT("Debug")) || N.Contains(TEXT("Icon"));
			if (bHelperName || C->IsA<UBillboardComponent>())
			{
				if (UPrimitiveComponent* Prim = Cast<UPrimitiveComponent>(C))
				{
					Prim->SetHiddenInGame(true);
					Prim->SetVisibility(false);
				}
			}
		}
	}
}
