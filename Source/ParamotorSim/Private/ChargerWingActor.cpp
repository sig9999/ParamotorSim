#include "ChargerWingActor.h"
#include "ChargerRigData.h"
#include "ParamotorActor.h"
#include "ParamotorGameInstance.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Components/CapsuleComponent.h"

namespace {
Charger::V3 ChargerToSI(FVector V){return {V.X*.01,V.Y*.01,V.Z*.01};}
FVector ChargerToUE(Charger::V3 V){return FVector(V.x,V.y,V.z)*100.;}
FLinearColor SailColor(int I){return I==0?FLinearColor(.85,.21,.008):I==1?FLinearColor(.86,.91,.94):FLinearColor(.018,.36,.68);}
}
AChargerWingActor::AChargerWingActor(){
 WingModel.Name=TEXT("MAC PARA Charger 1 25 — fresh build");WingModel.WingArea=25.43f;WingModel.WingWeight=5.6f;WingModel.MaxTakeoffWeight=137;WingModel.AR=5.14f;
 CanopyMesh->SetCastShadow(true);LineMesh->SetVisibility(true);WindDirection=FVector(-1,0,0);
}
void AChargerWingActor::BeginPlay(){
 AActor::BeginPlay();
 CachedGameInstance=GetGameInstance<UParamotorGameInstance>();
 GroundHeight=GetActorLocation().Z;FallbackGroundHeight=GroundHeight-5;CanopyCenter=GetActorLocation();Dynamics.yaw=FMath::DegreesToRadians(GetActorRotation().Yaw);
 WingState=EWingState::Spread;CanopyMesh->SetVisibility(true);LineMesh->SetVisibility(true);BagMesh->SetVisibility(false);
 auto* Material=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/ChargerFresh/M_ChargerFabric.M_ChargerFabric"));
 if(!Material)Material=LoadObject<UMaterialInterface>(nullptr,TEXT("/Engine/EngineDebugMaterials/VertexColorMaterial.VertexColorMaterial"));
 CanopyMesh->SetMaterial(0,Material);LineMesh->SetMaterial(0,Material);
 InitGroundCloth();BuildSail();UpdateMeshes();
 UE_LOG(LogTemp,Display,TEXT("Fresh Charger initialized: 46 cells; independent reflex dynamics; legacy solver inactive"));
}
ACharacter* AChargerWingActor::Pilot() const{return ConnectedParamotor?Cast<ACharacter>(ConnectedParamotor->GetAttachParentActor()):nullptr;}
void AChargerWingActor::EndPlay(const EEndPlayReason::Type Reason){if(bIsAirborne)RestoreCharacter(false);AActor::EndPlay(Reason);}
void AChargerWingActor::Tick(float Dt){
 AActor::Tick(Dt);Accumulator+=FMath::Min(Dt,.1f);VisualTime+=Dt;
 if(auto* PC=Cast<APlayerController>(UGameplayStatics::GetPlayerController(this,0));PC&&bRisersConnected){
  // Existing Q/E remain main brakes. V is speed bar; Z/C are independent tips.
  SetSpeedBar(PC->IsInputKeyDown(EKeys::V)?1.f:0.f);
  SetWingtipSteering(PC->IsInputKeyDown(EKeys::Z)?1.f:0.f,PC->IsInputKeyDown(EKeys::C)?1.f:0.f);
 }
 while(Accumulator>=1./120.){Step(1.f/120);Accumulator-=1./120.;}
 if(VisualTime>=1./30.){UpdateMeshes();VisualTime=0;}
}
void AChargerWingActor::SetIndependentTrimmers(float L,float R){bIndependentTrim=true;LeftTrimTarget=FMath::Clamp(L,-1.f,1.f);RightTrimTarget=FMath::Clamp(R,-1.f,1.f);}
void AChargerWingActor::SetSpeedBar(float Amount){SpeedBarAmount=FMath::Clamp(Amount,0.f,1.f);}
void AChargerWingActor::SetWingtipSteering(float L,float R){Inputs.tip[0]=FMath::Clamp(L,0.f,1.f);Inputs.tip[1]=FMath::Clamp(R,0.f,1.f);}
void AChargerWingActor::Step(float Dt){
 Elapsed+=Dt;
 if(!FMath::IsNearlyEqual(LastSharedTrim,TrimmerTarget)){bIndependentTrim=false;LastSharedTrim=TrimmerTarget;}
 if(!bIndependentTrim)LeftTrimTarget=RightTrimTarget=TrimmerTarget;
 LeftTrim=FMath::FInterpConstantTo(LeftTrim,LeftTrimTarget,Dt,.5f);RightTrim=FMath::FInterpConstantTo(RightTrim,RightTrimTarget,Dt,.5f);TrimmerPosition=(LeftTrim+RightTrim)*.5f;
 LeftBrakeAmount=FMath::FInterpTo(LeftBrakeAmount,LeftBrakeTarget,Dt,5.f);RightBrakeAmount=FMath::FInterpTo(RightBrakeAmount,RightBrakeTarget,Dt,5.f);
 Inputs.trim[0]=LeftTrim;Inputs.trim[1]=RightTrim;Inputs.brake[0]=LeftBrakeAmount;Inputs.brake[1]=RightBrakeAmount;Inputs.speedbar=SpeedBarAmount;Inputs.weightshift=WeightShift;Inputs.aLines=bLiftingALines;
 const double WindBase=CachedGameInstance?CachedGameInstance->WindSpeed:6.;
 const double Gust=CachedGameInstance?CachedGameInstance->WindGusts:0.;
 CurrentWindSpeed=FMath::Max(0.,WindBase+Gust*(.55*FMath::Sin(Elapsed*.71)+.25*FMath::Sin(Elapsed*1.93)));
 WindTurbulence=CurrentWindSpeed-WindBase;WindDirection=FRotator(0,WindHeadingDegrees,0).Vector();
 auto* Char=Pilot();FVector Velocity=Char?(bIsAirborne?FlightVelocity:Char->GetVelocity()):FVector::ZeroVector;
 if(!bRisersConnected){
  Dynamics.pressure[0]=Dynamics.pressure[1]=0;
  SimulateGroundCloth(Dt);SimulateLines(Dt);return;
 }
 FVector Air=Velocity-GetWindVector();
 if(!bIsAirborne){Dynamics.yaw=FMath::DegreesToRadians(GetActorRotation().Yaw);Dynamics.pitch=.09;}
 Forces=Charger::Evaluate(Dynamics,Inputs,ChargerToSI(Air),Charger::Density(Char?Char->GetActorLocation().Z*.01:0),Dt,!bIsAirborne);
 LeftPressure=Dynamics.pressure[0];RightPressure=Dynamics.pressure[1];InflationProgress=(LeftPressure+RightPressure)*.5f;
 LeftCollapseAmount=Dynamics.collapse[0];RightCollapseAmount=Dynamics.collapse[1];FrontalCollapseAmount=FMath::Min(LeftCollapseAmount,RightCollapseAmount);ReflexAmount=(Forces.reflex[0]+Forces.reflex[1])*.5;
 Airspeed=Air.Size();ClimbRate=Velocity.Z;DebugGroundAirspeed=Airspeed;DebugGroundSpeed=Velocity.Size2D();
 DebugHeadwind=-FVector::DotProduct(GetWindVector(),GetActorForwardVector());DebugWeightSupport=Forces.lift.z/((PilotWeight+5.6)*9.81);
 DebugTakeoffThreshold=FMath::Sqrt(2*(PilotWeight+5.6)*9.81/(1.225*25.43*1.5))*100;
 FVector Pivot=(LeftCarabinerPos+RightCarabinerPos)*.5;
 if(!bIsAirborne){
  const double TangentForce=Forces.liftN*FMath::Cos(KiteAngle)+Forces.dragN*FMath::Sin(KiteAngle)-5.6*9.81*FMath::Cos(KiteAngle);
  KiteRate+=FMath::Clamp(TangentForce/(5.6*6.62)-2.4*KiteRate,-6.,6.)*Dt;
  KiteAngle=FMath::Clamp(KiteAngle+KiteRate*Dt,0.,1.9);
  if(KiteAngle<=0&&KiteRate<0)KiteRate=0;
  CanopyCenter=Pivot+(-GetActorForwardVector()*FMath::Cos(KiteAngle)+FVector::UpVector*FMath::Sin(KiteAngle))*662+GetActorRightVector()*FMath::Sin(Dynamics.roll)*240;
  CanopyCenter.Z=FMath::Max<double>(GroundHeight+3,CanopyCenter.Z);
  WingState=InflationProgress>.7&&KiteAngle>1.25?EWingState::Inflated:(InflationProgress>.05?EWingState::Inflating:EWingState::Connected);
  if(Char&&InflationProgress>.3){
   FVector Pull=ChargerToUE(Forces.drag)*FMath::Sin(KiteAngle);
   Char->GetCharacterMovement()->AddForce(Pull);
  }
  if(bEnableFlight&&Char&&WingState==EWingState::Inflated&&DebugWeightSupport>1.02&&FVector::DotProduct(Air,Char->GetActorForwardVector())>500)EnterFlight();
 }else if(Char){
  const double Mass=PilotWeight+5.6;
  FVector Thrust=Char->GetActorForwardVector()*(ConnectedParamotor?ConnectedParamotor->GetThrustForceN(Airspeed*.01f):0);
  FVector Acceleration=ChargerToUE(Forces.force)/Mass+Thrust*(100/Mass)+FVector(0,0,-981);
  FlightVelocity+=Acceleration*Dt;
  FHitResult Hit;Char->SetActorLocation(Char->GetActorLocation()+FlightVelocity*Dt,true,&Hit);
  if(Hit.IsValidBlockingHit()){
   if(Hit.Normal.Z>.6&&FlightVelocity.Z<=0){RestoreCharacter(true);WingState=EWingState::Inflated;KiteAngle=1.57;}
   else FlightVelocity=FVector::VectorPlaneProject(FlightVelocity,Hit.Normal);
  }
  Char->GetCharacterMovement()->Velocity=FlightVelocity;
  if(bIsAirborne){
   Char->SetActorRotation(FRotator(FMath::RadiansToDegrees(Dynamics.pendulum),FMath::RadiansToDegrees(Dynamics.yaw),-FMath::RadiansToDegrees(Dynamics.roll)));
   Dynamics.pendulumRate+=(-9.81/6.62*FMath::Sin(Dynamics.pendulum)-.8*Dynamics.pendulumRate-FVector::DotProduct(Acceleration,Char->GetActorForwardVector())*.01/6.62)*Dt;
   Dynamics.pendulum+=Dynamics.pendulumRate*Dt;PendulumAngle=FMath::RadiansToDegrees(Dynamics.pendulum);
   Pivot+=FlightVelocity*Dt;CanopyCenter=Pivot+FVector(0,0,662)+Char->GetActorForwardVector()*(FMath::Sin(Dynamics.pendulum)*180);
   SetActorRotation(FRotator(0,FMath::RadiansToDegrees(Dynamics.yaw),0));WingState=EWingState::Flying;
  }
 }
 GlideRatio=FMath::Abs(ClimbRate)>1?Airspeed/FMath::Abs(ClimbRate):0;
 SimulateLines(Dt);
}
void AChargerWingActor::EnterFlight(){
 auto* Char=Pilot();if(!Char||bIsAirborne)return;auto* M=Char->GetCharacterMovement();
 SavedGravity=M->GravityScale;SavedAcceleration=M->MaxAcceleration;SavedAirControl=M->AirControl;SavedYaw=Char->bUseControllerRotationYaw;SavedOrient=M->bOrientRotationToMovement;SavedDesired=M->bUseControllerDesiredRotation;
 FlightVelocity=M->Velocity;M->SetMovementMode(MOVE_None);M->Velocity=FlightVelocity;M->GravityScale=0;M->MaxAcceleration=0;Char->bUseControllerRotationYaw=false;M->bOrientRotationToMovement=false;M->bUseControllerDesiredRotation=false;
 bIsAirborne=true;WingState=EWingState::Flying;Dynamics.yaw=FMath::DegreesToRadians(Char->GetActorRotation().Yaw);
}
void AChargerWingActor::RestoreCharacter(bool Grounded){
 if(auto* Char=Pilot()){auto* M=Char->GetCharacterMovement();M->GravityScale=SavedGravity;M->MaxAcceleration=SavedAcceleration;M->AirControl=SavedAirControl;Char->bUseControllerRotationYaw=SavedYaw;M->bOrientRotationToMovement=SavedOrient;M->bUseControllerDesiredRotation=SavedDesired;M->Velocity=FlightVelocity;M->SetMovementMode(Grounded?MOVE_Walking:MOVE_Falling);Char->SetActorRotation(FRotator(0,Char->GetActorRotation().Yaw,0));}
 bIsAirborne=false;bSeated=false;
}
bool AChargerWingActor::UnpackFromBag(){WingState=EWingState::Spread;return true;}
bool AChargerWingActor::GrabNearestEdge(FVector Hand){if(bRisersConnected)return false;float Best=180.f;int Index=INDEX_NONE;for(int I=0;I<GroundCloth.Num();++I){if(I%7!=0&&I%7!=6&&I/7!=0&&I/7!=46)continue;float Distance=FVector::Dist(Hand,GroundCloth[I]);if(Distance<Best){Best=Distance;Index=I;}}if(Index==INDEX_NONE)return false;GrabbedParticleIndex=Index;GrabHandPos=Hand;return true;}
void AChargerWingActor::ReleaseEdge(){GrabbedParticleIndex=-1;}
void AChargerWingActor::UpdateGrabPosition(FVector Hand){GrabHandPos=Hand;}
void AChargerWingActor::ConnectRisers(AParamotorActor* Motor,FVector L,FVector R){ConnectedParamotor=Motor;LeftCarabinerPos=L;RightCarabinerPos=R;bRisersConnected=Motor!=nullptr;WingState=EWingState::Connected;GrabbedParticleIndex=-1;KiteAngle=KiteRate=0;bNodesInitialized=false;}
void AChargerWingActor::DisconnectRisers(){if(bIsAirborne)RestoreCharacter(false);for(int I=0;I<GroundCloth.Num();++I){float S=-1.f+float(I/7)/23,U=float(I%7)/6;GroundCloth[I]=SailPoint({S,U,0,0,FLinearColor::White});GroundPrevious[I]=GroundCloth[I];}ConnectedParamotor=nullptr;bRisersConnected=false;WingState=EWingState::Spread;}
void AChargerWingActor::LiftALines(){bLiftingALines=bRisersConnected;}
void AChargerWingActor::ReleaseALines(){bLiftingALines=false;}
void AChargerWingActor::ForceTakeoff(){
#if WITH_EDITOR || UE_BUILD_DEVELOPMENT
 if(!bEnableFlight||!Pilot()||!bRisersConnected)return;Dynamics.pressure[0]=Dynamics.pressure[1]=1;KiteAngle=1.57;EnterFlight();Pilot()->AddActorWorldOffset(FVector(0,0,500),true);FlightVelocity=GetActorForwardVector()*1150+GetWindVector();
#endif
}
float AChargerWingActor::GetTakeoffChance()const{return FMath::Clamp(DebugWeightSupport*100.f,0.f,100.f);}
FVector AChargerWingActor::GetLeftBrakeHandleWorldPos()const{return HandlePoint(-1);}
FVector AChargerWingActor::GetRightBrakeHandleWorldPos()const{return HandlePoint(1);}
FVector AChargerWingActor::RiserPoint(int Side,int Row)const{
 FVector Pivot=Side<0?LeftCarabinerPos:RightCarabinerPos;
 if(!bRisersConnected){if(bNodesInitialized){for(int I=0;I<UE_ARRAY_COUNT(ChargerData::Nodes);++I){auto& N=ChargerData::Nodes[I];if(N.Kind==2&&N.Side==Side&&N.Row==Row)return NodePositions[I];}}Pivot=GetActorLocation()+GetActorForwardVector()*450+GetActorRightVector()*Side*21;Pivot.Z=TerrainHeight(Pivot)+1;}
 auto Length=Charger::RiserLengths(Side<0?LeftTrim:RightTrim,SpeedBarAmount)[Row];
 FVector Up=bRisersConnected?(CanopyCenter-Pivot).GetSafeNormal():GetActorForwardVector();
 return Pivot+Up*Length*100+GetActorForwardVector()*(12-7*Row);
}
FVector AChargerWingActor::HandlePoint(int Side,bool Tip)const{
 if(!bRisersConnected&&bNodesInitialized){for(int I=0;I<UE_ARRAY_COUNT(ChargerData::Nodes);++I){auto& N=ChargerData::Nodes[I];if(N.Kind==(Tip?4:3)&&N.Side==Side)return NodePositions[I];}}
 const double Brake=Tip?Inputs.tip[Side<0?0:1]:(Side<0?LeftBrakeAmount:RightBrakeAmount);
 return RiserPoint(Side,Tip?3:4)-FVector(0,0,18+Brake*50)+GetActorRightVector()*Side*(Tip?9:4);
}
FVector AChargerWingActor::SailPoint(const FSample& P)const{
 if(!bRisersConnected&&GroundCloth.Num())return GroundClothPoint(P.S,P.U,P.Skin);
 const double S=P.S,U=P.U;int Side=S<0?0:1;double F=bRisersConnected?Dynamics.pressure[Side]:0;
 double Arc=ChargerData::Arch*(.1*S+.9*S*S*S)*F,C=2.71*(1-ChargerData::Taper*FMath::Pow(FMath::Abs(S),3));
 double T=5*.16*(.2969*FMath::Sqrt(FMath::Max(0.,U))-.126*U-.3516*U*U+.2843*U*U*U-.1036*U*U*U*U);
 double Aft=FMath::Pow(FMath::Max(0.,(U-.6)/.4),2);
 double Camber=.035*FMath::Sin(PI*U)-Forces.reflex[Side]*.024*Aft;
 double Z=(Camber+P.Skin*T+P.Skin*.004*P.Billow*FMath::Sin(PI*U))*C*F;
 Z-=(Inputs.brake[Side]+.55*Inputs.tip[Side]*FMath::Max(0.,(FMath::Abs(S)-.65)/.35))*.24*C*Aft;
 Z-=Dynamics.collapse[Side]*.45*FMath::Sin(PI*U);
 double Y=0,Drop=0,Step=FMath::Abs(S)/24;
 for(int I=0;I<24;++I){double Q=(I+.5)*Step,A=ChargerData::Arch*(.1*Q+.9*Q*Q*Q)*F;Y+=FMath::Cos(A)*Step*5.72;Drop+=FMath::Sin(A)*Step*5.72;}
 FVector Local((.68-.14*S*S-U*C)*100,(S<0?-Y:Y)*100,(-Drop+Z)*100);
 if(F<1)Local.Z+=(1-F)*2*FMath::Sin(S*77+U*13);
 FQuat Bank=FQuat(GetActorForwardVector(),-Dynamics.roll*InflationProgress);
 FVector World=CanopyCenter+Bank.RotateVector(GetActorQuat().RotateVector(Local));
 World.Z=FMath::Max<double>(GroundHeight+2,World.Z);
 if(InflationProgress<1&&GroundCloth.Num())World=FMath::Lerp(GroundClothPoint(P.S,P.U,P.Skin),World,FMath::Clamp(InflationProgress,0.f,1.f));return World;
}
float AChargerWingActor::TerrainHeight(FVector Position)const{
 FHitResult Hit;FCollisionQueryParams Q(SCENE_QUERY_STAT(ChargerGround),false,this);if(ConnectedParamotor)Q.AddIgnoredActor(ConnectedParamotor);if(Pilot())Q.AddIgnoredActor(Pilot());
 if(GetWorld()->LineTraceSingleByChannel(Hit,Position+FVector(0,0,250),Position-FVector(0,0,100000),ECC_WorldStatic,Q))return Hit.ImpactPoint.Z;return FallbackGroundHeight;
}
void AChargerWingActor::InitGroundCloth(){
 GroundCloth.Reset();GroundPrevious.Reset();GroundConstraints.Reset();
 for(int I=0;I<47;++I)for(int J=0;J<7;++J){double S=-1.+I/23.,U=J/6.,C=2.71*(1-ChargerData::Taper*FMath::Pow(FMath::Abs(S),3));FVector P=GetActorTransform().TransformPosition(FVector((.68-.14*S*S-U*C)*100,S*572,0));P.Z=TerrainHeight(P)+.8+.5*FMath::Sin(I*2.1+J);GroundCloth.Add(P);GroundPrevious.Add(P);}
 auto Add=[&](int A,int B){GroundConstraints.Add({A,B,float(FVector::Dist(GroundCloth[A],GroundCloth[B]))});};
 for(int I=0;I<47;++I)for(int J=0;J<7;++J){int A=I*7+J;if(I<46)Add(A,A+7);if(J<6)Add(A,A+1);if(I<46&&J<6){Add(A,A+8);Add(A+1,A+7);}}
}
FVector AChargerWingActor::GroundClothPoint(float S,float U,float Skin)const{
 float Span=FMath::Clamp((S+1)*23.f,0.f,46.f),Chord=FMath::Clamp(U*6,0.f,6.f);int I=FMath::Min(45,FMath::FloorToInt(Span)),J=FMath::Min(5,FMath::FloorToInt(Chord));float A=Span-I,B=Chord-J;
 return FMath::Lerp(FMath::Lerp(GroundCloth[I*7+J],GroundCloth[(I+1)*7+J],A),FMath::Lerp(GroundCloth[I*7+J+1],GroundCloth[(I+1)*7+J+1],A),B)+FVector(0,0,Skin*.18);
}
void AChargerWingActor::SimulateGroundCloth(float Dt){
 if(!GroundCloth.Num())return;
 for(int I=0;I<GroundCloth.Num();++I){FVector P=GroundCloth[I],V=(P-GroundPrevious[I])*.99;GroundPrevious[I]=P;GroundCloth[I]+=V+FVector(0,0,-981)*Dt*Dt;}
 for(int Iter=0;Iter<7;++Iter){
  if(IsGrabbing())GroundCloth[GrabbedParticleIndex]=GrabHandPos;
  for(const auto& C:GroundConstraints){FVector D=GroundCloth[C.B]-GroundCloth[C.A];float Len=D.Size();if(Len<.001f)continue;float WA=C.A==GrabbedParticleIndex?0:1,WB=C.B==GrabbedParticleIndex?0:1;if(WA+WB==0)continue;FVector Correction=D*((Len-C.Length)/Len/(WA+WB)*.8);GroundCloth[C.A]+=Correction*WA;GroundCloth[C.B]-=Correction*WB;}
 }
 for(int I=0;I<GroundCloth.Num();++I){if(I==GrabbedParticleIndex)continue;float Z=TerrainHeight(GroundCloth[I])+.7f;if(GroundCloth[I].Z<Z){GroundCloth[I].Z=Z;FVector V=GroundCloth[I]-GroundPrevious[I];GroundPrevious[I]=GroundCloth[I]-FVector(V.X*.72,V.Y*.72,0);}}
 CanopyCenter=GroundCloth[23*7];SetActorLocation(CanopyCenter);GroundHeight=TerrainHeight(CanopyCenter);
}
void AChargerWingActor::BuildSail(){
 constexpr int SpanSteps=3,ChordSteps=10;
 for(int Cell=0;Cell<46;++Cell)for(int Skin:{1,-1})for(int Band=0;Band<3;++Band){
  int Base=Samples.Num();
  for(int J=0;J<=SpanSteps;++J)for(int K=0;K<=ChordSteps;++K){
   float S=-1.f+2.f*(Cell+float(J)/SpanSteps)/46;float Bound=Skin<0?.88f:.23f+.15f*FMath::Pow(FMath::Abs(S),6);
   float Low=Band==0?(Skin<0?.065f:0.f):(Band==1?.13f:Bound),High=Band==0?.13f:(Band==1?Bound:1.f);
   Samples.Add({S,FMath::Lerp(Low,High,float(K)/ChordSteps),float(Skin),FMath::Sin(PI*J/SpanSteps),SailColor(Band)});
  }
  for(int J=0;J<SpanSteps;++J)for(int K=0;K<ChordSteps;++K){int A=Base+J*(ChordSteps+1)+K,B=A+1,C=A+ChordSteps+2,D=A+ChordSteps+1;if(Skin>0)SailTriangles.Append({A,C,B,A,D,C});else SailTriangles.Append({A,B,C,A,C,D});}
 }
 for(int Rib=0;Rib<47;++Rib)for(int K=0;K<2;++K){
  float S=-1.f+Rib/23.f,U0=.065f+.935f*K/20,U1=.065f+.935f*(K+1)/20;
  for(int Band=0;Band<2;++Band){float Low=Band==0?-1.f:.5f,High=Band==0?-.5f:1.f;if(U0<.24f||U0>.72f){if(Band==1)continue;Low=-1;High=1;}
   int B=Samples.Num();Samples.Add({S,U0,Low,0,SailColor(1)});Samples.Add({S,U1,Low,0,SailColor(1)});Samples.Add({S,U1,High,0,SailColor(1)});Samples.Add({S,U0,High,0,SailColor(1)});SailTriangles.Append({B,B+1,B+2,B,B+2,B+3});
  }
 }
 SailVertices.SetNum(Samples.Num());SailNormals.SetNum(Samples.Num());
 for(const auto& S:Samples){SailUV.Add(FVector2D((S.S+1)*.5,S.U));SailColors.Add(S.Color);SailTangents.Add(FProcMeshTangent(-1,0,0));}
}
void AChargerWingActor::SimulateLines(float Dt){
 int N=UE_ARRAY_COUNT(ChargerData::Nodes);
 if(!bNodesInitialized){
  NodePositions.SetNum(N);LinePaths.Reset();LineConstraints.Reset();
  for(int I=0;I<N;++I){const auto& P=ChargerData::Nodes[I];FVector V=GetActorTransform().TransformPosition(FVector(P.X*100+450*(1-P.Z/6.62),P.Y*100,0));
   if(P.Kind==1)V=SailPoint({float(P.S),float(P.U),-1,0,FLinearColor::White});
   else {if(P.Kind==2)V=GetActorLocation()+GetActorForwardVector()*(502.5+P.X*100)+GetActorRightVector()*P.Side*21;V.Z=TerrainHeight(V)+.5;}
   NodePositions[I]=V;
  }
  for(const auto& E:ChargerData::Edges){TArray<int32> Path;Path.Add(E.A);int Count=FMath::Clamp(FMath::CeilToInt(E.Length/.45),2,14);
   for(int J=1;J<Count;++J){FVector P=FMath::Lerp(NodePositions[E.A],NodePositions[E.B],float(J)/Count);P.Z=TerrainHeight(P)+.5;Path.Add(NodePositions.Add(P));}
   Path.Add(E.B);for(int J=1;J<Path.Num();++J)LineConstraints.Add({Path[J-1],Path[J],float(E.Length*100/Count)});LinePaths.Add(MoveTemp(Path));
  }
  for(int I=0;I<N;++I)for(int J=I+1;J<N;++J){const auto& A=ChargerData::Nodes[I];const auto& B=ChargerData::Nodes[J];if(A.Kind>=2&&B.Kind>=2&&A.Side==B.Side)LineConstraints.Add({I,J,float(FVector::Dist(NodePositions[I],NodePositions[J]))});}
  NodePrevious=NodePositions;bNodesInitialized=true;
 }
 auto Pinned=[&](int I){return I<N&&(ChargerData::Nodes[I].Kind==1||(bRisersConnected&&ChargerData::Nodes[I].Kind>=2));};
 for(int I=0;I<NodePositions.Num();++I){FVector Old=NodePositions[I];int Kind=I<N?ChargerData::Nodes[I].Kind:0;
  if(Kind==1){const auto& P=ChargerData::Nodes[I];NodePositions[I]=SailPoint({float(P.S),float(P.U),-1,0,FLinearColor::White});}
  else if(bRisersConnected&&Kind==2)NodePositions[I]=RiserPoint(ChargerData::Nodes[I].Side,ChargerData::Nodes[I].Row);
  else if(bRisersConnected&&Kind>=3)NodePositions[I]=HandlePoint(ChargerData::Nodes[I].Side,Kind==4);
  else NodePositions[I]+=(NodePositions[I]-NodePrevious[I])*.99+FVector(0,0,-981)*Dt*Dt;
  NodePrevious[I]=Old;
 }
 for(int Iter=0;Iter<12;++Iter)for(const auto& E:LineConstraints){
  FVector Delta=NodePositions[E.B]-NodePositions[E.A];double Distance=Delta.Size();double Extra=Distance-E.Length;
  if(Extra<=0||Distance<.001)continue;double WA=Pinned(E.A)?0:1,WB=Pinned(E.B)?0:1;
  if(WA+WB==0)continue;FVector Correction=Delta*(Extra/Distance/(WA+WB));NodePositions[E.A]+=Correction*WA;NodePositions[E.B]-=Correction*WB;
 }
 for(int I=0;I<NodePositions.Num();++I)if(!Pinned(I)&&(!bIsAirborne||NodePositions[I].Z<GroundHeight+300)){float Z=TerrainHeight(NodePositions[I])+.35f;if(NodePositions[I].Z<Z){NodePositions[I].Z=Z;FVector V=NodePositions[I]-NodePrevious[I];NodePrevious[I]=NodePositions[I]-FVector(V.X*.65,V.Y*.65,0);}}
}
void AChargerWingActor::UpdateMeshes(){
 FTransform Inv=GetActorTransform().Inverse();
 for(int I=0;I<Samples.Num();++I){SailVertices[I]=Inv.TransformPosition(SailPoint(Samples[I]));SailNormals[I]=FVector::ZeroVector;}
 for(int I=0;I<SailTriangles.Num();I+=3){int A=SailTriangles[I],B=SailTriangles[I+1],C=SailTriangles[I+2];FVector N=FVector::CrossProduct(SailVertices[B]-SailVertices[A],SailVertices[C]-SailVertices[A]);SailNormals[A]+=N;SailNormals[B]+=N;SailNormals[C]+=N;}
 for(auto& N:SailNormals)N.Normalize();
 if(!bMeshesCreated)CanopyMesh->CreateMeshSection_LinearColor(0,SailVertices,SailTriangles,SailNormals,SailUV,SailColors,SailTangents,false);
 else CanopyMesh->UpdateMeshSection_LinearColor(0,SailVertices,SailNormals,SailUV,SailColors,SailTangents);
 if(!bNodesInitialized)SimulateLines(1.f/120);
 LineVertices.Reset();LineNormals.Reset();LineColors.Reset();LineUV.Reset();LineTangents.Reset();LineTriangles.Reset();
 auto Tube=[&](FVector A,FVector B,double Radius,FLinearColor Color,bool Strap=false){
  A=Inv.TransformPosition(A);B=Inv.TransformPosition(B);FVector Axis=(B-A).GetSafeNormal(),N=FVector::CrossProduct(Axis,FVector::UpVector).GetSafeNormal();if(N.IsNearlyZero())N=FVector::RightVector;FVector V=FVector::CrossProduct(Axis,N);int Base=LineVertices.Num();
  for(int End=0;End<2;++End)for(int J=0;J<4;++J){FVector Off=N*FMath::Cos(J*PI/2)*(Strap?1.2:1.)+V*FMath::Sin(J*PI/2)*(Strap?.15:1.);LineVertices.Add((End?B:A)+Off*Radius);LineNormals.Add(Off.GetSafeNormal());LineColors.Add(Color);LineUV.Add(FVector2D(J*.25,End));LineTangents.Add(FProcMeshTangent(Axis,false));}
  for(int J=0;J<4;++J){int K=(J+1)%4;LineTriangles.Append({Base+J,Base+K,Base+K+4,Base+J,Base+K+4,Base+J+4});}
 };
 for(int I=0;I<LinePaths.Num();++I){const auto& E=ChargerData::Edges[I];const auto& P=LinePaths[I];for(int J=1;J<P.Num();++J)Tube(NodePositions[P[J-1]],NodePositions[P[J]],E.Radius*100,E.Color==0?FLinearColor(.8,.025,.009):E.Color==1?FLinearColor(.74,.67,.18):FLinearColor(.9,.21,.015));}
 for(int Side:{-1,1}){
  FVector Pivot=bRisersConnected?(Side<0?LeftCarabinerPos:RightCarabinerPos):RiserPoint(Side,0)-GetActorForwardVector()*52.5;
  FVector SideAxis=GetActorRightVector(),HardwareUp=bRisersConnected?FVector::UpVector:GetActorForwardVector();FLinearColor Metal(.35,.38,.40),Webbing(.025,.028,.03);
  auto Loop=[&](FVector Center,float Width,float Height,FLinearColor Col){FVector X=SideAxis*Width*.5f,Z=HardwareUp*Height*.5f;Tube(Center-X-Z,Center+X-Z,.25,Col);Tube(Center+X-Z,Center+X+Z,.25,Col);Tube(Center+X+Z,Center-X+Z,.25,Col);Tube(Center-X+Z,Center-X-Z,.25,Col);};
  Loop(Pivot+HardwareUp*2.5,3.6,7,Webbing);
  for(int Row=0;Row<5;++Row){auto R=RiserPoint(Side,Row);Tube(Pivot,R,1.05,Row<2?FLinearColor(.42,.045,.022):Webbing,true);FVector A=R-SideAxis*1.8,B=R+SideAxis*1.8,C=R+HardwareUp*3.4;Tube(A,B,.3,Metal);Tube(B,C,.3,Metal);Tube(C,A,.3,Metal);}
  FVector Buckle=FMath::Lerp(Pivot,RiserPoint(Side,4),.6f);Loop(Buckle,3.8,4.4,Metal);Tube(Buckle-SideAxis*1.9,Buckle+SideAxis*1.9,.45,Metal);
  FVector Tail=Buckle-HardwareUp*22;Tube(Buckle,Tail,.95,Webbing,true);Loop(Tail,3.3,3.5,FLinearColor(.65,.17,.02));
  for(int Mark=1;Mark<5;++Mark){FVector M=Buckle-HardwareUp*(Mark*4);Tube(M-SideAxis,M+SideAxis,.1,FLinearColor(.6,.6,.56));}
  for(int Row:{2,4}){FVector Center=RiserPoint(Side,Row)-HardwareUp*7;for(int J=0;J<12;++J){double A=J*2*PI/12,B=(J+1)*2*PI/12;Tube(Center+SideAxis*FMath::Cos(A)*1.4+HardwareUp*FMath::Sin(A)*1.4,Center+SideAxis*FMath::Cos(B)*1.4+HardwareUp*FMath::Sin(B)*1.4,.28,Metal);}}
  for(bool Tip:{false,true}){FVector H=HandlePoint(Side,Tip)+(bRisersConnected?FVector::ZeroVector:FVector(0,0,.8)),Y=GetActorRightVector()*(Tip?2.3:5.);FVector Top=H+HardwareUp*(Tip?3.:6.);Tube(H-Y,H+Y,Tip?.4:.75,FLinearColor(.018,.02,.025));Tube(H-Y,Top,Tip?.25:.4,FLinearColor(.018,.02,.025));Tube(H+Y,Top,Tip?.25:.4,FLinearColor(.018,.02,.025));}
 }
 if(!bMeshesCreated)LineMesh->CreateMeshSection_LinearColor(0,LineVertices,LineTriangles,LineNormals,LineUV,LineColors,LineTangents,false);
 else LineMesh->UpdateMeshSection_LinearColor(0,LineVertices,LineNormals,LineUV,LineColors,LineTangents);
 bMeshesCreated=true;
}
