#pragma once
#include <algorithm>
#include <cmath>
#include <array>

// Independent SI-unit model. Published dimensions/riser travel; estimated aero
// coefficients. This is an engineering simulation, not a validated training model.
namespace Charger {
constexpr double Pi=3.14159265358979323846;
inline double clamp(double x,double a,double b){return std::max(a,std::min(b,x));}
inline double mix(double a,double b,double t){return a+(b-a)*t;}
struct V3 {
 double x=0,y=0,z=0;
 V3 operator+(V3 b)const{return {x+b.x,y+b.y,z+b.z};}
 V3 operator-(V3 b)const{return {x-b.x,y-b.y,z-b.z};}
 V3 operator*(double k)const{return {x*k,y*k,z*k};}
 V3 operator/(double k)const{return *this*(1/k);}
 double dot(V3 b)const{return x*b.x+y*b.y+z*b.z;}
 V3 cross(V3 b)const{return {y*b.z-z*b.y,z*b.x-x*b.z,x*b.y-y*b.x};}
 double length()const{return std::sqrt(dot(*this));}
 V3 unit()const{double l=length();return l>1e-8?*this/l:V3{};}
};
struct Controls {double trim[2]={0,0},brake[2]={0,0},tip[2]={0,0},speedbar=0,weightshift=0;bool aLines=false;};
inline std::array<double,5> RiserLengths(double trim,double speed){
 const double neutral[5]={.525,.525,.525,.525,.525},slow[5]={.525,.525,.525,.510,.495},fast[5]={.525,.525,.525,.555,.595};
 const double barNeutral[5]={.400,.400,.455,.485,.525},barFast[5]={.400,.400,.455,.520,.595};
 std::array<double,5> result{};trim=clamp(trim,-1,1);speed=clamp(speed,0,1);
 for(int i=0;i<5;++i){double t=mix(neutral[i],trim>=0?fast[i]:slow[i],std::abs(trim));double b=mix(barNeutral[i],barFast[i],std::max(0.,trim))+(trim<0?t-neutral[i]:0);result[i]=mix(t,b,speed);}return result;
}
inline double Density(double altitude){return 1.225*std::pow(clamp(1-.0065*altitude/288.15,.2,1.1),4.25588);}
inline double ReferenceSpeed(double trim,double speed){return (trim<0?mix(41.5,38.,-trim):mix(41.5,48.5,trim))+speed*mix(9.,10.,std::max(0.,trim));}
struct State {
 double pressure[2]={0,0},collapse[2]={0,0},stall[2]={0,0};
 double pitch=.09,pitchRate=0,roll=0,rollRate=0,yaw=0,yawRate=0;
 double pendulum=0,pendulumRate=0;
};
struct Result {
 V3 lift,drag,force;double liftN=0,dragN=0,airspeed=0,aoa=0,reflex[2]={0,0},brakeForce[2]={0,0};
};
inline Result Evaluate(State& s,const Controls& c,V3 airVelocity,double rho,double dt,bool grounded){
 Result o;double v=airVelocity.length();o.airspeed=v;
 V3 forward={std::cos(s.yaw),std::sin(s.yaw),0},right={-std::sin(s.yaw),std::cos(s.yaw),0};
 double along=airVelocity.dot(forward),vertical=airVelocity.z;
 double flightpath=std::atan2(vertical,std::max(.5,along));o.aoa=s.pitch-flightpath;
 V3 dragdir=airVelocity.unit()*-1.;V3 up={0,0,1};
 V3 lift0=(up-airVelocity.unit()*up.dot(airVelocity.unit())).unit();
 V3 liftdir=(lift0*std::cos(s.roll)+right*std::sin(s.roll)).unit();
 double q=.5*rho*v*v,sideLift[2]={},sideDrag[2]={};
 for(int i=0;i<2;++i){
  double trim=clamp(c.trim[i],-1,1),brake=clamp(c.brake[i],0,1),bar=clamp(c.speedbar,0,1);
  double reflex=clamp(std::max(0.,trim)*(.62+.38*bar)-brake*1.8,0,1);o.reflex[i]=reflex;
  double localAlpha=o.aoa+brake*.13;
  double intake=clamp(along/5.,0,1)*clamp((localAlpha+.06)/.10,0,1);
  if(grounded&&s.pressure[i]<.3&&!c.aLines)intake*=.04;
  double stalled=clamp((localAlpha-.30)/.16,0,1);
  double unloaded=clamp((-localAlpha-.025-reflex*.025)/.09,0,1);
  // Brakes remove aft reflex and move load aft. Full acceleration magnifies
  // this destabilizing interaction; no arbitrary unconditional collapse trigger.
  double acceleratedBrake=std::max(0.,trim)*bar*brake*clamp((.16-localAlpha)/.14,0,1);
  double targetCollapse=clamp(unloaded+acceleratedBrake,0,1);
  s.collapse[i]+= (targetCollapse-s.collapse[i])*(1-std::exp(-dt/(targetCollapse>s.collapse[i]?.18:1.1)));
  s.stall[i]+=(stalled-s.stall[i])*(1-std::exp(-dt/.3));
  double targetPressure=intake*(1-s.collapse[i])*(1-.5*s.stall[i]);
  double tau=targetPressure>s.pressure[i]?(grounded?1.05:.55):.25;
  s.pressure[i]=clamp(s.pressure[i]+(targetPressure-s.pressure[i])*(1-std::exp(-dt/tau)),0,1);
  double refV=ReferenceSpeed(trim,bar)/3.6;
  double clTrim=2*115*9.81/(1.225*25.43*refV*refV);
  double cl=clamp(clTrim+3.6*(localAlpha-.09),-.25,1.5);
  cl*=1-.82*s.stall[i];
  double cd=.018+cl*cl/(Pi*5.14*.83)+.10*brake*brake+.55*s.stall[i]+.3*s.collapse[i];
  double area=12.715*s.pressure[i]*(1-.7*s.collapse[i]);
  sideLift[i]=q*area*cl;sideDrag[i]=q*area*cd;
  o.brakeForce[i]=q*.075*brake*(1+1.8*reflex+bar);
 }
 o.liftN=sideLift[0]+sideLift[1];o.dragN=sideDrag[0]+sideDrag[1]+q*.32;
 o.lift=liftdir*o.liftN;o.drag=dragdir*o.dragN;o.force=o.lift+o.drag;
 double steering=clamp((c.brake[1]-c.brake[0])*.8+(c.tip[1]-c.tip[0])*.48+c.weightshift*.25+(c.trim[0]-c.trim[1])*.16,-1,1);
 double asymmetry=(sideLift[0]-sideLift[1])/std::max(100.,std::abs(o.liftN));
 double rollTorque=2.1*steering+1.3*asymmetry-1.8*s.roll-2.3*s.rollRate;
 s.rollRate+=rollTorque*dt;s.roll=clamp(s.roll+s.rollRate*dt,-1.15,1.15);
 double yawTarget=9.81*std::tan(s.roll)/std::max(5.,v)+(sideDrag[1]-sideDrag[0])*.0002;
 s.yawRate+=(yawTarget-s.yawRate)*(1-std::exp(-dt/.65));s.yaw+=s.yawRate*dt;
 double trim=(c.trim[0]+c.trim[1])*.5;
 double desiredAoA=.09-.018*std::max(0.,trim)*c.speedbar;
 double pitchTorque=-2.4*(o.aoa-desiredAoA)-1.8*s.pitchRate-.18*(c.brake[0]+c.brake[1])*std::max(0.,trim)*c.speedbar;
 s.pitchRate+=pitchTorque*dt;s.pitch=clamp(s.pitch+s.pitchRate*dt,-.7,.8);
 return o;
}
}
