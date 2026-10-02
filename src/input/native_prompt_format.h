#pragma once
#include "vr_control_hints.h"
#include <windows.h>
#include <cwchar>
#include <string>

namespace BLVR {
// Retail Flash's alias formatter receives a reference to a UTF-16 string
// object. Its first word is the text buffer. Assign through the native UTF-8
// setter so buffer ownership, length and capacity stay with the game.
using NativePromptAssign = void*(__thiscall*)(void*,const char*);
inline bool RemapNativePrompt(void** text,NativePromptAssign assign,
                              std::string* rendered=nullptr,bool buildContext=false) {
    if(!text||!*text||!assign)return false;
    const wchar_t* source=*static_cast<const wchar_t* const*>(*text);
    if(!source||!std::wcschr(source,L'/'))return false;
    const int bytes=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,source,-1,
                                       nullptr,0,nullptr,nullptr);
    if(bytes<=1)return false;
    std::string utf8(static_cast<size_t>(bytes),'\0');
    if(!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,source,-1,
                           utf8.data(),bytes,nullptr,nullptr))return false;
    utf8.pop_back();
    std::string replaced=VrPromptText(utf8,ActiveBindings(),buildContext);
    if(replaced==utf8)return false;
    assign(*text,replaced.c_str());
    if(rendered)*rendered=std::move(replaced);
    return true;
}
}
