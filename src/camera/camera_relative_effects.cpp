#include "camera_relative_effects.h"
#include "camera_hook.h"
#include "effect_attachment_evidence.h"
#include "player_view_rig.h"
#include "../diagnostics/log.h"
#include "../openxr/xr_host.h"
#include <windows.h>
#include <MinHook.h>
#include <cmath>
#include <cstring>

namespace BLVR { namespace {
using Prepare=uintptr_t(__thiscall*)(void*,void*,void*,void*);
Prepare originalPrepare=nullptr;
void* target=nullptr;
// Opt-in, read-only lineage observation. This never relocates an emitter or
// changes packed particle buffers; attachment/lifetime policy remains native.
using EventUpdate=uintptr_t(__thiscall*)(void*);
using EventTransform=uintptr_t(__thiscall*)(void*,void*);
using Build=uintptr_t(__thiscall*)(void*,void*,void*,void*,void*);
using Copy=uintptr_t(__thiscall*)(void*,void*);
EventUpdate originalEvent=nullptr;
EventTransform originalTransform=nullptr;
Build originalBuild=nullptr;
Copy originalCopy=nullptr;
void* diagnosticTargets[4]{};
bool attachmentDiagnostic=false;
bool trackedRibbonRoot=false;
uintptr_t executableBase=0,diagnosticPlayer=0;
uint64_t nextPlayerScan=0;
volatile LONG activationCalls=0,transformCalls=0,identityRejects=0,ownerRejects=0,evidenceExceptions=0,buildCalls=0,copyCalls=0,prepareCalls=0;
SRWLOCK evidenceLock=SRWLOCK_INIT;
EffectAttachmentEvidence systems[64]{};
struct PacketEvidence {uintptr_t packet=0;EffectAttachmentEvidence source{};bool copied=false;};
PacketEvidence packets[128]{};
unsigned nextSystem=0,nextPacket=0;
bool FirstIdentity(const EffectSystemIdentity& identity,EffectSystemIdentity (&seen)[8]) {
    bool first=false;AcquireSRWLockExclusive(&evidenceLock);
    for(const auto& old:seen)if(EffectIdentityMatches(old,identity)) {ReleaseSRWLockExclusive(&evidenceLock);return false;}
    for(auto& old:seen)if(!old.pool) {old=identity;first=true;break;}
    ReleaseSRWLockExclusive(&evidenceLock);return first;
}
template<class T>T Field(const void* p,unsigned offset) {return *reinterpret_cast<const T*>(static_cast<const uint8_t*>(p)+offset);}
bool Eddie(uintptr_t actor) {
    if(!actor)return false;
    const auto type=Field<const uint8_t*>(reinterpret_cast<void*>(actor),4);
    const auto definition=type?Field<const uint8_t*>(type,4):nullptr;
    const auto name=definition?Field<const char*>(definition,0):nullptr;
    return name&&std::strncmp(name,"Player_A",9)==0;
}
bool SafeEddie(uintptr_t actor) {
    __try {return Eddie(actor);} __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool ResolvePlayer(uintptr_t table,uint32_t capacity) {
    if(diagnosticPlayer) {
        const auto handle=Field<uint32_t>(reinterpret_cast<void*>(diagnosticPlayer),0x14);
        if(handle<capacity&&*reinterpret_cast<const uintptr_t*>(table+handle*12)==diagnosticPlayer&&SafeEddie(diagnosticPlayer))return true;
        diagnosticPlayer=0;
    }
    const auto now=GetTickCount64();if(now<nextPlayerScan)return false;nextPlayerScan=now+250;
    // Weapon-owned systems need an independently verified player. Looking
    // only at the supplied handle cannot discover Eddie from an axe handle.
    uintptr_t found=0;
    const auto limit=capacity<4096?capacity:4096;
    for(uint32_t handle=0;handle<limit;++handle) {
        const auto actor=*reinterpret_cast<const uintptr_t*>(table+handle*12);
        if(!actor||!SafeEddie(actor)||Field<uint32_t>(reinterpret_cast<void*>(actor),0x14)!=handle)continue;
        if(found&&found!=actor)return false;
        found=actor;
    }
    diagnosticPlayer=found;return found!=0;
}
bool OwnedHandle(uint32_t handle) {
    const auto capacity=*reinterpret_cast<const uint32_t*>(executableBase+0xb79d90);
    const auto table=*reinterpret_cast<const uintptr_t*>(executableBase+0xb79d8c);
    if(!table||capacity>0x100000)return false;
    if(handle<capacity) {
        const auto actor=*reinterpret_cast<const uintptr_t*>(table+handle*12);
        if(SafeEddie(actor)&&Field<uint32_t>(reinterpret_cast<void*>(actor),0x14)==handle)diagnosticPlayer=actor;
    }
    if(!ResolvePlayer(table,capacity))return false;
    const auto* actor=reinterpret_cast<const uint8_t*>(diagnosticPlayer);
    const auto inventory=Field<const uint8_t*>(actor,0x64);
    const bool validInventory=inventory&&Field<uintptr_t>(inventory,0x10)==diagnosticPlayer;
    return EffectOwnerMatchesPlayer(handle,Field<uint32_t>(actor,0x14),validInventory?Field<uint32_t>(inventory,0x20):0,
        validInventory?Field<uint32_t>(inventory,0x38):0,validInventory);
}
bool Identity(void* event,EffectSystemIdentity& key) {
    key.pool=Field<uintptr_t>(event,0x80);key.index=Field<uint32_t>(event,0x84);key.generation=Field<uint32_t>(event,0x88);
    if(!key.pool)return false;
    const auto* pool=reinterpret_cast<const uint8_t*>(key.pool);
    const auto count=Field<uint32_t>(pool,0)>>6;
    const auto entries=Field<uintptr_t>(pool,8);
    if(!entries||count>4096||key.index>=count)return false;
    const auto* slot=reinterpret_cast<const uint8_t*>(entries+key.index*8);
    if((Field<uint32_t>(slot,4)&0x7fffffffu)!=key.generation)return false;
    key.system=Field<uintptr_t>(slot,0);return key.system!=0;
}
bool LiveIdentity(const EffectSystemIdentity& identity) {
    __try {
        const auto* pool=reinterpret_cast<const uint8_t*>(identity.pool);
        if(!pool||!identity.system)return false;
        const auto count=Field<uint32_t>(pool,0)>>6;const auto entries=Field<uintptr_t>(pool,8);
        if(!entries||count>4096||identity.index>=count)return false;
        const auto* slot=reinterpret_cast<const uint8_t*>(entries+identity.index*8);
        return Field<uintptr_t>(slot,0)==identity.system&&(Field<uint32_t>(slot,4)&0x7fffffffu)==identity.generation;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
void ObserveEvent(void* event) {
    __try {
        EffectAttachmentEvidence sample{};
        if(!Identity(event,sample.identity)) {
            if(InterlockedIncrement(&identityRejects)<=2)Log("Effect attachment filter: invalid identity event=%p pool=%p index=%u generation=%u",event,reinterpret_cast<void*>(sample.identity.pool),sample.identity.index,sample.identity.generation);
            return;
        }
        const auto* system=reinterpret_cast<const uint8_t*>(sample.identity.system);
        sample.event=reinterpret_cast<uintptr_t>(event);sample.historyVertices=Field<uint16_t>(system,0x12);
        sample.parent=Field<uint32_t>(event,0x38);sample.owner=Field<uint32_t>(system,0x1c0);sample.rebound=Field<uint32_t>(system,0x250);
        if(!OwnedHandle(sample.parent)&&!OwnedHandle(sample.owner)&&!OwnedHandle(sample.rebound)) {
            if(InterlockedIncrement(&ownerRejects)<=2)Log("Effect attachment filter: unrelated owner event=%p parent=%u owner=%u rebound=%u player=%p",event,sample.parent,sample.owner,sample.rebound,reinterpret_cast<void*>(diagnosticPlayer));
            return;
        }
        sample.bone=Field<uint8_t>(event,0x50);sample.alternateBone=Field<uint8_t>(event,0x51);sample.serial=Field<uint32_t>(system,0x8c);
        sample.flags=Field<uint32_t>(system,0xc);sample.resourceId=Field<uint32_t>(system,0);
        std::memcpy(sample.emitter,system+0xd4,sizeof(sample.emitter));std::memcpy(sample.orientation,system+0xec,sizeof(sample.orientation));
        std::memcpy(sample.attachment,static_cast<uint8_t*>(event)+0x10,sizeof(sample.attachment));std::memcpy(sample.authoredOffset,system+0xfc,sizeof(sample.authoredOffset));
        for(float value:sample.emitter)if(!std::isfinite(value))return;
        for(float value:sample.orientation)if(!std::isfinite(value))return;
        for(float value:sample.attachment)if(!std::isfinite(value))return;
        for(float value:sample.authoredOffset)if(!std::isfinite(value))return;
        sample.observedAt=GetTickCount64();
        AcquireSRWLockExclusive(&evidenceLock);
        bool updated=false;
        for(auto& old:systems)if(EffectIdentityMatches(old.identity,sample.identity)) {old=sample;updated=true;break;}
        if(!updated)systems[nextSystem++%64]=sample;
        ReleaseSRWLockExclusive(&evidenceLock);
        static EffectSystemIdentity printed[8]{};
        if(FirstIdentity(sample.identity,printed))Log("Effect attachment event: system=%p pool=%p index=%u generation=%u parent=%u owner=%u rebound=%u bone=%u alternate=%u serial=%u flags=%08X resource=%08X emitter=(%.3f,%.3f,%.3f) attachment=(%.3f,%.3f,%.3f) orientation=(%.3f,%.3f,%.3f,%.3f) offset=(%.3f,%.3f,%.3f)",
            reinterpret_cast<void*>(sample.identity.system),reinterpret_cast<void*>(sample.identity.pool),sample.identity.index,sample.identity.generation,
            sample.parent,sample.owner,sample.rebound,sample.bone,sample.alternateBone,sample.serial,sample.flags,sample.resourceId,sample.emitter[0],sample.emitter[1],sample.emitter[2],
            sample.attachment[0],sample.attachment[1],sample.attachment[2],sample.orientation[0],sample.orientation[1],sample.orientation[2],sample.orientation[3],sample.authoredOffset[0],sample.authoredOffset[1],sample.authoredOffset[2]);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        if(InterlockedIncrement(&evidenceExceptions)<=2)Log("Effect attachment filter: unreadable event=%p exception=%08X",event,GetExceptionCode());
    }
}
uintptr_t __fastcall HookEvent(void* event,void*) {
    if(InterlockedIncrement(&activationCalls)==1)Log("Effect attachment coverage: activation reached event=%p",event);
    const auto result=originalEvent(event);ObserveEvent(event);return result;
}
uintptr_t __fastcall HookTransform(void* event,void*,void* transform) {
    if(InterlockedIncrement(&transformCalls)==1)Log("Effect attachment coverage: transform reached event=%p",event);
    const auto result=originalTransform(event,transform);ObserveEvent(event);return result;
}
bool FindSystem(void* system,EffectAttachmentEvidence& sample) {
    __try {
        const auto serial=Field<uint32_t>(system,0x8c);float emitter[3];std::memcpy(emitter,static_cast<uint8_t*>(system)+0xd4,sizeof(emitter));
        bool found=false;const auto now=GetTickCount64();
        AcquireSRWLockShared(&evidenceLock);
        for(const auto& source:systems)if(source.identity.system==reinterpret_cast<uintptr_t>(system)&&
            (trackedRibbonRoot||EffectEvidenceMatchesSystem(source,source.identity,serial,emitter,now))) {sample=source;found=true;}
        ReleaseSRWLockShared(&evidenceLock);
        if(!found)return false;
        if(!LiveIdentity(sample.identity))return false;
        if(!trackedRibbonRoot)return true;
        // The proven builder still has its exact live system. Re-read the
        // retained native event only when its complete pool key, binding and
        // setter equation agree. A freed/reused event or world-shot rebind
        // cannot confer a new emitter attachment merely through its pointer.
        EffectSystemIdentity eventKey{};
        auto* event=reinterpret_cast<void*>(sample.event);
        if(!event||!Identity(event,eventKey)||!EffectIdentityMatches(eventKey,sample.identity)||
           Field<uint32_t>(event,0x38)!=sample.parent||Field<uint8_t>(event,0x50)!=sample.bone||
           Field<uint8_t>(event,0x51)!=sample.alternateBone||Field<uint32_t>(system,0x1c0)!=sample.owner||
           Field<uint32_t>(system,0x250)!=sample.rebound||Field<uint32_t>(system,0xc)!=sample.flags||
           Field<uint32_t>(system,0)!=sample.resourceId)return false;
        sample.serial=serial;sample.historyVertices=Field<uint16_t>(system,0x12);
        std::memcpy(sample.emitter,emitter,12);std::memcpy(sample.orientation,static_cast<uint8_t*>(system)+0xec,16);
        std::memcpy(sample.attachment,static_cast<uint8_t*>(event)+0x10,12);
        std::memcpy(sample.authoredOffset,static_cast<uint8_t*>(system)+0xfc,12);
        sample.observedAt=now;
        if(!EffectAttachmentMatchesEmitter(sample)||!LiveIdentity(sample.identity)||Field<uint32_t>(system,0x8c)!=serial||
           std::memcmp(emitter,static_cast<uint8_t*>(system)+0xd4,12))return false;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
void SavePacket(void* packet,const EffectAttachmentEvidence& sample,bool copied) {
    __try {
        if(!packet||!EffectEvidenceMatchesPacket(sample,reinterpret_cast<const float*>(static_cast<uint8_t*>(packet)+0xc8)))return;
        AcquireSRWLockExclusive(&evidenceLock);packets[nextPacket++%128]={reinterpret_cast<uintptr_t>(packet),sample,copied};ReleaseSRWLockExclusive(&evidenceLock);
        static EffectSystemIdentity printed[8]{};
        if(FirstIdentity(sample.identity,printed))Log("Effect attachment packet: packet=%p scene=%p system=%p generation=%u bone=%u copied=%u",packet,reinterpret_cast<void*>(sample.sourceScene),reinterpret_cast<void*>(sample.identity.system),sample.identity.generation,sample.bone,copied);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}
void InvalidatePacket(void* packet) {
    AcquireSRWLockExclusive(&evidenceLock);
    for(auto& source:packets)if(source.packet==reinterpret_cast<uintptr_t>(packet))source={};
    ReleaseSRWLockExclusive(&evidenceLock);
}
bool FindPacket(void* packet,PacketEvidence& evidence) {
    if(!packet)return false;
    bool found=false;AcquireSRWLockShared(&evidenceLock);
    for(const auto& source:packets)if(source.packet==reinterpret_cast<uintptr_t>(packet)) {evidence=source;found=true;break;}
    ReleaseSRWLockShared(&evidenceLock);return found&&LiveIdentity(evidence.source.identity);
}
uintptr_t __fastcall HookBuild(void* packet,void*,void* scene,void* eye,void* flags,void* allocator) {
    if(InterlockedIncrement(&buildCalls)==1)Log("Effect attachment coverage: builder reached packet=%p scene=%p",packet,scene);
    EffectAttachmentEvidence sample{};bool observed=false;
    __try {observed=FindSystem(Field<void*>(packet,0x64),sample);} __except(EXCEPTION_EXECUTE_HANDLER) {}
    InvalidatePacket(packet);
    const auto result=originalBuild(packet,scene,eye,flags,allocator);
    if(observed) {
        sample.sourceScene=reinterpret_cast<uintptr_t>(scene);
        sample.sourceEpoch=XrHost::Get().GetRenderTrackingEpoch();
        SavePacket(packet,sample,false);
    }
    return result;
}
uintptr_t __fastcall HookCopy(void* packet,void*,void* allocator) {
    if(InterlockedIncrement(&copyCalls)==1)Log("Effect attachment coverage: copy reached packet=%p",packet);
    PacketEvidence source{};const bool observed=FindPacket(packet,source);
    const auto result=originalCopy(packet,allocator);
    if(result)InvalidatePacket(reinterpret_cast<void*>(result));
    if(observed)SavePacket(reinterpret_cast<void*>(result),source.source,true);
    return result;
}
void ObservePrepare(void* packet,void* scene) {
    __try {
        if(InterlockedIncrement(&prepareCalls)==1)Log("Effect attachment coverage: stereo prepare reached packet=%p activation=%ld transform=%ld identityReject=%ld ownerReject=%ld build=%ld copy=%ld",packet,activationCalls,transformCalls,identityRejects,ownerRejects,buildCalls,copyCalls);
        PacketEvidence sample{};
        if(!FindPacket(packet,sample)||!EffectEvidenceMatchesPacket(sample.source,reinterpret_cast<const float*>(static_cast<uint8_t*>(packet)+0xc8)))return;
        static EffectSystemIdentity printed[8]{};
        if(!FirstIdentity(sample.source.identity,printed))return;
        uint64_t pose=0;int64_t time=0;XrHost::Get().GetRenderTrackingStamp(pose,time);
        Log("Effect attachment render: packet=%p collectedScene=%p renderScene=%p system=%p generation=%u bone=%u copied=%u source=%llu pose=%llu epoch=%llu displayTime=%lld",
            packet,reinterpret_cast<void*>(sample.source.sourceScene),scene,reinterpret_cast<void*>(sample.source.identity.system),sample.source.identity.generation,sample.source.bone,sample.copied,
            static_cast<unsigned long long>(CameraHook_GetStereoSourceFrame()),static_cast<unsigned long long>(pose),static_cast<unsigned long long>(XrHost::Get().GetRenderTrackingEpoch()),static_cast<long long>(time));
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}
void InitializeAttachmentDiagnostic(uintptr_t base) {
    char enabled[8]{};
    GetEnvironmentVariableA("BLVR_EFFECT_ATTACHMENT_DIAGNOSTIC",enabled,sizeof(enabled));
    const bool diagnosticRequested=std::strcmp(enabled,"1")==0;
    char rootEnabled[8]{};GetEnvironmentVariableA("BLVR_TRACKED_EFFECT_ROOT",rootEnabled,sizeof(rootEnabled));
    const bool rootRequested=std::strcmp(rootEnabled,"1")==0;
    if(!diagnosticRequested&&!rootRequested)return;
    executableBase=base;
    diagnosticTargets[0]=reinterpret_cast<void*>(base+0x134b50);
    diagnosticTargets[1]=reinterpret_cast<void*>(base+0x337ce0);
    diagnosticTargets[2]=reinterpret_cast<void*>(base+0x3379e0);
    diagnosticTargets[3]=reinterpret_cast<void*>(base+0x134c90);
    const uint8_t expectedEvent[]={0x8b,0x81,0x80,0,0,0,0x56,0x85,0xc0};
    const uint8_t expectedBuild[]={0x55,0x8b,0xec,0x83,0xec,0x10,0x53,0x8b,0xd9};
    const uint8_t expectedCopy[]={0x55,0x8b,0xec,0x83,0xec,0x08,0x53,0x8b,0x5d,0x08};
    const uint8_t expectedTransform[]={0x55,0x8b,0xec,0x8b,0x81,0x80,0,0,0,0x85,0xc0};
    if(std::memcmp(diagnosticTargets[0],expectedEvent,sizeof(expectedEvent))||
       std::memcmp(diagnosticTargets[1],expectedBuild,sizeof(expectedBuild))||
       std::memcmp(diagnosticTargets[2],expectedCopy,sizeof(expectedCopy))||
       std::memcmp(diagnosticTargets[3],expectedTransform,sizeof(expectedTransform))) {
        Log("Effect attachment diagnostic: unsupported retail sites; disabled");return;
    }
    void* hooks[]={reinterpret_cast<void*>(&HookEvent),reinterpret_cast<void*>(&HookBuild),reinterpret_cast<void*>(&HookCopy),reinterpret_cast<void*>(&HookTransform)};
    void** originals[]={reinterpret_cast<void**>(&originalEvent),reinterpret_cast<void**>(&originalBuild),reinterpret_cast<void**>(&originalCopy),reinterpret_cast<void**>(&originalTransform)};
    for(unsigned i=0;i<4;++i) {
        const auto created=MH_CreateHook(diagnosticTargets[i],hooks[i],originals[i]);
        const auto active=created==MH_OK?MH_EnableHook(diagnosticTargets[i]):created;
        if(created!=MH_OK||active!=MH_OK) {
            for(void* site:diagnosticTargets)MH_DisableHook(site);
            Log("Effect attachment diagnostic: hook %u failed created=%d enabled=%d; disabled",i,int(created),int(active));return;
        }
    }
    attachmentDiagnostic=true;
    trackedRibbonRoot=rootRequested;
    Log("Effect attachment diagnostic: enabled, capped event/packet/render lineage; tracked appended ribbon root=%d",trackedRibbonRoot);
}
bool ApplyTrackedRibbonRoot(void* snapshot,void* scene,EffectRibbonRootBackup& backup) {
    if(!trackedRibbonRoot)return false;
    __try {
        PacketEvidence packet{};
        if(!FindPacket(snapshot,packet)||!EffectEvidenceMatchesPacket(packet.source,
            reinterpret_cast<const float*>(static_cast<uint8_t*>(snapshot)+0xc8)))return false;
        uint64_t pose=0;int64_t displayTime=0;
        if(!XrHost::Get().GetRenderTrackingStamp(pose,displayTime)||!CameraHook_GetStereoSourceFrame())return false;
        const auto epoch=XrHost::Get().GetRenderTrackingEpoch();
        TrackedWeaponAttachment weapon{};
        if(!PlayerViewRig_ReadWeaponAttachment(packet.source.parent,scene,pose,displayTime,epoch,weapon))return false;
        const auto count=Field<uint16_t>(snapshot,0x70);
        if(Field<uint8_t>(snapshot,0x72)!=1||!EffectRibbonRootEligible(packet.source,weapon.handle,count,GetTickCount64(),epoch))return false;
        auto* vertices=Field<uint8_t*>(snapshot,0xd8);
        if(!vertices)return false;
        const auto* endpoint=vertices+packet.source.historyVertices*64;
        float nativeDirection[3]{};std::memcpy(nativeDirection,endpoint+0x30,12);
        float center[3]{},direction[3]{};
        if(!EffectRibbonRootDelta(packet.source.emitter,weapon.nativeWorld,weapon.trackedWorld,nativeDirection,center,direction)||
           !EffectApplyRibbonRoot(vertices,(static_cast<uint32_t>(count)+3u)&~3u,packet.source.historyVertices,center,direction,backup))return false;
        static unsigned applied=0;
        if(++applied<=4)Log("Tracked ribbon root: source=%llu pose=%llu epoch=%llu owner=%u historicalVertices=%u endpointCopies=%u delta=(%.3f,%.3f,%.3f)",
            static_cast<unsigned long long>(CameraHook_GetStereoSourceFrame()),static_cast<unsigned long long>(pose),
            static_cast<unsigned long long>(epoch),weapon.handle,packet.source.historyVertices,backup.count,center[0],center[1],center[2]);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        __try {EffectRestoreRibbonRoot(backup);} __except(EXCEPTION_EXECUTE_HANDLER) {backup={};}
        return false;
    }
}
uintptr_t __fastcall HookPrepare(void* snapshot,void*,void* renderer,void* scene,void* flags) {
    if(!CameraHook_IsStereoRender()||!snapshot||!renderer)
        return originalPrepare(snapshot,renderer,scene,flags);
    if(attachmentDiagnostic)ObservePrepare(snapshot,scene);
    auto* packet=static_cast<uint8_t*>(snapshot);
    const auto* eye=reinterpret_cast<const float*>(static_cast<uint8_t*>(renderer)+0x140);
    const auto* world=reinterpret_cast<const float*>(packet+0xc8);
    auto* translation=reinterpret_cast<float*>(packet+0xb8);
    for(int k=0;k<3;++k)if(!std::isfinite(eye[k])||!std::isfinite(world[k]))
        return originalPrepare(snapshot,renderer,scene,flags);
    float saved[3];std::memcpy(saved,translation,sizeof(saved));
    EffectTranslationForEye(world,eye,translation);
    EffectRibbonRootBackup root{};ApplyTrackedRibbonRoot(snapshot,scene,root);
    static unsigned count=0;
    if(++count<=4)Log("Particle eye origin: packet=%p world=(%.3f,%.3f,%.3f) eye=(%.3f,%.3f,%.3f) relative=(%.3f,%.3f,%.3f)",
        snapshot,world[0],world[1],world[2],eye[0],eye[1],eye[2],translation[0],translation[1],translation[2]);
    const uintptr_t result=originalPrepare(snapshot,renderer,scene,flags);
    // The native helper copies constants. A second eye or auxiliary camera
    // must always see the original packet, without accumulated corrections.
    std::memcpy(translation,saved,sizeof(saved));
    EffectRestoreRibbonRoot(root);
    return result;
}
}
bool CameraRelativeEffects_Init() {
    const auto base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    target=reinterpret_cast<void*>(base+0x337fd0);
    const uint8_t expected[]{0x55,0x8b,0xec,0x66,0x83,0x79,0x70,0x00};
    if(std::memcmp(target,expected,sizeof(expected))) {
        Log("Particle origin hook: unsupported retail prologue");return false;
    }
    const auto created=MH_CreateHook(target,reinterpret_cast<void*>(&HookPrepare),reinterpret_cast<void**>(&originalPrepare));
    const auto enabled=created==MH_OK?MH_EnableHook(target):created;
    Log("Particle origin hook: created=%d enabled=%d",int(created),int(enabled));
    if(created==MH_OK&&enabled==MH_OK)InitializeAttachmentDiagnostic(base);
    return created==MH_OK&&enabled==MH_OK;
}
void CameraRelativeEffects_Shutdown() {
    attachmentDiagnostic=false;
    trackedRibbonRoot=false;
    for(void* site:diagnosticTargets)if(site)MH_DisableHook(site);
    if(target)MH_DisableHook(target);
}
}
