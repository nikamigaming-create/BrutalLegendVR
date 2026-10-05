#pragma once
#include <cstddef>
#include <cstdint>

namespace blvr_xr_bridge {
constexpr wchar_t RigMappingName[]=L"Local\\BLVR_TrackedEddie_v4";
constexpr uint32_t RigMagic=0x47495242u;
constexpr uint32_t RigVersion=4u;
constexpr uint32_t RigChestGuitar=1u;
constexpr unsigned RigBones=256,RigHistory=64;
#pragma pack(push,8)
struct RigFrame {
    volatile int32_t sequence;
    uint32_t magic;
    uint64_t producerEpoch,frameId;
    int64_t predictedDisplayTime;
    uint64_t skeletonSignature;
    uint32_t boneCount,selectedWeapon;
    // Row-vector transforms from original asset/bind coordinates into the
    // CENTER HMD's view space, in meters. Join to that exact source pose.
    float skinToHead[RigBones][16];
    float weaponToHead[2][16]; // axe, guitar
    // Retarget the game's current render-owned pose. No recorded motion or
    // earlier game pose crosses this bridge.
    uint64_t controlsSignature;
    uint32_t liveAction; // 0 tracked, 1 axe, 2 guitar, 3 Earthshaker
    float liveActionWeight;
    uint32_t supportHeld;
    // An invalid grip uses this render frame's native arm and attachment.
    // It must never be interpreted as a tracked bind-pose arm.
    uint32_t trackedHandMask; // bit 0 left, bit 1 right
    uint32_t version,structBytes;
    uint32_t presentationFlags; // RigChestGuitar: independent of either wrist
    uint32_t guitarFret;
};
struct RigHistoryBuffer { RigFrame slots[RigHistory]; };
#pragma pack(pop)
static_assert(offsetof(RigFrame,skinToHead)==48,"Rig bridge layout changed");
static_assert(sizeof(RigFrame)==16600,"Rig bridge ABI changed");
inline uint64_t RigNameHash(uint64_t hash,const char* name) {
    do {hash^=static_cast<uint8_t>(*name);hash*=1099511628211ull;} while(*name++);
    return hash;
}
}
