#pragma once
#include "WingActor.h"
#include "ChargerDynamics.h"
#include "ChargerWingActor.generated.h"

// Compatibility surface only: none of AWingActor's mesh or physics routines run.
UCLASS()
class PARAMOTORSIM_API AChargerWingActor : public AWingActor {
 GENERATED_BODY()
public:
 AChargerWingActor();
 virtual void BeginPlay() override;
 virtual void Tick(float DeltaTime) override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
 virtual bool UnpackFromBag() override;
 virtual bool GrabNearestEdge(FVector Hand) override;
 virtual void ReleaseEdge() override;
 virtual void UpdateGrabPosition(FVector Hand) override;
 virtual void ConnectRisers(AParamotorActor* Motor,FVector Left,FVector Right) override;
 virtual void DisconnectRisers() override;
 virtual void LiftALines() override;
 virtual void ReleaseALines() override;
 virtual void ForceTakeoff() override;
 virtual FVector GetLeftBrakeHandleWorldPos() const override;
 virtual FVector GetRightBrakeHandleWorldPos() const override;
 virtual float GetTakeoffChance() const override;
 UFUNCTION(BlueprintCallable,Category="Charger") void SetIndependentTrimmers(float Left,float Right);
 UFUNCTION(BlueprintCallable,Category="Charger") void SetSpeedBar(float Amount);
 UFUNCTION(BlueprintCallable,Category="Charger") void SetWingtipSteering(float Left,float Right);
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Charger") float WindHeadingDegrees=180;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Charger") bool bEnableFlight=true;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Charger") float SpeedBarAmount=0;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Charger") float LeftTrim=0;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Charger") float RightTrim=0;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Charger") float LeftPressure=0;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Charger") float RightPressure=0;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Charger") float ReflexAmount=0;
private:
 friend class FChargerGroundContact;
 struct FSample {float S,U,Skin,Billow;FLinearColor Color;};
 TArray<FSample> Samples;
 TArray<FVector> SailVertices,SailNormals,NodePositions,NodePrevious,LineVertices,LineNormals;
 TArray<FVector2D> SailUV,LineUV;
 TArray<FLinearColor> SailColors,LineColors;
 TArray<int32> SailTriangles,LineTriangles;
 TArray<FProcMeshTangent> SailTangents,LineTangents;
 struct FGroundConstraint {int A,B;float Length;};
 TArray<FVector> GroundCloth,GroundPrevious;
 TArray<FGroundConstraint> GroundConstraints,LineConstraints;
 TArray<TArray<int32>> LinePaths;
 Charger::State Dynamics;
 Charger::Controls Inputs;
 Charger::Result Forces;
 double Accumulator=0,Elapsed=0,VisualTime=0,KiteAngle=0,KiteRate=0;
 FVector FlightVelocity=FVector::ZeroVector,CanopyCenter=FVector::ZeroVector;
 float GroundHeight=0,LeftTrimTarget=0,RightTrimTarget=0,LastSharedTrim=0;
 float FallbackGroundHeight=0;
 bool bIndependentTrim=false,bMeshesCreated=false,bNodesInitialized=false;
 float SavedGravity=1,SavedAcceleration=2048,SavedAirControl=.35f;
 bool SavedYaw=false,SavedOrient=false,SavedDesired=false;
 FVector SailPoint(const FSample& P) const;
 FVector RiserPoint(int Side,int Row) const;
 FVector HandlePoint(int Side,bool Tip=false) const;
 void BuildSail();
 void UpdateMeshes();
 void SimulateLines(float Dt);
 void InitGroundCloth();
 void SimulateGroundCloth(float Dt);
 FVector GroundClothPoint(float S,float U,float Skin) const;
 float TerrainHeight(FVector Position) const;
 void Step(float Dt);
 void EnterFlight();
 void RestoreCharacter(bool Grounded);
 class ACharacter* Pilot() const;
};
