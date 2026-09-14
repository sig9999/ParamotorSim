// Copyright Epic Games, Inc. All Rights Reserved.

// ParamotorActor_Codex.cpp - Codex real-part assembly
// Spawns the real manufacturer Blueprints (Amaruk frame, EOS engine,
// PT130 prop, PowerSeat harness) onto the paramotor rig, hides helper
// markers, mounts audio at the BP's authored sockets, and applies
// the equipped hangar build's engine specs / rig mass.

#include "ParamotorActor.h"
#include "ParamotorLogging.h"
#include "ParamotorGameInstance.h"
#include "CodexHelpers.h"
#include "CodexParts.h"
#include "Components/AudioComponent.h"

void AParamotorActor::BuildCodexVisuals()
{
	if (!GetWorld()) return;

	// Canonical part registry — paths + frame-space transforms shared with
	// the hangar builder.
	const EHangarPart Parts[] = {
		EHangarPart::Frame, EHangarPart::Engine, EHangarPart::Prop, EHangarPart::Harness
	};

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	for (int32 i = 0; i < 4; ++i)
	{
		const TCHAR* Path = CodexParts::BPPath(Parts[i]);
		UClass* PartClass = Path ? LoadObject<UClass>(nullptr, Path) : nullptr;
		if (!PartClass)
		{
			UE_LOG(LogParamotor, Log, TEXT("ParamotorActor: Codex part BP missing: %s"), Path ? Path : TEXT("none"));
			continue;
		}
		AActor* Part = GetWorld()->SpawnActor<AActor>(PartClass, FTransform::Identity, Params);
		if (!Part) continue;
		CodexPartActors.Add(Part);
		Part->SetActorEnableCollision(false);
		CodexHelpers::HideHelperVisuals(Part);

		// Two-layer mount: harness welds to the body (Root); frame, engine,
		// and prop hang under HangPivot so the motor can swing on the back.
		USceneComponent* AttachParent = (i == 3) ? Root : HangPivot;
		if (i == 2 && CodexPartActors.IsValidIndex(1))
		{
			if (USceneComponent* PropSocket = CodexHelpers::FindHelper(CodexPartActors[1], CodexParts::PropSocket()))
			{
				AttachParent = PropSocket;
			}
		}
		Part->AttachToComponent(AttachParent, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		if (AttachParent == Root || AttachParent == HangPivot)
		{
			if (USceneComponent* PartRoot = Part->GetRootComponent())
			{
				FTransform Rel = CodexParts::RelTransform(Parts[i]);
				if (AttachParent == HangPivot)
					Rel.SetLocation(Rel.GetLocation() - HangPivotLoc);
				if (i == 3)
				{
					// Harness fit: nudge the authored transform toward the body
					// so the shoulder straps sit on the shoulders.
					Rel.SetLocation(Rel.GetLocation() + HarnessOffset);
					Rel.SetRotation(FQuat(HarnessRotOffset) * Rel.GetRotation());
					CodexHarnessActor = Part;
				}
				PartRoot->SetRelativeTransform(Rel);
			}
		}
		if (i == 2)
		{
			CodexPropActor = Part;
			if (USceneComponent* PropRoot = Part->GetRootComponent())
			{
				CodexPropBaseRot = PropRoot->GetRelativeRotation().Quaternion();
			}
		}
	}

	// Hide the engine's bare prop-mount flange — the prop BP carries its own hub.
	if (CodexPartActors.IsValidIndex(1) && CodexPartActors[1])
	{
		if (UPrimitiveComponent* Hub = Cast<UPrimitiveComponent>(CodexHelpers::FindHelper(CodexPartActors[1], CodexParts::PropHub())))
		{
			Hub->SetHiddenInGame(true);
			Hub->SetVisibility(false);
		}
	}

	// Move the audio onto the real parts — the EOS BP authors a
	// SOCKET_EngineAudio emitter point. Exhaust hangs low at the engine rear,
	// prop wind sits on the prop shaft.
	if (AActor* EnginePart = CodexPartActors.IsValidIndex(1) ? CodexPartActors[1] : nullptr)
	{
		if (USceneComponent* SndSocket = CodexHelpers::FindHelper(EnginePart, CodexParts::EngineAudioSocket()))
		{
			EngineAudio->AttachToComponent(SndSocket, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
			UE_LOG(LogParamotor, Log, TEXT("ParamotorActor: engine audio on SOCKET_EngineAudio"));
		}
		else if (USceneComponent* ERoot = EnginePart->GetRootComponent())
		{
			EngineAudio->AttachToComponent(ERoot, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
			TArray<UActorComponent*> Dbg;
			EnginePart->GetComponents(Dbg);
			FString Names;
			for (UActorComponent* C : Dbg) { Names += C ? C->GetFName().ToString() + TEXT(" ") : TEXT(""); }
			UE_LOG(LogParamotor, Log, TEXT("ParamotorActor: no SOCKET_EngineAudio — engine comps: %s"), *Names);
		}
		if (USceneComponent* ERoot = EnginePart->GetRootComponent())
		{
			ExhaustAudio->AttachToComponent(ERoot, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
			ExhaustAudio->SetRelativeLocation(FVector(0.0f, 0.0f, -25.0f));
		}
	}
	if (AActor* PropPart = CodexPartActors.IsValidIndex(2) ? CodexPartActors[2] : nullptr)
	{
		if (USceneComponent* PropAxis = CodexHelpers::FindHelper(PropPart, CodexParts::PropAxis()))
		{
			PropWindAudio->AttachToComponent(PropAxis, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		}
		else if (USceneComponent* PRoot = PropPart->GetRootComponent())
		{
			PropWindAudio->AttachToComponent(PRoot, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		}
	}

	// Rigging still needed for the wing connection + carabiner positions.
	BuildAArmHierarchy();

	UE_LOG(LogParamotor, Log, TEXT("ParamotorActor: Codex build mounted (%d parts)"), CodexPartActors.Num());
}


void AParamotorActor::ConfigureFromBuild(UParamotorGameInstance* GI)
{
	if (!GI) return;
	const FParamotorBuild& B = GI->CurrentBuild;

	if (GI->EngineSpecs.IsValidIndex(B.EngineIndex))
	{
		const FParamotorEngineSpec& ES = GI->EngineSpecs[B.EngineIndex];
		PowerScale = FMath::Max(0.1f, ES.Power_hp / 25.0f);
		if (ES.IdleRPM > 0.0f)    EngineIdleRPM = ES.IdleRPM;
		if (ES.RedlineRPM > 0.0f) EngineRedlineRPM = ES.RedlineRPM;
	}

	float Mass = 6.0f; // fuel + misc hardware
	if (GI->FrameSpecs.IsValidIndex(B.FrameIndex))     Mass += GI->FrameSpecs[B.FrameIndex].Weight_kg;
	if (GI->EngineSpecs.IsValidIndex(B.EngineIndex))   Mass += GI->EngineSpecs[B.EngineIndex].Weight_kg;
	if (GI->PropSpecs.IsValidIndex(B.PropIndex))       Mass += GI->PropSpecs[B.PropIndex].Weight_kg;
	if (GI->HarnessSpecs.IsValidIndex(B.HarnessIndex)) Mass += GI->HarnessSpecs[B.HarnessIndex].Weight_kg;
	RigMassKg = Mass;

	UE_LOG(LogParamotor, Log, TEXT("ParamotorActor: configured from build — PowerScale=%.2f RigMass=%.1f kg"),
		PowerScale, RigMassKg);
}

// ==== HANG SWING ====
// The frame+engine+prop hang under HangPivot at shoulder-strap height and
// swing like a damped pendulum driven by the pilot's acceleration. A real
// paramotor leans back when you accelerate forward, sways in turns, and
// settles vertical at rest — the torso leans under it independently.

void AParamotorActor::UpdateHangSwing(float DeltaTime)
{
	AActor* Parent = GetAttachParentActor();
	if (!Parent || !HangPivot) return;

	const FVector Vel = Parent->GetVelocity();
	if (!bHasPrevPilotVel) { PrevPilotVel = Vel; bHasPrevPilotVel = true; }
	const FVector Accel = (Vel - PrevPilotVel) / FMath::Max(DeltaTime, 1e-4f);
	PrevPilotVel = Vel;

	const FVector Fwd = Parent->GetActorForwardVector();
	const FVector Rt  = Parent->GetActorRightVector();
	const float A_F = FVector::DotProduct(Accel, Fwd);   // + forward
	const float A_R = FVector::DotProduct(Accel, Rt);    // + right
	const float G = 981.0f;

	// Hanging pendulum: mass trails acceleration. Bottom tips -Fwd under
	// +A_F (rotation about Rt by +atan), -Rt under +A_R (about Fwd by -atan).
	const float DesPitch = FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan2( A_F, G)), -HangSwingMax, HangSwingMax);
	const float DesRoll  = FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan2(-A_R, G)), -HangSwingMax, HangSwingMax);

	// Damped spring toward the desired tilt
	HangVel.X += (HangSwingK * (DesPitch - HangAng.X) - HangSwingC * HangVel.X) * DeltaTime;
	HangVel.Y += (HangSwingK * (DesRoll  - HangAng.Y) - HangSwingC * HangVel.Y) * DeltaTime;
	HangAng   += HangVel * DeltaTime;

	const FQuat WorldSwing =
		FQuat(Rt,  FMath::DegreesToRadians(HangAng.X)) *
		FQuat(Fwd, FMath::DegreesToRadians(HangAng.Y));

	// Convert the world-space swing into the pivot's local space (the actor
	// carries a baked -90/90 orientation relative to the bone).
	HangPivot->SetRelativeRotation((GetActorQuat().Inverse() * WorldSwing * GetActorQuat()).Rotator());
}
