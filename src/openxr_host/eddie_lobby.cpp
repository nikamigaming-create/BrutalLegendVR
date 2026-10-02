#include "eddie_lobby.h"
#include "roadie_room.h"
#include "../bridge/eddie_dimensions.h"
#include "../bridge/native_hand_bridge.h"
#include "../bridge/blvr_ui_bridge.h"
#include "../input/pose_controls.h"
#include <windows.h>
#include <DirectXMath.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <wincodec.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <stdexcept>
#include <vector>

namespace blvr_xr_host {
using namespace DirectX;
using Microsoft::WRL::ComPtr;
namespace {
constexpr unsigned MaxBones = 256;
const char* Shader = R"(
cbuffer Frame : register(b0) {
    row_major float4x4 vp;
    row_major float4x4 world;
    float4 color;
    float4 mode;
};
cbuffer Skeleton : register(b1) { row_major float4x4 bones[256]; };
Texture2D diffuse : register(t0); SamplerState sampleState : register(s0);
Texture2D stone : register(t1);
struct Vertex { float3 p:POSITION; float3 n:NORMAL; float2 uv:TEXCOORD0;
    float4 weights:BLENDWEIGHT; uint4 joints:BLENDINDICES; };
struct Pixel { float4 p:SV_POSITION; float3 n:NORMAL; float2 uv:TEXCOORD0;
    float3 w:TEXCOORD1; float3 local:TEXCOORD2; float3 tint:COLOR0;
    nointerpolation uint surface:TEXCOORD3; };
Pixel vsMain(Vertex v) {
    float4 p=float4(v.p,1); float3 n=v.n;
    if(mode.x>0.5) {
        row_major float4x4 skin = bones[v.joints.x]*v.weights.x + bones[v.joints.y]*v.weights.y
          + bones[v.joints.z]*v.weights.z + bones[v.joints.w]*v.weights.w;
        p=mul(p,skin); n=mul(float4(n,0),skin).xyz;
    }
    Pixel o; float4 w=mul(p,world); o.p=mul(w,vp); o.w=w.xyz;
    o.n=mode.z==2?n:mul(float4(n,0),world).xyz; o.uv=v.uv;
    o.local=v.p; o.tint=v.weights.rgb; o.surface=v.joints.x; return o;
}
float hash(float2 p) { return frac(sin(dot(p,float2(127.1,311.7)))*43758.5453); }
float noise(float2 p) {
    float2 i=floor(p),f=frac(p);f=f*f*(3-2*f);
    return lerp(lerp(hash(i),hash(i+float2(1,0)),f.x),lerp(hash(i+float2(0,1)),hash(i+1),f.x),f.y);
}
float3 fireLight(float3 p,float3 n,float3 lamp) {
    float3 d=lamp-p;float falloff=1/(1+dot(d,d)*.32);
    return float3(1.3,.40,.055)*falloff*(.22+.78*saturate(dot(n,normalize(d))));
}
float3 roomLight(float3 p,float3 n) {
    float3 light=float3(.22,.27,.34)+float3(.57,.62,.72)*saturate(dot(n,normalize(float3(-.4,.8,.3))));
    light+=fireLight(p,n,float3(-5.5,1.5,-4.8))+fireLight(p,n,float3(5.5,1.5,-4.8));
    light+=fireLight(p,n,float3(-5.5,1.5,4.8))+fireLight(p,n,float3(5.5,1.5,4.8));
    return light;
}
float3 roomSurface(Pixel p) {
    float3 n=normalize(p.n),base=p.tint;
    float grain=noise(p.local.xz*3.7+p.local.y*.43);
    if(p.surface==6) {
        float3 direction=normalize(p.local);
        float horizon=pow(saturate(1-abs(direction.y)),4);
        float cloud=noise(direction.xz*7+direction.y*5)*.6+noise(direction.xz*19)*.4;
        float3 sky=lerp(float3(.009,.016,.036),float3(.12,.054,.026),horizon);
        sky=lerp(sky,sky*.30,smoothstep(.43,.76,cloud)*saturate(direction.y*3+.3));
        float moon=1-smoothstep(.045,.049,length(direction-normalize(float3(-.38,.29,-.88))));
        return sky+float3(.64,.57,.39)*moon;
    }
    float3 blend=abs(n);blend/=max(.001,blend.x+blend.y+blend.z);
    float3 rock=stone.Sample(sampleState,p.local.zy*.28).rgb*blend.x
               +stone.Sample(sampleState,p.local.xz*.28).rgb*blend.y
               +stone.Sample(sampleState,p.local.xy*.28).rgb*blend.z;
    if(p.surface==1) {
        base=rock*(.70+.20*noise(p.local.xz*.45));
        // Soft contact darkening under the stage and at the terrace boundary.
        float pedestal=1-smoothstep(.55,1.1,length((p.local.xz-float2(0,-3.4))*float2(1,1.8)));
        base*=1-pedestal*.65;
        base*=.85+.15*saturate((8-length(p.local.xz))*.7);
    } else if(p.surface==0) {
        float stratum=noise(float2(p.local.y*5,p.local.x+p.local.z));
        base=rock*(.60+.47*grain+.18*stratum)*float3(.72,.83,1.0);
    } else if(p.surface==2||p.surface==3) {
        base*=.20+.22*grain+.65*rock;
        base+=pow(saturate(dot(n,normalize(float3(-.5,.65,.6)))),12)*(p.surface==3?.055:.018);
    } else if(p.surface==4) {
        float2 weave=abs(frac(float2(p.local.x,p.local.y)*70)-.5);
        float aa=max(fwidth(weave.x),fwidth(weave.y));
        base*=.45+.55*smoothstep(.12-aa,.12+aa,min(weave.x,weave.y));
    } else if(p.surface==5||p.surface==7) {
        float pulse=p.surface==7?.88+.12*sin(mode.w*5+p.local.y*7+p.local.x*2):1;
        float heat=noise(p.local.xz*11+p.local.y*8+float2(mode.w*.9,-mode.w*.7));
        return p.surface==7?lerp(float3(.9,.075,.006),float3(1.6,.75,.12),heat)*pulse:base*pulse*1.4;
    } else if(p.surface==8) {
        base*=.45+.7*grain;
    }
    float distanceFog=saturate((length(p.local.xz)-10)/32);
    return lerp(base*roomLight(p.local,n),float3(.075,.09,.115),distanceFog*.8);
}
float4 psMain(Pixel p) : SV_TARGET {
    if(mode.z==2)return float4(roomSurface(p),1);
    if(mode.z==1) {
        float2 q=abs(p.uv-.5);
        float rim=smoothstep(.42,.455,q.x)+smoothstep(.34,.40,q.y);
        // Inlaid play symbol on a real brass switch: no debug-room labels.
        float2 symbol=(p.uv-.5)*float2(2.875,1);
        float play=step(-.12,symbol.x)*step(symbol.x,.17)*step(abs(symbol.y),(.17-symbol.x)*.70);
        return float4(lerp(float3(.10,.075,.045),float3(.67,.39,.10),saturate(rim*.45+play))*color.rgb,1);
    }
    float4 tex=diffuse.Sample(sampleState,p.uv);
    if(mode.y>0.5) { return float4(tex.rgb*color.rgb,1); }
    if(mode.z==3) {
        clip(.49-length(p.uv-.5));
        return float4(tex.rgb*roomLight(float3((p.uv.x-.5)*4.12,0,(p.uv.y-.5)*4.12),float3(0,1,0)),1);
    }
    clip(tex.a-0.18);
    float3 n=normalize(p.n);
    float key=saturate(dot(n,normalize(float3(-0.4,0.8,0.5))));
    float fill=saturate(dot(n,normalize(float3(0.7,0.2,-0.6))));
    float3 light=0.34+0.66*key+float3(0.11,0.14,0.18)*fill;
    return float4(tex.rgb*color.rgb*light,1);
})";
void check(HRESULT result, const char* what) {
    if (FAILED(result)) throw std::runtime_error(std::string(what)+" failed ("+std::to_string(result)+")");
}
template<class T> void read(std::ifstream& stream, T& value) {
    if (!stream.read(reinterpret_cast<char*>(&value), sizeof(value))) throw std::runtime_error("Truncated Eddie cache");
}
void bytes(std::ifstream& stream, void* p, size_t size) {
    if (!stream.read(static_cast<char*>(p), static_cast<std::streamsize>(size))) throw std::runtime_error("Truncated Eddie cache array");
}
bool usablePose(const blvr_xr_bridge::Pose& pose) {
    float norm=0;
    for(float value:pose.orientation) {if(!std::isfinite(value))return false;norm+=value*value;}
    for(float value:pose.position)if(!std::isfinite(value)||std::fabs(value)>10000)return false;
    return norm>.5f&&norm<1.5f;
}
XMMATRIX matrix(const blvr_xr_bridge::Pose& pose) {
    XMMATRIX m=XMMatrixRotationQuaternion(XMQuaternionNormalize(XMLoadFloat4(reinterpret_cast<const XMFLOAT4*>(pose.orientation))));
    m.r[3]=XMVectorSet(pose.position[0],pose.position[1],pose.position[2],1); return m;
}
bool usableMatrix(const float* values) {
    for(int i=0;i<16;++i)if(!std::isfinite(values[i])||std::fabs(values[i])>20)return false;
    if(std::fabs(values[3])>.00001f||std::fabs(values[7])>.00001f||std::fabs(values[11])>.00001f||std::fabs(values[15]-1)>.00001f)return false;
    const XMMATRIX transform=XMLoadFloat4x4(reinterpret_cast<const XMFLOAT4X4*>(values));
    XMVECTOR scale,rotation,translation;
    if(!XMMatrixDecompose(&scale,&rotation,&translation,transform))return false;
    const float determinant=XMVectorGetX(XMMatrixDeterminant(transform));
    return std::isfinite(determinant)&&std::fabs(determinant)>.00000001f;
}
XMVECTOR point(const XMFLOAT4X4& m) { return XMVectorSet(m._41,m._42,m._43,1); }
float length(XMVECTOR p) { return XMVectorGetX(XMVector3Length(p)); }
XMMATRIX move(XMVECTOR p) { return XMMatrixTranslationFromVector(p); }
XMMATRIX aim(XMVECTOR from, XMVECTOR to) {
    from=XMVector3Normalize(from); to=XMVector3Normalize(to);
    const float dot=XMVectorGetX(XMVector3Dot(from,to));
    if (dot>0.99999f) return XMMatrixIdentity();
    if (dot<-.99999f) {
        XMVECTOR axis=XMVector3Cross(from,XMVectorSet(0,1,0,0));
        if(length(axis)<.001f) axis=XMVector3Cross(from,XMVectorSet(1,0,0,0));
        return XMMatrixRotationAxis(axis,XM_PI);
    }
    XMVECTOR q=XMVectorSetW(XMVector3Cross(from,to),1+dot);
    return XMMatrixRotationQuaternion(XMQuaternionNormalize(q));
}
using Vertex=RoomVertex;
static_assert(sizeof(Vertex)==64);
struct Constants { XMFLOAT4X4 vp, world; XMFLOAT4 color, mode; };
XMMATRIX blendMatrices(XMMATRIX a,XMMATRIX b,float weight) {
    XMVECTOR sa,qa,ta,sb,qb,tb;
    if(!XMMatrixDecompose(&sa,&qa,&ta,a)||!XMMatrixDecompose(&sb,&qb,&tb,b)) return weight<.5f?a:b;
    return XMMatrixScalingFromVector(XMVectorLerp(sa,sb,weight))
        *XMMatrixRotationQuaternion(XMQuaternionSlerp(qa,qb,weight))
        *XMMatrixTranslationFromVector(XMVectorLerp(ta,tb,weight));
}
ComPtr<ID3D11Buffer> buffer(ID3D11Device* device, unsigned flags, const void* data, unsigned size) {
    D3D11_BUFFER_DESC desc{}; desc.ByteWidth=size; desc.BindFlags=flags; desc.Usage=D3D11_USAGE_DEFAULT;
    D3D11_SUBRESOURCE_DATA initial{}; initial.pSysMem=data;
    ComPtr<ID3D11Buffer> result;
    check(device->CreateBuffer(&desc,data?&initial:nullptr,&result),"Eddie buffer"); return result;
}
ComPtr<ID3D11ShaderResourceView> texture(ID3D11Device* device, unsigned w, unsigned h, const void* pixels) {
    D3D11_TEXTURE2D_DESC desc{}; desc.Width=w; desc.Height=h; desc.MipLevels=1; desc.ArraySize=1;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; desc.SampleDesc.Count=1;
    desc.Usage=D3D11_USAGE_IMMUTABLE; desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data{}; data.pSysMem=pixels; data.SysMemPitch=w*4;
    ComPtr<ID3D11Texture2D> tex; check(device->CreateTexture2D(&desc,&data,&tex),"Eddie texture");
    ComPtr<ID3D11ShaderResourceView> srv; check(device->CreateShaderResourceView(tex.Get(),nullptr,&srv),"Eddie texture view"); return srv;
}
ComPtr<ID3D11ShaderResourceView> roomArtwork(ID3D11Device* device,ID3D11DeviceContext* context,const std::filesystem::path& path) {
    const HRESULT initialized=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    struct Apartment { HRESULT status; ~Apartment(){if(SUCCEEDED(status))CoUninitialize();} } apartment{initialized};
    ComPtr<IWICImagingFactory> factory;ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;ComPtr<IWICFormatConverter> converter;
    check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)),"Room image factory");
    check(factory->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder),"Room artwork");
    check(decoder->GetFrame(0,&frame),"Room image frame");
    UINT w=0,h=0;check(frame->GetSize(&w,&h),"Room image size");
    if(!w||!h||w>4096||h>4096)throw std::runtime_error("Invalid room artwork dimensions");
    check(factory->CreateFormatConverter(&converter),"Room image converter");
    check(converter->Initialize(frame.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom),"Room image format");
    std::vector<uint8_t> pixels(size_t(w)*h*4);
    check(converter->CopyPixels(nullptr,w*4,static_cast<UINT>(pixels.size()),pixels.data()),"Room image pixels");
    D3D11_TEXTURE2D_DESC desc{};desc.Width=w;desc.Height=h;desc.ArraySize=1;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;desc.SampleDesc.Count=1;
    desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;desc.MiscFlags=D3D11_RESOURCE_MISC_GENERATE_MIPS;
    ComPtr<ID3D11Texture2D> image;check(device->CreateTexture2D(&desc,nullptr,&image),"Room mipmapped image");
    context->UpdateSubresource(image.Get(),0,nullptr,pixels.data(),w*4,0);
    ComPtr<ID3D11ShaderResourceView> result;check(device->CreateShaderResourceView(image.Get(),nullptr,&result),"Room image view");
    context->GenerateMips(result.Get());return result;
}
}

struct EddieLobby::Impl {
    struct Bone { std::string name; int parent; XMFLOAT4X4 reference, inverse; };
    struct Mesh { unsigned group,count; XMFLOAT4 color; ComPtr<ID3D11Buffer> vb,ib; ComPtr<ID3D11ShaderResourceView> texture; };
    std::array<std::array<XMFLOAT4X4,MaxBones>,2> gripLocal{};
    XMFLOAT4X4 gripWeapon[2]{};
    ID3D11Device* device=nullptr; ID3D11DeviceContext* context=nullptr;
    ComPtr<ID3D11VertexShader> vs; ComPtr<ID3D11PixelShader> ps; ComPtr<ID3D11InputLayout> layout;
    ComPtr<ID3D11Buffer> constants,palette,quadVertices,quadIndices;
    ComPtr<ID3D11RasterizerState> rasterizer; ComPtr<ID3D11DepthStencilState> depthState;
    ComPtr<ID3D11SamplerState> sampler; ComPtr<ID3D11Texture2D> depth;
    ComPtr<ID3D11DepthStencilView> depthView;
    ComPtr<ID3D11ShaderResourceView> white,medallion,stoneMaterial;
    ComPtr<ID3D11Buffer> roomVertices,roomIndices;
    unsigned roomIndexCount=0;
    std::vector<Bone> bones; std::vector<Mesh> meshes;
    std::array<XMFLOAT4X4,MaxBones> skin{}, posed{};
    XMFLOAT4X4 root{}, panel{}, nav{};
    XMFLOAT4X4 weapons[2]{};
    float guitarHeadstockY = -1000.f;
    XMFLOAT3 navigation{}, origin{};
    XMFLOAT3 eye{};
    float yaw=0,scale=1,handScale=1,floorY=0,clock=0,pressUntil=0;
    int action=-1;
    float actionStart=0;
    bool actionDown[2]{};
    bool playing=false;
    unsigned selected=0,lastSelectionButtons=0,pendingSelection=0;
    unsigned weaponBeforeDriving=0;
    bool driving=false;
    unsigned lastNoteButtons=0,lastNativeSolo=0;
    bool physicalPlayback=false,supportHeld=false,opening=true;
    bool earthshakerDown=false;
    uint64_t sourceControlsSignature=0;
    XMFLOAT3 supportPoint{},lastAxeTip{},lastStrum{},lastPickingHand{};
    bool motionValid=false,swingArmed=false;
    float actionPulseUntil=0,lastPhysicalAction=-10;
    unsigned pulseAction=0;
    float lastContact[2]{1,1};
    bool latched=false,turnLatched=false,contactLatched=false,triggerDown=false;
    bool tracked[2]{},equipped[2]{};
    unsigned generation=0,recenter=0,width=0,height=0;
    bool loaded=false;
    blvr_xr_bridge::PoseBridge sourceInput{};
    int bone(const char* name) const {
        for(size_t i=0;i<bones.size();++i) if(bones[i].name==name) return static_cast<int>(i);
        throw std::runtime_error(std::string("Required Eddie joint missing: ")+name);
    }
    bool descendant(int joint,int ancestor) const {
        while(joint>=0 && joint!=ancestor) joint=bones[joint].parent; return joint==ancestor;
    }
    XMVECTOR position(int i) const { return point(bones[i].reference); }
    XMMATRIX palm(int hand) const {
        const int wrist=bone(hand==0?"Lf_Wrist":"Rt_Wrist");
        const int middle=bone(hand==0?"Lf_Middle1":"Rt_Middle1");
        const int index=bone(hand==0?"Lf_Index1":"Rt_Index1");
        const int pinky=bone(hand==0?"Lf_Pinky1":"Rt_Pinky1");
        const XMVECTOR z=XMVector3Normalize(position(wrist)-position(middle));
        XMVECTOR x=XMVector3Normalize((hand==0?1.f:-1.f)*(position(index)-position(pinky)));
        const XMVECTOR y=XMVector3Normalize(XMVector3Cross(z,x)); x=XMVector3Cross(y,z);
        return XMMATRIX(x,y,z,XMVectorSetW(position(wrist)*.35f+position(middle)*.65f,1));
    }
    XMMATRIX palmOffset(int hand) const {
        // Row-vector T_grip_from_anatomicalPalm. OpenXR grip -Z runs along
        // the held tube from little finger to index; +X is outward from the
        // left palm and inward to the right. It is NOT an aim-forward pose.
        // Anatomical +Y is the back, -Z the extended fingers, and left +X /
        // right -X the thumb side. This calibration never changes on equip.
        return XMMATRIX(XMVectorSet(0,0,hand==0?-1.f:1.f,0),
            XMVectorSet(hand==0?-1.f:1.f,0,0,0),
            XMVectorSet(0,1,0,0),XMVectorSet(0,0,0,1));
    }
    void fitGrip(int hand,std::array<XMFLOAT4X4,MaxBones>& model,XMMATRIX weapon,bool guitar) const {
        const char* fingers[]{"Index","Middle","Ring","Pinky","Thumb"};
        const XMMATRIX inverseWeapon=XMMatrixInverse(nullptr,weapon);
        for(int finger=0;finger<5;++finger) {
            int chain[3];
            for(int segment=0;segment<3;++segment) chain[segment]=bone(((hand==0?std::string("Lf_"):std::string("Rt_"))+fingers[finger]+std::to_string(segment+1)).c_str());
            // The distal bone is the final knuckle, not the fingertip. Use a
            // virtual pad beyond it so contact does not bury the whole finger.
            const XMVECTOR referenceTip=position(chain[2])+(position(chain[2])-position(chain[1]))*.72f;
            const XMVECTOR tipLocal=XMVector3TransformCoord(referenceTip,XMLoadFloat4x4(&bones[chain[2]].inverse));
            XMVECTOR original=XMVector3TransformCoord(tipLocal,XMLoadFloat4x4(&model[chain[2]])*inverseWeapon);
            XMVECTOR contact=original;
            if(guitar) {
                // Actual imported fretboard +Z surface and rear neck -Z.
                contact=XMVectorSet(finger==4?.012f:.015f+static_cast<float>(finger)*.006f,
                    XMVectorGetY(original),finger==4?-.040f:.055f,1);
            } else {
                XMVECTOR radial=XMVectorSet(XMVectorGetX(original),0,XMVectorGetZ(original),0);
                radial=XMVector3Normalize(radial)*.072f;
                contact=XMVectorSet(XMVectorGetX(radial),XMVectorGetY(original),XMVectorGetZ(radial),1);
            }
            const XMVECTOR target=XMVector3TransformCoord(contact,weapon);
            for(int iteration=0;iteration<10;++iteration) for(int segment=2;segment>=0;--segment) {
                const XMVECTOR tip=XMVector3TransformCoord(tipLocal,XMLoadFloat4x4(&model[chain[2]]));
                const XMVECTOR pivot=point(model[chain[segment]]);
                if(length(tip-target)<.001f||length(target-pivot)<.001f) continue;
                const XMMATRIX rotation=aim(tip-pivot,target-pivot);
                XMVECTOR q=XMQuaternionRotationMatrix(rotation);
                const float angle=2*std::acos(std::clamp(std::fabs(XMVectorGetW(q)),0.f,1.f));
                q=XMQuaternionSlerp(XMQuaternionIdentity(),q,(std::min)(1.f,.22f/(std::max)(angle,.001f)));
                const XMMATRIX delta=move(-pivot)*XMMatrixRotationQuaternion(q)*move(pivot);
                for(size_t i=0;i<bones.size();++i) if(descendant(static_cast<int>(i),chain[segment]))
                    XMStoreFloat4x4(&model[i],XMLoadFloat4x4(&model[i])*delta);
            }
        }
    }
    void arm(int hand,const blvr_xr_bridge::ControllerState& controller, XMMATRIX rootWorld,
             const XMFLOAT4X4* constrainedPalm=nullptr) {
        const auto sourcePose=posed;
        const int shoulder=bone(hand==0?"Lf_Shoulder":"Rt_Shoulder");
        const int elbow=bone(hand==0?"Lf_Elbow":"Rt_Elbow");
        const int wrist=bone(hand==0?"Lf_Wrist":"Rt_Wrist");
        const XMVECTOR s=point(sourcePose[shoulder]),e=point(sourcePose[elbow]),w=point(sourcePose[wrist]);
        const XMMATRIX gripWorld=matrix(controller.gripPose)*XMLoadFloat4x4(&nav);
        XMMATRIX targetPalm=(constrainedPalm?XMLoadFloat4x4(constrainedPalm):XMMatrixScaling(handScale,handScale,handScale)*palmOffset(hand)*gripWorld)*XMMatrixInverse(nullptr,rootWorld);
        const XMMATRIX sourcePalm=palm(hand)*XMLoadFloat4x4(&bones[wrist].inverse)*XMLoadFloat4x4(&sourcePose[wrist]);
        XMMATRIX wristDelta=XMMatrixInverse(nullptr,sourcePalm)*targetPalm;
        XMVECTOR target=XMVector3TransformCoord(w,wristDelta);
        const float upperLength=length(e-s),lowerLength=length(w-e);
        // A controller at the shoulder, or along the elbow pole, must not
        // normalize a zero vector and send NaNs through the entire skin.
        XMVECTOR direction=XMVector3Normalize(length(target-s)>.0001f?target-s:w-s);
        const float d=std::clamp(length(target-s),std::fabs(upperLength-lowerLength)+.001f,upperLength+lowerLength-.002f);
        const XMVECTOR clamped=s+direction*d;
        wristDelta=wristDelta*move(clamped-target); target=clamped;
        const XMVECTOR pole=XMVectorSet(hand==0?.65f:-.65f,-.5f,-.25f,0);
        XMVECTOR bend=pole-direction*XMVector3Dot(pole,direction);
        if(length(bend)<.001f) {
            const XMVECTOR fallback=std::fabs(XMVectorGetY(direction))<.9f?XMVectorSet(0,-1,0,0):XMVectorSet(0,0,1,0);
            bend=fallback-direction*XMVector3Dot(fallback,direction);
        }
        bend=XMVector3Normalize(bend);
        const float along=(upperLength*upperLength-lowerLength*lowerLength+d*d)/(2*d);
        const XMVECTOR joint=s+direction*along+bend*std::sqrt((std::max)(0.f,upperLength*upperLength-along*along));
        const XMMATRIX upper=move(-s)*aim(e-s,joint-s)*move(s);
        const XMMATRIX lower=move(-e)*aim(w-e,target-joint)*move(joint);
        for(size_t i=0;i<bones.size();++i) {
            XMMATRIX delta=XMMatrixIdentity();
            if(descendant(static_cast<int>(i),wrist)) delta=wristDelta;
            else if(descendant(static_cast<int>(i),elbow)) delta=lower;
            else if(descendant(static_cast<int>(i),shoulder)) delta=upper;
            else continue;
            XMStoreFloat4x4(&posed[i],XMLoadFloat4x4(&sourcePose[i])*delta);
        }
        // Distribute only axial wrist twist through the forearm. Blending
        // complete lower-arm/wrist matrices also bends the midpoint bone away
        // from the elbow-to-wrist line and folds the cuff into the arm.
        const int forearm=bone(hand==0?"Lf_Forearm":"Rt_Forearm");
        XMVECTOR ls,lq,lt,ws,wq,wt;
        XMMatrixDecompose(&ls,&lq,&lt,lower);XMMatrixDecompose(&ws,&wq,&wt,wristDelta);
        const XMMATRIX relative=XMMatrixTranspose(XMMatrixRotationQuaternion(lq))*XMMatrixRotationQuaternion(wq);
        XMVECTOR difference=XMQuaternionRotationMatrix(relative);
        if(XMVectorGetW(difference)<0) difference=-difference;
        const XMVECTOR axis=XMVector3Normalize(target-joint);
        XMVECTOR twist=XMVectorSetW(axis*XMVector3Dot(difference,axis),XMVectorGetW(difference));
        twist=XMVectorGetX(XMVector4LengthSq(twist))>.00001f?XMQuaternionNormalize(twist):XMQuaternionIdentity();
        XMMATRIX forearmDelta=lower*XMMatrixRotationQuaternion(XMQuaternionSlerp(XMQuaternionIdentity(),twist,.65f));
        const XMVECTOR middle=XMVector3TransformCoord(point(sourcePose[forearm]),lower);
        const XMVECTOR blended=XMVector3TransformCoord(point(sourcePose[forearm]),forearmDelta);
        forearmDelta=forearmDelta*move(middle-blended);
        XMStoreFloat4x4(&posed[forearm],XMLoadFloat4x4(&sourcePose[forearm])*forearmDelta);
        // Native game-evaluated finger poses, relative to their actual parents.
        // Interpolate toward the authored grip; never invent mirrored curls.
        const float curl=std::clamp(controller.squeeze,0.f,1.f);
        for(size_t i=0;i<bones.size();++i) {
            const auto& b=bones[i];
            if(static_cast<int>(i)==wrist || !descendant(static_cast<int>(i),wrist)) continue;
            const XMMATRIX local=XMLoadFloat4x4(&b.reference)*XMLoadFloat4x4(&bones[b.parent].inverse);
            XMMATRIX result=blendMatrices(local,XMLoadFloat4x4(&gripLocal[hand][i]),curl)*XMLoadFloat4x4(&posed[b.parent]);
            XMStoreFloat4x4(&posed[i],result);
        }
        // The named palm transform owns both the skin and the weapon socket.
        // Thus reach clamping cannot separate a weapon from its hand.
        XMMATRIX socket=XMLoadFloat4x4(&gripWeapon[hand])*XMLoadFloat4x4(&posed[wrist])*rootWorld;
        XMStoreFloat4x4(&weapons[hand],socket);
    }
    void twoHand(const blvr_xr_bridge::PoseBridge& input,XMMATRIX body,bool authored=false) {
        if(!selected||!tracked[0]||!tracked[1]) {supportHeld=false;return;}
        const auto unconstrainedPose=posed;
        const XMFLOAT4X4 unconstrainedWeapons[]{weapons[0],weapons[1]};
        const int primary=selected==1?1:0,secondary=1-primary;
        const auto& bindings=BLVR::ActiveBindings();
        if(BLVR::TouchValue(BLVR::TouchFromPose(input),secondary==0?bindings.supportLeft:bindings.supportRight)<.25f) {supportHeld=false;return;}
        const XMMATRIX weapon=XMLoadFloat4x4(&weapons[primary]);
        const XMVECTOR controller=(matrix(input.controllers[secondary].gripPose)*XMLoadFloat4x4(&nav)).r[3];
        const XMVECTOR local=XMVector3TransformCoord(controller,XMMatrixInverse(nullptr,weapon));
        if(!supportHeld) {
            if(selected==1) {
                // Capture a second point on the actual axe haft, separated
                // from the primary hand, rather than snapping to its blade.
                const float y=std::clamp(XMVectorGetY(local),-.02f,.70f);
                supportPoint={.068f,y,0};
            } else supportPoint={.035f,-.32f,.075f};
            const XMVECTOR candidate=XMVector3TransformCoord(XMLoadFloat3(&supportPoint),weapon);
            if(length(candidate-controller)>.22f) return;
            supportHeld=true;
        }
        const int primaryWrist=bone(primary==0?"Lf_Wrist":"Rt_Wrist");
        const XMMATRIX primaryPalm=palm(primary)*XMLoadFloat4x4(&bones[primaryWrist].inverse)*XMLoadFloat4x4(&posed[primaryWrist])*body;
        const XMVECTOR pivot=primaryPalm.r[3];
        const XMVECTOR source=XMVector3TransformCoord(XMLoadFloat3(&supportPoint),weapon);
        if(length(source-pivot)<.12f||length(controller-pivot)<.12f) {supportHeld=false;return;}
        if(!authored) {
            const XMMATRIX delta=move(-pivot)*aim(source-pivot,controller-pivot)*move(pivot);
            XMFLOAT4X4 firstPalm;XMStoreFloat4x4(&firstPalm,primaryPalm*delta);
            auto first=input.controllers[primary];first.squeeze=1;
            arm(primary,first,body,&firstPalm);
        }
        // The weapon's authored primary wrist attachment remains authoritative
        // if shoulder reach clamping moved that hand.
        const XMMATRIX attached=XMLoadFloat4x4(&weapons[primary]);
        XMMATRIX secondPalm=XMMatrixScaling(handScale,handScale,handScale)*palmOffset(secondary)
            *matrix(input.controllers[secondary].gripPose)*XMLoadFloat4x4(&nav);
        secondPalm.r[3]=XMVectorSetW(XMVector3TransformCoord(XMLoadFloat3(&supportPoint),attached),1);
        XMFLOAT4X4 second;XMStoreFloat4x4(&second,secondPalm);
        auto other=input.controllers[secondary];other.squeeze=1;arm(secondary,other,body,&second);
        // Reject unreachable secondary grips instead of leaving a floating hand.
        const int secondWrist=bone(secondary==0?"Lf_Wrist":"Rt_Wrist");
        const XMMATRIX actual=palm(secondary)*XMLoadFloat4x4(&bones[secondWrist].inverse)*XMLoadFloat4x4(&posed[secondWrist])*body;
        if(length(actual.r[3]-secondPalm.r[3])>.10f) {
            // A failed second-hand solve must not leave the primary weapon
            // rotated or the free hand pinned to an unreachable attachment.
            supportHeld=false;posed=unconstrainedPose;
            std::memcpy(weapons,unconstrainedWeapons,sizeof(weapons));return;
        }
        if(supportHeld&&selected==1)fitGrip(secondary,posed,attached*XMMatrixInverse(nullptr,body),false);
    }
    void update(const blvr_xr_bridge::PoseBridge& input,float dt,bool openingRoom,uint32_t nativeSolo,bool nativeDriving,
        const BLVR::ControlBindings& bindings) {
        opening=openingRoom;
        clock+=dt;
        if(!(input.flags&blvr_xr_bridge::PoseBridgeHmdValid)||!usablePose(input.hmdPose)) {
            // Do not republish the preceding pose or retain an interaction
            // pulse while the exact source head pose is unavailable.
            sourceInput={};tracked[0]=tracked[1]=false;
            action=-1;playing=false;actionPulseUntil=pressUntil=0;
            motionValid=false;supportHeld=false;return;
        }
        nativeDriving=nativeDriving&&!opening;
        if(nativeDriving!=driving) {
            if(nativeDriving) {weaponBeforeDriving=selected;selected=0;}
            else selected=weaponBeforeDriving;
            driving=nativeDriving;
            supportHeld=false;motionValid=false;action=-1;actionPulseUntil=0;
            // Enter/exit must not interpret a held face button as a new equip.
            lastSelectionButtons=input.controllers[0].buttons;
        }
        if(driving)nativeSolo=0;
        sourceInput=input;
        const auto& head=input.hmdPose;
        if(!latched || generation!=input.referenceSpaceGeneration || recenter!=input.recenterRequestId) {
            latched=true; generation=input.referenceSpaceGeneration; recenter=input.recenterRequestId;
            origin={head.position[0],head.position[1],head.position[2]}; navigation={};yaw=0;
            // LOCAL's origin is commonly at the headset, not at the floor.
            // Move the room into that space; never infer size from LOCAL.y.
            floorY=origin.y-blvr_rig::EyeHeightMeters;
            XMStoreFloat4x4(&nav,XMMatrixIdentity());
            XMVECTOR forward=XMVector3TransformNormal(XMVectorSet(0,0,-1,0),matrix(head));
            const float angle=std::atan2(-XMVectorGetX(forward),-XMVectorGetZ(forward));
            XMMATRIX initial=XMMatrixRotationY(angle)*XMMatrixTranslation(origin.x,floorY,origin.z);
            XMStoreFloat4x4(&panel,XMMatrixTranslation(0,2.10f,-3.1f)*initial);
            lastContact[0]=lastContact[1]=1; contactLatched=false; turnLatched=false;
            motionValid=false;supportHeld=false;
        }
        XMMATRIX navigationMatrix=XMMatrixRotationY(yaw)*XMMatrixTranslation(navigation.x,navigation.y,navigation.z);
        XMMATRIX headWorld=matrix(head)*navigationMatrix;
        const auto& right=input.controllers[1];
        sourceControlsSignature=BLVR::ControlsSignature(bindings);
        const auto touch=BLVR::TouchFromPose(input);
        const bool commandMode=BLVR::CommandHeld(touch,bindings);
        const bool pause=BLVR::ScopedActionDown(touch,BLVR::UiStart,commandMode,bindings)||
            BLVR::ScopedActionDown(touch,BLVR::Journal,commandMode,bindings)||BLVR::RecenterHeld(touch,bindings);
        const bool earthshaker=!opening&&!driving&&!commandMode&&!nativeSolo&&
            !pause&&BLVR::EarthshakerHeld(touch,bindings);
        // BuildRadial also needs the guitar as its native UI carrier. Preserve
        // that attachment policy without interpreting it as timed solo notes.
        const bool soloMode=!driving&&(nativeSolo||touch.hostRadial||(!commandMode&&!pause&&
            BLVR::ScopedActionDown(touch,BLVR::RockStance,commandMode,bindings)));
        if(earthshaker&&!earthshakerDown){action=3;actionStart=clock;physicalPlayback=false;}
        earthshakerDown=earthshaker;
        if(pause||commandMode||driving){action=-1;actionPulseUntil=0;}
        if(nativeSolo!=lastNativeSolo) {
            // Opening a menu or touching a selection cannot carry an old
            // combat strum into the first timed note.
            motionValid=false;actionPulseUntil=0;lastNoteButtons=0;
            lastNativeSolo=nativeSolo;
        }
        const unsigned selections=(driving||commandMode||soloMode||earthshaker||pause)?0:
            (BLVR::TouchValue(touch,bindings.equipAxe)>.5f?1u:0u)|(BLVR::TouchValue(touch,bindings.equipGuitar)>.5f?2u:0u);
        if(driving||commandMode||soloMode||earthshaker) pendingSelection=0;
        pendingSelection|=selections;
        unsigned chosen=selected;
        if(soloMode) chosen=2;
        // Equipment selection is separate from the two-grip Earthshaker chord.
        // Selection remains on release; a two-button selection is ignored.
        if(!selections&&lastSelectionButtons) {
            if(pendingSelection==1u) chosen=selected==1?0u:1u;
            if(pendingSelection==2u) chosen=selected==2?0u:2u;
            pendingSelection=0;
        }
        if(chosen!=selected) {selected=chosen;supportHeld=false;motionValid=false;action=-1;}
        lastSelectionButtons=selections;
        float turnX=0,turnY=0,moveX=0,moveY=0;
        BLVR::StickValues(touch,bindings.turnStick,turnX,turnY);
        BLVR::StickValues(touch,bindings.movementStick,moveX,moveY);
        if(std::fabs(turnX)<.3f) turnLatched=false;
        if(opening&&!commandMode&&!soloMode&&std::fabs(turnX)>.7f && !turnLatched) {
            const XMVECTOR pivot=headWorld.r[3];
            yaw+=turnX>0?-.785398163f:.785398163f; turnLatched=true;
            const XMVECTOR rotated=XMVector3TransformCoord(matrix(head).r[3],XMMatrixRotationY(yaw));
            XMStoreFloat3(&navigation,pivot-rotated);
        }
        navigationMatrix=XMMatrixRotationY(yaw)*XMMatrixTranslation(navigation.x,navigation.y,navigation.z);
        headWorld=matrix(head)*navigationMatrix;
        const XMVECTOR forward=XMVector3Normalize(XMVectorSetY(-headWorld.r[2],0));
        const XMVECTOR rightward=XMVector3Normalize(XMVectorSetY(headWorld.r[0],0));
        const float magnitude=std::hypot(moveX,moveY);
        if(opening&&magnitude>.2f) {
            const float speed=1.35f*std::clamp((magnitude-.2f)/.8f,0.f,1.f)*dt;
            const XMVECTOR displacement=(forward*moveY+rightward*moveX)*(speed/magnitude);
            navigation.x+=XMVectorGetX(displacement); navigation.z+=XMVectorGetZ(displacement);
            const XMMATRIX roomWorld=XMMatrixTranslation(0,-2.10f,3.1f)*XMLoadFloat4x4(&panel);
            const XMMATRIX candidateRoom=matrix(head)*XMMatrixRotationY(yaw)*XMMatrixTranslation(navigation.x,navigation.y,navigation.z)*XMMatrixInverse(nullptr,roomWorld);
            const float px=XMVectorGetX(candidateRoom.r[3]),pz=XMVectorGetZ(candidateRoom.r[3]);
            const float radius=std::hypot(px,pz);
            if(radius>6.35f) {
                const XMVECTOR correction=XMVector3TransformNormal(XMVectorSet(px*(6.35f/radius-1),0,pz*(6.35f/radius-1),0),roomWorld);
                navigation.x+=XMVectorGetX(correction);navigation.z+=XMVectorGetZ(correction);
            }
            const XMMATRIX candidate=matrix(head)*XMMatrixRotationY(yaw)*XMMatrixTranslation(navigation.x,navigation.y,navigation.z);
            const XMVECTOR local=XMVector3TransformCoord(candidate.r[3],XMMatrixInverse(nullptr,XMLoadFloat4x4(&panel)));
            if(std::fabs(XMVectorGetX(local))<2.12f&&XMVectorGetZ(local)<.40f) {
                const XMVECTOR correction=XMLoadFloat4x4(&panel).r[2]*(.40f-XMVectorGetZ(local));
                navigation.x+=XMVectorGetX(correction);navigation.z+=XMVectorGetZ(correction);
            }
        }
        navigationMatrix=XMMatrixRotationY(yaw)*XMMatrixTranslation(navigation.x,navigation.y,navigation.z);
        XMStoreFloat4x4(&nav,navigationMatrix); headWorld=matrix(head)*navigationMatrix;
        const XMVECTOR back=headWorld.r[2]; const float bodyYaw=std::atan2(XMVectorGetX(back),XMVectorGetZ(back))+XM_PI;
        XMMATRIX body=XMMatrixScaling(scale,scale,scale)*XMMatrixRotationY(bodyYaw);
        const XMVECTOR refEye=XMVector3TransformNormal(XMLoadFloat3(&eye),body);
        XMVECTOR rootPosition=headWorld.r[3]-refEye;
        body=body*move(rootPosition); XMStoreFloat4x4(&root,body);
        for(size_t i=0;i<bones.size();++i) posed[i]=bones[i].reference;
        for(int hand=0;hand<2;++hand) {
            auto controller=input.controllers[hand];
            tracked[hand]=(controller.activeFlags&blvr_xr_bridge::ControllerGripPose)!=0&&usablePose(controller.gripPose);
            equipped[hand]=tracked[hand] && selected==(hand==0?2u:1u);
            if(equipped[hand]) controller.squeeze=1;
            if(tracked[hand]) arm(hand,controller,body);

        }
        if((nativeSolo&2u)||earthshaker) supportHeld=false;
        else twoHand(input,body);
        playing=action>=0;
        if(playing && clock-actionStart >= (action==2?.86f:1.25f)) {
            action=-1;playing=false;
        }
        // The host publishes tracking and an action request. The x86 renderer
        // retargets its current native pose inside that exact stereo pair.
        for(size_t i=0;i<bones.size();++i)
            XMStoreFloat4x4(&skin[i],XMLoadFloat4x4(&bones[i].inverse)*XMLoadFloat4x4(&posed[i]));
        context->UpdateSubresource(palette.Get(),0,nullptr,skin.data(),0,0);
        // Contact is the skinned index fingertip, in this exact rendered rig.
        const XMMATRIX buttonWorld=XMMatrixTranslation(0,-1.06f,.16f)*XMLoadFloat4x4(&panel);
        const XMMATRIX invButton=XMMatrixInverse(nullptr,buttonWorld);
        bool released=true,pressed=false;
        for(int hand=0;hand<2;++hand) {
            if(!opening||!tracked[hand]||equipped[hand]||playing||supportHeld) {lastContact[hand]=1;continue;}
            const int index=bone(hand==0?"Lf_Index3":"Rt_Index3");
            const XMVECTOR tip=XMVector3TransformCoord(point(posed[index]),body*invButton);
            const float x=XMVectorGetX(tip),y=XMVectorGetY(tip),z=XMVectorGetZ(tip);
            const bool inside=std::fabs(x)<.46f && std::fabs(y)<.16f;
            if(inside && z<.035f && z>-.15f && lastContact[hand]>.035f) pressed=true;
            if(inside && z<.10f) released=false;
            lastContact[hand]=z;
        }
        // Aimed trigger is an accessible alternative using the same button.
        const bool trigger=BLVR::TouchValue(touch,bindings.openingConfirmAlternate)>.65f;
        if(opening&&!selected&&trigger && !triggerDown && (right.activeFlags&blvr_xr_bridge::ControllerAimPose)) {
            const XMMATRIX ray=matrix(right.aimPose)*navigationMatrix*invButton;
            const XMVECTOR o=ray.r[3],dir=-ray.r[2];
            const float dz=XMVectorGetZ(dir),oz=XMVectorGetZ(o);
            if(dz<-.001f && oz>0) {
                const float t=-oz/dz; const XMVECTOR hit=o+dir*t;
                if(t<5 && std::fabs(XMVectorGetX(hit))<.46f && std::fabs(XMVectorGetY(hit))<.16f) pressed=true;
            }
        }
        triggerDown=trigger;
        if(released) contactLatched=false;
        if(pressed && !contactLatched) {pressUntil=clock+.22f;contactLatched=true;}
        const unsigned noteButtons=((nativeSolo&2u)&&!commandMode&&!pause&&BLVR::TouchValue(touch,bindings.soloFret)<=.5f)?
            (BLVR::ScopedActionDown(touch,BLVR::SoloNote1,commandMode,bindings)?1u:0u)|
            (BLVR::ScopedActionDown(touch,BLVR::SoloNote2,commandMode,bindings)?2u:0u)|
            (BLVR::ScopedActionDown(touch,BLVR::SoloNote3,commandMode,bindings)?4u:0u):0u;
        const bool notePressed=(noteButtons&~lastNoteButtons)!=0;
        lastNoteButtons=noteButtons;
        const bool actionTrigger=BLVR::ScopedActionDown(touch,selected==2?BLVR::Guitar:BLVR::Axe,commandMode,bindings)&&
            !soloMode&&!commandMode&&!driving&&!pause&&!earthshaker;
        if(selected&&((actionTrigger&&!actionDown[1])||notePressed)&&!pressed) {
            action=static_cast<int>(selected);actionStart=clock;
            // Button attacks retarget the matching owned retail arm motion.
            // Each new press is visible, including presses during recovery.
            // Physical swings/strums below retain the tracked controller pose.
            physicalPlayback=false;
        }
        actionDown[1]=actionTrigger;
        // Controller motion is measured relative to the head/weapon, so walking
        // and snap turns cannot manufacture a melee strike or a guitar strum.
        XMFLOAT3 axeTip{},strum{},pickingHand{};
        if(tracked[1])XMStoreFloat3(&pickingHand,matrix(right.gripPose).r[3]-matrix(head).r[3]);
        if(selected==1&&tracked[1]) {
            const int wrist=bone("Rt_Wrist");
            const XMMATRIX controllerWeapon=XMLoadFloat4x4(&gripWeapon[1])*XMLoadFloat4x4(&bones[wrist].reference)
                *XMMatrixInverse(nullptr,palm(1))*XMMatrixScaling(handScale,handScale,handScale)*palmOffset(1)*matrix(right.gripPose);
            const XMVECTOR tip=XMVector3TransformCoord(XMVectorSet(0,1.28f,0,1),controllerWeapon)-matrix(head).r[3];
            XMStoreFloat3(&axeTip,tip);
        }
        if(selected==2&&tracked[0]&&tracked[1]) {
            const int index=bone("Rt_Index3"),previous=bone("Rt_Index2");
            const XMVECTOR referenceTip=position(index)+(position(index)-position(previous))*.72f;
            const XMVECTOR tip=XMVector3TransformCoord(referenceTip,XMLoadFloat4x4(&bones[index].inverse)*XMLoadFloat4x4(&posed[index]));
            XMStoreFloat3(&strum,XMVector3TransformCoord(tip,body*XMMatrixInverse(nullptr,XMLoadFloat4x4(&weapons[0]))));
        }
        bool gesture=false;
        if(motionValid&&dt>.003f&&dt<.10f&&(action<0||physicalPlayback)) {
            const float travel=length(XMLoadFloat3(&axeTip)-XMLoadFloat3(&lastAxeTip));
            const float speed=travel/dt;
            if(speed<.4f)swingArmed=true;
            if(selected==1&&tracked[1]&&swingArmed&&speed>1.8f&&travel<.25f) {gesture=true;swingArmed=false;}
            const bool crosses=(strum.x<0)!=(lastStrum.x<0);
            const float strumSpeed=length(XMLoadFloat3(&strum)-XMLoadFloat3(&lastStrum))/dt;
            const float pickingSpeed=length(XMLoadFloat3(&pickingHand)-XMLoadFloat3(&lastPickingHand))/dt;
            // Only the right picking hand plays the strings. Moving the neck
            // with the left hand past an idle right hand must not play a note.
            // Test the swept crossing, not just the final sample, so an actual
            // downstroke or upstroke can pass all the way across the strings.
            const float fraction=crosses?lastStrum.x/(lastStrum.x-strum.x):0.f;
            const float crossingY=lastStrum.y+(strum.y-lastStrum.y)*fraction;
            const float crossingZ=lastStrum.z+(strum.z-lastStrum.z)*fraction;
            if(selected==2&&!(nativeSolo&1u)&&tracked[0]&&tracked[1]&&!supportHeld&&crosses&&crossingY>-.65f&&crossingY<.01f&&
               std::fabs(crossingZ-.045f)<.23f&&strumSpeed>.25f&&strumSpeed<12&&pickingSpeed>.15f)gesture=true;
        }
        if(gesture&&clock-lastPhysicalAction>(selected==2?.18f:.35f)) {
            action=static_cast<int>(selected);actionStart=clock;physicalPlayback=true;
            lastPhysicalAction=clock;pulseAction=selected;actionPulseUntil=clock+(selected==2?.09f:.18f);
        }
        lastAxeTip=axeTip;lastStrum=strum;lastPickingHand=pickingHand;
        motionValid=!driving&&(selected==1?tracked[1]:(tracked[0]&&tracked[1]));
    }
    void draw(ID3D11Buffer* vb,ID3D11Buffer* ib,unsigned count,ID3D11ShaderResourceView* srv,
              XMMATRIX world,XMMATRIX vp,XMFLOAT4 color,XMFLOAT4 mode) {
        Constants c{};XMStoreFloat4x4(&c.vp,vp);XMStoreFloat4x4(&c.world,world);c.color=color;c.mode=mode;
        context->UpdateSubresource(constants.Get(),0,nullptr,&c,0,0);
        unsigned stride=sizeof(Vertex),offset=0; context->IASetVertexBuffers(0,1,&vb,&stride,&offset);
        context->IASetIndexBuffer(ib,DXGI_FORMAT_R32_UINT,0); context->PSSetShaderResources(0,1,&srv);
        context->DrawIndexed(count,0,0);
    }
    void render(ID3D11RenderTargetView* target,unsigned w,unsigned h,const XrView& view,ID3D11ShaderResourceView* menu) {
        if(!loaded||!latched) return;
        if(width!=w||height!=h) {
            depthView.Reset();depth.Reset();width=w;height=h;
            D3D11_TEXTURE2D_DESC desc{};desc.Width=w;desc.Height=h;desc.MipLevels=desc.ArraySize=1;
            desc.Format=DXGI_FORMAT_D32_FLOAT;desc.SampleDesc.Count=1;desc.BindFlags=D3D11_BIND_DEPTH_STENCIL;
            check(device->CreateTexture2D(&desc,nullptr,&depth),"Opening depth");
            check(device->CreateDepthStencilView(depth.Get(),nullptr,&depthView),"Opening depth view");
        }
        blvr_xr_bridge::Pose eyePose{};
        std::memcpy(eyePose.orientation,&view.pose.orientation,sizeof(view.pose.orientation));
        std::memcpy(eyePose.position,&view.pose.position,sizeof(view.pose.position));
        const XMMATRIX viewMatrix=XMMatrixInverse(nullptr,matrix(eyePose)*XMLoadFloat4x4(&nav));
        constexpr float nearZ=.025f,farZ=80;
        const XMMATRIX projection=XMMatrixPerspectiveOffCenterRH(std::tan(view.fov.angleLeft)*nearZ,
            std::tan(view.fov.angleRight)*nearZ,std::tan(view.fov.angleDown)*nearZ,std::tan(view.fov.angleUp)*nearZ,nearZ,farZ);
        const XMMATRIX vp=viewMatrix*projection;
        const float background[]{.018f,.023f,.035f,1}; context->ClearRenderTargetView(target,background);
        context->ClearDepthStencilView(depthView.Get(),D3D11_CLEAR_DEPTH,1,0);
        context->OMSetRenderTargets(1,&target,depthView.Get()); context->OMSetDepthStencilState(depthState.Get(),0);
        context->OMSetBlendState(nullptr,nullptr,0xffffffff);
        D3D11_VIEWPORT viewport{0,0,static_cast<float>(w),static_cast<float>(h),0,1};context->RSSetViewports(1,&viewport);
        context->RSSetState(rasterizer.Get());context->IASetInputLayout(layout.Get());
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vs.Get(),nullptr,0);context->PSSetShader(ps.Get(),nullptr,0);
        ID3D11Buffer* buffers[]{constants.Get(),palette.Get()}; context->VSSetConstantBuffers(0,2,buffers);
        context->PSSetConstantBuffers(0,1,buffers);ID3D11SamplerState* samplers[]{sampler.Get()};context->PSSetSamplers(0,1,samplers);
        auto quad=[&](XMMATRIX world,ID3D11ShaderResourceView* srv,XMFLOAT4 color,XMFLOAT4 mode) {
            draw(quadVertices.Get(),quadIndices.Get(),6,srv,world,vp,color,mode);
        };
        const XMMATRIX panelWorld=XMLoadFloat4x4(&panel);
        const XMMATRIX roomWorld=XMMatrixTranslation(0,-2.10f,3.1f)*panelWorld;
        ID3D11ShaderResourceView* stoneView=stoneMaterial.Get();context->PSSetShaderResources(1,1,&stoneView);
        draw(roomVertices.Get(),roomIndices.Get(),roomIndexCount,white.Get(),roomWorld,vp,{1,1,1,1},{0,0,2,clock});
        quad(XMMatrixScaling(4.12f,4.12f,1)*XMMatrixRotationX(-XM_PIDIV2)*XMMatrixTranslation(0,.005f,0)*roomWorld,
             medallion.Get(),{1,1,1,1},{0,0,3,0});
        // The game's own art and typography occupy the iron display. No host
        // heading, weapon caption or instructional placard surrounds it.
        if(menu)quad(XMMatrixScaling(3.1f,1.74375f,1)*panelWorld,menu,{1,1,1,1},{0,1,0,0});
        else quad(XMMatrixScaling(1.5f,1.5f,1)*XMMatrixTranslation(0,0,.01f)*panelWorld,medallion.Get(),{1,1,1,1},{0,0,3,0});
        quad(XMMatrixScaling(.92f,.32f,1)*XMMatrixTranslation(0,-1.06f,clock<pressUntil?.13f:.16f)*panelWorld,
             white.Get(),clock<pressUntil?XMFLOAT4{1.5f,1.5f,1.5f,1}:XMFLOAT4{1,1,1,1},{0,0,1,0});
        for(const auto& mesh:meshes) {
            XMMATRIX transform=XMLoadFloat4x4(&root);
            if(mesh.group) {
                const unsigned hand=mesh.group==1?1u:0u;
                if(!equipped[hand]) continue;
                transform=XMLoadFloat4x4(&weapons[hand]);
            }
            draw(mesh.vb.Get(),mesh.ib.Get(),mesh.count,mesh.texture.Get(),transform,vp,mesh.color,
                 {mesh.group==0?1.f:0.f,0,0,0});
        }
        ID3D11ShaderResourceView* noViews[2]{};context->PSSetShaderResources(0,2,noViews);
        context->OMSetRenderTargets(0,nullptr,nullptr);context->OMSetDepthStencilState(nullptr,0);
    }
};

EddieLobby::EddieLobby():impl_(std::make_unique<Impl>()) {}
EddieLobby::~EddieLobby()=default;
bool EddieLobby::exportRig(blvr_xr_bridge::RigFrame& output) const {
    const auto& p=*impl_;
    // The simulator recorder can observe the solved opening-room rig for
    // physical interaction choreography. Ordinary sessions keep retail-only publication.
    static const bool recording=[] {char name[128]{};return GetEnvironmentVariableA("BLVR_FINAL_EYE_CAPTURE",name,sizeof(name))>0;}();
    if(!p.loaded||!p.latched||(p.opening&&!recording)||!p.sourceInput.frameId) return false;
    output={};output.magic=blvr_xr_bridge::RigMagic;
    output.frameId=p.sourceInput.frameId;output.predictedDisplayTime=p.sourceInput.predictedDisplayTime;
    output.boneCount=static_cast<uint32_t>(p.bones.size());
    output.version=blvr_xr_bridge::RigVersion;output.structBytes=sizeof(output);
    output.trackedHandMask=(p.tracked[0]?1u:0u)|(p.tracked[1]?2u:0u);
    // Preserve equipment selection on tracking loss. The native renderer owns
    // the current arm and weapon attachment for each unavailable grip.
    output.selectedWeapon=p.selected;
    output.controlsSignature=p.sourceControlsSignature;
    output.supportHeld=p.supportHeld?1u:0u;
    if(p.playing&&!p.physicalPlayback&&!p.opening&&
       (p.action==3?output.trackedHandMask==3:p.action==2?output.trackedHandMask==3:(output.trackedHandMask&2u))) {
        output.liveAction=static_cast<uint32_t>(p.action);
        const float time=p.clock-p.actionStart,duration=p.action==2?.86f:1.25f;
        output.liveActionWeight=std::clamp((std::min)(time/.10f,(duration-time)/.18f),0.f,1.f);
    }
    output.skeletonSignature=14695981039346656037ull;
    const XMMATRIX headInverse=XMMatrixInverse(nullptr,matrix(p.sourceInput.hmdPose)*XMLoadFloat4x4(&p.nav));
    const XMMATRIX bodyToHead=XMLoadFloat4x4(&p.root)*headInverse;
    for(size_t i=0;i<p.bones.size();++i) {
        output.skeletonSignature=blvr_xr_bridge::RigNameHash(output.skeletonSignature,p.bones[i].name.c_str());
        XMFLOAT4X4 value;XMStoreFloat4x4(&value,XMLoadFloat4x4(&p.skin[i])*bodyToHead);
        std::memcpy(output.skinToHead[i],&value,sizeof(value));
    }
    for(int weapon=0;weapon<2;++weapon) {
        XMFLOAT4X4 value;XMStoreFloat4x4(&value,XMLoadFloat4x4(&p.weapons[weapon==0?1:0])*headInverse);
        std::memcpy(output.weaponToHead[weapon],&value,sizeof(value));
    }
    return true;
}
bool EddieLobby::exportUiMounts(UiMounts& output) const {
    output={};
    const auto& p=*impl_;
    if(!p.loaded||!p.latched||p.opening||!p.sourceInput.frameId) return false;
    // These are the solved anatomical joints, including reach clamping and
    // two-hand constraints, not an independently moving controller proxy.
    const XMMATRIX toLocal=XMMatrixInverse(nullptr,XMLoadFloat4x4(&p.nav));
    const XMMATRIX body=XMLoadFloat4x4(&p.root)*toLocal;
    const auto save=[&](unsigned mount,XMMATRIX transform) {
        XMVECTOR scale,rotation,translation;
        if(!XMMatrixDecompose(&scale,&rotation,&translation,transform)) return;
        XMStoreFloat4(reinterpret_cast<XMFLOAT4*>(&output.poses[mount].orientation),XMQuaternionNormalize(rotation));
        XMStoreFloat3(reinterpret_cast<XMFLOAT3*>(&output.poses[mount].position),translation);
        output.validMask|=1u<<mount;
    };
    for(int hand=0;hand<2;++hand) if(p.tracked[hand]) {
        const int wrist=p.bone(hand==0?"Lf_Wrist":"Rt_Wrist");
        const XMMATRIX palm=p.palm(hand)*XMLoadFloat4x4(&p.bones[wrist].inverse)*XMLoadFloat4x4(&p.posed[wrist])*body;
        const XMMATRIX backPlane(XMVectorSet(1,0,0,0),XMVectorSet(0,0,-1,0),
            XMVectorSet(0,1,0,0),XMVectorSet(0,.055f,.16f,1));
        save(hand==0?LeftForearm:RightForearm,backPlane*palm);
        if(hand==0) {
            const XMMATRIX palmPlane(XMVectorSet(-1,0,0,0),XMVectorSet(0,0,-1,0),
                XMVectorSet(0,-1,0,0),XMVectorSet(0,-.10f,-.055f,1));
            save(AbovePalm,palmPlane*palm);
        }
        const int tip=p.bone(hand==0?"Lf_Index3":"Rt_Index3");
        const int previous=p.bone(hand==0?"Lf_Index2":"Rt_Index2");
        const XMVECTOR referenceTip=p.position(tip)+(p.position(tip)-p.position(previous))*.72f;
        const XMVECTOR tipLocal=XMVector3TransformCoord(referenceTip,XMLoadFloat4x4(&p.bones[tip].inverse));
        XMStoreFloat3(reinterpret_cast<XMFLOAT3*>(&output.indexTips[hand]),
            XMVector3TransformCoord(tipLocal,XMLoadFloat4x4(&p.posed[tip])*body));
        output.tipMask|=1u<<hand;
    }
    if(p.selected==2&&p.tracked[0]&&p.guitarHeadstockY>-100) {
        // Guitar +Y runs up the neck, +Z is the string face. This screen's
        // right points back along the neck and its top rises off the strings.
        const XMMATRIX screen(XMVectorSet(0,-1,0,0),XMVectorSet(0,0,1,0),
            XMVectorSet(-1,0,0,0),XMVectorSet(0,p.guitarHeadstockY-.10f,.24f,1));
        save(GuitarHeadstock,screen*XMLoadFloat4x4(&p.weapons[0])*toLocal);
    }
    return output.validMask!=0;
}
bool EddieLobby::readRenderedUi(uint64_t epoch,uint64_t sourceFrame,uint64_t poseFrame,
    int64_t displayTime,const blvr_xr_bridge::Pose& head,UiMounts& output,bool& driving) const {
    driving=false;
    static HANDLE mapping=nullptr;
    static const blvr_xr_bridge::NativeHandHistoryBuffer* history=nullptr;
    if(!history) {
        mapping=OpenFileMappingW(FILE_MAP_READ,FALSE,blvr_xr_bridge::NativeHandMappingName);
        if(!mapping)return false;
        history=static_cast<const blvr_xr_bridge::NativeHandHistoryBuffer*>(MapViewOfFile(mapping,FILE_MAP_READ,0,0,sizeof(*history)));
        if(!history){CloseHandle(mapping);mapping=nullptr;return false;}
    }
    const auto& source=history->slots[sourceFrame%blvr_xr_bridge::NativeHandHistory];
    const auto sequence=source.sequence;if(sequence&1)return false;
    blvr_xr_bridge::NativeHandFrame frame{};
    MemoryBarrier();std::memcpy(&frame,&source,sizeof(frame));MemoryBarrier();
    if(source.sequence!=sequence||frame.magic!=blvr_xr_bridge::NativeHandMagic
        ||frame.producerEpoch!=epoch||frame.sourceFrameId!=sourceFrame||frame.poseFrameId!=poseFrame
        ||frame.predictedDisplayTime!=displayTime||frame.validMask!=3
        ||frame.version!=blvr_xr_bridge::NativeHandVersion||frame.pointerValidMask>3||
        (frame.flags&~(blvr_xr_bridge::NativeUiDriving|blvr_xr_bridge::NativeUiGuitar))||!usablePose(head))return false;
    for(const auto& palm:frame.palmToHead)if(!usableMatrix(palm))return false;
    for(const auto& tip:frame.indexTipToHead)for(float v:tip)if(!std::isfinite(v)||std::fabs(v)>20)return false;
    if(frame.flags&blvr_xr_bridge::NativeUiGuitar)
        if(!usableMatrix(frame.guitarToHead))return false;
    UiMounts candidate{};
    const XMMATRIX headToLocal=matrix(head);
    const auto save=[&](unsigned mount,XMMATRIX transform) {
        XMVECTOR scale,rotation,translation;
        if(!XMMatrixDecompose(&scale,&rotation,&translation,transform))return false;
        XMStoreFloat4(reinterpret_cast<XMFLOAT4*>(&candidate.poses[mount].orientation),XMQuaternionNormalize(rotation));
        XMStoreFloat3(reinterpret_cast<XMFLOAT3*>(&candidate.poses[mount].position),translation);
        candidate.validMask|=1u<<mount;return true;
    };
    for(int hand=0;hand<2;++hand) {
        const XMMATRIX palm=XMLoadFloat4x4(reinterpret_cast<const XMFLOAT4X4*>(frame.palmToHead[hand]))*headToLocal;
        const XMMATRIX backPlane(XMVectorSet(1,0,0,0),XMVectorSet(0,0,-1,0),
            XMVectorSet(0,1,0,0),XMVectorSet(0,.055f,.16f,1));
        if(!save(hand==0?LeftForearm:RightForearm,backPlane*palm))return false;
        if(hand==0) {
            const XMMATRIX palmPlane(XMVectorSet(-1,0,0,0),XMVectorSet(0,0,-1,0),
                XMVectorSet(0,-1,0,0),XMVectorSet(0,-.10f,-.055f,1));
            if(!save(AbovePalm,palmPlane*palm))return false;
        }
        XMStoreFloat3(reinterpret_cast<XMFLOAT3*>(&candidate.indexTips[hand]),
            XMVector3TransformCoord(XMLoadFloat3(reinterpret_cast<const XMFLOAT3*>(frame.indexTipToHead[hand])),headToLocal));
        candidate.tipMask|=frame.pointerValidMask&(1u<<hand);
    }
    if((frame.flags&blvr_xr_bridge::NativeUiGuitar)&&impl_->guitarHeadstockY>-100) {
        const XMMATRIX screen(XMVectorSet(0,-1,0,0),XMVectorSet(0,0,1,0),
            XMVectorSet(-1,0,0,0),XMVectorSet(0,impl_->guitarHeadstockY-.10f,.24f,1));
        if(!save(GuitarHeadstock,screen*XMLoadFloat4x4(reinterpret_cast<const XMFLOAT4X4*>(frame.guitarToHead))*headToLocal))return false;
    }
    driving=(frame.flags&blvr_xr_bridge::NativeUiDriving)!=0;
    output=candidate;
    return true;
}
bool EddieLobby::ready() const {return impl_->loaded;}
bool EddieLobby::confirming() const {return impl_->clock<impl_->pressUntil;}
unsigned EddieLobby::selectedWeapon() const {return impl_->selected;}
unsigned EddieLobby::physicalAction() const {return impl_->clock<impl_->actionPulseUntil?impl_->pulseAction:0u;}
void EddieLobby::update(const blvr_xr_bridge::PoseBridge& input,float seconds,bool opening,uint32_t nativeSolo,bool driving) {if(ready()) impl_->update(input,std::clamp(seconds,0.f,.05f),opening,nativeSolo,driving,BLVR::ActiveBindings());}
void EddieLobby::render(ID3D11RenderTargetView* target,uint32_t width,uint32_t height,const XrView& view,ID3D11ShaderResourceView* menu) {impl_->render(target,width,height,view,menu);}

bool EddieLobby::initialize(ID3D11Device* device,ID3D11DeviceContext* context,const std::filesystem::path& directory,std::string& failure) {
    try {
        auto& p=*impl_;p.device=device;p.context=context;
        std::ifstream stream(directory/"eddie.rigcache",std::ios::binary);char magic[8];bytes(stream,magic,8);
        if(std::memcmp(magic,"BLVRIG02",8)) throw std::runtime_error("Eddie cache version mismatch");
        unsigned boneCount=0,meshCount=0;read(stream,boneCount);read(stream,meshCount);
        if(!boneCount||boneCount>MaxBones||meshCount>200) throw std::runtime_error("Invalid Eddie counts");
        for(unsigned i=0;i<boneCount;++i) {
            char name[64];bytes(stream,name,64);name[63]=0;Impl::Bone b{};b.name=name;read(stream,b.parent);read(stream,b.reference);
            if(b.parent>=static_cast<int>(i)||b.parent< -1) throw std::runtime_error("Invalid Eddie hierarchy");
            XMStoreFloat4x4(&b.inverse,XMMatrixInverse(nullptr,XMLoadFloat4x4(&b.reference)));p.bones.push_back(b);
        }
        XMStoreFloat3(&p.eye,(p.position(p.bone("Lf_Eye"))+p.position(p.bone("Rt_Eye")))*.5f);
        p.scale=blvr_rig::EyeHeightMeters/p.eye.y;
        const XMVECTOR middleTip=p.position(p.bone("Lf_Middle3"))
            +(p.position(p.bone("Lf_Middle3"))-p.position(p.bone("Lf_Middle2")))*.72f;
        p.handScale=blvr_rig::HandLengthMeters/length(middleTip-p.position(p.bone("Lf_Wrist")));
        if(p.scale<.5f||p.scale>1.5f||p.handScale<.5f||p.handScale>1.5f)
            throw std::runtime_error("Eddie metric calibration is incompatible with this skeleton");
        std::map<std::string,ComPtr<ID3D11ShaderResourceView>> textures;
        const unsigned whitePixel=0xffffffff;p.white=texture(device,1,1,&whitePixel);textures[""]=p.white;
        for(unsigned i=0;i<meshCount;++i) {
            Impl::Mesh mesh{};unsigned vertices=0;read(stream,mesh.group);read(stream,vertices);read(stream,mesh.count);
            if(mesh.group>2||vertices>100000||mesh.count>500000||mesh.count%3) throw std::runtime_error("Invalid Eddie subset");
            char path[128];bytes(stream,path,128);path[127]=0;read(stream,mesh.color);
            const std::string filename(path);
            if(filename.find_first_of("/\\:")!=std::string::npos || filename=="..") throw std::runtime_error("Invalid cache texture path");
            if(!textures.count(filename)) {
                std::ifstream file(directory/filename,std::ios::binary);unsigned w=0,h=0;read(file,w);read(file,h);
                if(!w||!h||w>4096||h>4096) throw std::runtime_error("Invalid Eddie texture size");
                std::vector<uint8_t> pixels(static_cast<size_t>(w)*h*4);bytes(file,pixels.data(),pixels.size());textures[filename]=texture(device,w,h,pixels.data());
            }
            mesh.texture=textures[filename]; std::vector<Vertex> v(vertices);std::vector<unsigned> indices(mesh.count);
            bytes(stream,v.data(),v.size()*sizeof(Vertex));bytes(stream,indices.data(),indices.size()*4);
            if(mesh.group==2) for(const auto& vertex:v)
                p.guitarHeadstockY=(std::max)(p.guitarHeadstockY,vertex.p.y);
            for(unsigned index:indices) if(index>=vertices) throw std::runtime_error("Invalid Eddie triangle");
            for(const auto& vertex:v) for(unsigned joint:vertex.joints) if(joint>=boneCount) throw std::runtime_error("Invalid Eddie skin joint");
            mesh.vb=buffer(device,D3D11_BIND_VERTEX_BUFFER,v.data(),static_cast<unsigned>(v.size()*sizeof(Vertex)));
            mesh.ib=buffer(device,D3D11_BIND_INDEX_BUFFER,indices.data(),static_cast<unsigned>(indices.size()*4));p.meshes.push_back(mesh);
        }
        if(stream.peek()!=std::ifstream::traits_type::eof()) throw std::runtime_error("Trailing Eddie cache data");
        for(int hand=0;hand<2;++hand) {
            std::array<XMFLOAT4X4,MaxBones> model;
            for(size_t i=0;i<p.bones.size();++i)model[i]=p.bones[i].reference;
            const int wrist=p.bone(hand==0?"Lf_Wrist":"Rt_Wrist");
            const XMMATRIX stockPalm=p.palm(hand);
            // Measured contact locations in the owned weapon's coordinates.
            // These socket calibrations are independent of an animation take.
            const XMVECTOR gripCenter=hand==0?XMVectorSet(.103506f,.28f,.014298f,1):
                XMVectorSet(-.068336f,.201650f,-.035816f,1);
            // Weapon +Y is the haft/neck, +Z the blade/string face. Align the
            // handle with OpenXR grip -Z while retaining the measured contact
            // position. Correct the socket, never the player's hand rotation.
            const float side=hand==0?1.f:-1.f;
            XMMATRIX weaponToPalm(XMVectorSet(0,side,0,0),XMVectorSet(side,0,0,0),
                XMVectorSet(0,0,-1,0),XMVectorSet(0,0,0,1));
            weaponToPalm.r[3]=XMVectorSetW(-XMVector3TransformNormal(gripCenter,weaponToPalm),1);
            const XMMATRIX heldWeapon=weaponToPalm*stockPalm;
            p.fitGrip(hand,model,heldWeapon,hand==0);
            for(size_t i=0;i<p.bones.size();++i) {
                const int parent=p.bones[i].parent;
                const XMMATRIX local=parent>=0?XMLoadFloat4x4(&model[i])*XMMatrixInverse(nullptr,XMLoadFloat4x4(&model[parent])):XMLoadFloat4x4(&model[i]);
                XMStoreFloat4x4(&p.gripLocal[hand][i],local);
            }
            XMStoreFloat4x4(&p.gripWeapon[hand],heldWeapon*XMMatrixInverse(nullptr,XMLoadFloat4x4(&model[wrist])));
        }
        ComPtr<ID3DBlob> vs,ps,errors;
        HRESULT hr=D3DCompile(Shader,std::strlen(Shader),"eddie_lobby",nullptr,nullptr,"vsMain","vs_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&vs,&errors);
        if(FAILED(hr)) throw std::runtime_error(errors?static_cast<char*>(errors->GetBufferPointer()):"Eddie VS compilation");
        hr=D3DCompile(Shader,std::strlen(Shader),"eddie_lobby",nullptr,nullptr,"psMain","ps_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&ps,&errors);
        if(FAILED(hr)) throw std::runtime_error(errors?static_cast<char*>(errors->GetBufferPointer()):"Eddie PS compilation");
        check(device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&p.vs),"Eddie VS");
        check(device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&p.ps),"Eddie PS");
        const D3D11_INPUT_ELEMENT_DESC elements[]={
            {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,24,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"BLENDWEIGHT",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,32,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"BLENDINDICES",0,DXGI_FORMAT_R32G32B32A32_UINT,0,48,D3D11_INPUT_PER_VERTEX_DATA,0}};
        check(device->CreateInputLayout(elements,5,vs->GetBufferPointer(),vs->GetBufferSize(),&p.layout),"Eddie layout");
        p.constants=buffer(device,D3D11_BIND_CONSTANT_BUFFER,nullptr,sizeof(Constants));
        p.palette=buffer(device,D3D11_BIND_CONSTANT_BUFFER,nullptr,sizeof(p.skin));
        const Vertex quad[]={{{-.5f,-.5f,0},{0,0,1},{0,1},{1,0,0,0},{0,0,0,0}},
            {{-.5f,.5f,0},{0,0,1},{0,0},{1,0,0,0},{0,0,0,0}},
            {{.5f,.5f,0},{0,0,1},{1,0},{1,0,0,0},{0,0,0,0}},
            {{.5f,-.5f,0},{0,0,1},{1,1},{1,0,0,0},{0,0,0,0}}};
        const unsigned indices[]{0,1,2,0,2,3};
        p.quadVertices=buffer(device,D3D11_BIND_VERTEX_BUFFER,quad,sizeof(quad));p.quadIndices=buffer(device,D3D11_BIND_INDEX_BUFFER,indices,sizeof(indices));
        D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;raster.DepthClipEnable=TRUE;
        check(device->CreateRasterizerState(&raster,&p.rasterizer),"Eddie rasterizer");
        D3D11_DEPTH_STENCIL_DESC ds{};ds.DepthEnable=TRUE;ds.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;ds.DepthFunc=D3D11_COMPARISON_LESS_EQUAL;
        check(device->CreateDepthStencilState(&ds,&p.depthState),"Eddie depth state");
        D3D11_SAMPLER_DESC sampler{};sampler.Filter=D3D11_FILTER_ANISOTROPIC;sampler.MaxAnisotropy=8;
        sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;sampler.MaxLOD=D3D11_FLOAT32_MAX;
        check(device->CreateSamplerState(&sampler,&p.sampler),"Eddie sampler");
        const RoadieRoom room;
        p.roomVertices=buffer(device,D3D11_BIND_VERTEX_BUFFER,room.vertices.data(),static_cast<unsigned>(room.vertices.size()*sizeof(Vertex)));
        p.roomIndices=buffer(device,D3D11_BIND_INDEX_BUFFER,room.indices.data(),static_cast<unsigned>(room.indices.size()*sizeof(uint32_t)));
        p.roomIndexCount=static_cast<unsigned>(room.indices.size());
        p.medallion=roomArtwork(device,context,directory.parent_path().parent_path()/"assets/room/iron-medallion.png");
        p.stoneMaterial=roomArtwork(device,context,directory.parent_path().parent_path()/"assets/room/basalt.png");
        p.loaded=true;failure.clear();return true;
    } catch(const std::exception& e) {failure=e.what();return false;}
}

bool EddieLobby::visualTest(const std::filesystem::path& assets,const std::filesystem::path& output,std::string& failure) {
    try {
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level{};
        check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context),"Rig test device");
        EddieLobby lobby;if(!lobby.initialize(device.Get(),context.Get(),assets,failure))return false;
        std::filesystem::create_directories(output);
        constexpr unsigned width=1200,height=1000;
        D3D11_TEXTURE2D_DESC desc{};desc.Width=width;desc.Height=height;desc.MipLevels=desc.ArraySize=1;
        desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;desc.SampleDesc.Count=1;desc.BindFlags=D3D11_BIND_RENDER_TARGET;
        ComPtr<ID3D11Texture2D> target;check(device->CreateTexture2D(&desc,nullptr,&target),"Rig test color");
        ComPtr<ID3D11RenderTargetView> rtv;check(device->CreateRenderTargetView(target.Get(),nullptr,&rtv),"Rig test RTV");
        desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;check(device->CreateTexture2D(&desc,nullptr,&staging),"Rig test readback");
        blvr_xr_bridge::PoseBridge frame{};frame.flags=blvr_xr_bridge::PoseBridgeHmdValid;
        frame.frameId=1;frame.producerEpoch=1;
        frame.hmdPose.orientation[3]=1;frame.hmdPose.position[1]=1.7f;
        const auto setGripRotation=[&](int hand,XMMATRIX rotation) {
            XMStoreFloat4(reinterpret_cast<XMFLOAT4*>(frame.controllers[hand].gripPose.orientation),XMQuaternionRotationMatrix(rotation));
        };
        // A real upright tube has grip -Z upward. Identity grip orientation
        // points the tube forward and was hiding the old 90-degree error.
        const XMMATRIX uprightGrip=XMMatrixRotationX(XM_PIDIV2);
        const XMMATRIX frettingGrip(XMVectorSet(0,.9682458f,.25f,0),
            XMVectorSet(0,-.25f,.9682458f,0),XMVectorSet(1,0,0,0),XMVectorSet(0,0,0,1));
        for(int hand=0;hand<2;++hand) {
            auto& c=frame.controllers[hand];c.activeFlags=blvr_xr_bridge::ControllerGripPose|blvr_xr_bridge::ControllerAimPose;
            setGripRotation(hand,uprightGrip);
            c.gripPose.position[0]=hand==0?-.28f:.28f;c.gripPose.position[1]=1.25f;c.gripPose.position[2]=-.38f;c.aimPose=c.gripPose;
        }
        auto& calibration=*lobby.impl_;
        const auto solvedPalm=[&](int hand) {
            const int wrist=calibration.bone(hand==0?"Lf_Wrist":"Rt_Wrist");
            return calibration.palm(hand)*XMLoadFloat4x4(&calibration.bones[wrist].inverse)
                *XMLoadFloat4x4(&calibration.posed[wrist])*XMLoadFloat4x4(&calibration.root);
        };
        for(int originTest=0;originTest<2;++originTest) {
            const float headY=originTest?0.f:1.7f;
            frame.hmdPose.position[1]=headY;
            for(auto& controller:frame.controllers)controller.gripPose.position[1]=headY-.45f;
            ++frame.recenterRequestId;
            for(unsigned weapon=0;weapon<=2;++weapon) {
                calibration.selected=weapon;calibration.action=-1;calibration.motionValid=false;
                setGripRotation(0,weapon==2?frettingGrip:uprightGrip);
                lobby.update(frame,1.f/90,true);
                if(std::fabs(headY-calibration.floorY-1.72f)>.0001f)
                    throw std::runtime_error("Opening floor is not below the LOCAL headset origin");
                XMFLOAT4X4 openingPalms[2];
                for(int hand=0;hand<2;++hand) {
                    const XMMATRIX actual=solvedPalm(hand),grip=matrix(frame.controllers[hand].gripPose);
                    if(length(actual.r[3]-grip.r[3])>.002f)throw std::runtime_error("Metric rig cannot reach neutral controller");
                    // Full hand pose has the correct outward palm normal and
                    // little-to-index direction, independently of equipment.
                    if(XMVectorGetX(XMVector3Dot(XMVector3Normalize(actual.r[1]),grip.r[0]))*(hand==0?-1.f:1.f)<.999f
                        ||XMVectorGetX(XMVector3Dot(XMVector3Normalize(actual.r[0]),-grip.r[2]))*(hand==0?1.f:-1.f)<.999f)
                        throw std::runtime_error("Anatomical palm does not follow OpenXR grip axes");
                    XMStoreFloat4x4(&openingPalms[hand],actual);
                }
                if(weapon) {
                    const int hand=weapon==1?1:0;
                    const XMMATRIX held=XMLoadFloat4x4(&calibration.weapons[hand]),grip=matrix(frame.controllers[hand].gripPose);
                    if(XMVectorGetX(XMVector3Dot(XMVector3Normalize(held.r[1]),-grip.r[2]))<.999f
                        ||XMVectorGetX(XMVector3Dot(XMVector3Normalize(held.r[2]),-grip.r[1]))<.999f)
                        throw std::runtime_error("Weapon handle/string face does not follow the physical grip");
                }
                lobby.update(frame,1.f/90,false);
                for(int hand=0;hand<2;++hand) {
                    XMFLOAT4X4 gameplay;XMStoreFloat4x4(&gameplay,solvedPalm(hand));
                    for(int k=0;k<16;++k)if(std::fabs(reinterpret_cast<float*>(&gameplay)[k]-reinterpret_cast<float*>(&openingPalms[hand])[k])>.0001f)
                        throw std::runtime_error("Entering gameplay changes hand size or calibration");
                }
            }
        }
        std::ofstream dimensions(output/"calibration.txt");
        dimensions<<"calibration="<<blvr_rig::GripCalibrationVersion<<" eye_height_m="<<blvr_rig::EyeHeightMeters
            <<" body_scale="<<calibration.scale<<" hand_length_m="<<blvr_rig::HandLengthMeters
            <<" hand_weapon_scale="<<calibration.handScale<<" local_origin_floor_and_mode_parity=pass\n";
        frame.hmdPose.position[1]=1.7f;++frame.recenterRequestId;
        for(auto& controller:frame.controllers)controller.gripPose.position[1]=1.25f;
        for(int take=0;take<23;++take) {
            lobby.impl_->selected=take==1?1u:take>=5?(take<14?1u:2u):0u;
            setGripRotation(0,lobby.impl_->selected==2?frettingGrip:uprightGrip);
            lobby.impl_->action=-1;
            if(take>=5) {
                lobby.impl_->action=take<14?1:2;
                lobby.impl_->actionStart=lobby.impl_->clock-(.10f+static_cast<float>((take-5)%9)*.18f);
                lobby.impl_->physicalPlayback=false;
            }
            frame.controllers[0].squeeze=frame.controllers[1].squeeze=(take==1||take>=3)?1.f:0.f;
            lobby.update(frame,1.f/90);
            XrView view{XR_TYPE_VIEW};view.pose.orientation.w=1;view.pose.position={0,1.7f,0};view.fov={-.82f,.82f,.73f,-.9f};
            if(take<2||take>=5) {
                XMFLOAT4 q;XMStoreFloat4(&q,XMQuaternionRotationAxis(XMVectorSet(1,0,0,0),-.43f));
                view.pose.orientation={q.x,q.y,q.z,q.w};
            } else if(take==2) {view.pose.position={0,1.45f,-2.3f};view.pose.orientation={0,1,0,0};}
            else {
                XMFLOAT4 q;
                if(take==3) {view.pose.position={.28f,1.8f,-.38f};XMStoreFloat4(&q,XMQuaternionRotationAxis(XMVectorSet(1,0,0,0),-XM_PIDIV2));}
                else {view.pose.position={1.0f,1.25f,-.38f};XMStoreFloat4(&q,XMQuaternionRotationAxis(XMVectorSet(0,1,0,0),XM_PIDIV2));}
                view.pose.orientation={q.x,q.y,q.z,q.w};
            }
            lobby.render(rtv.Get(),width,height,view,nullptr);context->CopyResource(staging.Get(),target.Get());
            D3D11_MAPPED_SUBRESOURCE mapped{};check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"Rig test map");
            const std::string name=take>=5?std::string(take<14?"axe-":"guitar-")+std::to_string((take-5)%9):std::to_string(take);
            std::ofstream ppm(output/(name+".ppm"),std::ios::binary);ppm<<"P6\n"<<width<<' '<<height<<"\n255\n";
            for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
                auto* pixel=static_cast<uint8_t*>(mapped.pData)+y*mapped.RowPitch+x*4;ppm.write(reinterpret_cast<char*>(pixel),3);
            }
            context->Unmap(staging.Get(),0);
        }
        // Exercise the real input/rig code against the imported geometry.
        auto& rig=*lobby.impl_;rig.action=-1;rig.selected=0;rig.motionValid=false;
        frame.controllers[0].buttons=blvr_xr_bridge::ControllerPrimaryClick;
        lobby.update(frame,1.f/90);
        frame.controllers[0].buttons=0;lobby.update(frame,1.f/90);
        if(lobby.selectedWeapon()!=1)throw std::runtime_error("Releasing X did not equip the axe");
        const float stillYaw=rig.yaw;
        frame.controllers[1].thumbstickX=1;
        for(int i=0;i<90;++i)lobby.update(frame,1.f/90);
        if(std::fabs(rig.yaw-stillYaw+.785398163f)>.001f)throw std::runtime_error("Held stick repeated a snap turn");
        frame.controllers[1].thumbstickX=0;
        for(int i=0;i<30;++i)lobby.update(frame,1.f/90);
        if(lobby.physicalAction())throw std::runtime_error("Stationary controllers generated an attack");
        frame.controllers[0].buttons=blvr_xr_bridge::ControllerSecondaryClick;
        lobby.update(frame,1.f/90);
        frame.controllers[0].buttons=0;lobby.update(frame,1.f/90);
        if(lobby.selectedWeapon()!=2)throw std::runtime_error("Releasing Y did not equip guitar");
        frame.controllers[1].trigger=1;lobby.update(frame,1.f/90);
        if(rig.action!=2)throw std::runtime_error("Equipped guitar did not use native strum");
        // Entering a vehicle opens the free hand and clears attack playback.
        // Gas/face buttons must not equip or strum an invisible instrument.
        frame.controllers[0].buttons=blvr_xr_bridge::ControllerSecondaryClick;
        lobby.update(frame,1.f/90,false,0,true);
        if(lobby.selectedWeapon()||rig.equipped[0]||rig.action>=0||lobby.physicalAction())
            throw std::runtime_error("Driving retained a weapon grip or attack");
        frame.controllers[0].buttons=0;
        lobby.update(frame,1.f/90,false,0,true);
        frame.controllers[1].trigger=0;
        lobby.update(frame,1.f/90,false,0,false);
        if(lobby.selectedWeapon()!=2||!rig.equipped[0]||lobby.physicalAction())
            throw std::runtime_error("Dismount did not restore the held instrument cleanly");
        frame.controllers[1].trigger=0;
        // Attach/release both actual weapon supports, including the axe's
        // optional second hand. Move each controller to the rendered socket.
        rig.yaw=0;rig.navigation={};
        for(unsigned selected=1;selected<=2;++selected) {
            rig.selected=selected;rig.action=-1;rig.motionValid=false;rig.supportHeld=false;
            setGripRotation(0,selected==2?frettingGrip:uprightGrip);
            frame.controllers[0].squeeze=frame.controllers[1].squeeze=0;
            for(int hand=0;hand<2;++hand) {
                frame.controllers[hand].gripPose.position[0]=hand==0?-.28f:.28f;
                frame.controllers[hand].gripPose.position[1]=1.25f;frame.controllers[hand].gripPose.position[2]=-.38f;
            }
            if(selected==1)frame.controllers[1].gripPose.position[0]=.05f;
            lobby.update(frame,1.f/90);
            const int primary=selected==1?1:0,secondary=1-primary;
            const XMVECTOR socket=XMVector3TransformCoord(selected==1?XMVectorSet(.068f,.58f,0,1):XMVectorSet(.035f,-.32f,.075f,1),XMLoadFloat4x4(&rig.weapons[primary]));
            XMStoreFloat3(reinterpret_cast<XMFLOAT3*>(frame.controllers[secondary].gripPose.position),socket);
            frame.controllers[secondary].squeeze=1;lobby.update(frame,1.f/90);
            if(!rig.supportHeld)throw std::runtime_error(selected==1?"Axe support did not attach":"Guitar support did not attach");
            XrView supported{XR_TYPE_VIEW};supported.pose.position={0,1.7f,0};supported.fov={-.82f,.82f,.73f,-.9f};
            XMFLOAT4 rotation;XMStoreFloat4(&rotation,XMQuaternionRotationAxis(XMVectorSet(1,0,0,0),-.43f));
            supported.pose.orientation={rotation.x,rotation.y,rotation.z,rotation.w};
            lobby.render(rtv.Get(),width,height,supported,nullptr);context->CopyResource(staging.Get(),target.Get());
            D3D11_MAPPED_SUBRESOURCE mapped{};check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"Support readback");
            std::ofstream shot(output/(selected==1?"axe-two-hand.ppm":"guitar-two-hand.ppm"),std::ios::binary);shot<<"P6\n"<<width<<' '<<height<<"\n255\n";
            for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x)shot.write(reinterpret_cast<char*>(mapped.pData)+y*mapped.RowPitch+x*4,3);
            context->Unmap(staging.Get(),0);
            const auto beforeUnreachablePose=rig.posed;
            const XMFLOAT4X4 beforeUnreachableWeapons[]{rig.weapons[0],rig.weapons[1]};
            const auto reachableSupport=rig.supportPoint;
            rig.supportPoint.y+=4;
            rig.twoHand(frame,XMLoadFloat4x4(&rig.root));
            if(rig.supportHeld||std::memcmp(rig.posed.data(),beforeUnreachablePose.data(),sizeof(rig.posed))||
                std::memcmp(rig.weapons,beforeUnreachableWeapons,sizeof(rig.weapons)))
                throw std::runtime_error("Unreachable support changed the primary weapon or left a constrained free hand");
            rig.supportPoint=reachableSupport;lobby.update(frame,1.f/90);
            if(!rig.supportHeld)throw std::runtime_error("Released unreachable support could not reattach at the real socket");
            frame.controllers[1].trigger=1;lobby.update(frame,1.f/90);lobby.update(frame,1.f/90);
            if(!rig.supportHeld)throw std::runtime_error("Trigger attack detached the support hand");
            frame.controllers[1].trigger=0;
            frame.controllers[secondary].squeeze=0;lobby.update(frame,1.f/90);
            if(rig.supportHeld)throw std::runtime_error("Support did not release");
        }
        rig.selected=1;rig.action=-1;rig.motionValid=false;rig.lastPhysicalAction=-10;
        for(int i=0;i<3;++i)lobby.update(frame,1.f/90);
        frame.controllers[1].gripPose.position[2]-=.06f;lobby.update(frame,1.f/90);
        if(lobby.physicalAction()!=1)throw std::runtime_error("Physical axe swing did not produce native attack pulse");
        rig.action=-1;rig.selected=2;rig.supportHeld=false;rig.motionValid=false;rig.lastPhysicalAction=-10;
        frame.controllers[0].gripPose.position[0]=-.28f;frame.controllers[0].gripPose.position[1]=1.25f;frame.controllers[0].gripPose.position[2]=-.38f;
        frame.controllers[0].squeeze=frame.controllers[1].squeeze=0;
        lobby.update(frame,1.f/90);
        const int fingertip=rig.bone("Rt_Index3"),previousFinger=rig.bone("Rt_Index2");
        const XMVECTOR referenceTip=rig.position(fingertip)+(rig.position(fingertip)-rig.position(previousFinger))*.72f;
        auto fingerAt=[&](float x,float z=.06f) {
            const XMVECTOR target=XMVector3TransformCoord(XMVectorSet(x,-.25f,z,1),XMLoadFloat4x4(&rig.weapons[0]));
            const XMVECTOR current=XMVector3TransformCoord(referenceTip,XMLoadFloat4x4(&rig.bones[fingertip].inverse)*XMLoadFloat4x4(&rig.posed[fingertip])*XMLoadFloat4x4(&rig.root));
            for(int k=0;k<3;++k)frame.controllers[1].gripPose.position[k]+=XMVectorGetByIndex(target-current,k);
        };
        fingerAt(-.045f);rig.motionValid=false;lobby.update(frame,1.f/90);
        fingerAt(-.045f);rig.motionValid=false;lobby.update(frame,1.f/90);
        fingerAt(.045f);lobby.update(frame,1.f/90);
        if(lobby.physicalAction()!=2)throw std::runtime_error("Crossing the guitar strings did not produce native strum pulse");
        // A reversed picking stroke works as well, without any button held.
        rig.action=-1;rig.actionPulseUntil=0;rig.lastPhysicalAction=-10;
        fingerAt(-.045f);lobby.update(frame,1.f/90);
        if(lobby.physicalAction()!=2)throw std::runtime_error("Right-hand upstroke did not produce a guitar pulse");
        // Move only the guitar through the stationary picking hand. This is
        // a deliberate false-positive fixture for left-hand-only movement.
        rig.action=-1;rig.actionPulseUntil=0;rig.lastPhysicalAction=-10;
        const XMVECTOR across=XMLoadFloat4x4(&rig.weapons[0]).r[0]*-.10f;
        for(int k=0;k<3;++k)frame.controllers[0].gripPose.position[k]+=XMVectorGetByIndex(across,k);
        lobby.update(frame,1.f/90);
        if(lobby.physicalAction())throw std::runtime_error("Moving only the fretting hand played a guitar note");
        // A comfortable near-string stroke works while gripping either Touch
        // controller; solo picking must never latch the right palm to the neck.
        rig.action=-1;rig.actionPulseUntil=0;rig.lastPhysicalAction=-10;
        frame.controllers[0].squeeze=frame.controllers[1].squeeze=1;
        lobby.update(frame,1.f/90,false,2);
        for(int i=0;i<3;++i) {fingerAt(-.05f,.23f);rig.motionValid=false;lobby.update(frame,1.f/90,false,2);}
        fingerAt(.05f,.23f);lobby.update(frame,1.f/90,false,2);
        if(rig.supportHeld||lobby.physicalAction()!=2)throw std::runtime_error("Near-string solo stroke with both grips did not play");
        rig.action=-1;rig.selected=0;rig.yaw=0;rig.navigation={};
        frame.controllers[0].squeeze=frame.controllers[1].squeeze=0;
        frame.controllers[1].aimPose.orientation[0]=frame.controllers[1].aimPose.orientation[1]=frame.controllers[1].aimPose.orientation[2]=0;
        frame.controllers[1].aimPose.orientation[3]=1;
        const float confirmY=rig.floorY+1.04f;
        frame.controllers[1].aimPose.position[0]=0;frame.controllers[1].aimPose.position[1]=confirmY;frame.controllers[1].aimPose.position[2]=0;
        lobby.update(frame,1.f/90);frame.controllers[1].trigger=1;lobby.update(frame,1.f/90);
        if(!lobby.confirming())throw std::runtime_error("Aimed confirm failed on the rendered button");
        frame.controllers[1].trigger=0;rig.pressUntil=0;rig.contactLatched=false;
        rig.navigation.z=-2.65f;lobby.update(frame,1.f/90);
        auto touchAt=[&](float z) {
            const XMVECTOR target=XMVectorSet(0,confirmY,-2.94f+z,1);
            const XMVECTOR current=XMVector3TransformCoord(point(rig.posed[fingertip]),XMLoadFloat4x4(&rig.root));
            for(int k=0;k<3;++k)frame.controllers[1].gripPose.position[k]+=XMVectorGetByIndex(target-current,k);
        };
        for(int i=0;i<3;++i){touchAt(.13f);lobby.update(frame,1.f/90);}
        touchAt(-.025f);lobby.update(frame,1.f/90);
        if(!lobby.confirming())throw std::runtime_error("Physical index-finger confirm failed");
        // Button input must request live native animation without inventing host motion.
        EddieLobby buttonLobby;
        if(!buttonLobby.initialize(device.Get(),context.Get(),assets,failure))return false;
        auto& buttons=*buttonLobby.impl_;
        frame.controllers[0].buttons=frame.controllers[1].buttons=0;
        frame.controllers[0].trigger=frame.controllers[1].trigger=0;
        frame.controllers[0].squeeze=frame.controllers[1].squeeze=0;
        for(int hand=0;hand<2;++hand) {
            frame.controllers[hand].gripPose.position[0]=hand==0?-.28f:.28f;
            frame.controllers[hand].gripPose.position[1]=frame.hmdPose.position[1]-.45f;
            frame.controllers[hand].gripPose.position[2]=-.38f;
        }
        buttons.selected=0;buttons.motionValid=false;
        frame.controllers[1].trigger=0;buttonLobby.update(frame,1.f/90,false);
        blvr_xr_bridge::RigFrame emptyHandRequest{};
        if(!buttonLobby.exportRig(emptyHandRequest)||emptyHandRequest.selectedWeapon||emptyHandRequest.trackedHandMask!=3||
            emptyHandRequest.controlsSignature!=BLVR::ControlsSignature(BLVR::ActiveBindings()))
            throw std::runtime_error("Empty hands did not publish the current control layout and tracking state");
        for(unsigned weapon=1;weapon<=2;++weapon) {
            buttons.selected=weapon;buttons.action=-1;buttons.actionDown[1]=false;buttons.motionValid=false;
            setGripRotation(0,weapon==2?frettingGrip:uprightGrip);
            frame.controllers[1].trigger=0;
            buttonLobby.update(frame,1.f/90,false);
            const auto neutralWrist=buttons.posed[buttons.bone("Rt_Wrist")];
            const auto stableRoot=buttons.root;
            frame.controllers[1].trigger=1;
            buttonLobby.update(frame,1.f/90,false);
            for(int i=0;i<18;++i)buttonLobby.update(frame,1.f/90,false);
            const auto attackingWrist=buttons.posed[buttons.bone("Rt_Wrist")];
            blvr_xr_bridge::RigFrame request{};
            if(!buttonLobby.exportRig(request)||request.liveAction!=weapon||request.liveActionWeight<.5f)
                throw std::runtime_error("Button attack did not request current-frame native retargeting");
            if(length(XMLoadFloat4x4(&neutralWrist).r[3]-XMLoadFloat4x4(&attackingWrist).r[3])>.002f)
                throw std::runtime_error("Host substituted recorded movement for the game's live pose");
            if(std::memcmp(&stableRoot,&buttons.root,sizeof(stableRoot))||buttons.physicalPlayback||buttons.selected!=weapon)
                throw std::runtime_error("Button attack moved the body anchor or changed selection");
            const float priorStart=buttons.actionStart;
            frame.controllers[1].trigger=0;buttonLobby.update(frame,1.f/90,false);
            frame.controllers[1].trigger=1;buttonLobby.update(frame,1.f/90,false);
            if(buttons.actionStart<=priorStart)
                throw std::runtime_error("A repeated button attack during recovery had no visible response");
            frame.controllers[1].trigger=0;
            for(int i=0;i<125;++i)buttonLobby.update(frame,1.f/90,false);
            if(buttons.action>=0||length(XMLoadFloat4x4(&neutralWrist).r[3]-XMLoadFloat4x4(&buttons.posed[buttons.bone("Rt_Wrist")]).r[3])>.002f)
                throw std::runtime_error("Button attack did not return to the tracked hand");
            frame.controllers[0].squeeze=frame.controllers[1].squeeze=1;
            buttonLobby.update(frame,1.f/90,false);
            frame.controllers[0].squeeze=frame.controllers[1].squeeze=0;
            buttonLobby.update(frame,1.f/90,false);
            if(buttonLobby.selectedWeapon()!=weapon)
                throw std::runtime_error("Earthshaker grip chord changed weapon selection");
            const auto validRight=frame.controllers[1];
            frame.controllers[1].activeFlags=0;frame.controllers[1].gripPose.position[0]=NAN;
            buttonLobby.update(frame,1.f/90,false);
            if(!buttonLobby.exportRig(request)||request.trackedHandMask!=1||request.selectedWeapon!=weapon||request.liveAction||
               request.controlsSignature!=BLVR::ControlsSignature(BLVR::ActiveBindings()))
                throw std::runtime_error("Lost grip published a tracked bind pose, lost equipment, or retained an action");
            frame.controllers[1]=validRight;buttonLobby.update(frame,1.f/90,false);
            if(!buttonLobby.exportRig(request)||request.trackedHandMask!=3||buttonLobby.physicalAction())
                throw std::runtime_error("Grip reacquisition manufactured a physical attack or retained invalid tracking");
            std::fill_n(frame.controllers[1].gripPose.orientation,4,0.f);
            buttonLobby.update(frame,1.f/90,false);
            if(!buttonLobby.exportRig(request)||request.trackedHandMask!=1)
                throw std::runtime_error("A flagged but singular controller pose was accepted as tracked");
            frame.controllers[1]=validRight;
        }
        const auto validHead=frame.hmdPose;
        frame.hmdPose.position[0]=NAN;buttonLobby.update(frame,1.f/90,false);
        UiMounts invalidHeadMounts{};blvr_xr_bridge::RigFrame invalidHeadRequest{};
        if(buttonLobby.exportRig(invalidHeadRequest)||buttonLobby.exportUiMounts(invalidHeadMounts)||buttonLobby.physicalAction()||buttonLobby.confirming())
            throw std::runtime_error("Invalid source HMD republished an older rig or retained interaction pulses");
        frame.hmdPose=validHead;buttonLobby.update(frame,1.f/90,false);
        // Feed custom layouts directly to the actual host reader. Production
        // still receives the live layout through EddieLobby::update; fixtures
        // need no controls.ini writes or reload timing assumptions.
        auto scoped=BLVR::DefaultBindings;
        const auto resetScoped=[&](unsigned weapon) {
            buttons.selected=weapon;buttons.action=-1;buttons.playing=false;buttons.actionPulseUntil=0;
            buttons.actionDown[1]=false;buttons.motionValid=false;buttons.supportHeld=false;
            buttons.lastSelectionButtons=buttons.pendingSelection=buttons.lastNoteButtons=0;
            buttons.lastNativeSolo=0;buttons.earthshakerDown=false;buttons.physicalPlayback=false;
            for(auto& controller:frame.controllers) {
                controller.buttons=0;controller.trigger=controller.squeeze=0;
            }
        };
        for(const auto action:{BLVR::UiStart,BLVR::Journal}) {
            scoped=BLVR::DefaultBindings;scoped.actions[action]={BLVR::TI::X,true};resetScoped(0);
            frame.controllers[0].buttons=blvr_xr_bridge::ControllerPrimaryClick;
            buttons.update(frame,1.f/90,false,0,false,scoped);
            if(BLVR::MapTouch(BLVR::TouchFromPose(frame),scoped).buttons[action])
                throw std::runtime_error("Ordinary equipment press emitted a command-bound pause action");
            frame.controllers[0].buttons=0;buttons.update(frame,1.f/90,false,0,false,scoped);
            blvr_xr_bridge::RigFrame scopedRequest{};
            if(!buttonLobby.exportRig(scopedRequest)||scopedRequest.selectedWeapon!=1||
                scopedRequest.controlsSignature!=BLVR::ControlsSignature(scoped))
                throw std::runtime_error("Command-bound pause swallowed ordinary axe equip or published the wrong layout");
            resetScoped(0);frame.controllers[0].squeeze=1;
            frame.controllers[0].buttons=blvr_xr_bridge::ControllerPrimaryClick|blvr_xr_bridge::ControllerThumbstickPressed;
            buttons.update(frame,1.f/90,false,0,false,scoped);
            if(buttons.selected||buttons.action>=0||!BLVR::MapTouch(BLVR::TouchFromPose(frame),scoped).buttons[action])
                throw std::runtime_error("Command-bound host pause disagreed with native scope");
        }
        for(unsigned weapon=1;weapon<=2;++weapon) {
            scoped=BLVR::DefaultBindings;const auto action=weapon==1?BLVR::Axe:BLVR::Guitar;
            scoped.actions[action].command=true;resetScoped(weapon);frame.controllers[1].trigger=1;
            buttons.update(frame,1.f/90,false,0,false,scoped);
            if(buttons.action>=0||BLVR::MapTouch(BLVR::TouchFromPose(frame),scoped).buttons[action])
                throw std::runtime_error("Host button attack leaked without its remapped command modifier");
            scoped.actions[action]={BLVR::TI::LeftTrigger};resetScoped(weapon);frame.controllers[0].trigger=1;
            buttons.update(frame,1.f/90,false,0,false,scoped);
            if(buttons.action!=static_cast<int>(weapon))
                throw std::runtime_error("Host button attack ignored its ordinary remapped trigger");
            scoped.actions[action].input=BLVR::TI::None;resetScoped(weapon);frame.controllers[1].trigger=1;
            buttons.update(frame,1.f/90,false,0,false,scoped);
            if(buttons.action>=0)throw std::runtime_error("Unbound host button attack retained a hidden trigger");
        }
        for(unsigned n=0;n<3;++n) {
            scoped=BLVR::DefaultBindings;const auto action=static_cast<BLVR::NativeAction>(BLVR::SoloNote1+n);
            scoped.actions[action]={BLVR::TI::RightTrigger,true};resetScoped(2);frame.controllers[1].trigger=1;
            buttons.update(frame,1.f/90,false,2,false,scoped);
            auto nativeTouch=BLVR::TouchFromPose(frame);nativeTouch.soloNotes=true;
            if(buttons.action>=0||BLVR::MapTouch(nativeTouch,scoped).buttons[action])
                throw std::runtime_error("Timed-note animation leaked without its required command modifier");
            scoped.actions[action].command=false;resetScoped(2);frame.controllers[1].trigger=1;
            buttons.update(frame,1.f/90,false,2,false,scoped);
            if(buttons.action!=2)throw std::runtime_error("Timed-note animation ignored a remapped native note button");
            resetScoped(2);frame.controllers[1].trigger=1;frame.controllers[0].squeeze=1;
            frame.controllers[0].buttons=blvr_xr_bridge::ControllerThumbstickPressed;
            buttons.update(frame,1.f/90,false,2,false,scoped);
            if(buttons.action>=0)throw std::runtime_error("Command mode manufactured a timed-note animation");
        }
        scoped=BLVR::DefaultBindings;scoped.actions[BLVR::UiStart].input=scoped.actions[BLVR::Journal].input=BLVR::TI::None;
        resetScoped(1);frame.controllers[0].buttons=blvr_xr_bridge::ControllerMenuClick;frame.controllers[1].trigger=1;
        buttons.update(frame,1.f/90,false,0,false,scoped);
        if(buttons.action!=1)throw std::runtime_error("Unbound host pause retained a hidden menu button");
        scoped=BLVR::DefaultBindings;resetScoped(1);frame.controllers[1].trigger=1;
        buttons.update(frame,1.f/90,false,blvr_ui_bridge::BuildRadial,false,scoped);
        blvr_xr_bridge::RigFrame buildCarrier{};
        if(!buttonLobby.exportRig(buildCarrier)||buildCarrier.selectedWeapon!=2||buildCarrier.liveAction||
            buttonLobby.physicalAction()||buttons.lastNoteButtons)
            throw std::runtime_error("Build wheel lost its guitar carrier or manufactured a combat/solo note");
        // Exact rendered-geometry UI joins, including on-foot versus mounted
        // identity and a changing rendered hand with unchanged host tracking.
        HANDLE uiMapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,
            sizeof(blvr_xr_bridge::NativeHandHistoryBuffer),blvr_xr_bridge::NativeHandMappingName);
        if(!uiMapping||GetLastError()==ERROR_ALREADY_EXISTS) {
            if(uiMapping)CloseHandle(uiMapping);
            throw std::runtime_error("Rendered UI fixture requires an isolated stopped game");
        }
        auto* uiHistory=static_cast<blvr_xr_bridge::NativeHandHistoryBuffer*>(MapViewOfFile(uiMapping,FILE_MAP_WRITE,0,0,0));
        if(!uiHistory){CloseHandle(uiMapping);throw std::runtime_error("Rendered UI fixture map failed");}
        auto& uiFrame=uiHistory->slots[65%blvr_xr_bridge::NativeHandHistory];uiFrame={};
        uiFrame.sequence=2;uiFrame.magic=blvr_xr_bridge::NativeHandMagic;
        uiFrame.producerEpoch=7;uiFrame.sourceFrameId=65;uiFrame.poseFrameId=8;
        uiFrame.predictedDisplayTime=900;uiFrame.validMask=3;uiFrame.flags=blvr_xr_bridge::NativeUiGuitar;
        uiFrame.version=blvr_xr_bridge::NativeHandVersion;uiFrame.pointerValidMask=3;
        for(int hand=0;hand<2;++hand) {
            uiFrame.palmToHead[hand][0]=uiFrame.palmToHead[hand][5]=uiFrame.palmToHead[hand][10]=uiFrame.palmToHead[hand][15]=1;
            uiFrame.palmToHead[hand][12]=hand==0?-.3f:.3f;uiFrame.palmToHead[hand][13]=-.4f;
        }
        uiFrame.guitarToHead[0]=uiFrame.guitarToHead[5]=uiFrame.guitarToHead[10]=uiFrame.guitarToHead[15]=1;
        blvr_xr_bridge::Pose uiHead{};uiHead.orientation[3]=1;UiMounts joined{};bool mounted=false;
        if(!lobby.readRenderedUi(7,65,8,900,uiHead,joined,mounted)||mounted||!(joined.validMask&(1u<<GuitarHeadstock)))
            throw std::runtime_error("Rendered guitar UI fixture failed or confused on-foot with driving");
        const float beforeUi=joined.poses[RightForearm].position.x;
        uiFrame.palmToHead[1][12]+=.2f;uiFrame.sequence=4;
        if(!lobby.readRenderedUi(7,65,8,900,uiHead,joined,mounted)||std::fabs(joined.poses[RightForearm].position.x-beforeUi-.2f)>.0001f)
            throw std::runtime_error("Wrist UI did not follow the actual rendered arm");
        if(lobby.readRenderedUi(7,66,8,900,uiHead,joined,mounted)||lobby.readRenderedUi(7,65,9,900,uiHead,joined,mounted)||
           lobby.readRenderedUi(6,65,8,900,uiHead,joined,mounted)||lobby.readRenderedUi(7,65,8,901,uiHead,joined,mounted))
            throw std::runtime_error("Rendered UI accepted a mismatched source generation");
        uiFrame.pointerValidMask=1;uiFrame.sequence+=2;
        if(!lobby.readRenderedUi(7,65,8,900,uiHead,joined,mounted)||joined.tipMask!=1||
            (joined.validMask&((1u<<LeftForearm)|(1u<<RightForearm)))!=((1u<<LeftForearm)|(1u<<RightForearm)))
            throw std::runtime_error("Native fallback geometry lost UI attachment or enabled an untracked pointer");
        const UiMounts beforeMalformed=joined;
        const auto rejectMalformed=[&] {
            uiFrame.sequence+=2;
            if(lobby.readRenderedUi(7,65,8,900,uiHead,joined,mounted)||std::memcmp(&joined,&beforeMalformed,sizeof(joined)))
                throw std::runtime_error("Malformed rendered UI was accepted or partially changed output");
        };
        uiFrame.version=2;rejectMalformed();uiFrame.version=blvr_xr_bridge::NativeHandVersion;
        uiFrame.pointerValidMask=4;rejectMalformed();uiFrame.pointerValidMask=1;
        uiFrame.palmToHead[1][0]=0;rejectMalformed();uiFrame.palmToHead[1][0]=1;
        uiFrame.palmToHead[1][3]=.2f;rejectMalformed();uiFrame.palmToHead[1][3]=0;
        uiFrame.guitarToHead[10]=0;rejectMalformed();uiFrame.guitarToHead[10]=1;
        uiFrame.indexTipToHead[0][0]=NAN;rejectMalformed();uiFrame.indexTipToHead[0][0]=0;
        uiFrame.flags=blvr_xr_bridge::NativeUiDriving;uiFrame.sequence+=2;
        if(!lobby.readRenderedUi(7,65,8,900,uiHead,joined,mounted)||!mounted||(joined.validMask&(1u<<GuitarHeadstock)))
            throw std::runtime_error("Mounted rendered UI fixture failed");
        uiFrame.validMask=1;uiFrame.sequence+=2;
        if(lobby.readRenderedUi(7,65,8,900,uiHead,joined,mounted))throw std::runtime_error("Rendered UI accepted a missing hand");
        UnmapViewOfFile(uiHistory);CloseHandle(uiMapping);
        failure.clear();return true;
    }catch(const std::exception& e){failure=e.what();return false;}
}
}
