#pragma once
// Optional final-compositor recording tap. Included by the local simulator only.
// The environment opt-in is absent in ordinary game/headset sessions.
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace blvr_capture {
inline constexpr uint32_t Magic=0x43525842,Version=2;
inline constexpr uint32_t MaxEyeWidth=1920,MaxHeight=1080;
inline constexpr size_t Capacity=64+size_t(MaxEyeWidth)*2*MaxHeight*4;
enum class CaptureMode:uint32_t {Left=0,Sbs=1};
struct Header {
    volatile LONG sequence;uint32_t width,height,format;double qpcSeconds;uint64_t frame;
    uint32_t magic,version,mode,sourceWidth,sourceHeight,producerPid;uint64_t producerEpoch;
};
static_assert(sizeof(Header)==64&&offsetof(Header,magic)==32&&offsetof(Header,producerEpoch)==56);
inline CaptureMode Mode() {
    static const CaptureMode mode=[] {
        wchar_t value[16]{};GetEnvironmentVariableW(L"BLVR_FINAL_EYE_CAPTURE_MODE",value,16);
        return lstrcmpiW(value,L"sbs")==0?CaptureMode::Sbs:CaptureMode::Left;
    }();return mode;
}
inline bool CaptureWidth(uint32_t sourceWidth,uint32_t height,CaptureMode mode,uint32_t& width) {
    if((mode!=CaptureMode::Left&&mode!=CaptureMode::Sbs)||!sourceWidth||(sourceWidth&1)||
       sourceWidth>MaxEyeWidth*2||!height||height>MaxHeight)return false;
    width=mode==CaptureMode::Sbs?sourceWidth:sourceWidth/2;
    return true;
}
inline bool Enabled() {static const bool enabled=[]{wchar_t name[128]{};return GetEnvironmentVariableW(L"BLVR_FINAL_EYE_CAPTURE",name,128)>0;}();return enabled;}
inline void Frame(ID3D11Device* device,ID3D11DeviceContext* context,IDXGISwapChain1* swapchain,uint64_t frame) {
    if(!Enabled()||!device||!context||!swapchain)return;
    using Microsoft::WRL::ComPtr;
    static HANDLE mapping=nullptr;static unsigned char* shared=nullptr;
    static ComPtr<ID3D11Texture2D> staging;
    static ID3D11Device* stagingDevice=nullptr;static uint64_t producerEpoch=0;
    static LARGE_INTEGER frequency{};
    LARGE_INTEGER clock{};QueryPerformanceCounter(&clock);
    if(!frequency.QuadPart)QueryPerformanceFrequency(&frequency);
    if(frequency.QuadPart<=0)return;
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
    // One copy of the actual completed SBS compositor. Default left capture
    // retains its historical behavior; opt-in SBS never fabricates a right eye.
    const auto mode=Mode();UINT width=0;const UINT height=desc.Height;
    if(!CaptureWidth(desc.Width,height,mode,width)||desc.SampleDesc.Count!=1||
       (desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM&&desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB&&
        desc.Format!=DXGI_FORMAT_B8G8R8A8_UNORM&&desc.Format!=DXGI_FORMAT_B8G8R8A8_UNORM_SRGB))return;
    const UINT sourceWidth=desc.Width;
    if(stagingDevice!=device) {
        staging.Reset();stagingDevice=device;producerEpoch=static_cast<uint64_t>(clock.QuadPart);
    }
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
    header->magic=Magic;header->version=Version;header->mode=static_cast<uint32_t>(mode);
    header->sourceWidth=sourceWidth;header->sourceHeight=height;
    header->producerPid=GetCurrentProcessId();header->producerEpoch=producerEpoch;
    for(UINT y=0;y<height;++y)memcpy(shared+64+size_t(y)*width*4,static_cast<unsigned char*>(pixels.pData)+size_t(y)*pixels.RowPitch,size_t(width)*4);
    MemoryBarrier();InterlockedIncrement(&header->sequence);
    context->Unmap(staging.Get(),0);
}
}
