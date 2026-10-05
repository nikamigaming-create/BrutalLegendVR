#include "retail_input_bridge.h"
#include "../camera/camera_hook.h"
#include "touch_controls.h"
#include "pose_controls.h"
#include "ui_navigation_dedup.h"
#include "../openxr/blvr_pose_bridge.h"
#include <windows.h>
#include "MinHook.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <cstdarg>
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
SRWLOCK inputLogLock=SRWLOCK_INIT;
constexpr uint64_t InputLogHardLimit=32ull*1024ull*1024ull;
uint64_t inputLogLimit=InputLogHardLimit,inputLogBytes=0;
bool inputLogStopped=false;
void InputLog(const char* format,...) {
    if(!logFile)return;
    char row[1024]{};va_list args;va_start(args,format);
    const int bytes=_vsnprintf_s(row,sizeof(row),_TRUNCATE,format,args);va_end(args);
    if(bytes<0)return;
    AcquireSRWLockExclusive(&inputLogLock);
    if(!inputLogStopped) {
        if(inputLogLimit>InputLogHardLimit)inputLogLimit=InputLogHardLimit;
        if(inputLogLimit<128||inputLogBytes+static_cast<unsigned>(bytes)>inputLogLimit-128) {
            inputLogStopped=true;
            const char stop[]="RetailInputLog stopped=byte_limit\n";
            if(inputLogBytes+sizeof(stop)-1<=inputLogLimit) {
                inputLogBytes+=fwrite(stop,1,sizeof(stop)-1,logFile);fflush(logFile);
            }
        } else {
            inputLogBytes+=fwrite(row,1,static_cast<unsigned>(bytes),logFile);fflush(logFile);
        }
    }
    ReleaseSRWLockExclusive(&inputLogLock);
}
uint64_t updates = 0;
uint64_t lastLog[2] = {};
uint64_t contextUpdates[2] = {};
GameplayInputMapper gameplayMapper = nullptr;
uintptr_t originalUiDispatch = 0;
bool uiDedupInstalled = false;

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

struct UiInputIntent {
    RawInput raw{};
    uintptr_t controller=0;
    uint64_t tick=0,epoch=0,frame=0;
    uint32_t producer=0,validHands=0,directions=0;
};
SRWLOCK uiIntentLock=SRWLOCK_INIT;
UiInputIntent uiIntent[2]{};

void PublishUiIntent(const RawInput& raw,const NativeControls& controls,const PoseBridge& sample,
                     const void* controller,unsigned stream) {
    UiInputIntent value{};
    value.raw=raw;value.controller=reinterpret_cast<uintptr_t>(controller);
    value.tick=GetTickCount64();value.epoch=sample.producerEpoch;value.frame=sample.frameId;
    value.producer=sample.producerPid;value.validHands=sample.flags&(LeftControllerValid|RightControllerValid);
    for(unsigned action : {unsigned(UiUp),unsigned(UiDown),unsigned(UiLeft),unsigned(UiRight)})
        if(controls.buttons[action])value.directions|=1u<<action;
    AcquireSRWLockExclusive(&uiIntentLock);uiIntent[stream]=value;ReleaseSRWLockExclusive(&uiIntentLock);
}

struct UiDispatchState {
    uintptr_t manager=0,owner=0,movie=0,list=0,controller=0;
    unsigned index=0,count=0;
    alignas(8) uint8_t packet[NativeUiInputBytes]{};
};

// The caller holds the native Flash critical section. This leaf still refuses
// stale/replaced owners and invalid pointers, without calling game getters.
bool ReadUiDispatchState(uintptr_t owner,uintptr_t manager,uintptr_t caller,UiDispatchState* out) {
    __try {
        if(!base||!owner||!manager||!out||caller!=base+0x694b6 ||
           *reinterpret_cast<uintptr_t*>(base+0xc09ca8)!=manager||
           *reinterpret_cast<uintptr_t*>(manager+4)!=owner)return false;
        const unsigned index=*reinterpret_cast<unsigned*>(owner);
        // The native manager has two 0x350 input records, before +0x6fc.
        if(index>1||!*(uint8_t*)(owner+8)||!*(uint8_t*)(owner+0xa)||!*(uint8_t*)(owner+0xb)||
           *reinterpret_cast<uintptr_t*>(owner+0xc))return false;
        const unsigned count=*reinterpret_cast<unsigned*>(manager+0xc)>>6;
        const uintptr_t list=*reinterpret_cast<uintptr_t*>(manager+0x14);
        if(!list||count==0||count>256)return false;
        unsigned matches=0;
        for(unsigned i=0;i<count;++i)if(reinterpret_cast<const uintptr_t*>(list)[i]==owner)++matches;
        if(matches!=1)return false;
        const unsigned modalCount=*reinterpret_cast<unsigned*>(manager+0x48)>>6;
        const uintptr_t modalList=*reinterpret_cast<uintptr_t*>(manager+0x50);
        if(modalCount>256||(modalCount&&(!modalList||reinterpret_cast<const uintptr_t*>(modalList)[modalCount-1]!=owner)))return false;
        const uintptr_t movie=*reinterpret_cast<uintptr_t*>(owner+0x60);
        if(!movie||!*(uint8_t*)(movie+0x64)||*(uint8_t*)(movie+0x65)||!*reinterpret_cast<uintptr_t*>(movie))return false;
        const uintptr_t packet=manager+0x5c+index*NativeUiInputBytes;
        const unsigned frame=*reinterpret_cast<unsigned*>(packet+0x108);
        if(frame==*reinterpret_cast<unsigned*>(movie+0x68))return false;
        int active=*reinterpret_cast<int*>(base+0xc12130);if(active==-1)active=0;
        const unsigned pads=*reinterpret_cast<unsigned*>(base+0xc11a80)>>6;
        const uintptr_t records=*reinterpret_cast<uintptr_t*>(base+0xc11a88);
        if(!records||active<0||pads>256||static_cast<unsigned>(active)>=pads)return false;
        out->manager=manager;out->owner=owner;out->movie=movie;out->list=list;
        out->index=index;out->count=count;out->controller=records+active*0x494;
        std::memcpy(out->packet,reinterpret_cast<void*>(packet),sizeof(out->packet));
        return *reinterpret_cast<uintptr_t*>(base+0xc09ca8)==manager&&
            *reinterpret_cast<uintptr_t*>(manager+4)==owner&&
            *reinterpret_cast<uintptr_t*>(manager+0x14)==list&&
            (*reinterpret_cast<unsigned*>(manager+0xc)>>6)==count&&
            *reinterpret_cast<unsigned*>(owner)==index&&*(uint8_t*)(owner+8)&&*(uint8_t*)(owner+0xa)&&*(uint8_t*)(owner+0xb)&&
            !*reinterpret_cast<uintptr_t*>(owner+0xc)&&*reinterpret_cast<uintptr_t*>(owner+0x60)==movie&&
            *(uint8_t*)(movie+0x64)&&!*(uint8_t*)(movie+0x65)&&
            *reinterpret_cast<unsigned*>(packet+0x108)==frame&&*reinterpret_cast<unsigned*>(movie+0x68)!=frame&&
            std::memcmp(out->packet,reinterpret_cast<void*>(packet),sizeof(out->packet))==0;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

unsigned PrepareUiDispatchCopy(UiDispatchState& state,const PoseBridge& current,uint64_t now) {
    UiInputIntent saved[2]{};
    if(!TryAcquireSRWLockShared(&uiIntentLock))return 0;
    saved[0]=uiIntent[0];saved[1]=uiIntent[1];ReleaseSRWLockShared(&uiIntentLock);
    for(const auto& intent:saved) {
        if(!intent.directions||!intent.producer||!intent.epoch||intent.tick>now||now-intent.tick>=250||
           intent.controller!=state.controller||intent.producer!=current.producerPid||intent.epoch!=current.producerEpoch||
           intent.frame>current.frameId||intent.validHands!=(current.flags&(LeftControllerValid|RightControllerValid))||
           std::memcmp(&intent.raw,state.packet,sizeof(RawInput))!=0)continue;
        const unsigned mask=NativeUiDuplicateDirections(state.packet,intent.directions);
        SuppressNativeUiDuplicateDirections(state.packet,mask);return mask;
    }
    return 0;
}

extern "C" __declspec(naked) void CallOriginalUiDispatch(void*,void*) {
    __asm {
        push esi
        mov esi,dword ptr [esp+8]
        mov ecx,dword ptr [esp+12]
        call dword ptr [originalUiDispatch]
        pop esi
        ret
    }
}
void __cdecl DispatchMappedUi(uintptr_t owner,uintptr_t manager,uintptr_t caller);
extern "C" __declspec(naked) void HookUiDispatch() {
    __asm {
        push dword ptr [esp]
        push ecx
        push esi
        call DispatchMappedUi
        add esp,12
        ret
    }
}

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

void __cdecl DispatchMappedUi(uintptr_t owner,uintptr_t manager,uintptr_t caller) {
    UiDispatchState state{};PoseBridge current{};unsigned mask=0;
    if(ReadControllers(current)&&ReadUiDispatchState(owner,manager,caller,&state))
        mask=PrepareUiDispatchCopy(state,current,GetTickCount64());
    // The dispatcher only uses ECX to locate this one input record. A private
    // record copy preserves every native axis, strength and history byte except
    // the duplicate digital event gate. The actual manager is never modified.
    const uintptr_t input=mask?reinterpret_cast<uintptr_t>(state.packet)-0x5c-state.index*NativeUiInputBytes:manager;
    CallOriginalUiDispatch(reinterpret_cast<void*>(owner),reinterpret_cast<void*>(input));
    static unsigned receipts=0;
    if(mask&&logFile&&receipts<4) {
        ++receipts;
        InputLog("NativeUiDirectionDedup owner=%p input=%u generation=%u mask=%X mode=arrows copy=1\n",
            reinterpret_cast<void*>(owner),state.index,*reinterpret_cast<unsigned*>(state.packet+0x108),mask);
    }
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
        touch.buildRadial=(right.activeFlags&BuildRadialMetadata)!=0;
        touch.driving=(right.activeFlags&DrivingMetadata)!=0;
        touch.guitarFretting=leftActive&&(right.activeFlags&GuitarFretMetadata)!=0;
        touch.hostRadial=rightActive&&(right.activeFlags&HostRadialMetadata)!=0;
        touch.hostAccept=rightActive&&(right.activeFlags&HostAcceptMetadata)!=0;
        touch.hostConfirm=rightActive&&(right.activeFlags&HostConfirmMetadata)!=0;
        // Selection and native UI/driving context survive a lost hand, but
        // generated button pulses must release with their source controller.
        touch.weapon=right.reserved[0];touch.physical=rightActive?right.reserved[1]:0;
        // Pin the authored note for the entire stroke pulse. Advancing the
        // native sequence must never turn one held pulse into another note.
        static unsigned strumNote[2]{};
        static bool strumHeld[2]{};
        const unsigned inputStream=context==0?0u:1u;
        const bool playingStroke=touch.soloNotes&&touch.physical==2;
        if(!playingStroke)strumNote[inputStream]=0;
        else if(!strumHeld[inputStream]) {
            const unsigned authored=CameraHook_GetSoloNextNote();
            const bool command=CommandHeld(touch,bindings);
            const unsigned fingerNote=touch.guitarFretting?left.reserved[0]:0;
            strumNote[inputStream]=fingerNote>=1&&fingerNote<=3?fingerNote:authored?authored:ScopedActionDown(touch,SoloNote3,command,bindings)?3u:
                ScopedActionDown(touch,SoloNote2,command,bindings)?2u:1u;
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
        if(rightActive&&(right.activeFlags&HostRadialAxesMetadata))UnpackRadialAxes(left.reserved[1],radialX,radialY);
        if(recenterChord)moveX=moveY=lookX=lookY=radialX=radialY=0;
        const bool pause=controls.buttons[UiStart]||controls.buttons[Journal];
        if(rightActive && touch.driving && !controls.command && !pause && moveX==0 &&
           (right.activeFlags&WheelSteeringMetadata)) {
            float wheel=0;std::memcpy(&wheel,&left.reserved[0],sizeof(wheel));
            if(std::isfinite(wheel))moveX=std::clamp(wheel,-1.f,1.f);
        }
        if(controls.command||controls.radial||controls.build||controls.targeting||touch.soloNotes)lookX=lookY=0;
        if (gameplayMapper) gameplayMapper(moveX, moveY, lookX, lookY, context == 0);
        float menuX=recenterChord?0:ScopedInputStrength(touch,UiRight,controls.command,bindings)-ScopedInputStrength(touch,UiLeft,controls.command,bindings);
        float menuY=recenterChord?0:ScopedInputStrength(touch,UiUp,controls.command,bindings)-ScopedInputStrength(touch,UiDown,controls.command,bindings);
        if(!recenterChord&&rightActive&&(right.activeFlags&HostOpeningAxesMetadata))UnpackRadialAxes(left.reserved[1],menuX,menuY);
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
            const bool enabled=axis==4?controls.targeting:(controls.radial||controls.command||controls.build);
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
        PublishUiIntent(raw,controls,sample,record,inputStream);
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
            InputLog(
                "tick=%llu update=%llu frame=%llu state=%p context=%d desktopFocused=%d sticks=(%.3f,%.3f,%.3f,%.3f) buttons=(%X,%X) raw=(%.3f,%.3f) actions=(%u,%u,%u) pause=%u xrFlags=%X\n",
                now, updates, sample.frameId, state, context, foregroundPid == GetCurrentProcessId(), lx, ly, rx, ry, leftButtons, rightButtons,
                static_cast<const RawInput*>(state)->axes[0][0],
                static_cast<const RawInput*>(state)->axes[0][1],
                static_cast<const RawInput*>(state)->buttons[2], static_cast<const RawInput*>(state)->buttons[19],
                static_cast<const RawInput*>(state)->buttons[20], static_cast<const RawInput*>(state)->buttons[18], sample.flags);
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
    char telemetry[16]{};
    const auto enabledLength=GetEnvironmentVariableA("BLVR_TELEMETRY",telemetry,sizeof(telemetry));
    if(enabledLength&&enabledLength<sizeof(telemetry)&&(!strcmp(telemetry,"1")||!_stricmp(telemetry,"true")))
        logFile = _fsopen(path, "wb", _SH_DENYNO);
    const MH_STATUS init = MH_Initialize();
    if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) return false;
    const MH_STATUS created = MH_CreateHook(target, reinterpret_cast<void*>(&HookBuild),
                                            reinterpret_cast<void**>(&originalBuild));
    const MH_STATUS enabled = created == MH_OK ? MH_EnableHook(target) : created;
    installed = created == MH_OK && enabled == MH_OK;
    // Only the supported dispatcher/owner layout can activate this correction.
    static const uint8_t uiEntry[]={0x55,0x8b,0xec,0x83,0xec,0x78,0x8b,0x06,0x69,0xc0,0x50,0x03,0,0,0x8d,0x4c,0x08,0x5c};
    static const uint8_t uiCaller[]={0x8b,0xcb,0xe8,0x9a,0x01,0,0};
    static const uint8_t arrowMode[]={0x8a,0x4e,0x0b,0xd9,0xee,0x8b,0x43,0x6c,0x33,0xd2,0x84,0xc9};
    if(installed&&std::memcmp(reinterpret_cast<void*>(base+0x69650),uiEntry,sizeof(uiEntry))==0&&
       std::memcmp(reinterpret_cast<void*>(base+0x694af),uiCaller,sizeof(uiCaller))==0&&
       std::memcmp(reinterpret_cast<void*>(base+0x699f4),arrowMode,sizeof(arrowMode))==0) {
        void* uiTarget=reinterpret_cast<void*>(base+0x69650);
        const auto uiCreated=MH_CreateHook(uiTarget,reinterpret_cast<void*>(&HookUiDispatch),reinterpret_cast<void**>(&originalUiDispatch));
        uiDedupInstalled=uiCreated==MH_OK&&MH_EnableHook(uiTarget)==MH_OK;
    }
    if (logFile) {
        InputLog("RetailRawInputBridge pid=%lu base=%p target=%p created=%d enabled=%d\n",
            GetCurrentProcessId(), reinterpret_cast<void*>(base), target, created, enabled);
        InputLog("NativeUiDirectionDedup installed=%d\n",uiDedupInstalled?1:0);
    }
    return installed;
}
} // namespace BLVR
