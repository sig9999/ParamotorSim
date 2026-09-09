#include "WingActor.h"
#include "ParamotorActor.h"
#include "ParamotorCharacter.h"
#include "ParamotorGameInstance.h"
#include "AirfoilPolar.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

// ============================================================
// TAKEOFF CHECK — transitions from Inflated to Flying
// Called every frame while Inflated and risers connected.
// Real physics: lift must exceed weight for the pilot to leave ground.
// ============================================================

void AWingActor::CheckTakeoff(float DeltaTime)
{
	if (!ConnectedParamotor) return;
	APawn* Pawn = Cast<APawn>(ConnectedParamotor->GetAttachParentActor());
	if (!Pawn) return;
	ACharacter* Char = Cast<ACharacter>(Pawn);
	if (!Char || !Char->GetCharacterMovement()) return;

	FVector Vel = Pawn->GetVelocity();
	float GroundSpeedMs = FVector(Vel.X, Vel.Y, 0.0f).Size() / CM_PER_M;

	// Apparent wind = wind - pilot velocity (horizontal)
	FVector WindVec = GetWindVector();
	FVector Apparent = WindVec - Vel;
	FVector ApparentHoriz(Apparent.X, Apparent.Y, 0.0f);
	float ApparentMs = ApparentHoriz.Size() / CM_PER_M;

	// Headwind component (positive = headwind, helps takeoff)
	FVector Fwd = Pawn->GetActorForwardVector();
	Fwd.Z = 0.0f;
	Fwd.Normalize();
	float HeadwindMs = -FVector::DotProduct(ApparentHoriz, Fwd) / CM_PER_M;
	float AirspeedMs = FMath::Max(0.0f, HeadwindMs);

	// Debug diagnostics for HUD
	DebugGroundAirspeed = AirspeedMs * CM_PER_M;
	DebugGroundSpeed = GroundSpeedMs * CM_PER_M;
	DebugHeadwind = HeadwindMs * CM_PER_M;

	// Takeoff speed from stall speed: V_stall = sqrt(2 * W / (rho * S * CLmax))
	float TotalWeightKg = PilotWeight + WingModel.WingWeight;
	float StallDenom = AIR_DENSITY_SEA_LEVEL * WingModel.WingArea * WingModel.CLMax;
	float TakeoffSpeedMs = StallDenom > 0.0f
		? FMath::Sqrt(2.0f * TotalWeightKg * GRAVITY_MS2 / StallDenom)
		: MIN_TAKEOFF_AIRSPEED_MS;
	TakeoffSpeedMs *= 1.2f;  // 20% margin above stall for safe takeoff

	DebugTakeoffThreshold = TakeoffSpeedMs * CM_PER_M;

	// Lift at current airspeed — use CL that scales with airspeed toward CLMax.
	// During ground run, the wing is at trim AoA. As airspeed approaches takeoff
	// speed, the wing approaches flying CL. We interpolate: at 0 airspeed CL=CL0,
	// at takeoff speed CL=CLMax. This gives realistic lift buildup during the run.
	float DynPressure = 0.5f * AIR_DENSITY_SEA_LEVEL * AirspeedMs * AirspeedMs;
	float SpeedRatio = (TakeoffSpeedMs > 0.0f) ? FMath::Clamp(AirspeedMs / TakeoffSpeedMs, 0.0f, 1.0f) : 0.0f;
	float CL = FMath::Lerp(WingModel.CL0, WingModel.CLMax, SpeedRatio);
	float LiftN = DynPressure * WingModel.WingArea * CL;
	float WeightN = TotalWeightKg * GRAVITY_MS2;

	// Weight support fraction (how much lift supports the pilot)
	DebugWeightSupport = (WeightN > 0.0f) ? (LiftN / WeightN * PERCENT_FACTOR) : 0.0f;

	// Takeoff conditions:
	// 1. Airspeed above takeoff speed
	// 2. Lift supports at least 80% of weight (near flight)
	// 3. Throttle above 25% (pilot is committing to takeoff)
	bool bSpeedOK = AirspeedMs >= TakeoffSpeedMs;
	bool bLiftOK = LiftN >= WeightN * 0.8f;
	bool bThrottleOK = ThrottleAmount > 0.25f;

	if (bSpeedOK && bLiftOK && bThrottleOK)
	{
		WingState = EWingState::Flying;
		bIsAirborne = true;
		Char->GetCharacterMovement()->SetMovementMode(MOVE_None);
		Char->GetCharacterMovement()->GravityScale = 0.0f;
		Char->GetCharacterMovement()->AirControl = 0.0f;
		Char->GetCharacterMovement()->MaxAcceleration = 0.0f;
		Char->bUseControllerRotationYaw = false;
		Char->GetCharacterMovement()->bOrientRotationToMovement = false;
		Char->GetCharacterMovement()->bUseControllerDesiredRotation = false;

		// Set mesh to base relative rotation (MetaHuman -90° yaw)
		FQuat FlightBaseQuat(FVector::UpVector, FMath::DegreesToRadians(-90.0f));
		Char->GetMesh()->SetRelativeRotation(FlightBaseQuat);

		PilotPitchTarget = -75.0f;  // Begin seat transition
		bAutoSeatPending = true;
		FlightTime = 0.0f;
		LaunchTimer = 0.0f;

		// Initial velocity from ground run
		Char->GetCharacterMovement()->Velocity = FVector(Vel.X, Vel.Y, 50.0f);

		UE_LOG(LogTemp, Warning, TEXT("WingActor: TAKEOFF! airspeed=%.1f m/s, lift=%.0f N, weight=%.0f N"),
			AirspeedMs, LiftN, WeightN);
	}
}

// ============================================================
// FLIGHT PHYSICS — full aerodynamic simulation
// Called every frame while WingState == Flying.
// The WingActor has 100% control over pilot position, velocity,
// and rotation. CMC is in MOVE_None mode.
// ============================================================

void AWingActor::UpdateFlight(float DeltaTime)
{
	if (!ConnectedParamotor) return;
	APawn* Pawn = Cast<APawn>(ConnectedParamotor->GetAttachParentActor());
	if (!Pawn) return;
	ACharacter* Char = Cast<ACharacter>(Pawn);
	if (!Char || !Char->GetCharacterMovement()) return;
	UCharacterMovementComponent* CMC = Char->GetCharacterMovement();

	FlightTime += DeltaTime;

	// === TRIMMER INTERPOLATION ===
	TrimmerPosition = FMath::FInterpTo(TrimmerPosition, TrimmerTarget, DeltaTime, TrimmerSpeed);

	// === BRAKE LAG (trailing edge deflection takes time) ===
	float BrakeLagRate = 3.0f;  // ~0.3s to full deflection
	EffectiveLeftBrake = FMath::FInterpTo(EffectiveLeftBrake, LeftBrakeAmount, DeltaTime, BrakeLagRate);
	EffectiveRightBrake = FMath::FInterpTo(EffectiveRightBrake, RightBrakeAmount, DeltaTime, BrakeLagRate);

	FVector Vel = CMC->Velocity;
	FVector PawnLoc = Pawn->GetActorLocation();

	// === AERO STATE (apparent wind, AoA, coefficients, collapses) ===
	FFlightCtx Ctx;
	ComputeAeroState(Ctx, Vel, DeltaTime, PawnLoc);

	// === FORCES (lift, drag, thrust) ===
	ComputeForces(Ctx);

	// === INTEGRATE (force integration + thermal + velocity caps) ===
	IntegrateForces(Ctx, Vel, DeltaTime, PawnLoc);

	// === PENDULUM + STEERING ===
	UpdatePendulumAndSteering(DeltaTime);

	// === PILOT ORIENTATION (yaw, roll, pitch, mesh rotation) ===
	UpdatePilotOrientation(Pawn, Char, DeltaTime);

	// === MOVE PILOT ===
	CMC->Velocity = Ctx.NewVel;
	FVector NewLoc = PawnLoc + Ctx.NewVel * DeltaTime;

	// === LANDING CHECK ===
	HandleLanding(Pawn, Char, CMC, Ctx, NewLoc, DeltaTime);

	CMC->Velocity = Ctx.NewVel;
	Pawn->SetActorLocation(NewLoc, false);

	// === WING POSITION + ROTATION ===
	UpdateWingTransform(Ctx, NewLoc, DeltaTime);

	// === HUD VARIABLES + LOG ===
	ClimbRate = Ctx.NewVel.Z;  // cm/s
	FlightLogTimer += DeltaTime;
	if (FlightLogTimer > 1.0f)
	{
		FlightLogTimer = 0.0f;
		UE_LOG(LogTemp, Log, TEXT("Flight: AS=%.1f m/s, climb=%.1f m/s, GR=%.1f, alt=%.0f m, thr=%.0f N, roll=%.0f°, yaw=%.0f°/s"),
			Ctx.Vms, Ctx.NewVel.Z / CM_PER_M, GlideRatio, Ctx.AltitudeM, Ctx.ThrustN, CurrentRoll, WingYawRate);
	}
}

// ============================================================
// FLIGHT PHYSICS HELPERS
// ============================================================

void AWingActor::ComputeAeroState(FFlightCtx& Ctx, FVector Vel, float DeltaTime, FVector PawnLoc)
{
	Ctx.AltitudeM = (PawnLoc.Z - GetCachedGroundZ(PawnLoc)) / CM_PER_M;
	Ctx.Rho = AirDensityAtAltitude(Ctx.AltitudeM);

	// === APPARENT WIND ===
	Ctx.ShiftedWindDir = GetShiftedWindDirection();
	FVector WindVec = GetWindVector();

	FVector TurbVel = FVector::ZeroVector;
	if (TurbIntensity > 0.01f)
	{
		FVector WindFwd = Ctx.ShiftedWindDir.GetSafeNormal();
		FVector WindRight = FVector::CrossProduct(FVector::UpVector, WindFwd).GetSafeNormal();
		TurbVel = WindFwd * TurbLongState * CM_PER_M + WindRight * TurbLatState * CM_PER_M
			+ FVector::UpVector * TurbVertState * CM_PER_M;
	}

	FVector ApparentWind = (WindVec + TurbVel) - Vel;
	Ctx.ApparentHoriz = FVector(ApparentWind.X, ApparentWind.Y, 0.0f);
	Ctx.Vms = Ctx.ApparentHoriz.Size() / CM_PER_M;
	Airspeed = Ctx.Vms * CM_PER_M;

	// === ANGLE OF ATTACK ===
	float BrakeAoA = (EffectiveLeftBrake + EffectiveRightBrake) * 0.5f * 4.0f;
	float TurbAoA = 0.0f;
	if (Ctx.Vms > 0.5f && TurbIntensity > 0.01f)
	{
		TurbAoA = FMath::Atan2(TurbVertState, FMath::Max(0.5f, Ctx.Vms)) * RAD_TO_DEG;
	}
	Ctx.AoA = WingModel.TrimAoA + BrakeAoA + GetTrimmerAoAOffset() + TurbAoA;

	// === GROUND EFFECT ===
	float WingspanM = FMath::Sqrt(WingModel.WingArea * WingModel.AR);
	float GroundEffectFactor = 1.0f;
	if (Ctx.AltitudeM < WingspanM)
	{
		float H = FMath::Max(0.0f, Ctx.AltitudeM / WingspanM);
		GroundEffectFactor = FMath::Min(1.0f, 0.5f + 0.5f * H);
	}

	// === AERODYNAMIC COEFFICIENTS ===
	Ctx.TotalBrake = (EffectiveLeftBrake + EffectiveRightBrake) * 0.5f;
	float CL, CD;
	LookupAirfoilPolarWithCamber(Ctx.AoA, Ctx.TotalBrake, CL, CD);
	CL = FMath::Clamp(CL, -0.4f, WingModel.CLMax);
	float CDinduced = CL * CL / (PI * WingModel.AR * OswaldEfficiency);
	Ctx.CL = CL;
	Ctx.CDtotal = WingModel.CD0 + CDinduced * GroundEffectFactor + CD;

	// === COLLAPSE DETECTION ===
	float CollapseRisk = GetTrimmerCollapseRisk();
	float AsymBrake = FMath::Abs(EffectiveLeftBrake - EffectiveRightBrake);
	if (AsymBrake > 0.7f && Ctx.AoA > WingModel.StallAoA * 0.8f && Ctx.Vms > 8.0f)
	{
		float CollapseAmount = FMath::Clamp((AsymBrake - 0.7f) * 3.0f * CollapseRisk, 0.0f, 1.0f);
		if (EffectiveLeftBrake > EffectiveRightBrake)
			LeftCollapseAmount = FMath::Max(LeftCollapseAmount, CollapseAmount);
		else
			RightCollapseAmount = FMath::Max(RightCollapseAmount, CollapseAmount);
	}

	if (Ctx.AoA < -2.0f && Ctx.Vms > 8.0f)
	{
		FrontalCollapseAmount = FMath::Max(FrontalCollapseAmount, FMath::Clamp((-Ctx.AoA - 2.0f) * 0.1f, 0.0f, 1.0f));
	}

	// Collapse recovery
	float RecoveryRate = FMath::Max(0.3f, CurrentWindSpeed * 0.2f);
	RecoveryRate *= (1.0f - Ctx.TotalBrake * 0.5f);
	LeftCollapseAmount = FMath::Max(0.0f, LeftCollapseAmount - RecoveryRate * DeltaTime);
	RightCollapseAmount = FMath::Max(0.0f, RightCollapseAmount - RecoveryRate * DeltaTime);
	FrontalCollapseAmount = FMath::Max(0.0f, FrontalCollapseAmount - RecoveryRate * DeltaTime);

	// Collapse reduces lift and increases drag
	float AvgCollapse = (LeftCollapseAmount + RightCollapseAmount) * 0.5f;
	Ctx.CL *= (1.0f - AvgCollapse * 0.7f);
	Ctx.CDtotal += AvgCollapse * 0.5f;
}

void AWingActor::ComputeForces(FFlightCtx& Ctx)
{
	float DynPressure = 0.5f * Ctx.Rho * Ctx.Vms * Ctx.Vms;
	float HalfArea = WingModel.WingArea * 0.5f;

	// Per-side lift (for roll moment from asymmetric collapse)
	float LeftLiftFraction = 1.0f - LeftCollapseAmount * 0.9f;
	float RightLiftFraction = 1.0f - RightCollapseAmount * 0.9f;
	float LeftLiftN = DynPressure * HalfArea * Ctx.CL * LeftLiftFraction;
	float RightLiftN = DynPressure * HalfArea * Ctx.CL * RightLiftFraction;
	Ctx.TotalLiftN = LeftLiftN + RightLiftN;

	// Wing drag
	float WingDragN = DynPressure * WingModel.WingArea * Ctx.CDtotal;

	// Pilot + line drag
	float PilotDragArea = bSeated ? 0.4f : 0.5f;
	float PilotDragN = 0.5f * Ctx.Rho * Ctx.Vms * Ctx.Vms * PilotDragArea * 0.5f;
	float LineDragN = 0.5f * Ctx.Rho * Ctx.Vms * Ctx.Vms * 0.06f * 1.0f;
	Ctx.TotalDragN = WingDragN + PilotDragN + LineDragN;

	// Thrust from paramotor engine
	Ctx.ThrustN = 0.0f;
	if (ConnectedParamotor && ConnectedParamotor->IsEquipped())
		Ctx.ThrustN = ConnectedParamotor->GetThrustForceN(Ctx.Vms);

	Ctx.TotalMassKg = PilotWeight + WingModel.WingWeight;

	GlideRatio = (Ctx.TotalDragN > 1.0f) ? (Ctx.TotalLiftN / Ctx.TotalDragN) : 0.0f;
}

void AWingActor::IntegrateForces(FFlightCtx& Ctx, FVector Vel, float DeltaTime, FVector PawnLoc)
{
	Ctx.ApparentDir = Ctx.ApparentHoriz.GetSafeNormal();

	APawn* Pawn = Cast<APawn>(ConnectedParamotor->GetAttachParentActor());
	FVector Fwd = Pawn ? Pawn->GetActorForwardVector() : FVector::ForwardVector;
	FVector HorizFwd(Fwd.X, Fwd.Y, 0.0f);
	HorizFwd.Normalize();

	// Lift acts perpendicular to apparent wind (upward)
	FVector LiftDir = FVector::CrossProduct(FVector::CrossProduct(Ctx.ApparentDir, FVector::UpVector), Ctx.ApparentDir).GetSafeNormal();
	FVector LiftForce = LiftDir * Ctx.TotalLiftN * CM_PER_M;
	FVector DragForce = -Ctx.ApparentDir * Ctx.TotalDragN * CM_PER_M;
	FVector ThrustForce = HorizFwd * Ctx.ThrustN * CM_PER_M;
	FVector GravityForce = FVector(0, 0, -Ctx.TotalMassKg * GRAVITY_CM_S2);

	// Thermal lift (velocity contribution, not force)
	float ThermalLiftMs = GetThermalLiftAtPosition(PawnLoc, Ctx.AltitudeM);
	// Compensating subsidence between thermals
	float LocalThermalSinkMs = GetThermalSinkAtPosition(PawnLoc, Ctx.AltitudeM);

	FVector Accel = (LiftForce + DragForce + ThrustForce + GravityForce) / Ctx.TotalMassKg;
	Ctx.NewVel = Vel + Accel * DeltaTime;

	// Thermal updraft: couple velocity toward updraft speed
	if (ThermalLiftMs > 0.01f)
	{
		float ThermalCoupling = 0.5f;
		float UpdraftCm = ThermalLiftMs * CM_PER_M;
		Ctx.NewVel.Z = FMath::Lerp(Ctx.NewVel.Z, UpdraftCm, ThermalCoupling * DeltaTime);
	}

	// Thermal sink (subsidence): push velocity downward between thermals
	if (LocalThermalSinkMs > 0.01f)
	{
		float SinkCoupling = 0.3f;  // Gentler than updraft coupling
		float SinkCm = -LocalThermalSinkMs * CM_PER_M;
		Ctx.NewVel.Z = FMath::Lerp(Ctx.NewVel.Z, SinkCm, SinkCoupling * DeltaTime);
	}

	// Velocity safety caps
	float MaxVelMs = 30.0f;
	float MaxVelCm = MaxVelMs * CM_PER_M;
	if (Ctx.NewVel.Size() > MaxVelCm)
		Ctx.NewVel = Ctx.NewVel.GetSafeNormal() * MaxVelCm;
	if (FMath::IsNaN(Ctx.NewVel.X) || FMath::IsNaN(Ctx.NewVel.Y) || FMath::IsNaN(Ctx.NewVel.Z))
		Ctx.NewVel = Vel;
}

void AWingActor::UpdatePendulumAndSteering(float DeltaTime)
{
	// === PENDULUM PHYSICS ===
	float PendulumOmega = FMath::Sqrt(GRAVITY_CM_S2 / LINE_LENGTH_CM);
	float PendulumDamping = 0.3f;
	float PendulumAccel = -PENDULUM_LENGTH_CM * PendulumOmega * PendulumOmega * FMath::Sin(PendulumAngle)
		- PendulumDamping * PendulumAngularVel * LINE_LENGTH_CM;
	PendulumAngularVel += (PendulumAccel / LINE_LENGTH_CM) * DeltaTime;
	PendulumAngle += PendulumAngularVel * DeltaTime;
	PendulumAngle = FMath::Clamp(PendulumAngle, -0.5f, 0.5f);

	// === ROLL STEERING ===
	float WeightShiftRoll = WeightShift * 15.0f;
	float BrakeAsym = EffectiveRightBrake - EffectiveLeftBrake;
	float BrakeYawRate = BrakeAsym * 30.0f;
	float CollapseYawRate = (LeftCollapseAmount - RightCollapseAmount) * 20.0f;

	float TargetYawRate = BrakeYawRate + CollapseYawRate;
	WingYawRate = FMath::FInterpTo(WingYawRate, TargetYawRate, DeltaTime, 2.0f);

	float CollapseRoll = (LeftCollapseAmount - RightCollapseAmount) * 10.0f;
	float TargetRoll = WeightShiftRoll + CollapseRoll;

	// Spiral dive
	float CurrentBank = FMath::Abs(TargetRoll);
	if (CurrentBank > 40.0f)
	{
		SpiralDiveFactor = FMath::Clamp((CurrentBank - 40.0f) / 30.0f, 0.0f, 1.0f);
		TargetRoll += SpiralDiveFactor * 10.0f * FMath::Sign(TargetRoll);
	}
	else
	{
		SpiralDiveFactor = FMath::Max(0.0f, SpiralDiveFactor - DeltaTime * 0.5f);
	}

	WingRollRate = FMath::FInterpTo(WingRollRate, (TargetRoll - CurrentRoll) * 3.0f, DeltaTime, 2.0f);
	CurrentRoll += WingRollRate * DeltaTime;
	CurrentRoll = FMath::Clamp(CurrentRoll, -60.0f, 60.0f);
}

void AWingActor::UpdatePilotOrientation(APawn* Pawn, ACharacter* Char, float DeltaTime)
{
	FRotator CurrentRot = Pawn->GetActorRotation();
	float NewYaw = CurrentRot.Yaw + WingYawRate * DeltaTime;

	PilotRollRate = FMath::FInterpTo(PilotRollRate, (CurrentRoll - PilotRollAngle) * 2.0f, DeltaTime, 1.5f);
	PilotRollAngle += PilotRollRate * DeltaTime;
	PilotRollAngle = FMath::Clamp(PilotRollAngle, -60.0f, 60.0f);

	if (bAutoSeatPending)
	{
		LaunchTimer += DeltaTime;
		if (LaunchTimer > 2.0f)
		{
			bSeated = true;
			bAutoSeatPending = false;
		}
	}
	PilotPitch = FMath::FInterpTo(PilotPitch, PilotPitchTarget, DeltaTime, 2.0f);

	Pawn->SetActorRotation(FRotator(0.0f, NewYaw, 0.0f));

	if (Char->GetMesh())
	{
		FQuat BaseQuat(FVector::UpVector, FMath::DegreesToRadians(-90.0f));
		FQuat PitchQuat(FVector::RightVector, FMath::DegreesToRadians(PilotPitch));
		FQuat RollQuat(FVector::ForwardVector, FMath::DegreesToRadians(PilotRollAngle));
		Char->GetMesh()->SetRelativeRotation(BaseQuat * PitchQuat * RollQuat);
	}
}

void AWingActor::HandleLanding(APawn* Pawn, ACharacter* Char, UCharacterMovementComponent* CMC, FFlightCtx& Ctx, FVector& NewLoc, float DeltaTime)
{
	float GroundZ = GetCachedGroundZ(NewLoc);
	if (NewLoc.Z > GroundZ + 1.0f || Ctx.NewVel.Z >= 0.0f)
		return;

	// Ground contact — landed
	float ImpactSpeedMs = -Ctx.NewVel.Z / CM_PER_M;
	NewLoc.Z = GroundZ;
	Ctx.NewVel.Z = 0.0f;

	// Ground friction
	float HorizSpeed = FMath::Sqrt(Ctx.NewVel.X * Ctx.NewVel.X + Ctx.NewVel.Y * Ctx.NewVel.Y);
	float FrictionDecel = 0.4f * GRAVITY_CM_S2 * DeltaTime;
	if (HorizSpeed > 0.0f)
	{
		float Factor = FMath::Max(0.0f, 1.0f - FrictionDecel / HorizSpeed);
		Ctx.NewVel.X *= Factor;
		Ctx.NewVel.Y *= Factor;
	}

	// Injury from hard landing
	AParamotorCharacter* ParamotorChar = Cast<AParamotorCharacter>(Char);
	if (ParamotorChar && ImpactSpeedMs > 3.0f)
		ParamotorChar->ApplyImpactInjury(ImpactSpeedMs, false);

	// Reset flight state
	bIsAirborne = false;
	CMC->GravityScale = 1.0f;
	CMC->AirControl = 0.05f;
	CMC->MaxAcceleration = 2048.0f;
	Char->bUseControllerRotationYaw = true;
	CMC->bOrientRotationToMovement = false;
	CMC->SetMovementMode(MOVE_Walking);

	PendulumAngle = 0.0f;
	PendulumAngularVel = 0.0f;
	PilotPitch = 0.0f;
	PilotPitchTarget = 0.0f;
	bSeated = false;
	bAutoSeatPending = false;
	LaunchTimer = 0.0f;
	SeatTransition = 0.0f;
	WingPitchAngle = 0.0f;
	WingPitchRate = 0.0f;
	WingRollRate = 0.0f;
	WingYawRate = 0.0f;
	CurrentRoll = 0.0f;
	PilotRollAngle = 0.0f;
	PilotRollRate = 0.0f;
	EffectiveLeftBrake = 0.0f;
	EffectiveRightBrake = 0.0f;
	LeftCollapseAmount = 0.0f;
	RightCollapseAmount = 0.0f;
	FrontalCollapseAmount = 0.0f;
	SpiralDiveFactor = 0.0f;
	TrimmerPosition = 0.0f;
	TrimmerTarget = 0.0f;

	WingState = EWingState::Inflated;
	UE_LOG(LogTemp, Warning, TEXT("WingActor: Landed (horiz speed %.1f m/s)"), Ctx.NewVel.Size2D() / CM_PER_M);
}

void AWingActor::UpdateWingTransform(FFlightCtx& Ctx, FVector PilotLoc, float DeltaTime)
{
	FVector WingOffset = FVector(0, 0, LINE_LENGTH_CM);
	if (Ctx.Vms > 0.5f)
	{
		FVector UpwindDir = -Ctx.ApparentDir;
		WingOffset += UpwindDir * LINE_LENGTH_CM * 0.1f;
	}
	SetActorLocation(PilotLoc + WingOffset, false);

	// Yaw wing to face into wind
	FVector WindDir2D = Ctx.ShiftedWindDir;
	WindDir2D.Z = 0.0f;
	WindDir2D.Normalize();
	float TargetWingYaw = FMath::Atan2(WindDir2D.Y, WindDir2D.X) * RAD_TO_DEG;
	FRotator WingRot = GetActorRotation();
	WingRot.Yaw = FMath::FInterpTo(WingRot.Yaw, TargetWingYaw, DeltaTime, 3.0f);
	WingRot.Pitch = FMath::FInterpTo(WingRot.Pitch, -Ctx.AoA * 0.3f, DeltaTime, 5.0f);
	WingRot.Roll = FMath::FInterpTo(WingRot.Roll, CurrentRoll * 0.5f, DeltaTime, 3.0f);
	SetActorRotation(WingRot);
}
