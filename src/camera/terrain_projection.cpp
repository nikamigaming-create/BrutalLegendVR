#include "terrain_projection.h"
#include "camera_hook.h"
#include "../diagnostics/log.h"
#include "../diagnostics/frame_profiler.h"
#include <windows.h>
#include <MinHook.h>
#include <cmath>
#include <unordered_map>

namespace BLVR {
namespace {
using Build = bool(__stdcall*)(void*,void*,float,void*);
using Copy = void*(__stdcall*)(void*,void*);
using Prepare = bool(__thiscall*)(void*,void*,void*,void*);
Build originalBuild=nullptr;
Copy originalCopy=nullptr;
Prepare originalPrepare=nullptr;
void* targets[3]{};
bool enabled=true;
struct Origin {
    float eye[3];
    uintptr_t materials[4];
    unsigned layers;
    uint64_t touched;
};
SRWLOCK originsLock=SRWLOCK_INIT;
std::unordered_map<void*,Origin> origins;
unsigned buildLogs=0,prepareLogs=0,missLogs=0;

void PruneOrigins(uint64_t now) {
    // Pointer reuse is replaced by every build/copy; retire freed scene buffers
    // after loading. Live render packets refresh their age on every use.
    // Collection calls this for every terrain packet. A full scan per packet
    // became quadratic once the cache crossed 16K entries during travel.
    // Retention is unchanged; only garbage collection is amortized.
    static uint64_t lastPrune=0;
    if(origins.size()<16384||now-lastPrune<1000)return;
    lastPrune=now;
    blvr_perf::Scope pruneCost(blvr_perf::TerrainPrune);
    const size_t before=origins.size();
    for(auto it=origins.begin();it!=origins.end();) {
        if(now-it->second.touched>30000)it=origins.erase(it);
        else ++it;
    }
    static uint64_t lastLog=0;
    if(now-lastLog>=10000) {
        lastLog=now;Log("Terrain cache: entries=%zu retired=%zu pruneIntervalMs=1000",origins.size(),before-origins.size());
    }
}

bool __stdcall HookBuild(void* output,void* scene,float distance,void* node) {
    blvr_perf::Scope buildCost(blvr_perf::TerrainBuild);
    const bool okay=originalBuild(output,scene,distance,node);
    Origin origin{};
    if(okay) {
        // VA 0x73B1C4..0x73B26F: collection reads scene +0x810, the
        // translation-only camera-to-world matrix, before building all layers.
        std::memcpy(origin.eye,static_cast<uint8_t*>(scene)+0x840,sizeof(origin.eye));
        auto* packet=static_cast<uint8_t*>(output);
        origin.layers=packet[0x1d4];
        std::memcpy(origin.materials,packet+0x68,sizeof(origin.materials));
        origin.touched=GetTickCount64();
    }
    const bool valid=okay&&origin.layers<=4&&std::isfinite(origin.eye[0])&&
        std::isfinite(origin.eye[1])&&std::isfinite(origin.eye[2]);
    AcquireSRWLockExclusive(&originsLock);
    if(valid) {PruneOrigins(origin.touched);origins[output]=origin;}
    else origins.erase(output);
    ReleaseSRWLockExclusive(&originsLock);
    if(valid&&buildLogs++<2)Log("Terrain source: packet=%p scene=%p layers=%u origin=(%.3f,%.3f,%.3f)",
        output,scene,origin.layers,origin.eye[0],origin.eye[1],origin.eye[2]);
    return okay;
}

void* __stdcall HookCopy(void* output,void* source) {
    void* result=originalCopy(output,source);
    AcquireSRWLockExclusive(&originsLock);
    const auto it=origins.find(source);
    if(it!=origins.end()) {
        Origin origin=it->second;origin.touched=GetTickCount64();origins[output]=origin;
    } else origins.erase(output);
    ReleaseSRWLockExclusive(&originsLock);
    return result;
}

bool __fastcall HookPrepare(void* snapshot,void*,void* renderer,void* second,void* third) {
    if(!enabled||!CameraHook_IsStereoRender())return originalPrepare(snapshot,renderer,second,third);
    auto* packet=static_cast<uint8_t*>(snapshot);
    Origin origin{};bool found=false;
    AcquireSRWLockExclusive(&originsLock);
    const auto it=origins.find(snapshot);
    if(it!=origins.end()) {
        it->second.touched=GetTickCount64();origin=it->second;found=true;
    }
    ReleaseSRWLockExclusive(&originsLock);
    if(!found||origin.layers!=packet[0x1d4]||
       std::memcmp(origin.materials,packet+0x68,sizeof(origin.materials))!=0) {
        if(missLogs++<3)Log("Terrain origin unavailable: packet=%p layers=%u",snapshot,unsigned(packet[0x1d4]));
        return originalPrepare(snapshot,renderer,second,third);
    }
    // Retail already uses this current renderer origin for ModelTranslate,
    // Blend, AlbedoFront and AlbedoSide. Only the four baked albedo matrices
    // need rebasing; touching the other matrices would apply it twice.
    const auto* eye=reinterpret_cast<const float*>(static_cast<uint8_t*>(renderer)+0x140);
    if(!std::isfinite(eye[0])||!std::isfinite(eye[1])||!std::isfinite(eye[2]))
        return originalPrepare(snapshot,renderer,second,third);
    float saved[4][16];
    auto* matrices=reinterpret_cast<float*>(packet+0x80);
    std::memcpy(saved,matrices,sizeof(saved));
    for(unsigned layer=0;layer<origin.layers;++layer)
        RebaseTerrainProjection(origin.eye,eye,saved[layer],matrices+layer*16);
    if(prepareLogs++<4)Log("Terrain eye rebase: packet=%p layers=%u source=(%.3f,%.3f,%.3f) eye=(%.3f,%.3f,%.3f)",
        snapshot,origin.layers,origin.eye[0],origin.eye[1],origin.eye[2],eye[0],eye[1],eye[2]);
    const bool result=originalPrepare(snapshot,renderer,second,third);
    // Copied render packets can be reused by another camera/pass. Never
    // accumulate an eye correction or mutate the authored terrain material.
    std::memcpy(matrices,saved,sizeof(saved));
    return result;
}
}

bool TerrainProjection_Init() {
    static_assert(sizeof(void*)==4,"Retail terrain adapter is x86");
    const auto base=reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    targets[0]=reinterpret_cast<void*>(base+0x33aea0);
    targets[1]=reinterpret_cast<void*>(base+0x33bbb0);
    targets[2]=reinterpret_cast<void*>(base+0x33bdc0);
    const unsigned char buildStart[]={0x55,0x8b,0xec,0x83,0xe4,0xf8,0x81,0xec,0x64,0x02,0,0};
    const unsigned char copyStart[]={0x55,0x8b,0xec,0x53,0x8b,0x5d,0x08,0x56,0x8b,0x75,0x0c};
    const unsigned char prepareStart[]={0x55,0x8b,0xec,0x56,0x8b,0x75,0x08,0x57,0x8b,0xf9};
    if(std::memcmp(targets[0],buildStart,sizeof(buildStart))||
       std::memcmp(targets[1],copyStart,sizeof(copyStart))||
       std::memcmp(targets[2],prepareStart,sizeof(prepareStart))) {
        Log("Terrain projection hooks: unsupported retail prologue");return false;
    }
    char option[8]{};GetEnvironmentVariableA("BLVR_TERRAIN_REBASE",option,sizeof(option));
    enabled=option[0]!='0';
    void* hooks[]={reinterpret_cast<void*>(&HookBuild),reinterpret_cast<void*>(&HookCopy),reinterpret_cast<void*>(&HookPrepare)};
    void** originals[]={reinterpret_cast<void**>(&originalBuild),reinterpret_cast<void**>(&originalCopy),reinterpret_cast<void**>(&originalPrepare)};
    for(int i=0;i<3;++i) {
        const auto status=MH_CreateHook(targets[i],hooks[i],originals[i]);
        if(status!=MH_OK) {
            for(int j=0;j<i;++j)MH_RemoveHook(targets[j]);
            Log("Terrain projection hook creation failed: index=%d status=%d",i,int(status));return false;
        }
    }
    for(int i=0;i<3;++i)if(MH_EnableHook(targets[i])!=MH_OK) {
        for(void* target:targets) {MH_DisableHook(target);MH_RemoveHook(target);}
        Log("Terrain projection hook enable failed: index=%d",i);return false;
    }
    Log("Terrain projection hooks: source/copy/eye ready correction=%d",int(enabled));return true;
}

void TerrainProjection_Shutdown() {
    for(void* target:targets)if(target)MH_DisableHook(target);
}
}
