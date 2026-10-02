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
    constexpr unsigned BoneCount=13;
    uint8_t actor[0x68]{},mesh[0xb0]{},skeleton[0x28]{},animation[0x54]{},pose[0x2c]{},resource[0x50]{},scene[0x1160]{},snapshot[0x138]{},header[8]{};
    const char names[]="Root\0Neck1\0Head\0Lf_Eye\0Rt_Eye\0Hair\0Lf_Shoulder\0Lf_Wrist\0Rt_Shoulder\0Rt_Wrist\0Wings\0Lf_Patch_Spine2\0Lf_Wing_Hand\0";
    uint32_t identifiers[BoneCount*2]{};unsigned offset=0;
    for(unsigned i=0;i<BoneCount;++i){identifiers[i*2]=offset;offset+=static_cast<unsigned>(std::strlen(names+offset))+1;}
    int16_t parents[]={-1,0,1,2,2,2,0,6,0,8,0,10,11};
    float reference[BoneCount][12]{},localPose[BoneCount][12]{},skin[BoneCount][12]{},original[BoneCount][12];
    for(unsigned i=0;i<BoneCount;++i){reference[i][7]=1;reference[i][8]=reference[i][9]=reference[i][10]=1;skin[i][0]=skin[i][5]=skin[i][10]=1;}
    reference[1][1]=1.5f;reference[2][1]=.3f;reference[3][0]=-.032f;reference[4][0]=.032f;
    reference[3][1]=reference[4][1]=.1f;reference[3][2]=reference[4][2]=.12f;
    reference[10][1]=1.3f;reference[10][2]=-.2f;reference[11][0]=.1f;reference[12][0]=.4f;
    for(unsigned i=0;i<BoneCount;++i){skin[i][3]=.2f;skin[i][7]=.04f;skin[i][11]=-.1f;}
    std::memcpy(original,skin,sizeof(skin));
    std::memcpy(localPose,reference,sizeof(localPose));
    localPose[0][0]=.2f;localPose[0][1]=.04f;localPose[0][2]=-.1f;
    Put(actor,0x24,skeleton);Put(actor,0x38,mesh);Put(mesh,0,base+0xa9796c);Put(mesh,0x10,actor);
    Put(skeleton,0,base+0xaf8b00);Put(skeleton,0x10,actor);Put(skeleton,0x24,animation);Put(animation,4,resource);
    Put(animation,0x50,pose);Put(pose,0x28,localPose);
    Put(resource,8,names);Put(resource,0x14,BoneCount);Put(resource,0x18,identifiers);Put(resource,0x1c,parents);Put(resource,0x48,reference);
    uint8_t* entries[]={snapshot};Put(scene,0x1100,64u);Put(scene,0x1108,entries);
    Put(snapshot,0,base+0xab18b4);Put(snapshot,0xe0,mesh);Put(snapshot,0x134,header);Put(header,0,skin);Put(header,4,uint16_t(BoneCount));
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
    for(unsigned i=0;i<BoneCount;++i){skin[i][3]+=2;skin[i][7]-=1;skin[i][11]+=3;}
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
    blvr_xr_bridge::RigFrame rig{};rig.boneCount=BoneCount;rig.skeletonSignature=14695981039346656037ull;
    rig.version=blvr_xr_bridge::RigVersion;rig.structBytes=sizeof(rig);rig.trackedHandMask=3;
    rig.controlsSignature=BLVR::ControlsSignature(BLVR::ActiveBindings());
    for(unsigned i=0;i<BoneCount;++i) {
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
    float appliedSkin[BoneCount][12];std::memcpy(appliedSkin,skin,sizeof(skin));
    bad=rig;bad.version=2;
    Check(!BLVR::PlayerViewRig_ApplyTracked(bad,headWorld),"reject old rig bridge version");
    bad=rig;bad.structBytes-=8;
    Check(!BLVR::PlayerViewRig_ApplyTracked(bad,headWorld),"reject truncated rig layout");
    bad=rig;bad.trackedHandMask=4;
    Check(!BLVR::PlayerViewRig_ApplyTracked(bad,headWorld),"reject unknown tracking mask");
    bad=rig;bad.skinToHead[7][0]=bad.skinToHead[7][5]=bad.skinToHead[7][10]=0;
    Check(!BLVR::PlayerViewRig_ApplyTracked(bad,headWorld),"reject singular tracked hand transform");
    float badHead[16];std::memcpy(badHead,headWorld,sizeof(badHead));badHead[0]=badHead[5]=badHead[10]=0;
    Check(!BLVR::PlayerViewRig_ApplyTracked(rig,badHead),"reject singular head transform");
    Check(std::memcmp(skin,appliedSkin,sizeof(skin))==0,"malformed tracked frames leave current rendered skin untouched");
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
    const auto jointWorld=[&](int joint,float out[3]) {
        for(int k=0;k<3;++k)out[k]=skin[joint][3]*world[k]+skin[joint][7]*world[4+k]+skin[joint][11]*world[8+k]+world[12+k];
    };
    skin[7][3]+=.6f;
    Check(BLVR::PlayerViewRig_Begin(scene,actor,movingEye),"lost left grip begins from current native pose");
    rig.trackedHandMask=2;rig.selectedWeapon=1;
    Check(BLVR::PlayerViewRig_ApplyTracked(rig,headWorld),"missing left grip keeps current native arm beneath tracked torso");
    float lostShoulder[3],lostWrist[3];jointWorld(6,lostShoulder);jointWorld(7,lostWrist);
    Check(std::fabs(lostWrist[0]-lostShoulder[0]-.6f)<.0001f&&std::fabs(lostWrist[1]-lostShoulder[1])<.0001f&&
        std::fabs(lostWrist[2]-lostShoulder[2])<.0001f,"fallback arm preserves native local shoulder-to-wrist pose");
    BLVR::PlayerViewRig_End();skin[7][3]+=.2f;
    Check(BLVR::PlayerViewRig_Begin(scene,actor,movingEye),"tracking-loss next render frame");
    Check(BLVR::PlayerViewRig_ApplyTracked(rig,headWorld),"tracking-loss fallback updates with native animation");
    float nextLostWrist[3];jointWorld(7,nextLostWrist);
    Check(std::fabs(nextLostWrist[0]-lostWrist[0]-.2f)<.0001f,"lost hand never freezes an earlier animation pose");
    BLVR::PlayerViewRig_End();
    Check(BLVR::PlayerViewRig_Begin(scene,actor,movingEye),"grip reacquisition frame");
    rig.trackedHandMask=3;
    Check(BLVR::PlayerViewRig_ApplyTracked(rig,headWorld),"reacquired grip resumes exact tracked pose");
    jointWorld(7,nextLostWrist);
    Check(std::fabs(nextLostWrist[0]-10.25f)<.0001f&&std::fabs(nextLostWrist[1]-3.6f)<.0001f&&
        std::fabs(nextLostWrist[2]+3.5f)<.0001f,"reacquired hand returns to source HMD-relative target");
    BLVR::PlayerViewRig_End();std::memcpy(skin,original,sizeof(skin));rig.selectedWeapon=0;
    // Wings, the unlabelled membrane patch, and its descendant must share
    // the same tracked spine while retaining this render's native flapping.
    const float wingMotion[3][3]={{.31f,.24f,.5f},{.42f,.18f,.6f},{.57f,.32f,.7f}};
    for(int joint=0;joint<13;++joint)if(joint==0||joint>=10) {
        skin[joint][5]=skin[joint][10]=0;skin[joint][6]=-1;skin[joint][9]=1;
    }
    // A pitched native parent rotates local wing motion into (x,-z,y).
    // The tracked parent must remove that pitch without losing the flap.
    for(int joint=10;joint<13;++joint) {
        skin[joint][3]+=wingMotion[joint-10][0];
        skin[joint][7]-=wingMotion[joint-10][2];
        skin[joint][11]+=wingMotion[joint-10][1];
    }
    float nativeWings[BoneCount][12];std::memcpy(nativeWings,skin,sizeof(skin));
    Check(BLVR::PlayerViewRig_Begin(scene,actor,movingEye),"native flight wing frame");
    Check(BLVR::PlayerViewRig_ApplyTracked(rig,headWorld),"reexpress entire native wing subtree beneath tracked root");
    float trackedRoot[3];jointWorld(0,trackedRoot);
    for(int joint=10;joint<13;++joint) {
        float rendered[3];jointWorld(joint,rendered);
        for(int k=0;k<3;++k)Check(std::fabs(rendered[k]-trackedRoot[k]-wingMotion[joint-10][k])<.0001f,
            "wing root, membrane patch and hand retain current native motion beneath one tracked parent");
    }
    BLVR::PlayerViewRig_End();Check(std::memcmp(skin,nativeWings,sizeof(skin))==0,"native wings exactly restore after stereo");
    skin[12][3]+=.15f;
    Check(BLVR::PlayerViewRig_Begin(scene,actor,movingEye)&&BLVR::PlayerViewRig_ApplyTracked(rig,headWorld),"next live wing flap frame");
    float nextWing[3];jointWorld(0,trackedRoot);jointWorld(12,nextWing);
    Check(std::fabs(nextWing[0]-trackedRoot[0]-wingMotion[2][0]-.15f)<.0001f,"wing motion follows this render instead of freezing the earlier flap");
    BLVR::PlayerViewRig_End();std::memcpy(skin,original,sizeof(skin));
    for(int joint=10;joint<13;++joint)skin[joint][0]=skin[joint][5]=skin[joint][10]=0;
    std::memcpy(nativeWings,skin,sizeof(skin));
    Check(BLVR::PlayerViewRig_Begin(scene,actor,movingEye)&&BLVR::PlayerViewRig_ApplyTracked(rig,headWorld),"collapsed stowed wings need no singular child inverse");
    for(int joint=10;joint<13;++joint)Check(skin[joint][0]==0&&skin[joint][5]==0&&skin[joint][10]==0,"stowed wing geometry remains collapsed");
    BLVR::PlayerViewRig_End();Check(std::memcmp(skin,nativeWings,sizeof(skin))==0,"collapsed wings restore exactly");
    std::memcpy(skin,original,sizeof(skin));skin[0][0]=skin[0][5]=skin[0][10]=0;
    Check(BLVR::PlayerViewRig_Begin(scene,actor,movingEye),"malformed native wing parent frame");
    std::memcpy(appliedSkin,skin,sizeof(skin));
    Check(!BLVR::PlayerViewRig_ApplyTracked(rig,headWorld),"reject singular native wing parent instead of publishing broken geometry");
    Check(std::memcmp(skin,appliedSkin,sizeof(skin))==0,"invalid wing parent leaves the render palette untouched");
    BLVR::PlayerViewRig_End();std::memcpy(skin,original,sizeof(skin));
    Check(BLVR::PlayerViewRig_Begin(scene,actor,movingEye,true),"mounted eye begin");
    Check(BLVR::PlayerViewRig_ApplyTracked(rig,headWorld,true),"mounted steering pose retained");
    Check(std::memcmp(skin[0],original[0],24*sizeof(float))==0,"seated body and neck keep native animation");
    Check(std::memcmp(skin[7],original[7],12*sizeof(float))!=0,"driving left wrist remains freely tracked for waving");
    Check(std::memcmp(skin[8],original[8],24*sizeof(float))==0,"driving right shoulder and wrist retain native wheel contact");
    Check(std::memcmp(skin[10],original[10],36*sizeof(float))==0,"mounted wings keep native body ownership");
    BLVR::PlayerViewRig_End();Check(std::memcmp(skin,original,sizeof(skin))==0,"mounted pose restoration");
    Check(BLVR::PlayerViewRig_Begin(scene,actor,movingEye,true),"mounted free right hand begin");
    Check(BLVR::PlayerViewRig_ApplyTracked(rig,headWorld,true,false),"release wheel frees right hand");
    Check(std::memcmp(skin[9],original[9],12*sizeof(float))!=0,"right wrist tracks after release");
    Check(std::memcmp(skin[0],original[0],24*sizeof(float))==0,"free right hand keeps seated body");
    BLVR::PlayerViewRig_End();
    Check(BLVR::PlayerViewRig_Begin(scene,actor,movingEye,true),"mounted lost right grip begin");
    rig.trackedHandMask=1;
    Check(BLVR::PlayerViewRig_ApplyTracked(rig,headWorld,true,false),"lost released wheel hand retains native animation");
    Check(std::memcmp(skin[8],original[8],24*sizeof(float))==0,"untracked mounted right arm never becomes a bind pose");
    BLVR::PlayerViewRig_End();rig.trackedHandMask=3;
    // A lower mesh palette is a prefix of the same named skeleton. The final
    // bone is a canary: neither applying nor restoring may touch its storage.
    Put(header,4,uint16_t(7));
    Check(BLVR::PlayerViewRig_Begin(scene,actor,movingEye),"accept native prefix palette");
    Check(BLVR::PlayerViewRig_ApplyTracked(rig,headWorld),"track hands with shorter native palette");
    Check(std::memcmp(skin[7],original[7],12*sizeof(float))==0,"short palette has no out-of-bounds write");
    BLVR::PlayerViewRig_End();Check(std::memcmp(skin,original,sizeof(skin))==0,"short palette exact restoration");
    Put(header,4,uint16_t(4));
    Check(!BLVR::PlayerViewRig_Begin(scene,actor,eye),"reject palette without both eye joints");
    Put(header,4,uint16_t(BoneCount+1));
    Check(!BLVR::PlayerViewRig_Begin(scene,actor,eye),"reject palette longer than skeleton");
    Put(header,4,uint16_t(BoneCount));
    HANDLE oldMapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,8,L"Local\\BLVR_TrackedEddie_v2");
    Check(oldMapping&&GetLastError()!=ERROR_ALREADY_EXISTS,"old-version fixture requires an isolated stopped game");
    blvr_xr_bridge::RigFrame received{};received.frameId=1234;
    Check(!BLVR::PlayerViewRig_ReadTracked(65,900,7,received)&&received.frameId==1234,"old mapping is ignored without changing caller output");
    HANDLE mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(blvr_xr_bridge::RigHistoryBuffer),blvr_xr_bridge::RigMappingName);
    Check(mapping&&GetLastError()!=ERROR_ALREADY_EXISTS,"rig reader fixture requires an isolated stopped game");
    auto* history=static_cast<blvr_xr_bridge::RigHistoryBuffer*>(MapViewOfFile(mapping,FILE_MAP_WRITE,0,0,0));
    Check(history!=nullptr,"map rig fixture");
    auto& publication=history->slots[65%blvr_xr_bridge::RigHistory];publication=rig;
    publication.sequence=2;publication.magic=blvr_xr_bridge::RigMagic;publication.producerEpoch=7;
    publication.frameId=65;publication.predictedDisplayTime=900;
    Check(BLVR::PlayerViewRig_ReadTracked(65,900,7,received)&&received.trackedHandMask==3,"read exact versioned rig transaction");
    const auto beforeMalformed=received;
    Check(!BLVR::PlayerViewRig_ReadTracked(66,900,7,received)&&!BLVR::PlayerViewRig_ReadTracked(65,901,7,received)&&
        !BLVR::PlayerViewRig_ReadTracked(65,900,8,received),"reject frame, display-time and epoch mismatch");
    publication.version=2;
    Check(!BLVR::PlayerViewRig_ReadTracked(65,900,7,received),"reader rejects stale payload version");publication.version=blvr_xr_bridge::RigVersion;
    publication.structBytes-=8;
    Check(!BLVR::PlayerViewRig_ReadTracked(65,900,7,received),"reader rejects truncated payload layout");publication.structBytes=sizeof(publication);
    publication.trackedHandMask=4;
    Check(!BLVR::PlayerViewRig_ReadTracked(65,900,7,received),"reader rejects unknown grip validity bits");publication.trackedHandMask=3;
    publication.sequence=3;
    Check(!BLVR::PlayerViewRig_ReadTracked(65,900,7,received),"reader rejects in-progress publication");
    Check(std::memcmp(&received,&beforeMalformed,sizeof(received))==0,"rejected rig reads preserve caller's exact prior output");
    UnmapViewOfFile(history);CloseHandle(mapping);CloseHandle(oldMapping);
    Put(snapshot,0xe0,actor);Check(!BLVR::PlayerViewRig_Begin(scene,actor,eye),"reject unrelated owner");
    Check(std::memcmp(skin,original,sizeof(skin))==0,"NPC skin untouched");
    std::puts("PASS: standing/seated anchors, live native attacks and whole-wing poses, tracking loss/reacquisition, version and singular-transform rejection, exact skin restoration");
}
