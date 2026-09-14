// Copyright Epic Games, Inc. All Rights Reserved.

// ParamotorActor_Propulsion.cpp - prop spin + blade-element thrust
// PROP_* constants and AirDensityAtAltitude live in WingActor.h.

#include "ParamotorActor.h"
#include "WingActor.h"

void AParamotorActor::UpdatePropMeshRotation()
{
	// Codex prop: spin the whole prop actor about its mount axis (the
	// SOCKET_Propeller forward is the shaft axis in prop-local space).
	if (CodexPropActor)
	{
		if (USceneComponent* PropRoot = CodexPropActor->GetRootComponent())
		{
			PropRoot->SetRelativeRotation(CodexPropBaseRot * FQuat(FVector::ForwardVector, PropRotation));
		}
		return;
	}

	FQuat PropQuat = FQuat(FVector::ForwardVector, PropRotation);
	for (int32 i = 0; i < PropMeshes.Num(); i++)
	{
		if (i >= PropMeshBaseTransforms.Num()) break;
		FTransform BaseTransform = PropMeshBaseTransforms[i];
		FVector RelPos = BaseTransform.GetTranslation();
		FVector Offset = RelPos - PropCenter;
		FVector NewOffset = PropQuat.RotateVector(Offset);
		FVector NewPos = PropCenter + NewOffset;
		FQuat NewRot = PropQuat * BaseTransform.GetRotation();
		PropMeshes[i]->SetRelativeLocationAndRotation(NewPos, NewRot);
	}
}


// ============================================================
// REAL PROPULSION API
// Based on propeller momentum theory and Moster 185 specifications
// ============================================================

AParamotorActor::FBladeElementResult AParamotorActor::ComputeBladeElement(float AirspeedMs) const
{
	FBladeElementResult Result = {0.0f, 0.0f};

	float PropRPM = GetPropRPM();
	float AltitudeM = GetActorLocation().Z  / CM_PER_M;
	float Rho = AirDensityAtAltitude(AltitudeM);
	float Omega = PropRPM * 2.0f * PI / 60.0f;
	float PropRadius = PROP_DIAMETER_CM / 2.0f  / CM_PER_M;
	float HubRadius = 0.05f;
	float BladeChord = PROP_CHORD_CM  / CM_PER_M;
	float PitchAngle = FMath::Atan2(PROP_PITCH_CM  / CM_PER_M, 2.0f * PI * PropRadius);

	int32 NumElements = (int32)PROP_BLADE_ELEMENTS;
	float dr = (PropRadius - HubRadius) / NumElements;

	for (int32 i = 0; i < NumElements; i++)
	{
		float r = HubRadius + (i + 0.5f) * dr;
		float Vt = Omega * r;
		float Va = AirspeedMs;
		float Vrel = FMath::Sqrt(Va * Va + Vt * Vt);
		if (Vrel < 0.1f) continue;

		float Phi = FMath::Atan2(Va, FMath::Max(0.1f, Vt));
		float rNorm = (r - HubRadius) / (PropRadius - HubRadius);
		float BetaRoot = 30.0f * PI_DIV_180;
		float BetaTip = PitchAngle;
		float Beta = FMath::Lerp(BetaRoot, BetaTip, rNorm);
		float Alpha = Beta - Phi;
		float AlphaDeg = Alpha * RAD_TO_DEG;

		float CL, CD;
		if (FMath::Abs(AlphaDeg) < 15.0f)
		{
			CL = 2.0f * PI * Alpha;
			CD = 0.015f + 0.04f * Alpha * Alpha;
		}
		else
		{
			CL = 0.5f * FMath::Sign(AlphaDeg);
			CD = 1.0f + 0.3f * (FMath::Abs(AlphaDeg) - 15.0f) * PI_DIV_180;
		}

		float dL = 0.5f * Rho * Vrel * Vrel * BladeChord * CL * dr;
		float dD = 0.5f * Rho * Vrel * Vrel * BladeChord * CD * dr;

		// Thrust (axial) and torque (tangential × radius)
		Result.ThrustN += dL * FMath::Cos(Phi) - dD * FMath::Sin(Phi);
		Result.TorqueNM += (dL * FMath::Sin(Phi) + dD * FMath::Cos(Phi)) * r;
	}

	Result.ThrustN *= PROP_NUM_BLADES;
	Result.TorqueNM *= PROP_NUM_BLADES;
	return Result;
}

float AParamotorActor::GetThrustForceN(float AirspeedMs) const
{
	if (EngineState != EEngineState::Running) return 0.0f;

	FBladeElementResult BE = ComputeBladeElement(AirspeedMs);
	float TotalThrustN = BE.ThrustN * PowerScale;

	// Engine power limit: T ≤ P × η / V (can't exceed engine power at high speed)
	float EnginePowerW = GetEnginePowerW();
	if (AirspeedMs > 0.5f && EnginePowerW > 1.0f)
	{
		float ComputedEfficiency = (TotalThrustN * AirspeedMs) / EnginePowerW;
		ComputedEfficiency = FMath::Clamp(ComputedEfficiency, 0.0f, 0.85f);
		float MaxThrustFromPower = EnginePowerW * ComputedEfficiency / AirspeedMs;
		TotalThrustN = FMath::Min(TotalThrustN, MaxThrustFromPower);
	}

	return FMath::Max(0.0f, TotalThrustN);
}

FTransform AParamotorActor::GetPropTransform() const
{
	if (CodexPropActor) return CodexPropActor->GetActorTransform();
	return GetActorTransform();
}

float AParamotorActor::GetPropTorqueNM(float AirspeedMs) const
{
	if (EngineState != EEngineState::Running) return 0.0f;
	FBladeElementResult BE = ComputeBladeElement(AirspeedMs);
	return BE.TorqueNM;
}

float AParamotorActor::GetPropRPM() const
{
	// Moster 185 has a 2.58:1 reduction ratio
	return (EngineRPM > 0.0f) ? EngineRPM / PROP_REDUCTION_RATIO : 0.0f;
}

float AParamotorActor::GetEnginePowerW() const
{
	if (EngineState != EEngineState::Running) return 0.0f;
	// Moster 185: ~18.5 kW (25 HP) at peak RPM — PowerScale shifts this to the
	// equipped engine's real rating (EOS 100 ≈ 18hp → ×0.72).
	float EffectiveThrottle = FMath::Max(0.0f, (Throttle - 0.1f) / 0.9f);
	float RPMFactor = FMath::Max(0.0f, EngineRPM / EngineRedlineRPM);
	const float PeakPowerW = 18500.0f * PowerScale;
	// Real power curve: P = P_peak * throttle * (RPM/redline) for 2-stroke
	return PeakPowerW * EffectiveThrottle * RPMFactor;
}
