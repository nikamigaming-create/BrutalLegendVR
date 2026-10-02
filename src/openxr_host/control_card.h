#pragma once
#include "../bridge/blvr_prompt_bridge.h"
#include "../input/control_bindings.h"
#include <d3d11.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <filesystem>
#include <vector>
#include <string>
#include <cstring>
#include <algorithm>

namespace blvr_xr_host {
// Original generated artwork, with ordinary font rasterization for exact,
// localized native tutorial text. No text is baked into the generated image.
class ControlCard {
public:
    ~ControlCard() {
        if(shared_) UnmapViewOfFile(shared_);
        if(mapping_) CloseHandle(mapping_);
    }
    ID3D11ShaderResourceView* update(ID3D11Device* device,ID3D11DeviceContext* context,
                                    uint32_t pid,uint64_t epoch) {
        if(!pid||!epoch) return nullptr;
        if(!shared_) {
            mapping_=OpenFileMappingW(FILE_MAP_READ,FALSE,blvr_prompt_bridge::MappingName);
            if(!mapping_) return nullptr;
            shared_=static_cast<const blvr_prompt_bridge::Snapshot*>(MapViewOfFile(mapping_,FILE_MAP_READ,0,0,sizeof(last_)));
            if(!shared_) {CloseHandle(mapping_);mapping_=nullptr;return nullptr;}
        }
        blvr_prompt_bridge::Snapshot current{};
        bool coherent=false;
        for(unsigned i=0;i<3;++i) {
            const LONG sequence=shared_->sequence;
            if(!sequence||(sequence&1)) continue;
            MemoryBarrier();std::memcpy(&current,shared_,sizeof(current));MemoryBarrier();
            if(shared_->sequence==sequence&&current.sequence==sequence) {coherent=true;break;}
        }
        const uint64_t now=GetTickCount64();
        if(!coherent||current.magic!=blvr_prompt_bridge::Magic||current.producerPid!=pid||
           current.producerEpoch!=epoch||current.tickMs>now||now-current.tickMs>1000||!current.active)
            return nullptr;
        current.title[sizeof(current.title)-1]=0;current.body[sizeof(current.body)-1]=0;
        const auto bindings=BLVR::ActiveBindings();
        const auto signature=BLVR::ControlsSignature(bindings);
        if(view_&&signature==controlsSignature_&&std::strcmp(last_.title,current.title)==0&&std::strcmp(last_.body,current.body)==0)
            return view_.Get();
        if(!loadArtwork()) return nullptr; // The live native card remains usable.
        if(!rasterize(current.title,current.body,bindings)) return nullptr;
        if(!texture_) {
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width=width_;desc.Height=height_;desc.MipLevels=desc.ArraySize=1;
            desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;desc.SampleDesc.Count=1;
            desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            if(FAILED(device->CreateTexture2D(&desc,nullptr,&texture_))||
               FAILED(device->CreateShaderResourceView(texture_.Get(),nullptr,&view_))) {
                texture_.Reset();view_.Reset();return nullptr;
            }
        }
        context->UpdateSubresource(texture_.Get(),0,nullptr,pixels_.data(),width_*4,0);
        last_=current;controlsSignature_=signature;
        return view_.Get();
    }
    uint32_t width() const {return width_;}
    uint32_t height() const {return height_;}
private:
    bool loadArtwork() {
        if(!art_.empty()) return true;
        if(attempted_) return false;
        attempted_=true;
        const HRESULT com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        bool loaded=false;
        {
            Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
            Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
            Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
            Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
            wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);
            const auto path=std::filesystem::path(exe).parent_path().parent_path()/L"assets/ui/vr-controls-frame.png";
            if(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)))&&
               SUCCEEDED(factory->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder))&&
               SUCCEEDED(decoder->GetFrame(0,&frame))&&SUCCEEDED(frame->GetSize(&width_,&height_))&&
               width_>=512&&height_>=256&&width_<=4096&&height_<=4096&&
               SUCCEEDED(factory->CreateFormatConverter(&converter))&&
               SUCCEEDED(converter->Initialize(frame.Get(),GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom))) {
                art_.resize(size_t(width_)*height_*4);
                loaded=SUCCEEDED(converter->CopyPixels(nullptr,width_*4,static_cast<UINT>(art_.size()),art_.data()));
                if(!loaded) art_.clear();
            }
        }
        if(SUCCEEDED(com)) CoUninitialize();
        return loaded;
    }
    static std::wstring wide(const char* text) {
        UINT encoding=CP_UTF8;
        int count=MultiByteToWideChar(encoding,MB_ERR_INVALID_CHARS,text,-1,nullptr,0);
        if(!count) {encoding=CP_ACP;count=MultiByteToWideChar(encoding,0,text,-1,nullptr,0);}
        std::wstring result(static_cast<size_t>((std::max)(count,1)),L'\0');
        MultiByteToWideChar(encoding,0,text,-1,result.data(),static_cast<int>(result.size()));
        if(!result.empty()) result.pop_back();
        return result;
    }
    bool rasterize(const char* title,const char* body,const BLVR::ControlBindings& bindings) {
        HDC dc=CreateCompatibleDC(nullptr);if(!dc) return false;
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth=static_cast<LONG>(width_);info.bmiHeader.biHeight=-static_cast<LONG>(height_);
        info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        void* bits=nullptr;HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);
        if(!bitmap) {DeleteDC(dc);return false;}
        const auto old=SelectObject(dc,bitmap);std::memcpy(bits,art_.data(),art_.size());
        SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(243,236,217));
        const auto rect=[&](float x0,float y0,float x1,float y1) {
            return RECT{LONG(x0*width_),LONG(y0*height_),LONG(x1*width_),LONG(y1*height_)};
        };
        const auto draw=[&](const std::wstring& text,RECT box,int initial,bool bold) {
            HFONT font=nullptr;HGDIOBJ previous=nullptr;
            for(int size=initial;size>=20;size-=2) {
                font=CreateFontW(-size,0,0,0,bold?FW_BOLD:FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
                    OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,bold?L"Copperplate Gothic Bold":L"Rockwell Condensed");
                previous=SelectObject(dc,font);RECT measured=box;
                DrawTextW(dc,text.c_str(),static_cast<int>(text.size()),&measured,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);
                if(measured.bottom<=box.bottom||size==20) break;
                SelectObject(dc,previous);DeleteObject(font);font=nullptr;
            }
            if(font) {
                DrawTextW(dc,text.c_str(),static_cast<int>(text.size()),&box,DT_WORDBREAK|DT_NOPREFIX);
                SelectObject(dc,previous);DeleteObject(font);
            }
        };
        draw(wide(title),rect(.105f,.185f,.895f,.32f),int(height_*.0625f),true);
        draw(wide(body),rect(.105f,.34f,.895f,.70f),int(height_*.049f),false);
        SetTextColor(dc,RGB(192,185,172));
        const bool commands=std::strstr(body,"command +")!=nullptr;
        const std::string footer=(commands?"COMMAND: hold "+BLVR::CommandChordLabel(bindings)+"\n":"")+
            BLVR::ActionLabel(BLVR::Cancel,bindings)+"  Continue";
        draw(wide(footer.c_str()),
             rect(.105f,.715f,.895f,.80f),int(height_*.033f),true);
        GdiFlush();pixels_.assign(static_cast<uint8_t*>(bits),static_cast<uint8_t*>(bits)+art_.size());
        // GDI clears alpha where glyphs land. The card's text field is opaque;
        // keep the generated edge alpha everywhere else.
        for(size_t i=0;i<pixels_.size();i+=4)
            if(std::memcmp(pixels_.data()+i,art_.data()+i,3)) pixels_[i+3]=255;
        SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);return true;
    }
    HANDLE mapping_=nullptr;
    const blvr_prompt_bridge::Snapshot* shared_=nullptr;
    blvr_prompt_bridge::Snapshot last_{};
    uint64_t controlsSignature_=0;
    bool attempted_=false;
    UINT width_=0,height_=0;
    std::vector<uint8_t> art_,pixels_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view_;
};
}
