#include "../src/camera/tracked_basis.h"
#include "../src/camera/headset_visibility.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <cstring>
static void Check(bool value,const char* name) { if(!value){printf("FAIL: %s\n",name);std::exit(1);} }
static bool Near(float a,float b) {return std::fabs(a-b)<0.0001f;}
static void CheckCullDepth() {
    const float identity[4]={0,0,0,1},headP[3]={0,0,0};
    float eyeQ[2][4]={{0,0,0,1},{0,0,0,1}},eyeP[2][3]={{-.032f,0,0},{.032f,0,0}};
    const float fov[2][4]={{-1.02f,.81f,.90f,-.96f},{-.81f,1.02f,.90f,-.96f}};
    const float nativeNear=.1f,nativeFar=80.f;
    const float source[16]={1,0,0,0, 0,1,0,0, 0,0,-nativeFar/(nativeFar-nativeNear),-1,
        0,0,-nativeFar*nativeNear/(nativeFar-nativeNear),0};
    float cull[16];
    Check(BLVR::HeadsetCullProjection(source,identity,headP,eyeQ,eyeP,fov,cull),"finite binocular cull projection");
    const float setback=BLVR::HeadsetCullSetbackMeters;
    const float originalClipZ=-source[10]*(nativeFar+setback)+source[14];
    Check(originalClipZ>nativeFar+setback,"negative fixture: unchanged far plane clips eye-visible geometry after setback");
    auto checkCorners=[&](const float headQ[4]) {
        Check(BLVR::HeadsetCullProjection(source,headQ,headP,eyeQ,eyeP,fov,cull),"canted binocular collection projection");
        float hr[3],hu[3],hf[3];BLVR::TrackedEyeBasis(headQ,0,-1,hr,hu,hf);
        for(float distance:{nativeNear,1.f,20.f,nativeFar})for(int eye=0;eye<2;++eye) {
            float er[3],eu[3],ef[3];BLVR::TrackedEyeBasis(eyeQ[eye],0,-1,er,eu,ef);
            for(int x=0;x<2;++x)for(int y=2;y<4;++y) {
                float screenX=0,screenY=0,depth=0;
                for(int k=0;k<3;++k) {
                    const float point=eyeP[eye][k]+distance*(er[k]*std::tan(fov[eye][x])+
                        eu[k]*std::tan(fov[eye][y])+ef[k])+setback*hf[k];
                    screenX+=point*hr[k];screenY+=point*hu[k];depth+=point*hf[k];
                }
                const float clipZ=-depth*cull[10]+cull[14];
                Check(std::fabs(screenX*cull[0])<depth&&std::fabs(screenY*cull[5])<depth,
                    "eye corners fit collection side planes");
                Check(clipZ>=-1e-5f&&clipZ<=depth+1e-5f,"eye corners fit collection near and far planes");
            }
        }
    };
    checkCorners(identity);
    eyeQ[0][1]=std::sin(.06f);eyeQ[0][3]=std::cos(.06f);
    eyeQ[1][1]=-eyeQ[0][1];eyeQ[1][3]=eyeQ[0][3];
    checkCorners(identity);
    // Rotate the entire headset through a pitched, rolled ride. Eye cant and
    // IPD remain relative to that head, rather than to the level world plane.
    const float head[4]={.25f,-.3f,.1f,.91515026f};
    float hr[3],hu[3],hf[3];BLVR::TrackedEyeBasis(head,0,-1,hr,hu,hf);
    for(int eye=0;eye<2;++eye) {
        const float angle=eye?-.06f:.06f,c=std::cos(angle),s=std::sin(angle);
        eyeQ[eye][0]=head[0]*c-head[2]*s;eyeQ[eye][1]=head[1]*c+head[3]*s;
        eyeQ[eye][2]=head[2]*c+head[0]*s;eyeQ[eye][3]=head[3]*c-head[1]*s;
        for(int k=0;k<3;++k)eyeP[eye][k]=(eye?.032f:-.032f)*hr[k];
    }
    checkCorners(head);
    float infinite[16];std::memcpy(infinite,source,sizeof(infinite));infinite[10]=-1;infinite[14]=-.1f;
    Check(BLVR::HeadsetCullProjection(infinite,head,headP,eyeQ,eyeP,fov,cull)&&cull[10]==-1,
        "infinite far plane remains infinite");
    float sentinel[16];for(float& value:sentinel)value=23.f;
    float malformed[16];std::memcpy(malformed,source,sizeof(malformed));malformed[5]=std::numeric_limits<float>::quiet_NaN();
    Check(!BLVR::HeadsetCullProjection(malformed,head,headP,eyeQ,eyeP,fov,sentinel),"nonfinite native projection rejected");
    for(float value:sentinel)Check(value==23.f,"rejected cull update does not partially overwrite matrices");
    eyeP[1][0]=std::numeric_limits<float>::quiet_NaN();
    Check(!BLVR::HeadsetCullProjection(source,head,headP,eyeQ,eyeP,fov,cull),"nonfinite right-eye position rejected");
    eyeP[1][0]=.032f;
    float malformedFov[2][4];std::memcpy(malformedFov,fov,sizeof(fov));malformedFov[1][0]=malformedFov[1][1];
    Check(!BLVR::HeadsetCullProjection(source,head,headP,eyeQ,eyeP,malformedFov,cull),"collapsed right-eye FOV rejected");
}
int main() {
    CheckCullDepth();
    float r[3],u[3],f[3];
    const float identity[4]={0,0,0,1};
    Check(BLVR::TrackedEyeBasis(identity,1,0,r,u,f),"identity valid");
    Check(Near(f[0],1)&&Near(u[1],1)&&Near(r[2],1),"identity faces native forward");
    const float pitch[4]={0.5f,0,0,0.866025404f};
    BLVR::TrackedEyeBasis(pitch,1,0,r,u,f);
    Check(Near(f[0],0.5f)&&Near(f[1],0.866025404f),"60 degree pitch follows headset");
    const float yaw[4]={0,-0.70710678f,0,0.70710678f};
    BLVR::TrackedEyeBasis(yaw,1,0,r,u,f);
    Check(Near(f[2],1)&&Near(f[1],0),"right yaw turns toward native right");
    const float roll[4]={0,0,0.382683432f,0.923879533f};
    BLVR::TrackedEyeBasis(roll,1,0,r,u,f);
    Check(Near(r[1],0.70710678f)&&Near(u[2],-0.70710678f),"roll preserves OpenXR handedness");
    const float mixed[4]={0.35f,-0.45f,0.3f,0.6f};
    BLVR::TrackedEyeBasis(mixed,-0.6f,0.8f,r,u,f);
    float dotRU=0,dotRF=0,lenF=0;
    for(int i=0;i<3;++i){dotRU+=r[i]*u[i];dotRF+=r[i]*f[i];lenF+=f[i]*f[i];}
    Check(Near(dotRU,0)&&Near(dotRF,0)&&Near(lenF,1),"combined rotation remains orthonormal");
    const float projection[16]={1.1f,0,0,0, 0,1.3f,0,0, 0.17f,-0.08f,-1.001f,-1, 0,0,-0.1f,0};
    float inverse[16]; Check(BLVR::InvertCameraMatrix(projection,inverse),"asymmetric eye inverse");
    for(int a=0;a<4;++a)for(int b=0;b<4;++b){float value=0;for(int k=0;k<4;++k)value+=projection[a*4+k]*inverse[k*4+b];Check(Near(value,a==b?1.0f:0.0f),"eye projection round trip");}
    float eyeQ[2][4]={{0,0,0,1},{0,0,0,1}},eyeP[2][3]={{-.036f,0,0},{.036f,0,0}};
    const float headP[3]={0,0,0};float fovs[2][4]={{-1.02f,.81f,.90f,-.96f},{-.81f,1.02f,.90f,-.96f}};
    float tx=0,ty=0;
    Check(BLVR::HeadsetCullTangents(identity,headP,eyeQ,eyeP,fovs,tx,ty),"binocular visibility bounds");
    Check(tx>std::tan(1.02f)&&ty>std::tan(.96f),"both asymmetric eyes and angular margin fit");
    for(float d:{.1f,1.f,100.f,4096.f})for(int e=0;e<2;++e)for(int x=0;x<2;++x)for(int y=2;y<4;++y) {
        Check(std::fabs(eyeP[e][0]+d*std::tan(fovs[e][x]))<tx*(d+3.f),"near and far eye corners fit horizontally");
        Check(std::fabs(d*std::tan(fovs[e][y]))<ty*(d+3.f),"near and far eye corners fit vertically");
    }
    eyeQ[0][1]=std::sin(.06f);eyeQ[0][3]=std::cos(.06f);
    eyeQ[1][1]=-eyeQ[0][1];eyeQ[1][3]=eyeQ[0][3];
    float cantedX=0,cantedY=0;
    Check(BLVR::HeadsetCullTangents(identity,headP,eyeQ,eyeP,fovs,cantedX,cantedY)&&cantedX>tx,"canted eye orientation widens visibility");
    puts("PASS: headset pitch/yaw/roll, binocular cull depth and cant, malformed-pose rejection, asymmetric projection inverse");
}
