// Exercises the bridge against the legally installed game's own input updater.
// No game loop, XR runtime, desktop input, or shared-mailbox writes are used.
#include "../src/input/retail_input_bridge.cpp"
#include "../src/input/vr_prompt_text.h"
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
static unsigned expectedSoloNote=0;
namespace BLVR { unsigned CameraHook_GetSoloNextNote() {return expectedSoloNote;} }
static std::array<unsigned,256> uiKeyCounts{};
static bool __fastcall UiTestEvent(void*,void*,const uint32_t* event) {
    if(event[0]==5&&event[2]<uiKeyCounts.size())++uiKeyCounts[event[2]];
    return true;
}
static bool __fastcall UiTestVariable(void*,void*,const char*,void*,unsigned) {return true;}
static bool __fastcall UiTestArray(void*,void*,unsigned,const char*,unsigned,void*,unsigned,unsigned) {return true;}

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
    auto nativeRepeat=reinterpret_cast<bool(__cdecl*)(float,float,float,float)>(base+0x27bbd0);
    for(float dt : {.01f,1.f/60.f,.2f})for(unsigned tick=1;tick<=180;++tick) {
        const float elapsed=tick*dt;
        const bool actual=NativeUiRepeatDue(elapsed,dt),expected=nativeRepeat(elapsed,dt,.5f,4.f);
        if(actual!=expected)fprintf(stderr,"Repeat mismatch tick=%u elapsed=%a dt=%a expected=%d actual=%d\n",tick,elapsed,dt,expected,actual);
        Check(actual==expected,
              "UI dedup repeat timing matches the owned native predicate");
    }
    alignas(8) std::array<uint8_t, 0x494> record{};
    *reinterpret_cast<uintptr_t*>(base + 0xc11a88) = reinterpret_cast<uintptr_t>(record.data());
    *reinterpret_cast<uint32_t*>(base + 0xc11a80) = 1u << 6;
    *reinterpret_cast<int32_t*>(base + 0xc12130) = 0;
    auto* state = record.data() + 0x134;
    auto& raw = *reinterpret_cast<RawInput*>(state);
    PoseBridge sample{};
    sample.magic = Magic; sample.version = Version; sample.structBytes = sizeof(sample);
    sample.producerPid=GetCurrentProcessId();sample.producerEpoch=1;
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
    // Exercise the OWNED native Flash dispatcher with an inert GFx view. It
    // dispatches real native KeyDown events, without rendering or running game
    // code beyond the same isolated input functions used by this fixture.
    std::array<uintptr_t,34> uiVtable{};
    uiVtable[0x84/4]=reinterpret_cast<uintptr_t>(&UiTestEvent);
    uiVtable[0x2c/4]=reinterpret_cast<uintptr_t>(&UiTestVariable);
    uiVtable[0x34/4]=reinterpret_cast<uintptr_t>(&UiTestArray);
    uintptr_t uiView=reinterpret_cast<uintptr_t>(uiVtable.data());
    alignas(8) std::array<uint8_t,0x70> uiMovie{},uiOwner{};
    alignas(8) std::array<uint8_t,0x710> uiManager{};
    uintptr_t uiOwners[]={reinterpret_cast<uintptr_t>(uiOwner.data())};
    *reinterpret_cast<uintptr_t*>(uiMovie.data())=reinterpret_cast<uintptr_t>(&uiView);
    uiMovie[0x64]=1;uiOwner[8]=uiOwner[0xa]=uiOwner[0xb]=1;
    *reinterpret_cast<uintptr_t*>(uiOwner.data()+0x60)=reinterpret_cast<uintptr_t>(uiMovie.data());
    *reinterpret_cast<uintptr_t*>(uiManager.data()+4)=reinterpret_cast<uintptr_t>(uiOwner.data());
    *reinterpret_cast<unsigned*>(uiManager.data()+0xc)=1u<<6;
    *reinterpret_cast<uintptr_t*>(uiManager.data()+0x14)=reinterpret_cast<uintptr_t>(uiOwners);
    originalUiDispatch=base+0x69650;
    bool lastUiGuard=false;unsigned lastUiMask=0;
    const auto navigate=[&](bool arrows=true) {
        uiOwner[0xb]=arrows?1:0;uiMovie[0x68]=0;
        *reinterpret_cast<unsigned*>(uiMovie.data()+0x68)=*reinterpret_cast<unsigned*>(state+0x108)-1;
        std::memcpy(uiManager.data()+0x5c,state,NativeUiInputBytes);
        std::array<uint8_t,NativeUiInputBytes> before{};
        std::memcpy(before.data(),uiManager.data()+0x5c,before.size());
        *reinterpret_cast<uintptr_t*>(base+0xc09ca8)=reinterpret_cast<uintptr_t>(uiManager.data());
        UiDispatchState debugState{};
        lastUiGuard=ReadUiDispatchState(reinterpret_cast<uintptr_t>(uiOwner.data()),reinterpret_cast<uintptr_t>(uiManager.data()),base+0x694b6,&debugState);
        lastUiMask=lastUiGuard?PrepareUiDispatchCopy(debugState,sample,GetTickCount64()):0;
        uiKeyCounts.fill(0);
        DispatchMappedUi(reinterpret_cast<uintptr_t>(uiOwner.data()),reinterpret_cast<uintptr_t>(uiManager.data()),base+0x694b6);
        Check(std::memcmp(before.data(),uiManager.data()+0x5c,before.size())==0,
              "per-movie UI dedup never changes native gameplay/history/analog input");
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
    step(); // A fresh navigation edge, rather than the preceding .6 axis hold.
    sample.controllers[0].thumbstickX=1;step();navigate();
    if(uiKeyCounts[39]!=1||lastUiMask!=(1u<<UiRight)) {
        fprintf(stderr,"UI fresh guard=%d mask=%X raw0cmp=%d derivedcmp=%d\n",lastUiGuard,lastUiMask,
            memcmp(&uiIntent[0].raw,state,sizeof(RawInput)),memcmp(&uiIntent[0].raw,state+0x6c,sizeof(RawInput)));
        for(unsigned key=0;key<uiKeyCounts.size();++key)if(uiKeyCounts[key])fprintf(stderr,"UI key=%u count=%u\n",key,uiKeyCounts[key]);
    }
    Check(lastUiGuard&&lastUiMask==(1u<<UiRight),"fresh mapped direction activates the exact native packet-copy guard");
    Check(uiKeyCounts[39]==1,"full-strength VR UI direction creates exactly one native menu step");
    navigate(false);
    if(uiKeyCounts[39]!=1||uiKeyCounts[47]!=1)
        for(unsigned key=0;key<uiKeyCounts.size();++key)if(uiKeyCounts[key])fprintf(stderr,"Opening key=%u count=%u\n",key,uiKeyCounts[key]);
    Check(uiKeyCounts[39]==1&&uiKeyCounts[47]==1,
          "opening mode retains distinct digital direction and native carousel stick event");
    step();navigate();Check(!uiKeyCounts[39],"held direction does not repeat before native delay");
    for(unsigned n=0;n<40;++n) {step();navigate();Check(uiKeyCounts[39]<=1,"held native repeat never emits duplicate steps");}
    sample.controllers[0].thumbstickX=0;step();navigate();Check(!uiKeyCounts[39],"direction release stays released");
    sample.controllers[0].thumbstickX=.4f;step();navigate();
    Check(uiKeyCounts[39]==1&&!raw.buttons[UiRight],"partial stick keeps native axis-only navigation");
    UiDispatchState guarded{};
    Check(!ReadUiDispatchState(reinterpret_cast<uintptr_t>(uiOwner.data()),reinterpret_cast<uintptr_t>(uiManager.data()),base+0x694b5,&guarded),
          "unknown native caller refuses UI dedup");
    uiOwner[0xa]=0;
    Check(!ReadUiDispatchState(reinterpret_cast<uintptr_t>(uiOwner.data()),reinterpret_cast<uintptr_t>(uiManager.data()),base+0x694b6,&guarded),
          "inactive native input owner refuses UI dedup");uiOwner[0xa]=1;
    *reinterpret_cast<uintptr_t*>(uiOwner.data()+0xc)=1;
    Check(!ReadUiDispatchState(reinterpret_cast<uintptr_t>(uiOwner.data()),reinterpret_cast<uintptr_t>(uiManager.data()),base+0x694b6,&guarded),
          "unknown native device override refuses UI dedup");
    *reinterpret_cast<uintptr_t*>(uiOwner.data()+0xc)=0;
    sample.controllers[0].thumbstickX=0;step();
    sample.controllers[0].thumbstickX=1;step();
    std::memcpy(uiManager.data()+0x5c,state,NativeUiInputBytes);
    *reinterpret_cast<unsigned*>(uiMovie.data()+0x68)=*reinterpret_cast<unsigned*>(state+0x108)-1;
    Check(ReadUiDispatchState(reinterpret_cast<uintptr_t>(uiOwner.data()),reinterpret_cast<uintptr_t>(uiManager.data()),base+0x694b6,&guarded),
          "supported native owner/input join is eligible");
    auto originalPacket=guarded;
    PoseBridge foreignEpoch=sample;++foreignEpoch.producerEpoch;
    Check(!PrepareUiDispatchCopy(guarded,foreignEpoch,GetTickCount64())&&
          !std::memcmp(guarded.packet,originalPacket.packet,sizeof(guarded.packet)),
          "different host epoch refuses packet-copy filtering");
    PoseBridge lostHand=sample;lostHand.flags&=~LeftControllerValid;
    Check(!PrepareUiDispatchCopy(guarded,lostHand,GetTickCount64()),"lost source controller refuses stale direction filtering");
    guarded.packet[0]^=1;
    Check(!PrepareUiDispatchCopy(guarded,sample,GetTickCount64()),"unmatched native raw payload refuses filtering");
    guarded=originalPacket;
    Check(!PrepareUiDispatchCopy(guarded,sample,GetTickCount64()+1000),"old injected direction intent refuses filtering");
    sample.controllers[0].thumbstickX=0;step();
    sample.controllers[0].buttons = PrimaryClick;
    step();
    Check(state[0x10c+UiX]&&raw.buttons[UiX]&&!raw.buttons[UiA]&&!raw.buttons[Use],
          "X creates the separate menu tutorial action without selecting or interacting");
    sample.controllers[0].buttons=0;step();
    sample.controllers[1].buttons = PrimaryClick;
    step();
    Check(state[0x10c+Use] && state[0x172+Use]&&raw.buttons[UiA]&&!raw.buttons[UiX], "A creates native interaction/select edge without the tutorial action");
    Check(state[0x10c+18] == 0 && state[0x10c+17] == 0, "A does not pause or open map");
    step();
    Check(state[0x10c+Use] == 0 && state[0x172+Use], "held A does not repeat press edge");
    sample.controllers[1].buttons = 0;
    step();
    Check(state[0x13f+Use] && state[0x172+Use] == 0, "A release is processed natively");
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
    sample.controllers[1].trigger=0;sample.controllers[1].reserved[1]=1;step();
    Check(raw.buttons[Axe]&&state[0x10c+Axe],"host physical stroke produces a native attack edge");
    sample.flags&=~RightControllerValid;step();
    Check(!raw.buttons[Axe]&&!raw.buttons[Guitar]&&state[0x13f+Axe],"lost hand releases a retained physical stroke");
    sample.flags|=RightControllerValid;sample.controllers[1].reserved[1]=0;
    sample.controllers[1].activeFlags|=HostConfirmMetadata|HostAcceptMetadata|HostRadialMetadata;
    step();Check(raw.buttons[Accept]&&raw.buttons[RadialAccept]&&raw.buttons[RockStance],"host UI pulses reach native held state");
    sample.flags&=~RightControllerValid;step();
    Check(!raw.buttons[Accept]&&!raw.buttons[RadialAccept]&&!raw.buttons[RockStance]&&state[0x13f+Accept],
          "lost hand releases generated confirm, solo accept and radial pulses");
    sample.flags|=RightControllerValid;
    sample.controllers[1].activeFlags&=~(HostConfirmMetadata|HostAcceptMetadata|HostRadialMetadata);
    sample.controllers[1].trigger=1;
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
    sample.controllers[0].trigger=sample.controllers[1].trigger=0;
    sample.controllers[1].buttons=PrimaryClick;step();
    Check(raw.buttons[BuildMenu]&&raw.buttons[UiShoulderRight]&&!raw.buttons[Use]&&!raw.buttons[Accept]&&!raw.buttons[Axe],
          "first command A retains native build/menu routing before build feedback");
    sample.controllers[1].activeFlags|=BuildRadialMetadata;
    sample.controllers[1].thumbstickX=.65f;sample.controllers[1].thumbstickY=.8f;step();
    Check(raw.buttons[BuildMenu]&&!raw.buttons[RadialAccept]&&!raw.buttons[Use]&&!raw.buttons[UiA]&&
          !raw.buttons[UiShoulderRight]&&!raw.buttons[OrderCharge]&&!raw.buttons[PlaylistUI]&&
          raw.axes[5][0]==.65f&&raw.axes[5][1]==.8f,
          "held native build wheel selects radially without confirming or issuing an order");
    sample.controllers[1].trigger=1;step();
    Check(state[0x10c+RadialAccept]&&raw.buttons[BuildMenu]&&!raw.buttons[Use]&&!raw.buttons[UiA]&&
          !raw.buttons[Accept]&&!raw.buttons[Ascend]&&!raw.buttons[Axe]&&!raw.buttons[Guitar]&&!raw.special[0],
          "held build plus alternate confirm creates native recruitment edge without ascending or attacking");
    step();Check(!state[0x10c+RadialAccept]&&state[0x172+RadialAccept],"held recruitment confirmation cannot repeat native press edges");
    sample.controllers[1].trigger=0;step();
    Check(state[0x13f+RadialAccept]&&!state[0x172+RadialAccept],"recruitment confirm releases while native build remains held");
    sample.controllers[0].trigger=1;step();
    Check(state[0x10c+Use]&&!raw.buttons[RadialAccept]&&!raw.buttons[Descend]&&!raw.buttons[Target]&&!raw.buttons[UiA],
          "default left trigger requests a separate native research upgrade while build remains held");
    step();Check(!state[0x10c+Use]&&state[0x172+Use],"held default research alternate cannot repeat its press edge");
    sample.flags&=~LeftControllerValid;step();
    Check(state[0x13f+Use]&&!raw.buttons[BuildMenu]&&!raw.buttons[UiA]&&!raw.buttons[Accept],
          "lost command/research hand releases upgrade and build without converting held A into normal acceptance");
    sample.flags|=LeftControllerValid;sample.controllers[0].trigger=0;step();
    sample.controllers[0].trigger=1;step();
    sample.controllers[0].trigger=0;step();
    Check(state[0x13f+Use],"default research alternate releases natively without closing held build");
    sample.controllers[0].buttons=ThumbstickPressed|PrimaryClick;step();
    Check(state[0x10c+CancelBuildItem]&&raw.buttons[BuildMenu]&&!raw.buttons[Map]&&!raw.buttons[UiX],
          "X cancels a queued native build item without opening the map while held build is active");
    step();Check(!state[0x10c+CancelBuildItem],"held cancellation does not repeat its press edge");
    sample.controllers[0].buttons=ThumbstickPressed;step();
    Check(state[0x13f+CancelBuildItem],"queued cancellation releases natively");
    {
        const auto candidate=std::filesystem::temp_directory_path()/("blvr-build-input-"+std::to_string(GetCurrentProcessId())+".ini");
        wchar_t originalPath[32768]{};const auto originalLength=GetEnvironmentVariableW(L"BLVR_CONTROLS_FILE",originalPath,32768);
        {std::ofstream file(candidate);file<<"[controls]\nbuild_research_alternate=b\n[actions]\nUse=left_trigger\n";}
        SetEnvironmentVariableW(L"BLVR_CONTROLS_FILE",candidate.c_str());Sleep(300);
        sample.controllers[1].buttons=PrimaryClick|SecondaryClick;step();
        Check(state[0x10c+Use]&&!raw.buttons[RadialAccept]&&!raw.buttons[Beacon]&&!raw.buttons[UiB],
              "live remapped build research alternate creates native Use without a beacon or menu back");
        step();Check(!state[0x10c+Use]&&state[0x172+Use],"held remapped research alternate cannot repeat its native edge");
        sample.controllers[1].buttons=PrimaryClick;step();
        Check(state[0x13f+Use],"remapped research alternate releases while build remains held");
        sample.controllers[0].trigger=1;step();
        Check(state[0x10c+Use]&&!raw.buttons[RadialAccept]&&!raw.buttons[Descend]&&!raw.buttons[Target],
              "live remapped research input creates a separate native Use edge under held build");
        step();Check(!state[0x10c+Use]&&state[0x172+Use],"held research request cannot repeat its native edge");
        sample.controllers[0].trigger=0;step();
        Check(state[0x13f+Use],"research request releases natively while build remains held");
        {std::ofstream file(candidate);file<<"[controls]\nsolo_accept_alternate=left_grip\n"
            "build_research_alternate=left_stick_click\n[actions]\nRadialAccept=left_stick_click\n"
            "Use=left_grip\nCancelBuildItem=left_stick_click\n";}
        Sleep(300);
        sample.controllers[1].activeFlags&=~BuildRadialMetadata;step();
        sample.controllers[1].activeFlags|=BuildRadialMetadata;step();
        Check(raw.buttons[BuildMenu]&&!state[0x10c+RadialAccept]&&!state[0x10c+Use]&&!state[0x10c+CancelBuildItem]&&
              !state[0x172+RadialAccept]&&!state[0x172+Use]&&!state[0x172+CancelBuildItem],
              "opening feedback cannot create native actions from already held default command modifiers");
        step();Check(!raw.buttons[RadialAccept]&&!raw.buttons[Use]&&!raw.buttons[CancelBuildItem],
              "holding conflicting modifiers cannot recruit, research or cancel repeatedly");
        {std::ofstream file(candidate);file<<"[controls]\ncommand_grip=right_grip\ncommand_click=right_stick_click\n"
            "solo_accept_alternate=right_grip\nbuild_research_alternate=right_stick_click\n"
            "[actions]\nRadialAccept=right_stick_click\nUse=right_grip\nCancelBuildItem=right_stick_click\n";}
        Sleep(300);
        sample.controllers[0].buttons=0;
        sample.controllers[1].squeeze=1;sample.controllers[1].buttons=PrimaryClick|ThumbstickPressed;
        sample.controllers[1].activeFlags&=~BuildRadialMetadata;step();
        sample.controllers[1].activeFlags|=BuildRadialMetadata;step();
        Check(raw.buttons[BuildMenu]&&!state[0x10c+RadialAccept]&&!state[0x10c+Use]&&!state[0x10c+CancelBuildItem]&&
              !state[0x172+RadialAccept]&&!state[0x172+Use]&&!state[0x172+CancelBuildItem],
              "native opening edge protection follows live remapped command modifiers and alternates");
        // A missing original controls file intentionally retains the last good
        // layout. Restore defaults explicitly before removing our candidate.
        {std::ofstream file(candidate);file<<"[actions]\nUiRight=x\n";}
        sample.controllers[1].activeFlags&=~BuildRadialMetadata;
        sample.controllers[0].buttons=sample.controllers[1].buttons=0;
        sample.controllers[0].squeeze=sample.controllers[1].squeeze=0;
        sample.controllers[0].thumbstickX=sample.controllers[0].thumbstickY=0;
        sample.controllers[1].thumbstickX=sample.controllers[1].thumbstickY=0;
        sample.controllers[0].trigger=sample.controllers[1].trigger=0;
        Sleep(300);step();sample.controllers[0].buttons=PrimaryClick;step();navigate();
        Check(raw.buttons[UiRight]&&raw.axes[0][0]==1&&uiKeyCounts[39]==1,
              "runtime UiRight button remap produces exactly one native step");
        {std::ofstream file(candidate);file<<"[actions]\nUiRight=command+b\n";}
        sample.controllers[0].buttons=0;Sleep(300);step();
        sample.controllers[1].buttons=SecondaryClick;step();navigate();
        Check(!raw.buttons[UiRight]&&!uiKeyCounts[39],"command UiRight remap stays inactive without its chord");
        sample.controllers[0].squeeze=1;sample.controllers[0].buttons=ThumbstickPressed;
        step();navigate();Check(raw.buttons[UiRight]&&uiKeyCounts[39]==1,
              "runtime command UiRight remap survives stage aliases and deduplicates native navigation");
        sample.controllers[0].squeeze=0;sample.controllers[0].buttons=sample.controllers[1].buttons=0;
        step();navigate();Check(!uiKeyCounts[39],"remapped command direction releases normally");
        {std::ofstream file(candidate);file<<"[controls]\n";}
        Sleep(300);Check(ControlsSignature(ActiveBindings())==ControlsSignature(DefaultBindings),
              "temporary live remap fixture restores default bindings before teardown");
        SetEnvironmentVariableW(L"BLVR_CONTROLS_FILE",originalLength&&originalLength<32768?originalPath:nullptr);
        Sleep(300);std::filesystem::remove(candidate);
        sample.controllers[0].buttons=ThumbstickPressed;
        sample.controllers[1].buttons=PrimaryClick;sample.controllers[1].squeeze=0;
        sample.controllers[0].squeeze=1;sample.controllers[1].activeFlags|=BuildRadialMetadata;
    }
    sample.controllers[1].trigger=1;step();
    sample.flags&=~RightControllerValid;step();
    Check(state[0x13f+RadialAccept]&&!raw.buttons[RadialAccept]&&!raw.buttons[BuildMenu],
          "lost build hand releases both recruitment confirmation and held wheel request");
    sample.flags|=RightControllerValid;sample.controllers[1].trigger=0;
    sample.controllers[1].activeFlags&=~BuildRadialMetadata;
    sample.controllers[1].thumbstickX=sample.controllers[1].thumbstickY=0;
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
    Check(raw.buttons[Accept]&&!raw.buttons[BuildMenu],"after releasing command chord A requests normal acceptance without holding build");
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
    {
        const auto candidate=std::filesystem::temp_directory_path()/("blvr-scope-input-"+std::to_string(GetCurrentProcessId())+".ini");
        wchar_t originalPath[32768]{};const auto originalLength=GetEnvironmentVariableW(L"BLVR_CONTROLS_FILE",originalPath,32768);
        {std::ofstream file(candidate);file<<"[actions]\nPrimaryVehicleAttack=command+right_trigger\nSecondaryVehicleAttack=command+left_trigger\n";}
        SetEnvironmentVariableW(L"BLVR_CONTROLS_FILE",candidate.c_str());Sleep(300);
        sample.controllers[0].buttons=sample.controllers[1].buttons=0;
        sample.controllers[0].squeeze=sample.controllers[1].squeeze=0;
        sample.controllers[0].trigger=.25f;sample.controllers[1].trigger=.75f;step();
        Check(!raw.special[0]&&!raw.special[1]&&!raw.buttons[PrimaryVehicleAttack]&&!raw.buttons[SecondaryVehicleAttack],
              "native vehicle analog cannot bypass command-scoped attack bindings without their chord");
        sample.controllers[0].buttons=ThumbstickPressed;sample.controllers[0].squeeze=1;step();
        Check(!raw.special[0]&&!raw.special[1],"native command mode still reserves triggers from vehicle analog attack");
        {std::ofstream file(candidate);file<<"[actions]\nPrimaryVehicleAttack=left_trigger\nSecondaryVehicleAttack=right_trigger\n";}
        Sleep(300);sample.controllers[0].buttons=0;sample.controllers[0].squeeze=0;step();
        Check(raw.special[0]==uint8_t(.25f*255.f)&&raw.special[1]==uint8_t(.75f*255.f),
              "native remapped vehicle analog preserves partial trigger strength");
        {std::ofstream file(candidate);file<<"[actions]\nPrimaryVehicleAttack=none\nSecondaryVehicleAttack=none\n";}
        Sleep(300);step();
        Check(!raw.special[0]&&!raw.special[1]&&!raw.buttons[PrimaryVehicleAttack]&&!raw.buttons[SecondaryVehicleAttack],
              "native unbound vehicle attacks retain no hidden analog or digital triggers");
        {std::ofstream file(candidate);file<<"[actions]\nSoloNote2=command+x\nSoloNote3=command+y\n";}
        Sleep(300);sample.controllers[0].trigger=sample.controllers[1].trigger=0;
        sample.controllers[1].activeFlags|=SoloNotesMetadata;sample.controllers[0].squeeze=1;
        sample.controllers[0].buttons=PrimaryClick;sample.controllers[1].reserved[1]=0;step();
        sample.controllers[1].reserved[1]=2;step();
        Check(state[0x10c+SoloNote1]&&!raw.buttons[SoloNote2]&&!raw.buttons[SoloNote3],
              "physical strum fallback cannot borrow a command-bound note button without its chord");
        sample.controllers[0].buttons=SecondaryClick;step();
        Check(raw.buttons[SoloNote1]&&!state[0x10c+SoloNote1]&&!raw.buttons[SoloNote3],
              "held strum keeps its scoped fallback note as another remapped button changes");
        sample.controllers[1].reserved[1]=0;step();
        sample.controllers[1].reserved[1]=2;step();
        Check(state[0x10c+SoloNote1]&&!raw.buttons[SoloNote3],
              "new physical fallback stroke also ignores command-bound note three without its chord");
        sample.controllers[1].reserved[1]=0;step();expectedSoloNote=3;
        sample.controllers[1].reserved[1]=2;step();
        Check(state[0x10c+SoloNote3]&&!raw.buttons[SoloNote1],
              "scoped fallback preserves the authoritative authored next note");
        sample.controllers[1].reserved[1]=0;expectedSoloNote=0;step();
        sample.controllers[1].activeFlags&=~SoloNotesMetadata;
        sample.controllers[0].squeeze=0;sample.controllers[0].buttons=0;
        {std::ofstream file(candidate);file<<"[controls]\n";}
        Sleep(300);Check(ControlsSignature(ActiveBindings())==ControlsSignature(DefaultBindings),
              "native scope fixture restores default bindings before teardown");
        SetEnvironmentVariableW(L"BLVR_CONTROLS_FILE",originalLength&&originalLength<32768?originalPath:nullptr);
        Sleep(300);std::filesystem::remove(candidate);
    }
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
    logFile=tmpfile();Check(logFile!=nullptr,"open isolated tiny input-log budget fixture");
    inputLogLimit=512;inputLogBytes=0;inputLogStopped=false;
    for(unsigned n=0;n<100;++n)InputLog("input receipt %u\n",n);
    Check(inputLogStopped&&_ftelli64(logFile)<=512,"input diagnostics stop within their byte budget");
    const auto capped=_ftelli64(logFile);InputLog("must stay stopped\n");
    Check(_ftelli64(logFile)==capped,"input log stops growing after its terminal receipt");
    fclose(logFile);logFile=nullptr;inputLogLimit=InputLogHardLimit;
    puts("PASS: all 51 native actions, analog gas/brake, combo, radial selection, native edges/releases, gameplay caller, tracking loss, join, scripted locks, stale input");
    return 0;
}
