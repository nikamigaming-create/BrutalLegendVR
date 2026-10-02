// Execute the owned retail Flash alias formatter without launching the game.
// This covers the UTF-16 path used by *LINECODE floating HUD notifications,
// which bypasses the previously tested UTF-8 text setters.
#include "../src/input/native_prompt_format.h"
#include "../src/input/touch_controls.h"
#include "MinHook.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <filesystem>

namespace {
void Check(bool value,const char* message) {
    if(!value) {std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}
}
using Format=void(__stdcall*)(void**);
Format originalFormat=nullptr;
BLVR::NativePromptAssign assignText=nullptr;
unsigned remaps=0,allocations=0;
bool buildContext=false;
uintptr_t retailBase=0;
const char* activeCase="setup";
void __stdcall Hook(void** text) {
    if(BLVR::RemapNativePrompt(text,assignText,nullptr,buildContext))++remaps;
    originalFormat(text);
}
// Only the isolated formatter's allocator is supplied. The game's entry
// point, missions, input, save system and XR runtime never execute.
void* __fastcall Allocate(void*,void*,size_t bytes) {
    void* result=HeapAlloc(GetProcessHeap(),0,bytes);
    if(result)++allocations;
    return result;
}
void __fastcall Release(void*,void*,void* value) {
    if(value) {Check(allocations>0,"balanced retail allocations");--allocations;HeapFree(GetProcessHeap(),0,value);}
}
void* __fastcall Reallocate(void*,void*,void* value,size_t bytes) {
    return value?HeapReAlloc(GetProcessHeap(),0,value,bytes):Allocate(nullptr,nullptr,bytes);
}
void PatchPointer(uintptr_t address,void* pointer) {
    DWORD previous=0;
    Check(VirtualProtect(reinterpret_cast<void*>(address),sizeof(pointer),PAGE_READWRITE,&previous)!=0,"fixture pointer writable");
    std::memcpy(reinterpret_cast<void*>(address),&pointer,sizeof(pointer));
    DWORD ignored=0;VirtualProtect(reinterpret_cast<void*>(address),sizeof(pointer),previous,&ignored);
}
void BindRuntimeImports(uintptr_t base) {
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS*>(base+dos->e_lfanew);
    const auto* library=reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base+
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
    for(;library->Name;++library) {
        const auto* name=reinterpret_cast<const char*>(base+library->Name);
        if(_stricmp(name,"KERNEL32.dll")&&_stricmp(name,"MSVCR100.dll"))continue;
        HMODULE module=LoadLibraryA(name);Check(module!=nullptr,"fixture CRT/system library");
        const auto* symbols=reinterpret_cast<const IMAGE_THUNK_DATA*>(base+library->OriginalFirstThunk);
        for(unsigned i=0;symbols[i].u1.AddressOfData;++i) {
            const auto symbol=symbols[i].u1.AddressOfData;
            const char* function=IMAGE_SNAP_BY_ORDINAL(symbol)?MAKEINTRESOURCEA(IMAGE_ORDINAL(symbol)):
                reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base+symbol)->Name;
            FARPROC address=GetProcAddress(module,function);Check(address!=nullptr,"fixture runtime import");
            PatchPointer(base+library->FirstThunk+i*sizeof(void*),reinterpret_cast<void*>(address));
        }
    }
}
struct WideText {
    wchar_t* text;
    unsigned length;
    wchar_t* externalBuffer;
    unsigned capacity;
};
}

int wmain(int argc,wchar_t** argv) {
    SetUnhandledExceptionFilter([](EXCEPTION_POINTERS* fault)->LONG {
        HMODULE owner=nullptr;wchar_t modulePath[MAX_PATH]{};
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          reinterpret_cast<LPCWSTR>(fault->ExceptionRecord->ExceptionAddress),&owner);
        if(owner)GetModuleFileNameW(owner,modulePath,MAX_PATH);
        std::fwprintf(stderr,L"Fault module=%s offset=%08X access=%u address=%p\n",modulePath,
            unsigned(reinterpret_cast<uintptr_t>(fault->ExceptionRecord->ExceptionAddress)-reinterpret_cast<uintptr_t>(owner)),
            unsigned(fault->ExceptionRecord->ExceptionInformation[0]),reinterpret_cast<void*>(fault->ExceptionRecord->ExceptionInformation[1]));
        std::fprintf(stderr,"Native prompt exception %08lX at %p (RVA %08X), case=%s, EAX=%08lX ECX=%08lX EDX=%08lX\n",
            fault->ExceptionRecord->ExceptionCode,fault->ExceptionRecord->ExceptionAddress,
            unsigned(reinterpret_cast<uintptr_t>(fault->ExceptionRecord->ExceptionAddress)-retailBase),
            activeCase,fault->ContextRecord->Eax,fault->ContextRecord->Ecx,fault->ContextRecord->Edx);
        return EXCEPTION_EXECUTE_HANDLER;
    });
    using namespace BLVR;
    Check(argc>=2,"provide installed BrutalLegend.exe path; optional local StringTable");
    Check(VrPromptText("Earthshaker (/South/ + /West/)")=="Earthshaker (left grip + right grip together)","legacy Earthshaker chord");
    Check(VrPromptText("Press /AxeAttack/ and /GuitarAttack/.")=="Press left grip + right grip together.","semantic Earthshaker chord");
    Check(VrPromptText("/AxeAttack//GuitarAttack//AxeAttack/")==
          "axe (X): right trigger; guitar (Y): right trigger; axe (X): right trigger","Grand Slam retains weapon order");
    Check(VrPromptText("/AxeAttack/ /AxeAttack/ /AxeAttack/  /GuitarAttack/")==
          "axe (X): right trigger x3; guitar (Y): right trigger","Rock Kick retains every hit without stowing the axe");
    Check(VrPromptText("Press /South/ to accept, /East/ to reject.")=="Press A to accept, B to reject.","menu confirmation is not an axe attack");
    Check(VrPromptText("Pyro: Hold /West/")=="Pyro: Hold right trigger (guitar: Y)","legacy guitar requires selected guitar");
    Check(VrPromptText("Press /Map/")=="Press left grip + left stick click + X","inline command explains its full chord");
    Check(VrPromptText("While locked on an enemy, press /Dodge/ and /MovementControls/ to dodge in any direction.")==
          "While locked on an enemy, press B + left stick to dodge in any direction.",
          "owned lock-on dodge template names its direction input once");
    {
        auto dodge=DefaultBindings;dodge.actions[Evade]={TI::LeftTrigger};dodge.movementStick=TouchStick::Right;
        Check(VrPromptText("While locked on an enemy, press /Dodge/ and /MovementControls/ to dodge in any direction.",dodge)==
              "While locked on an enemy, press left trigger + right stick to dodge in any direction.",
              "lock-on dodge combo follows remapped evade and movement roles");
        Check(VrPromptText("Use /Dodge/ in any direction.",dodge)=="Use left trigger + right stick in any direction.",
              "standalone Dodge keeps its directional movement instruction");
    }
    Check(VrPromptText("Hold /GuitarAttack/ to fire secondary weapons.")=="Hold left trigger to fire secondary weapons.","mounted secondary fire uses the vehicle action");
    Check(VrPromptText("/Halford/ /bleep/ /unknown/")=="/Halford/ /bleep/ /unknown/","non-input authored markup stays intact");
    Check(VrPromptText("/kBI_UI_A/ Select; /kBI_UI_B/ Back; /kBI_UI_X/ Tutorial")==
          "A Select; B Back; X Tutorial","native front-end enum hints match separate select/back/tutorial controls");
    Check(VrPromptText("/kBI_RadialAccept/ recruit; /kBI_Use/ research",DefaultBindings,true)==
          "right trigger recruit; left trigger research","native build hints identify usable confirmation inputs instead of held A");
    Check(VrPromptText("/kBI_RadialAccept/ select; /kBI_Use/ interact",DefaultBindings,false)==
          "A select; A interact","ordinary native Use and solo confirmation remain unchanged outside build context");
    Check(VrPromptText("/LeftStick/ TO SELECT A SQUAD; /Accept/ TO RECRUIT; /Accept/ TO UPGRADE; /Use/ research",DefaultBindings,true)==
          "right stick TO SELECT A SQUAD; right trigger TO RECRUIT; right trigger TO UPGRADE; left trigger research",
          "native build templates use radial stick and confirmation while research remains distinct");
    const auto cachedBuildCaption=VrPromptText("/Accept/ TO RECRUIT",DefaultBindings,true);
    Check(cachedBuildCaption=="right trigger TO RECRUIT / UPGRADE",
          "cached native recruitment template names both action25 purposes before any selection");
    Check(VrPromptText("/RadialAccept/ TO RECRUIT",DefaultBindings,true)==cachedBuildCaption,
          "exact live UTF-16 formatter source names both primary build purposes");
    Check(VrPromptText(cachedBuildCaption,DefaultBindings,true)==cachedBuildCaption,
          "already formatted build caption retains both purposes without selection feedback");
    Check(VrPromptText("/Accept/ TO RECRUIT",DefaultBindings,false)=="A TO RECRUIT",
          "ordinary confirmation wording stays unchanged outside an actual native build wheel");
    Check(VrPromptText("/RadialAccept/ TO RECRUIT",DefaultBindings,false)=="A TO RECRUIT",
          "live radial alias retains ordinary wording outside the native build wheel");
    Check(VrPromptText("RECRUIT /Accept/; /Use/ research",DefaultBindings,true)=="RECRUIT right trigger; left trigger research",
          "only the exact native template changes, leaving authored wording and research intact");
    Check(VrPromptText("Hint: /RadialAccept/ TO RECRUIT",DefaultBindings,true)=="Hint: right trigger TO RECRUIT",
          "observed radial alias does not widen shared wording to general authored text");
    {
        auto tier=DefaultBindings;tier.actions[RadialAccept]={TI::B};tier.soloAcceptAlternate=TI::None;
        TouchControls held{};held.lg=1;held.leftClick=held.a=held.b=true;held.buildRadial=true;
        Check(VrPromptText("/RadialAccept/ TO RECRUIT",tier,true)=="B TO RECRUIT / UPGRADE"&&
              MapTouch(held,tier).buttons[RadialAccept]&&!MapTouch(held,tier).buttons[Use],
              "shared recruitment/upgrade caption follows configured action25 without switching to research16");
        tier.actions[RadialAccept]={TI::A};
        Check(VrPromptText("/RadialAccept/ TO RECRUIT",tier,true)=="unbound TO RECRUIT / UPGRADE",
              "shared build caption retains required-held-input conflict protection");
    }
    Check(VrPromptText("/CancelBuildItem/ TO CANCEL; /kBI_CancelBuildItem/",DefaultBindings,true)=="X TO CANCEL; X",
          "held build cancellation names the usable button without repeating the held chord");
    Check(VrPromptText("/CancelBuildItem/",DefaultBindings,false)=="X",
          "ordinary cancellation retains its configured unscoped input");
    {
        auto modal=DefaultBindings;modal.actions[CancelBuildItem]={TI::Y,true};
        Check(VrPromptText("/CancelBuildItem/",modal,false)=="left grip + left stick click + Y",
              "command-scoped cancellation outside build retains its required chord");
        Check(VrPromptText("/CancelBuildItem/",modal,true)=="Y","build cancellation hint follows its logical remap");
        modal.actions[CancelBuildItem].input=modal.actions[BuildMenu].input;
        Check(VrPromptText("/CancelBuildItem/",modal,true)==TouchLabel(TI::None),
              "build cancellation hint identifies an unusable held-input conflict");
        modal.actions[CancelBuildItem]={TI::Y,true};modal.actions[BuildMenu].command=false;
        Check(VrPromptText("/CancelBuildItem/",modal,true)==TouchLabel(TI::None),
              "build cancellation hint does not advertise an unavailable command scope");
        modal=DefaultBindings;modal.commandGrip=TI::RightGrip;modal.commandClick=TI::RightClick;
        modal.actions[RadialAccept]={TI::RightClick};modal.soloAcceptAlternate=TI::RightGrip;
        modal.actions[Use]={TI::RightGrip};modal.buildResearchAlternate=TI::RightClick;
        modal.actions[CancelBuildItem]={TI::RightClick};
        Check(VrPromptText("/RadialAccept/ /Use/ /CancelBuildItem/",modal,true)=="unbound unbound unbound",
              "build hints reject remapped required modifiers for primary and alternate inputs");
    }
    Check(VrPromptText("/kBI_BuildMenu/ /kBI_Fly/ /kBI_Ascend/ /kBI_Descend/")==
          "left grip + left stick click + A left grip + left stick click + Y left grip + left stick click + right trigger left grip + left stick click + left trigger",
          "native stage enum hints spell out their command context");
    Check(VrPromptText("/kBI_PrimaryMeleeAttack/ + /kBI_SecondaryMeleeAttack/")=="left grip + right grip together",
          "native melee enum chord retains Earthshaker semantics");
    Check(VrPromptText("/kBI_UnknownAction/ /kSI_Unknown/ /kAI_Unknown/")=="/kBI_UnknownAction/ /kSI_Unknown/ /kAI_Unknown/",
          "unknown native markup remains untouched");
    const auto combo=VrPromptText("/South//West//South/");
    Check(VrPromptText(combo)==combo&&combo.find('/')==std::string::npos,"generated combo survives repeated/native formatting");
    auto changed=DefaultBindings;changed.equipAxe=TI::Y;changed.equipGuitar=TI::X;
    changed.actions[Block].input=TI::A;
    TouchControls input{};input.a=true;
    Check(MapTouch(input,changed).buttons[Block]&&VrPromptText("/Block/",changed)=="A","hint and input share remapped action");
    Check(VrPromptText("/South/ + /West/",changed)=="left grip + right grip together","equipment remap does not change Earthshaker");
    changed.earthshakerLeft=TI::X;changed.earthshakerRight=TI::Y;
    input={};input.x=input.y=true;
    Check(MapTouch(input,changed).buttons[Axe]&&MapTouch(input,changed).buttons[Guitar]&&
          VrPromptText("/South/ + /West/",changed)=="X + Y together","combo input and hints share the dedicated chord binding");
    changed.movementStick=TouchStick::Right;
    changed.turnStick=changed.radialStick=TouchStick::Left;
    changed.wheelGrip=TI::LeftTrigger;
    changed.actions[UiA].input=TI::LeftTrigger;changed.actions[UiB].input=TI::RightTrigger;changed.actions[UiX].input=TI::Y;
    Check(VrPromptText("/kBI_UI_A/ /kBI_UI_B/ /kBI_UI_X/",changed)=="left trigger right trigger Y",
          "native menu enum aliases use logical remaps rather than physical gamepad letters");
    Check(VrPromptText("/kSI_Move/ /kSI_Look/ /kSI_RadialMenu/ /kSI_SwitchTarget/ /kAI_Gas/ /kAI_Brake/",changed)==
          "right stick head movement and left stick snap turn left stick left stick while targeting right trigger left trigger",
          "native stick and analog aliases follow remapped semantic roles");
    Check(VrPromptText("/MovementControls/ /CameraControls/ /RadialSelect/ /TargetSwitch/ /DrivingControls/",changed)==
          "right stick head movement and left stick snap turn left stick left stick while targeting left trigger + hand turn, or right stick",
          "all semantic stick hints follow runtime remapping");

    HMODULE retail=LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
    Check(retail!=nullptr,"map owned executable without startup");
    const uintptr_t base=reinterpret_cast<uintptr_t>(retail);
    retailBase=base;
    const uint8_t formatterBytes[]{0x55,0x8b,0xec,0x8b,0x45,0x08,0x8b,0x00,0x8b,0x00,0x83,0xec,0x40};
    const uint8_t assignBytes[]{0x55,0x8b,0xec,0x53,0x8b,0x5d,0x08,0x56,0x57,0x6a,0xff};
    Check(!std::memcmp(reinterpret_cast<void*>(base+0x67810),formatterBytes,sizeof(formatterBytes)),"verified retail formatter");
    Check(!std::memcmp(reinterpret_cast<void*>(base+0x6a1510),assignBytes,sizeof(assignBytes)),"verified native UTF-8 assignment");
    void* heapVtable[]{nullptr,reinterpret_cast<void*>(&Allocate),reinterpret_cast<void*>(&Release),reinterpret_cast<void*>(&Reallocate)};
    void** heap=heapVtable;
    PatchPointer(base+0xb5c000,&heap);
    BindRuntimeImports(base);
    assignText=reinterpret_cast<NativePromptAssign>(base+0x6a1510);
    auto format=reinterpret_cast<Format>(base+0x67810);
    Check(MH_Initialize()==MH_OK,"initialize fixture hook");
    Check(MH_CreateHook(reinterpret_cast<void*>(format),reinterpret_cast<void*>(&Hook),reinterpret_cast<void**>(&originalFormat))==MH_OK,"create exact formatter hook");
    Check(MH_EnableHook(reinterpret_cast<void*>(format))==MH_OK,"enable formatter hook");

    unsigned cases=0;
    const auto native=[&](const std::string& source,const std::string& expected) {
        activeCase=source.c_str();
        std::array<wchar_t,4096> buffer{};
        WideText value{buffer.data(),0,buffer.data(),static_cast<unsigned>(buffer.size())};
        assignText(&value,source.c_str());
        void* reference=&value;format(&reference);
        std::array<char,16384> output{};
        Check(WideCharToMultiByte(CP_UTF8,0,value.text,-1,output.data(),static_cast<int>(output.size()),nullptr,nullptr)>0,"native output is UTF-16");
        if(output.data()!=expected)std::fprintf(stderr,"source: %s\nactual: %s\nexpected: %s\n",source.c_str(),output.data(),expected.c_str());
        Check(output.data()==expected,"real Flash formatter preserves Quest controls");
        Check(allocations==0,"native formatter released temporary strings");
        ++cases;
    };
    native("Earthshaker (/South/ + /West/) can repel enemies.","Earthshaker (left grip + right grip together) can repel enemies.");
    native("Press /Block/ to block.","Press B to block.");
    native("/AxeAttack//GuitarAttack//AxeAttack/","axe (X): right trigger; guitar (Y): right trigger; axe (X): right trigger");
    native("Press /South/ to select","Press A to select");
    native("Press /Map/","Press left grip + left stick click + X");
    native("While locked on an enemy, press /Dodge/ and /MovementControls/ to dodge in any direction.",
        "While locked on an enemy, press B + left stick to dodge in any direction.");
    native(u8"Br\u00fctal Legend: /Activate/",u8"Br\u00fctal Legend: A");
    native("Already mapped: X + Y together","Already mapped: X + Y together");
    native("Walk /MovementControls/; turn /CameraControls/.","Walk left stick; turn head movement and right stick snap turn.");
    native("/kBI_UI_X/ Tutorial  /kBI_UI_A/ Select  /kBI_UI_B/ Back","X Tutorial  A Select  B Back");
    native("/kBI_UI_TriggerLeft/ /kBI_UI_TriggerRight/","left grip + left stick click + left trigger left grip + left stick click + right trigger");
    native("/kBI_BuildMenu/ /kBI_Fly/","left grip + left stick click + A left grip + left stick click + Y");
    native("/kSI_Move/ /kAI_Gas/ /kAI_Brake/","left stick right trigger left trigger");
    buildContext=true;
    native("/kBI_RadialAccept/ recruit; /kBI_Use/ research","right trigger recruit; left trigger research");
    native("/RadialAccept/ recruit; /Use/ research","right trigger recruit; left trigger research");
    native("/LeftStick/ TO SELECT A SQUAD","right stick TO SELECT A SQUAD");
    native("/Accept/ TO RECRUIT","right trigger TO RECRUIT / UPGRADE");
    native("/RadialAccept/ TO RECRUIT","right trigger TO RECRUIT / UPGRADE");
    native("/Accept/ TO UPGRADE","right trigger TO UPGRADE");
    native("right trigger TO RECRUIT / UPGRADE","right trigger TO RECRUIT / UPGRADE");
    native("/CancelBuildItem/ TO CANCEL","X TO CANCEL");
    native("/kBI_CancelBuildItem/ TO CANCEL","X TO CANCEL");
    buildContext=false;
    native("/RadialAccept/ TO RECRUIT","A TO RECRUIT");
    native("/kBI_Use/ interact","A interact");
    {
        const auto candidate=std::filesystem::temp_directory_path()/("blvr-menu-hints-"+std::to_string(GetCurrentProcessId())+".ini");
        wchar_t originalPath[32768]{};const auto originalLength=GetEnvironmentVariableW(L"BLVR_CONTROLS_FILE",originalPath,32768);
        {std::ofstream file(candidate);file<<"[actions]\nUiA=left_trigger\nUiB=right_trigger\nUiX=y\n";}
        SetEnvironmentVariableW(L"BLVR_CONTROLS_FILE",candidate.c_str());Sleep(300);
        native("/kBI_UI_X/ Tutorial  /kBI_UI_A/ Select  /kBI_UI_B/ Back","Y Tutorial  left trigger Select  right trigger Back");
        SetEnvironmentVariableW(L"BLVR_CONTROLS_FILE",originalLength&&originalLength<32768?originalPath:nullptr);
        Sleep(300);std::filesystem::remove(candidate);
    }
    // Exercise locally owned corpus text; never check in the game's strings.
    if(argc>=3) {
        std::ifstream stream(argv[2],std::ios::binary);
        Check(bool(stream),"open local StringTable");
        const std::string corpus((std::istreambuf_iterator<char>(stream)),{});
        size_t at=0;
        while((at=corpus.find("=LineCodeData{Text=",at))!=std::string::npos) {
            at+=19;const size_t end=corpus.find(";VolumeDB=",at);if(end==std::string::npos)break;
            std::string line=corpus.substr(at,end-at);at=end;
            if(line.size()>1&&line.front()=='"'&&line.back()=='"')line=line.substr(1,line.size()-2);
            const auto expected=VrPromptText(line);
            if(expected==line||expected.find('/')!=std::string::npos)continue;
            native(line,expected);
        }
    }
    Check(remaps>=6&&cases>=7,"UTF-16 hook exercised");
    MH_DisableHook(MH_ALL_HOOKS);MH_Uninitialize();
    std::printf("PASS: Quest hint semantics and %u real retail Flash formatter cases (%u remaps)\n",cases,remaps);
    FreeLibrary(retail);
}
