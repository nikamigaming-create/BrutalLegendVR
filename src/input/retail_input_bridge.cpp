#include "retail_input_bridge.h"
#include "../camera/camera_hook.h"
#include "touch_controls.h"
#include "pose_controls.h"
#include "../openxr/blvr_pose_bridge.h"
#include <windows.h>
#include "MinHook.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <share.h>

namespace BLVR {
namespace {
using namespace BlvrPoseInputBridge;
using BuildInput = uintptr_t(__cdecl*)(void*, const void*, int, int);
BuildInput originalBuild = nullptr;
HANDLE mapping = nullptr;
const PoseBridge* bridge = nullptr;
bool installed = false;
uintptr_t base = 0;
FILE* logFile = nullptr;
uint64_t updates = 0;
uint64_t lastLog[2] = {};
uint64_t contextUpdates[2] = {};
GameplayInputMapper gameplayMapper = nullptr;

// Retail builds a 0x6c-byte logical input packet, then RVA 0x27b6c0
// computes held/pressed/released state. Axes are six pairs of floats.
// Buttons at +0x32 are logical actions, not XInput button IDs. The live
// keyboard binding table identifies actions 0=Return, 1=Escape, 2=E,
// 17=M, 18=Escape/pause, 19=LMB/axe, 20=RMB/guitar, 21/22=Space.
// Never write the derived history at +0x10c, +0x13f, or +0x172.
struct RawInput {
    float axes[6][2];
    uint8_t special[2];
    uint8_t buttons[51];
    uint8_t mouseAxes[6];
    uint8_t padding;
};
static_assert(sizeof(RawInput) == 0x6c, "Retail raw input layout");
static_assert(offsetof(RawInput, buttons) == 0x32, "Retail action offset");

bool ReadControllers(PoseBridge& value) {
    if (!bridge) {
        mapping = OpenFileMappingA(FILE_MAP_READ, FALSE, MappingName);
        if (!mapping) return false;
        bridge = static_cast<const PoseBridge*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, sizeof(PoseBridge)));
        if (!bridge) { CloseHandle(mapping); mapping = nullptr; return false; }
    }
    for (int attempt = 0; attempt < 3; ++attempt) {
        const int32_t sequence = bridge->sequence;
        if (sequence & 1) continue;
        MemoryBarrier();
        memcpy(&value, bridge, sizeof(value));
        MemoryBarrier();
        if (bridge->sequence != sequence) continue;
        const uint64_t now = GetTickCount64();
        return value.magic == Magic && value.version == Version &&
            value.structBytes == sizeof(PoseBridge) &&
            (value.flags & SessionRunning) &&
            value.heartbeatTickMs <= now && now - value.heartbeatTickMs < 250;
    }
    return false;
}

bool IsSelectedController(const void* record) {
    int32_t active = *reinterpret_cast<const int32_t*>(base + 0xc12130);
    const uint32_t count = *reinterpret_cast<const uint32_t*>(base + 0xc11a80) >> 6;
    const uintptr_t pads = *reinterpret_cast<const uintptr_t*>(base + 0xc11a88);
    if (active == -1) active = 0;
    return active >= 0 && static_cast<uint32_t>(active) < count && pads &&
        record == reinterpret_cast<void*>(pads + static_cast<uintptr_t>(active) * 0x494);
}

float Axis(float value) {
    return std::isfinite(value) && std::fabs(value) >= 0.18f
        ? std::clamp(value, -1.0f, 1.0f) : 0.0f;
}

// Retail calls this builder both from the front-end poll (RVA 0x278b20)
// and directly for gameplay (RVA 0x278cea). Inject before either caller
// copies, aggregates, or derives edges from the packet.
uintptr_t __cdecl HookBuild(void* state, const void* record, int mask, int context) {
    const uintptr_t result = originalBuild(state, record, mask, context);
    PoseBridge sample{};
    bool mapped = false;
    float lx = 0, ly = 0, rx = 0, ry = 0;
    uint32_t leftButtons = 0, rightButtons = 0;
    if (state && IsSelectedController(record) && ReadControllers(sample)) {
        auto& raw = *static_cast<RawInput*>(state);
        const auto& left = sample.controllers[0];
        const auto& right = sample.controllers[1];
        const bool leftActive = (sample.flags & LeftControllerValid) != 0;
        const bool rightActive = (sample.flags & RightControllerValid) != 0;
        lx = leftActive && (left.activeFlags & Thumbstick) ? Axis(left.thumbstickX) : 0;
        ly = leftActive && (left.activeFlags & Thumbstick) ? Axis(left.thumbstickY) : 0;
        rx = rightActive && (right.activeFlags & Thumbstick) ? Axis(right.thumbstickX) : 0;
        ry = rightActive && (right.activeFlags & Thumbstick) ? Axis(right.thumbstickY) : 0;
        leftButtons = leftActive ? left.buttons : 0;
        rightButtons = rightActive ? right.buttons : 0;
        TouchControls touch{};
        touch.lx=lx;touch.ly=ly;touch.rx=rx;touch.ry=ry;
        touch.lt=leftActive&&(left.activeFlags&Trigger)?left.trigger:0;
        touch.rt=rightActive&&(right.activeFlags&Trigger)?right.trigger:0;
        touch.lg=leftActive&&(left.activeFlags&Squeeze)?left.squeeze:0;
        touch.rg=rightActive&&(right.activeFlags&Squeeze)?right.squeeze:0;
        touch.a=(rightButtons&PrimaryClick)!=0;touch.b=(rightButtons&SecondaryClick)!=0;
        touch.x=(leftButtons&PrimaryClick)!=0;touch.y=(leftButtons&SecondaryClick)!=0;
        touch.leftClick=(leftButtons&ThumbstickPressed)!=0;
        touch.rightClick=(rightButtons&ThumbstickPressed)!=0;
        touch.menu=((leftButtons|rightButtons)&MenuClick)!=0;
        const auto& bindings=ActiveBindings();
        const bool recenterChord=RecenterHeld(touch,bindings);
        touch.rigMetadata=(right.activeFlags&RigActionMetadata)!=0;
        touch.soloNotes=(right.activeFlags&SoloNotesMetadata)!=0;
        touch.soloRadial=(right.activeFlags&SoloRadialMetadata)!=0;
        touch.driving=(right.activeFlags&DrivingMetadata)!=0;
        touch.hostRadial=(right.activeFlags&HostRadialMetadata)!=0;
        touch.hostAccept=(right.activeFlags&HostAcceptMetadata)!=0;
        touch.hostConfirm=(right.activeFlags&HostConfirmMetadata)!=0;
        touch.weapon=right.reserved[0];touch.physical=right.reserved[1];
        // Pin the authored note for the entire stroke pulse. Advancing the
        // native sequence must never turn one held pulse into another note.
        static unsigned strumNote[2]{};
        static bool strumHeld[2]{};
        const unsigned inputStream=context==0?0u:1u;
        const bool playingStroke=touch.soloNotes&&touch.physical==2;
        if(!playingStroke)strumNote[inputStream]=0;
        else if(!strumHeld[inputStream]) {
            const unsigned authored=CameraHook_GetSoloNextNote();
            strumNote[inputStream]=authored?authored:TouchValue(touch,bindings.actions[SoloNote3].input)>.5f?3u:
                TouchValue(touch,bindings.actions[SoloNote2].input)>.5f?2u:1u;
        }
        strumHeld[inputStream]=playingStroke;
        touch.soloStrumNote=strumNote[inputStream];
        const NativeControls controls=MapTouch(touch,bindings);
        // Menu directions retain the unrotated stick. Gameplay movement is
        // converted from HMD-relative intent to the native camera reference.
        float moveX=0,moveY=0,lookX=0,lookY=0,radialX=0,radialY=0;
        StickValues(touch,bindings.movementStick,moveX,moveY);
        StickValues(touch,bindings.turnStick,lookX,lookY);
        StickValues(touch,bindings.radialStick,radialX,radialY);
        if(right.activeFlags&HostRadialAxesMetadata)UnpackRadialAxes(left.reserved[1],radialX,radialY);
        if(recenterChord)moveX=moveY=lookX=lookY=radialX=radialY=0;
        const bool pause=TouchValue(touch,bindings.actions[UiStart].input)>.5f||TouchValue(touch,bindings.actions[Journal].input)>.5f;
        if(rightActive && touch.driving && !controls.command && !pause && moveX==0 &&
           (right.activeFlags&WheelSteeringMetadata)) {
            float wheel=0;std::memcpy(&wheel,&left.reserved[0],sizeof(wheel));
            if(std::isfinite(wheel))moveX=std::clamp(wheel,-1.f,1.f);
        }
        if(controls.command||controls.radial||controls.targeting||touch.soloNotes)lookX=lookY=0;
        if (gameplayMapper) gameplayMapper(moveX, moveY, lookX, lookY, context == 0);
        float menuX=recenterChord?0:InputStrength(touch,bindings.actions[UiRight].input)-InputStrength(touch,bindings.actions[UiLeft].input);
        float menuY=recenterChord?0:InputStrength(touch,bindings.actions[UiUp].input)-InputStrength(touch,bindings.actions[UiDown].input);
        if(!recenterChord&&(right.activeFlags&HostOpeningAxesMetadata))UnpackRadialAxes(left.reserved[1],menuX,menuY);
        raw.axes[0][0]=std::clamp(raw.axes[0][0]+menuX,-1.f,1.f);
        raw.axes[0][1]=std::clamp(raw.axes[0][1]+menuY,-1.f,1.f);
        raw.axes[2][0]=std::clamp(raw.axes[2][0]+moveX,-1.f,1.f);
        raw.axes[2][1]=std::clamp(raw.axes[2][1]+moveY,-1.f,1.f);
        // Pairs 1/3/5 are the retail look contexts. Mouse-relative mode must
        // be cleared for joystick values so the game applies its turn rate.
        for (int axis : {1, 3}) {
            raw.axes[axis][0] = std::clamp(raw.axes[axis][0] + lookX, -1.0f, 1.0f);
            raw.axes[axis][1] = std::clamp(raw.axes[axis][1] + lookY, -1.0f, 1.0f);
            if (lookX || lookY) raw.mouseAxes[axis] = 0;
        }
        // Target switching and radial menus must not be eaten by snap turning.
        for(int axis : {4,5}) {
            const bool enabled=axis==4?controls.targeting:(controls.radial||controls.command);
            if(enabled) {
                raw.axes[axis][0]=radialX;raw.axes[axis][1]=radialY;raw.mouseAxes[axis]=0;
            }
        }
        for(unsigned i=0;i<2;++i) raw.special[i]=(std::max)(raw.special[i],controls.analog[i]);
        const bool a = controls.buttons[Accept]||controls.buttons[UiA];
        if (a && *reinterpret_cast<int32_t*>(base + 0xc12130) == -1 &&
            (*reinterpret_cast<uint32_t*>(base + 0xc11a80) >> 6) > 0) {
            // A is the user's explicit title/join action.
            *reinterpret_cast<int32_t*>(base + 0xc12130) = 0;
        }
        const auto press = [&raw](int action, bool down) {
            if (down) raw.buttons[action] = 255;
        };
        for(unsigned i=0;i<NativeActionCount;++i)press(i,controls.buttons[i]!=0);
        mapped = true;
    }
    if (mapped) {
        ++updates;
        const unsigned stream = context == 0 ? 0 : 1;
        ++contextUpdates[stream];
        const uint64_t now = GetTickCount64();
        if (logFile && (contextUpdates[stream] <= 3 || now - lastLog[stream] >= 500)) {
            DWORD foregroundPid = 0;
            GetWindowThreadProcessId(GetForegroundWindow(), &foregroundPid);
            fprintf(logFile,
                "tick=%llu update=%llu frame=%llu state=%p context=%d desktopFocused=%d sticks=(%.3f,%.3f,%.3f,%.3f) buttons=(%X,%X) raw=(%.3f,%.3f) actions=(%u,%u,%u) pause=%u xrFlags=%X\n",
                now, updates, sample.frameId, state, context, foregroundPid == GetCurrentProcessId(), lx, ly, rx, ry, leftButtons, rightButtons,
                static_cast<const RawInput*>(state)->axes[0][0],
                static_cast<const RawInput*>(state)->axes[0][1],
                static_cast<const RawInput*>(state)->buttons[2], static_cast<const RawInput*>(state)->buttons[19],
                static_cast<const RawInput*>(state)->buttons[20], static_cast<const RawInput*>(state)->buttons[18], sample.flags);
            fflush(logFile);
            lastLog[stream] = now;
        }
    }
    return result;
}
} // namespace

bool RetailInputBridgeActive() { return installed; }
void RetailInputBridgeSetGameplayMapper(GameplayInputMapper mapper) { gameplayMapper = mapper; }

bool InstallRetailInputBridge() {
    if (installed) return true;
    base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    void* target = reinterpret_cast<void*>(base + 0x278060);
    static const uint8_t expected[] = {0x55,0x8b,0xec,0x83,0xe4,0xf8,0x83,0xec,0x2c,0x53,0x56};
    if (memcmp(target, expected, sizeof(expected)) != 0) return false;
    char path[MAX_PATH]{};
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    if (char* slash = strrchr(path, '\\')) strcpy_s(slash+1, MAX_PATH-(slash+1-path), "blvr_input.log");
    logFile = _fsopen(path, "a", _SH_DENYNO);
    const MH_STATUS init = MH_Initialize();
    if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) return false;
    const MH_STATUS created = MH_CreateHook(target, reinterpret_cast<void*>(&HookBuild),
                                            reinterpret_cast<void**>(&originalBuild));
    const MH_STATUS enabled = created == MH_OK ? MH_EnableHook(target) : created;
    installed = created == MH_OK && enabled == MH_OK;
    if (logFile) {
        fprintf(logFile, "RetailRawInputBridge pid=%lu base=%p target=%p created=%d enabled=%d\n",
            GetCurrentProcessId(), reinterpret_cast<void*>(base), target, created, enabled);
        fflush(logFile);
    }
    return installed;
}
} // namespace BLVR
