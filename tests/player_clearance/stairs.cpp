// Real Bullet sweeps with the production Stepper implementation injected by the runner.
#include <BulletCollision/CollisionDispatch/btCollisionWorld.h>
#include <BulletCollision/CollisionDispatch/btDefaultCollisionConfiguration.h>
#include <BulletCollision/BroadphaseCollision/btDbvtBroadphase.h>
#include <BulletCollision/CollisionShapes/btBoxShape.h>
#include <BulletCollision/CollisionShapes/btCylinderShape.h>
#include <BulletCollision/CollisionShapes/btBvhTriangleMeshShape.h>
#include <BulletCollision/CollisionShapes/btTriangleMesh.h>
#include <osg/Math>
#include <osg/Vec3f>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <memory>
#include <vector>
#include <apps/openmw/mwphysics/playerbodyshape.hpp>
#include <apps/openmw/mwphysics/actorbodyrules.hpp>
#include <apps/openmw/mwphysics/stepper.hpp>
#include <apps/openmw/mwphysics/constants.hpp>
#include <apps/openmw/mwphysics/collisiontype.hpp>
namespace MWPhysics {
static osg::Vec3f reject(const osg::Vec3f& d,const osg::Vec3f& n){return d-n*(d*n);}
template<class V>static bool isWalkableSlope(const V& n){return n.z()>std::cos(osg::DegreesToRadians(47.f));}
void ActorTracer::doTrace(const btCollisionObject* actor,const osg::Vec3f& start,const osg::Vec3f& end,const btCollisionWorld* world){
 const btVector3 a(start.x(),start.y(),start.z()),b(end.x(),end.y(),end.z());
 struct Callback:btCollisionWorld::ClosestConvexResultCallback{
  const btCollisionObject* me;btVector3 motion;
  Callback(const btCollisionObject* obj,const btVector3& from,const btVector3& to):ClosestConvexResultCallback(from,to),me(obj),motion(from-to){}
  btScalar addSingleResult(btCollisionWorld::LocalConvexResult& r,bool worldNormal)override{
   if(r.m_hitCollisionObject==me)return 1;
   auto n=worldNormal?r.m_hitNormalLocal:r.m_hitCollisionObject->getWorldTransform().getBasis()*r.m_hitNormalLocal;
   if(!ActorBodyRules::blocksMotion(motion.dot(n),motion.length()))return 1;
   return ClosestConvexResultCallback::addSingleResult(r,worldNormal);
  }
 } cb(actor,a,b);
 cb.m_collisionFilterGroup=actor->getBroadphaseHandle()->m_collisionFilterGroup;
 cb.m_collisionFilterMask=actor->getBroadphaseHandle()->m_collisionFilterMask;
 btTransform from(actor->getWorldTransform()),to(from);from.setOrigin(a);to.setOrigin(b);
 world->convexSweepTest(static_cast<const btConvexShape*>(actor->getCollisionShape()),from,to,cb);
 mFraction=cb.hasHit()?cb.m_closestHitFraction:1;
 mEndPos=start+(end-start)*mFraction;
 mPlaneNormal=cb.hasHit()?osg::Vec3f(cb.m_hitNormalWorld.x(),cb.m_hitNormalWorld.y(),cb.m_hitNormalWorld.z()):osg::Vec3f(0,0,1);
 mHitPoint=cb.hasHit()?osg::Vec3f(cb.m_hitPointWorld.x(),cb.m_hitPointWorld.y(),cb.m_hitPointWorld.z()):end;
 mHitObject=cb.hasHit()?cb.m_hitCollisionObject:nullptr;
}
// INSERT_STEPPER
}
using namespace MWPhysics;
struct Scene{
 btDefaultCollisionConfiguration config;btCollisionDispatcher dispatcher{&config};btDbvtBroadphase broadphase;
 btCollisionWorld world{&dispatcher,&broadphase,&config};
 std::vector<std::unique_ptr<btTriangleMesh>> meshes;
 std::vector<std::unique_ptr<btCollisionShape>> shapes;std::vector<std::unique_ptr<btCollisionObject>> objects;
 void box(const btVector3& half,const btVector3& origin){
  shapes.push_back(std::make_unique<btBoxShape>(half));objects.push_back(std::make_unique<btCollisionObject>());
  auto& o=*objects.back();o.setCollisionShape(shapes.back().get());btTransform t;t.setIdentity();t.setOrigin(origin);o.setWorldTransform(t);world.addCollisionObject(&o,CollisionType_World,CollisionType_Actor);
 }
 void meshBox(const btVector3& half,const btVector3& origin){
  meshes.push_back(std::make_unique<btTriangleMesh>());auto& mesh=*meshes.back();
  const btScalar x=half.x(),y=half.y(),z=half.z();
  btVector3 v[]={{-x,-y,-z},{x,-y,-z},{x,y,-z},{-x,y,-z},{-x,-y,z},{x,-y,z},{x,y,z},{-x,y,z}};
  const int faces[][3]={{0,2,1},{0,3,2},{4,5,6},{4,6,7},{0,1,5},{0,5,4},{1,2,6},{1,6,5},{2,3,7},{2,7,6},{3,0,4},{3,4,7}};
  for(const auto& f:faces)mesh.addTriangle(v[f[0]],v[f[1]],v[f[2]]);
  shapes.push_back(std::make_unique<btBvhTriangleMeshShape>(&mesh,true));objects.push_back(std::make_unique<btCollisionObject>());
  auto& o=*objects.back();o.setCollisionShape(shapes.back().get());btTransform t;t.setIdentity();t.setOrigin(origin);o.setWorldTransform(t);world.addCollisionObject(&o,CollisionType_World,CollisionType_Actor);
 }
 ~Scene(){for(auto& p:objects)world.removeCollisionObject(p.get());}
};
void dimensions(){
 float x=22,y=22,z=64,c=64;ActorBodyRules::reducePlayerWidth(true,x);ActorBodyRules::reducePlayerHeight(true,z,c);
 assert(std::abs(x-18.7f)<.001f&&y==22&&std::abs(z-54.4f)<.001f&&std::abs(c-z)<.001f);
 auto shape=makePlayerBodyShape(btVector3(x,y,z));btTransform t;t.setIdentity();btVector3 a,b;shape->getAabb(t,a,b);
 assert(std::abs(b.x()-x)<.1&&std::abs(b.y()-y)<.1&&std::abs(b.z()-z)<.1);
 // Yaw rotates the narrow shoulder axis with the player, preserving depth.
 t.setRotation(btQuaternion(btVector3(0,0,1),SIMD_HALF_PI));shape->getAabb(t,a,b);assert(std::abs(b.x()-y)<.1&&std::abs(b.y()-x)<.1);
 for(float scale:{.8f,1.f,1.3f}){shape->setLocalScaling(btVector3(scale,scale,scale));t.setIdentity();shape->getAabb(t,a,b);assert(std::abs(b.z()-z*scale)<.15);}
 std::cout<<"PASS dimensions, width/depth axes, flat feet, scaling\n";
}
void stairs(float yaw,int direction,float scale,bool mesh){
 Scene scene;scene.box(btVector3(500,600,10),btVector3(0,100,-10));
 const int steps=8;const float stepDepth=36,stepHeight=12;
 for(int i=0;i<steps;++i){auto half=btVector3(120,stepDepth/2,stepHeight*(i+1)/2);auto center=btVector3(0,(i+.5f)*stepDepth,stepHeight*(i+1)/2);if(mesh)scene.meshBox(half,center);else scene.box(half,center);}
 const float halfZ=64*.85f*scale;auto shape=makePlayerBodyShape(btVector3(22*.85f*scale,22*scale,halfZ));
 btCollisionObject actor;actor.setCollisionShape(shape.get());btTransform tr;tr.setIdentity();tr.setRotation(btQuaternion(btVector3(0,0,1),yaw));
 osg::Vec3f p(0,direction>0?-50.f:steps*stepDepth-18.f,halfZ+1+(direction>0?0:steps*stepHeight));tr.setOrigin(btVector3(p.x(),p.y(),p.z()));actor.setWorldTransform(tr);
 scene.world.addCollisionObject(&actor,CollisionType_Actor,CollisionType_World);
 Stepper stepper(&scene.world,&actor);ActorTracer trace;bool hitTop=false;int moved=0;
 for(int frame=0;frame<250;++frame){
  osg::Vec3f velocity(0,120.f*direction,0);float remaining=1.f/60;
  auto next=p+velocity*remaining;trace.doTrace(&actor,p,next,&scene.world);
  if(trace.mFraction>=1)p=next;
  else if(trace.mHitPoint.z()-trace.mEndPos.z()+halfZ<sStepSizeUp){
   if(stepper.step(p,velocity,remaining,true,true))++moved;

  }
  trace.doTrace(&actor,p,p-osg::Vec3f(0,0,sStepSizeDown+2*sGroundOffset),&scene.world);
  if(trace.mFraction<1 && trace.mPlaneNormal.z()>.65f)p.z()=trace.mEndPos.z()+sGroundOffset;
  auto t=actor.getWorldTransform();t.setOrigin(btVector3(p.x(),p.y(),p.z()));actor.setWorldTransform(t);scene.world.updateSingleAabb(&actor);
  if(p.z()-halfZ>90)hitTop=true;
  if(direction>0 && p.y()>steps*stepDepth-18.f)break;
  if(direction<0 && p.y()<-45)break;
 }
 const bool passed=direction>0?p.y()>steps*stepDepth-18.f&&hitTop:p.y()<-45&&std::abs(p.z()-halfZ-1)<1;
 if(!passed)std::cerr<<"FAIL stairs yaw="<<yaw<<" direction="<<direction<<" scale="<<scale<<" mesh="<<mesh<<" pos="<<p.y()<<","<<p.z()<<" stepped="<<moved<<"\n";
 assert(passed);
 scene.world.removeCollisionObject(&actor);
}
void barriers(){
 Scene scene;scene.box(btVector3(100,100,2),btVector3(0,0,123));scene.box(btVector3(100,2,100),btVector3(0,100,80));
 auto shape=makePlayerBodyShape(btVector3(18.7,22,54.4));btCollisionObject actor;actor.setCollisionShape(shape.get());btTransform t;t.setIdentity();actor.setWorldTransform(t);scene.world.addCollisionObject(&actor,CollisionType_Actor,CollisionType_World);
 ActorTracer trace;trace.doTrace(&actor,osg::Vec3f(0,0,55.4),osg::Vec3f(0,0,89.4),&scene.world);assert(trace.mFraction<1&&trace.mPlaneNormal.z()<-.99f);
 trace.doTrace(&actor,osg::Vec3f(0,0,55.4),osg::Vec3f(0,120,55.4),&scene.world);assert(trace.mFraction<1&&trace.mPlaneNormal.y()<-.99f);
 // The tolerance only removes grazing contacts; real slope/floor/ceiling normals stay blocking.
 assert(ActorBodyRules::blocksMotion(1.f,1.f));assert(!ActorBodyRules::blocksMotion(.00005f,1.f));assert(!ActorBodyRules::blocksMotion(-1.f,1.f));
 scene.world.removeCollisionObject(&actor);std::cout<<"PASS real wall and low-ceiling collisions retained\n";
}
void passage(){
 Scene scene;scene.box(btVector3(2,100,100),btVector3(-23,100,80));scene.box(btVector3(2,100,100),btVector3(23,100,80));
 auto shape=makePlayerBodyShape(btVector3(18.7,22,54.4));btCollisionObject actor;actor.setCollisionShape(shape.get());btTransform t;t.setIdentity();actor.setWorldTransform(t);scene.world.addCollisionObject(&actor,CollisionType_Actor,CollisionType_World);
 ActorTracer trace;trace.doTrace(&actor,osg::Vec3f(0,-100,55.4),osg::Vec3f(0,100,55.4),&scene.world);assert(trace.mFraction==1);
 t.setRotation(btQuaternion(btVector3(0,0,1),SIMD_HALF_PI));actor.setWorldTransform(t);scene.world.updateSingleAabb(&actor);trace.doTrace(&actor,osg::Vec3f(0,-100,55.4),osg::Vec3f(0,100,55.4),&scene.world);assert(trace.mFraction<1);
 scene.world.removeCollisionObject(&actor);std::cout<<"PASS narrow shoulder passage, chest-depth collision retained\n";
}
int main(){dimensions();passage();barriers();for(float scale:{.8f,1.f,1.3f})for(float yaw:{0.f,.78539816f,1.5707963f})for(int d:{1,-1})for(bool mesh:{false,true})stairs(yaw,d,scale,mesh);std::cout<<"PASS 36 real Bullet box/triangle-mesh staircase ascent/descent scenarios\n";}
