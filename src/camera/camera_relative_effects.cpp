#include "camera_relative_effects.h"
#include "camera_hook.h"
#include "../diagnostics/log.h"
#include <windows.h>
#include <MinHook.h>
#include <cmath>
#include <cstring>

namespace BLVR { namespace {
using Prepare=uintptr_t(__thiscall*)(void*,void*,void*,void*);
Prepare originalPrepare=nullptr;
void* target=nullptr;
uintptr_t __fastcall HookPrepare(void* snapshot,void*,void* renderer,void* scene,void* flags) {
    if(!CameraHook_IsStereoRender()||!snapshot||!renderer)
        return originalPrepare(snapshot,renderer,scene,flags);
    auto* packet=static_cast<uint8_t*>(snapshot);
    const auto* eye=reinterpret_cast<const float*>(static_cast<uint8_t*>(renderer)+0x140);
    const auto* world=reinterpret_cast<const float*>(packet+0xc8);
    auto* translation=reinterpret_cast<float*>(packet+0xb8);
    for(int k=0;k<3;++k)if(!std::isfinite(eye[k])||!std::isfinite(world[k]))
        return originalPrepare(snapshot,renderer,scene,flags);
    float saved[3];std::memcpy(saved,translation,sizeof(saved));
    EffectTranslationForEye(world,eye,translation);
    static unsigned count=0;
    if(++count<=4)Log("Particle eye origin: packet=%p world=(%.3f,%.3f,%.3f) eye=(%.3f,%.3f,%.3f) relative=(%.3f,%.3f,%.3f)",
        snapshot,world[0],world[1],world[2],eye[0],eye[1],eye[2],translation[0],translation[1],translation[2]);
    const uintptr_t result=originalPrepare(snapshot,renderer,scene,flags);
    // The native helper copies constants. A second eye or auxiliary camera
    // must always see the original packet, without accumulated corrections.
    std::memcpy(translation,saved,sizeof(saved));
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
    return created==MH_OK&&enabled==MH_OK;
}
void CameraRelativeEffects_Shutdown() {if(target)MH_DisableHook(target);}
}
