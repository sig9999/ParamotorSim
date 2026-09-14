// Copyright Epic Games, Inc. All Rights Reserved.

#include "SandDebrisScatter.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"

ASandDebrisScatter::ASandDebrisScatter()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.5f;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void ASandDebrisScatter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	RepopulateTimer -= DeltaTime;
	if (RepopulateTimer > 0.0f) return;
	RepopulateTimer = 0.5f;
	if (!CachedPawn)
		CachedPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!CachedPawn) return;
	Repopulate(CachedPawn);
}

void ASandDebrisScatter::Repopulate(APawn* Pawn)
{
	const FVector P = Pawn->GetActorLocation();
	TSet<FCellKey> Wanted;
	const int32 MinC = FMath::FloorToInt((P.X - RadiusCm) / CellCm);
	const int32 MaxC = FMath::CeilToInt((P.X + RadiusCm) / CellCm);
	const int32 MinR = FMath::FloorToInt((P.Y - RadiusCm) / CellCm);
	const int32 MaxR = FMath::CeilToInt((P.Y + RadiusCm) / CellCm);
	for (int32 CY = MinR; CY <= MaxR; ++CY)
		for (int32 CX = MinC; CX <= MaxC; ++CX)
		{
			const FVector Cc((CX + 0.5f) * CellCm, (CY + 0.5f) * CellCm, 0.0f);
			if (FVector::DistSquaredXY(Cc, P) <= RadiusCm * RadiusCm)
				Wanted.Add({CX, CY});
		}

	for (auto It = LiveCells.CreateIterator(); It; ++It)
	{
		if (!Wanted.Contains(It->Key))
		{
			if (It->Value) It->Value->DestroyComponent();
			It.RemoveCurrent();
		}
	}
	for (const FCellKey& K : Wanted)
		if (!LiveCells.Contains(K))
			SpawnCell(K);
}

void ASandDebrisScatter::SpawnCell(const FCellKey& K)
{
	const FRandomStream RS(HashCombine(GetTypeHash(K), 0xA5A5A5A5));

	UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(this);
	ISM->SetupAttachment(RootComponent);
	ISM->SetMobility(EComponentMobility::Static);
	ISM->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")));
	ISM->bCastDynamicShadow = true;
	// Wear the landscape ground material — world-position-driven, so the
	// pebble inherits the sand look where it sits.
	UMaterialInterface* GroundMat = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Game/Materials/M_GroundBeach5.M_GroundBeach5"));
	if (GroundMat) ISM->SetMaterial(0, GroundMat);
	ISM->RegisterComponent();

	bool bAny = false;
	for (int32 I = 0; I < 60; ++I)
	{
		const float LX = (K.X + RS.FRand()) * CellCm;
		const float LY = (K.Y + RS.FRand()) * CellCm;
		FHitResult Hit;
		if (!GetWorld()->LineTraceSingleByChannel(Hit,
			FVector(LX, LY, 20000.0f), FVector(LX, LY, -20000.0f), ECC_Visibility)) continue;
		const float GZ = Hit.ImpactPoint.Z;
		if (GZ > SandMaxZCm || GZ < -80.0f) continue;  // sand band only, not underwater

		// Pebble: squashed sphere 2-9cm wide, 1-3cm thick
		const float Wide = 0.02f + RS.FRand() * 0.07f;
		const float Flat = 0.008f + RS.FRand() * 0.02f;
		const FRotator R(FMath::RadiansToDegrees(RS.FRand() * 0.4f - 0.2f),
			RS.FRand() * 360.0f, FMath::RadiansToDegrees(RS.FRand() * 0.4f - 0.2f));
		ISM->AddInstance(FTransform(R, FVector(LX, LY, GZ + Flat * 40.0f), FVector(Wide, Wide * (0.7f + RS.FRand() * 0.6f), Flat)));
		bAny = true;
	}
	if (!bAny) { ISM->DestroyComponent(); ISM = nullptr; }
	LiveCells.Add(K, ISM);
}
