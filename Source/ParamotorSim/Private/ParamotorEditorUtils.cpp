// Copyright Epic Games, Inc. All Rights Reserved.

#include "ParamotorEditorUtils.h"
#include "ParamotorLogging.h"
#include "Misc/FileHelper.h"

#if WITH_EDITOR
#include "Landscape.h"
#include "LandscapeProxy.h"
#include "Editor.h"
#include "Materials/Material.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#endif

bool UParamotorEditorUtils::ScatterStaticMeshInstances(const FString& CsvPath, const FString& MeshAssetPath, const FString& ActorLabel)
{
#if WITH_EDITOR
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) return false;
	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *MeshAssetPath);
	if (!Mesh)
	{
		UE_LOG(LogParamotor, Error, TEXT("Scatter: mesh not found %s"), *MeshAssetPath);
		return false;
	}
	FString Content;
	if (!FFileHelper::LoadFileToString(Content, *CsvPath))
	{
		UE_LOG(LogParamotor, Error, TEXT("Scatter: cannot read %s"), *CsvPath);
		return false;
	}
	TArray<FTransform> Xf;
	TArray<FString> Lines;
	Content.ParseIntoArrayLines(Lines);
	for (const FString& L : Lines)
	{
		TArray<FString> P;
		L.ParseIntoArray(P, TEXT(","));
		if (P.Num() < 5) continue;
		Xf.Add(FTransform(FRotator(0.f, FCString::Atof(*P[3]), 0.f),
			FVector(FCString::Atof(*P[0]), FCString::Atof(*P[1]), FCString::Atof(*P[2])),
			FVector(FCString::Atof(*P[4]))));
	}
	if (Xf.Num() == 0) return false;

	AActor* A = World->SpawnActor<AActor>();
	if (!A) return false;
	A->SetActorLabel(*ActorLabel);
	UInstancedStaticMeshComponent* C = NewObject<UInstancedStaticMeshComponent>(A);
	C->CreationMethod = EComponentCreationMethod::Instance;
	A->AddInstanceComponent(C);
	A->SetRootComponent(C);
	C->RegisterComponent();
	C->SetStaticMesh(Mesh);
	C->SetMobility(EComponentMobility::Static);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->AddInstances(Xf, false);
	UE_LOG(LogParamotor, Log, TEXT("Scatter: %s -> %d instances of %s"), *ActorLabel, Xf.Num(), *MeshAssetPath);
	return true;
#else
	return false;
#endif
}

bool UParamotorEditorUtils::ConfigureDecalMaterial(UMaterial* Mat)
{
#if WITH_EDITOR
	if (!Mat) return false;
	Mat->MaterialDomain = MD_DeferredDecal;
	Mat->BlendMode = BLEND_Translucent;
	Mat->DecalBlendMode = EDecalBlendMode::DBM_Translucent;
	Mat->PostEditChange();
	Mat->MarkPackageDirty();
	UE_LOG(LogParamotor, Log, TEXT("ConfigureDecalMaterial: done on %s"), *Mat->GetName());
	return true;
#else
	return false;
#endif
}

bool UParamotorEditorUtils::ImportLandscapeFromRawHeightmap(const FString& R16Path, int32 SizeX, int32 SizeY,
	int32 QuadsPerSection, int32 SectionsPerComponent, float ScaleXY, float ScaleZ, bool bCenterAtOrigin)
{
#if WITH_EDITOR
	TArray<uint8> Raw;
	if (!FFileHelper::LoadFileToArray(Raw, *R16Path))
	{
		UE_LOG(LogParamotor, Error, TEXT("LandscapeImport: cannot read %s"), *R16Path);
		return false;
	}
	const int64 Expected = (int64)SizeX * SizeY * 2;
	if (Raw.Num() != Expected)
	{
		UE_LOG(LogParamotor, Error, TEXT("LandscapeImport: %s has %lld bytes, expected %lld (%dx%d uint16)"),
			*R16Path, (int64)Raw.Num(), Expected, SizeX, SizeY);
		return false;
	}

	TArray<uint16> HeightData;
	HeightData.SetNumUninitialized(SizeX * SizeY);
	FMemory::Memcpy(HeightData.GetData(), Raw.GetData(), Raw.Num());

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		UE_LOG(LogParamotor, Error, TEXT("LandscapeImport: no editor world"));
		return false;
	}

	const int32 QuadsPerComponent = QuadsPerSection * SectionsPerComponent;
	const int32 CompX = (SizeX - 1) / QuadsPerComponent;
	const int32 CompY = (SizeY - 1) / QuadsPerComponent;

	FVector Offset = FVector::ZeroVector;
	if (bCenterAtOrigin)
	{
		Offset = FTransform(FRotator::ZeroRotator, FVector::ZeroVector,
			FVector(ScaleXY, ScaleXY, ScaleZ)).TransformVector(
			FVector(-CompX * QuadsPerComponent / 2.0, -CompY * QuadsPerComponent / 2.0, 0.0));
	}

	ALandscape* Landscape = World->SpawnActor<ALandscape>(Offset, FRotator::ZeroRotator);
	if (!Landscape)
	{
		UE_LOG(LogParamotor, Error, TEXT("LandscapeImport: spawn failed"));
		return false;
	}
	Landscape->SetActorRelativeScale3D(FVector(ScaleXY, ScaleXY, ScaleZ));
	Landscape->StaticLightingLOD = 3;

	TMap<FGuid, TArray<uint16>> HeightDataPerLayers;
	HeightDataPerLayers.Add(FGuid(), MoveTemp(HeightData));
	TMap<FGuid, TArray<FLandscapeImportLayerInfo>> EmptyLayers;
	EmptyLayers.Add(FGuid(), TArray<FLandscapeImportLayerInfo>());

	Landscape->Import(FGuid::NewGuid(), 0, 0, SizeX - 1, SizeY - 1,
		SectionsPerComponent, QuadsPerSection, HeightDataPerLayers, *R16Path,
		EmptyLayers, ELandscapeImportAlphamapType::Additive,
		TArrayView<const FLandscapeLayer>());

	if (ULandscapeInfo* Info = Landscape->GetLandscapeInfo())
	{
		Info->UpdateLayerInfoMap(Landscape);
	}

	UE_LOG(LogParamotor, Log, TEXT("LandscapeImport: done — %dx%d verts, %dx%d components, scale %.0f/%.0f/%.0f"),
		SizeX, SizeY, CompX, CompY, ScaleXY, ScaleXY, ScaleZ);
	return true;
#else
	UE_LOG(LogParamotor, Error, TEXT("LandscapeImport: editor-only function"));
	return false;
#endif
}

void UParamotorEditorUtils::DumpMeshSections(const FString& MeshAssetPath)
{
#if WITH_EDITOR
	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *MeshAssetPath);
	if (!Mesh) { UE_LOG(LogParamotor, Error, TEXT("DumpMeshSections: no mesh %s"), *MeshAssetPath); return; }
	if (FStaticMeshRenderData* RD = Mesh->GetRenderData())
	{
		if (RD->LODResources.Num() > 0)
		{
			FStaticMeshLODResources& LOD = RD->LODResources[0];
			for (int32 i = 0; i < LOD.Sections.Num(); ++i)
			{
				const FStaticMeshSection& S = LOD.Sections[i];
				UMaterialInterface* M = Mesh->GetMaterial(S.MaterialIndex);
				UE_LOG(LogParamotor, Log, TEXT("Sec%d tris=%d mat=%s"),
					i, S.NumTriangles, M ? *M->GetPathName() : TEXT("none"));
			}
		}
	}
	else { UE_LOG(LogParamotor, Error, TEXT("DumpMeshSections: no render data")); }
#endif
}


#if WITH_EDITOR
#include "MaterialEditingLibrary.h"

bool UParamotorEditorUtils::ConnectPixelDepthOffset(UMaterial* Mat, UMaterialExpression* Expr)
{
	if (!Mat || !Expr) return false;
	UMaterialEditingLibrary::ConnectMaterialProperty(Expr, TEXT(""), EMaterialProperty::MP_PixelDepthOffset);
	return true;
}
#endif

#if WITH_EDITOR
#include "Materials/MaterialInstanceConstant.h"

bool UParamotorEditorUtils::ConfigureOceanInstance(UMaterialInstanceConstant* MI)
{
	if (!MI) return false;
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Water/Materials/WaterSurface/Water_Material.Water_Material"));
	if (!Base) { UE_LOG(LogParamotor, Error, TEXT("OceanMI: no Water_Material")); return false; }

	UMaterialEditingLibrary::SetMaterialInstanceParent(MI, Base);

	// Static switches — write the parameter set directly (library setters
	// require an existing override entry which a fresh MIC lacks).
	FStaticParameterSet SPS;
	auto Sw = [&](const TCHAR* N, bool V)
	{
		FStaticSwitchParameter P;
		P.ParameterInfo.Name = FName(N);
		P.Value = V;
		P.bOverride = true;
		SPS.StaticSwitchParameters.Add(P);
		UE_LOG(LogParamotor, Log, TEXT("OceanMI switch %s -> %d"), N, V);
	};
	Sw(TEXT("Enable Ocean Foam"), true);
	Sw(TEXT("Enable Ocean Transition"), true);
	Sw(TEXT("Caustics"), true);
	MI->UpdateStaticPermutation(SPS);

	auto Vec = [&](const TCHAR* N, FLinearColor C)
	{
		FVectorParameterValue P;
		P.ParameterInfo.Name = FName(N);
		P.ParameterValue = C;
		MI->VectorParameterValues.Add(P);
	};
	Vec(TEXT("Scattering"), FLinearColor(0.030f, 0.100f, 0.160f));
	Vec(TEXT("Absorption"), FLinearColor(0.45f, 0.16f, 0.06f));
	Vec(TEXT("Foam Scattering"), FLinearColor(0.85f, 0.90f, 0.95f));

	auto Sca = [&](const TCHAR* N, float V)
	{
		FScalarParameterValue P;
		P.ParameterInfo.Name = FName(N);
		P.ParameterValue = V;
		MI->ScalarParameterValues.Add(P);
	};
	Sca(TEXT("Water Distant Normal Offset"), 200000.0f);
	Sca(TEXT("Water Distant Normal Length"), 500000.0f);

	UMaterialEditingLibrary::UpdateMaterialInstance(MI);
	MI->PostEditChange();
	MI->MarkPackageDirty();
	UE_LOG(LogParamotor, Log, TEXT("OceanMI configured — switches %d, scalars %d, vectors %d"),
		SPS.StaticSwitchParameters.Num(), MI->ScalarParameterValues.Num(), MI->VectorParameterValues.Num());
	return true;
}
#endif

#if WITH_EDITOR
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialExpressionCollectionParameter.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionSubtract.h"
#include "Materials/MaterialExpressionDivide.h"
#include "Materials/MaterialExpressionMin.h"
#include "Materials/MaterialExpressionSaturate.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Engine/TextureRenderTarget2D.h"

bool UParamotorEditorUtils::AddCollectionScalar(UMaterialParameterCollection* MPC, const FString& Name, float DefaultValue)
{
	if (!MPC) return false;
	for (const FCollectionScalarParameter& P : MPC->ScalarParameters)
		if (P.ParameterName == FName(*Name)) return true;   // already there

	FCollectionScalarParameter P;
	P.ParameterName = FName(*Name);
	P.Id = FGuid::NewGuid();
	P.DefaultValue = DefaultValue;
	MPC->ScalarParameters.Add(P);
	MPC->PostEditChange();
	MPC->MarkPackageDirty();
	UE_LOG(LogParamotor, Log, TEXT("MPC +%s = %.2f"), *Name, DefaultValue);
	return true;
}

bool UParamotorEditorUtils::AddCollectionVector(UMaterialParameterCollection* MPC, const FString& Name)
{
	if (!MPC) return false;
	for (const FCollectionVectorParameter& P : MPC->VectorParameters)
		if (P.ParameterName == FName(*Name)) return true;

	FCollectionVectorParameter P;
	P.ParameterName = FName(*Name);
	P.Id = FGuid::NewGuid();
	P.DefaultValue = FLinearColor::Transparent;
	MPC->VectorParameters.Add(P);
	MPC->PostEditChange();
	MPC->MarkPackageDirty();
	UE_LOG(LogParamotor, Log, TEXT("MPC +vec %s"), *Name);
	return true;
}

void UParamotorEditorUtils::DumpMaterialGraph(UMaterial* Mat)
{
	if (!Mat) return;
	const FMaterialExpressionCollection& Coll = Mat->GetExpressionCollection();
	UE_LOG(LogParamotor, Log, TEXT("Dump %s: %d expressions"), *Mat->GetName(), Coll.Expressions.Num());
	for (UMaterialExpression* E : Coll.Expressions)
	{
		if (!E) continue;
		FString Extra;
		if (UMaterialExpressionTextureSample* TS = Cast<UMaterialExpressionTextureSample>(E))
			Extra = FString::Printf(TEXT(" tex=%s"), TS->Texture ? *TS->Texture->GetName() : TEXT("null"));
		if (UMaterialExpressionCollectionParameter* CP = Cast<UMaterialExpressionCollectionParameter>(E))
			Extra = FString::Printf(TEXT(" param=%s"), *CP->ParameterName.ToString());
		UE_LOG(LogParamotor, Log, TEXT("  %s%s"), *E->GetClass()->GetName(), *Extra);
	}
	for (int32 P = 0; P < (int32)MP_MAX; ++P)
	{
		if (FExpressionInput* In = Mat->GetExpressionInputForProperty((EMaterialProperty)P))
			if (In->Expression)
				UE_LOG(LogParamotor, Log, TEXT("  INPUT %d connected"), P);
	}
}

int32 UParamotorEditorUtils::PruneInvalidCollectionParams(UMaterial* Mat)
{
	if (!Mat) return 0;
	const FMaterialExpressionCollection& Coll = Mat->GetExpressionCollection();
	TArray<UMaterialExpression*> Dead;
	for (UMaterialExpression* E : Coll.Expressions)
		if (UMaterialExpressionCollectionParameter* CP = Cast<UMaterialExpressionCollectionParameter>(E))
			if (!CP->ParameterId.IsValid())
				Dead.Add(E);
	for (UMaterialExpression* E : Dead)
		UMaterialEditingLibrary::DeleteMaterialExpression(Mat, E);
	if (Dead.Num() > 0)
	{
		UMaterialEditingLibrary::RecompileMaterial(Mat);
		Mat->PostEditChange();
		Mat->MarkPackageDirty();
	}
	UE_LOG(LogParamotor, Log, TEXT("PruneInvalidCollectionParams: removed %d"), Dead.Num());
	return Dead.Num();
}

bool UParamotorEditorUtils::EnsureLandscapeSandSink(UMaterial* Mat)
{
	if (!Mat) return false;
	UMaterialParameterCollection* MPC = LoadObject<UMaterialParameterCollection>(nullptr,
		TEXT("/Game/Materials/MPC_Sand.MPC_Sand"));
	UTextureRenderTarget2D* RT = LoadObject<UTextureRenderTarget2D>(nullptr,
		TEXT("/Game/Textures/RT_SandDeform.RT_SandDeform"));
	if (!MPC || !RT) { UE_LOG(LogParamotor, Error, TEXT("SandSink: missing MPC/RT")); return false; }

	auto Mk = [&](UClass* Cls, int32 X, int32 Y) -> UMaterialExpression*
	{
		return UMaterialEditingLibrary::CreateMaterialExpression(Mat, Cls, X, Y);
	};
	// CollectionParameter expressions resolve by ParameterId (GUID), not
	// name — bind the matching MPC entry's Id or the node stays invalid.
	auto BindParam = [&](UMaterialExpressionCollectionParameter* E, FName PName)
	{
		E->Collection = MPC; E->ParameterName = PName;
		for (const FCollectionScalarParameter& P : MPC->ScalarParameters)
			if (P.ParameterName == PName) { E->ParameterId = P.Id; return; }
		for (const FCollectionVectorParameter& P : MPC->VectorParameters)
			if (P.ParameterName == PName) { E->ParameterId = P.Id; return; }
	};

	// Idempotent: if the sink chain already exists (e.g. a prior run added
	// it with unbound ParameterIds), just repair the bindings in place.
	{
		const FMaterialExpressionCollection& Coll = Mat->GetExpressionCollection();
		TArray<UMaterialExpressionCollectionParameter*> Existing;
		for (UMaterialExpression* E : Coll.Expressions)
			if (UMaterialExpressionCollectionParameter* CP = Cast<UMaterialExpressionCollectionParameter>(E))
				if (CP->ParameterName == TEXT("SandDeformOrigin") || CP->ParameterName == TEXT("SandDeformSize")
					|| CP->ParameterName == TEXT("SandActive"))
					Existing.Add(CP);
		if (Existing.Num() >= 3)
		{
			for (auto* CP : Existing) BindParam(CP, CP->ParameterName);
			UMaterialEditingLibrary::RecompileMaterial(Mat);
			Mat->PostEditChange();
			Mat->MarkPackageDirty();
			UE_LOG(LogParamotor, Log, TEXT("SandSink: rebound %d existing collection params"), Existing.Num());
			return true;
		}
	}

	// windowUV = (WorldPos.xy - Origin) / Size
	auto* WP = Cast<UMaterialExpressionWorldPosition>(Mk(UMaterialExpressionWorldPosition::StaticClass(), -1600, -200));
	WP->WorldPositionShaderOffset = EWorldPositionIncludedOffsets::WPT_Default;
	auto* WXY = Cast<UMaterialExpressionComponentMask>(Mk(UMaterialExpressionComponentMask::StaticClass(), -1450, -200));
	WXY->R = true; WXY->G = true;
	UMaterialEditingLibrary::ConnectMaterialExpressions(WP, TEXT(""), WXY, TEXT(""));

	auto* COrigin = Cast<UMaterialExpressionCollectionParameter>(Mk(UMaterialExpressionCollectionParameter::StaticClass(), -1600, -60));
	BindParam(COrigin, TEXT("SandDeformOrigin"));
	auto* COxy = Cast<UMaterialExpressionComponentMask>(Mk(UMaterialExpressionComponentMask::StaticClass(), -1450, -60));
	COxy->R = true; COxy->G = true;
	UMaterialEditingLibrary::ConnectMaterialExpressions(COrigin, TEXT(""), COxy, TEXT(""));

	auto* Sub = Cast<UMaterialExpressionSubtract>(Mk(UMaterialExpressionSubtract::StaticClass(), -1300, -150));
	UMaterialEditingLibrary::ConnectMaterialExpressions(WXY, TEXT(""), Sub, TEXT("A"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(COxy, TEXT(""), Sub, TEXT("B"));

	auto* CSize = Cast<UMaterialExpressionCollectionParameter>(Mk(UMaterialExpressionCollectionParameter::StaticClass(), -1300, 40));
	BindParam(CSize, TEXT("SandDeformSize"));
	auto* Div = Cast<UMaterialExpressionDivide>(Mk(UMaterialExpressionDivide::StaticClass(), -1150, -150));
	UMaterialEditingLibrary::ConnectMaterialExpressions(Sub, TEXT(""), Div, TEXT("A"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(CSize, TEXT(""), Div, TEXT("B"));

	// edge mask = saturate(min(min(u,1-u),min(v,1-v)) * 20)  -> 0 at rim, 1 inside
	auto* MU = Cast<UMaterialExpressionComponentMask>(Mk(UMaterialExpressionComponentMask::StaticClass(), -1000, -150));
	MU->R = true;
	UMaterialEditingLibrary::ConnectMaterialExpressions(Div, TEXT(""), MU, TEXT(""));
	auto* MV = Cast<UMaterialExpressionComponentMask>(Mk(UMaterialExpressionComponentMask::StaticClass(), -1000, -80));
	MV->G = true;
	UMaterialEditingLibrary::ConnectMaterialExpressions(Div, TEXT(""), MV, TEXT(""));

	auto* One = Cast<UMaterialExpressionConstant>(Mk(UMaterialExpressionConstant::StaticClass(), -1000, 60));
	One->R = 1.0f;
	auto* S1 = Cast<UMaterialExpressionSubtract>(Mk(UMaterialExpressionSubtract::StaticClass(), -880, -170));
	UMaterialEditingLibrary::ConnectMaterialExpressions(One, TEXT(""), S1, TEXT("A"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(MU, TEXT(""), S1, TEXT("B"));
	auto* S2 = Cast<UMaterialExpressionSubtract>(Mk(UMaterialExpressionSubtract::StaticClass(), -880, -60));
	UMaterialEditingLibrary::ConnectMaterialExpressions(One, TEXT(""), S2, TEXT("A"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(MV, TEXT(""), S2, TEXT("B"));

	auto* M1 = Cast<UMaterialExpressionMin>(Mk(UMaterialExpressionMin::StaticClass(), -760, -170));
	UMaterialEditingLibrary::ConnectMaterialExpressions(MU, TEXT(""), M1, TEXT("A"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(S1, TEXT(""), M1, TEXT("B"));
	auto* M2 = Cast<UMaterialExpressionMin>(Mk(UMaterialExpressionMin::StaticClass(), -760, -60));
	UMaterialEditingLibrary::ConnectMaterialExpressions(MV, TEXT(""), M2, TEXT("A"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(S2, TEXT(""), M2, TEXT("B"));
	auto* M3 = Cast<UMaterialExpressionMin>(Mk(UMaterialExpressionMin::StaticClass(), -640, -120));
	UMaterialEditingLibrary::ConnectMaterialExpressions(M1, TEXT(""), M3, TEXT("A"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(M2, TEXT(""), M3, TEXT("B"));
	auto* E20 = Cast<UMaterialExpressionConstant>(Mk(UMaterialExpressionConstant::StaticClass(), -640, 20));
	E20->R = 20.0f;
	auto* EMul = Cast<UMaterialExpressionMultiply>(Mk(UMaterialExpressionMultiply::StaticClass(), -520, -120));
	UMaterialEditingLibrary::ConnectMaterialExpressions(M3, TEXT(""), EMul, TEXT("A"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(E20, TEXT(""), EMul, TEXT("B"));
	auto* Sat = Cast<UMaterialExpressionSaturate>(Mk(UMaterialExpressionSaturate::StaticClass(), -400, -120));
	UMaterialEditingLibrary::ConnectMaterialExpressions(EMul, TEXT(""), Sat, TEXT(""));

	// h = RT_SandDeform.r at windowUV
	auto* TS = Cast<UMaterialExpressionTextureSample>(Mk(UMaterialExpressionTextureSample::StaticClass(), -1000, -500));
	TS->Texture = RT;
	TS->SamplerType = EMaterialSamplerType::SAMPLERTYPE_LinearColor;
	UMaterialEditingLibrary::ConnectMaterialExpressions(Div, TEXT(""), TS, TEXT("UVs"));

	// sink = (h - 5cm) * edgeMask * SandActive
	auto* SinkC = Cast<UMaterialExpressionConstant>(Mk(UMaterialExpressionConstant::StaticClass(), -700, -380));
	SinkC->R = 5.0f;
	auto* HSub = Cast<UMaterialExpressionSubtract>(Mk(UMaterialExpressionSubtract::StaticClass(), -560, -500));
	UMaterialEditingLibrary::ConnectMaterialExpressions(TS, TEXT("R"), HSub, TEXT("A"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(SinkC, TEXT(""), HSub, TEXT("B"));
	auto* MulM = Cast<UMaterialExpressionMultiply>(Mk(UMaterialExpressionMultiply::StaticClass(), -400, -420));
	UMaterialEditingLibrary::ConnectMaterialExpressions(HSub, TEXT(""), MulM, TEXT("A"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(Sat, TEXT(""), MulM, TEXT("B"));
	auto* CAct = Cast<UMaterialExpressionCollectionParameter>(Mk(UMaterialExpressionCollectionParameter::StaticClass(), -560, -280));
	BindParam(CAct, TEXT("SandActive"));
	auto* MulA = Cast<UMaterialExpressionMultiply>(Mk(UMaterialExpressionMultiply::StaticClass(), -240, -420));
	UMaterialEditingLibrary::ConnectMaterialExpressions(MulM, TEXT(""), MulA, TEXT("A"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(CAct, TEXT(""), MulA, TEXT("B"));

	// WPO = (0,0,sink)
	auto* Z = Cast<UMaterialExpressionConstant>(Mk(UMaterialExpressionConstant::StaticClass(), -240, -320));
	Z->R = 0.0f;
	auto* AppXY = Cast<UMaterialExpressionAppendVector>(Mk(UMaterialExpressionAppendVector::StaticClass(), -160, -480));
	UMaterialEditingLibrary::ConnectMaterialExpressions(Z, TEXT(""), AppXY, TEXT("A"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(Z, TEXT(""), AppXY, TEXT("B"));
	auto* App = Cast<UMaterialExpressionAppendVector>(Mk(UMaterialExpressionAppendVector::StaticClass(), -100, -400));
	UMaterialEditingLibrary::ConnectMaterialExpressions(AppXY, TEXT(""), App, TEXT("A"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(MulA, TEXT(""), App, TEXT("B"));

	if (FExpressionInput* WPO = Mat->GetExpressionInputForProperty(EMaterialProperty::MP_WorldPositionOffset))
	{
		if (WPO->Expression)
			UE_LOG(LogParamotor, Warning, TEXT("SandSink: WPO already connected — replacing (sand sink supersedes prior deform dip)"));
	}
	UMaterialEditingLibrary::ConnectMaterialProperty(App, TEXT(""), EMaterialProperty::MP_WorldPositionOffset);
	UMaterialEditingLibrary::RecompileMaterial(Mat);
	Mat->PostEditChange();
	Mat->MarkPackageDirty();
	UE_LOG(LogParamotor, Log, TEXT("SandSink: WPO sink wired into %s"), *Mat->GetName());
	return true;
}
#else
// Non-editor stubs — the UFUNCTION exec thunks still reference these in
// Shipping builds, so the symbols must exist even though they do nothing.
bool UParamotorEditorUtils::ConnectPixelDepthOffset(UMaterial*, UMaterialExpression*) { return false; }
bool UParamotorEditorUtils::ConfigureOceanInstance(UMaterialInstanceConstant*) { return false; }
bool UParamotorEditorUtils::AddCollectionScalar(UMaterialParameterCollection*, const FString&, float) { return false; }
bool UParamotorEditorUtils::AddCollectionVector(UMaterialParameterCollection*, const FString&) { return false; }
void UParamotorEditorUtils::DumpMaterialGraph(UMaterial*) {}
int32 UParamotorEditorUtils::PruneInvalidCollectionParams(UMaterial*) { return 0; }
bool UParamotorEditorUtils::EnsureLandscapeSandSink(UMaterial*) { return false; }
#endif
