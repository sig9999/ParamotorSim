// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NewportBridge.generated.h"

class UInstancedStaticMeshComponent;

/** Structural archetype for a landmark crossing. */
enum class EBridgeStyle : uint8
{
	Suspension,   // towers + main cables + suspenders (Pell, Mount Hope)
	Truss,        // raised through-truss over the navigation span (Jamestown)
	Viaduct,      // low beam bridge on piers (Sakonnet)
};

/** Everything the builder needs to place and shape one real bridge. */
struct FBridgeSpec
{
	const TCHAR* Name;
	double LatA, LonA, LatB, LonB;   // centerline endpoints (either order)
	float TotalLenM;                  // overall length
	float TowerXM;                    // suspension towers at ±X from center (0 = none)
	float RaisedEndM;                 // half-length of the raised navigation section
	float DeckWM;                     // roadway width
	float TowerTopM;                  // tower top above MSL
	float MidDeckM;                   // deck MSL at center
	float RaisedDeckM;                // deck MSL at TowerX / raised-section edge
	float SideDeckM;                  // deck MSL at RaisedEnd
	float EndDeckM;                   // deck MSL at the shoreline ends
	float TrussHeightM;               // Truss style: truss depth above deck
	float ClipHalfWidthM;             // PRT melt-clip corridor half-width (m)
	float ClipHalfLengthM;            // clip half-length along axis (0 = axis+160m)
	// Optional explicit clip polygon (lon,lat verts) for melt fields too big
	// or oddly shaped for the axis-aligned corridor — trace the water area.
	TArray<FVector2D> ClipPolyLonLat;
	EBridgeStyle Style;
};

/**
 * ANewportBridge — procedural, geodetically-anchored Rhode Island landmark
 * bridges. One actor per spec; the GameMode spawns the whole set on globe
 * maps. Instances are world-space on unattached ISMs (VegetationField
 * pattern). Because world-space instances can't follow a georeference
 * origin rebase, a slow Tick watches the georef origin and re-places +
 * rebuilds the whole structure if it ever shifts (dev teleports >50km).
 *
 * Reference data: RITBA / Structurae / Wikipedia / OSM centerlines.
 */
UCLASS()
class PARAMOTORSIM_API ANewportBridge : public AActor
{
	GENERATED_BODY()

public:
	ANewportBridge();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	/** Real specs for the significant Narragansett Bay / Sakonnet crossings. */
	static const TArray<FBridgeSpec>& GetSpecs();

	/** Deferred-spawn entry: set which spec this actor builds. */
	void SetSpecIndex(int32 InIdx) { SpecIndex = InIdx; }

private:
	void Build();             // one-time: components, meshes, materials
	void GenerateInstances(); // fills the ISMs from the current BridgeXf
	void PlaceAtGeodetic();   // geodetic midpoint/axis → actor transform + BridgeXf
	void Rebase();            // origin moved: re-place + regenerate

	// Shape helpers — all positions bridge-local cm, +X endA→endB, Z up.
	void AddBox(const FVector& Pos, const FRotator& Rot, const FVector& Size, bool bAsphalt = false);
	void AddCable(const FVector& A, const FVector& B, float Radius);
	float DeckZ(float Xcm) const;      // deck-top height (cm above MSL) at local X cm
	float CableZ(float Xcm) const;     // suspension main-cable height (cm) at local X
	void BuildSuspension();
	void BuildTruss();
	void BuildViaduct();

	UPROPERTY()
	UInstancedStaticMeshComponent* BoxISM = nullptr;    // green steel
	UPROPERTY()
	UInstancedStaticMeshComponent* DeckISM = nullptr;   // asphalt
	UPROPERTY()
	UInstancedStaticMeshComponent* CylISM = nullptr;    // cables/suspenders

	FBridgeSpec Spec;
	int32      SpecIndex = 0;
	FTransform BridgeXf;                                 // local→world for instance baking
	FVector    MeshHalfSize = FVector(50.0f);            // cube half-extents (cm) at scale 1
	FVector    CylHalfSize  = FVector(50.0f);            // cylinder half-extents at scale 1

	// spec values cached in cm for the builders
	float HalfLen, TowerX, RaisedEnd, DeckW, TowerTopZ;
	float MidDeckZ, RaisedDeckZ, SideDeckZ, EndDeckZ, TrussZ;

	FVector BuiltOriginLLH = FVector::ZeroVector; // georef origin at last build
};
