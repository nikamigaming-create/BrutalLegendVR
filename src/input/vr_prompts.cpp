#include "vr_prompts.h"
#include "vr_prompt_text.h"
#include "native_prompt_format.h"
#include "../bridge/blvr_prompt_bridge.h"
#include "../camera/camera_hook.h"
#include "../diagnostics/log.h"
#include "MinHook.h"
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <mutex>
#include <unordered_set>

namespace BLVR { namespace {
using ShowCard=bool(__stdcall*)(void*,const void*);
using SetText=void(__stdcall*)(void*,const void*,const char*);
using SetIndexedText=void(__stdcall*)(void*,const void*,unsigned,const char*);
using SetLiteralText=void(__stdcall*)(void*,const char*,const char*);
// The retail StringID lookup takes its sole argument in EDX, returns a stable
// UTF-8 string in EAX, and does not pop any stack arguments.
using LocalizedText=const char*(__fastcall*)(void*,const unsigned*);
ShowCard originalShow=nullptr;
SetText originalText=nullptr;
SetIndexedText originalIndexed=nullptr;
SetLiteralText originalLiteral=nullptr;
LocalizedText originalLocalized=nullptr;
using FormatAliases=void(__stdcall*)(void**);
FormatAliases originalFormat=nullptr;
NativePromptAssign nativeAssign=nullptr;
thread_local bool showingCard=false;
thread_local std::string cardTitle,cardBody;
std::mutex promptMutex;
void* tutorialHud=nullptr;
const void* tutorialMovie=nullptr;
std::string title,body;
HANDLE mapping=nullptr;
blvr_prompt_bridge::Snapshot* shared=nullptr;
std::mutex localizedMutex;
std::unordered_set<std::string> localizedStrings;
std::unordered_set<std::string> loggedHints;

// Keep guarded native reads in a plain leaf: the logging function's lazy
// diagnostic initialization requires C++ unwinding, which MSVC forbids in SEH.
__declspec(noinline) bool ReadBuildHintSource(void** text,wchar_t* units,
    unsigned& count,bool& truncated) {
    __try {
        if(!text||!*text)return false;
        const wchar_t* source=*static_cast<const wchar_t* const*>(*text);
        if(!source)return false;
        while(count<128&&source[count]) {units[count]=source[count];++count;}
        truncated=count==128&&source[count]!=0;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

// Read-only, explicitly enabled and bounded to four original formatter inputs.
// The exact UTF-16 units expose alias/markup changes made before this hook.
void LogBuildHintSource(void** text,bool buildContext) {
    static const bool enabled=[] {
        char value[16]{};
        GetEnvironmentVariableA("BLVR_BUILD_HINT_DIAGNOSTIC",value,sizeof(value));
        return std::strcmp(value,"1")==0||_stricmp(value,"true")==0;
    }();
    static volatile LONG samples=0;
    if(!enabled||samples>=4)return;
    wchar_t units[129]{};
    unsigned count=0;bool truncated=false;
    if(!ReadBuildHintSource(text,units,count,truncated))return;
    if(!std::wcsstr(units,L"TO RECRUIT"))return;
    const LONG sample=InterlockedIncrement(&samples);
    if(sample>4)return;
    char utf8[385]{},hex[641]{};
    WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,units,-1,utf8,sizeof(utf8),nullptr,nullptr);
    for(unsigned i=0;i<count;++i)
        std::snprintf(hex+i*5,sizeof(hex)-i*5,"%04X ",static_cast<unsigned>(units[i]));
    Log("Build hint source sample=%ld build=%u units=%u truncated=%u utf8='%s' codeunits=[%s]",
        sample,buildContext?1u:0u,count,truncated?1u:0u,utf8,hex);
}

void __stdcall HookFormat(void** text) {
    std::string rendered;
    const bool buildContext=CameraHook_IsBuildUiOpen();
    LogBuildHintSource(text,buildContext);
    if(RemapNativePrompt(text,nativeAssign,&rendered,buildContext)) {
        std::lock_guard<std::mutex> lock(localizedMutex);
        if(loggedHints.size()<48&&loggedHints.emplace(rendered).second)
            Log("Quest hint: %s",rendered.c_str());
    }
    // Keep retail formatting (including non-input markup) after replacing the
    // controls. This also catches *LINECODE text resolved inside Flash, where
    // the StringID lookup is inlined and bypasses HookLocalized/HookText.
    originalFormat(text);
}

// Verified retail Name: the Name value points to an intern record whose first
// word is a NUL-terminated string; +8 is its reference count.
const char* NameText(const void* name) {
    __try {return name?**reinterpret_cast<const char* const* const*>(name):nullptr;}
    __except(EXCEPTION_EXECUTE_HANDLER) {return nullptr;}
}
bool ActiveCard() {
    __try {
        // +0 is the current movie and becomes null in HideTutorialCard.
        // +0x14 is the history ARRAY of shown names, not the current Name.
        return tutorialHud && tutorialMovie && *static_cast<void**>(tutorialHud)==tutorialMovie;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
void __stdcall HookText(void* movie,const void* name,const char* source) {
    if(!source) {originalText(movie,name,source);return;}
    const std::string replaced=VrPromptText(source,ActiveBindings(),CameraHook_IsBuildUiOpen());
    if(showingCard) {
        const char* property=NameText(name);
        if(property&&std::strcmp(property,"cardname")==0)cardTitle=replaced;
    }
    originalText(movie,name,replaced.c_str());
}
void __stdcall HookIndexed(void* movie,const void* name,unsigned index,const char* source) {
    if(!source) {originalIndexed(movie,name,index,source);return;}
    const std::string replaced=VrPromptText(source,ActiveBindings(),CameraHook_IsBuildUiOpen());
    if(showingCard) {
        const char* property=NameText(name);
        if(property&&std::strcmp(property,"info")==0) {
            if(!cardBody.empty())cardBody+="\n\n";
            cardBody+=replaced;
        }
    }
    originalIndexed(movie,name,index,replaced.c_str());
}
void __stdcall HookLiteral(void* movie,const char* property,const char* source) {
    if(!source) {originalLiteral(movie,property,source);return;}
    const std::string replaced=VrPromptText(source,ActiveBindings(),CameraHook_IsBuildUiOpen());
    originalLiteral(movie,property,replaced.c_str());
}
const char* __fastcall HookLocalized(void* unused,const unsigned* id) {
    const char* source=originalLocalized(unused,id);
    if(!source || !std::strchr(source,'/'))return source;
    std::string replaced=VrPromptText(source,ActiveBindings(),CameraHook_IsBuildUiOpen());
    if(replaced==source)return source;
    // Native HUDs retain localization pointers. Intern the replacement for
    // process lifetime; a temporary/thread-local buffer would corrupt them.
    std::lock_guard<std::mutex> lock(localizedMutex);
    return localizedStrings.emplace(std::move(replaced)).first->c_str();
}
bool __stdcall HookShow(void* hud,const void* name) {
    showingCard=true;cardTitle.clear();cardBody.clear();
    const bool accepted=originalShow(hud,name);
    showingCard=false;
    if(accepted) {
        std::lock_guard<std::mutex> lock(promptMutex);
        tutorialHud=hud;tutorialMovie=*static_cast<void**>(hud);
        title=cardTitle;body=cardBody;
        Log("VR tutorial: name=%s title=%s textBytes=%u",NameText(name),title.c_str(),unsigned(body.size()));
    }
    return accepted;
}
template<class T> bool Install(uintptr_t base,unsigned rva,void* hook,T& original,const uint8_t* expected,size_t size) {
    auto* target=reinterpret_cast<void*>(base+rva);
    if(std::memcmp(target,expected,size))return false;
    return MH_CreateHook(target,hook,reinterpret_cast<void**>(&original))==MH_OK && MH_EnableHook(target)==MH_OK;
}
}
bool VrPrompts_Init() {
    const uintptr_t base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    const uint8_t textPrologue[]{0x55,0x8b,0xec,0x83,0xec,0x08,0x53};
    const uint8_t showPrologue[]{0x55,0x8b,0xec,0x81,0xec,0xb4,0,0,0};
    const uint8_t literalPrologue[]{0x55,0x8b,0xec,0x83,0xec,0x14,0x56};
    const bool a=Install(base,0x71830,reinterpret_cast<void*>(&HookText),originalText,textPrologue,sizeof(textPrologue));
    const bool b=Install(base,0x71a00,reinterpret_cast<void*>(&HookIndexed),originalIndexed,textPrologue,sizeof(textPrologue));
    const bool c=a&&b&&Install(base,0x18e1d0,reinterpret_cast<void*>(&HookShow),originalShow,showPrologue,sizeof(showPrologue));
    const bool d=Install(base,0x71b40,reinterpret_cast<void*>(&HookLiteral),originalLiteral,literalPrologue,sizeof(literalPrologue));
    const auto* lookup=reinterpret_cast<const uint8_t*>(base+0x93150);
    const uint8_t lookupBody[]{0x8b,0x41,0x14,0xc1,0xe8,0x06};
    const bool verifiedLookup=lookup[0]==0x8b&&lookup[1]==0x0d&&
        *reinterpret_cast<const uintptr_t*>(lookup+2)==base+0xc09cc4&&
        std::memcmp(lookup+6,lookupBody,sizeof(lookupBody))==0;
    const bool e=verifiedLookup&&Install(base,0x93150,reinterpret_cast<void*>(&HookLocalized),originalLocalized,lookup,12);
    const uint8_t assignPrologue[]{0x55,0x8b,0xec,0x53,0x8b,0x5d,0x08,0x56,0x57,0x6a,0xff};
    const uint8_t formatPrologue[]{0x55,0x8b,0xec,0x8b,0x45,0x08,0x8b,0x00,0x8b,0x00,0x83,0xec,0x40};
    const bool verifiedAssign=std::memcmp(reinterpret_cast<void*>(base+0x6a1510),
                                         assignPrologue,sizeof(assignPrologue))==0;
    if(verifiedAssign)nativeAssign=reinterpret_cast<NativePromptAssign>(base+0x6a1510);
    const bool f=verifiedAssign&&Install(base,0x67810,reinterpret_cast<void*>(&HookFormat),
                                        originalFormat,formatPrologue,sizeof(formatPrologue));
    Log("VR prompt hooks: text=%d indexed=%d tutorial=%d literal=%d localized=%d aliases=%d",a,b,c,d,e,f);
    return a&&b&&c&&d&&e&&f;
}
void VrPrompts_Publish(uint64_t epoch) {
    if(!shared) {
        mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(blvr_prompt_bridge::Snapshot),blvr_prompt_bridge::MappingName);
        if(mapping)shared=static_cast<blvr_prompt_bridge::Snapshot*>(MapViewOfFile(mapping,FILE_MAP_WRITE,0,0,0));
        if(!shared)return;
    }
    std::lock_guard<std::mutex> lock(promptMutex);
    InterlockedIncrement(&shared->sequence);MemoryBarrier();
    shared->magic=blvr_prompt_bridge::Magic;shared->producerPid=GetCurrentProcessId();
    shared->producerEpoch=epoch;shared->tickMs=GetTickCount64();
    shared->active=ActiveCard()&&!title.empty()&&!body.empty();
    strncpy_s(shared->title,title.c_str(),_TRUNCATE);strncpy_s(shared->body,body.c_str(),_TRUNCATE);
    MemoryBarrier();InterlockedIncrement(&shared->sequence);
}
}
