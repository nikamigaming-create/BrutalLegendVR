#pragma once
// Optional final-compositor recording tap. Included by the local simulator only.
// The environment opt-in is absent in ordinary game/headset sessions.
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <cstdint>
#include <cstring>
namespace blvr_capture {
inline constexpr size_t Capacity=64+1920*1080*4;
struct Header { volatile LONG sequence; uint32_t width,height,format; double qpcSeconds; uint64_t frame; };
inline bool Enabled() {static const bool enabled=[]{wchar_t name[128]{};return GetEnvironmentVariableW(L"BLVR_FINAL_EYE_CAPTURE",name,128)>0;}();return enabled;}
inline void Frame(ID3D11Device* device,ID3D11DeviceContext* context,IDXGISwapChain1* swapchain,uint64_t frame) {
    if(!Enabled()||!device||!context||!swapchain)return;
    using Microsoft::WRL::ComPtr;
    static HANDLE mapping=nullptr;static unsigned char* shared=nullptr;
    static ComPtr<ID3D11Texture2D> staging;
    static LARGE_INTEGER frequency{};
    LARGE_INTEGER clock{};QueryPerformanceCounter(&clock);
    if(!frequency.QuadPart)QueryPerformanceFrequency(&frequency);
    const double now=double(clock.QuadPart)/double(frequency.QuadPart);
    if(!shared) {
        wchar_t name[128]{};GetEnvironmentVariableW(L"BLVR_FINAL_EYE_CAPTURE",name,128);
        mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,DWORD(Capacity),name);
        if(!mapping)return;
        shared=static_cast<unsigned char*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,Capacity));
        if(!shared)return;
        memset(shared,0,64);
    }
    ComPtr<ID3D11Texture2D> source;
    if(FAILED(swapchain->GetBuffer(0,IID_PPV_ARGS(&source))))return;
    D3D11_TEXTURE2D_DESC desc{};source->GetDesc(&desc);
    // Simulator compositor is SBS: preserve the complete left eye, including overlays.
    const UINT width=desc.Width/2,height=desc.Height;
    if(!width||width>1920||height>1080)return;
    D3D11_TEXTURE2D_DESC previous{};if(staging)staging->GetDesc(&previous);
    if(previous.Width!=width||previous.Height!=height||previous.Format!=desc.Format) {
        staging.Reset();desc.Width=width;desc.MipLevels=desc.ArraySize=1;
        desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=desc.MiscFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        if(FAILED(device->CreateTexture2D(&desc,nullptr,&staging)))return;
    }
    D3D11_BOX box{0,0,0,width,height,1};
    context->CopySubresourceRegion(staging.Get(),0,0,0,0,source.Get(),0,&box);
    D3D11_MAPPED_SUBRESOURCE pixels{};
    if(FAILED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&pixels)))return;
    auto* header=reinterpret_cast<Header*>(shared);InterlockedIncrement(&header->sequence);
    header->width=width;header->height=height;header->format=uint32_t(desc.Format);
    header->qpcSeconds=now;header->frame=frame;
    for(UINT y=0;y<height;++y)memcpy(shared+64+size_t(y)*width*4,static_cast<unsigned char*>(pixels.pData)+size_t(y)*pixels.RowPitch,size_t(width)*4);
    MemoryBarrier();InterlockedIncrement(&header->sequence);
    context->Unmap(staging.Get(),0);
}
}
