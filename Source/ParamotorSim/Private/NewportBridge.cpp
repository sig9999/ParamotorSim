// Copyright Epic Games, Inc. All Rights Reserved.

#include "NewportBridge.h"
#include "ParamotorLogging.h"
#include "CesiumWorldHelpers.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"

// ---------------------------------------------------------------------------
// Real bridge specs — Narragansett Bay + Sakonnet crossings.
// Endpoints are OSM centerline coordinates; dimensions RITBA/Structurae.
// ---------------------------------------------------------------------------
const TArray<FBridgeSpec>& ANewportBridge::GetSpecs()
{
	static const TArray<FBridgeSpec> Specs = {
		{ // Claiborne Pell Newport Bridge — RI-138, East Passage (OSM 883791472)
			TEXT("NewportPell"),
			41.50787, -71.36138,   41.49955, -71.32220,
			3428.0f, 244.0f, 453.0f, 15.0f,
			122.0f, 66.0f, 64.0f, 55.0f, 12.0f, 0.0f, 130.0f, 0.0f, {},
			EBridgeStyle::Suspension
		},
		{ // Jamestown Verrazzano Bridge — RI-138, West Passage. Cantilever
		  // deck truss, ~2237 m, deck ~41 m MSL; signature tall through-truss
		  // over the central navigation reach.
			TEXT("JamestownVerrazzano"),
			41.53000, -71.40100,   41.52470, -71.37470,
			2237.0f, 0.0f, 275.0f, 12.0f,
			0.0f, 41.0f, 41.0f, 38.0f, 6.0f, 24.0f, 200.0f, 0.0f, {},
			EBridgeStyle::Truss
		},
		{ // Mount Hope Bridge — RI-114, Bristol↔Portsmouth. Suspension,
		  // ~1860 m, main span 366 m, towers ~87 m, deck ~41 m MSL.
		  // PRT's reconstruction of this crossing is catastrophically melted —
		  // debris sheets cover most of Mount Hope Bay. The clip polygon is
		  // an explicit decagon hugging the water (~150 m inside shorelines):
		  // tiles fully inside drop out, shore-straddling tiles survive.
			TEXT("MountHope"),
			41.63440, -71.26230,   41.64140, -71.25300,
			1860.0f, 183.0f, 300.0f, 11.0f,
			87.0f, 41.0f, 40.0f, 33.0f, 6.0f, 0.0f, 900.0f, 1600.0f,
			{
				{-71.252,41.612}, {-71.268,41.620}, {-71.274,41.632},
				{-71.272,41.645}, {-71.264,41.656}, {-71.252,41.658},
				{-71.240,41.652}, {-71.233,41.642}, {-71.234,41.634},
				{-71.240,41.622},
			},
			EBridgeStyle::Suspension
		},
		{ // Sakonnet River Bridge — RI-24, Tiverton↔Portsmouth. Low beam
		  // viaduct ~750 m, deck ~15 m MSL.
			TEXT("SakonnetRiver"),
			41.65770, -71.21200,   41.65030, -71.20620,
			750.0f, 0.0f, 0.0f, 13.0f,
			0.0f, 15.0f, 15.0f, 14.0f, 4.0f, 0.0f, 200.0f, 0.0f, {},
			EBridgeStyle::Viaduct
		},
	};
	return Specs;
}

ANewportBridge::ANewportBridge()
{
	// Slow tick — only watches for georeference origin rebases, which
	// silently strand world-space ISM instances ~50+km off-site.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.5f;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void ANewportBridge::BeginPlay()
{
	Super::BeginPlay();

	UWorld* W = GetWorld();
	if (!W || !CesiumWorld::DetectGlobe(W)) return;

	const TArray<FBridgeSpec>& Specs = GetSpecs();
	if (!Specs.IsValidIndex(SpecIndex)) return;
	Spec = Specs[SpecIndex];
	HalfLen      = Spec.TotalLenM  * 50.0f;
	TowerX       = Spec.TowerXM    * 100.0f;
	RaisedEnd    = Spec.RaisedEndM * 100.0f;
	DeckW        = Spec.DeckWM     * 100.0f;
	TowerTopZ    = Spec.TowerTopM  * 100.0f;
	MidDeckZ     = Spec.MidDeckM   * 100.0f;
	RaisedDeckZ  = Spec.RaisedDeckM* 100.0f;
	SideDeckZ    = Spec.SideDeckM  * 100.0f;
	EndDeckZ     = Spec.EndDeckM   * 100.0f;
	TrussZ       = Spec.TrussHeightM * 100.0f;

	PlaceAtGeodetic();
	CesiumWorld::AnchorToGlobe(W, this);
	CesiumWorld::GetGeorefOrigin(W, BuiltOriginLLH);

	Build();
}

void ANewportBridge::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UWorld* W = GetWorld();
	FVector Cur;
	if (!W || !CesiumWorld::GetGeorefOrigin(W, Cur)) return;
	// ~1e-5° ≈ 1 m — anything past that means the world was re-based and our
	// world-space instance transforms now point at the wrong geography.
	if (FMath::Abs(Cur.X - BuiltOriginLLH.X) > 1e-5 ||
		FMath::Abs(Cur.Y - BuiltOriginLLH.Y) > 1e-5 ||
		FMath::Abs(Cur.Z - BuiltOriginLLH.Z) > 1.0)
	{
		BuiltOriginLLH = Cur;
		Rebase();
	}
}

void ANewportBridge::PlaceAtGeodetic()
{
	// Geodetic placement: midpoint between the real endpoints, yawed to the
	// axis bearing, Z = local MSL-0 at the center (ellipsoid + geoid N).
	UWorld* W = GetWorld();
	const double MidLat = (Spec.LatA + Spec.LatB) * 0.5;
	const double MidLon = (Spec.LonA + Spec.LonB) * 0.5;
	const double Msl0 = CesiumWorld::GeoidUndulationM(MidLat, MidLon);
	const FVector C  = CesiumWorld::GeodeticToUnreal(W, MidLon, MidLat, Msl0);
	const FVector Pa = CesiumWorld::GeodeticToUnreal(W, Spec.LonA, Spec.LatA, Msl0);
	const FVector Pb = CesiumWorld::GeodeticToUnreal(W, Spec.LonB, Spec.LatB, Msl0);
	const FVector Axis = Pb - Pa;
	const float YawDeg = FMath::RadiansToDegrees(FMath::Atan2(Axis.Y, Axis.X));
	SetActorLocation(C);
	SetActorRotation(FRotator(0.0f, YawDeg, 0.0f));
	BridgeXf = FTransform(GetActorRotation(), GetActorLocation(), FVector::OneVector);
	UE_LOG(LogParamotor, Log, TEXT("%s: placed at %.5f,%.5f yaw=%.1f Z=%.0fcm"),
		Spec.Name, MidLat, MidLon, YawDeg, C.Z);
}

// Georeference origin moved — every baked world-space instance is stranded.
// Re-place the actor geodetically and regenerate all instances.
void ANewportBridge::Rebase()
{
	PlaceAtGeodetic();
	for (UInstancedStaticMeshComponent* I : {BoxISM, DeckISM, CylISM})
		if (I) I->ClearInstances();
	GenerateInstances();
	UE_LOG(LogParamotor, Warning, TEXT("%s: rebuilt after georeference rebase"), Spec.Name);
}

// Deck-top height above MSL (cm) at local X (cm). Gentle crest across the
// raised navigation section, then grades down the approaches to the shore.
float ANewportBridge::DeckZ(float Xcm) const
{
	const float a = FMath::Abs(Xcm);
	if (a <= TowerX)     // raised span: MidDeck at center → RaisedDeck at towers
		return RaisedDeckZ + (MidDeckZ - RaisedDeckZ) * (1.0f - (a / TowerX) * (a / TowerX));
	if (a <= RaisedEnd)  // side spans: RaisedDeck → SideDeck
		return FMath::Lerp(RaisedDeckZ, SideDeckZ, (a - TowerX) / (RaisedEnd - TowerX));
	// approaches: SideDeck → EndDeck at the shoreline
	return FMath::Lerp(SideDeckZ, EndDeckZ, (a - RaisedEnd) / (HalfLen - RaisedEnd));
}

// Suspension main-cable height (cm above MSL) at local X.
float ANewportBridge::CableZ(float Xcm) const
{
	const float a = FMath::Abs(Xcm);
	const float MidCableZ = MidDeckZ + 500.0f;   // ~5 m over the crest
	if (a <= TowerX)
		return MidCableZ + (TowerTopZ - MidCableZ) * (a / TowerX) * (a / TowerX);
	if (a <= RaisedEnd)
		return FMath::Lerp(TowerTopZ, SideDeckZ - 700.0f, (a - TowerX) / (RaisedEnd - TowerX))
			+ 900.0f * FMath::Sin(PI * (a - TowerX) / (RaisedEnd - TowerX));
	return FMath::Max(FMath::Lerp(SideDeckZ - 700.0f, 900.0f, (a - RaisedEnd) / (HalfLen - RaisedEnd)),
		DeckZ(Xcm) + 150.0f);
}

void ANewportBridge::AddBox(const FVector& Pos, const FRotator& Rot, const FVector& Size, bool bAsphalt)
{
	UInstancedStaticMeshComponent* ISM = bAsphalt ? DeckISM : BoxISM;
	if (!ISM) return;
	// World-space instance transforms on an unattached component — the same
	// pattern VegetationField uses (attached+local failed to render on the
	// globe-anchored actor). Size is in meters; the cube's real extents come
	// from its bounds rather than an assumed 100 cm edge.
	const FVector Scale(Size.X * 100.0f / (2.0f * MeshHalfSize.X),
		Size.Y * 100.0f / (2.0f * MeshHalfSize.Y),
		Size.Z * 100.0f / (2.0f * MeshHalfSize.Z));
	ISM->AddInstance(FTransform(Rot, Pos, Scale) * BridgeXf, true);
}

void ANewportBridge::AddCable(const FVector& A, const FVector& B, float Radius)
{
	if (!CylISM) return;
	const FVector D = B - A;
	const float Len = D.Size();
	if (Len < 1.0f) return;
	const FRotator Rot = FRotationMatrix::MakeFromZ(D).Rotator();
	// scale from the cylinder mesh's real bounds, not an assumed size
	CylISM->AddInstance(FTransform(Rot, (A + B) * 0.5f,
		FVector(Radius / CylHalfSize.X, Radius / CylHalfSize.Y,
			Len / (2.0f * CylHalfSize.Z))) * BridgeXf, true);
}

void ANewportBridge::Build()
{
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UStaticMesh* Cyl  = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (!Cube || !Cyl) { UE_LOG(LogParamotor, Warning, TEXT("%s: basic shapes missing"), Spec.Name); return; }

	BoxISM  = NewObject<UInstancedStaticMeshComponent>(this, TEXT("Boxes"));
	DeckISM = NewObject<UInstancedStaticMeshComponent>(this, TEXT("Deck"));
	CylISM  = NewObject<UInstancedStaticMeshComponent>(this, TEXT("Cyls"));
	for (UInstancedStaticMeshComponent* I : {BoxISM, DeckISM, CylISM})
	{
		// Unattached + world-space instances (VegetationField pattern).
		I->SetMobility(EComponentMobility::Movable);
		I->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		I->RegisterComponent();
	}
	BoxISM->SetStaticMesh(Cube);
	DeckISM->SetStaticMesh(Cube);
	CylISM->SetStaticMesh(Cyl);
	MeshHalfSize = Cube->GetBounds().BoxExtent;
	CylHalfSize  = Cyl->GetBounds().BoxExtent;

	UMaterialInterface* MSteel = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Game/Materials/M_BridgeGreen.M_BridgeGreen"));
	if (!MSteel) MSteel = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_PoleMetal.M_PoleMetal"));
	UMaterialInterface* MDeck = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_RoadAsphalt.M_RoadAsphalt"));
	if (MSteel) BoxISM->SetMaterial(0, MSteel);
	if (MSteel) CylISM->SetMaterial(0, MSteel);
	if (MDeck)  DeckISM->SetMaterial(0, MDeck);

	GenerateInstances();
}

// Fills all three ISMs in world space via the current BridgeXf. Re-runnable —
// Rebase() clears the instances and calls this again under the new origin.
void ANewportBridge::GenerateInstances()
{
	const float YD = DeckW * 0.5f;

	// ---- deck: stepped slabs following the profile -----------------------
	for (float x = -HalfLen; x < HalfLen; x += 1500.0f)
	{
		const float xm = x + 750.0f;
		const float z0 = DeckZ(x), z1 = DeckZ(x + 1500.0f);
		const float zc = (z0 + z1) * 0.5f;
		const float pitch = FMath::RadiansToDegrees(FMath::Atan2(z1 - z0, 1500.0f));
		// road slab (asphalt)
		AddBox(FVector(xm, 0, zc - 25.0f), FRotator(pitch, 0.0f, 0.0f), FVector(15.6f, DeckW / 100.0f, 0.5f), true);
		// edge girders under each side
		AddBox(FVector(xm, -YD + 40.0f, zc - 250.0f), FRotator(pitch, 0.0f, 0.0f), FVector(15.6f, 0.8f, 4.5f));
		AddBox(FVector(xm,  YD - 40.0f, zc - 250.0f), FRotator(pitch, 0.0f, 0.0f), FVector(15.6f, 0.8f, 4.5f));
		// center barrier + edge rails
		AddBox(FVector(xm, 0, zc + 45.0f), FRotator(pitch, 0.0f, 0.0f), FVector(15.6f, 0.35f, 0.9f));
		AddBox(FVector(xm, -YD + 15.0f, zc + 55.0f), FRotator(pitch, 0.0f, 0.0f), FVector(15.6f, 0.15f, 1.1f));
		AddBox(FVector(xm,  YD - 15.0f, zc + 55.0f), FRotator(pitch, 0.0f, 0.0f), FVector(15.6f, 0.15f, 1.1f));
	}

	// ---- piers under the deck every ~50 m outside the raised section -----
	for (float sx : {-1.0f, 1.0f})
		for (float x = RaisedEnd + 2500.0f; x < HalfLen - 500.0f; x += 5000.0f)
		{
			const float ax = sx * x;
			const float dz = DeckZ(ax);
			const float colH = dz + 600.0f;    // pier runs a few m under water/mud
			for (float sy : {-1.0f, 1.0f})
				AddBox(FVector(ax, sy * YD * 0.7f, (dz - 600.0f) * 0.5f), FRotator::ZeroRotator,
					FVector(2.4f, 2.4f, colH / 100.0f));
			AddBox(FVector(ax, 0, dz - 420.0f), FRotator::ZeroRotator, FVector(2.8f, YD * 0.017f, 1.4f));
		}

	switch (Spec.Style)
	{
	case EBridgeStyle::Suspension: BuildSuspension(); break;
	case EBridgeStyle::Truss:      BuildTruss();      break;
	case EBridgeStyle::Viaduct:    BuildViaduct();    break;
	}

	UE_LOG(LogParamotor, Log, TEXT("%s: built — %d box / %d deck / %d cyl instances"),
		Spec.Name, BoxISM->GetNumInstances(), DeckISM->GetNumInstances(), CylISM->GetNumInstances());
}

// Suspension towers + catenary cables + suspenders (Pell, Mount Hope).
void ANewportBridge::BuildSuspension()
{
	const float YL = DeckW * 0.5f - 150.0f;
	const float YD = DeckW * 0.5f;

	// ---- towers -----------------------------------------------------------
	for (float sx : {-1.0f, 1.0f})
	{
		const float x = sx * TowerX;
		// caisson pier rising just above water
		AddBox(FVector(x, 0, -300.0f), FRotator::ZeroRotator, FVector(24.0f, 18.0f, 5.0f));
		for (float sy : {-1.0f, 1.0f})
		{
			// legs: slightly tapered via two stacked boxes, water → tower top
			const float LegZ1 = TowerTopZ * 0.25f, LegZ2 = TowerTopZ * 0.78f;
			AddBox(FVector(x, sy * YD * 0.9f, LegZ1), FRotator::ZeroRotator,
				FVector(5.5f, 4.0f, TowerTopZ * 0.0059f));
			AddBox(FVector(x, sy * YD * 0.9f, LegZ2), FRotator::ZeroRotator,
				FVector(4.0f, 3.2f, TowerTopZ * 0.0047f));
		}
		// portal struts between the legs: below deck, mid, and at the top
		AddBox(FVector(x, 0, TowerTopZ * 0.066f),  FRotator::ZeroRotator, FVector(5.0f, YD * 0.018f, 7.0f));
		AddBox(FVector(x, 0, TowerTopZ * 0.59f),   FRotator::ZeroRotator, FVector(4.5f, YD * 0.018f, 8.0f));
		AddBox(FVector(x, 0, TowerTopZ * 0.975f),  FRotator::ZeroRotator, FVector(4.5f, YD * 0.018f, 6.0f));
		// saddles
		AddBox(FVector(x, -YL, TowerTopZ + 50.0f), FRotator::ZeroRotator, FVector(4.0f, 1.4f, 1.4f));
		AddBox(FVector(x,  YL, TowerTopZ + 50.0f), FRotator::ZeroRotator, FVector(4.0f, 1.4f, 1.4f));
	}

	// ---- main cables ------------------------------------------------------
	for (float sy : {-1.0f, 1.0f})
	{
		const float y = sy * YL;
		FVector Prev;
		bool bFirst = true;
		for (float x = -HalfLen; x <= HalfLen; x += 1200.0f)
		{
			const FVector P(x, y, CableZ(x));
			if (!bFirst) AddCable(Prev, P, 35.0f);
			Prev = P; bFirst = false;
		}
	}

	// ---- suspenders every ~12 m where the cable rides above the deck ------
	for (float x = -RaisedEnd; x <= RaisedEnd; x += 1200.0f)
	{
		const float cz = CableZ(x), dz = DeckZ(x);
		if (cz - dz < 300.0f) continue;
		for (float sy : {-1.0f, 1.0f})
			AddCable(FVector(x, sy * YL, cz), FVector(x, sy * YL, dz), 12.0f);
	}
}

// Cantilever deck-truss (Jamestown): tall through-truss walls flank the deck
// over the navigation reach, with cross struts overhead.
void ANewportBridge::BuildTruss()
{
	const float YD = DeckW * 0.5f;

	// truss side walls — boxes stepped along the raised section, depth TrussZ
	for (float x = -RaisedEnd; x < RaisedEnd; x += 1500.0f)
	{
		const float xm = x + 750.0f;
		const float z0 = DeckZ(x), z1 = DeckZ(x + 1500.0f);
		const float zc = (z0 + z1) * 0.5f;
		const float pitch = FMath::RadiansToDegrees(FMath::Atan2(z1 - z0, 1500.0f));
		for (float sy : {-1.0f, 1.0f})
			AddBox(FVector(xm, sy * (YD - 40.0f), zc + TrussZ * 0.5f - 100.0f),
				FRotator(pitch, 0.0f, 0.0f), FVector(15.6f, 0.9f, TrussZ / 100.0f));
		// overhead portal struts every ~30 m
		if (FMath::FloorToInt(x / 1500.0f) % 2 == 0)
			AddBox(FVector(xm, 0, zc + TrussZ - 150.0f), FRotator::ZeroRotator,
				FVector(1.2f, YD * 0.017f, 1.2f));
	}

	// tall channel piers under the raised section
	for (float x = -RaisedEnd + 2500.0f; x < RaisedEnd; x += 5000.0f)
	{
		const float dz = DeckZ(x);
		const float colH = dz + 600.0f;
		AddBox(FVector(x, 0, (dz - 600.0f) * 0.5f), FRotator::ZeroRotator,
			FVector(4.5f, YD * 0.014f, colH / 100.0f));
	}
}

// Low beam bridge (Sakonnet): short piers carry the near-flat deck.
void ANewportBridge::BuildViaduct()
{
	const float YD = DeckW * 0.5f;
	// channel piers inside the raised section too — beam bridges have them
	// everywhere the water is navigable
	for (float x = -HalfLen + 2500.0f; x < HalfLen; x += 5000.0f)
	{
		const float dz = DeckZ(x);
		const float colH = dz + 600.0f;
		AddBox(FVector(x, 0, (dz - 600.0f) * 0.5f), FRotator::ZeroRotator,
			FVector(4.0f, YD * 0.014f, colH / 100.0f));
	}
}
