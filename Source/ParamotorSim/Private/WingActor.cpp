#include "WingActor.h"
#include "ParamotorActor.h"
#include "ParamotorCharacter.h"
#include "ParamotorGameInstance.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "ProceduralMeshComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "ParagliderWingMesh.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UObject/ConstructorHelpers.h"

// ============================================================
// WingActor.cpp — WingActor implementation split
// ============================================================

AWingActor::AWingActor()

{
	PrimaryActorTick.bCanEverTick = true;
	// Tick after the character so our rotation isn't overridden by character Tick
	PrimaryActorTick.TickGroup = TG_PostPhysics;
Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
SetRootComponent(Root);
Root->SetMobility(EComponentMobility::Movable);
CanopyMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("CanopyMesh"));
CanopyMesh->SetupAttachment(Root);
CanopyMesh->SetMobility(EComponentMobility::Movable);
CanopyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
CanopyMesh->SetSimulatePhysics(false);
CanopyMesh->SetCastShadow(false);
BagMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BagMesh"));
BagMesh->SetupAttachment(Root);
BagMesh->SetMobility(EComponentMobility::Movable);
BagMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
BagMesh->SetSimulatePhysics(false);
BagMesh->SetCastShadow(false);
BagMesh->SetVisibility(false);
LineMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("LineMesh"));
LineMesh->SetupAttachment(Root);
LineMesh->SetMobility(EComponentMobility::Movable);
LineMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
LineMesh->SetSimulatePhysics(false);
LineMesh->SetCastShadow(false);
LineMesh->SetVisibility(false);
}

// ============================================================
// BEGIN PLAY
// ============================================================

void AWingActor::BeginPlay(){
	Super::BeginPlay();
// Load canopy material — use engine VertexColorMaterial (shows vertex colors as emissive)
// This is in EngineDebugMaterials, not EngineMaterials
	CanopyMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineDebugMaterials/VertexColorMaterial.VertexColorMaterial"));
if (!CanopyMaterial)	{
		// Fallback to M_Canopy (project material with VertexColor node)
		CanopyMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_Canopy.M_Canopy"));
}
	if (!CanopyMaterial)	{
		CanopyMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/EmissiveMeshMaterial.EmissiveMeshMaterial"));
UE_LOG(LogTemp, Warning, TEXT("WingActor: All canopy materials not found, using EmissiveMeshMaterial fallback"));
}
	else	{
		UE_LOG(LogTemp, Warning, TEXT("WingActor: Loaded canopy material"));
}
// Initialize cloth from new paraglider mesh
	InitCloth();
InitLines();
// Spawn fully laid out for testing (skip bag/crinkle)
	WingState = EWingState::Spread;
SetSpreadState();
UpdateCanopyMesh();
UpdateLineMesh();

UE_LOG(LogTemp, Warning, TEXT("WingActor: BeginPlay done — %d canopy verts, %d line particles"),		Particles.Num(), LineParticles.Num());
}

void AWingActor::LiftALines()
{
	if (!bRisersConnected) return;
	if (WingState != EWingState::Connected && WingState != EWingState::Inflating) return;
	// A-line pull: physically opens the leading edge intakes.
	// This does NOT force inflation — the wing still needs apparent wind to fill.
	// In real life, pulling A-risers just opens the cell mouths so air can enter.
	// If there's no wind, the wing won't inflate no matter how hard you pull.
	bLiftingALines = true;
	// State transition to Inflating happens naturally in the physics update
	// when cells start filling from ram pressure through the open intakes.
}

void AWingActor::ReleaseALines()
{
	// Releasing A-risers closes the intakes back to normal position.
	// Cells that are already pressurized stay pressurized (air can't easily escape).
	// But new air can't enter as easily, so a deflating wing won't re-inflate.
	bLiftingALines = false;
}

void AWingActor::SetLeftBrake(float Amount) {
 LeftBrakeTarget = FMath::Clamp(Amount, 0.0f, 1.0f);
}

void AWingActor::SetRightBrake(float Amount) {
 RightBrakeTarget = FMath::Clamp(Amount, 0.0f, 1.0f);
}

void AWingActor::PumpBrakes(){
	if (WingState != EWingState::Inflated && WingState != EWingState::Flying) return;
// Brake pumping pushes air out of cells, then they reinflate from wind.
// This is handled by the cell pressure model — no fake openness boost.
// The visual canopy shape comes from CellFillFactor, not a magic variable.
UE_LOG(LogTemp, Log, TEXT("WingActor: Brake pump (cell pressure handles reinflation)"));
}

void AWingActor::ForceTakeoff(){
	if (WingState != EWingState::Inflated) return;
APawn* Pawn = nullptr;
if (ConnectedParamotor)		Pawn = Cast<APawn>(ConnectedParamotor->GetAttachParentActor());
if (!Pawn) return;
ACharacter* Char = Cast<ACharacter>(Pawn);
if (!Char) return;
WingState = EWingState::Flying;
bIsAirborne = true;
// BYPASS CMC ENTIRELY — use MOVE_None so CMC does zero physics processing
// WingActor has 100% control over velocity, position, and rotation
Char->GetCharacterMovement()->SetMovementMode(MOVE_None);
Char->GetCharacterMovement()->GravityScale = 0.0f;
Char->GetCharacterMovement()->AirControl = 0.0f;
Char->GetCharacterMovement()->MaxAcceleration = 0.0f;
// Disable all rotation flags — WingActor controls rotation
Char->bUseControllerRotationYaw = false;
Char->GetCharacterMovement()->bOrientRotationToMovement = false;
Char->GetCharacterMovement()->bUseControllerDesiredRotation = false;
// Set mesh to base relative rotation (MetaHuman -90° yaw)
FQuat FlightBaseQuat(FVector::UpVector, FMath::DegreesToRadians(-90.0f));
Char->GetMesh()->SetRelativeRotation(FlightBaseQuat);
// DEBUG SHORTCUT — editor/dev only. Real takeoff goes through ground handling.
#if WITH_EDITOR || UE_BUILD_DEVELOPMENT
FVector PawnLoc = Pawn->GetActorLocation();
float GroundZ = GetGroundZ(PawnLoc);
PawnLoc.Z = GroundZ + 500.0f;  // 5m — just enough to clear ground collision
Pawn->SetActorLocation(PawnLoc, false);
Char->GetCharacterMovement()->Velocity = FVector(0.0f, 0.0f, 100.0f);  // 1 m/s up
PendulumAngle = 0.0f;
PendulumAngularVel = 0.0f;  // Start at rest
#endif
PilotRollAngle = 0.0f;
PilotRollRate = 0.0f;
PrevThrottleAmount = ThrottleAmount;
ThrottleRate = 0.0f;
FlightTime = 0.0f;
PilotPitch = 0.0f;
PilotPitchTarget = -75.0f;
UE_LOG(LogTemp, Warning, TEXT("WingActor: FORCED TAKEOFF!"));
}

void AWingActor::SetPilotWeight(float TotalWeightKg){
	PilotWeight = FMath::Max(40.0f, TotalWeightKg);  // Real min pilot ~40kg
UE_LOG(LogTemp, Warning, TEXT("WingActor: Pilot weight set to %.1f kg (max=%.1f, loading=%.1f kg/m²)"),		PilotWeight, WingModel.MaxTakeoffWeight, GetWingLoading());
}

float AWingActor::GetWingLoading() const{
	return (PilotWeight + WingModel.WingWeight) / WingModel.WingArea;
// kg/m²
}

bool AWingActor::UnpackFromBag(){
	if (WingState != EWingState::InBag) return false;
WingState = EWingState::Spread;
SetSpreadState();
return true;
}

bool AWingActor::GrabNearestEdge(FVector PlayerHandPos){
	if (WingState != EWingState::Spread && WingState != EWingState::Crinkled) return false;
FVector ActorLoc = GetActorLocation();
FVector LocalHand = PlayerHandPos - ActorLoc;
int32 Nearest = -1;
float NearestDist = 1e9;
for (int32 i = 0; i < Particles.Num(); i++)	{
		float D = (Particles[i].Pos - LocalHand).Size();
if (D < NearestDist)		{
			NearestDist = D;
Nearest = i;
}
}
	if (Nearest >= 0 && NearestDist < 200.0f)	{
		GrabbedParticleIndex = Nearest;
Particles[Nearest].bGrabbed = true;
GrabHandPos = PlayerHandPos;
return true;
}
	return false;
}

void AWingActor::ReleaseEdge(){
	if (GrabbedParticleIndex >= 0 && GrabbedParticleIndex < Particles.Num())		Particles[GrabbedParticleIndex].bGrabbed = false;
GrabbedParticleIndex = -1;
}

void AWingActor::UpdateGrabPosition(FVector NewHandPos){
	GrabHandPos = NewHandPos;
}

void AWingActor::ConnectRisers(AParamotorActor* Paramotor, FVector LeftCarabiner, FVector RightCarabiner){
	ConnectedParamotor = Paramotor;
LeftCarabinerPos = LeftCarabiner;
RightCarabinerPos = RightCarabiner;
bRisersConnected = true;
WingState = EWingState::Connected;
// Capture wind direction from the pilot's facing direction.
// Wind blows FROM the direction the pilot faces (headwind for takeoff).
// This is realistic — you always lay out your wing and face into the wind.

if (Paramotor)	{
		APawn* Pawn = Cast<APawn>(Paramotor->GetAttachParentActor());
if (Pawn)		{
			FVector PilotFwd = Pawn->GetActorForwardVector();
PilotFwd.Z = 0.0f;
PilotFwd.Normalize();
// Wind direction = opposite of pilot forward = blows toward pilot's face
			WindDirection = -PilotFwd;
}
}
	// === INITIALIZE WING YAW TO FACE INTO WIND ===
	// Wing leading edge (-X local) should point upwind (into wind).
	// WindDirection = direction wind blows TOWARD.
	// Upwind = -WindDirection.
	// Wing +X points downwind = WindDirection.
	// Yaw for +X pointing in WindDirection = atan2(WindDir.Y, WindDir.X)
	WingGroundYaw = FMath::Atan2(WindDirection.Y, WindDirection.X) * 180.0f / PI;
	WingGroundYawRate = 0.0f;
	WingGroundVel = FVector::ZeroVector;
	KiteYawDrift = 0.0f;
	KiteYawVel = 0.0f;
	UE_LOG(LogTemp, Warning, TEXT("WingActor: Risers connected — L=%s R=%s WindDir=%s WingYaw=%.1f°"),		*LeftCarabiner.ToString(), *RightCarabiner.ToString(), *WindDirection.ToString(), WingGroundYaw);
}

void AWingActor::DisconnectRisers(){
	// Reset character gravity and movement mode BEFORE nulling ConnectedParamotor
if (ConnectedParamotor)	{
		APawn* Pawn = Cast<APawn>(ConnectedParamotor->GetAttachParentActor());
if (Pawn)		{
			ACharacter* Char = Cast<ACharacter>(Pawn);
if (Char && Char->GetCharacterMovement())			{
				Char->GetCharacterMovement()->GravityScale = 1.0f;
if (Char->GetCharacterMovement()->IsFlying())					Char->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
}
}
}
	bRisersConnected = false;
ConnectedParamotor = nullptr;
if (WingState == EWingState::Connected || WingState == EWingState::Inflating ||
	WingState == EWingState::Inflated || WingState == EWingState::Flying)		WingState = EWingState::Spread;
// Unpin carabiners
if (LeftCarabinerIdx >= 0 && LeftCarabinerIdx < LineParticles.Num())		LineParticles[LeftCarabinerIdx].bPinned = false;
if (RightCarabinerIdx >= 0 && RightCarabinerIdx < LineParticles.Num())		LineParticles[RightCarabinerIdx].bPinned = false;
}

void AWingActor::UpdateCarabinerPositions(FVector LeftCarabiner, FVector RightCarabiner){
	LeftCarabinerPos = LeftCarabiner;
RightCarabinerPos = RightCarabiner;
}

float AWingActor::GetSpreadPercent() const{
	if (Particles.Num() == 0) return 0.0f;
float MinY = 1e9, MaxY = -1e9;
for (const auto& P : Particles)	{
		MinY = FMath::Min(MinY, P.Pos.Y);
MaxY = FMath::Max(MaxY, P.Pos.Y);
}
	float WingSpanCm = FMath::Sqrt(WingModel.WingArea * WingModel.AR) * 100.0f;
	return FMath::Clamp((MaxY - MinY) / WingSpanCm, 0.0f, 1.0f);
}

bool AWingActor::IsMostlySpread() const {
 return GetSpreadPercent() > 0.6f;
}

float AWingActor::GetGroundZ(FVector Pos) const{
	if (!GetWorld()) return 0.0f;
FHitResult Hit;
FCollisionQueryParams QueryParams;
QueryParams.AddIgnoredActor(this);
FVector Start = Pos + FVector(0, 0, 200.0f);
FVector End = Pos - FVector(0, 0, 1000.0f);
if (GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, QueryParams))		return Hit.Location.Z;
return 0.0f;
}

// ============================================================
// COMMON HELPERS — extracted from duplicated code across functions
// ============================================================

AWingActor::FPilotFrame AWingActor::GetPilotFrame() const
{
	FPilotFrame Frame;
	if (!ConnectedParamotor) return Frame;
	APawn* Pawn = Cast<APawn>(ConnectedParamotor->GetAttachParentActor());
	if (!Pawn) return Frame;

	Frame.Velocity = Pawn->GetVelocity();
	Frame.Forward = Pawn->GetActorForwardVector();
	Frame.Forward.Z = 0;
	Frame.Forward.Normalize();
	Frame.Right = Pawn->GetActorRightVector();
	Frame.Right.Z = 0;
	Frame.Right.Normalize();
	FVector HorizVel(Frame.Velocity.X, Frame.Velocity.Y, 0.0f);
	Frame.SpeedMs = HorizVel.Size() / CM_PER_M;
	Frame.bValid = true;
	return Frame;
}

void AWingActor::UpdateDynamicWind(float DeltaTime, float GustAmplitude)
{
	// Unified wind update — replaces duplicated code in UpdateInflation and UpdateFlight
	// Gusts are fully handled by the von Kármán/Dryden turbulence model below.
	// No separate sine-based gusts — the Dryden filter produces realistic
	// low-frequency wind variations with proper spectral content.
	float GustTotal = 0.0f;

	// === VON KÁRMÁN TURBULENCE (Dryden approximation) ===
	// The von Kármán spectrum describes atmospheric turbulence energy distribution:
	//   Φ(Ω) = σ² × L / (π × (1 + (1.339 × L × Ω)²)^(5/6))
	// where σ = turbulence intensity, L = scale length, Ω = spatial frequency.
	//
	// The Dryden approximation implements this as first-order filters on white noise:
	//   h(s) = σ × √(2L/V) × 1 / (1 + L·s/V)
	// where V = airspeed (m/s), L = scale length (m), σ = intensity (m/s)
	//
	// This produces turbulence that:
	// - Has proper temporal correlation (not independent frame-to-frame)
	// - Builds up and decays naturally (gusts ramp up, not instant)
	// - Has realistic frequency content (more low-freq energy than high-freq)
	// - Matches real atmospheric turbulence spectra
	//
	// Three independent axes: longitudinal, lateral, vertical
	// Each gets its own white noise input filtered through the Dryden filter.

	float BaseWind = 0.0f;  // No fake default wind
	if (GetWorld() && GetWorld()->GetGameInstance<UParamotorGameInstance>())
	{
		BaseWind = FMath::Max(0.0f, GetWorld()->GetGameInstance<UParamotorGameInstance>()->WindSpeed);
	}

	// Turbulence intensity: σ in m/s
	// Real atmospheric turbulence: σ = gust_amplitude × (V / V_ref)^0.5
	// At moderate wind (6 m/s) with 2 m/s gusts: σ ≈ 2 m/s
	// The gust amplitude IS the turbulence intensity (by definition)
	TurbIntensity = GustAmplitude;
	// Scale length: larger at altitude (bigger eddies), smaller near ground
	// Typical: 200m at low altitude, up to 500m at high altitude
	APawn* Pawn = Cast<APawn>(GetAttachParentActor());
	if (Pawn)
	{
		float AltM = (Pawn->GetActorLocation().Z - GetGroundZ(Pawn->GetActorLocation())) / CM_PER_M;
		TurbScaleLength = FMath::Lerp(150.0f, 500.0f, FMath::Clamp(AltM / 1000.0f, 0.0f, 1.0f));
	}

	// Airspeed for filter time constant (use wind speed as the "V" in the filter)
	float Vfilter = FMath::Max(2.0f, BaseWind);  // m/s, minimum 2 to avoid div by zero

	// Dryden filter time constant: τ = L / V
	// Real Dryden model: lateral and vertical scale lengths are smaller than
	// longitudinal (lateral ≈ 0.5× longitudinal, vertical ≈ 0.5× at low altitude)
	// This is because crosswind/vertical eddies are smaller than along-wind eddies
	float ScaleLat = TurbScaleLength * 0.5f;   // Lateral scale length (real Dryden)
	float ScaleVert = TurbScaleLength * 0.5f;  // Vertical scale length (real Dryden)
	float TauLong = TurbScaleLength / Vfilter;        // Longitudinal
	float TauLat = ScaleLat / Vfilter;                // Lateral (smaller eddies)
	float TauVert = ScaleVert / Vfilter;              // Vertical (smaller eddies)

	// White noise input (Gaussian approximation: sum of uniform randoms)
	auto WhiteNoise = []() -> float {
		return (FMath::FRand() + FMath::FRand() + FMath::FRand() - 1.5f) * 0.8165f;
	};

	// Dryden filter: x(t+dt) = x(t) × exp(-dt/τ) + σ × √(2/τ) × √dt × white_noise
	// This is the exact discrete-time implementation of the first-order filter
	// shaped to match the von Kármán spectrum.
	float ExpLong = FMath::Exp(-DeltaTime / FMath::Max(0.01f, TauLong));
	float ExpLat = FMath::Exp(-DeltaTime / FMath::Max(0.01f, TauLat));
	float ExpVert = FMath::Exp(-DeltaTime / FMath::Max(0.01f, TauVert));

	float GainLong = TurbIntensity * FMath::Sqrt(2.0f / FMath::Max(0.01f, TauLong)) * FMath::Sqrt(DeltaTime);
	float GainLat = TurbIntensity * FMath::Sqrt(2.0f / FMath::Max(0.01f, TauLat)) * FMath::Sqrt(DeltaTime);
	float GainVert = TurbIntensity * FMath::Sqrt(2.0f / FMath::Max(0.01f, TauVert)) * FMath::Sqrt(DeltaTime);

	TurbLongState = TurbLongState * ExpLong + GainLong * WhiteNoise();
	TurbLatState = TurbLatState * ExpLat + GainLat * WhiteNoise();
	TurbVertState = TurbVertState * ExpVert + GainVert * WhiteNoise();

	// Combine into a scalar turbulence for wind speed variation.
	// WindTurbulence represents variation in wind speed along the wind direction.
	// The longitudinal component (TurbLongState) IS that variation — it's already
	// von Kármán filtered, smooth, and naturally signed. Using the magnitude with
	// Sign() creates discontinuous jumps when the component crosses zero.
	WindTurbulence = TurbLongState;  // Smooth, signed, physically correct

	// Direction shift: from lateral turbulence state (physical, not sine)
	// The lateral turbulence component naturally shifts the wind direction
	WindDirShift = TurbLatState / FMath::Max(1.0f, BaseWind);  // Ratio of lateral to base wind

	// ASSIGN wind speed each frame (not accumulate — gusts are oscillations, not accelerations)
	CurrentWindSpeed = FMath::Max(0.0f, BaseWind + GustTotal + WindTurbulence);  // No cap - physical
}

void AWingActor::Tick(float DeltaTime){
	Super::Tick(DeltaTime);
if (WingState == EWingState::InBag) return;
SimulateCloth(DeltaTime);
SimulateLines(DeltaTime);
// When connected, wing follows player via line tension
	// (Position dragging is now handled in SimulateLines via direct constraints)
	// Inflated/Flying states handle their own rotation in SimulateCloth
	if (bRisersConnected && WingState != EWingState::Inflated && WingState != EWingState::Flying)	{
		FVector ActorLoc = GetActorLocation();
FVector CarabMidWorld = (LeftCarabinerPos + RightCarabinerPos) * 0.5f;
// === WING YAW: WIND-DRIVEN, NOT PILOT-COUPLED ===
// The wing has its own yaw dynamics. It weathervanes into the wind.
// The pilot turning does NOT rotate the wing — the wing stays pointed into wind.
// This is what makes reverse launch possible: pilot turns to face the wing,
// the wing stays pointed upwind, and the lines cross.
{
    // Wind direction yaw (where wind blows TOWARD)
    FVector WindDir2D = WindDirection;
    WindDir2D.Z = 0.0f;
    WindDir2D.Normalize();
    float WindYaw = FMath::Atan2(WindDir2D.Y, WindDir2D.X) * 180.0f / PI;

    // For Inflating/Inflated states, KiteYawDrift is updated by the kiting physics
    // in SimulateCloth. It's an offset from the wind-direction base.
    // For Connected state, we compute weathervaning here.
    if (WingState == EWingState::Connected)
    {
        // Weathervane: target yaw = wind direction (wing +X points downwind)
        float YawError = WindYaw - WingGroundYaw;
        while (YawError > 180.0f) YawError -= 360.0f;
        while (YawError < -180.0f) YawError += 360.0f;

        // Weathervane moment: proportional to yaw error x wind speed^2
        float WindMs = FMath::Max(0.0f, CurrentWindSpeed);
        float DynPressure = 0.5f * AIR_DENSITY_SEA_LEVEL * WindMs * WindMs;
        float WingspanM = FMath::Sqrt(WingModel.WingArea * WingModel.AR);
        float WingMassKg = WingModel.WingWeight;
        float ApparentMassKg = AIR_DENSITY_SEA_LEVEL * WingModel.WingArea * (WingspanM * 0.5f) * CellFillFactor * 0.5f;  // Potential flow added mass
        float EffectiveMassKg = WingMassKg + ApparentMassKg;
        float WingInertiaYaw = EffectiveMassKg * (WingspanM * 0.5f) * (WingspanM * 0.5f);

        // Weathervane moment from crosswind component on the wing body
        // C_N_beta ≈ 0.05 per radian for paraglider (same as kiting physics)
        float WeathervaneMoment = 0.05f * DynPressure * WingModel.WingArea * WingspanM * FMath::Sin(YawError * PI / 180.0f);
        float WeathervaneAccel = (WingInertiaYaw > 0.1f) ? WeathervaneMoment / WingInertiaYaw * 180.0f / PI : 0.0f;

        // Aerodynamic yaw damping: c_yaw = 0.5 × ρ × V × S × b² / I_yaw
        // Same formula as kiting physics (WingClothSim.cpp line 842)
        float YawDampCoeff = (WingInertiaYaw > 0.1f)
            ? 0.5f * AIR_DENSITY_SEA_LEVEL * WindMs * WingModel.WingArea * WingspanM * WingspanM / WingInertiaYaw
            : 0.0f;
        float YawDamping = -WingGroundYawRate * YawDampCoeff;

        // Integrate yaw dynamics (no clamp — aerodynamic damping bounds the rate)
        WingGroundYawRate += (WeathervaneAccel + YawDamping) * DeltaTime;
        WingGroundYaw += WingGroundYawRate * DeltaTime;

        // Sync KiteYawDrift for compatibility (0 = aligned with wind)
        KiteYawDrift = WingGroundYaw - WindYaw;
        while (KiteYawDrift > 180.0f) KiteYawDrift -= 360.0f;
        while (KiteYawDrift < -180.0f) KiteYawDrift += 360.0f;
    }
    else
    {
        // Inflating/Inflated: KiteYawDrift is updated by kiting physics in SimulateCloth
        WingGroundYaw = WindYaw + KiteYawDrift;
    }

    // Blend with direction-to-wing when wing is far from carabiner
    FVector ToWing = GetActorLocation() - CarabMidWorld;
    ToWing.Z = 0.0f;
    float ToWingDist = ToWing.Size();
    float DesiredYaw = WingGroundYaw;
    if (ToWingDist > LINE_LENGTH_CM * 0.1f)
    {
        float BlendWeight = FMath::Clamp((ToWingDist - LINE_LENGTH_CM * 0.1f) / (LINE_LENGTH_CM * 0.3f), 0.0f, 0.5f);
        float ToWingYaw = FMath::Atan2(ToWing.Y, ToWing.X) * 180.0f / PI;
        float YawDiff = ToWingYaw - WingGroundYaw;
        while (YawDiff > 180.0f) YawDiff -= 360.0f;
        while (YawDiff < -180.0f) YawDiff += 360.0f;
        DesiredYaw = WingGroundYaw + YawDiff * BlendWeight;
    }
    // === PITCH DURING INFLATION ===
    // Research: pitch evolves 0° → 15° (cell fill) → 70° (rapid rise) → 90° (overhead)
    // CRITICAL: Wing mesh has leading edge at -X. Need NEGATIVE pitch to lift leading edge UP.
    // Pitch is driven by pendulum angle (angle from vertical = 0° overhead, 90° on ground)
    float DesiredPitch = 0.0f;
    if (WingState == EWingState::Inflating)
    {
        // Pendulum angle: 90° on ground (flat), 0° at zenith (upright)
        // Wing pitch = -(90° - pendAngle) = pendAngle - 90°
        // On ground: pendAngle=90°, pitch=0° (flat)
        // At zenith: pendAngle=0°, pitch=-90° (upright)
        FVector CarabMidPitch = (LeftCarabinerPos + RightCarabinerPos) * 0.5f;
        FVector ToWingPitch = GetActorLocation() - CarabMidPitch;
        float HorizDistPitch = FVector(ToWingPitch.X, ToWingPitch.Y, 0.0f).Size();
        float HeightAboveCarabPitch = FMath::Max(0.0f, ToWingPitch.Z);
        float PendAnglePitch = FMath::Atan2(HorizDistPitch, FMath::Max(1.0f, HeightAboveCarabPitch));
        // Pitch from pendulum angle: 0° flat → -90° upright
        DesiredPitch = -(PI/2.0f - PendAnglePitch) * 180.0f / PI;
        // A-line pull lifts the leading edge — real physics
        // When pilot pulls A-risers, the LE is physically lifted up.
        // This angles the intakes into the wind, which is what causes inflation.
        // The pitch angle comes from the A-line force being applied to the LE,
        // not the whole wing. Only applies while A-lines are actively pulled.
        if (bLiftingALines && ALineFadeFactor > 0.01f)
        {
            float GZp = GetGroundZ(GetActorLocation());
            float HAG = GetActorLocation().Z - GZp;
            float HeightFraction = FMath::Clamp(HAG / LINE_LENGTH_CM, 0.0f, 1.0f);
            // LE lift angle: ~25° at ground, fades as wing rises (pilot can't reach as high)
            DesiredPitch -= 25.0f * (1.0f - HeightFraction) * ALineFadeFactor;
        }
        // Cell fill adds pitch (pressurized airfoil naturally angles up)
        DesiredPitch -= CellFillFactor * 5.0f;
        DesiredPitch = FMath::Clamp(DesiredPitch, -90.0f, 0.0f);
    }
    float CurrentYaw = GetActorRotation().Yaw;
    float YawDiff2 = DesiredYaw - CurrentYaw;
    while (YawDiff2 > 180.0f) YawDiff2 -= 360.0f;
    while (YawDiff2 < -180.0f) YawDiff2 += 360.0f;
    // Yaw rate from KiteYawVel (integrated from aerodynamic moments in SimulateCloth)
    // No arbitrary clamp — the yaw dynamics already have physical damping
    float RotSpeed = FMath::Abs(KiteYawVel);
    float RotStep = FMath::Clamp(YawDiff2, -RotSpeed * DeltaTime, RotSpeed * DeltaTime);
    // Pitch: use KitePitchOvershoot (integrated from pendulum geometry in SimulateCloth)
    float CurrentPitch = GetActorRotation().Pitch;
    float PitchStep = FMath::FInterpTo(CurrentPitch, DesiredPitch + KitePitchOvershoot, DeltaTime, 8.0f) - CurrentPitch;
    if (FMath::Abs(RotStep) > 0.1f || FMath::Abs(PitchStep) > 0.1f)
    {
        FRotator NewRot = GetActorRotation();
        NewRot.Yaw += RotStep;
        NewRot.Pitch += PitchStep;
        SetActorRotation(NewRot);
        // NOTE: Do NOT rotate particles manually — they are in actor-local space.
        // SetActorRotation already handles the visual transform.
    }
}
	// NOTE: Brake visual deformation is handled entirely by ApplyInflatedShape()
	// in WingVisuals.cpp. Do NOT modify particle Z here — it gets overwritten.
}
	UpdateInflation(DeltaTime);
UpdateFlight(DeltaTime);
	// Debug: log position at end of Tick
	if (WingState == EWingState::Inflating)
	{
		static float EndLogTimer = 0.0f;
		EndLogTimer += DeltaTime;
		if (EndLogTimer > 0.2f)
		{
			EndLogTimer = 0.0f;
			UE_LOG(LogTemp, Warning, TEXT("END_TICK: posZ=%.0f velZ=%.0f state=%d"), GetActorLocation().Z, WingVel.Z, (int32)WingState);
		}
	}
UpdateCanopyMesh();
UpdateLineMesh();
}

