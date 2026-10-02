#pragma once

#include <windows.h>
#include <cstdint>
#include <cstddef>

// A single native Flash snapshot, captured after rendering and before the
// desktop composite. Pixels retain native premultiplied BGRA and alpha.
// No process-local pointers cross the x86/x64 boundary.
namespace blvr_ui_bridge {
constexpr wchar_t MappingName[] = L"Local\\BLVR_NativeUi_v2";
constexpr uint32_t Magic = 0x49554c42u;
constexpr uint32_t Version = 2u;
enum ContentFlags : uint32_t { SoloRadial = 1u, SoloNotes = 2u, BuildRadial = 4u };
constexpr uint32_t MaxWidth = 1280u;
constexpr uint32_t MaxHeight = 720u;
constexpr size_t PixelBytes = size_t(MaxWidth) * MaxHeight * 4u;
#pragma pack(push, 8)
struct Header {
    uint32_t magic;
    uint32_t version;
    uint32_t headerBytes;
    volatile LONG sequence;
    uint32_t producerPid;
    uint32_t width;
    uint32_t height;
    uint32_t rowBytes;
    uint64_t producerEpoch;
    uint64_t tickMs;
    uint64_t sourceFrame;
    uint32_t contentSamples;
    uint32_t flags;
};
#pragma pack(pop)
constexpr size_t MappingBytes = sizeof(Header) + PixelBytes;
static_assert(sizeof(Header) == 64u, "Native UI mailbox ABI");
}
