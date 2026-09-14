// Copyright Epic Games, Inc. All Rights Reserved.

#include "RoadsActor.h"
#include "ParamotorLogging.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"

ARoadsActor::ARoadsActor()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Mesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Roads"));
	Mesh->SetupAttachment(RootComponent);
}

void ARoadsActor::BeginPlay()
{
	Super::BeginPlay();
	if (GeomJsonPath.IsEmpty())
		GeomJsonPath = FPaths::ProjectDir() / TEXT("TerrainData/Processed/roads_geom.json");
	Build();
}

void ARoadsActor::Build()
{
	FString Raw;
	if (!FFileHelper::LoadFileToString(Raw, *GeomJsonPath))
	{
		UE_LOG(LogParamotor, Warning, TEXT("Roads: no geom json at %s"), *GeomJsonPath);
		return;
	}
	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Raw);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid()) return;

	const double E0 = Root->GetNumberField(TEXT("E0"));
	const double N0 = Root->GetNumberField(TEXT("N0"));
	const TArray<TSharedPtr<FJsonValue>>* Strips = nullptr;
	if (!Root->TryGetArrayField(TEXT("strips"), Strips) || !Strips) return;

	// Two sections: 0 asphalt (roads), 1 worn (parking/runway/sidewalk/taxiway).
	TArray<FVector> V[2]; TArray<int32> T[2]; TArray<FVector2D> UV[2];
	TArray<FVector> Nrm;  TArray<FProcMeshTangent> Tan;

	for (const TSharedPtr<FJsonValue>& SVal : *Strips)
	{
		const TSharedPtr<FJsonObject>* S;
		if (!SVal->TryGetObject(S)) continue;
		double Mat = 0; (*S)->TryGetNumberField(TEXT("mat"), Mat);
		const int32 Sec = (int32)Mat == 0 ? 0 : 1;

		const TArray<TSharedPtr<FJsonValue>>* Pts = nullptr;
		(*S)->TryGetArrayField(TEXT("pts"), Pts);
		const TArray<TSharedPtr<FJsonValue>>* Poly = nullptr;
		(*S)->TryGetArrayField(TEXT("poly"), Poly);

		auto PtAt = [](const TArray<TSharedPtr<FJsonValue>>& A, int32 I, double& X, double& Y, double& Z)
		{
			const TArray<TSharedPtr<FJsonValue>>* P;
			if (!A[I]->TryGetArray(P) || P->Num() < 3) return false;
			X = (*P)[0]->AsNumber(); Y = (*P)[1]->AsNumber(); Z = (*P)[2]->AsNumber();
			return true;
		};

		if (Poly && Poly->Num() >= 3)
		{
			// Filled polygon (parking lot): centroid fan.
			double CX = 0, CY = 0, CZ = 0;
			TArray<FVector> Ring;
			for (int32 I = 0; I < Poly->Num(); ++I)
			{
				double X, Y, Z;
				if (!PtAt(*Poly, I, X, Y, Z)) continue;
				CX += X; CY += Y; CZ += Z;
				Ring.Add(FVector(X, Y, Z));
			}
			if (Ring.Num() < 3) continue;
			CX /= Ring.Num(); CY /= Ring.Num(); CZ = CZ / Ring.Num() + 0.12;
			const int32 Base = V[Sec].Num();
			V[Sec].Add(FVector(CX - E0, CY - N0, CZ));
			UV[Sec].Add(FVector2D(CX / 4.0, CY / 4.0));
			for (const FVector& P : Ring)
			{
				V[Sec].Add(P);
				UV[Sec].Add(FVector2D(P.X / 4.0, P.Y / 4.0));
			}
			for (int32 I = 0; I < Ring.Num(); ++I)
				T[Sec].Add(Base), T[Sec].Add(Base + 1 + (I + 1) % Ring.Num()), T[Sec].Add(Base + 1 + I);
			continue;
		}

		if (!Pts || Pts->Num() < 2) continue;
		double Width = 6.0; (*S)->TryGetNumberField(TEXT("width"), Width);
		const double HW = Width / 2.0;

		TArray<FVector> P;
		for (int32 I = 0; I < Pts->Num(); ++I)
		{
			double X, Y, Z;
			if (PtAt(*Pts, I, X, Y, Z)) P.Add(FVector(X, Y, Z));
		}
		if (P.Num() < 2) continue;

		const int32 Base = V[Sec].Num();
		for (int32 I = 0; I < P.Num(); ++I)
		{
			FVector Dir;
			if (I == 0)                Dir = P[1] - P[0];
			else if (I == P.Num() - 1) Dir = P[P.Num() - 1] - P[P.Num() - 2];
			else                       Dir = P[I + 1] - P[I - 1];
			Dir.Z = 0;
			const float L = Dir.Size2D() > 0.001f ? Dir.Size2D() : 1.0f;
			const FVector Perp(-Dir.Y / L, Dir.X / L, 0.0f);
			const float Z = P[I].Z + 12.0f;  // +12cm — hover over the landscape
			V[Sec].Add(FVector(P[I].X + Perp.X * HW - E0, P[I].Y + Perp.Y * HW - N0, Z));
			V[Sec].Add(FVector(P[I].X - Perp.X * HW - E0, P[I].Y - Perp.Y * HW - N0, Z));
			UV[Sec].Add(FVector2D((P[I].X + Perp.X * HW) / 4.0, (P[I].Y + Perp.Y * HW) / 4.0));
			UV[Sec].Add(FVector2D((P[I].X - Perp.X * HW) / 4.0, (P[I].Y - Perp.Y * HW) / 4.0));
		}
		for (int32 I = 0; I < P.Num() - 1; ++I)
		{
			const int32 A = Base + I * 2;
			T[Sec].Add(A);     T[Sec].Add(A + 2); T[Sec].Add(A + 1);
			T[Sec].Add(A + 1); T[Sec].Add(A + 2); T[Sec].Add(A + 3);
		}
	}

	for (int32 S = 0; S < 2; ++S)
	{
		if (V[S].Num() == 0) continue;
		Nrm.Init(FVector::UpVector, V[S].Num());
		Tan.Init(FProcMeshTangent(1, 0, 0), V[S].Num());
		TArray<FColor> Col; Col.Init(FColor::White, V[S].Num());
		TArray<FVector2D> UV2, UV3, UV4;
		Mesh->CreateMeshSection(S, V[S], T[S], Nrm, UV[S], UV2, UV3, UV4, Col, Tan, false);
	}

	UMaterialInterface* MAsphalt = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_RoadAsphalt.M_RoadAsphalt"));
	UMaterialInterface* MWorn = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_RoadWorn.M_RoadWorn"));
	if (MAsphalt) Mesh->SetMaterial(0, MAsphalt);
	if (MWorn) Mesh->SetMaterial(1, MWorn);

	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(false);

	UE_LOG(LogParamotor, Log, TEXT("Roads: built %d + %d verts from %s"),
		V[0].Num(), V[1].Num(), *GeomJsonPath);
}
