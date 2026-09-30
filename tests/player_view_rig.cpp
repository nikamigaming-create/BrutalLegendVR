#include "../src/camera/player_view_rig.h"
#include "../src/input/control_bindings.h"
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

namespace BLVR { void Log(const char*,...) {} }
static void Check(bool okay,const char* what) { if(!okay){std::fprintf(stderr,"FAIL: %s\n",what);std::exit(1);} }
template<class T> void Put(void* p,unsigned off,T v) {std::memcpy(static_cast<uint8_t*>(p)+off,&v,sizeof(v));}
int main() {
    const uintptr_t base=reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    uint8_t actor[0x40]{},mesh[0xb0]{},skeleton[0x28]{},animation[0x54]{},pose[0x2c]{},resource[0x50]{},scene[0x1160]{},snapshot[0x138]{},header[8]{};
    const char names[]="Root\0Neck1\0Head\0Lf_Eye\0Rt_Eye\0Hair\0Lf_Shoulder\0Lf_Wrist\0Rt_Shoulder\0Rt_Wrist\0";
    uint32_t identifiers[20]{};unsigned offset=0;
    for(int i=0;i<10;++i){identifiers[i*2]=offset;offset+=static_cast<unsigned>(std::strlen(names+offset))+1;}
    int16_t parents[]={-1,0,1,2,2,2,0,6,0,8};
    float reference[10][12]{},localPose[10][12]{},skin[10][12]{},original[10][12];
    for(int i=0;i<10;++i){reference[i][7]=1;reference[i][8]=reference[i][9]=reference[i][10]=1;skin[i][0]=skin[i][5]=skin[i][10]=1;}
    reference[1][1]=1.5f;reference[2][1]=.3f;reference[3][0]=-.032f;reference[4][0]=.032f;
    reference[3][1]=reference[4][1]=.1f;reference[3][2]=reference[4][2]=.12f;
    for(int i=0;i<10;++i){skin[i][3]=.2f;skin[i][7]=.04f;skin[i][11]=-.1f;}
    std::memcpy(original,skin,sizeof(skin));
    std::memcpy(localPose,reference,sizeof(localPose));
    localPose[0][0]=.2f;localPose[0][1]=.04f;localPose[0][2]=-.1f;
    Put(actor,0x24,skeleton);Put(actor,0x38,mesh);Put(mesh,0,base+0xa9796c);Put(mesh,0x10,actor);
    Put(skeleton,0,base+0xaf8b00);Put(skeleton,0x10,actor);Put(skeleton,0x24,animation);Put(animation,4,resource);
    Put(animation,0x50,pose);Put(pose,0x28,localPose);
    Put(resource,8,names);Put(resource,0x14,10u);Put(resource,0x18,identifiers);Put(resource,0x1c,parents);Put(resource,0x48,reference);
    uint8_t* entries[]={snapshot};Put(scene,0x1100,64u);Put(scene,0x1108,entries);
    Put(snapshot,0,base+0xab18b4);Put(snapshot,0xe0,mesh);Put(snapshot,0x134,header);Put(header,0,skin);Put(header,4,uint16_t(10));
    float world[16]={0,0,1,0, 0,1,0,0, -1,0,0,0, 10,2,-3,1};Put(snapshot,0x70,world[0]);std::memcpy(snapshot+0x70,world,sizeof(world));
    std::memcpy(mesh+0x70,world,sizeof(world));
    float eye[3],movingEye[3];Check(BLVR::PlayerViewRig_Begin(scene,actor,eye),"bind exact player snapshot");
    Check(std::fabs(eye[0]-10.f)<.0001f && std::fabs(eye[1]-3.72f)<.0001f && std::fabs(eye[2]+3.f)<.0001f,"metric eye midpoint keeps feet grounded and excludes animation bob");
    Check(std::memcmp(skin[6],original[6],24*sizeof(float))==0,"wrist and weapon remain unchanged");
    Check(skin[2][0]==0 && skin[5][0]==0,"head and hair collapse");
    Check(std::memcmp(skin[1],original[1],12*sizeof(float))==0,"neck remains unchanged");
    BLVR::PlayerViewRig_End();Check(std::memcmp(skin,original,sizeof(skin))==0,"exact restoration for later passes");
    float collectionEye[3]{};
    Check(BLVR::PlayerViewRig_ReadLiveEyeAnchor(actor,false,collectionEye),"read collection eye without touching render skin");
    for(int k=0;k<3;++k)Check(std::fabs(collectionEye[k]-eye[k])<.0001f,"collection and stereo share on-foot anchor");
    Check(BLVR::PlayerViewRig_Begin(scene,actor,movingEye,true),"mounted uses seated eyes");
    Check(std::fabs(movingEye[0]-9.98f)<.0001f && std::fabs(movingEye[1]-3.94f)<.0001f &&
        std::fabs(movingEye[2]+2.8f)<.0001f,"mounted position includes seated head offset");
    BLVR::PlayerViewRig_End();
    Check(BLVR::PlayerViewRig_ReadLiveEyeAnchor(actor,true,collectionEye),"read live seated eye hierarchy");
    for(int k=0;k<3;++k)Check(std::fabs(collectionEye[k]-movingEye[k])<.0001f,"collection and stereo share mounted anchor");
    localPose[0][1]-=.75f;
    Check(BLVR::PlayerViewRig_ReadLiveEyeAnchor(actor,true,collectionEye)&&
        std::fabs(collectionEye[1]-(movingEye[1]-.75f))<.0001f,"collection follows a low scripted seat instead of standing height");
    localPose[0][1]+=.75f;
    const float canaryEye[3]={101,102,103};
    std::memcpy(collectionEye,canaryEye,sizeof(collectionEye));parents[2]=2;
    Check(!BLVR::PlayerViewRig_ReadLiveEyeAnchor(actor,true,collectionEye),"reject cyclic live eye hierarchy");parents[2]=1;
    Check(std::memcmp(collectionEye,canaryEye,sizeof(collectionEye))==0,"failed collection anchor preserves caller output");
    localPose[2][0]=NAN;
    Check(!BLVR::PlayerViewRig_ReadLiveEyeAnchor(actor,true,collectionEye),"reject nonfinite seated animation");localPose[2][0]=0;
    Put(mesh,0x10,skeleton);
    Check(!BLVR::PlayerViewRig_ReadLiveEyeAnchor(actor,false,collectionEye),"collection rejects unrelated mesh owner");Put(mesh,0x10,actor);
    Check(std::memcmp(skin,original,sizeof(skin))==0,"collection leaves native skin unchanged");
    for(int i=0;i<10;++i){skin[i][3]+=2;skin[i][7]-=1;skin[i][11]+=3;}
    Check(BLVR::PlayerViewRig_Begin(scene,actor,movingEye),"combat pose remains valid");
    Check(std::memcmp(eye,movingEye,sizeof(eye))==0,"combat head excursion cannot yank camera pivot");
    BLVR::PlayerViewRig_End();std::memcpy(skin,original,sizeof(skin));
    // Roll the body's local right/up about its forward axis. View height and
    // horizontal eye position must remain level while its root is unchanged.
    world[1]=.5f;world[2]=.8660254f;world[5]=.8660254f;world[6]=-.5f;
    std::memcpy(snapshot+0x70,world,sizeof(world));
    Check(BLVR::PlayerViewRig_Begin(scene,actor,movingEye),"rolled body pose remains valid");
    for(int k=0;k<3;++k)Check(std::fabs(eye[k]-movingEye[k])<.0001f,"body roll cannot tilt pivot");
    BLVR::PlayerViewRig_End();
    float savedWorld[16];std::memcpy(savedWorld,world,sizeof(world));
    world[0]=1;world[1]=0;world[2]=0;world[4]=0;world[5]=1;world[6]=0;world[8]=0;world[9]=0;world[10]=1;
    std::memcpy(snapshot+0x70,world,sizeof(world));
    Check(BLVR::PlayerViewRig_Begin(scene,actor,movingEye),"turned actor remains valid");
    for(int k=0;k<3;++k)Check(std::fabs(eye[k]-movingEye[k])<.0001f,"actor yaw cannot slide floor");
    BLVR::PlayerViewRig_End();std::memcpy(world,savedWorld,sizeof(world));
    // A tracked pose is expressed relative to the exact HMD view, then
    // converted into the existing native render packet's local space.
    std::memcpy(snapshot+0x70,world,sizeof(world));
    Check(BLVR::PlayerViewRig_Begin(scene,actor,movingEye),"tracked rig begin");
    blvr_xr_bridge::RigFrame rig{};rig.boneCount=10;rig.skeletonSignature=14695981039346656037ull;
    rig.controlsSignature=BLVR::ControlsSignature(BLVR::ActiveBindings());
    for(int i=0;i<10;++i) {
        rig.skeletonSignature=blvr_xr_bridge::RigNameHash(rig.skeletonSignature,names+identifiers[i*2]);
        rig.skinToHead[i][0]=rig.skinToHead[i][5]=rig.skinToHead[i][10]=rig.skinToHead[i][15]=1;
        rig.skinToHead[i][12]=.25f;rig.skinToHead[i][13]=-.4f;rig.skinToHead[i][14]=-.5f;
    }
    float headWorld[16]={1,0,0,0,0,1,0,0,0,0,1,0,10,4,-3,1};
    Check(BLVR::PlayerViewRig_ApplyTracked(rig,headWorld),"apply matching tracked rig");
    float wristLocal[3]={skin[6][3],skin[6][7],skin[6][11]};
    float wristWorld[3];
    for(int k=0;k<3;++k)wristWorld[k]=wristLocal[0]*world[k]+wristLocal[1]*world[4+k]+wristLocal[2]*world[8+k]+world[12+k];
    Check(std::fabs(wristWorld[0]-10.25f)<.0001f&&std::fabs(wristWorld[1]-3.6f)<.0001f&&std::fabs(wristWorld[2]+3.5f)<.0001f,"tracked hand stays at HMD-relative target through actor roll");
    Check(skin[2][0]==0&&skin[5][0]==0,"tracked rig still hides head and hair");
    auto bad=rig;bad.skeletonSignature++;
    Check(!BLVR::PlayerViewRig_ApplyTracked(bad,headWorld),"reject incompatible skeleton");
    BLVR::PlayerViewRig_End();Check(std::memcmp(skin,original,sizeof(skin))==0,"tracked skin exact restoration");
    const float neutralRightX=skin[9][3];
    skin[9][3]+=.8f;
    Check(BLVR::PlayerViewRig_Begin(scene,actor,movingEye),"live action reads current native snapshot");
    rig.liveAction=1;rig.liveActionWeight=1;
    Check(BLVR::PlayerViewRig_ApplyTracked(rig,headWorld),"retarget live axe pose without action files");
    const float firstLive[3]={skin[9][3],skin[9][7],skin[9][11]};
    BLVR::PlayerViewRig_End();
    Check(std::fabs(skin[9][3]-(neutralRightX+.8f))<.0001f,"live action exactly restores original native pose");
    skin[9][3]=neutralRightX+.3f;
    Check(BLVR::PlayerViewRig_Begin(scene,actor,movingEye),"next native action frame");
    const bool appliedLive=BLVR::PlayerViewRig_ApplyTracked(rig,headWorld);
    const float liveChange=std::fabs(skin[9][3]-firstLive[0])+std::fabs(skin[9][7]-firstLive[1])+std::fabs(skin[9][11]-firstLive[2]);
    Check(appliedLive&&liveChange>.1f,"first-person arm follows a changed native pose in the same render frame");
    auto stale=rig;stale.controlsSignature++;
    Check(!BLVR::PlayerViewRig_ApplyTracked(stale,headWorld),"reject old control layout during live reload");
    BLVR::PlayerViewRig_End();skin[9][3]=neutralRightX;rig.liveAction=0;rig.liveActionWeight=0;
    Check(BLVR::PlayerViewRig_Begin(scene,actor,movingEye,true),"mounted eye begin");
    Check(BLVR::PlayerViewRig_ApplyTracked(rig,headWorld,true),"mounted steering pose retained");
    Check(std::memcmp(skin[0],original[0],24*sizeof(float))==0,"seated body and neck keep native animation");
    Check(std::memcmp(skin[7],original[7],12*sizeof(float))!=0,"driving left wrist remains freely tracked for waving");
    Check(std::memcmp(skin[8],original[8],24*sizeof(float))==0,"driving right shoulder and wrist retain native wheel contact");
    BLVR::PlayerViewRig_End();Check(std::memcmp(skin,original,sizeof(skin))==0,"mounted pose restoration");
    Check(BLVR::PlayerViewRig_Begin(scene,actor,movingEye,true),"mounted free right hand begin");
    Check(BLVR::PlayerViewRig_ApplyTracked(rig,headWorld,true,false),"release wheel frees right hand");
    Check(std::memcmp(skin[9],original[9],12*sizeof(float))!=0,"right wrist tracks after release");
    Check(std::memcmp(skin[0],original[0],24*sizeof(float))==0,"free right hand keeps seated body");
    BLVR::PlayerViewRig_End();
    // A lower mesh palette is a prefix of the same named skeleton. The final
    // bone is a canary: neither applying nor restoring may touch its storage.
    Put(header,4,uint16_t(7));
    Check(BLVR::PlayerViewRig_Begin(scene,actor,movingEye),"accept native prefix palette");
    Check(BLVR::PlayerViewRig_ApplyTracked(rig,headWorld),"track hands with shorter native palette");
    Check(std::memcmp(skin[7],original[7],12*sizeof(float))==0,"short palette has no out-of-bounds write");
    BLVR::PlayerViewRig_End();Check(std::memcmp(skin,original,sizeof(skin))==0,"short palette exact restoration");
    Put(header,4,uint16_t(4));
    Check(!BLVR::PlayerViewRig_Begin(scene,actor,eye),"reject palette without both eye joints");
    Put(header,4,uint16_t(11));
    Check(!BLVR::PlayerViewRig_Begin(scene,actor,eye),"reject palette longer than skeleton");
    Put(header,4,uint16_t(10));
    Put(snapshot,0xe0,actor);Check(!BLVR::PlayerViewRig_Begin(scene,actor,eye),"reject unrelated owner");
    Check(std::memcmp(skin,original,sizeof(skin))==0,"NPC skin untouched");
    std::puts("PASS: collection/stereo standing and seated anchors, malformed live pose rejection, anatomical pivot, head hiding, tracked wrists and restoration");
}
