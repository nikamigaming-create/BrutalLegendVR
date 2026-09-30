#pragma once
#include <cstdint>

namespace blvr_xr_bridge {
constexpr wchar_t NativeHandMappingName[]=L"Local\\BLVR_RenderedUi_v2";
constexpr uint32_t NativeHandMagic=0x48444e42u;
constexpr unsigned NativeHandHistory=64;
constexpr uint32_t NativeUiDriving=1u,NativeUiGuitar=2u;
#pragma pack(push,8)
struct NativeHandFrame {
    volatile int32_t sequence;
    uint32_t magic;
    uint64_t producerEpoch,sourceFrameId,poseFrameId;
    int64_t predictedDisplayTime;
    uint32_t validMask,flags;
    // Actual rendered hands: native right-wheel grip and tracked left arm,
    // relative to the exact center eye used for the game image. Never
    // substitute current controller poses for this source-frame publication.
    float palmToHead[2][16];
    float indexTipToHead[2][3];
    float guitarToHead[16];
};
struct NativeHandHistoryBuffer { NativeHandFrame slots[NativeHandHistory]; };
#pragma pack(pop)
static_assert(sizeof(NativeHandFrame)==264,"Native hand bridge ABI changed");
}
