#pragma once

#include <d3d11.h>
#include <d3dcompiler.h>
#include <openxr/openxr.h>
#include <wrl/client.h>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace blvr_xr_host {
// Draw a LOCAL-space plane through the same eye pose/FOV as the world pixels.
// This also works on runtimes whose overlay preview ignores quad transforms.
class WorldQuad {
public:
    void draw(ID3D11Device* device, ID3D11DeviceContext* context,
        ID3D11ShaderResourceView* texture, const XrView& eye,
        const XrPosef& pose, float width, float height,
        XrVector4f uvRect = {0,0,1,1}) {
        if (!vs_) initialize(device);
        struct Constants {
            XrVector4f centerAndWidth;
            XrQuaternionf quadOrientation;
            XrVector4f eyeAndHeight;
            XrQuaternionf inverseEyeOrientation;
            XrVector4f tangents;
            XrVector4f uvRect;
        } constants{};
        constants.centerAndWidth = {pose.position.x, pose.position.y, pose.position.z, width};
        constants.quadOrientation = pose.orientation;
        constants.eyeAndHeight = {eye.pose.position.x, eye.pose.position.y, eye.pose.position.z, height};
        const auto& q = eye.pose.orientation;
        constants.inverseEyeOrientation = {-q.x, -q.y, -q.z, q.w};
        constants.tangents = {std::tan(eye.fov.angleLeft), std::tan(eye.fov.angleRight),
            std::tan(eye.fov.angleDown), std::tan(eye.fov.angleUp)};
        constants.uvRect = uvRect;
        context->UpdateSubresource(constants_.Get(), 0, nullptr, &constants, 0, 0);
        ID3D11Buffer* buffer = constants_.Get();
        context->VSSetConstantBuffers(0, 1, &buffer);
        context->VSSetShader(vs_.Get(), nullptr, 0);
        context->PSSetShader(ps_.Get(), nullptr, 0);
        context->PSSetShaderResources(0, 1, &texture);
        ID3D11SamplerState* sampler = sampler_.Get();
        context->PSSetSamplers(0, 1, &sampler);
        context->IASetInputLayout(nullptr);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->RSSetState(rasterizer_.Get());
        context->OMSetDepthStencilState(depth_.Get(), 0);
        context->OMSetBlendState(blend_.Get(), nullptr, 0xffffffffu);
        context->Draw(6, 0);
        ID3D11ShaderResourceView* none = nullptr;
        context->PSSetShaderResources(0, 1, &none);
        context->OMSetBlendState(nullptr, nullptr, 0xffffffffu);
        context->OMSetDepthStencilState(nullptr, 0);
    }
private:
    template<class T> using ComPtr = Microsoft::WRL::ComPtr<T>;
    void initialize(ID3D11Device* device) {
        const char* source = R"(
cbuffer QuadConstants : register(b0) {
    float4 centerAndWidth; float4 quadQ; float4 eyeAndHeight; float4 inverseEyeQ; float4 tangents; float4 uvRect;
};
float3 rotateQ(float4 q, float3 v) { return v + 2.0 * cross(q.xyz, cross(q.xyz,v) + q.w*v); }
struct V { float4 position : SV_Position; float2 uv : TEXCOORD0; };
V vs(uint id : SV_VertexID) {
    const float2 coords[6] = {float2(0,0),float2(1,0),float2(0,1),float2(0,1),float2(1,0),float2(1,1)};
    V o; o.uv = coords[id];
    float3 local = float3((o.uv.x-.5)*centerAndWidth.w,(.5-o.uv.y)*eyeAndHeight.w,0);
    float3 world = centerAndWidth.xyz + rotateQ(quadQ,local);
    float3 view = rotateQ(inverseEyeQ,world-eyeAndHeight.xyz);
    float w = -view.z;
    o.position = float4((2*view.x-(tangents.y+tangents.x)*w)/(tangents.y-tangents.x),
        (2*view.y-(tangents.w+tangents.z)*w)/(tangents.w-tangents.z), w-.05, w);
    o.uv = lerp(uvRect.xy,uvRect.zw,o.uv);
    return o;
}
Texture2D<float4> image : register(t0); SamplerState imageSampler : register(s0);
float4 ps(V v) : SV_Target { return image.Sample(imageSampler,v.uv); }
)";
        ComPtr<ID3DBlob> vsCode, psCode, errors;
        auto require = [](HRESULT hr) { if (FAILED(hr)) throw std::runtime_error("Native HUD world-quad creation failed"); };
        require(D3DCompile(source, std::strlen(source), nullptr, nullptr, nullptr, "vs", "vs_5_0", 0, 0, &vsCode, &errors));
        require(D3DCompile(source, std::strlen(source), nullptr, nullptr, nullptr, "ps", "ps_5_0", 0, 0, &psCode, &errors));
        require(device->CreateVertexShader(vsCode->GetBufferPointer(), vsCode->GetBufferSize(), nullptr, &vs_));
        require(device->CreatePixelShader(psCode->GetBufferPointer(), psCode->GetBufferSize(), nullptr, &ps_));
        D3D11_BUFFER_DESC buffer{};
        buffer.ByteWidth = 96; buffer.Usage = D3D11_USAGE_DEFAULT; buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        require(device->CreateBuffer(&buffer, nullptr, &constants_));
        D3D11_BLEND_DESC blend{};
        auto& b = blend.RenderTarget[0];
        b.BlendEnable = TRUE; b.SrcBlend = D3D11_BLEND_ONE; b.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        b.BlendOp = b.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        b.SrcBlendAlpha = D3D11_BLEND_ONE; b.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        b.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        require(device->CreateBlendState(&blend, &blend_));
        D3D11_DEPTH_STENCIL_DESC depth{};
        depth.DepthEnable = FALSE; depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO; depth.DepthFunc = D3D11_COMPARISON_ALWAYS;
        require(device->CreateDepthStencilState(&depth, &depth_));
        D3D11_RASTERIZER_DESC raster{};
        // A wrist/palm screen has one readable face. Drawing its back projects
        // mirrored text through the hand when the player raises it to wave.
        raster.FillMode = D3D11_FILL_SOLID; raster.CullMode = D3D11_CULL_BACK; raster.DepthClipEnable = TRUE;
        require(device->CreateRasterizerState(&raster, &rasterizer_));
        D3D11_SAMPLER_DESC sampler{};
        sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.MaxLOD = D3D11_FLOAT32_MAX;
        require(device->CreateSamplerState(&sampler, &sampler_));
    }
    ComPtr<ID3D11VertexShader> vs_;
    ComPtr<ID3D11PixelShader> ps_;
    ComPtr<ID3D11Buffer> constants_;
    ComPtr<ID3D11BlendState> blend_;
    ComPtr<ID3D11DepthStencilState> depth_;
    ComPtr<ID3D11RasterizerState> rasterizer_;
    ComPtr<ID3D11SamplerState> sampler_;
};
}
