// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SurfaceLibrary.h"
#include "ParamotorSandSystem.generated.h"

class UTextureRenderTarget2D;
class UMaterialInstanceDynamic;
class UMaterialParameterCollection;
class UProceduralMeshComponent;
class USceneCaptureComponent2D;
class UInstancedStaticMeshComponent;
class AParamotorActor;
class AParamotorOceanSystem;
class ASurfaceService;

/**
 * AParamotorSandSystem — local interactive sand surface.
 *
 * A ~24m high-resolution patch of "real" sand follows the player across the
 * beach band. Three cooperating pieces:
 *
 *  1. Deform field — RT_SandDeform (RGBA16F, 2048², ~1.2cm/px). R = signed
 *     height delta in cm (footprints down, displaced rims up), G = disturbance
 *     / loosened-grain mask. Stamps are drawn into it as tiny rotated quads
 *     (canvas DrawMaterial, additive) so every interaction writes the SAME
 *     field — footprints and prop wash share state by construction.
 *  2. Terrain field — a top-down ortho SceneCapture2D (256² depth) re-taken
 *     whenever the window recenters. The overlay mesh is flat; its material
 *     reconstructs world Z per vertex as (CaptureZ - Depth) + Deform + Lift,
 *     so the patch conforms to whatever terrain is rendered underneath —
 *     landscape or Cesium — with zero CPU traces.
 *  3. Overlay mesh — a dense ProceduralMeshComponent grid (~6cm spacing)
 *     whose material does real WPO displacement + per-pixel normals from the
 *     combined heightfield, plus the layered sand shading (macro/meso/micro
 *     noise, wetness darkening, grain glints). The underlying landscape is
 *     sunk a few cm inside the window by the same RT so it can't poke
 *     through dents.
 *
 * Footprints are stamped from actual foot_l/foot_r bone contacts. Prop wash
 * marches a cone wake from the real propeller transform at real GetPropRPM()
 * and both scours the field and feeds the airborne-grain ISM pools.
 *
 * Everything gates off when the player is high or away from the sand band —
 * at flight altitude the whole system is a few dormant buffers.
 */
UCLASS()
class PARAMOTORSIM_API AParamotorSandSystem : public AActor
{
	GENERATED_BODY()

public:
	AParamotorSandSystem();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	// ---- Generic interaction API (feet now, tires/drags later) ----

	/** Press an oriented contact shape into the sand.
	 *  WorldPos = contact center, YawDeg = shape orientation,
	 *  Length/WidthCm = shape footprint, DepthCm = max depression (signed),
	 *  RimCm = displaced-sand buildup around the rim, Angle ignored for now. */
	UFUNCTION(BlueprintCallable)
	void ApplySurfaceContact(const FVector& WorldPos, float YawDeg, float LengthCm, float WidthCm, float DepthCm, float RimCm);

	/** Foot plant — wraps ApplySurfaceContact with shoe-lobe profile. */
	void StampFootprint(const FVector& WorldPos, float YawDeg, bool bLeftFoot, float SpeedCmS);

	/** Airflow scour — erodes height toward 0 and raises disturbance.
	 *  Directional: elongated along WashDir. DeltaTime scales accumulation. */
	void ApplyWashStamp(const FVector& WorldPos, const FVector2D& WashDir, float RadiusCm, float Strength01, float DeltaTime, float Wet01);

	/** Wetness 0(dry)..1(saturated) at a world position from elevation vs water. */
	float GetWetnessAt(const FVector& WorldPos) const;
	/** Same, when ground height is already known (skips the trace). */
	float WetnessFromGroundZ(const FVector& WorldPos, float GroundZ) const;

	/** True if the world position sits inside the interactive sand band. */
	bool IsSandAt(const FVector& WorldPos) const;

	// ---- Tuning ----

	/** World size of the interaction window, cm (square). */
	UPROPERTY(EditAnywhere, Category="Sand|Region") float RegionSizeCm = 2400.0f;
	/** Overlay mesh vertices per side. */
	UPROPERTY(EditAnywhere, Category="Sand|Region") int32 GridVerts = 512;
	/** Only interact below this terrain height above water, cm. */
	UPROPERTY(EditAnywhere, Category="Sand|Region") float SandMaxZCm = 600.0f;
	/** Overlay hover above terrain, cm (hides z-fight; landscape sinks below). */
	UPROPERTY(EditAnywhere, Category="Sand|Region") float OverlayLiftCm = 1.5f;
	/** How far the player moves before the window recenters, cm. */
	UPROPERTY(EditAnywhere, Category="Sand|Region") float RecenterStepCm = 200.0f;

	/** Footprint: depression depth at heel, cm. */
	UPROPERTY(EditAnywhere, Category="Sand|Footprints") float FootDepthCm = 5.0f;
	/** Footprint: rim buildup height, cm. */
	UPROPERTY(EditAnywhere, Category="Sand|Footprints") float FootRimCm = 1.2f;
	/** Foot length/width used for the stamp, cm. */
	UPROPERTY(EditAnywhere, Category="Sand|Footprints") float FootLengthCm = 30.0f;
	UPROPERTY(EditAnywhere, Category="Sand|Footprints") float FootWidthCm = 12.0f;
	/** Min walk speed that still prints, cm/s. */
	UPROPERTY(EditAnywhere, Category="Sand|Footprints") float MinPrintSpeed = 40.0f;

	/** Prop wash: prop RPM below which nothing happens. */
	UPROPERTY(EditAnywhere, Category="Sand|Wash") float WashMinRPM = 1200.0f;
	/** Prop wash: cone half-angle, degrees. */
	UPROPERTY(EditAnywhere, Category="Sand|Wash") float WashSpreadDeg = 11.0f;
	/** Prop wash: wake reach behind the prop, cm. */
	UPROPERTY(EditAnywhere, Category="Sand|Wash") float WashLengthCm = 1400.0f;
	/** Prop wash: max ground scour per second at full RPM, cm. */
	UPROPERTY(EditAnywhere, Category="Sand|Wash") float WashScourCmPerSec = 4.0f;
	/** Prop wash: footprint/disturbance erosion multiplier vs scour. */
	UPROPERTY(EditAnywhere, Category="Sand|Wash") float WashErodeMul = 1.6f;

	/** Grains: max airborne grain instances. */
	UPROPERTY(EditAnywhere, Category="Sand|Grains") int32 MaxGrains = 1600;
	/** Grains: emission rate at full RPM over dry sand, grains/sec. */
	UPROPERTY(EditAnywhere, Category="Sand|Grains") float GrainRate = 900.0f;
	/** Dust: max soft puff instances. */
	UPROPERTY(EditAnywhere, Category="Sand|Grains") int32 MaxDust = 240;

	/** Debug: 0=off 1=region+wake 2=+stats log. */
	UPROPERTY(EditAnywhere, Category="Sand|Debug") int32 DebugMode = 0;

	/** Drop a test footprint under the pawn — bypasses bone detection. */
	void DebugTestStamp();
	/** Log full window/capture/material state for diagnosis. */
	void DebugDump();
	/** Set overlay material debug view: 0=lit, 1=deform sign, 2=wetness. */
	void DebugSetView(int32 V);

private:
	// ---- components ----
	UPROPERTY() UProceduralMeshComponent* OverlayMesh = nullptr;
	UPROPERTY() USceneCaptureComponent2D* TerrainCapture = nullptr;
	UPROPERTY() UInstancedStaticMeshComponent* GrainISM = nullptr;
	UPROPERTY() UInstancedStaticMeshComponent* DustISM = nullptr;

	// ---- assets ----
	UPROPERTY() UTextureRenderTarget2D* RT_Deform = nullptr;   // RGBA16F shared field
	UPROPERTY() UTextureRenderTarget2D* RT_Scratch = nullptr;  // ping-pong for scroll
	UPROPERTY() UTextureRenderTarget2D* RT_Terrain = nullptr;  // scene depth
	UPROPERTY() UMaterialInstanceDynamic* OverlayMID = nullptr;
	UPROPERTY() UMaterialInstanceDynamic* ShiftMID = nullptr;
	UPROPERTY() UMaterialInstanceDynamic* FootStampMID = nullptr;
	UPROPERTY() UMaterialInstanceDynamic* WashStampMID = nullptr;
	UPROPERTY() UMaterialInstanceDynamic* ErodeStampMID = nullptr;
	UPROPERTY() UMaterialInstanceDynamic* FadeMID = nullptr;
	UPROPERTY() UMaterialParameterCollection* MPC = nullptr;

	// ---- window state ----
	FVector2D RegionCenter = FVector2D::ZeroVector;   // texel-snapped
	FVector2D RegionOriginCm = FVector2D::ZeroVector; // world min XY
	float CaptureZ = 0.0f;                            // capture plane height
	bool bActive = false;

	APawn* CachedPawn = nullptr;
	AParamotorOceanSystem* Ocean = nullptr;
	ASurfaceService* SurfSvc = nullptr;
	/** Surface resolved under the pawn (globe maps); deformability drives the
	 *  activation gate and stamp strength — semantic identity, not geometry. */
	FResolvedSurface PawnSurface;
	float SurfQueryTimer = 0.0f;
	float RecaptureTimer = 0.0f;
	float FadeTimer = 0.0f;
	bool bInitialized = false;
	void EnsureInitialized();

	// ---- foot tracking ----
	bool bLeftWasDown = false;
	bool bRightWasDown = false;
	float LoGapL = 14.0f, LoGapR = 14.0f;
	float HiGapL = 22.0f, HiGapR = 22.0f;

	// ---- grain pools ----
	int32 GrainCursor = 0;
	int32 DustCursor = 0;
	float GrainEmitAccum = 0.0f;

	// ---- internals ----
	void UpdateActive(APawn* Pawn);
	void UpdateRegion(bool bForce);
	void RecaptureTerrain();
	void UpdateMPC();
	/** Keep pawn/paramotor/wing out of the depth capture; gear can equip late. */
	void RefreshCaptureHiddenActors();
	void BuildOverlayMesh();
	void UpdateFootprints(APawn* Pawn, float DeltaTime);
	void UpdatePropWash(APawn* Pawn, float DeltaTime);
	void EmitGrain(const FVector& Pos, const FVector& Vel, float Life, float Size01);
	void EmitDust(const FVector& Pos, const FVector& Vel, float Life, float Size01);
	void DebugDraw(APawn* Pawn, const FVector& PropPos, const FVector& WashDir, float Strength) const;
	void SetOverlayParams();

	/** world->deform-UV; returns false outside the window. */
	bool WorldToUV(const FVector& WorldPos, FVector2D& OutUV) const;
	UTextureRenderTarget2D* GetDeformTarget() const { return RT_Deform; }
};
