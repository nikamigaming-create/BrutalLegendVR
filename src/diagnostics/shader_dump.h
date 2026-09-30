#pragma once
#include <windows.h>
#include <d3dcompiler.h>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace BLVR {
// Private, opt-in inspection of shaders from the user's running installation.
// No bytecode or disassembly is part of the mod's shipped assets.
template<class Shader> void DumpShader(Shader* shader,const char* stage) {
    char directory[MAX_PATH]{};
    if(!shader||!GetEnvironmentVariableA("BLVR_SHADER_DUMP_DIR",directory,sizeof(directory)))return;
    UINT bytes=0;
    if(FAILED(shader->GetFunction(nullptr,&bytes))||!bytes||bytes>1024*1024)return;
    std::vector<uint8_t> code(bytes);
    if(FAILED(shader->GetFunction(code.data(),&bytes)))return;
    uint64_t hash=14695981039346656037ull;
    for(uint8_t byte:code){hash^=byte;hash*=1099511628211ull;}
    char path[MAX_PATH]{};
    sprintf_s(path,"%s\\%s-%016llx.txt",directory,stage,static_cast<unsigned long long>(hash));
    if(GetFileAttributesA(path)!=INVALID_FILE_ATTRIBUTES)return;
    using Disassemble=HRESULT(WINAPI*)(const void*,SIZE_T,UINT,LPCSTR,ID3DBlob**);
    static HMODULE library=LoadLibraryW(L"d3dcompiler_47.dll");
    static auto disassemble=library?reinterpret_cast<Disassemble>(GetProcAddress(library,"D3DDisassemble")):nullptr;
    ID3DBlob* text=nullptr;
    if(!disassemble||FAILED(disassemble(code.data(),bytes,0,nullptr,&text))||!text)return;
    FILE* file=nullptr;
    if(fopen_s(&file,path,"wb")==0&&file){fwrite(text->GetBufferPointer(),text->GetBufferSize(),1,file);fclose(file);}
    text->Release();
}
}
