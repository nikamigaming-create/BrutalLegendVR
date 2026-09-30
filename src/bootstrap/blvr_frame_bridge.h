#pragma once

#include <cstdint>
#include <windows.h>

// The BLVR x64 host consumes this CPU mailbox. The retail game DLL publishes
// downscaled copies of the D3D9 eye renders for OpenXR composition.
namespace BLVR {
namespace BlvrFrameTransport {

constexpr wchar_t DescriptorName[] = L"Local\\BLVR_XR_FrameBridge_v6";
constexpr wchar_t CpuMailboxName[] = L"Local\\BLVR_XR_CpuFrame_v3";
constexpr uint32_t Magic = 0x46525847u;
constexpr uint32_t Version = 6u;
constexpr uint32_t CpuMagic = 0x50435847u;
constexpr uint32_t CpuVersion = 3u;
constexpr uint32_t SlotCount = 3u;
constexpr uint32_t EyeCount = 2u;
constexpr uint32_t MaxWidth = 2048u;
constexpr uint32_t MaxHeight = 2048u;
constexpr uint32_t BytesPerPixel = 4u;
constexpr uint64_t EyeBytes = static_cast<uint64_t>(MaxWidth) * MaxHeight * BytesPerPixel;
constexpr uint64_t SlotBytes = EyeCount * EyeBytes;

enum Flags : uint32_t {
    Running = 1u << 0u,
    Stereo = 1u << 2u,
    EyesDistinct = 1u << 3u,
    SameSimulationTick = 1u << 4u,
    PoseStamped = 1u << 5u,
    ImmersiveMono = 1u << 8u,
    CpuBgraMailbox = 1u << 9u,
    VerifiedDrawSceneStereo = 1u << 10u,
    FirstPersonCamera = 1u << 12u,
    NativeHeadHidden = 1u << 13u,
};

enum class PixelFormat : uint32_t {
    B8G8R8A8Unorm = 1u,
    B8G8R8X8Unorm = 2u,
};

enum class PresentationMode : uint32_t {
    WorldStereo = 1u,
    UiQuad = 2u,
    WorldMono = 3u,
};
constexpr uint32_t UiReasonCinematic = 1u << 3u;

#pragma pack(push, 8)
struct EyeResource { uint64_t textureHandle; };
struct ResourceSlot {
    EyeResource eyes[EyeCount];
    uint64_t transactionId;
};
struct CpuFrameMailbox {
    uint32_t magic;
    uint32_t version;
    uint32_t headerBytes;
    uint32_t slotCount;
    uint32_t maxWidth;
    uint32_t maxHeight;
    uint32_t bytesPerPixel;
    uint32_t eyeCount;
    volatile LONG slotSequence[SlotCount];
    uint32_t reserved1;
    uint64_t slotTransactionId[SlotCount];
    uint64_t mappingBytes;
};
struct FrameBridge {
    uint32_t magic;
    uint32_t version;
    uint32_t structBytes;
    uint32_t slotCount;
    volatile LONG publicationSequence;
    uint32_t producerPid;
    uint64_t producerEpoch;
    uint64_t resourceSetId;
    uint64_t adapterLuid;
    uint32_t width;
    uint32_t height;
    PixelFormat format;
    uint32_t flags;
    uint32_t currentSlot;
    uint32_t resourceGeneration;
    uint64_t transactionId;
    uint64_t sourceFrameId[EyeCount];
    uint64_t poseSequence[EyeCount];
    int64_t renderedDisplayTime[EyeCount];
    uint64_t heartbeatTickMs;
    uint64_t readyFenceHandle;
    uint64_t releaseFenceHandle;
    ResourceSlot slots[SlotCount];
    uint64_t mappingBytes;
    PresentationMode presentationMode;
    uint32_t uiReasonFlags;
    uint32_t uiEye;
    uint32_t contentWidth;
    uint32_t contentHeight;
    uint32_t reserved32;
};
#pragma pack(pop)

constexpr uint64_t CpuMappingBytes = sizeof(CpuFrameMailbox) + SlotCount * SlotBytes;
static_assert(sizeof(CpuFrameMailbox) == 80u, "BLVR CPU mailbox ABI changed");
static_assert(sizeof(FrameBridge) == 256u, "BLVR frame bridge ABI changed");

} // namespace BlvrFrameTransport
} // namespace BLVR
