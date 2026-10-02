#pragma once
#include "../input/control_bindings.h"
#include <d3d11.h>
#include <windows.h>
#include <wrl/client.h>
#include <vector>
#include <cstring>

namespace blvr_xr_host {
// The native timed sequence keeps its original A/X/Y artwork. This live
// guitar-mounted legend identifies the configured input for each note.
class SoloControlLegend {
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view_;
    uint64_t signature_=0;
public:
    ID3D11ShaderResourceView* update(ID3D11Device* device) {
        const auto bindings=BLVR::ActiveBindings();
        const uint64_t signature=BLVR::ControlsSignature(bindings);
        if(view_&&signature==signature_)return view_.Get();
        constexpr unsigned width=1024,height=140;
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-static_cast<LONG>(height);
        info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        void* pixels=nullptr;HDC dc=CreateCompatibleDC(nullptr);
        HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
        if(!dc||!bitmap||!pixels){if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);return nullptr;}
        const HGDIOBJ previous=SelectObject(dc,bitmap);
        std::memset(pixels,0,width*height*4);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(255,238,205));
        HFONT font=CreateFontW(-32,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        const HGDIOBJ oldFont=SelectObject(dc,font);
        RECT title{12,7,1012,46};DrawTextW(dc,L"SOLO BUTTONS — CURRENT CONTROLS",-1,&title,DT_CENTER|DT_SINGLELINE);
        const BLVR::NativeAction notes[]{BLVR::SoloNote1,BLVR::SoloNote2,BLVR::SoloNote3};
        const char* glyphs[]{"A: ","X: ","Y: "};
        for(unsigned i=0;i<3;++i) {
            const std::string text=std::string(glyphs[i])+BLVR::ActionLabel(notes[i],bindings);
            std::wstring wide(text.begin(),text.end());
            RECT line{static_cast<LONG>(i*width/3+8),64,static_cast<LONG>((i+1)*width/3-8),133};
            DrawTextW(dc,wide.c_str(),-1,&line,DT_CENTER|DT_WORDBREAK);
        }
        GdiFlush();auto* bytes=static_cast<unsigned char*>(pixels);
        for(unsigned i=0;i<width*height;++i)bytes[i*4+3]=235;
        D3D11_TEXTURE2D_DESC desc{};desc.Width=width;desc.Height=height;desc.MipLevels=desc.ArraySize=1;
        desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_IMMUTABLE;
        desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA data{};data.pSysMem=pixels;data.SysMemPitch=width*4;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        const HRESULT created=device->CreateTexture2D(&desc,&data,&texture);
        SelectObject(dc,oldFont);DeleteObject(font);SelectObject(dc,previous);DeleteObject(bitmap);DeleteDC(dc);
        if(FAILED(created))return nullptr;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> next;
        if(FAILED(device->CreateShaderResourceView(texture.Get(),nullptr,&next)))return nullptr;
        view_=next;signature_=signature;return view_.Get();
    }
};
}
