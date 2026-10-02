#include "player_view_rig.h"
#include "tracked_basis.h"
#include "shadow_transform.h"
#include "../bridge/eddie_dimensions.h"
#include "../bridge/native_hand_bridge.h"
#include "../input/control_bindings.h"
#include <DirectXMath.h>
#include <algorithm>
#include "../diagnostics/log.h"
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <limits>

namespace BLVR {
namespace {
constexpr unsigned MaxBones = 512;
struct Binding {
    const uint8_t* resource = nullptr;
    unsigned count = 0;
    int head = -1, neck = -1, leftEye = -1, rightEye = -1;
    bool hidden[MaxBones]{};
    bool wing[MaxBones]{};
    int wingRootParent[MaxBones]{};
    bool arm[MaxBones]{};
    bool leftArm[MaxBones]{};
    bool leg[MaxBones]{};
    int parent[MaxBones]{};
    float reference[MaxBones][16]{};
    float eyeCenter[3]{};
    int wrist[2]{-1,-1},middle[2]{-1,-1},index[2]{-1,-1},pinky[2]{-1,-1},tip[2]{-1,-1},previousTip[2]{-1,-1};
    int shoulder[2]{-1,-1},elbow[2]{-1,-1},forearm[2]{-1,-1};
    uint64_t signature=0;
};
Binding binding;
float savedSkin[MaxBones][12];
float* editedSkin = nullptr;
unsigned editedCount = 0;
uint32_t published = 0;
uint8_t* editedSnapshot=nullptr;
float* weaponWorld[2]{};
float savedWeaponWorld[2][16]{};
float* editedWeaponWorld[2]{};
unsigned activeSelectedWeapon=0;
unsigned activeTrackedHands=0;
void* editedScene=nullptr;
uint32_t weaponHandles[2]{0xffffffffu,0xffffffffu};
uint64_t activeRigPose=0,activeRigEpoch=0;
int64_t activeRigDisplayTime=0;

template<class T> T Read(const void* object, unsigned offset) {
    return *reinterpret_cast<const T*>(static_cast<const uint8_t*>(object) + offset);
}
bool Bind(const uint8_t* resource) {
    if (binding.resource == resource) return true;
    binding = {};
    const unsigned count = Read<unsigned>(resource, 0x14);
    const auto* names = Read<const char*>(resource, 8);
    const auto* identifiers = Read<const uint32_t*>(resource, 0x18);
    const auto* parents = Read<const int16_t*>(resource, 0x1c);
    const auto* reference = Read<const float*>(resource, 0x48);
    if (!count || count > MaxBones || !names || !identifiers || !parents || !reference) return false;
    binding.signature=14695981039346656037ull;
    for (unsigned i=0; i<count; ++i) {
        const char* name = names + (identifiers[i*2] & 0xffff);
        binding.signature=blvr_xr_bridge::RigNameHash(binding.signature,name);
        if (std::strcmp(name,"Head")==0) binding.head=i;
        if (std::strcmp(name,"Neck1")==0) binding.neck=i;
        if (std::strcmp(name,"Lf_Eye")==0) binding.leftEye=i;
        if (std::strcmp(name,"Rt_Eye")==0) binding.rightEye=i;
        for(int hand=0;hand<2;++hand)if(std::strncmp(name,hand==0?"Lf_":"Rt_",3)==0) {
            const char* joint=name+3;
            if(std::strcmp(joint,"Wrist")==0)binding.wrist[hand]=i;
            if(std::strcmp(joint,"Shoulder")==0)binding.shoulder[hand]=i;
            if(std::strcmp(joint,"Elbow")==0)binding.elbow[hand]=i;
            if(std::strcmp(joint,"Forearm")==0)binding.forearm[hand]=i;
            if(std::strcmp(joint,"Middle1")==0)binding.middle[hand]=i;
            if(std::strcmp(joint,"Index1")==0)binding.index[hand]=i;
            if(std::strcmp(joint,"Pinky1")==0)binding.pinky[hand]=i;
            if(std::strcmp(joint,"Index3")==0)binding.tip[hand]=i;
            if(std::strcmp(joint,"Index2")==0)binding.previousTip[hand]=i;
        }
        const int parent = parents[i];
        if (parent < -1 || parent >= static_cast<int>(i)) return false;
        binding.parent[i]=parent;
        binding.wing[i]=std::strcmp(name,"Wings")==0||(parent>=0&&binding.wing[parent]);
        binding.wingRootParent[i]=binding.wing[i]?
            (parent>=0&&binding.wing[parent]?binding.wingRootParent[parent]:parent):-1;
        binding.arm[i]=std::strcmp(name,"Lf_Shoulder")==0||std::strcmp(name,"Rt_Shoulder")==0||
            (parent>=0&&binding.arm[parent]);
        binding.leftArm[i]=std::strcmp(name,"Lf_Shoulder")==0||(parent>=0&&binding.leftArm[parent]);
        binding.leg[i]=std::strcmp(name,"Lf_Leg")==0||std::strcmp(name,"Rt_Leg")==0||(parent>=0&&binding.leg[parent]);
        const float* qs = reference+i*12;
        float right[3],up[3],forward[3],local[16]{};
        if (!TrackedEyeBasis(qs+4,0,-1,right,up,forward)) return false;
        for (int k=0;k<3;++k) {
            local[k]=right[k]*qs[8]; local[4+k]=up[k]*qs[9];
            local[8+k]=-forward[k]*qs[10]; local[12+k]=qs[k];
        }
        local[15]=1;
        if (parent >= 0) MultiplyCameraMatrices(local,binding.reference[parent],binding.reference[i]);
        else std::memcpy(binding.reference[i],local,sizeof(local));
    }
    if (binding.head<0 || binding.neck<0 || binding.leftEye<0 || binding.rightEye<0) return false;
    for (int k=0;k<3;++k)
        binding.eyeCenter[k]=(binding.reference[binding.leftEye][12+k]+binding.reference[binding.rightEye][12+k])*.5f;
    for (unsigned i=0;i<count;++i) {
        int ancestor=i;
        while (ancestor>=0 && ancestor!=binding.head) ancestor=parents[ancestor];
        binding.hidden[i] = ancestor==binding.head;
    }
    binding.resource=resource; binding.count=count;
    Log("Player rig bound resource=%p bones=%u Head=%d eyes=%d/%d Neck1=%d",
        resource,count,binding.head,binding.leftEye,binding.rightEye,binding.neck);
    return true;
}
void SkinnedJoint(const float* skin, unsigned joint, float output[3]) {
    const float* reference=binding.reference[joint]+12;
    const float* m=skin+joint*12;
    for (int k=0;k<3;++k)
        output[k]=m[k*4]*reference[0]+m[k*4+1]*reference[1]+m[k*4+2]*reference[2]+m[k*4+3];
}
uint8_t* FindSnapshot(uint8_t* scene,const void* renderOwner,uintptr_t base) {
    // The native opaque, visible and secondary lists contain the same
    // DynamicMeshSnapshot. Ownership is the CoRenderMesh pointer at +e0.
    constexpr unsigned lists[]={0x10f4u,0x1100u,0x1148u,0x1154u};
    for (unsigned offset : lists) {
        const unsigned count=Read<unsigned>(scene,offset)>>6;
        auto* const* entries=Read<uint8_t**>(scene,offset+8);
        if (count>10000 || !entries) continue;
        for (unsigned i=0;i<count;++i) {
            auto* candidate=entries[i];
            if (candidate && Read<uintptr_t>(candidate,0)==base+0xab18b4 &&
                Read<void*>(candidate,0xe0)==renderOwner) return candidate;
        }
    }
    return nullptr;
}
using namespace DirectX;
XMMATRIX Load(const float* m) {return XMLoadFloat4x4(reinterpret_cast<const XMFLOAT4X4*>(m));}
void Store(float* m,FXMMATRIX value) {XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4*>(m),value);}
bool FiniteMatrix(const float* m,float bound=100) {
    for(int i=0;i<16;++i)if(!std::isfinite(m[i])||std::fabs(m[i])>bound)return false;
    return true;
}
bool AffineMatrix(const float* m,float bound=100) {
    return FiniteMatrix(m,bound)&&std::fabs(m[3])<.00001f&&std::fabs(m[7])<.00001f&&
        std::fabs(m[11])<.00001f&&std::fabs(m[15]-1)<.00001f;
}
bool UsableMatrix(const float* m,float bound=100) {
    if(!AffineMatrix(m,bound))return false;
    const float determinant=XMVectorGetX(XMMatrixDeterminant(Load(m)));
    return std::isfinite(determinant)&&std::fabs(determinant)>.00000001f;
}
bool Descendant(unsigned joint,int ancestor) {
    if(ancestor<0)return false;
    for(int i=static_cast<int>(joint);i>=0;i=binding.parent[i])if(i==ancestor)return true;
    return false;
}
XMMATRIX Blend(FXMMATRIX a,FXMMATRIX b,float weight) {
    XMVECTOR sa,qa,ta,sb,qb,tb;
    if(!XMMatrixDecompose(&sa,&qa,&ta,a)||!XMMatrixDecompose(&sb,&qb,&tb,b))return a;
    return XMMatrixScalingFromVector(XMVectorLerp(sa,sb,weight))*
        XMMatrixRotationQuaternion(XMQuaternionSlerp(qa,qb,weight))*XMMatrixTranslationFromVector(XMVectorLerp(ta,tb,weight));
}
float Length(FXMVECTOR v) {return XMVectorGetX(XMVector3Length(v));}
XMMATRIX Aim(FXMVECTOR a,FXMVECTOR b) {
    if(Length(a)<.0001f||Length(b)<.0001f)return XMMatrixIdentity();
    const XMVECTOR from=XMVector3Normalize(a),to=XMVector3Normalize(b);
    const float dot=XMVectorGetX(XMVector3Dot(from,to));
    if(dot>.99999f)return XMMatrixIdentity();
    if(dot<-.99999f) {
        XMVECTOR axis=XMVector3Cross(from,XMVectorSet(0,1,0,0));
        if(Length(axis)<.001f)axis=XMVector3Cross(from,XMVectorSet(1,0,0,0));
        return XMMatrixRotationAxis(axis,XM_PI);
    }
    return XMMatrixRotationQuaternion(XMQuaternionNormalize(XMVectorSetW(XMVector3Cross(from,to),1+dot)));
}
void SolveLiveArm(float model[MaxBones][16],int hand,FXMMATRIX requested) {
    const int shoulder=binding.shoulder[hand],elbow=binding.elbow[hand],wrist=binding.wrist[hand];
    if(shoulder<0||elbow<0||wrist<0||static_cast<unsigned>(wrist)>=editedCount)return;
    const XMVECTOR s=Load(model[shoulder]).r[3],e=Load(model[elbow]).r[3],w=Load(model[wrist]).r[3];
    const float upper=Length(e-s),lower=Length(w-e);
    if(upper<.001f||lower<.001f)return;
    XMMATRIX wristDelta=XMMatrixInverse(nullptr,Load(model[wrist]))*requested;
    const XMVECTOR raw=requested.r[3];
    const XMVECTOR direction=XMVector3Normalize(Length(raw-s)>.0001f?raw-s:w-s);
    const float d=std::clamp(Length(raw-s),std::fabs(upper-lower)+.001f,upper+lower-.002f);
    const XMVECTOR target=s+direction*d;
    wristDelta=wristDelta*XMMatrixTranslationFromVector(target-raw);
    const XMVECTOR pole=XMVectorSet(hand==0?.65f:-.65f,-.5f,-.25f,0);
    XMVECTOR bend=pole-direction*XMVector3Dot(pole,direction);
    if(Length(bend)<.001f) {
        const XMVECTOR fallback=std::fabs(XMVectorGetY(direction))<.9f?XMVectorSet(0,-1,0,0):XMVectorSet(0,0,1,0);
        bend=fallback-direction*XMVector3Dot(fallback,direction);
    }
    const float along=(upper*upper-lower*lower+d*d)/(2*d);
    const XMVECTOR joint=s+direction*along+XMVector3Normalize(bend)*std::sqrt((std::max)(0.f,upper*upper-along*along));
    const XMMATRIX upperDelta=XMMatrixTranslationFromVector(-s)*Aim(e-s,joint-s)*XMMatrixTranslationFromVector(s);
    const XMMATRIX lowerDelta=XMMatrixTranslationFromVector(-e)*Aim(w-e,target-joint)*XMMatrixTranslationFromVector(joint);
    for(unsigned i=0;i<editedCount;++i) {
        if(Descendant(i,wrist))Store(model[i],Load(model[i])*wristDelta);
        else if(Descendant(i,elbow))Store(model[i],Load(model[i])*lowerDelta);
        else if(Descendant(i,shoulder))Store(model[i],Load(model[i])*upperDelta);
    }
    const int forearm=binding.forearm[hand];
    if(forearm>=0&&static_cast<unsigned>(forearm)<editedCount) {
        XMVECTOR ls,lq,lt,ws,wq,wt;
        if(XMMatrixDecompose(&ls,&lq,&lt,lowerDelta)&&XMMatrixDecompose(&ws,&wq,&wt,wristDelta)) {
            XMVECTOR difference=XMQuaternionRotationMatrix(XMMatrixTranspose(XMMatrixRotationQuaternion(lq))*XMMatrixRotationQuaternion(wq));
            if(XMVectorGetW(difference)<0)difference=-difference;
            const XMVECTOR axis=XMVector3Normalize(target-joint);
            XMVECTOR twist=XMVectorSetW(axis*XMVector3Dot(difference,axis),XMVectorGetW(difference));
            twist=XMVectorGetX(XMVector4LengthSq(twist))>.00001f?XMQuaternionNormalize(twist):XMQuaternionIdentity();
            // Keep the midpoint on the solved forearm while distributing axial twist.
            XMMATRIX rotation=XMMatrixRotationQuaternion(XMQuaternionSlerp(XMQuaternionIdentity(),twist,.65f));
            const XMVECTOR midpoint=Load(model[forearm]).r[3];
            rotation=XMMatrixTranslationFromVector(-midpoint)*rotation*XMMatrixTranslationFromVector(midpoint);
            Store(model[forearm],Load(model[forearm])*rotation);
        }
    }
}
bool RetargetLive(const blvr_xr_bridge::RigFrame& rig,const float headToBody[16],
                  const float world[16],float skins[MaxBones][16],float weapons[2][16]) {
    float native[MaxBones][16]{},tracked[MaxBones][16]{},model[MaxBones][16]{};
    for(unsigned i=0;i<editedCount;++i) {
        float m[16]{};m[15]=1;
        for(int r=0;r<3;++r)for(int c=0;c<4;++c)m[c*4+r]=savedSkin[i][r*4+c];
        Store(native[i],Load(binding.reference[i])*Load(m));
        Store(tracked[i],Load(binding.reference[i])*Load(skins[i]));
    }
    std::memcpy(model,tracked,sizeof(model));
    // Re-express this frame's native legs and any unavailable arm beneath the
    // stable tracked torso. Never substitute an authored neutral or old pose.
    for(unsigned i=0;i<editedCount;++i)if(binding.parent[i]>=0&&
        (binding.leg[i]||(binding.arm[i]&&!(rig.trackedHandMask&(binding.leftArm[i]?1u:2u))))) {
        const int parent=binding.parent[i];
        Store(model[i],Load(native[i])*XMMatrixInverse(nullptr,Load(native[parent]))*Load(model[parent]));
    }
    // The native Wings subtree includes membrane patch joints whose names do
    // not contain "Wing". Preserve the whole current flapping pose beneath
    // the tracked spine; mixing native wings with tracked patches stretches
    // the membrane across the viewer during flight. One parent delta also
    // preserves legitimately collapsed/stowed wing joints without inverting
    // their singular transforms.
    for(unsigned i=0;i<editedCount;++i)if(binding.wing[i]) {
        const int parent=binding.wingRootParent[i];
        if(parent<0||static_cast<unsigned>(parent)>=editedCount||!UsableMatrix(native[parent]))return false;
        Store(model[i],Load(native[i])*XMMatrixInverse(nullptr,Load(native[parent]))*Load(model[parent]));
    }
    const float weight=rig.liveActionWeight;
    if(rig.liveAction==2&&rig.trackedHandMask==3&&weaponWorld[1]&&binding.wrist[1]>=0&&
       static_cast<unsigned>(binding.wrist[1])<editedCount) {
        const int wrist=binding.wrist[1];
        XMVECTOR determinant;
        const XMMATRIX nativeGuitarInverse=XMMatrixInverse(&determinant,Load(weaponWorld[1]));
        // A native hidden/stowed guitar can have a collapsed packet. Keep
        // tracking and live legs when its attachment is unavailable.
        if(std::isfinite(XMVectorGetX(determinant))&&std::fabs(XMVectorGetX(determinant))>=.00001f) {
            XMMATRIX target=Load(native[wrist])*Load(world)*nativeGuitarInverse*Load(rig.weaponToHead[1])*Load(headToBody);
            XMVECTOR ts,tq,tt,ss,sq,st;
            if(XMMatrixDecompose(&ts,&tq,&tt,target)&&XMMatrixDecompose(&ss,&sq,&st,Load(tracked[wrist])))
                target=XMMatrixScalingFromVector(ss)*XMMatrixRotationQuaternion(tq)*XMMatrixTranslationFromVector(tt);
            SolveLiveArm(model,1,Blend(Load(tracked[wrist]),target,weight));
        }
    } else if(rig.liveAction==1||rig.liveAction==3) {
        for(unsigned i=0;i<editedCount;++i) {
            if(!binding.arm[i]||(rig.liveAction==1&&binding.leftArm[i])||
                !(rig.trackedHandMask&(binding.leftArm[i]?1u:2u)))continue;
            const int parent=binding.parent[i];
            if(parent<0||static_cast<unsigned>(parent)>=editedCount)continue;
            const XMMATRIX local=Load(tracked[i])*XMMatrixInverse(nullptr,Load(tracked[parent]));
            XMMATRIX authored=Load(native[i])*XMMatrixInverse(nullptr,Load(native[parent]));
            const int hand=binding.leftArm[i]?0:1;
            if(static_cast<int>(i)==binding.wrist[hand]) {
                XMVECTOR ss,sq,st,ns,nq,nt;
                if(XMMatrixDecompose(&ss,&sq,&st,local)&&XMMatrixDecompose(&ns,&nq,&nt,authored))
                    authored=XMMatrixScalingFromVector(ss)*XMMatrixRotationQuaternion(nq)*XMMatrixTranslationFromVector(nt);
            }
            // Keep the measured finger-to-handle contact while the live arm moves.
            const bool finger=static_cast<int>(i)!=binding.wrist[hand]&&Descendant(i,binding.wrist[hand]);
            Store(model[i],(finger?local:Blend(local,authored,weight))*Load(model[parent]));
        }
        if(rig.supportHeld&&rig.trackedHandMask==3&&rig.liveAction==1&&binding.wrist[0]>=0&&binding.wrist[1]>=0) {
            const XMMATRIX delta=XMMatrixInverse(nullptr,Load(tracked[binding.wrist[1]]))*Load(model[binding.wrist[1]]);
            SolveLiveArm(model,0,Load(tracked[binding.wrist[0]])*delta);
        }
    }
    for(unsigned i=0;i<editedCount;++i)if(binding.arm[i]||binding.leg[i]||binding.wing[i]) {
        const XMMATRIX changed=XMMatrixInverse(nullptr,Load(binding.reference[i]))*Load(model[i]);
        XMFLOAT4X4 finite;XMStoreFloat4x4(&finite,changed);
        if(!AffineMatrix(reinterpret_cast<const float*>(&finite)))return false;
    }
    for(unsigned i=0;i<editedCount;++i)if(binding.arm[i]||binding.leg[i]||binding.wing[i])Store(skins[i],XMMatrixInverse(nullptr,Load(binding.reference[i]))*Load(model[i]));
    for(int weapon=0;weapon<2;++weapon) {
        const int hand=weapon==0?1:0,wrist=binding.wrist[hand];
        if(wrist<0||static_cast<unsigned>(wrist)>=editedCount)continue;
        const bool trackedHand=(rig.trackedHandMask&(1u<<hand))!=0;
        // A missing controller keeps the native weapon-to-wrist relationship
        // from the same render snapshot, then shares the fallback arm's root.
        const XMMATRIX socket=trackedHand?
            Load(rig.weaponToHead[weapon])*Load(headToBody)*XMMatrixInverse(nullptr,Load(tracked[wrist])):
            (weaponWorld[weapon]?Load(weaponWorld[weapon])*XMMatrixInverse(nullptr,Load(world))*XMMatrixInverse(nullptr,Load(native[wrist])):
                Load(rig.weaponToHead[weapon])*Load(headToBody)*XMMatrixInverse(nullptr,Load(tracked[wrist])));
        Store(weapons[weapon],socket*Load(model[wrist])*Load(world));
        if(weaponWorld[weapon]&&!AffineMatrix(weapons[weapon],std::numeric_limits<float>::max()))return false;
    }
    return true;
}
}

void* PlayerViewRig_ResolveActor(void* cameraActor) {
    if(!cameraActor)return nullptr;
    __try {
        const uintptr_t base=reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
        const auto isEddie=[](void* actor) {
            if(!actor)return false;
            const auto* type=Read<uint8_t*>(actor,4);
            const auto* definition=type?Read<uint8_t*>(type,4):nullptr;
            const auto* name=definition?Read<const char*>(definition,0):nullptr;
            return name&&std::strncmp(name,"Player_A",9)==0;
        };
        if(isEddie(cameraActor))return cameraActor;
        // Entity's sorted component map: count at +3c >> 6, pairs at +44
        // (retail lookup 0x4b9ef0). CoMount +20 is its current rider handle.
        const unsigned count=Read<unsigned>(cameraActor,0x3c)>>6;
        const auto* entries=Read<uint8_t*>(cameraActor,0x44);
        if(!entries||count>256)return nullptr;
        for(unsigned i=0;i<count;++i) {
            auto* mount=Read<uint8_t*>(entries,i*8+4);
            if(!mount||Read<uintptr_t>(mount,0)!=base+0xad38f4||Read<void*>(mount,0x10)!=cameraActor)continue;
            const unsigned handle=Read<unsigned>(mount,0x20);
            const auto* table=*reinterpret_cast<uint8_t**>(base+0xb79d8c);
            // +94 is the LIVE count, not the largest allocated handle. After
            // checkpoints/retries a valid rider can occupy a higher sparse slot.
            const unsigned entities=*reinterpret_cast<unsigned*>(base+0xb79d90);
            if(!table||entities>0x100000||handle>=entities)return nullptr;
            auto* rider=Read<void*>(table,handle*12);
            if(rider&&Read<unsigned>(rider,0x14)==handle&&isEddie(rider))return rider;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return nullptr;
}

void* PlayerViewRig_ResolveMount(void* actor) {
    if(!actor)return nullptr;
    __try {
        const uintptr_t base=reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
        const auto* player=Read<const uint8_t*>(actor,0x28);
        if(!player||Read<uintptr_t>(player,0)!=base+0xab71ec||Read<void*>(player,0x10)!=actor)return nullptr;
        // CoPlayer +34 is the native carrier entity handle (FFFFFFFF on foot).
        // Observed through the real enter/exit path, including LampreyCamera.
        const unsigned handle=Read<unsigned>(player,0x34);
        const auto* table=*reinterpret_cast<const uint8_t**>(base+0xb79d8c);
        const unsigned capacity=*reinterpret_cast<unsigned*>(base+0xb79d90);
        if(!table||capacity>0x100000||handle>=capacity)return nullptr;
        auto* carrier=Read<void*>(table,handle*12);
        if(!carrier||Read<unsigned>(carrier,0x14)!=handle)return nullptr;
        const unsigned count=Read<unsigned>(carrier,0x3c)>>6;
        const auto* entries=Read<const uint8_t*>(carrier,0x44);
        if(!entries||count>256)return nullptr;
        for(unsigned i=0;i<count;++i) {
            const auto* mount=Read<const uint8_t*>(entries,i*8+4);
            if(mount&&Read<uintptr_t>(mount,0)==base+0xad38f4&&Read<void*>(mount,0x10)==carrier&&
                Read<unsigned>(mount,0x20)==Read<unsigned>(actor,0x14))return carrier;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return nullptr;
}

bool PlayerViewRig_ReadLiveEyeAnchor(void* actor,bool mounted,float eyeWorld[3]) {
    if(!actor)return false;
    __try {
        const uintptr_t base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        const auto* mesh=Read<const uint8_t*>(actor,0x38);
        const auto* skeleton=Read<const uint8_t*>(actor,0x24);
        if(!mesh||!skeleton||Read<const void*>(mesh,0x10)!=actor||Read<const void*>(skeleton,0x10)!=actor||
           Read<uintptr_t>(mesh,0)!=base+0xa9796c||Read<uintptr_t>(skeleton,0)!=base+0xaf8b00)return false;
        const auto* world=reinterpret_cast<const float*>(mesh+0x70);
        float result[3]={world[12],world[13],world[14]};
        const float scale=std::sqrt(world[0]*world[0]+world[1]*world[1]+world[2]*world[2]);
        if(!std::isfinite(scale)||scale<.001f)return false;
        if(!mounted)result[1]+=scale*blvr_rig::EyeHeightMeters;
        else {
            // Owned retail animation takes store local TRS values in pose+28.
            // Reconstruct only the current eye joints; no global rig binding or
            // native skin buffer is modified on the collection thread.
            const auto* animation=Read<const uint8_t*>(skeleton,0x24);
            if(!animation)return false;
            const auto* resource=Read<const uint8_t*>(animation,4);
            const auto* pose=Read<const uint8_t*>(animation,0x50);
            if(!resource||!pose)return false;
            const unsigned count=Read<unsigned>(resource,0x14);
            if(!count||count>MaxBones)return false;
            const auto* names=Read<const char*>(resource,8);
            const auto* identifiers=Read<const uint32_t*>(resource,0x18);
            const auto* parents=Read<const int16_t*>(resource,0x1c);
            const auto* local=Read<const float*>(pose,0x28);
            if(!names||!identifiers||!parents||!local)return false;
            float model[MaxBones][16]{};int eyes[2]{-1,-1};
            for(unsigned i=0;i<count;++i) {
                const char* name=names+(identifiers[i*2]&0xffff);
                if(std::strcmp(name,"Lf_Eye")==0)eyes[0]=i;
                if(std::strcmp(name,"Rt_Eye")==0)eyes[1]=i;
                const float* trs=local+i*12;
                for(unsigned k=0;k<12;++k)if(!std::isfinite(trs[k]))return false;
                float right[3],up[3],forward[3],transform[16]{};
                if(!TrackedEyeBasis(trs+4,0,-1,right,up,forward))return false;
                for(int k=0;k<3;++k) {
                    transform[k]=right[k]*trs[8];transform[4+k]=up[k]*trs[9];
                    transform[8+k]=-forward[k]*trs[10];transform[12+k]=trs[k];
                }
                transform[15]=1;
                const int parent=parents[i];
                if(parent < -1||parent>=static_cast<int>(i))return false;
                if(parent>=0)MultiplyCameraMatrices(transform,model[parent],model[i]);
                else std::memcpy(model[i],transform,sizeof(transform));
                if(eyes[0]>=0&&eyes[1]>=0)break;
            }
            if(eyes[0]<0||eyes[1]<0)return false;
            float center[3];for(int k=0;k<3;++k)center[k]=(model[eyes[0]][12+k]+model[eyes[1]][12+k])*.5f;
            for(int k=0;k<3;++k)result[k]+=center[0]*world[k]+center[1]*world[4+k]+center[2]*world[8+k];
        }
        for(float v:result)if(!std::isfinite(v))return false;
        std::memcpy(eyeWorld,result,sizeof(result));return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

bool PlayerViewRig_Begin(void* scenePointer,void* actor,float eyeWorld[3],bool mounted) {
    PlayerViewRig_End();
    if (!scenePointer || !actor) return false;
    __try {
        const uintptr_t base=reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
        auto* mesh=Read<uint8_t*>(actor,0x38);
        auto* skeleton=Read<uint8_t*>(actor,0x24);
        if (!mesh || !skeleton || Read<void*>(mesh,0x10)!=actor || Read<void*>(skeleton,0x10)!=actor ||
            Read<uintptr_t>(mesh,0)!=base+0xa9796c || Read<uintptr_t>(skeleton,0)!=base+0xaf8b00) return false;
        auto* animation=Read<uint8_t*>(skeleton,0x24);
        if (!animation || !Bind(Read<uint8_t*>(animation,4))) return false;
        auto* snapshot=FindSnapshot(static_cast<uint8_t*>(scenePointer),mesh,base);
        if (!snapshot) {
            static unsigned missing=0;
            if(++missing<=3 || missing%300==0) {
                auto* scene=static_cast<uint8_t*>(scenePointer);
                Log("Player rig missing snapshot: scene=%p actor=%p owner=%p lists=%u/%u/%u/%u mounted=%d",
                    scenePointer,actor,mesh,Read<unsigned>(scene,0x10f4)>>6,Read<unsigned>(scene,0x1100)>>6,
                    Read<unsigned>(scene,0x1148)>>6,Read<unsigned>(scene,0x1154)>>6,mounted);
            }
            return false;
        }
        const auto* header=Read<uint8_t*>(snapshot,0x134);
        if (!header) return false;
        // Retail builds a prefix palette through the mesh's highest joint
        // (0x733929..0x733942 -> 0x41c9a0), not always the full skeleton.
        // Eddie's regular 204-entry mesh omits eight trailing deformation
        // joints used by the 212-entry variant. Indices are not renumbered.
        const unsigned paletteCount=Read<uint16_t>(header,4);
        if (paletteCount>binding.count || paletteCount<=static_cast<unsigned>(binding.head) ||
            paletteCount<=static_cast<unsigned>(binding.neck) ||
            paletteCount<=static_cast<unsigned>(binding.leftEye) ||
            paletteCount<=static_cast<unsigned>(binding.rightEye)) return false;
        auto* skin=Read<float*>(header,0);
        if (!skin) return false;
        float left[3],right[3],neck[3],center[3];
        SkinnedJoint(skin,binding.leftEye,left);SkinnedJoint(skin,binding.rightEye,right);
        SkinnedJoint(skin,binding.neck,neck);
        for (int k=0;k<3;++k) center[k]=(left[k]+right[k])*.5f;
        const float* world=reinterpret_cast<const float*>(snapshot+0x70);
        const float headingLength=std::hypot(world[8],world[10]);
        const float scale=std::sqrt(world[0]*world[0]+world[1]*world[1]+world[2]*world[2]);
        if (!std::isfinite(headingLength) || headingLength<.001f || !std::isfinite(scale) || scale<.001f) return false;
        // The skeleton supplies Eddie's anatomical eye center; gameplay supplies
        // its moving root. Combat animation must not translate, pitch or roll
        // the viewer. Use the same metric body height as the tracked rig and
        // keep a level body yaw frame, with feet at the native actor root.
        // The HMD alone supplies the view's pitch/roll and room-scale motion.
        // A bind-eye horizontal offset rotated by the animated actor made the
        // floor slide when Eddie turned in place. Only tracking moves this pivot.
        eyeWorld[0]=world[12];
        eyeWorld[1]=world[13]+scale*blvr_rig::EyeHeightMeters;
        eyeWorld[2]=world[14];
        // Scripted mounteds can move Eddie far from the animation root.
        // Follow the seat while leaving pitch and roll to the HMD.
        // Gameplay keeps the stable pivot above, including attack animations.
        if(mounted) for(int k=0;k<3;++k)
            eyeWorld[k]=world[12+k]+center[0]*world[k]+center[1]*world[4+k]+center[2]*world[8+k];
        for (int k=0;k<3;++k) {
            if (!std::isfinite(eyeWorld[k]) || !std::isfinite(neck[k])) return false;
        }
        // Copy before touching the render-owned skin matrices. Collapse only
        // the named Head subtree to Neck1; shoulders, wrists and weapon sockets
        // retain every byte of their authored skin transforms.
        std::memcpy(savedSkin,skin,paletteCount*12*sizeof(float));
        editedSkin=skin;editedCount=paletteCount;
        editedSnapshot=snapshot;
        editedScene=scenePointer;
        // Inventory handles, not proximity or any NPC's matching mesh.
        // The two owned weapon render packets use the same observed snapshot
        // layout as Eddie; mutation is limited to this stereo render pair.
        weaponWorld[0]=weaponWorld[1]=nullptr;
        __try {
            auto* inventory=Read<uint8_t*>(actor,0x64);
            auto* table=*reinterpret_cast<uint8_t**>(base+0xb79d8c);
            if(inventory&&table&&Read<void*>(inventory,0x10)==actor) {
                const unsigned offsets[]{0x20,0x38};
                for(int i=0;i<2;++i) {
                    const unsigned handle=Read<unsigned>(inventory,offsets[i]);
                    if(handle>=0x100000)continue;
                    auto* entity=Read<uint8_t*>(table,handle*12);
                    if(!entity)continue;
                    auto* render=Read<uint8_t*>(entity,0x38);
                    if(!render||Read<void*>(render,0x10)!=entity||Read<uintptr_t>(render,0)!=base+0xa9796c)continue;
                    auto* packet=FindSnapshot(static_cast<uint8_t*>(scenePointer),render,base);
                    if(packet) {weaponWorld[i]=reinterpret_cast<float*>(packet+0x70);weaponHandles[i]=handle;}
                }
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {weaponWorld[0]=weaponWorld[1]=nullptr;}
        for (unsigned i=0;i<editedCount;++i) if (binding.hidden[i]) {
            std::memset(skin+i*12,0,12*sizeof(float));
            for (int k=0;k<3;++k) skin[i*12+k*4+3]=neck[k];
        }
        if (++published<=3 || published%300==0)
            Log("Player rig frame=%u actor=%p snapshot=%p palette=%u/%u eye=(%.3f,%.3f,%.3f) animatedLocalEye=(%.3f,%.3f,%.3f) headHidden=1 mounted=%d",
                published,actor,snapshot,editedCount,binding.count,eyeWorld[0],eyeWorld[1],eyeWorld[2],center[0],center[1],center[2],mounted);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        PlayerViewRig_End();return false;
    }
}
bool PlayerViewRig_ReadTracked(uint64_t frame,int64_t displayTime,uint64_t epoch,blvr_xr_bridge::RigFrame& out) {
    static HANDLE mapping=nullptr;
    static const blvr_xr_bridge::RigHistoryBuffer* history=nullptr;
    if(!history) {
        mapping=OpenFileMappingW(FILE_MAP_READ,FALSE,blvr_xr_bridge::RigMappingName);
        if(!mapping)return false;
        history=static_cast<const blvr_xr_bridge::RigHistoryBuffer*>(MapViewOfFile(mapping,FILE_MAP_READ,0,0,sizeof(*history)));
        if(!history){CloseHandle(mapping);mapping=nullptr;return false;}
    }
    const auto& source=history->slots[frame%blvr_xr_bridge::RigHistory];
    const int32_t sequence=source.sequence;
    if(sequence&1)return false;
    blvr_xr_bridge::RigFrame candidate{};
    MemoryBarrier();std::memcpy(&candidate,&source,sizeof(candidate));MemoryBarrier();
    if(sequence!=source.sequence||candidate.magic!=blvr_xr_bridge::RigMagic||candidate.producerEpoch!=epoch||
        candidate.frameId!=frame||candidate.predictedDisplayTime!=displayTime||!candidate.boneCount||
        candidate.boneCount>blvr_xr_bridge::RigBones||candidate.version!=blvr_xr_bridge::RigVersion||
        candidate.structBytes!=sizeof(candidate)||candidate.trackedHandMask>3)return false;
    out=candidate;return true;
}
bool PlayerViewRig_ApplyTracked(const blvr_xr_bridge::RigFrame& rig,const float headWorld[16],bool nativeDrivingPose,bool wheelGrip) {
    if(!editedSkin||!editedSnapshot||rig.boneCount>blvr_xr_bridge::RigBones||rig.boneCount!=binding.count||rig.skeletonSignature!=binding.signature||rig.selectedWeapon>2||
       rig.version!=blvr_xr_bridge::RigVersion||rig.structBytes!=sizeof(rig)||rig.trackedHandMask>3||rig.supportHeld>1)return false;
    if(rig.controlsSignature!=ControlsSignature(ActiveBindings())||rig.liveAction>3||
        !std::isfinite(rig.liveActionWeight)||rig.liveActionWeight<0||rig.liveActionWeight>1)return false;
    // Never partially apply a malformed transform or substitute a newer rig
    // for the source pose whose eyes will be submitted to OpenXR.
    for(unsigned i=0;i<rig.boneCount;++i)if(!UsableMatrix(rig.skinToHead[i]))return false;
    for(const auto& weapon:rig.weaponToHead)if(!FiniteMatrix(weapon))return false;
    if(!UsableMatrix(headWorld,std::numeric_limits<float>::max()))return false;
    // Retain the authored RIGHT-hand wheel grip and native weapon stow. The
    // left shoulder/arm stays tracked so the player can wave out the window.
    __try {
        const float* world=reinterpret_cast<const float*>(editedSnapshot+0x70);
        if(!UsableMatrix(world,std::numeric_limits<float>::max()))return false;
        float inverse[16]{};
        const float scaleSq=world[0]*world[0]+world[1]*world[1]+world[2]*world[2];
        if(scaleSq<.00001f||!std::isfinite(scaleSq))return false;
        for(int r=0;r<3;++r)for(int c=0;c<3;++c)inverse[r*4+c]=world[c*4+r]/scaleSq;
        for(int c=0;c<3;++c)for(int r=0;r<3;++r)inverse[12+c]-=world[12+r]*inverse[r*4+c];
        inverse[15]=1;
        float headToBody[16];MultiplyCameraMatrices(headWorld,inverse,headToBody);
        float transformedSkin[MaxBones][16]{},transformedWeapons[2][16]{};
        for(unsigned i=0;i<editedCount;++i)MultiplyCameraMatrices(rig.skinToHead[i],headToBody,transformedSkin[i]);
        for(int i=0;i<2;++i)MultiplyCameraMatrices(rig.weaponToHead[i],headWorld,transformedWeapons[i]);
        if(!nativeDrivingPose&&!RetargetLive(rig,headToBody,world,transformedSkin,transformedWeapons))return false;
        for(unsigned i=0;i<editedCount;++i) {
            if(nativeDrivingPose&&(!binding.arm[i]||(wheelGrip&&!binding.leftArm[i])))continue;
            if(nativeDrivingPose&&binding.arm[i]&&!(rig.trackedHandMask&(binding.leftArm[i]?1u:2u)))continue;
            for(int r=0;r<3;++r)for(int c=0;c<4;++c)editedSkin[i*12+r*4+c]=transformedSkin[i][c*4+r];
        }
        float neck[3];SkinnedJoint(editedSkin,binding.neck,neck);
        for(unsigned i=0;i<editedCount;++i)if(binding.hidden[i]) {
            std::memset(editedSkin+i*12,0,12*sizeof(float));
            for(int k=0;k<3;++k)editedSkin[i*12+k*4+3]=neck[k];
        }
        for(int i=0;i<2;++i)if(!nativeDrivingPose&&weaponWorld[i]) {
            editedWeaponWorld[i]=weaponWorld[i];std::memcpy(savedWeaponWorld[i],weaponWorld[i],64);
            float* transformed=transformedWeapons[i];
            if(rig.selectedWeapon!=static_cast<unsigned>(i+1)) {
                for(int r=0;r<3;++r)for(int c=0;c<3;++c)transformed[r*4+c]=0;
            }
            std::memcpy(weaponWorld[i],transformed,64);
        }
        static unsigned applied=0;
        activeSelectedWeapon=nativeDrivingPose?0u:rig.selectedWeapon;
        activeTrackedHands=rig.trackedHandMask;
        activeRigPose=rig.frameId;activeRigEpoch=rig.producerEpoch;activeRigDisplayTime=rig.predictedDisplayTime;
        if(++applied<=3||applied%300==0)Log("Tracked Eddie: frame=%llu bones=%u selected=%u weaponPackets=%d/%d",
            static_cast<unsigned long long>(rig.frameId),rig.boneCount,rig.selectedWeapon,weaponWorld[0]!=nullptr,weaponWorld[1]!=nullptr);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {PlayerViewRig_End();return false;}
}
bool PlayerViewRig_ReadWeaponAttachment(uint32_t handle,void* scene,uint64_t poseFrame,
    int64_t displayTime,uint64_t epoch,TrackedWeaponAttachment& output) {
    if(!editedSkin||!editedSnapshot||scene!=editedScene||!poseFrame||!epoch||
       poseFrame!=activeRigPose||displayTime!=activeRigDisplayTime||epoch!=activeRigEpoch||
       activeSelectedWeapon<1||activeSelectedWeapon>2)return false;
    const unsigned weapon=activeSelectedWeapon-1;
    const unsigned requiredHand=weapon==0?2u:1u;
    if(handle==0xffffffffu||handle!=weaponHandles[weapon]||
       !(activeTrackedHands&requiredHand)||!editedWeaponWorld[weapon])return false;
    __try {
        TrackedWeaponAttachment candidate{};
        std::memcpy(candidate.nativeWorld,savedWeaponWorld[weapon],64);
        std::memcpy(candidate.trackedWorld,editedWeaponWorld[weapon],64);
        if(!AffineMatrix(candidate.nativeWorld,std::numeric_limits<float>::max())||
           !AffineMatrix(candidate.trackedWorld,std::numeric_limits<float>::max()))return false;
        candidate.handle=handle;candidate.poseFrame=poseFrame;candidate.epoch=epoch;candidate.displayTime=displayTime;
        output=candidate;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
void PlayerViewRig_PublishRenderedUi(uint64_t sourceFrame,uint64_t poseFrame,
    int64_t displayTime,uint64_t epoch,const float headWorld[16],bool mounted) {
    static unsigned attempts=0;
    if(++attempts<=3)Log("Rendered UI: publish source=%llu pose=%llu epoch=%llu skin=%p snapshot=%p",
        sourceFrame,poseFrame,epoch,editedSkin,editedSnapshot);
    if(!editedSkin||!editedSnapshot||!sourceFrame||!epoch)return;
    static HANDLE mapping=nullptr;
    static blvr_xr_bridge::NativeHandHistoryBuffer* history=nullptr;
    if(!history) {
        mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,
            sizeof(blvr_xr_bridge::NativeHandHistoryBuffer),blvr_xr_bridge::NativeHandMappingName);
        if(!mapping){Log("Rendered UI: mapping failed error=%lu",GetLastError());return;}
        history=static_cast<blvr_xr_bridge::NativeHandHistoryBuffer*>(MapViewOfFile(mapping,FILE_MAP_WRITE,0,0,sizeof(*history)));
        if(!history){Log("Rendered UI: map view failed error=%lu",GetLastError());CloseHandle(mapping);mapping=nullptr;return;}
        // Keep the named object handle alive so the other process can open it.
        // A mapped view alone does not retain the object-manager name.
    }
    blvr_xr_bridge::NativeHandFrame output{};
    output.magic=blvr_xr_bridge::NativeHandMagic;output.producerEpoch=epoch;
    output.version=blvr_xr_bridge::NativeHandVersion;output.pointerValidMask=activeTrackedHands;
    output.sourceFrameId=sourceFrame;output.poseFrameId=poseFrame;output.predictedDisplayTime=displayTime;
    output.flags=mounted?blvr_xr_bridge::NativeUiDriving:0u;
    float inverseHead[16]{};inverseHead[15]=1;
    for(int r=0;r<3;++r)for(int c=0;c<3;++c)inverseHead[r*4+c]=headWorld[c*4+r];
    for(int c=0;c<3;++c)for(int r=0;r<3;++r)inverseHead[12+c]-=headWorld[12+r]*inverseHead[r*4+c];
    const float* world=reinterpret_cast<const float*>(editedSnapshot+0x70);
    float bodyToHead[16];MultiplyCameraMatrices(world,inverseHead,bodyToHead);
    if(activeSelectedWeapon==2&&weaponWorld[1]) {
        MultiplyCameraMatrices(weaponWorld[1],inverseHead,output.guitarToHead);
        if(UsableMatrix(output.guitarToHead,20))output.flags|=blvr_xr_bridge::NativeUiGuitar;
    }
    const auto normalize=[](float* v) {
        const float n=std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);
        if(!std::isfinite(n)||n<.00001f)return false;
        for(int k=0;k<3;++k)v[k]/=n;return true;
    };
    const auto cross=[](const float* a,const float* b,float* v) {
        v[0]=a[1]*b[2]-a[2]*b[1];v[1]=a[2]*b[0]-a[0]*b[2];v[2]=a[0]*b[1]-a[1]*b[0];
    };
    for(int hand=0;hand<2;++hand) {
        const int joints[]{binding.wrist[hand],binding.middle[hand],binding.index[hand],binding.pinky[hand],binding.tip[hand],binding.previousTip[hand]};
        bool valid=true;for(int joint:joints)if(joint<0||joint>=static_cast<int>(editedCount))valid=false;
        if(!valid)continue;
        float palm[16]{};palm[15]=1;
        const float *w=binding.reference[joints[0]]+12,*m=binding.reference[joints[1]]+12,
            *i=binding.reference[joints[2]]+12,*p=binding.reference[joints[3]]+12;
        for(int k=0;k<3;++k) {palm[8+k]=w[k]-m[k];palm[k]=(hand==0?1.f:-1.f)*(i[k]-p[k]);palm[12+k]=w[k]*.35f+m[k]*.65f;}
        if(!normalize(palm+8)||!normalize(palm))continue;
        cross(palm+8,palm,palm+4);if(!normalize(palm+4))continue;cross(palm+4,palm+8,palm);
        float skin[16]{};skin[15]=1;
        for(int r=0;r<3;++r)for(int c=0;c<4;++c)skin[c*4+r]=editedSkin[joints[0]*12+r*4+c];
        float modelPalm[16];MultiplyCameraMatrices(palm,skin,modelPalm);
        MultiplyCameraMatrices(modelPalm,bodyToHead,output.palmToHead[hand]);
        float refTip[3],tip[3];
        for(int k=0;k<3;++k)refTip[k]=binding.reference[joints[4]][12+k]
            +.72f*(binding.reference[joints[4]][12+k]-binding.reference[joints[5]][12+k]);
        const float* tipSkin=editedSkin+joints[4]*12;
        for(int k=0;k<3;++k)tip[k]=refTip[0]*tipSkin[k*4]+refTip[1]*tipSkin[k*4+1]+refTip[2]*tipSkin[k*4+2]+tipSkin[k*4+3];
        for(int k=0;k<3;++k)output.indexTipToHead[hand][k]=tip[0]*bodyToHead[k]+tip[1]*bodyToHead[4+k]+tip[2]*bodyToHead[8+k]+bodyToHead[12+k];
        bool finiteTip=true;for(float value:output.indexTipToHead[hand])finiteTip&=std::isfinite(value)&&std::fabs(value)<20;
        if(UsableMatrix(output.palmToHead[hand],20)&&finiteTip)output.validMask|=1u<<hand;
    }
    auto& destination=history->slots[sourceFrame%blvr_xr_bridge::NativeHandHistory];
    auto* sequence=reinterpret_cast<volatile LONG*>(&destination.sequence);
    const LONG writing=static_cast<LONG>((static_cast<uint32_t>(destination.sequence)&~1u)+1u);
    InterlockedExchange(sequence,writing);MemoryBarrier();
    std::memcpy(reinterpret_cast<char*>(&destination)+4,reinterpret_cast<const char*>(&output)+4,sizeof(output)-4);
    MemoryBarrier();InterlockedExchange(sequence,writing+1);
    if(attempts<=3)Log("Rendered UI: published validMask=%u pointerMask=%u",output.validMask,output.pointerValidMask);
}
void PlayerViewRig_End() {
    for(int i=0;i<2;++i)if(editedWeaponWorld[i]) {
        __try {std::memcpy(editedWeaponWorld[i],savedWeaponWorld[i],64);} __except(EXCEPTION_EXECUTE_HANDLER) {}
        editedWeaponWorld[i]=nullptr;
    }
    if (editedSkin) {
        __try { std::memcpy(editedSkin,savedSkin,editedCount*12*sizeof(float)); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
        editedSkin=nullptr;editedCount=0;
    }
    editedSnapshot=nullptr;
    editedScene=nullptr;weaponHandles[0]=weaponHandles[1]=0xffffffffu;
    activeRigPose=activeRigEpoch=0;activeRigDisplayTime=0;
    activeSelectedWeapon=0;
    activeTrackedHands=0;
}
}
