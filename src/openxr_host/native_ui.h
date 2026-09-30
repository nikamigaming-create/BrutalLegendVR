#pragma once

#include "../bridge/blvr_ui_bridge.h"
#include "attached_ui.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <cstring>
#include <vector>

namespace blvr_xr_host {
class NativeUi {
public:
    ~NativeUi() { reset(); }
    void reset() {
        view_.Reset(); texture_.Reset();
        if (mapping_) UnmapViewOfFile(mapping_);
        if (handle_) CloseHandle(handle_);
        mapping_ = nullptr; handle_ = nullptr;
        last_ = {};
        panels_.clear();
    }
    ID3D11ShaderResourceView* update(ID3D11Device* device,
        ID3D11DeviceContext* context, uint32_t pid, uint64_t epoch) {
        using namespace blvr_ui_bridge;
        if (!pid || !epoch) return nullptr;
        if (!mapping_) {
            handle_ = OpenFileMappingW(FILE_MAP_READ, FALSE, MappingName);
            if (!handle_) return nullptr;
            mapping_ = static_cast<const Header*>(
                MapViewOfFile(handle_, FILE_MAP_READ, 0, 0, MappingBytes));
            if (!mapping_) { CloseHandle(handle_); handle_ = nullptr; return nullptr; }
        }
        for (unsigned attempt = 0; attempt < 2; ++attempt) {
            const LONG sequence = mapping_->sequence;
            if (!sequence || (sequence & 1)) break;
            MemoryBarrier();
            Header header{};
            std::memcpy(&header, mapping_, sizeof(header));
            MemoryBarrier();
            if(mapping_->sequence!=sequence || header.sequence!=sequence) continue;
            const uint64_t now = GetTickCount64();
            if (header.magic != Magic || header.version != Version ||
                header.headerBytes != sizeof(Header) || header.producerPid != pid ||
                header.producerEpoch != epoch || header.tickMs > now ||
                now - header.tickMs > 1000u || !header.width || !header.height ||
                header.width > MaxWidth || header.height > MaxHeight ||
                header.rowBytes != header.width * 4u) return nullptr;
            if (!header.contentSamples) { last_ = header; panels_.clear(); return nullptr; }
            if (last_.sequence == sequence && last_.producerEpoch == epoch &&
                last_.producerPid == pid) return view_.Get();
            pixels_.resize(size_t(header.rowBytes) * header.height);
            std::memcpy(pixels_.data(), reinterpret_cast<const uint8_t*>(mapping_) +
                sizeof(Header), pixels_.size());
            MemoryBarrier();
            if (mapping_->sequence != sequence) continue;
            if (!texture_ || header.width != last_.width || header.height != last_.height) {
                view_.Reset(); texture_.Reset();
                D3D11_TEXTURE2D_DESC desc{};
                desc.Width = header.width; desc.Height = header.height;
                desc.MipLevels = desc.ArraySize = 1;
                desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
                desc.SampleDesc.Count = 1;
                desc.Usage = D3D11_USAGE_DEFAULT;
                desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                if (FAILED(device->CreateTexture2D(&desc, nullptr, &texture_)) ||
                    FAILED(device->CreateShaderResourceView(texture_.Get(), nullptr, &view_)))
                    return nullptr;
            }
            context->UpdateSubresource(texture_.Get(), 0, nullptr, pixels_.data(), header.rowBytes, 0);
            last_ = header;
            panels_ = layoutAttachedUi(pixels_.data(),header.width,header.height,header.flags);
            return view_.Get();
        }
        const uint64_t now = GetTickCount64();
        return last_.producerPid == pid && last_.producerEpoch == epoch &&
            now >= last_.tickMs && now - last_.tickMs <= 1000u ? view_.Get() : nullptr;
    }
    uint32_t width() const { return last_.width; }
    uint32_t height() const { return last_.height; }
    uint32_t flags() const {
        const uint64_t now=GetTickCount64();
        return now>=last_.tickMs && now-last_.tickMs<500 ? last_.flags : 0;
    }
    const std::vector<AttachedUiPanel>& panels() const { return panels_; }
private:
    HANDLE handle_ = nullptr;
    const blvr_ui_bridge::Header* mapping_ = nullptr;
    blvr_ui_bridge::Header last_{};
    std::vector<uint8_t> pixels_;
    std::vector<AttachedUiPanel> panels_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view_;
};
}
