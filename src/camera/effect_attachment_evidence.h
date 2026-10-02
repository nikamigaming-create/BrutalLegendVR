#pragma once
#include <cstdint>
#include <cmath>
#include <cstring>
#include "tracked_basis.h"
#include "shadow_transform.h"

namespace BLVR {
inline bool EffectOwnerMatchesPlayer(uint32_t handle,uint32_t player,uint32_t axe,uint32_t guitar,bool inventoryValid) {
    if(handle==0xffffffffu)return false;
    return handle==player||(inventoryValid&&
        ((axe&&axe!=0xffffffffu&&handle==axe)||(guitar&&guitar!=0xffffffffu&&handle==guitar)));
}
// Native pool identity survives pointer reuse; an owner handle alone does not
// establish whether an existing particle remains attached to its emitter.
struct EffectSystemIdentity {
    uintptr_t pool=0,system=0;
    uint32_t index=0,generation=0;
};
inline bool EffectIdentityMatches(const EffectSystemIdentity& a,const EffectSystemIdentity& b) {
    return a.pool&&a.system&&a.pool==b.pool&&a.system==b.system&&
        a.index==b.index&&a.generation==b.generation;
}
struct EffectAttachmentEvidence {
    EffectSystemIdentity identity{};
    uint32_t parent=0xffffffffu,owner=0xffffffffu,rebound=0xffffffffu,serial=0;
    uint32_t flags=0,resourceId=0;
    uint8_t bone=0xff,alternateBone=0xff;
    float emitter[3]{},orientation[4]{},attachment[3]{},authoredOffset[3]{};
    uint64_t observedAt=0;
    uintptr_t sourceScene=0;
    uintptr_t event=0;
    uint32_t historyVertices=0;
    uint64_t sourceEpoch=0;
};
inline bool EffectEvidenceMatchesSystem(const EffectAttachmentEvidence& evidence,
    const EffectSystemIdentity& identity,uint32_t serial,const float emitter[3],uint64_t now) {
    if(!EffectIdentityMatches(evidence.identity,identity)||serial!=evidence.serial||
       now<evidence.observedAt||now-evidence.observedAt>250)return false;
    for(int k=0;k<3;++k)if(!std::isfinite(emitter[k])||emitter[k]!=evidence.emitter[k])return false;
    return true;
}
inline bool EffectEvidenceMatchesPacket(const EffectAttachmentEvidence& evidence,const float emitter[3]) {
    if(!evidence.sourceScene||!evidence.identity.system)return false;
    for(int k=0;k<3;++k)if(!std::isfinite(emitter[k])||emitter[k]!=evidence.emitter[k])return false;
    return true;
}
inline bool EffectAttachmentMatchesEmitter(const EffectAttachmentEvidence& source) {
    const float* q=source.orientation;
    const float norm=q[0]*q[0]+q[1]*q[1]+q[2]*q[2]+q[3]*q[3];
    if(!std::isfinite(norm)||std::fabs(norm-1.f)>.001f)return false;
    const float* v=source.authoredOffset;
    const float t[3]{2*(q[1]*v[2]-q[2]*v[1]),2*(q[2]*v[0]-q[0]*v[2]),2*(q[0]*v[1]-q[1]*v[0])};
    const float rotated[3]{v[0]+q[3]*t[0]+q[1]*t[2]-q[2]*t[1],
        v[1]+q[3]*t[1]+q[2]*t[0]-q[0]*t[2],v[2]+q[3]*t[2]+q[0]*t[1]-q[1]*t[0]};
    for(int k=0;k<3;++k)if(!std::isfinite(source.emitter[k])||!std::isfinite(source.attachment[k])||
        !std::isfinite(rotated[k])||std::fabs(source.attachment[k]+rotated[k]-source.emitter[k])>.001f)return false;
    return true;
}
inline bool EffectRibbonRootEligible(const EffectAttachmentEvidence& source,uint32_t weapon,
    uint32_t packetVertices,uint64_t now,uint64_t expectedEpoch=0) {
    return source.sourceScene&&source.sourceEpoch&&(!expectedEpoch||source.sourceEpoch==expectedEpoch)&&
        source.identity.system&&weapon!=0xffffffffu&&
        source.parent==weapon&&source.owner==weapon&&source.rebound==0xffffffffu&&
        source.bone==0xff&&source.alternateBone==0xff&&(source.flags&0x60u)==0x60u&&
        source.historyVertices>0&&source.historyVertices<=4096&&packetVertices==source.historyVertices+1&&
        now>=source.observedAt&&now-source.observedAt<=250&&EffectAttachmentMatchesEmitter(source);
}
// Transform only a newly appended emitter endpoint. The packet emitter and
// every earlier particle remain in their original world coordinate system.
inline bool EffectRibbonRootDelta(const float emitter[3],const float nativeWorld[16],
    const float trackedWorld[16],const float nativeDirection[3],float center[3],float direction[3]) {
    for(int i=0;i<16;++i)if(!std::isfinite(nativeWorld[i])||!std::isfinite(trackedWorld[i]))return false;
    const int affineIndices[]{3,7,11};
    for(int i:affineIndices)if(std::fabs(nativeWorld[i])>.00001f||std::fabs(trackedWorld[i])>.00001f)return false;
    if(std::fabs(nativeWorld[15]-1)>.00001f||std::fabs(trackedWorld[15]-1)>.00001f)return false;
    float inverse[16]{},trackedInverse[16]{},delta[16]{};
    if(!InvertCameraMatrix(nativeWorld,inverse)||!InvertCameraMatrix(trackedWorld,trackedInverse))return false;
    MultiplyCameraMatrices(inverse,trackedWorld,delta);
    float candidate[3]{},axis[3]{},lengthSq=0,shiftSq=0;
    for(int k=0;k<3;++k) {
        candidate[k]=emitter[0]*delta[k]+emitter[1]*delta[4+k]+emitter[2]*delta[8+k]+delta[12+k]-emitter[k];
        axis[k]=nativeDirection[0]*delta[k]+nativeDirection[1]*delta[4+k]+nativeDirection[2]*delta[8+k];
        if(!std::isfinite(candidate[k])||!std::isfinite(axis[k]))return false;
        shiftSq+=candidate[k]*candidate[k];lengthSq+=axis[k]*axis[k];
    }
    // A tracked weapon can reach only the nearby body volume. Far world shots
    // cannot be silently pulled back to the hands by an ownership mistake.
    if(!std::isfinite(shiftSq)||shiftSq>16||!std::isfinite(lengthSq)||lengthSq<.000001f)return false;
    const float scale=1/std::sqrt(lengthSq);
    for(int k=0;k<3;++k) {center[k]=candidate[k];direction[k]=axis[k]*scale;}
    return true;
}
struct EffectRibbonRootBackup {
    uint8_t* vertices=nullptr;
    uint32_t first=0,count=0;
    float center[4][3]{},direction[4][3]{};
};
inline bool EffectApplyRibbonRoot(uint8_t* vertices,uint32_t allocatedVertices,uint32_t historyVertices,
    const float center[3],const float direction[3],EffectRibbonRootBackup& backup) {
    if(!vertices||!historyVertices||historyVertices>4096||allocatedVertices<=historyVertices||
       allocatedVertices-historyVertices>4)return false;
    const auto* endpoint=vertices+historyVertices*64;
    float originalCenter[4]{};std::memcpy(originalCenter,endpoint,16);
    for(float v:originalCenter)if(v!=0)return false;
    for(int k=0;k<3;++k)if(!std::isfinite(center[k])||!std::isfinite(direction[k]))return false;
    // Native padding consists exclusively of byte-identical copies of the
    // appended endpoint; reject any layout that would include past particles.
    for(uint32_t i=historyVertices+1;i<allocatedVertices;++i)
        if(std::memcmp(endpoint,vertices+i*64,64))return false;
    EffectRibbonRootBackup candidate{};candidate.vertices=vertices;candidate.first=historyVertices;
    candidate.count=allocatedVertices-historyVertices;
    for(uint32_t i=0;i<candidate.count;++i) {
        auto* root=vertices+(historyVertices+i)*64;
        std::memcpy(candidate.center[i],root,12);std::memcpy(candidate.direction[i],root+0x30,12);
    }
    backup=candidate;
    for(uint32_t i=0;i<candidate.count;++i) {
        auto* root=vertices+(historyVertices+i)*64;
        std::memcpy(root,center,12);std::memcpy(root+0x30,direction,12);
    }
    return true;
}
inline void EffectRestoreRibbonRoot(EffectRibbonRootBackup& backup) {
    if(!backup.vertices)return;
    for(uint32_t i=0;i<backup.count;++i) {
        auto* root=backup.vertices+(backup.first+i)*64;
        std::memcpy(root,backup.center[i],12);std::memcpy(root+0x30,backup.direction[i],12);
    }
    backup={};
}
}
