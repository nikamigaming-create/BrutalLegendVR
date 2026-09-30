// Exercises the bridge against the legally installed game's own input updater.
// No game loop, XR runtime, desktop input, or shared-mailbox writes are used.
#include "../src/input/retail_input_bridge.cpp"
#include "../src/input/vr_prompt_text.h"
#include <array>
#include <cstdlib>
static unsigned expectedSoloNote=0;
namespace BLVR { unsigned CameraHook_GetSoloNextNote() {return expectedSoloNote;} }

static void Check(bool condition, const char* message) {
    if (!condition) { fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

int wmain(int argc, wchar_t** argv) {
    SetUnhandledExceptionFilter([](EXCEPTION_POINTERS* fault) -> LONG {
        fprintf(stderr, "Native test exception code=%08lX address=%p access=%08lX\n",
            fault->ExceptionRecord->ExceptionCode, fault->ExceptionRecord->ExceptionAddress,
            fault->ExceptionRecord->NumberParameters > 1 ?
                static_cast<DWORD>(fault->ExceptionRecord->ExceptionInformation[1]) : 0);
        return EXCEPTION_EXECUTE_HANDLER;
    });
    using namespace BLVR;
    using namespace BLVR::BlvrPoseInputBridge;
    Check(argc == 2, "provide installed BrutalLegend.exe path");
    HMODULE retail = LoadLibraryExW(argv[1], nullptr, DONT_RESOLVE_DLL_REFERENCES);
    Check(retail != nullptr, "map retail image without executing its entry point");
    base = reinterpret_cast<uintptr_t>(retail);
    const uint8_t prologue[] = {0x55,0x8b,0xec,0x83,0xe4,0xf8,0xd9,0x45,0x08};
    Check(memcmp(reinterpret_cast<void*>(base + 0x27b6c0), prologue, sizeof(prologue)) == 0,
          "expected retail input update build");
    auto nativeUpdate = reinterpret_cast<void(__thiscall*)(void*,float)>(base + 0x27b6c0);
    alignas(8) std::array<uint8_t, 0x494> record{};
    *reinterpret_cast<uintptr_t*>(base + 0xc11a88) = reinterpret_cast<uintptr_t>(record.data());
    *reinterpret_cast<uint32_t*>(base + 0xc11a80) = 1u << 6;
    *reinterpret_cast<int32_t*>(base + 0xc12130) = 0;
    auto* state = record.data() + 0x134;
    auto& raw = *reinterpret_cast<RawInput*>(state);
    PoseBridge sample{};
    sample.magic = Magic; sample.version = Version; sample.structBytes = sizeof(sample);
    sample.flags = SessionRunning | LeftControllerValid | RightControllerValid;
    sample.controllers[0].activeFlags = Thumbstick | Primary | Secondary | Trigger | Menu;
    sample.controllers[1].activeFlags = Thumbstick | Primary | Secondary | Trigger | Menu;
    sample.controllers[0].activeFlags |= Squeeze;
    sample.controllers[1].activeFlags |= Squeeze;
    bridge = &sample;
    originalBuild = [](void* packet, const void*, int, int) -> uintptr_t {
        memset(packet, 0, sizeof(RawInput));
        return 0;
    };
    const auto step = [&]() {
        memset(&raw, 0, sizeof(raw)); // the retail poll rebuilds only the raw packet
        sample.heartbeatTickMs = GetTickCount64();
        ++sample.frameId;
        HookBuild(state, record.data(), 0, 0);
        nativeUpdate(state, 1.0f/60.0f);
    };
    step();
    Check(state[0x10c+18] == 0 && state[0x172+18] == 0, "idle controller must not pause");
    sample.controllers[0].thumbstickX = 0.6f;
    sample.controllers[0].thumbstickY = 0.8f;
    sample.controllers[1].thumbstickX = -0.7f;
    step();
    Check(raw.axes[0][0] == 0.6f && raw.axes[0][1] == 0.8f &&
          raw.axes[2][0] == 0.6f && raw.axes[2][1] == 0.8f, "native locomotion axes");
    Check(raw.axes[1][0] == -0.7f && raw.mouseAxes[1] == 0, "native joystick look mode");
    Check(*reinterpret_cast<float*>(state+0x6c) == 0.6f, "native updater consumed movement");
    Check(state[0x10c+18] == 0, "stick movement must not pause");
    sample.controllers[0].thumbstickX = sample.controllers[0].thumbstickY = 0;
    sample.controllers[1].thumbstickX = 0;
    sample.controllers[1].buttons = PrimaryClick;
    step();
    Check(state[0x10c+2] && state[0x172+2], "A creates native interaction edge and hold");
    Check(state[0x10c+18] == 0 && state[0x10c+17] == 0, "A does not pause or open map");
    step();
    Check(state[0x10c+2] == 0 && state[0x172+2], "held A does not repeat press edge");
    sample.controllers[1].buttons = 0;
    step();
    Check(state[0x13f+2] && state[0x172+2] == 0, "A release is processed natively");
    sample.controllers[1].trigger = 1;
    sample.controllers[0].trigger = 1;
    step();
    Check(*reinterpret_cast<float*>(state+0x1a8+19*4) == 1.0f, "full-strength native action");
    Check(state[0x10c+19] && state[0x10c+20], "legacy input without rig metadata retains native actions");
    sample.controllers[1].activeFlags |= RigActionMetadata;
    sample.controllers[1].reserved[0] = 2;
    sample.controllers[0].trigger = 0;
    step();
    Check(raw.buttons[Guitar] && !raw.buttons[Axe], "selected guitar routes the original right trigger");
    sample.controllers[0].trigger = 1;
    step();
    Check(raw.buttons[Guitar] && !raw.buttons[Axe] && raw.buttons[Target], "target plus attack keeps selected guitar without Earthshaker");
    sample.controllers[0].buttons=PrimaryClick|SecondaryClick;step();
    Check(raw.buttons[Guitar] && !raw.buttons[Axe], "X+Y no longer requests Earthshaker");
    sample.controllers[0].buttons=0;
    sample.controllers[0].trigger=sample.controllers[1].trigger=0;
    sample.controllers[0].squeeze=1;sample.controllers[1].squeeze=0;step();
    Check(!raw.buttons[Axe]&&!raw.buttons[Guitar],"one grip cannot trigger Earthshaker");
    sample.controllers[1].squeeze=1;step();
    Check(state[0x10c+Axe]&&state[0x10c+Guitar]&&raw.buttons[Axe]&&raw.buttons[Guitar],
          "both grips create simultaneous native Earthshaker press edges");
    step();Check(!state[0x10c+Axe]&&!state[0x10c+Guitar],"held grips do not repeat Earthshaker edges");
    sample.controllers[0].squeeze=sample.controllers[1].squeeze=0;step();
    Check(state[0x13f+Axe]&&state[0x13f+Guitar],"releasing grips releases both native attacks");
    sample.controllers[0].squeeze=sample.controllers[1].squeeze=1;step();
    Check(state[0x10c+Axe]&&state[0x10c+Guitar],"another squeeze repeats the Earthshaker gesture");
    for(unsigned blocked : {SoloNotesMetadata,SoloRadialMetadata,DrivingMetadata}) {
        sample.controllers[1].activeFlags|=blocked;step();
        Check(!raw.buttons[Axe]&&!raw.buttons[Guitar],"Earthshaker cannot fire in a solo or vehicle");
        sample.controllers[1].activeFlags&=~blocked;
    }
    sample.controllers[0].buttons=ThumbstickPressed;step();
    Check(!raw.buttons[Axe]&&!raw.buttons[Guitar],"stage command chord suppresses Earthshaker");
    sample.controllers[0].buttons=MenuClick;step();
    Check(!raw.buttons[Axe]&&!raw.buttons[Guitar],"pause suppresses Earthshaker");
    sample.controllers[0].buttons=0;
    sample.controllers[0].squeeze=sample.controllers[1].squeeze=0;
    sample.controllers[0].trigger=.4f;sample.controllers[1].trigger=.8f;
    step();
    Check(raw.special[0]==204 && raw.special[1]==102, "vehicle gas and brake retain analog strength");
    sample.controllers[1].activeFlags|=DrivingMetadata;
    sample.controllers[0].buttons=ThumbstickPressed;
    step();
    Check(raw.buttons[Boost] && !raw.buttons[Axe] && !raw.buttons[Guitar] && !raw.buttons[RockStance],
          "vehicle nitro and throttle cannot swing weapons or open solos");
    sample.controllers[0].buttons=0;sample.controllers[0].thumbstickX=0;
    sample.controllers[1].activeFlags|=WheelSteeringMetadata;
    const float physicalWheel=.73f;std::memcpy(&sample.controllers[0].reserved[0],&physicalWheel,4);
    step();Check(std::fabs(raw.axes[2][0]-.73f)<.0001f&&raw.axes[0][0]==0,"physical right wheel steers gameplay without menu movement");
    sample.controllers[0].thumbstickX=-.8f;
    step();Check(raw.axes[2][0]==-.8f,"left stick overrides physical wheel");
    sample.controllers[0].thumbstickX=0;sample.controllers[1].activeFlags&=~WheelSteeringMetadata;
    sample.controllers[0].buttons=ThumbstickPressed;
    sample.controllers[1].activeFlags&=~DrivingMetadata;
    sample.controllers[0].squeeze=1;sample.controllers[1].buttons=0;
    step();
    Check(!raw.buttons[Boost]&&!raw.buttons[PlaylistRewind],"entering command chord cannot nitro or rewind playlist");
    sample.controllers[1].thumbstickX=.7f;sample.controllers[1].thumbstickY=.9f;step();
    Check(raw.buttons[OrderCharge]&&!raw.buttons[OrderMove]&&!raw.buttons[RockStance]&&!raw.buttons[Boost],
          "diagonal RTS order issues only dominant direction without solo or nitro");
    for(unsigned direction=0;direction<4;++direction) {
        sample.controllers[1].thumbstickX=sample.controllers[1].thumbstickY=0;step();
        sample.controllers[1].thumbstickX=direction==2?1.f:direction==3?-1.f:0.f;
        sample.controllers[1].thumbstickY=direction==0?1.f:direction==1?-1.f:0.f;step();
        for(unsigned order=0;order<4;++order)
            Check(bool(state[0x10c+OrderCharge+order])==(order==direction),"RTS stick creates exactly one native order edge");
        step();Check(!state[0x10c+OrderCharge+direction],"held RTS direction cannot repeat an order edge");
    }
    sample.controllers[1].thumbstickX=sample.controllers[1].thumbstickY=0;
    sample.controllers[1].buttons=PrimaryClick;step();
    Check(raw.buttons[BuildMenu]&&!raw.buttons[Use]&&!raw.buttons[Accept]&&!raw.buttons[Axe],
          "command A opens build without interaction, acceptance or attack");
    sample.controllers[1].buttons=0;sample.controllers[0].buttons=ThumbstickPressed|SecondaryClick;step();
    Check(raw.buttons[Fly]&&!raw.buttons[Guitar],"command Y toggles flight without guitar attack");
    sample.controllers[0].buttons=ThumbstickPressed;
    sample.controllers[0].trigger=1;sample.controllers[1].trigger=1;step();
    Check(raw.buttons[Descend]&&raw.buttons[Ascend]&&!raw.buttons[Axe]&&!raw.buttons[Guitar]&&
          !raw.buttons[RadialAccept]&&!raw.special[0]&&!raw.special[1],
          "flight triggers route vertically without throttle, attack or solo acceptance");
    sample.controllers[0].trigger=sample.controllers[1].trigger=0;
    sample.controllers[0].buttons=0;sample.controllers[0].squeeze=0;step();
    sample.controllers[1].buttons=PrimaryClick;step();
    Check(raw.buttons[Accept]&&!raw.buttons[BuildMenu],"after releasing command chord A confirms native build menu");
    sample.controllers[1].buttons=0;step();
    sample.controllers[0].buttons=ThumbstickPressed;sample.controllers[0].squeeze=1;
    sample.controllers[1].thumbstickX=sample.controllers[1].thumbstickY=0;
    sample.controllers[1].buttons=ThumbstickPressed;step();
    Check(raw.buttons[PlaylistRewind]&&!raw.buttons[RockStance],"command click rewinds without opening solo");
    sample.controllers[0].buttons=sample.controllers[1].buttons=0;
    Check(VrPromptText("/South/ and /West/")=="left grip + right grip together","authored Earthshaker prompt matches Touch combo");
    Check(VrPromptText("Hold /Block/. /Halford/ /unknown/")=="Hold B. /Halford/ /unknown/",
          "prompt remapping preserves unrelated slash-delimited authored content");
    {
        auto remapped=DefaultBindings;remapped.actions[Block].input=TI::A;
        TouchControls controls{};controls.a=true;
        Check(MapTouch(controls,remapped).buttons[Block]&&VrPromptText("Hold /Block/",remapped)=="Hold A",
              "input and native hints follow one binding change");
        controls.a=false;controls.b=true;
        Check(!MapTouch(controls,remapped).buttons[Block],"old binding is released after remap");
        Check(VrPromptText("/SoloNote2/")=="right-hand strum (or X)","solo hint includes physical strumming");
    }
    sample.controllers[1].activeFlags|=SoloNotesMetadata;
    sample.controllers[0].trigger=sample.controllers[1].trigger=0;
    sample.controllers[0].squeeze=1;
    for(unsigned note=0;note<3;++note) {
        sample.controllers[0].buttons=note==1?PrimaryClick:note==2?SecondaryClick:0;
        step();
        Check(!raw.buttons[SoloNote1]&&!raw.buttons[SoloNote2]&&!raw.buttons[SoloNote3],
              "holding a fret waits for the picking hand");
        for(int stroke=0;stroke<2;++stroke) {
            sample.controllers[1].reserved[1]=2;step();
            for(unsigned other=0;other<3;++other)
                Check(bool(state[0x10c+SoloNote1+other])==(note==other),"physical stroke creates only the selected native note edge");
            step();Check(!state[0x10c+SoloNote1+note],"one held physical pulse creates one note");
            sample.controllers[1].reserved[1]=0;step();
            Check(state[0x13f+SoloNote1+note]&&!state[0x172+SoloNote1+note],"physical pulse releases the selected fret for another stroke");
        }
    }
    sample.controllers[0].buttons=0;sample.controllers[0].squeeze=0;
    // A physical stroke chooses the authored note without a grip/fret chord.
    // Changing the upcoming native note during the pulse cannot play it early.
    expectedSoloNote=3;sample.controllers[1].reserved[1]=2;step();
    Check(raw.buttons[SoloNote3]&&!raw.buttons[SoloNote1]&&!raw.buttons[SoloNote2],"natural strum uses the next authored note");
    expectedSoloNote=2;step();
    Check(raw.buttons[SoloNote3]&&!raw.buttons[SoloNote2]&&!state[0x10c+SoloNote3],"one stroke remains one note as the native sequence advances");
    sample.controllers[1].reserved[1]=0;step();
    sample.controllers[1].reserved[1]=2;step();
    Check(state[0x10c+SoloNote2]&&!raw.buttons[SoloNote3],"next stroke uses the next authored note");
    sample.controllers[1].reserved[1]=0;expectedSoloNote=0;step();
    sample.controllers[1].activeFlags&=~SoloNotesMetadata;
    sample.controllers[1].activeFlags &= ~RigActionMetadata;
    sample.controllers[1].trigger = sample.controllers[0].trigger = 0;
    sample.controllers[0].buttons = MenuClick;
    step();
    Check(state[0x10c+18], "deliberate Menu press pauses");
    step();
    Check(state[0x10c+18] == 0, "held Menu does not repeat pause edge");
    sample.flags = SessionRunning; // tracking loss must release prior input
    step();
    Check(state[0x172+18] == 0 && state[0x13f+18], "controller loss releases Menu");
    sample.flags |= LeftControllerValid | RightControllerValid;
    sample.controllers[0].buttons = 0;
    sample.controllers[1].buttons = PrimaryClick;
    *reinterpret_cast<int32_t*>(base + 0xc12130) = -1;
    step();
    Check(*reinterpret_cast<int32_t*>(base + 0xc12130) == 0 && state[0x10c],
          "explicit A joins title player and creates accept edge");
    // Exhaust the actual controller combinations against the retail edge
    // updater. This catches dropped native actions, not just mapping helpers.
    bool covered[NativeActionCount]{};
    for(unsigned bits=0;bits<128;++bits) for(unsigned axes=0;axes<5;++axes) {
        sample.controllers[0].buttons=(bits&1?PrimaryClick:0)|(bits&2?SecondaryClick:0)|
            (bits&4?ThumbstickPressed:0)|(bits&8?MenuClick:0);
        sample.controllers[1].buttons=(bits&16?PrimaryClick:0)|(bits&32?SecondaryClick:0)|
            (bits&64?ThumbstickPressed:0);
        sample.controllers[0].thumbstickX=sample.controllers[1].thumbstickX=axes==1?-1.f:axes==2?1.f:0.f;
        sample.controllers[0].thumbstickY=sample.controllers[1].thumbstickY=axes==3?-1.f:axes==4?1.f:0.f;
        sample.controllers[0].trigger=sample.controllers[1].trigger=1;
        sample.controllers[0].squeeze=sample.controllers[1].squeeze=1;
        step();
        for(unsigned action=0;action<NativeActionCount;++action)
            covered[action]|=state[0x172+action]!=0;
    }
    for(unsigned action=0;action<NativeActionCount;++action) {
        if(!covered[action])fprintf(stderr,"Unreachable native action: %u\n",action);
        Check(covered[action],"all 51 native actions reachable through XR controller combinations");
    }
    sample.controllers[0].buttons=ThumbstickPressed|PrimaryClick;
    sample.controllers[1].buttons=0;
    step();
    Check(raw.buttons[Map] && !raw.buttons[Axe] && !raw.buttons[Use],"command X opens map without attacking or interacting");
    sample.controllers[0].buttons=0;sample.controllers[1].buttons=ThumbstickPressed;
    sample.controllers[1].thumbstickX=.75f;sample.controllers[1].thumbstickY=-.8f;
    step();
    Check(raw.buttons[RockStance] && raw.axes[5][0]==.75f && raw.axes[5][1]==-.8f,
          "solo radial selection retains its independent stick axes");
    sample.controllers[0].buttons=sample.controllers[1].buttons=0;
    sample.controllers[0].thumbstickX=sample.controllers[0].thumbstickY=0;
    sample.controllers[1].thumbstickX=sample.controllers[1].thumbstickY=0;
    sample.controllers[0].trigger=sample.controllers[1].trigger=0;
    sample.controllers[0].squeeze=sample.controllers[1].squeeze=0;
    step();
    for(unsigned action=0;action<NativeActionCount;++action)
        Check(!state[0x172+action],"all native actions release when XR controls return to neutral");
    // Exercise the actual gameplay caller, including its native controller
    // selection and scripted-input lock. Only the physical-device builder is
    // stubbed; desktop input is deliberately absent from this process.
    Check(MH_Initialize() == MH_OK, "initialize isolated test hook");
    void* builder = reinterpret_cast<void*>(base + 0x278060);
    Check(MH_CreateHook(builder, reinterpret_cast<void*>(&HookBuild), nullptr) == MH_OK &&
          MH_EnableHook(builder) == MH_OK, "route native gameplay packet through XR hook");
    auto nativeGameplay = reinterpret_cast<void(__stdcall*)(void*,unsigned)>(base + 0x278c90);
    // DONT_RESOLVE_DLL_REFERENCES intentionally leaves imports unbound.
    // RawInput's native constructor calls the retail memset thunk; bind only
    // that dependency in this private image instead of starting the game.
    void** memsetImport = reinterpret_cast<void**>(base + 0xa344f4);
    DWORD previousProtection = 0;
    Check(VirtualProtect(memsetImport, sizeof(void*), PAGE_READWRITE, &previousProtection) != 0,
          "bind isolated native memset import");
    *memsetImport = reinterpret_cast<void*>(&memset);
    VirtualProtect(memsetImport, sizeof(void*), previousProtection, &previousProtection);
    sample.controllers[0].thumbstickY = 0.8f;
    sample.controllers[1].trigger = 1.0f;
    sample.heartbeatTickMs = GetTickCount64();
    nativeGameplay(state, 0);
    Check(raw.axes[0][1] == 0.8f && raw.buttons[19] == 255,
          "real gameplay caller consumes XR without a desktop device");
    *reinterpret_cast<uint32_t*>(record.data()+0x490) = 1;
    nativeGameplay(state, 0);
    Check(raw.axes[0][1] == 0 && raw.buttons[19] == 0,
          "scripted input locks remain authoritative");
    *reinterpret_cast<uint32_t*>(record.data()+0x490) = 0;
    nativeGameplay(state, 1);
    Check(raw.axes[0][1] == 0 && raw.buttons[19] == 0, "other players receive no XR input");
    sample.heartbeatTickMs = GetTickCount64()-1000;
    nativeGameplay(state, 0);
    Check(raw.axes[0][1] == 0 && raw.buttons[19] == 0, "stale XR packets do not stick");
    MH_Uninitialize();
    puts("PASS: all 51 native actions, analog gas/brake, combo, radial selection, native edges/releases, gameplay caller, tracking loss, join, scripted locks, stale input");
    return 0;
}
