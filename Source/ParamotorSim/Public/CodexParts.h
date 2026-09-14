// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CodexParts.generated.h"

/**
 * Part slots in a hangar build / in-world paramotor assembly.
 * Extend this enum (and the tables below) when adding new part kinds —
 * trike wheels, quad motors, additional fuel tanks, etc.
 */
UENUM(BlueprintType)
enum class EHangarPart : uint8
{
	Frame,
	Engine,
	Prop,
	Harness,
	Wing
};

/**
 * Canonical Codex part registry — the single source of truth for which real
 * manufacturer Blueprint fills each slot, where it mounts in frame space,
 * and which authored helper components (SOCKET_/AXIS_/MOUNT_) to prefer.
 *
 * Used by both AParamotorBuilder (hangar preview) and AParamotorActor
 * (in-world rig) so they can never drift apart.
 */
namespace CodexParts
{
	/** The real manufacturer Blueprint for a slot. nullptr = not a Codex BP
	 *  (the wing is a plain static mesh). */
	inline const TCHAR* BPPath(EHangarPart Part)
	{
		switch (Part)
		{
			case EHangarPart::Frame:
				return TEXT("/Game/Kangook_Amaruk140_Standard/BP_Kangook_Amaruk140_Standard.BP_Kangook_Amaruk140_Standard_C");
			case EHangarPart::Engine:
				return TEXT("/Game/EOS100_ICI_Standard/BP_EOS100_ICI_Standard.BP_EOS100_ICI_Standard_C");
			case EHangarPart::Prop:
				return TEXT("/Game/EOS_PT130/BP_EOS_PT130.BP_EOS_PT130_C");
			case EHangarPart::Harness:
				return TEXT("/Game/Dudek_PowerSeatDP_Standard_SM/BP_Dudek_PowerSeatDP_Standard_SM.BP_Dudek_PowerSeatDP_Standard_SM_C");
			default:
				return nullptr;
		}
	}

	/** Frame-space mount transform — positions converted from the Codex
	 *  Blender manifests: (X,Y,Z) m → (100X,-100Y,100Z) cm. */
	inline FTransform RelTransform(EHangarPart Part)
	{
		switch (Part)
		{
			case EHangarPart::Frame:
				return FTransform::Identity;

			case EHangarPart::Engine:
			{
				// EngineWorld = FrameEngineSocket * inverse(EngineFrameMountLocal)
				// SOCKET_Engine_EOS100 (frame space): (-0.0703, +0.096, +0.689)m, euler (0, -3°, 180°)
				// X pushed ~9cm aft (-7.03 → -16.0): the authored socket put the
				// prop disc on the cage-ring plane — blades clipped the netting.
				const FTransform SocketRel(FRotator(3.0f, 180.0f, 0.0f), FVector(-16.0f, -9.6f, 68.9f));
				// SOCKET_FrameMount (engine space): (-0.10, 0, 0)m
				const FTransform MountRel(FRotator::ZeroRotator, FVector(-10.0f, 0.0f, 0.0f));
				return SocketRel * MountRel.Inverse();
			}

			case EHangarPart::Prop:
			{
				// Fallback when the engine socket isn't found:
				// engine rel * SOCKET_Propeller (0.11, 0.096, 0)m → (11, -9.6, 0)cm
				const FTransform EngineRel = RelTransform(EHangarPart::Engine);
				return EngineRel * FTransform(FRotator::ZeroRotator, FVector(11.0f, -9.6f, 0.0f));
			}

			case EHangarPart::Harness:
			{
				// Codex-validated fixture: harness at (0.3345, 0, 0.352)m, identity
				// rotation — seat forward of the cage, carabiners at the pivots.
				return FTransform(FRotator::ZeroRotator, FVector(33.45f, 0.0f, 35.2f));
			}
		}
		return FTransform::Identity;
	}

	/** Authored helper-component names inside the part BPs. */
	inline FName PropSocket()      { return TEXT("SOCKET_Propeller"); }   // on the engine BP
	inline FName EngineAudioSocket(){ return TEXT("SOCKET_EngineAudio"); } // on the engine BP
	inline FName PropAxis()        { return TEXT("AXIS_Prop"); }          // on the prop BP
	inline FName PropHub()         { return TEXT("PropHub"); }            // bare flange on the engine BP
}
