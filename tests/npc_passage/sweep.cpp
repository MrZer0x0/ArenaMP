#include <apps/openmw/mwphysics/npcpassagerules.hpp>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
using btScalar=float;
constexpr float SIMD_EPSILON=1e-6;
float btSqrt(float x){return std::sqrt(x);} float btFabs(float x){return std::abs(x);}
struct btVector3 {
 float a,b,c;btVector3(float x=0,float y=0,float z=0):a(x),b(y),c(z){}
 float x()const{return a;}float y()const{return b;}float z()const{return c;}
 void setZ(float x){c=x;}float length2()const{return a*a+b*b+c*c;}
 float dot(btVector3 v)const{return a*v.a+b*v.b+c*v.c;}
 btVector3 operator+(btVector3 v)const{return {a+v.a,b+v.b,c+v.c};}
 btVector3 operator-(btVector3 v)const{return {a-v.a,b-v.b,c-v.c};}
 btVector3 operator-()const{return {-a,-b,-c};}
 btVector3 operator*(float f)const{return {a*f,b*f,c*f};}
 void normalize(){float f=1/std::sqrt(length2());a*=f;b*=f;c*=f;}
};
namespace osg { using Vec3f=btVector3; }
struct Transform{btVector3 p;btVector3 getOrigin()const{return p;}};
struct btCollisionObject{Transform t;Transform getWorldTransform()const{return t;}};
struct Actor{btVector3 half{22,22,64};btVector3 getHalfExtents()const{return half;}};
// INSERT_REAL_SWEEP
int main(){
 using MWPhysics::NpcPassageRules::scale;
 assert(scale(true,true,true,false,.2,.88)==.2f);
 assert(scale(true,true,false,true,.2,.88)==.2f); // remote/local direction symmetric
 assert(scale(true,true,true,true,.2,.88)==1.f); // player/player unchanged
 assert(scale(true,true,false,false,.2,.88)==.88f);
 assert(scale(true,false,true,false,.2,.88)==1.f); // creatures unchanged
 assert(scale(true,true,true,false,std::numeric_limits<float>::quiet_NaN(),.88)==.2f);
 assert(scale(true,true,true,false,0,.88)==.1f);
 Actor a,b;btCollisionObject x{{{0,0,0}}},y{{{0,30,0}}};float f;btVector3 normal;
 // Real narrow-phase sweep from actorconvexcallback.cpp: a lateral corridor
 // which is blocked at the former 0.75 spacing is open at the requested 0.2.
 assert(compactActorSweep(&a,&b,&x,&y,{0,-60,0},.2,f,normal));
 assert(f>.35f && f<.36f); // contact after (30 - 44*.2)/60
 y.t.p={20,30,0};
 assert(!compactActorSweep(&a,&b,&x,&y,{0,-60,0},.2,f,normal));
 assert(compactActorSweep(&a,&b,&x,&y,{0,-60,0},.75,f,normal));
 y.t.p={0,30,200};assert(!compactActorSweep(&a,&b,&x,&y,{0,-60,0},.2,f,normal));
 y.t.p={0,30,0};assert(!compactActorSweep(&a,&b,&x,&y,{0,0,0},.2,f,normal));
 assert(!compactActorSweep(nullptr,&b,&x,&y,{0,-60,0},.2,f,normal));
}
