#pragma once

// The BLVR x64 OpenXR host publishes this pointer-free mailbox. The retail
// game DLL consumes poses and input here because the installed headset runtime
// is 64-bit.

#include <cstdint>

namespace BLVR {
namespace BlvrPoseInputBridge {

constexpr char MappingName[] = "Local\\BLVR_XR_PoseBridge_v1";
constexpr uint32_t Magic = 0x50525847u;
constexpr uint32_t Version = 1u;

constexpr uint32_t SessionRunning = 1u << 1u;
constexpr uint32_t ViewsValid = 1u << 2u;
constexpr uint32_t HmdValid = 1u << 3u;
constexpr uint32_t LeftControllerValid = 1u << 4u;
constexpr uint32_t RightControllerValid = 1u << 5u;
// OpenXR session focus, independent of the desktop game window.
constexpr uint32_t InputFocused = 1u << 6u;

constexpr uint32_t GripPose = 1u << 0u;
constexpr uint32_t AimPose = 1u << 1u;
constexpr uint32_t Trigger = 1u << 2u;
constexpr uint32_t Squeeze = 1u << 3u;
constexpr uint32_t Thumbstick = 1u << 4u;
constexpr uint32_t Primary = 1u << 5u;
constexpr uint32_t Secondary = 1u << 6u;
constexpr uint32_t Menu = 1u << 7u;
constexpr uint32_t ThumbstickClick = 1u << 8u;

constexpr uint32_t PrimaryClick = 1u << 0u;
constexpr uint32_t SecondaryClick = 1u << 1u;
constexpr uint32_t MenuClick = 1u << 2u;
constexpr uint32_t ThumbstickPressed = 1u << 3u;
constexpr uint32_t TriggerPressed = 1u << 4u;
constexpr uint32_t SqueezePressed = 1u << 5u;

#pragma pack(push, 8)
struct Pose {
    float orientation[4];
    float position[3];
    float reserved;
};

struct ControllerState {
    Pose gripPose;
    Pose aimPose;
    float trigger;
    float squeeze;
    float thumbstickX;
    float thumbstickY;
    uint32_t buttons;
    uint32_t activeFlags;
    uint32_t reserved[2];
};

struct PoseBridge {
    uint32_t magic;
    uint32_t version;
    uint32_t structBytes;
    uint32_t producerPid;
    // BLVR host calls this publicationSequence; the generic BLVR reader uses
    // the shorter local name without changing the ABI or offset.
    volatile int32_t sequence;
    uint32_t flags;
    uint64_t producerEpoch;
    uint64_t frameId;
    int64_t predictedDisplayTime;
    uint64_t heartbeatTickMs;
    uint32_t referenceSpaceGeneration;
    uint32_t recenterRequestId;
    Pose hmdPose;
    Pose eyePoses[2];
    // OpenXR angleLeft, angleRight, angleUp, angleDown, in radians.
    float eyeFovs[2][4];
    ControllerState controllers[2];
};
#pragma pack(pop)

static_assert(sizeof(Pose) == 32u, "BLVR host Pose ABI changed");
static_assert(sizeof(ControllerState) == 96u, "BLVR host Controller ABI changed");
static_assert(sizeof(PoseBridge) == 384u, "BLVR host PoseBridge ABI changed");

} // namespace BlvrPoseInputBridge
} // namespace BLVR
