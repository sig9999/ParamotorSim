// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ChargerWingActor.h"
#include "ParamotorActor.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Components/BoxComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChargerFreshSmoke,"Charger.Fresh.RuntimeInflationAndControls",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FChargerFreshSmoke::RunTest(const FString&){
 UWorld* W=UWorld::CreateWorld(EWorldType::Game,false);
 FWorldContext& Context=GEngine->CreateNewWorldContext(EWorldType::Game);Context.SetCurrentWorld(W);
 auto* Char=W->SpawnActor<ACharacter>(FVector(0,0,100),FRotator::ZeroRotator);
 Char->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
 auto* Motor=W->SpawnActor<AParamotorActor>(FVector(0,0,100),FRotator::ZeroRotator);
 Motor->AttachToActor(Char,FAttachmentTransformRules::KeepWorldTransform);
 auto* Wing=W->SpawnActor<AChargerWingActor>(FVector(-500,0,5),FRotator::ZeroRotator);
 // WindHeadingDegrees is meteorological "wind FROM". Heading 0 (from +X)
 // makes air move -X — headwind for the +X-facing pilot and wing.
 Wing->WindHeadingDegrees=0.0f;
 W->BeginPlay();
 if(!Wing->HasActorBegunPlay())Wing->DispatchBeginPlay();
 TestEqual(TEXT("Fresh wing starts spread"),Wing->GetWingState(),EWingState::Spread);
 Wing->ConnectRisers(Motor,FVector(0,-21,150),FVector(0,21,150));Wing->LiftALines();
 for(int I=0;I<600;++I)Wing->Tick(1.f/120);
 TestTrue(TEXT("Headwind and A lines inflate cells"),Wing->LeftPressure>.9f&&Wing->RightPressure>.9f);
 TestFalse(TEXT("Six m/s wind alone does not launch pilot"),Wing->IsAirborne());
 TestTrue(TEXT("Canopy reaches overhead ground state"),Wing->IsInflated());
 Wing->SetIndependentTrimmers(-1,1);Wing->SetSpeedBar(.5);
 for(int I=0;I<360;++I)Wing->Tick(1.f/120);
 TestTrue(TEXT("Independent trimmers reach endpoints"),FMath::IsNearlyEqual(Wing->LeftTrim,-1.f)&&FMath::IsNearlyEqual(Wing->RightTrim,1.f));
 FVector HandleBefore=Wing->GetLeftBrakeHandleWorldPos();Wing->SetLeftBrake(.4f);for(int I=0;I<120;++I)Wing->Tick(1.f/120);
 TestTrue(TEXT("Brake control moves physical handle down"),Wing->GetLeftBrakeHandleWorldPos().Z<HandleBefore.Z-10);
 Wing->SetLeftBrake(0);Wing->SetIndependentTrimmers(0,0);Wing->SetSpeedBar(0);Wing->ReleaseALines();
 for(int I=0;I<240;++I){if(!Wing->IsAirborne())Char->GetCharacterMovement()->Velocity=FVector(650,0,0);Wing->Tick(1.f/120);}
 TestTrue(TEXT("Sufficient airflow permits physical takeoff without debug shortcut"),Wing->IsAirborne());
 TestFalse(TEXT("Flight position remains finite"),Char->GetActorLocation().ContainsNaN());
 Wing->DisconnectRisers();TestFalse(TEXT("Disconnect restores airborne flag"),Wing->IsAirborne());
 TestTrue(TEXT("Disconnect restores movement"),Char->GetCharacterMovement()->MovementMode!=MOVE_None);
 GEngine->DestroyWorldContext(W);W->DestroyWorld(false);
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChargerGroundContact,"Charger.Fresh.GroundClothLinesAndDragging",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FChargerGroundContact::RunTest(const FString&){
 UWorld* W=UWorld::CreateWorld(EWorldType::Game,false);auto& Context=GEngine->CreateNewWorldContext(EWorldType::Game);Context.SetCurrentWorld(W);
 auto Box=[&](FVector P,FVector Extent){auto* A=W->SpawnActor<AActor>(P,FRotator::ZeroRotator);auto* C=NewObject<UBoxComponent>(A);A->SetRootComponent(C);C->SetBoxExtent(Extent);C->SetCollisionProfileName(TEXT("BlockAll"));C->RegisterComponent();C->SetWorldLocation(P);};
 Box(FVector(0,0,-20),FVector(2000,2000,20));Box(FVector(-75,375,12),FVector(200,80,12));
 auto* Wing=W->SpawnActor<AChargerWingActor>(FVector(0,0,5),FRotator::ZeroRotator);W->BeginPlay();if(!Wing->HasActorBegunPlay())Wing->DispatchBeginPlay();
 TestEqual(TEXT("Spawn has zero inflation"),Wing->GetInflationProgress(),0.f);
 TestTrue(TEXT("Spawned fabric follows raised terrain"),Wing->GroundCloth[38*7+3].Z>24);
 for(int I=0;I<120;++I)Wing->Tick(1.f/120);
 double MaxLineHeight=0,MinLineClearance=1000;
 for(const FVector& P:Wing->NodePositions){double H=P.Z-Wing->TerrainHeight(P);MaxLineHeight=FMath::Max(MaxLineHeight,H);MinLineClearance=FMath::Min(MinLineClearance,H);}
 AddInfo(FString::Printf(TEXT("Maximum line clearance above local terrain: %.2f cm"),MaxLineHeight));
 // Lines may bridge the 24 cm step; they must not remain hanging overhead.
 TestTrue(TEXT("Unconnected lines settle within terrain obstacle height"),MaxLineHeight<30);
 TestTrue(TEXT("Lines stay above terrain"),MinLineClearance>=-.1);
 FVector Hand=Wing->GroundCloth[23*7],Far=Wing->GroundCloth[0];TestTrue(TEXT("Can grab actual leading edge"),Wing->GrabNearestEdge(Hand));
 for(int I=0;I<240;++I){Wing->UpdateGrabPosition(Hand+FVector(200,0,80)*(float(I+1)/240));Wing->Tick(1.f/120);}
 TestTrue(TEXT("Grabbed fabric follows hand"),FVector::Dist(Wing->GroundCloth[Wing->GrabbedParticleIndex],Hand+FVector(200,0,80))<5);
 TestTrue(TEXT("Far fabric does not move as a rigid wing"),FVector::Dist2D(Wing->GroundCloth[0],Far)<180);
 Wing->ReleaseEdge();for(int I=0;I<240;++I)Wing->Tick(1.f/120);
 bool Finite=true,Above=true;for(const auto& P:Wing->GroundCloth){Finite&=!P.ContainsNaN();Above&=P.Z>=Wing->TerrainHeight(P)-.1;}
 TestTrue(TEXT("Dragged cloth remains finite"),Finite);TestTrue(TEXT("Released cloth respects terrain"),Above);
 TestTrue(TEXT("Released leading edge drops from hand height"),Wing->GroundCloth[23*7].Z<Hand.Z+40);
 GEngine->DestroyWorldContext(W);W->DestroyWorld(false);return true;
}
#endif
