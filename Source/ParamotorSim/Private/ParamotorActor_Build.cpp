// Copyright Epic Games, Inc. All Rights Reserved.

#include "ParamotorActor.h"
#include "ParamotorLogging.h"
#include "UW_InteractPrompt.h"
#include "Components/StaticMeshComponent.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundWave.h"
#include "Misc/Paths.h"

// ==== MESH / RIG CONSTRUCTION ====

void AParamotorActor::BuildParamotorMeshes()
{
	// Codex build — real part Blueprints instead of the procedural body.
	if (bUseCodexBuild)
	{
		BuildCodexVisuals();
		return;
	}

	const TArray<FString> MeshPaths = GetParamotorMeshPaths();

	for (int32 i = 0; i < MeshPaths.Num(); i++)
	{
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *MeshPaths[i]);
		if (!Mesh) continue;

		FString BaseName = FPaths::GetBaseFilename(MeshPaths[i]);
		FString CompName = FString::Printf(TEXT("Mesh_%d_%s"), i, *BaseName);
		UStaticMeshComponent* MeshComp = NewObject<UStaticMeshComponent>(this, FName(*CompName));
		MeshComp->SetupAttachment(Root);
		MeshComp->SetStaticMesh(Mesh);
		MeshComp->SetMobility(EComponentMobility::Movable);
		MeshComp->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		MeshComp->SetCollisionResponseToAllChannels(ECR_Ignore);
		MeshComp->RegisterComponent();
		MeshComponents.Add(MeshComp);

		// Track prop parts for rotation
		if (BaseName.StartsWith(TEXT("Prop_Blade")) || BaseName == TEXT("Prop_Hub") ||
			BaseName == TEXT("Prop_HubCone"))
		{
			PropMeshes.Add(MeshComp);
			PropMeshBaseTransforms.Add(MeshComp->GetRelativeTransform());  // Store base for rotation
			PropCenter = FVector(15.0f, 0.0f, 73.0f);
		}
	}

	UE_LOG(LogParamotor, Log, TEXT("ParamotorActor: Built %d mesh components, %d prop meshes"),
		MeshComponents.Num(), PropMeshes.Num());

	// === BUILD A-ARM PIVOT HIERARCHY ===
	// Real paramotor connection:
	//   Frame → Stud (fixed) → AArm (PIVOTS) → Ring → Carabiner → Risers → QuickLinks → Lines
	// The A-arms pivot freely so the wing can swing independently of the frame.
	BuildAArmHierarchy();
}

void AParamotorActor::BuildAArmHierarchy()
{
	// Find the stud mesh components (pivot attachment points on the frame)
	UStaticMeshComponent* StudLeft = nullptr;
	UStaticMeshComponent* StudRight = nullptr;
	for (UStaticMeshComponent* Comp : MeshComponents)
	{
		if (!Comp) continue;
		FString Name = Comp->GetName();
		if (Name.Contains(TEXT("WingClip_Stud_Left"))) StudLeft = Comp;
		else if (Name.Contains(TEXT("WingClip_Stud_Right"))) StudRight = Comp;
	}

	// Build both sides
	AArmPivotLeft = BuildSingleAArmSide(true, StudLeft);
	AArmPivotRight = BuildSingleAArmSide(false, StudRight);

	// Codex build: the procedural frame's stud meshes don't exist, so the
	// pivots landed on the actor root (ground level). Move them to carabiner
	// height on the frame — roughly where the harness shoulder straps sit.
	if (bUseCodexBuild)
	{
		if (AArmPivotLeft)  AArmPivotLeft->SetRelativeLocation(FVector(30.0f, -24.0f, 75.0f));
		if (AArmPivotRight) AArmPivotRight->SetRelativeLocation(FVector(30.0f,  24.0f, 75.0f));

		// The procedural rigging meshes (A-arms, rings, carabiners, toggles,
		// quick links) were authored against the procedural frame — on the
		// Codex assembly they read as floating debris. Hide them; the pivots
		// remain as pure physics anchors and the line mesh renders the lines.
		for (UStaticMeshComponent* C : {
			AArmMeshLeft, AArmMeshRight, RingMeshLeft, RingMeshRight,
			CarabinerMeshLeft, CarabinerMeshRight,
			BrakeToggleMeshLeft, BrakeToggleMeshRight })
		{
			if (C) { C->SetVisibility(false); C->SetHiddenInGame(true); }
		}
		for (auto& KVP : QuickLinkMeshes)
		{
			if (KVP.Value) { KVP.Value->SetVisibility(false); KVP.Value->SetHiddenInGame(true); }
		}
	}

	// Capture the ring mesh's world-space offset from the pivot BEFORE any rotation.
	if (RingMeshLeft && AArmPivotLeft)
	{
		FVector RingWorld = RingMeshLeft->Bounds.Origin;
		FVector PivotWorld = AArmPivotLeft->GetComponentLocation();
		CarabinerOffsetFromPivot = AArmPivotLeft->GetComponentTransform().InverseTransformPosition(RingWorld);
		UE_LOG(LogParamotor, Log, TEXT("ParamotorActor: Carabiner offset from pivot = %s (RingWorld=%s PivotWorld=%s)"),
			*CarabinerOffsetFromPivot.ToString(), *RingWorld.ToString(), *PivotWorld.ToString());
	}

	UE_LOG(LogParamotor, Log, TEXT("ParamotorActor: Built A-arm hierarchy (L/R pivots + meshes)"));
}

void AParamotorActor::UpdateAArmPivots()
{
	if (!AArmPivotLeft || !AArmPivotRight) return;

	// A-arms always hang straight down from the frame.
	// They do NOT track the wing position — in real life, A-arms are short
	// and pivot only a few degrees from gravity and line tension.
	// The riser webbing (49cm strap) handles the angular difference between
	// the A-arm and the wing. Rotating the A-arms to track the wing causes
	// wild swinging because the wing position oscillates from turbulence/pendulum.
	// The line physics uses the fixed carabiner position (from GetCarabinerWorldPositions),
	// not the A-arm rotation, so this is purely visual.
	AArmPivotLeft->SetRelativeRotation(FRotator::ZeroRotator);
	AArmPivotRight->SetRelativeRotation(FRotator::ZeroRotator);
}

bool AParamotorActor::GetCarabinerWorldPositions(FVector& OutLeft, FVector& OutRight) const
{
	// Use the A-arm PIVOT position + fixed offset (captured at setup before rotation).
	// This breaks the feedback loop: A-arm rotation is purely visual and does NOT
	// change the carabiner position that drives line physics.
	// Using RingMesh->Bounds.Origin would feed A-arm rotation back into line physics,
	// causing oscillation (wing moves → A-arm rotates → carabiner moves → lines move → wing moves).
	if (AArmPivotLeft && AArmPivotRight && CarabinerOffsetFromPivot != FVector::ZeroVector)
	{
		FVector PivotLeftPos = AArmPivotLeft->GetComponentLocation();
		FVector PivotRightPos = AArmPivotRight->GetComponentLocation();
		// Apply the fixed offset in the pivot's local space (before rotation)
		// Since the offset was captured before rotation, we apply it in the pivot's
		// local space WITHOUT the current rotation — use the parent's transform.
		FTransform PivotLeftTM = AArmPivotLeft->GetAttachParent() ? AArmPivotLeft->GetAttachParent()->GetComponentTransform() : FTransform::Identity;
		FTransform PivotRightTM = AArmPivotRight->GetAttachParent() ? AArmPivotRight->GetAttachParent()->GetComponentTransform() : FTransform::Identity;
		OutLeft = PivotLeftTM.TransformPosition(CarabinerOffsetFromPivot);
		OutRight = PivotRightTM.TransformPosition(CarabinerOffsetFromPivot);
		return true;
	}

	// Legacy fallback: ring mesh bounds directly (can feed A-arm rotation back
	// into line physics — kept as a last resort for non-Codex rigs).
	if (RingMeshLeft && RingMeshRight)
	{
		OutLeft = RingMeshLeft->Bounds.Origin;
		OutRight = RingMeshRight->Bounds.Origin;
		return true;
	}

	// Final fallback: derive from the actor position + parent's right vector
	// (rigs with no A-arm hierarchy at all).
	FVector ActorLoc = GetActorLocation();
	FVector Right = FVector::RightVector;
	if (AActor* ParentActor = GetAttachParentActor())
	{
		Right = ParentActor->GetActorRightVector();
	}
	const float CarabHalfSpacing = 21.0f;  // ~42cm total spacing
	OutLeft = ActorLoc + Right * -CarabHalfSpacing;
	OutRight = ActorLoc + Right * CarabHalfSpacing;
	return true;
}