#include "WingActor.h"
#include "ParamotorActor.h"
#include "ParamotorGameInstance.h"
#include "AtmosphereModel.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "ProceduralMeshComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "MacParaChargerLines.h"
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

void AWingActor::LoadMaterials()
{
	// Load canopy material — use engine VertexColorMaterial (shows vertex colors as emissive)
	// This is in EngineDebugMaterials, not EngineMaterials
	CanopyMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineDebugMaterials/VertexColorMaterial.VertexColorMaterial"));
	if (!CanopyMaterial)
	{
		// Fallback to M_Canopy (project material with VertexColor node)
		CanopyMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_Canopy.M_Canopy"));
	}
	if (!CanopyMaterial)
	{
		CanopyMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/EmissiveMeshMaterial.EmissiveMeshMaterial"));
		UE_LOG(LogTemp, Warning, TEXT("WingActor: All canopy materials not found, using EmissiveMeshMaterial fallback"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("WingActor: Loaded canopy material"));
	}
	// Cache line mesh material — use VertexColorMaterial so per-vertex line colors show
	LineMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineDebugMaterials/VertexColorMaterial.VertexColorMaterial"));
	if (!LineMaterial)
		LineMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/EmissiveMeshMaterial.EmissiveMeshMaterial"));
}

void AWingActor::BeginPlay(){
	Super::BeginPlay();
	// Cache game instance (avoids per-frame GetWorld()->GetGameInstance calls)
	if (GetWorld())
		CachedGameInstance = GetWorld()->GetGameInstance<UParamotorGameInstance>();

	LoadMaterials();

	// === MacPara Charger 25M — the only wing ===
	InitTestWingCloth();
	InitTestWingLines();

	// Start deflated on the ground (loose, crumpled)
	WingState = EWingState::Spread;
	SetSpreadState();
	TestWingInflation = 0.0f;
	SetTestWingDeflated();

	// Show the canopy and line meshes (real MacPara data)
	if (CanopyMesh) CanopyMesh->SetVisibility(true);
	if (LineMesh) LineMesh->SetVisibility(true);

	// Create mesh sections ONCE (performance: avoids recreating 88k verts every frame)
	InitTestWingMeshes();

	UE_LOG(LogTemp, Warning, TEXT("WingActor: MacPara Charger initialized — %d cloth particles, %d line particles"),
		Particles.Num(), TestLineParticles.Num());
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
	LeftBrakeAmount = FMath::Clamp(Amount, 0.0f, 1.0f);
	LeftBrakeTarget = LeftBrakeAmount;
}

void AWingActor::SetRightBrake(float Amount) {
	RightBrakeAmount = FMath::Clamp(Amount, 0.0f, 1.0f);
	RightBrakeTarget = RightBrakeAmount;
}

void AWingActor::EnterFlightMode(ACharacter* Char)
{
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
}

void AWingActor::ForceTakeoff()
{
	if (WingState != EWingState::Inflated) return;

	APawn* Pawn = nullptr;
	if (ConnectedParamotor)
		Pawn = Cast<APawn>(ConnectedParamotor->GetAttachParentActor());
	if (!Pawn) return;

	ACharacter* Char = Cast<ACharacter>(Pawn);
	if (!Char) return;

	WingState = EWingState::Flying;
	bIsAirborne = true;

	EnterFlightMode(Char);

	// DEBUG SHORTCUT — editor/dev only. Real takeoff goes through ground handling.
#if WITH_EDITOR || UE_BUILD_DEVELOPMENT
	FVector PawnLoc = Pawn->GetActorLocation();
	float GroundZ = GetCachedGroundZ(PawnLoc);
	PawnLoc.Z = GroundZ + 500.0f;  // 5m — just enough to clear ground collision
	Pawn->SetActorLocation(PawnLoc, false);
	Char->GetCharacterMovement()->Velocity = FVector(0.0f, 0.0f, 100.0f);  // 1 m/s up
	PendulumAngle = 0.0f;
	PendulumAngularVel = 0.0f;  // Start at rest
#endif
	PilotRollAngle = 0.0f;
	PilotRollRate = 0.0f;
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

// === Functions moved from deleted procedural wing files ===

void AWingActor::SetSpreadState(){
	if (CanopyMesh) CanopyMesh->SetVisibility(true);
	if (LineMesh) LineMesh->SetVisibility(true);
	if (BagMesh) BagMesh->SetVisibility(false);
}

float AWingActor::GetTrimmerAoAOffset() const
{
	float TrimmerTravelCm = 5.0f;
	float WingspanM = FMath::Sqrt(WingModel.WingArea * WingModel.AR);
	float MeanChordCm = (WingspanM > 0.0f) ? (WingModel.WingArea / WingspanM) * CM_PER_M : 200.0f;
	float ADistanceCm = MeanChordCm * 0.6f;
	return -TrimmerPosition * FMath::Atan2(TrimmerTravelCm, ADistanceCm) * RAD_TO_DEG;
}

float AWingActor::GetTrimmerPitchOffset() const
{
	return GetTrimmerAoAOffset();
}

float AWingActor::GetTrimmerCollapseRisk() const
{
	float TrimAoA = WingModel.TrimAoA;
	float StallAoA = WingModel.StallAoA;
	float CurrentAoA = TrimAoA + GetTrimmerAoAOffset();
	float MarginToStall = StallAoA - CurrentAoA;
	float TrimMargin = StallAoA - TrimAoA;
	if (TrimMargin < 1.0f) return 1.0f;
	float RiskRatio = TrimMargin / FMath::Max(1.0f, MarginToStall);
	return RiskRatio * RiskRatio;
}

FString AWingActor::GetTrimmerLabel() const
{
	if (TrimmerPosition > 0.7f) return TEXT("FAST");
	if (TrimmerPosition > 0.3f) return TEXT("FAST+");
	if (TrimmerPosition > 0.05f) return TEXT("FAST-");
	if (TrimmerPosition < -0.7f) return TEXT("SLOW");
	if (TrimmerPosition < -0.3f) return TEXT("SLOW+");
	if (TrimmerPosition < -0.05f) return TEXT("SLOW-");
	return TEXT("NEUTRAL");
}

void AWingActor::ToggleSeat()
{
	bSeated = !bSeated;
}

void AWingActor::AdjustHangPoint(float Delta)
{
	HangPointOffset = FMath::Clamp(HangPointOffset + Delta, -1.0f, 1.0f);
}

float AWingActor::GetTakeoffChance() const
{
	float WeightRatio = WingModel.MaxTakeoffWeight > 0.0f ? PilotWeight / WingModel.MaxTakeoffWeight : 1.0f;
	if (WeightRatio >= 1.0f) return 0.0f;
	float WeightFactor = FMath::Clamp(1.0f - WeightRatio * 0.8f, 0.0f, 1.0f);
	float CurrentAirspeed = 0.0f;
	if (ConnectedParamotor)
	{
		APawn* Pawn = Cast<APawn>(ConnectedParamotor->GetAttachParentActor());
		if (Pawn)
		{
			FVector Vel = Pawn->GetVelocity();
			FVector Wind = GetWindVector();
			FVector Apparent = Wind - Vel;
			FVector Fwd = Pawn->GetActorForwardVector();
			Fwd.Z = 0;
			Fwd.Normalize();
			float Headwind = -FVector::DotProduct(Apparent, Fwd) / CM_PER_M;
			CurrentAirspeed = FMath::Max(0.0f, Headwind);
		}
	}
	float StallDenom = AIR_DENSITY_SEA_LEVEL * WingModel.WingArea * WingModel.CLMax;
	float TakeoffSpeed = StallDenom > 0.0f ? FMath::Sqrt(2.0f * (PilotWeight + WingModel.WingWeight) * GRAVITY_MS2 / StallDenom) : 5.4f;
	float SpeedFactor = FMath::Clamp(CurrentAirspeed / TakeoffSpeed, 0.0f, 1.5f);
	float ThrottleFactor = (ThrottleAmount > 0.25f) ? 1.0f : ThrottleAmount / 0.25f;
	float Chance = WeightFactor * SpeedFactor * ThrottleFactor * PERCENT_FACTOR;
	return FMath::Clamp(Chance, 0.0f, 100.0f);
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
	float InitialWingYaw = FMath::Atan2(WindDirection.Y, WindDirection.X) * RAD_TO_DEG;
	UE_LOG(LogTemp, Warning, TEXT("WingActor: Risers connected — L=%s R=%s WindDir=%s WingYaw=%.1f°"),		*LeftCarabiner.ToString(), *RightCarabiner.ToString(), *WindDirection.ToString(), InitialWingYaw);
}

void AWingActor::DisconnectRisers(){
	// Reset character gravity and movement mode BEFORE nulling ConnectedParamotor
if (ConnectedParamotor)	{
		APawn* Pawn = Cast<APawn>(ConnectedParamotor->GetAttachParentActor());
if (Pawn)		{
			ACharacter* Char = Cast<ACharacter>(Pawn);
if (Char && Char->GetCharacterMovement())			{
				Char->GetCharacterMovement()->GravityScale = 1.0f;
			// Flight uses MOVE_None (not MOVE_Flying), so IsFlying() is always false.
			// Restore appropriate movement mode based on ground contact.
			if (Char->GetCharacterMovement()->MovementMode == MOVE_None)
			{
				if (Char->GetCharacterMovement()->IsMovingOnGround())
					Char->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
				else
					Char->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
			}
}
}
}
	bRisersConnected = false;
ConnectedParamotor = nullptr;
if (WingState == EWingState::Connected || WingState == EWingState::Inflating ||
	WingState == EWingState::Inflated || WingState == EWingState::Flying)
{
	WingState = EWingState::Spread;
	// Reset flight state to avoid stale values on next connect
	bIsAirborne = false;
	InflationProgress = 0.0f;
}
}

void AWingActor::UpdateCarabinerPositions(FVector LeftCarabiner, FVector RightCarabiner){
	LeftCarabinerPos = LeftCarabiner;
RightCarabinerPos = RightCarabiner;
}

FVector AWingActor::GetLeftBrakeHandleWorldPos() const
{
	// Brake handles hang below the carabiners at roughly riser length
	return LeftCarabinerPos - FVector(0, 0, RISER_LENGTH_CM);
}

FVector AWingActor::GetRightBrakeHandleWorldPos() const
{
	return RightCarabinerPos - FVector(0, 0, RISER_LENGTH_CM);
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

float AWingActor::GetCachedGroundZ(FVector Pos)
{
	// If close to cached wing position, reuse the cached value
	if (bGroundZCachedThisTick)
	{
		FVector WingPos = GetActorLocation();
		float DistSq = FVector::DistSquared2D(Pos, WingPos);
		if (DistSq < 10000.0f)  // Within 100cm of wing position
			return CachedWingGroundZ;
	}
	// Otherwise do a fresh trace and cache it
	float Z = GetGroundZ(Pos);
	CachedWingGroundZ = Z;
	bGroundZCachedThisTick = true;
	return Z;
}

// ============================================================
// UNIFIED WIND ACCESS — single source of truth for all consumers
// ============================================================

FVector AWingActor::GetShiftedWindDirection() const
{
	// Total direction shift = turbulence shift + Ekman spiral veering
	float TotalShift = WindDirShift + EkmanShift;
	return WindDirection.RotateAngleAxis(TotalShift * RAD_TO_DEG, FVector(0, 0, 1));
}

FVector AWingActor::GetWindVector() const
{
	return GetShiftedWindDirection() * CurrentWindSpeed * CM_PER_M;
}

float AWingActor::GetWindHeadingDeg() const
{
	FVector Shifted = GetShiftedWindDirection();
	Shifted.Z = 0.0f;
	if (Shifted.Size2D() < 0.01f) return 0.0f;
	return FMath::Atan2(Shifted.Y, Shifted.X) * RAD_TO_DEG;
}

// ============================================================
// COMMON HELPERS — extracted from duplicated code across functions
// ============================================================

void AWingActor::UpdateDynamicWind(float DeltaTime, float GustAmplitude)
{
	// ============================================================
	// REAL ATMOSPHERIC WIND MODEL
	// Implements: Pasquill stability, log-law wind profile with
	// Monin-Obukhov correction, Ekman spiral direction veering,
	// MIL-HDBK-1797B Dryden turbulence with proper scale lengths
	// and 2nd-order lateral/vertical filters, sea breeze, and
	// stability-dependent turbulence intensity.
	// ============================================================

	float BaseWind = 0.0f;
	float TempC = 20.0f;
	float Humidity = 60.0f;
	float CloudCover = 0.25f;
	float TimeH = 12.0f;
	if (CachedGameInstance)
	{
		BaseWind = FMath::Max(0.0f, CachedGameInstance->WindSpeed);
		TempC = CachedGameInstance->Temperature;
		Humidity = CachedGameInstance->Humidity;
		CloudCover = CachedGameInstance->CloudCover;
		TimeH = CachedGameInstance->TimeOfDay;
	}

	// Get pilot position and altitude
	APawn* Pawn = nullptr;
	if (ConnectedParamotor)
		Pawn = Cast<APawn>(ConnectedParamotor->GetAttachParentActor());
	float AltM = 10.0f;  // Default reference height
	if (Pawn)
	{
		const FVector PawnLoc = Pawn->GetActorLocation();
		float GroundZ = GetCachedGroundZ(PawnLoc);
		AltM = FMath::Max(0.5f, (PawnLoc.Z - GroundZ) / CM_PER_M);
	}

	// === 1. PASQUILL STABILITY CLASS ===
	CurrentStability = ComputePasquillClass(TempC, BaseWind, TimeH, CloudCover);

	// === 2. BOUNDARY LAYER HEIGHT ===
	BoundaryLayerHeightM = ComputeBoundaryLayerHeight(CurrentStability, TimeH);

	// === 3. DEW POINT & CLOUDBASE ===
	DewPointC = ComputeDewPoint(TempC, Humidity);
	CloudbaseAltitudeM = ComputeCloudbase(TempC, DewPointC);

	// === 4. INVERSION ALTITUDE ===
	InversionAltitudeM = ComputeInversionAltitude(CurrentStability, TimeH);

	// === 5. MONIN-OBUKHOV LENGTH ===
	MoninObukhovLength = EstimateMoninObukhovLength(CurrentStability);

	// === 6. FRICTION VELOCITY u* ===
	// From log law: u* = κ * U_ref / [ln(z_ref/z0) - ψm(z_ref/L)]
	float Z0 = AtmosConst::Z0_DEFAULT;  // Coastal grass field
	float ZetaRef = AtmosConst::REF_HEIGHT_M / MoninObukhovLength;
	FrictionVelocityUStar = ComputeFrictionVelocity(BaseWind, Z0, ZetaRef);

	// === 7. WIND SPEED PROFILE (log law with stability correction) ===
	// U(z) = (u*/κ) * [ln(z/z0) - ψm(z/L) + ψm(z0/L)]
	float ZetaZ = AltM / MoninObukhovLength;
	float ZetaZ0 = Z0 / MoninObukhovLength;
	float ProfiledWind = LogWindProfile(AltM, FrictionVelocityUStar, Z0, ZetaZ, ZetaZ0);

	// Above the boundary layer, wind transitions to geostrophic (free atmosphere)
	// The BL top has the strongest wind; above that, wind may increase or decrease
	// depending on pressure patterns. We model a gradual increase above BL.
	if (AltM > BoundaryLayerHeightM)
	{
		// Above BL: wind increases slowly (geostrophic is typically stronger than surface)
		float AboveBL = (AltM - BoundaryLayerHeightM) / 1000.0f;  // km above BL
		float GeoFactor = 1.0f + FMath::Clamp(AboveBL * 0.1f, 0.0f, 0.5f);  // +10% per km
		ProfiledWind *= GeoFactor;
	}

	// === 8. EKMAN SPIRAL — direction veering with altitude ===
	// Wind veers (turns clockwise in NH) with altitude through the BL.
	// Total veering: ~20° from surface to BL top.
	EkmanShift = EkmanDirectionShift(AltM, BoundaryLayerHeightM);

	// === 9. SEA BREEZE (coastal) ===
	// Westerly, RI is coastal. Sea breeze adds onshore flow during the day.
	// Water temperature: ocean has high heat capacity, changes slowly.
	// At night, water stays warmer than air (air cools faster).
	bool bIsNightWind = (TimeH < 6.0f || TimeH > 18.0f);
	float WaterTempC = FMath::Lerp(12.0f, 20.0f, FMath::Clamp((TempC - 10.0f) / 25.0f, 0.0f, 1.0f));
	if (bIsNightWind) WaterTempC = FMath::Max(WaterTempC, TempC + 3.0f);  // Water warmer at night
	SeaBreezeMs = ComputeSeaBreeze(TempC, TimeH, WaterTempC);
	// Sea breeze only affects the lower layers (100-500m depth)
	float SeaBreezeDepth = ComputeSeaBreezeDepth(TempC - WaterTempC);
	if (AltM > SeaBreezeDepth)
		SeaBreezeMs *= FMath::Clamp(1.0f - (AltM - SeaBreezeDepth) / 200.0f, 0.0f, 1.0f);
	// Add sea breeze to the profiled wind (positive = sea breeze, negative = land breeze)
	// CurrentWindSpeed is clamped to non-negative below, so land breeze just reduces wind
	ProfiledWind += SeaBreezeMs;

	// === 10. DRYDEN TURBULENCE (MIL-HDBK-1797B) ===
	// Scale lengths are altitude-dependent per MIL-HDBK-1797B.
	FDrydenScales Scales = ComputeDrydenScales(AltM);
	float Lu = Scales.Lu;
	float Lv = Scales.Lv;
	float Lw = Scales.Lw;

	// Turbulence intensity from stability, wind, and friction velocity
	FTurbulenceIntensity Sigma = ComputeTurbulenceIntensity(
		BaseWind, AltM, CurrentStability, FrictionVelocityUStar, GustAmplitude, BoundaryLayerHeightM);
	TurbSigmaU = Sigma.SigmaU;
	TurbSigmaV = Sigma.SigmaV;
	TurbSigmaW = Sigma.SigmaW;
	TurbIntensity = TurbSigmaU;  // For backward compat

	// Filter speed: true airspeed (how fast we traverse eddies)
	float PilotSpeedMs = 0.0f;
	if (Pawn)
		PilotSpeedMs = Pawn->GetVelocity().Size() / CM_PER_M;
	float TrueAirspeedMs = FMath::Sqrt(BaseWind * BaseWind + PilotSpeedMs * PilotSpeedMs);
	float Vfilter = FMath::Max(2.0f, TrueAirspeedMs);

	// White noise (Gaussian via CLT: 12 uniform samples → mean 0, variance 1)
	auto WhiteNoise = []() -> float {
		float W = 0.0f;
		for (int32 i = 0; i < 12; ++i) W += FMath::FRand();
		return W - 6.0f;
	};

	// --- Longitudinal: 1st-order filter ---
	// H_u(s) = σ_u * √(2L_u/(πV)) * 1/(1 + L_u*s/V)
	float TauU = Lu / Vfilter;
	float ExpU = FMath::Exp(-DeltaTime / FMath::Max(0.01f, TauU));
	float GainU = TurbSigmaU * FMath::Sqrt(2.0f / FMath::Max(0.01f, TauU)) * FMath::Sqrt(DeltaTime);
	TurbLongState = TurbLongState * ExpU + GainU * WhiteNoise();

	// --- Lateral: 2nd-order filter (MIL-HDBK-1797B) ---
	// H_v(s) = σ_v * √(2L_v/(πV)) * (1 + 2√3 * L_v/V * s) / (1 + 2L_v/V * s)²
	// Implementation: two cascaded 1st-order filters (denominator), with
	// the lead term approximated by feeding the input through both stages.
	// The cascaded filter gives the correct 2nd-order low-pass behavior.
	// The lead term (numerator) shapes high-frequency response but doesn't
	// change the DC gain, so we omit it for stability and correct variance.
	float TauV = Lv / Vfilter;
	float ExpV = FMath::Exp(-DeltaTime / FMath::Max(0.01f, TauV));
	float GainV = TurbSigmaV * FMath::Sqrt(2.0f / FMath::Max(0.01f, TauV)) * FMath::Sqrt(DeltaTime);
	// Stage 1: filter white noise
	TurbLatState2 = TurbLatState2 * ExpV + GainV * WhiteNoise();
	// Stage 2: filter stage 1 output (cascaded 1st-order = 2nd-order low-pass)
	TurbLatState = TurbLatState * ExpV + TurbLatState2 * (1.0f - ExpV);

	// --- Vertical: 2nd-order filter (same form as lateral) ---
	float TauW = Lw / Vfilter;
	float ExpW = FMath::Exp(-DeltaTime / FMath::Max(0.01f, TauW));
	float GainW = TurbSigmaW * FMath::Sqrt(2.0f / FMath::Max(0.01f, TauW)) * FMath::Sqrt(DeltaTime);
	TurbVertState2 = TurbVertState2 * ExpW + GainW * WhiteNoise();
	TurbVertState = TurbVertState * ExpW + TurbVertState2 * (1.0f - ExpW);

	// === 11. COMBINE TURBULENCE INTO WIND ===
	// Longitudinal turbulence modulates wind speed
	WindTurbulence = TurbLongState;

	// Lateral turbulence shifts wind direction (physically: crosswind gusts)
	// Clamp to ±20° to avoid unrealistic swings at low wind
	WindDirShift = FMath::Clamp(TurbLatState / FMath::Max(2.0f, BaseWind), -0.35f, 0.35f);

	// Final wind speed = profiled mean + turbulence
	CurrentWindSpeed = FMath::Max(0.0f, ProfiledWind + WindTurbulence);

	// === 12. OVERDEVELOPMENT RISK ===
	bOverdevelopmentRisk = ComputeOverdevelopmentRisk(TempC, Humidity, TimeH, CloudCover);

	// === 13. FLYABILITY ===
	CurrentFlyability = AssessFlyability(BaseWind, GustAmplitude, TempC, Humidity,
		TimeH, CloudCover, CurrentStability);

	// === 14. WIND DIRECTION DRIFT ===
	// Real wind direction slowly shifts over time (random walk).
	// Rate: ~5°/min in unstable conditions, ~1°/min in stable.
	if (BaseWind > 0.5f)
	{
		float DriftRate = (CurrentStability <= EPasquillStability::C_SlightlyUnstable) ? 0.083f : 0.017f;
		float DriftNoise = (FMath::FRand() - 0.5f) * 2.0f * DriftRate * DeltaTime;
		WindDirection = WindDirection.RotateAngleAxis(DriftNoise * RAD_TO_DEG, FVector(0, 0, 1));
		WindDirection.Z = 0.0f;
		WindDirection.Normalize();
	}
}

// ============================================================
// THERMAL SYSTEM — Real atmospheric thermal physics
// Implements: trigger temperature, lifecycle (bubble→column→decay),
// cloudbase from dew point, inversion capping, compensating subsidence,
// cloud street alignment, overdevelopment risk, blue vs cloud thermals.
// ============================================================

float AWingActor::ComputeThermalStrengthMultiplier() const
{
	if (!CachedGameInstance) return 0.0f;

	float Temp = CachedGameInstance->Temperature;
	float Clouds = CachedGameInstance->CloudCover;
	float TimeH = CachedGameInstance->TimeOfDay;
	float Humidity = CachedGameInstance->Humidity;

	// Use the real thermal climb rate computation
	float ThermalClimb = ComputeThermalClimbRate(Temp, BoundaryLayerHeightM, Clouds, TimeH, Humidity);

	// Normalize to 0-1 multiplier (5 m/s = full strength)
	return FMath::Clamp(ThermalClimb / 5.0f, 0.0f, 2.0f);
}

void AWingActor::SpawnThermal()
{
	if (!CachedGameInstance) return;

	float Temp = CachedGameInstance->Temperature;
	float Humidity = CachedGameInstance->Humidity;
	float Clouds = CachedGameInstance->CloudCover;
	float TimeH = CachedGameInstance->TimeOfDay;

	// Check if thermals can trigger at all
	if (!ThermalsCanTrigger(Temp, TimeH, Clouds)) return;

	// Compute current peak climb rate from real physics
	CurrentThermalClimbRate = ComputeThermalClimbRate(Temp, BoundaryLayerHeightM, Clouds, TimeH, Humidity);
	if (CurrentThermalClimbRate < 0.3f) return;  // Too weak to bother

	// Compute sink between thermals
	ThermalSinkMs = ComputeThermalSink(CurrentThermalClimbRate, TimeH);

	// Spawn thermal near the pilot
	APawn* Pawn = nullptr;
	if (ConnectedParamotor)
		Pawn = Cast<APawn>(ConnectedParamotor->GetAttachParentActor());
	if (!Pawn) return;

	FVector PawnLoc = Pawn->GetActorLocation();

	// Thermal spacing: 1.5-3× BL height (real atmospheric scaling)
	float Spacing = ComputeThermalSpacing(BoundaryLayerHeightM) * CM_PER_M;  // cm

	// Cloud street alignment: when wind > 5 m/s, thermals align in streets
	// parallel to the wind direction. We bias the spawn position along the wind.
	float WindSpeed = CurrentWindSpeed;
	FVector WindDir2D = GetShiftedWindDirection();
	WindDir2D.Z = 0.0f;
	if (WindDir2D.Size2D() < 0.01f) WindDir2D = FVector(1, 0, 0);
	WindDir2D.Normalize();

	float Angle = FMath::FRand() * 360.0f;
	float Dist = Spacing * (0.5f + FMath::FRand() * 0.5f);  // 0.5-1.0× spacing
	FVector Offset(FMath::Cos(FMath::DegreesToRadians(Angle)) * Dist,
		FMath::Sin(FMath::DegreesToRadians(Angle)) * Dist, 0.0f);

	// Cloud street: bias along wind direction if wind is strong enough
	if (WindSpeed > 5.0f)
	{
		float StreetBias = FMath::FRand() * Spacing * 0.8f;
		Offset += WindDir2D * StreetBias;
	}

	FThermal Thermal;
	Thermal.Center = PawnLoc + Offset;
	Thermal.Center.Z = GetCachedGroundZ(Thermal.Center);

	// Thermal radius: stronger thermals are narrower (real relationship)
	// ComputeThermalRadius returns cm directly (1500cm = 15m)
	Thermal.Radius = ComputeThermalRadius(CurrentThermalClimbRate);  // cm

	// Thermal strength: peak climb rate with some random variation
	Thermal.Strength = CurrentThermalClimbRate * (0.7f + FMath::FRand() * 0.3f);

	// Thermal base: starts at ground level
	Thermal.BaseAltitude = 0.0f;

	// Thermal top: minimum of cloudbase and inversion altitude
	// Blue thermals (low humidity, high cloudbase) stop at inversion
	// Cloud thermals stop at cloudbase
	float TopAlt = FMath::Min(CloudbaseAltitudeM, InversionAltitudeM);
	Thermal.TopAltitude = TopAlt * (0.7f + FMath::FRand() * 0.3f);  // 70-100% of top

	// Thermal lifetime: 3-10 minutes (stronger = longer)
	Thermal.MaxLifetime = ComputeThermalLifetime(CurrentThermalClimbRate);
	Thermal.Lifetime = Thermal.MaxLifetime;

	// Thermal drift: thermals drift with the wind at ~80% of wind speed
	// (thermals are buoyant columns that get advected by the mean flow)
	Thermal.DriftVel = CurrentWindSpeed * CM_PER_M * 0.8f;

	Thermal.bActive = true;

	Thermals.Add(Thermal);
	UE_LOG(LogTemp, Log, TEXT("Thermal spawned — climb=%.1f m/s, radius=%.0f m, top=%.0f m, life=%.0f s, sink=%.1f m/s"),
		Thermal.Strength, Thermal.Radius / CM_PER_M, Thermal.TopAltitude, Thermal.MaxLifetime, ThermalSinkMs);
}

void AWingActor::UpdateThermals(float DeltaTime)
{
	// Cloudbase is already computed in UpdateDynamicWind from dew point.
	// Inversion altitude is also computed there.

	// Spawn new thermals periodically
	ThermalStrengthMultiplier = ComputeThermalStrengthMultiplier();
	if (ThermalStrengthMultiplier > 0.05f)
	{
		// Spawn rate: more thermals in strong conditions
		// Real thermal spacing: 1.5-3× BL height. With BL=1500m, spacing=3750m.
		// A pilot can typically reach 2-3 thermals, so we keep 8-15 active.
		float SpawnInterval = FMath::Lerp(30.0f, 8.0f, ThermalStrengthMultiplier);
		ThermalSpawnTimer += DeltaTime;
		if (ThermalSpawnTimer >= SpawnInterval && Thermals.Num() < 15)
		{
			ThermalSpawnTimer = 0.0f;
			SpawnThermal();
		}
	}

	// Update existing thermals
	FVector WindDir2D = GetShiftedWindDirection();
	WindDir2D.Z = 0.0f;
	if (WindDir2D.Size2D() < 0.01f) WindDir2D = FVector(1, 0, 0);
	WindDir2D.Normalize();

	for (int32 i = Thermals.Num() - 1; i >= 0; i--)
	{
		FThermal& T = Thermals[i];

		// Drift with wind
		T.Center += WindDir2D * T.DriftVel * DeltaTime;

		// Age
		T.Lifetime -= DeltaTime;

		// Remove dead thermals
		if (T.Lifetime <= 0.0f)
		{
			Thermals.RemoveAt(i);
		}
	}
}

float AWingActor::GetThermalLiftAtPosition(FVector WorldPos, float AltitudeM)
{
	float TotalLift = 0.0f;

	for (const FThermal& T : Thermals)
	{
		if (!T.bActive) continue;

		// Check altitude range (thermals exist between base and top)
		if (AltitudeM < T.BaseAltitude || AltitudeM > T.TopAltitude) continue;

		// Distance from thermal center (horizontal only)
		float Dist = FVector::Dist2D(WorldPos, T.Center);

		// Outside thermal radius
		if (Dist > T.Radius) continue;

		// Strength profile: peak at center, cosine falloff to zero at edge
		float RadiusFraction = Dist / T.Radius;
		float CoreFactor = FMath::Cos(RadiusFraction * PI * 0.5f);
		CoreFactor = FMath::Max(0.0f, CoreFactor);

		// Altitude profile: strongest at ~1/3 height (real thermal profile)
		// Thermals accelerate from ground, peak in lower-middle, weaken toward top
		float AltFraction = (AltitudeM - T.BaseAltitude) / (T.TopAltitude - T.BaseAltitude);
		AltFraction = FMath::Clamp(AltFraction, 0.0f, 1.0f);
		// Real profile: peak at ~30% height, gradual decay above
		float AltFactor = FMath::Sin(AltFraction * PI);  // Smooth rise and fall
		// Bias toward lower portion (thermals are strongest low down)
		AltFactor = FMath::Pow(AltFactor, 0.7f);

		// Lifetime strength scale (thermal lifecycle)
		// Phase 1 (0-10%): bubble/trigger — ramp up
		// Phase 2 (10-90%): column/mature — full strength
		// Phase 3 (90-100%): decay/dilution — ramp down
		float AgeFraction = 1.0f - (T.Lifetime / T.MaxLifetime);
		float StrengthScale;
		if (AgeFraction < 0.1f)
			StrengthScale = AgeFraction / 0.1f;  // Ramp up (bubble phase)
		else if (AgeFraction > 0.9f)
			StrengthScale = (1.0f - AgeFraction) / 0.1f;  // Ramp down (decay phase)
		else
			StrengthScale = 1.0f;  // Full strength (column phase)

		TotalLift += T.Strength * CoreFactor * AltFactor * StrengthScale;
	}

	return TotalLift;  // m/s upward
}

float AWingActor::GetThermalSinkAtPosition(FVector WorldPos, float AltitudeM)
{
	// Compensating subsidence: air that rises in thermals must descend elsewhere.
	// The sink rate is proportional to thermal strength and inversely proportional
	// to the area outside thermals (mass conservation).
	//
	// Real values: 1-3 m/s in moderate conditions, 2-5 m/s in strong conditions.
	// Sink is strongest at mid-altitudes and near the edges of thermals.
	//
	// We only apply sink if the pilot is NOT in a thermal (lift < 0.5 m/s).
	float Lift = GetThermalLiftAtPosition(WorldPos, AltitudeM);
	if (Lift > 0.5f) return 0.0f;  // In a thermal — no sink

	// Only apply sink within the boundary layer (thermals don't extend above BL)
	if (AltitudeM > BoundaryLayerHeightM) return 0.0f;

	// Sink rate from current conditions
	if (ThermalSinkMs < 0.1f) return 0.0f;

	// Altitude profile: sink is weakest near ground, strongest at mid-BL
	float AltFraction = AltitudeM / FMath::Max(100.0f, BoundaryLayerHeightM);
	AltFraction = FMath::Clamp(AltFraction, 0.0f, 1.0f);
	float AltFactor = FMath::Sin(AltFraction * PI);  // Peak at mid-BL

	return ThermalSinkMs * AltFactor;
}

void AWingActor::Tick(float DeltaTime){
	Super::Tick(DeltaTime);
	// Invalidate ground Z cache at start of each tick
	InvalidateGroundZCache();

	TickTestWing(DeltaTime);
}

void AWingActor::UpdateConnectedState(float DeltaTime)
{
	if (!bRisersConnected) return;

	// Ram-air inflation: when connected and wind is present, the wing inflates
	float WindMs = GetCurrentWindSpeedMs();
	if (WindMs > 1.0f && WingState == EWingState::Connected)
	{
		WingState = EWingState::Inflating;
		UE_LOG(LogTemp, Warning, TEXT("WingActor: Test wing inflating (wind=%.1f m/s)"), WindMs);
	}

	// === LINE TENSION DRAG ===
	// The wing does NOT move on its own. It only moves when the lines go taut
	// and pull it toward the pilot. This is how real paragliding works:
	// you connect the risers, walk, and the wing gets dragged when lines are taut.
	FVector CurrentPos = GetActorLocation();
	FVector CarabMid = (LeftCarabinerPos + RightCarabinerPos) * 0.5f;
	float DistToPilot = FVector::Dist2D(CurrentPos, CarabMid);
	float LineLen = 700.0f;  // ~7m total line + riser length

	// When the pilot walks far enough that lines would be taut,
	// drag the wing toward the pilot (keeping it at line length distance).
	if (DistToPilot > LineLen)
	{
		// Direction from wing to pilot (horizontal only)
		FVector ToPilot = CarabMid - CurrentPos;
		ToPilot.Z = 0.0f;
		ToPilot.Normalize();

		// Target position: at line length from pilot, in the direction from pilot to wing
		// This keeps the wing at the correct distance — it follows but doesn't jump to the pilot
		FVector TargetPos = CarabMid - ToPilot * LineLen;
		// Keep wing on the ground when deflated, raise with inflation
		float GroundZ = GetCachedGroundZ(CarabMid);
		TargetPos.Z = GroundZ + TestWingInflation * 700.0f + 5.0f;

		// Smooth drag — not instant, lines have some elasticity
		FVector NewPos = FMath::Lerp(CurrentPos, TargetPos, DeltaTime * 2.0f);
		SetActorLocation(NewPos, false);
	}

	// Yaw wing to face into wind (leading edge upwind) — only when inflating
	if (WingState == EWingState::Inflating || WingState == EWingState::Inflated ||
		WingState == EWingState::Flying)
	{
		FVector WindDir2D = GetShiftedWindDirection();
		WindDir2D.Z = 0.0f;
		WindDir2D.Normalize();
		float TargetYaw = FMath::Atan2(WindDir2D.Y, WindDir2D.X) * RAD_TO_DEG;
		FRotator CurrentRot = GetActorRotation();
		float NewYaw = FMath::Lerp(CurrentRot.Yaw, TargetYaw, DeltaTime * 1.0f);
		SetActorRotation(FRotator(CurrentRot.Pitch, NewYaw, CurrentRot.Roll));

		if (TestWingInflation > 0.95f && WingState == EWingState::Inflating)
		{
			WingState = EWingState::Inflated;
			UE_LOG(LogTemp, Warning, TEXT("WingActor: Test wing inflated!"));
		}
	}
}

void AWingActor::UpdateRiserMesh()
{
	if (!RiserMeshComponent || !RiserMeshComponent->IsVisible()) return;

	FVector ActorLoc = GetActorLocation();

	// The carabiner attaches to the TOP of the riser mesh, not the center.
	// Mesh bounds: center=(0.046, -0.075, 25.423), extent=(7.586, 22.325, 25.978)
	// Top of mesh in local space = center + extent.Z = 51.4cm
	FVector RiserTopLocal = bRiserBoundsCached
		? FVector(RiserMeshBoundsCenter.X, RiserMeshBoundsCenter.Y, RiserMeshBoundsCenter.Z + RiserMeshBoundsExtent.Z)
		: FVector(0.0f, 0.0f, 51.4f);

	if (bRisersConnected)
	{
		// Position risers at carabiner midpoint, rotated to align with carabiner direction
		FVector MidCarabiner = (LeftCarabinerPos + RightCarabinerPos) * 0.5f;
		FVector CarabDir = (RightCarabinerPos - LeftCarabinerPos).GetSafeNormal();

		// The riser mesh's Y axis spans left-to-right (carabiner spacing direction).
		// In Unreal, Yaw=θ maps local +Y to world (-sin θ, cos θ).
		// To align +Y with carabiner dir (Dx, Dy): θ = atan2(-Dx, Dy)
		float CarabYaw = FMath::Atan2(-CarabDir.X, CarabDir.Y) * RAD_TO_DEG;
		FQuat RiserRot(FRotator(0.0f, CarabYaw, 0.0f));

		// The TOP of the riser (carabiner attachment) must be at the carabiner midpoint.
		// Transform the top point from mesh local space to world space using the rotation,
		// then place the mesh origin so the top ends up at MidCarabiner.
		FVector RiserTopWorld = RiserRot.RotateVector(RiserTopLocal);
		RiserMeshComponent->SetWorldLocationAndRotation(
			MidCarabiner - RiserTopWorld,
			RiserRot);
	}
	else
	{
		// Position risers at average line bottom (in world space)
		FVector AvgBottom(0, 0, 0);
		int32 Count = 0;
		for (int32 li = 0; li < MPCL_LineCount; li++)
		{
			int32 BotIdx = li * 2 + 1;
			if (BotIdx < TestLineParticles.Num())
			{
				AvgBottom += TestLineParticles[BotIdx].Pos;
				Count++;
			}
		}
		if (Count > 0)
		{
			AvgBottom /= Count;
			FVector TargetWorld = AvgBottom + ActorLoc;
			// When not connected, no rotation — use the top offset directly
			RiserMeshComponent->SetWorldLocationAndRotation(
				TargetWorld - RiserTopLocal,
				FQuat::Identity);
		}
	}
}

void AWingActor::TickTestWing(float DeltaTime)
{
	if (WingState == EWingState::InBag) return;

	// Update dynamic wind
	float GustAmp = 0.0f;
	if (CachedGameInstance)
		GustAmp = CachedGameInstance->WindGusts;
	UpdateDynamicWind(DeltaTime, GustAmp);

	// Update thermal system (spawn, drift, age)
	UpdateThermals(DeltaTime);

	// Connected state: line tension drag, yaw, inflation transition
	UpdateConnectedState(DeltaTime);

	// Flight physics
	if (WingState == EWingState::Flying)
	{
		UpdateFlight(DeltaTime);
	}
	else if (WingState == EWingState::Inflated && bRisersConnected)
	{
		CheckTakeoff(DeltaTime);
	}

	// Run cloth and line simulation (uses updated actor position)
	SimulateTestWingCloth(DeltaTime);
	SimulateTestWingLines(DeltaTime);

	// Update procedural meshes with simulated data
	UpdateTestWingCanopyMesh();
	UpdateTestWingLineMesh();

	// Update riser mesh position to follow line bottoms or carabiners
	UpdateRiserMesh();
}


