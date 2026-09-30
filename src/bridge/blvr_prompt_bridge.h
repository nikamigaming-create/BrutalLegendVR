#pragma once
#include <windows.h>
#include <cstdint>
namespace blvr_prompt_bridge {
constexpr wchar_t MappingName[]=L"Local\\BLVR_ControlPrompts_v1";
constexpr uint32_t Magic=0x50524c42;
struct Snapshot {
    volatile LONG sequence;
    uint32_t magic;
    uint32_t producerPid;
    uint32_t active;
    uint64_t producerEpoch;
    uint64_t tickMs;
    char title[192];
    char body[1536];
};
static_assert(sizeof(Snapshot)==1760,"Prompt mailbox ABI must match x86 and x64");
}
