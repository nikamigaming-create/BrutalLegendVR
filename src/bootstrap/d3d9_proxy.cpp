#include <windows.h>
#include "../diagnostics/frame_profiler.h"
#include "../bridge/render_quality.h"
#include <d3d9.h>
#include "../diagnostics/log.h"
#include "../diagnostics/telemetry.h"
#include "../diagnostics/shader_dump.h"
#include "../openxr/xr_host.h"
#include "../camera/camera_hook.h"
#include "../input/retail_input_bridge.h"
#include "video_capture.h"
#include "MinHook.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <intrin.h>
#include <utility>
#include <vector>

// Forward declarations of original pointers
typedef IDirect3D9* (WINAPI* FnDirect3DCreate9)(UINT SDKVersion);
static FnDirect3DCreate9 g_Real_Direct3DCreate9 = nullptr;
static HMODULE g_RealD3D9Module = nullptr;

typedef HRESULT (WINAPI* FnCreateDevice)(
    IDirect3D9* thisPtr,
    UINT Adapter,
    D3DDEVTYPE DeviceType,
    HWND hFocusWindow,
    DWORD BehaviorFlags,
    D3DPRESENT_PARAMETERS* pPresentationParameters,
    IDirect3DDevice9** ppReturnedDeviceInterface
);
static FnCreateDevice g_Original_CreateDevice = nullptr;
using CreatePixelShader = HRESULT(WINAPI*)(IDirect3DDevice9*,const DWORD*,IDirect3DPixelShader9**);
using CreateVertexShader = HRESULT(WINAPI*)(IDirect3DDevice9*,const DWORD*,IDirect3DVertexShader9**);
static CreatePixelShader g_OriginalCreatePixelShader=nullptr;
static CreateVertexShader g_OriginalCreateVertexShader=nullptr;
using CreateQuery = HRESULT(WINAPI*)(IDirect3DDevice9*,D3DQUERYTYPE,IDirect3DQuery9**);
static CreateQuery g_OriginalCreateQuery=nullptr;
static volatile LONG g_QueryObservationCount=0;
static HRESULT WINAPI Hook_CreateQuery(IDirect3DDevice9* device,D3DQUERYTYPE type,IDirect3DQuery9** query) {
    // Observe the retail query path without changing visibility decisions or GPU results.
    const HRESULT result=g_OriginalCreateQuery(device,type,query);
    const LONG index=InterlockedIncrement(&g_QueryObservationCount);
    if(index<=64) {
        char fields[192]{};
        std::snprintf(fields,sizeof(fields),"\"index\":%ld,\"type\":%u,\"occlusion\":%s,\"created\":%s,\"hr\":%ld",
            index,static_cast<unsigned>(type),type==D3DQUERYTYPE_OCCLUSION?"true":"false",
            SUCCEEDED(result)&&query&&*query?"true":"false",static_cast<long>(result));
        BLVR::Telemetry_Write("gpu_query_create",fields);
    }
    return result;
}
static HRESULT WINAPI Hook_CreatePixelShader(IDirect3DDevice9* device,const DWORD* code,IDirect3DPixelShader9** shader) {
    const HRESULT result=g_OriginalCreatePixelShader(device,code,shader);
    if(SUCCEEDED(result)&&shader)BLVR::DumpShader(*shader,"ps");
    return result;
}
static HRESULT WINAPI Hook_CreateVertexShader(IDirect3DDevice9* device,const DWORD* code,IDirect3DVertexShader9** shader) {
    const HRESULT result=g_OriginalCreateVertexShader(device,code,shader);
    if(SUCCEEDED(result)&&shader)BLVR::DumpShader(*shader,"vs");
    return result;
}

typedef HRESULT (WINAPI* FnPresent)(
    IDirect3DDevice9* thisPtr,
    const RECT* pSourceRect,
    const RECT* pDestRect,
    HWND hDestWindowOverride,
    const RGNDATA* pDirtyRegion
);
static FnPresent g_Original_Present = nullptr;

typedef HRESULT (WINAPI* FnReset)(
    IDirect3DDevice9* thisPtr,
    D3DPRESENT_PARAMETERS* pPresentationParameters
);
static FnReset g_Original_Reset = nullptr;

typedef HRESULT (WINAPI* FnSetTransform)(
    IDirect3DDevice9* thisPtr,
    D3DTRANSFORMSTATETYPE state,
    const D3DMATRIX* matrix
);
static FnSetTransform g_Original_SetTransform = nullptr;
static uint32_t g_SetTransformViewCount = 0;
static uint32_t g_SetTransformOverrideCount = 0;

typedef HRESULT (WINAPI* FnSetVertexShaderConstantF)(
    IDirect3DDevice9* thisPtr,
    UINT startRegister,
    const float* data,
    UINT vector4fCount
);
static FnSetVertexShaderConstantF g_Original_SetVertexShaderConstantF = nullptr;
typedef HRESULT (WINAPI* FnSetRenderState)(
    IDirect3DDevice9* thisPtr,
    D3DRENDERSTATETYPE state,
    DWORD value
);
static FnSetRenderState g_Original_SetRenderState = nullptr;
typedef HRESULT (WINAPI* FnDrawIndexedPrimitive)(
    IDirect3DDevice9* thisPtr,
    D3DPRIMITIVETYPE primitiveType,
    INT baseVertexIndex,
    UINT minVertexIndex,
    UINT numVertices,
    UINT startIndex,
    UINT primCount
);
static FnDrawIndexedPrimitive g_Original_DrawIndexedPrimitive = nullptr;
static volatile LONG g_D3D9DrawCallCount = 0;
static IDirect3DDevice9* g_LastD3D9Device = nullptr;
static uint32_t g_VertexMatrixOverrideCount = 0;
static bool g_ArmRigConfigured = false;
static bool g_ArmRigEnabled = false;
static uint32_t g_ArmRigOverrideCount = 0;
static bool g_ArmBodyYOffsetConfigured = false;
static float g_ArmBodyYOffset = 0.0f;
static bool g_ArmBodyXOffsetConfigured = false;
static float g_ArmBodyXOffset = 0.0f;
static bool g_ArmHeadXOffsetConfigured = false;
static float g_ArmHeadXOffset = 0.0f;
static bool g_ArmWorldAttachConfigured = false;
static bool g_ArmWorldAttachEnabled = false;
static bool g_BodyWorldOffsetConfigured = false;
static float g_BodyWorldOffset[3] = {};
static bool g_ForceTwoSidedConfigured = false;
static bool g_ForceTwoSidedEnabled = false;
static bool g_BodyObjectAnchorConfigured = false;
static bool g_BodyObjectAnchorEnabled = false;
static bool g_BodyObjectAnchorLocked = false;
static bool g_BodyObjectAnchorPending = false;
static float g_BodyObjectAnchorHead[3] = {};
static uint32_t g_ArmPaletteSequence = 0;
static uint32_t g_ArmPaletteDrawLogCount = 0;
static uint32_t g_ArmPaletteDrawsSinceUpload = 0;
static bool g_ArmPaletteReadyForDraw = false;
static float g_LastObjectWorld[16] = {};
static bool g_HasLastObjectWorld = false;
static uint32_t g_PaletteUploadCount = 0;
static uint32_t g_PaletteOwnerLogCount = 0;
static uintptr_t g_PaletteOwnerCallers[32] = {};
static uint32_t g_PaletteCandidateLogCount = 0;
static bool g_ConstantTraceConfigured = false;
static bool g_ConstantTraceEnabled = false;
static uint32_t g_ConstantTraceCount = 0;
static bool g_RenderStateTraceConfigured = false;
static bool g_RenderStateTraceEnabled = false;
static uint32_t g_RenderStateTraceCount = 0;
static bool g_ArmDrawTraceConfigured = false;
static bool g_ArmDrawTraceEnabled = false;
static uint32_t g_ArmDrawTraceCount = 0;
static bool g_ArmBoneTraceConfigured = false;
static bool g_ArmBoneTraceEnabled = false;
static bool g_ArmBoneTraceLogged = false;
static bool g_BodyDrawSkipConfigured = false;
static int g_BodyDrawSkipStart = -1;
static int g_BodyDrawSkipEnd = -1;
static bool g_BodyDrawSkipSweepEnabled = false;
static int g_BodyDrawSkipSweepPhase = -1;
static bool g_NativeHeadHideConfigured = false;
static bool g_NativeHeadHideEnabled = false;
static uint32_t g_NativeHeadHideCount = 0;
static bool g_HeadOnlyArmsConfigured = false;
static bool g_HeadOnlyArmsEnabled = false;
static uint32_t g_HeadOnlyArmsMask = 0x3Fu;
static uint32_t g_HeadOnlyArmsApplyCount = 0;
static bool g_ForceOpaqueConfigured = false;
static bool g_ForceOpaqueEnabled = false;
static uint32_t g_ForceOpaqueApplyCount = 0;
static int g_WorldMatrixOverrideMode = 0; // 0 = disabled, >0 = one register, -1 = legacy broad opt-in
static bool g_WorldMatrixOverrideConfigured = false;

// The retail character is a third-person skinned actor.  Its arm palette is
// not a stable first-person presentation surface, so the VR presentation uses
// a small opaque D3D9 overlay driven by the same tracked controller poses.
// This is deliberately drawn after the retail world and before capture/XR
// publication.  It keeps the hands and equipment visible without changing
// the game's collision, animation, or combat state.
static bool g_FirstPersonOverlayConfigured = false;
static bool g_FirstPersonOverlayEnabled = false;
static uint32_t g_FirstPersonOverlayDrawCount = 0;

struct OverlayVertex {
    float x;
    float y;
    float z;
    float rhw;
    DWORD color;
};

static constexpr DWORD OverlayColor(BYTE r, BYTE g, BYTE b, BYTE a = 255) {
    return (static_cast<DWORD>(a) << 24) |
           (static_cast<DWORD>(r) << 16) |
           (static_cast<DWORD>(g) << 8) |
           static_cast<DWORD>(b);
}

static void OverlayAddQuad(
    std::vector<OverlayVertex>& vertices,
    float x0, float y0, float x1, float y1,
    DWORD color) {
    vertices.push_back({x0, y0, 0.0f, 1.0f, color});
    vertices.push_back({x1, y0, 0.0f, 1.0f, color});
    vertices.push_back({x1, y1, 0.0f, 1.0f, color});
    vertices.push_back({x0, y0, 0.0f, 1.0f, color});
    vertices.push_back({x1, y1, 0.0f, 1.0f, color});
    vertices.push_back({x0, y1, 0.0f, 1.0f, color});
}

static void OverlayAddSegment(
    std::vector<OverlayVertex>& vertices,
    float x0, float y0, float x1, float y1,
    float thickness, DWORD color) {
    const float dx = x1 - x0;
    const float dy = y1 - y0;
    const float length = sqrtf(dx * dx + dy * dy);
    if (length < 0.5f) return;
    const float nx = -dy / length * thickness * 0.5f;
    const float ny = dx / length * thickness * 0.5f;
    vertices.push_back({x0 + nx, y0 + ny, 0.0f, 1.0f, color});
    vertices.push_back({x1 + nx, y1 + ny, 0.0f, 1.0f, color});
    vertices.push_back({x1 - nx, y1 - ny, 0.0f, 1.0f, color});
    vertices.push_back({x0 + nx, y0 + ny, 0.0f, 1.0f, color});
    vertices.push_back({x1 - nx, y1 - ny, 0.0f, 1.0f, color});
    vertices.push_back({x0 - nx, y0 - ny, 0.0f, 1.0f, color});
}

static void OverlayAddDisc(
    std::vector<OverlayVertex>& vertices,
    float cx, float cy, float radius, DWORD color) {
    constexpr int kSegments = 12;
    for (int i = 0; i < kSegments; ++i) {
        const float a0 = static_cast<float>(i) * 6.28318530718f / kSegments;
        const float a1 = static_cast<float>(i + 1) * 6.28318530718f / kSegments;
        vertices.push_back({cx, cy, 0.0f, 1.0f, color});
        vertices.push_back({cx + cosf(a0) * radius, cy + sinf(a0) * radius,
                            0.0f, 1.0f, color});
        vertices.push_back({cx + cosf(a1) * radius, cy + sinf(a1) * radius,
                            0.0f, 1.0f, color});
    }
}

static void OverlayAddTriangle(
    std::vector<OverlayVertex>& vertices,
    float x0, float y0, float x1, float y1, float x2, float y2,
    DWORD color) {
    vertices.push_back({x0, y0, 0.0f, 1.0f, color});
    vertices.push_back({x1, y1, 0.0f, 1.0f, color});
    vertices.push_back({x2, y2, 0.0f, 1.0f, color});
}

static void DrawFirstPersonOverlay(IDirect3DDevice9* device) {
    if (!device || !BLVR::CameraHook_IsActive()) return;
    if (!g_FirstPersonOverlayConfigured) {
        char overlayEnv[16] = {};
        GetEnvironmentVariableA("BLVR_FIRST_PERSON_OVERLAY", overlayEnv, sizeof(overlayEnv));
        g_FirstPersonOverlayEnabled = overlayEnv[0] == '1' ||
            _stricmp(overlayEnv, "true") == 0;
        g_FirstPersonOverlayConfigured = true;
        BLVR::Log("BLVR first-person overlay=%d", g_FirstPersonOverlayEnabled ? 1 : 0);
    }
    if (!g_FirstPersonOverlayEnabled) return;

    D3DVIEWPORT9 viewport{};
    if (FAILED(device->GetViewport(&viewport)) || viewport.Width == 0 || viewport.Height == 0) {
        return;
    }

    BLVR::ControllerState controllers{};
    BLVR::Pose6DoF head{};
    const bool haveControllers = BLVR::XrHost::Get().GetControllerState(controllers) &&
        controllers.runtimeActive && controllers.left.tracked && controllers.right.tracked;
    const bool haveHead = BLVR::XrHost::Get().GetHeadPose(head);
    if (!haveControllers) return;

    // Project local controller poses into a comfortable near-field overlay.
    // The world remains the retail render; these points are body-relative and
    // therefore stay attached to the HMD even while the player looks around.
    const float cx = static_cast<float>(viewport.X) + viewport.Width * 0.5f;
    const float shoulderY = static_cast<float>(viewport.Y) + viewport.Height * 0.54f;
    const float focal = static_cast<float>(std::min(viewport.Width, viewport.Height)) * 0.42f;
    auto project = [&](const BLVR::Pose6DoF& pose, float xBias, float yBias) {
        const float relX = pose.pos[0] - (haveHead ? head.pos[0] : 0.0f) + xBias;
        const float relY = pose.pos[1] - (haveHead ? head.pos[1] : 0.0f) + yBias;
        const float depth = std::max(0.28f, -pose.pos[2] + 0.18f);
        const float scale = focal / depth;
        return std::pair<float, float>{cx + relX * scale,
                                       shoulderY - relY * scale};
    };

    const auto leftWrist = project(controllers.left.pose, 0.0f, 0.0f);
    const auto rightWrist = project(controllers.right.pose, 0.0f, 0.0f);
    const auto leftShoulder = std::pair<float, float>{cx - viewport.Width * 0.105f,
                                                       shoulderY + viewport.Height * 0.045f};
    const auto rightShoulder = std::pair<float, float>{cx + viewport.Width * 0.105f,
                                                        shoulderY + viewport.Height * 0.045f};
    const auto leftElbow = std::pair<float, float>{
        (leftShoulder.first + leftWrist.first) * 0.5f - viewport.Width * 0.025f,
        (leftShoulder.second + leftWrist.second) * 0.5f + viewport.Height * 0.025f};
    const auto rightElbow = std::pair<float, float>{
        (rightShoulder.first + rightWrist.first) * 0.5f + viewport.Width * 0.025f,
        (rightShoulder.second + rightWrist.second) * 0.5f + viewport.Height * 0.025f};

    std::vector<OverlayVertex> vertices;
    vertices.reserve(256);
    const DWORD sleeve = OverlayColor(38, 26, 32);
    const DWORD sleeveEdge = OverlayColor(108, 47, 42);
    const DWORD skin = OverlayColor(180, 97, 68);
    const DWORD skinLight = OverlayColor(235, 157, 103);
    const DWORD metal = OverlayColor(208, 215, 220);
    const DWORD darkMetal = OverlayColor(62, 66, 78);
    const DWORD wood = OverlayColor(79, 37, 28);
    const DWORD woodLight = OverlayColor(150, 81, 43);
    const bool axeBeat = controllers.right.x || controllers.right.trigger > 0.75f;
    const bool guitarBeat = controllers.right.y || controllers.left.y;

    OverlayAddSegment(vertices, leftShoulder.first, leftShoulder.second,
                      leftElbow.first, leftElbow.second, viewport.Height * 0.095f, sleeve);
    OverlayAddSegment(vertices, leftShoulder.first, leftShoulder.second,
                      leftElbow.first, leftElbow.second, viewport.Height * 0.052f, sleeveEdge);
    OverlayAddSegment(vertices, leftElbow.first, leftElbow.second,
                      leftWrist.first, leftWrist.second, viewport.Height * 0.078f, skin);
    OverlayAddSegment(vertices, leftElbow.first, leftElbow.second,
                      leftWrist.first, leftWrist.second, viewport.Height * 0.033f, skinLight);
    OverlayAddSegment(vertices, rightShoulder.first, rightShoulder.second,
                      rightElbow.first, rightElbow.second, viewport.Height * 0.095f, sleeve);
    OverlayAddSegment(vertices, rightShoulder.first, rightShoulder.second,
                      rightElbow.first, rightElbow.second, viewport.Height * 0.052f, sleeveEdge);
    OverlayAddSegment(vertices, rightElbow.first, rightElbow.second,
                      rightWrist.first, rightWrist.second, viewport.Height * 0.078f, skin);
    OverlayAddSegment(vertices, rightElbow.first, rightElbow.second,
                      rightWrist.first, rightWrist.second, viewport.Height * 0.033f, skinLight);
    OverlayAddDisc(vertices, leftWrist.first, leftWrist.second, viewport.Height * 0.050f, skin);
    OverlayAddDisc(vertices, rightWrist.first, rightWrist.second, viewport.Height * 0.050f, skin);

    // Wrist hardware gives the motion beat a readable controller reference.
    OverlayAddSegment(vertices, leftWrist.first - viewport.Width * 0.016f,
                      leftWrist.second + viewport.Height * 0.016f,
                      leftWrist.first + viewport.Width * 0.020f,
                      leftWrist.second - viewport.Height * 0.020f,
                      viewport.Height * 0.022f, metal);
    OverlayAddSegment(vertices, rightWrist.first - viewport.Width * 0.020f,
                      rightWrist.second + viewport.Height * 0.020f,
                      rightWrist.first + viewport.Width * 0.016f,
                      rightWrist.second - viewport.Height * 0.016f,
                      viewport.Height * 0.022f, metal);

    // Clementine: a near-field guitar body and neck held by the left hand.
    const float guitarX = leftWrist.first - viewport.Width * 0.125f;
    const float guitarY = leftWrist.second + viewport.Height * 0.115f;
    OverlayAddDisc(vertices, guitarX, guitarY, viewport.Height * 0.12f,
                   guitarBeat ? OverlayColor(218, 77, 43) : wood);
    OverlayAddDisc(vertices, guitarX, guitarY - viewport.Height * 0.018f,
                   viewport.Height * 0.073f,
                   guitarBeat ? OverlayColor(255, 160, 56) : woodLight);
    OverlayAddSegment(vertices, guitarX + viewport.Width * 0.025f, guitarY - viewport.Height * 0.045f,
                      leftWrist.first + viewport.Width * 0.045f, leftWrist.second - viewport.Height * 0.030f,
                      viewport.Height * 0.040f, wood);
    OverlayAddSegment(vertices, guitarX + viewport.Width * 0.025f, guitarY - viewport.Height * 0.045f,
                      leftWrist.first + viewport.Width * 0.045f, leftWrist.second - viewport.Height * 0.030f,
                      viewport.Height * 0.012f, metal);
    if (guitarBeat) {
        OverlayAddTriangle(vertices, guitarX - viewport.Width * 0.10f, guitarY,
                           guitarX - viewport.Width * 0.19f, guitarY - viewport.Height * 0.09f,
                           guitarX - viewport.Width * 0.16f, guitarY + viewport.Height * 0.08f,
                           OverlayColor(255, 234, 132, 215));
    }

    // Separator: a readable axe silhouette in the right hand.
    const float axeX = rightWrist.first + viewport.Width * 0.13f;
    const float axeY = rightWrist.second - viewport.Height * 0.15f;
    OverlayAddSegment(vertices, rightWrist.first, rightWrist.second,
                      axeX, axeY, viewport.Height * 0.028f, darkMetal);
    OverlayAddSegment(vertices, rightWrist.first, rightWrist.second,
                      axeX, axeY, viewport.Height * 0.010f, metal);
    const DWORD axeColor = axeBeat ? OverlayColor(255, 92, 42) : OverlayColor(186, 205, 220);
    OverlayAddTriangle(vertices, axeX, axeY,
                       axeX + viewport.Width * 0.105f, axeY - viewport.Height * 0.090f,
                       axeX + viewport.Width * 0.125f, axeY + viewport.Height * 0.035f,
                       axeColor);
    OverlayAddTriangle(vertices, axeX + viewport.Width * 0.008f, axeY,
                       axeX + viewport.Width * 0.075f, axeY - viewport.Height * 0.115f,
                       axeX + viewport.Width * 0.040f, axeY + viewport.Height * 0.020f,
                       darkMetal);

    DWORD oldFvf = 0;
    DWORD oldLighting = 0;
    DWORD oldZEnable = 0;
    DWORD oldZWrite = 0;
    DWORD oldAlphaTest = 0;
    DWORD oldAlphaBlend = 0;
    DWORD oldSrcBlend = 0;
    DWORD oldDestBlend = 0;
    DWORD oldCull = 0;
    DWORD oldColorWrite = 0;
    device->GetFVF(&oldFvf);
    device->GetRenderState(D3DRS_LIGHTING, &oldLighting);
    device->GetRenderState(D3DRS_ZENABLE, &oldZEnable);
    device->GetRenderState(D3DRS_ZWRITEENABLE, &oldZWrite);
    device->GetRenderState(D3DRS_ALPHATESTENABLE, &oldAlphaTest);
    device->GetRenderState(D3DRS_ALPHABLENDENABLE, &oldAlphaBlend);
    device->GetRenderState(D3DRS_SRCBLEND, &oldSrcBlend);
    device->GetRenderState(D3DRS_DESTBLEND, &oldDestBlend);
    device->GetRenderState(D3DRS_CULLMODE, &oldCull);
    device->GetRenderState(D3DRS_COLORWRITEENABLE, &oldColorWrite);
    IDirect3DVertexShader9* oldVertexShader = nullptr;
    IDirect3DPixelShader9* oldPixelShader = nullptr;
    IDirect3DVertexDeclaration9* oldVertexDeclaration = nullptr;
    IDirect3DBaseTexture9* oldTexture = nullptr;
    IDirect3DVertexBuffer9* oldStream = nullptr;
    IDirect3DIndexBuffer9* oldIndex = nullptr;
    UINT oldStreamOffset = 0;
    UINT oldStreamStride = 0;
    device->GetVertexShader(&oldVertexShader);
    device->GetPixelShader(&oldPixelShader);
    device->GetVertexDeclaration(&oldVertexDeclaration);
    device->GetTexture(0, &oldTexture);
    device->GetStreamSource(0, &oldStream, &oldStreamOffset, &oldStreamStride);
    device->GetIndices(&oldIndex);
    device->SetVertexShader(nullptr);
    device->SetPixelShader(nullptr);
    device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    device->SetTexture(0, nullptr);
    device->SetRenderState(D3DRS_LIGHTING, FALSE);
    device->SetRenderState(D3DRS_ZENABLE, FALSE);
    device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    device->SetRenderState(D3DRS_COLORWRITEENABLE,
                           D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN |
                           D3DCOLORWRITEENABLE_BLUE | D3DCOLORWRITEENABLE_ALPHA);
    device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, static_cast<UINT>(vertices.size() / 3),
                            vertices.data(), sizeof(OverlayVertex));

    // DrawPrimitiveUP changes stream 0 and the fixed-function declaration on
    // several D3D9 implementations.  Restore every piece explicitly; a
    // state-block Apply is not sufficient under the game's DXVK D3D9 layer
    // and leaves the next retail pass black.
    device->SetVertexShader(oldVertexShader);
    device->SetPixelShader(oldPixelShader);
    if (oldVertexDeclaration) device->SetVertexDeclaration(oldVertexDeclaration);
    else device->SetFVF(oldFvf);
    device->SetTexture(0, oldTexture);
    device->SetStreamSource(0, oldStream, oldStreamOffset, oldStreamStride);
    device->SetIndices(oldIndex);
    device->SetRenderState(D3DRS_LIGHTING, oldLighting);
    device->SetRenderState(D3DRS_ZENABLE, oldZEnable);
    device->SetRenderState(D3DRS_ZWRITEENABLE, oldZWrite);
    device->SetRenderState(D3DRS_ALPHATESTENABLE, oldAlphaTest);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, oldAlphaBlend);
    device->SetRenderState(D3DRS_SRCBLEND, oldSrcBlend);
    device->SetRenderState(D3DRS_DESTBLEND, oldDestBlend);
    device->SetRenderState(D3DRS_CULLMODE, oldCull);
    device->SetRenderState(D3DRS_COLORWRITEENABLE, oldColorWrite);
    if (oldVertexShader) oldVertexShader->Release();
    if (oldPixelShader) oldPixelShader->Release();
    if (oldVertexDeclaration) oldVertexDeclaration->Release();
    if (oldTexture) oldTexture->Release();
    if (oldStream) oldStream->Release();
    if (oldIndex) oldIndex->Release();
    ++g_FirstPersonOverlayDrawCount;
    if (g_FirstPersonOverlayDrawCount <= 3 || (g_FirstPersonOverlayDrawCount % 600) == 0) {
        BLVR::Log("BLVR first-person overlay draw #%u hands=(%.2f,%.2f)/(%.2f,%.2f) axe=%d guitar=%d",
                  g_FirstPersonOverlayDrawCount,
                  controllers.left.pose.pos[0], controllers.left.pose.pos[1],
                  controllers.right.pose.pos[0], controllers.right.pose.pos[1],
                  axeBeat ? 1 : 0, guitarBeat ? 1 : 0);
    }
}

static bool g_HooksInstalled = false;
static uint32_t g_PresentCount = 0;
static uint32_t g_GameplayTick = 0;
static volatile LONG g_PresentDepth = 0;
static HWND g_GameHwnd = nullptr;
static HANDLE g_hAutoAdvanceThread = nullptr;
static WNDPROC g_OriginalGameWndProc = nullptr;
static bool g_IgnoreFocusPause = false;
static bool g_GameplayFocusLock = false;
static volatile LONG g_AutomationEnabled = 0;
static bool g_XrXInputInstalled = false;
static bool g_XrNativePadPolled = false;
static volatile LONG g_TitleEventOnly = 0;
static volatile LONG g_TitleEventOnlyArmed = 0;
static bool g_RecordDelayConfigured = false;
static bool g_RecordDelayArmed = false;
static uint32_t g_RecordDelayPresents = 0;
static uint32_t g_RecordEligiblePresent = 0;
static bool g_PresentPacingConfigured = false;
static double g_PresentPacingFps = 0.0;
static LARGE_INTEGER g_PresentPacingFrequency{};
static LONGLONG g_LastPacedPresentTick = 0;

static void PaceD3D9Present() {
    if (!g_PresentPacingConfigured) {
        g_PresentPacingConfigured = true;
        char fpsEnv[32] = {};
        GetEnvironmentVariableA("BLVR_FRAME_LIMIT_FPS", fpsEnv, sizeof(fpsEnv));
        const double requestedFps = fpsEnv[0] ? atof(fpsEnv) : 0.0;
        if (requestedFps >= 15.0 && requestedFps <= 240.0 &&
            QueryPerformanceFrequency(&g_PresentPacingFrequency)) {
            g_PresentPacingFps = requestedFps;
            BLVR::Log("D3D9 Present pacing enabled at %.1f FPS", g_PresentPacingFps);
        }
    }
    if (g_PresentPacingFps <= 0.0 || g_PresentPacingFrequency.QuadPart <= 0) return;

    LARGE_INTEGER now{};
    if (!QueryPerformanceCounter(&now)) return;
    const LONGLONG interval = static_cast<LONGLONG>(
        static_cast<double>(g_PresentPacingFrequency.QuadPart) / g_PresentPacingFps);
    if (g_LastPacedPresentTick == 0) {
        g_LastPacedPresentTick = now.QuadPart;
        return;
    }

    const LONGLONG deadline = g_LastPacedPresentTick + interval;
    if (now.QuadPart >= deadline) {
        // Do not accumulate debt when rendering or the runtime already took
        // longer than the selected frame interval.
        g_LastPacedPresentTick = now.QuadPart;
        return;
    }

    while (now.QuadPart < deadline) {
        const LONGLONG remaining = deadline - now.QuadPart;
        if (remaining > g_PresentPacingFrequency.QuadPart / 500) {
            Sleep(1);
        } else {
            SwitchToThread();
        }
        if (!QueryPerformanceCounter(&now)) break;
    }
    g_LastPacedPresentTick = deadline;
}

static bool IsTrackedRenderState(D3DRENDERSTATETYPE state) {
    switch (state) {
    case D3DRS_ZENABLE:
    case D3DRS_ZWRITEENABLE:
    case D3DRS_ALPHATESTENABLE:
    case D3DRS_ALPHAREF:
    case D3DRS_ALPHAFUNC:
    case D3DRS_ALPHABLENDENABLE:
    case D3DRS_SRCBLEND:
    case D3DRS_DESTBLEND:
    case D3DRS_CULLMODE:
    case D3DRS_COLORWRITEENABLE:
        return true;
    default:
        return false;
    }
}

static const char* RenderStateName(D3DRENDERSTATETYPE state) {
    switch (state) {
    case D3DRS_ZENABLE: return "ZENABLE";
    case D3DRS_ZWRITEENABLE: return "ZWRITEENABLE";
    case D3DRS_ALPHATESTENABLE: return "ALPHATESTENABLE";
    case D3DRS_ALPHAREF: return "ALPHAREF";
    case D3DRS_ALPHAFUNC: return "ALPHAFUNC";
    case D3DRS_ALPHABLENDENABLE: return "ALPHABLENDENABLE";
    case D3DRS_SRCBLEND: return "SRCBLEND";
    case D3DRS_DESTBLEND: return "DESTBLEND";
    case D3DRS_CULLMODE: return "CULLMODE";
    case D3DRS_COLORWRITEENABLE: return "COLORWRITEENABLE";
    default: return "OTHER";
    }
}

static HRESULT SetOriginalRenderState(
    IDirect3DDevice9* device,
    D3DRENDERSTATETYPE state,
    DWORD value
) {
    return g_Original_SetRenderState
        ? g_Original_SetRenderState(device, state, value)
        : device->SetRenderState(state, value);
}

static void ApplyOpaqueArmRenderStates(IDirect3DDevice9* device) {
    if (!device || !g_ForceOpaqueEnabled) return;
    SetOriginalRenderState(device, D3DRS_ZENABLE, TRUE);
    SetOriginalRenderState(device, D3DRS_ZWRITEENABLE, TRUE);
    SetOriginalRenderState(device, D3DRS_ALPHATESTENABLE, FALSE);
    SetOriginalRenderState(device, D3DRS_ALPHABLENDENABLE, FALSE);
    SetOriginalRenderState(device, D3DRS_SRCBLEND, D3DBLEND_ONE);
    SetOriginalRenderState(device, D3DRS_DESTBLEND, D3DBLEND_ZERO);
    SetOriginalRenderState(device, D3DRS_CULLMODE, D3DCULL_NONE);
    SetOriginalRenderState(device, D3DRS_COLORWRITEENABLE,
                           D3DCOLORWRITEENABLE_RED |
                           D3DCOLORWRITEENABLE_GREEN |
                           D3DCOLORWRITEENABLE_BLUE |
                           D3DCOLORWRITEENABLE_ALPHA);
    g_ForceOpaqueApplyCount++;
    if (g_ForceOpaqueApplyCount <= 3 || (g_ForceOpaqueApplyCount % 1000) == 0) {
        BLVR::Log("BLVR opaque arm render state applied #%u", g_ForceOpaqueApplyCount);
    }
}

static HRESULT WINAPI Hook_SetRenderState(
    IDirect3DDevice9* thisPtr,
    D3DRENDERSTATETYPE state,
    DWORD value
) {
    const bool gameplay = BLVR::CameraHook_IsActive();
    if (gameplay && !g_RenderStateTraceConfigured) {
        char traceEnv[16] = {};
        GetEnvironmentVariableA("BLVR_TRACE_RENDER_STATES", traceEnv, sizeof(traceEnv));
        g_RenderStateTraceEnabled = traceEnv[0] == '1' || _stricmp(traceEnv, "true") == 0;
        g_RenderStateTraceConfigured = true;
        if (g_RenderStateTraceEnabled) {
            BLVR::Log("D3D9 render-state trace enabled");
        }
    }
    if (gameplay && g_RenderStateTraceEnabled && IsTrackedRenderState(state) &&
        g_RenderStateTraceCount < 320) {
        ++g_RenderStateTraceCount;
        BLVR::Log("D3D9 render state #%u caller=%p %s(%u)=0x%08X armReady=%d",
                  g_RenderStateTraceCount, _ReturnAddress(), RenderStateName(state),
                  static_cast<unsigned>(state), static_cast<unsigned>(value),
                  g_ArmPaletteReadyForDraw ? 1 : 0);
    }

    if (gameplay && g_ForceOpaqueEnabled && g_ArmPaletteReadyForDraw) {
        switch (state) {
        case D3DRS_ZENABLE: value = TRUE; break;
        case D3DRS_ZWRITEENABLE: value = TRUE; break;
        case D3DRS_ALPHATESTENABLE: value = FALSE; break;
        case D3DRS_ALPHABLENDENABLE: value = FALSE; break;
        case D3DRS_SRCBLEND: value = D3DBLEND_ONE; break;
        case D3DRS_DESTBLEND: value = D3DBLEND_ZERO; break;
        case D3DRS_CULLMODE: value = D3DCULL_NONE; break;
        case D3DRS_COLORWRITEENABLE:
            value = D3DCOLORWRITEENABLE_RED |
                    D3DCOLORWRITEENABLE_GREEN |
                    D3DCOLORWRITEENABLE_BLUE |
                    D3DCOLORWRITEENABLE_ALPHA;
            break;
        default: break;
        }
    }
    return SetOriginalRenderState(thisPtr, state, value);
}

static HRESULT WINAPI Hook_DrawIndexedPrimitive(
    IDirect3DDevice9* thisPtr,
    D3DPRIMITIVETYPE primitiveType,
    INT baseVertexIndex,
    UINT minVertexIndex,
    UINT numVertices,
    UINT startIndex,
    UINT primCount
) {
    const bool gameplay = BLVR::CameraHook_IsActive();
    if (gameplay && !g_ArmDrawTraceConfigured) {
        char traceEnv[16] = {};
        GetEnvironmentVariableA("BLVR_TRACE_ARM_DRAWS", traceEnv, sizeof(traceEnv));
        g_ArmDrawTraceEnabled = traceEnv[0] == '1' || _stricmp(traceEnv, "true") == 0;
        g_ArmDrawTraceConfigured = true;
        if (g_ArmDrawTraceEnabled) {
            BLVR::Log("D3D9 skinned draw trace enabled");
        }
    }
    if (gameplay && !g_BodyDrawSkipConfigured) {
        char skipEnv[32] = {};
        GetEnvironmentVariableA("BLVR_BODY_DRAW_SKIP_START", skipEnv, sizeof(skipEnv));
        if (skipEnv[0]) g_BodyDrawSkipStart = atoi(skipEnv);
        skipEnv[0] = '\0';
        GetEnvironmentVariableA("BLVR_BODY_DRAW_SKIP_END", skipEnv, sizeof(skipEnv));
        if (skipEnv[0]) g_BodyDrawSkipEnd = atoi(skipEnv);
        char sweepEnv[16] = {};
        GetEnvironmentVariableA("BLVR_BODY_DRAW_SKIP_SWEEP", sweepEnv, sizeof(sweepEnv));
        g_BodyDrawSkipSweepEnabled = sweepEnv[0] == '1' || _stricmp(sweepEnv, "true") == 0;
        g_BodyDrawSkipConfigured = true;
        BLVR::Log("BLVR body draw skip afterPalette=%d..%d sweep=%d",
                  g_BodyDrawSkipStart, g_BodyDrawSkipEnd,
                  g_BodyDrawSkipSweepEnabled ? 1 : 0);
    }
    if (gameplay && !g_NativeHeadHideConfigured) {
        char hideEnv[16] = {};
        GetEnvironmentVariableA("BLVR_HIDE_NATIVE_HEAD", hideEnv, sizeof(hideEnv));
        g_NativeHeadHideEnabled = hideEnv[0] == '1' || _stricmp(hideEnv, "true") == 0;
        g_NativeHeadHideConfigured = true;
        BLVR::Log("BLVR native head/body visibility suppression=%d",
                  g_NativeHeadHideEnabled ? 1 : 0);
    }
    if (gameplay && !g_HeadOnlyArmsConfigured) {
        char armsOnlyEnv[16] = {};
        GetEnvironmentVariableA("BLVR_HEAD_ONLY_ARMS", armsOnlyEnv, sizeof(armsOnlyEnv));
        g_HeadOnlyArmsEnabled = armsOnlyEnv[0] == '1' || _stricmp(armsOnlyEnv, "true") == 0;
        char maskEnv[16] = {};
        GetEnvironmentVariableA("BLVR_HEAD_ONLY_ARMS_MASK", maskEnv, sizeof(maskEnv));
        if (maskEnv[0]) {
            char* end = nullptr;
            const unsigned long parsed = strtoul(maskEnv, &end, 0);
            if (end != maskEnv) g_HeadOnlyArmsMask = static_cast<uint32_t>(parsed) & 0x3Fu;
        }
        g_HeadOnlyArmsConfigured = true;
        BLVR::Log("BLVR arms-only head/torso collapse=%d mask=0x%02X",
                  g_HeadOnlyArmsEnabled ? 1 : 0, g_HeadOnlyArmsMask);
    }

    const bool bodyWorld = gameplay && g_HasLastObjectWorld &&
        fabsf(g_LastObjectWorld[12]) < 1.0f &&
        g_LastObjectWorld[13] > -3.25f && g_LastObjectWorld[13] < -1.25f &&
        fabsf(g_LastObjectWorld[14]) < 1.0f;
    // The player actor is submitted as a mixed skinned object: head/torso
    // passes and the first-person arm/weapon passes share the same near-field
    // world transform.  Keep arm-palette submissions alive so the tracked
    // controller rig can publish native hands/equipment; suppress only the
    // non-arm submissions that contain the camera-obstructing head/body.
    if (bodyWorld && g_NativeHeadHideEnabled && !g_ArmPaletteReadyForDraw) {
        ++g_NativeHeadHideCount;
        if (g_NativeHeadHideCount <= 3 || (g_NativeHeadHideCount % 10000) == 0) {
            BLVR::Log("BLVR native head/body draw suppressed #%u worldT=(%.2f,%.2f,%.2f)",
                      g_NativeHeadHideCount,
                      g_LastObjectWorld[12], g_LastObjectWorld[13], g_LastObjectWorld[14]);
        }
        return D3D_OK;
    }

    if (gameplay && g_ArmPaletteReadyForDraw) {
        ++g_ArmPaletteDrawsSinceUpload;
        int skipStart = g_BodyDrawSkipStart;
        int skipEnd = g_BodyDrawSkipEnd;
        if (g_BodyDrawSkipSweepEnabled) {
            // One capture walks the candidate body ranges in one-second
            // slices. This makes head/arm isolation empirical instead of
            // burning a fresh title-to-game run for every guessed range.
            const int phase = std::min(7, BLVR::VideoCapture_GetRecordedFrames() / 30);
            if (phase != g_BodyDrawSkipSweepPhase) {
                g_BodyDrawSkipSweepPhase = phase;
                static const int starts[] = {-1, 0, 41, 47, 50, 55, 61, 69};
                static const int ends[] = {-1, 40, 46, 49, 54, 60, 68, 90};
                skipStart = starts[phase];
                skipEnd = ends[phase];
                BLVR::Log("BLVR body draw skip sweep phase=%d range=%d..%d recorded=%d",
                          phase, skipStart, skipEnd,
                          BLVR::VideoCapture_GetRecordedFrames());
            } else {
                static const int starts[] = {-1, 0, 41, 47, 50, 55, 61, 69};
                static const int ends[] = {-1, 40, 46, 49, 54, 60, 68, 90};
                skipStart = starts[phase];
                skipEnd = ends[phase];
            }
        }
        if (bodyWorld && skipStart >= 0 && skipEnd >= skipStart &&
            static_cast<int>(g_ArmPaletteDrawsSinceUpload) >= skipStart &&
            static_cast<int>(g_ArmPaletteDrawsSinceUpload) <= skipEnd) {
            return D3D_OK;
        }
        if (g_ArmDrawTraceEnabled && g_ArmDrawTraceCount < 160) {
            ++g_ArmDrawTraceCount;
            // Keep this diagnostic side-effect free. Calling GetMaterial or
            // GetRenderState from inside the retail draw hook can re-enter
            // the D3D9 wrapper and destabilize the game. The draw geometry
            // and palette ordinal are sufficient to identify the mixed
            // body/arm ranges.
            BLVR::Log("D3D9 skinned draw #%u palette=%u afterPalette=%u type=%u base=%d min=%u verts=%u start=%u prims=%u worldT=(%.2f,%.2f,%.2f)",
                      g_ArmDrawTraceCount, g_ArmPaletteSequence,
                      g_ArmPaletteDrawsSinceUpload, static_cast<unsigned>(primitiveType),
                      baseVertexIndex, minVertexIndex, numVertices, startIndex, primCount,
                      g_HasLastObjectWorld ? g_LastObjectWorld[12] : 0.0f,
                      g_HasLastObjectWorld ? g_LastObjectWorld[13] : 0.0f,
                      g_HasLastObjectWorld ? g_LastObjectWorld[14] : 0.0f);
        }
    }
    InterlockedIncrement(&g_D3D9DrawCallCount);
    return g_Original_DrawIndexedPrimitive(
        thisPtr, primitiveType, baseVertexIndex, minVertexIndex,
        numVertices, startIndex, primCount);
}

extern "C" uint32_t __cdecl BLVR_GetD3D9DrawCallCount() {
    return static_cast<uint32_t>(InterlockedCompareExchange(
        &g_D3D9DrawCallCount, 0, 0));
}

extern "C" IDirect3DDevice9* __cdecl BLVR_GetD3D9Device() {
    return g_LastD3D9Device;
}

// The retail front end needs Start to skip intro logos and open the vinyl record,
// followed by A to select "Continue". Once "Continue" is selected and level
// loading begins (around present 550), automation MUST stop pressing buttons!
// Pressing Start during or after loading opens the pause menu, and pressing A
// while paused clicks "Load Checkpoint".
static bool RetailAutomationShouldPress(bool& outIsStart) {
    if (g_PresentCount < 45 || g_PresentCount >= 550) {
        return false;
    }
    const bool press = ((g_PresentCount % 20) < 10);
    if (!press) return false;
    outIsStart = (g_PresentCount < 240u);
    return true;
}

static bool AutomationEnabled() {
    return InterlockedCompareExchange(&g_AutomationEnabled, 0, 0) != 0;
}

static bool TitleEventOnlyEnabled() {
    return InterlockedCompareExchange(&g_TitleEventOnly, 0, 0) != 0;
}

static bool TitleEventOnlyArmed() {
    return InterlockedCompareExchange(&g_TitleEventOnlyArmed, 0, 0) != 0;
}

static HRESULT WINAPI Hook_SetTransform(
    IDirect3DDevice9* thisPtr,
    D3DTRANSFORMSTATETYPE state,
    const D3DMATRIX* matrix
) {
    if (state == D3DTS_VIEW || state == D3DTS_PROJECTION || state == D3DTS_WORLD) {
        g_SetTransformViewCount++;
        if (g_SetTransformViewCount <= 24) {
            BLVR::Log("D3D9 SetTransform state=%u caller=%p row0=(%.3f,%.3f,%.3f,%.3f) row1=(%.3f,%.3f,%.3f,%.3f) row2=(%.3f,%.3f,%.3f,%.3f) row3=(%.3f,%.3f,%.3f,%.3f)",
                      static_cast<unsigned>(state), _ReturnAddress(),
                      matrix ? matrix->m[0][0] : 0.0f, matrix ? matrix->m[0][1] : 0.0f,
                      matrix ? matrix->m[0][2] : 0.0f, matrix ? matrix->m[0][3] : 0.0f,
                      matrix ? matrix->m[1][0] : 0.0f, matrix ? matrix->m[1][1] : 0.0f,
                      matrix ? matrix->m[1][2] : 0.0f, matrix ? matrix->m[1][3] : 0.0f,
                      matrix ? matrix->m[2][0] : 0.0f, matrix ? matrix->m[2][1] : 0.0f,
                      matrix ? matrix->m[2][2] : 0.0f, matrix ? matrix->m[2][3] : 0.0f,
                      matrix ? matrix->m[3][0] : 0.0f, matrix ? matrix->m[3][1] : 0.0f,
                      matrix ? matrix->m[3][2] : 0.0f, matrix ? matrix->m[3][3] : 0.0f);
        }
    }
    if (state == D3DTS_VIEW) {
        D3DMATRIX firstPersonView{};
        if (BLVR::CameraHook_GetViewMatrix(&firstPersonView.m[0][0])) {
            g_SetTransformOverrideCount++;
            if (g_SetTransformOverrideCount <= 3 || (g_SetTransformOverrideCount % 600) == 0) {
                BLVR::Log("D3D9 SetTransform VIEW #%u override=%u", g_SetTransformViewCount, g_SetTransformOverrideCount);
            }
            return g_Original_SetTransform(thisPtr, state, &firstPersonView);
        }
    }
    return g_Original_SetTransform(thisPtr, state, matrix);
}

struct ArmVector3 {
    float x;
    float y;
    float z;
};

static ArmVector3 PaletteBonePosition(const float* palette, uint32_t bone) {
    const uint32_t base = bone * 12;
    return {palette[base + 3], palette[base + 7], palette[base + 11]};
}

static void SetPaletteBonePosition(float* palette, uint32_t bone, ArmVector3 position) {
    const uint32_t base = bone * 12;
    palette[base + 3] = position.x;
    palette[base + 7] = position.y;
    palette[base + 11] = position.z;
}

static ArmVector3 AddArmVector(ArmVector3 left, ArmVector3 right) {
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}

static ArmVector3 SubtractArmVector(ArmVector3 left, ArmVector3 right) {
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}

static ArmVector3 ScaleArmVector(ArmVector3 value, float scale) {
    return {value.x * scale, value.y * scale, value.z * scale};
}

static float DotArmVector(ArmVector3 left, ArmVector3 right) {
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

static float LengthArmVector(ArmVector3 value) {
    return sqrtf(DotArmVector(value, value));
}

static ArmVector3 NormalizeArmVector(ArmVector3 value) {
    const float length = LengthArmVector(value);
    return length > 0.0001f ? ScaleArmVector(value, 1.0f / length) : ArmVector3{0.0f, 0.0f, 0.0f};
}

static void MultiplyArmMatrix(const float* left, const float* right, float* output) {
    float result[16] = {};
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            result[row * 4 + column] =
                left[row * 4 + 0] * right[0 * 4 + column] +
                left[row * 4 + 1] * right[1 * 4 + column] +
                left[row * 4 + 2] * right[2 * 4 + column] +
                left[row * 4 + 3] * right[3 * 4 + column];
        }
    }
    memcpy(output, result, sizeof(result));
}

static ArmVector3 SolveArmElbow(ArmVector3 shoulder, ArmVector3 elbow, ArmVector3 wrist,
                                ArmVector3 target, float bendSide) {
    const float upperLength = LengthArmVector(SubtractArmVector(elbow, shoulder));
    const float lowerLength = LengthArmVector(SubtractArmVector(wrist, elbow));
    ArmVector3 ray = SubtractArmVector(target, shoulder);
    float reach = LengthArmVector(ray);
    if (upperLength < 0.05f || lowerLength < 0.05f || reach < 0.0001f) return elbow;

    const float minimumReach = fabsf(upperLength - lowerLength) + 0.001f;
    const float maximumReach = upperLength + lowerLength - 0.001f;
    reach = std::max(minimumReach, std::min(maximumReach, reach));
    const ArmVector3 direction = NormalizeArmVector(ray);
    const float along = (upperLength * upperLength - lowerLength * lowerLength + reach * reach) /
                        (2.0f * reach);
    const float bendLength = sqrtf(std::max(0.0f, upperLength * upperLength - along * along));
    ArmVector3 bendHint = NormalizeArmVector({bendSide, -0.75f, -0.20f});
    bendHint = SubtractArmVector(bendHint, ScaleArmVector(direction, DotArmVector(bendHint, direction)));
    bendHint = NormalizeArmVector(bendHint);
    if (LengthArmVector(bendHint) < 0.0001f) bendHint = {0.0f, -1.0f, 0.0f};
    return AddArmVector(AddArmVector(shoulder, ScaleArmVector(direction, along)),
                        ScaleArmVector(bendHint, bendLength));
}

static bool ApplyTrackedArmRig(float* palette, uint32_t vector4fCount) {
    if (!palette || vector4fCount < 96 || !g_ArmRigEnabled) return false;

    const ArmVector3 candidateHead = PaletteBonePosition(palette, 0);
    float minimumX = 1e30f;
    float maximumX = -1e30f;
    float minimumZ = 1e30f;
    float maximumZ = -1e30f;
    float maximumY = -1e30f;
    for (uint32_t bone = 0; bone < 32; ++bone) {
        const ArmVector3 position = PaletteBonePosition(palette, bone);
        if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z)) return false;
        minimumX = std::min(minimumX, position.x);
        maximumX = std::max(maximumX, position.x);
        minimumZ = std::min(minimumZ, position.z);
        maximumZ = std::max(maximumZ, position.z);
        maximumY = std::max(maximumY, position.y);
    }
    if (candidateHead.x < -0.25f || candidateHead.x > 0.25f ||
        candidateHead.y < 1.50f || candidateHead.y > 1.85f ||
        candidateHead.z < -0.25f || candidateHead.z > 0.25f ||
        maximumY < 1.6f || maximumX < 1.0f || minimumX > -0.8f ||
        maximumZ - minimumZ < 1.0f) return false;

    if (g_BodyObjectAnchorEnabled) {
        if (!g_BodyObjectAnchorLocked) {
            g_BodyObjectAnchorPending = true;
            g_BodyObjectAnchorHead[0] = candidateHead.x;
            g_BodyObjectAnchorHead[1] = candidateHead.y;
            g_BodyObjectAnchorHead[2] = candidateHead.z;
            BLVR::CameraHook_SetNativeBodyHeadLocal(candidateHead.x, candidateHead.y, candidateHead.z);
        }
    } else {
        BLVR::CameraHook_SetNativeBodyHeadLocal(candidateHead.x, candidateHead.y, candidateHead.z);
    }

    BLVR::ControllerState controllers{};
    if (!BLVR::XrHost::Get().GetControllerState(controllers) || !controllers.runtimeActive ||
        !controllers.left.tracked || !controllers.right.tracked) return false;

    if (fabsf(g_ArmBodyYOffset) > 0.0001f || fabsf(g_ArmBodyXOffset) > 0.0001f) {
        for (uint32_t bone = 0; bone < 32; ++bone) {
            ArmVector3 position = PaletteBonePosition(palette, bone);
            position.x += g_ArmBodyXOffset;
            position.y += g_ArmBodyYOffset;
            SetPaletteBonePosition(palette, bone, position);
        }
    }

    if (fabsf(g_ArmHeadXOffset) > 0.0001f) {
        ArmVector3 headOnly = PaletteBonePosition(palette, 0);
        headOnly.x += g_ArmHeadXOffset;
        SetPaletteBonePosition(palette, 0, headOnly);
    }

    const ArmVector3 head = PaletteBonePosition(palette, 0);
    const uint32_t shoulderBones[2] = {6, 10};
    const uint32_t elbowBones[2] = {7, 11};
    const uint32_t wristBones[2] = {8, 12};
    const BLVR::HandState* hands[2] = {&controllers.left, &controllers.right};
    for (uint32_t side = 0; side < 2; ++side) {
        const ArmVector3 shoulder = PaletteBonePosition(palette, shoulderBones[side]);
        const ArmVector3 elbow = PaletteBonePosition(palette, elbowBones[side]);
        const ArmVector3 wrist = PaletteBonePosition(palette, wristBones[side]);
        const ArmVector3 target = AddArmVector(head, {
            hands[side]->pose.pos[0], hands[side]->pose.pos[1], hands[side]->pose.pos[2]});
        const ArmVector3 solvedElbow = SolveArmElbow(shoulder, elbow, wrist, target,
                                                      side == 0 ? -0.35f : 0.35f);
        const ArmVector3 solvedWrist = target;
        SetPaletteBonePosition(palette, elbowBones[side], solvedElbow);
        SetPaletteBonePosition(palette, wristBones[side], solvedWrist);
    }
    g_ArmRigOverrideCount++;
    if (g_ArmRigOverrideCount <= 3 || (g_ArmRigOverrideCount % 1000) == 0) {
        BLVR::Log("BLVR arm rig palette override #%u head=(%.2f,%.2f,%.2f) wrists=(%.2f,%.2f,%.2f)/(%.2f,%.2f,%.2f)",
                  g_ArmRigOverrideCount, head.x, head.y, head.z,
                  PaletteBonePosition(palette, wristBones[0]).x,
                  PaletteBonePosition(palette, wristBones[0]).y,
                  PaletteBonePosition(palette, wristBones[0]).z,
                  PaletteBonePosition(palette, wristBones[1]).x,
                  PaletteBonePosition(palette, wristBones[1]).y,
                  PaletteBonePosition(palette, wristBones[1]).z);
    }
    return true;
}

// The retail first-person candidate uses one skinned palette for the head,
// torso, arms, and attached equipment.  Keeping the whole palette makes the
// third-person Eddie mesh appear in front of the headset.  Collapse only the
// non-arm torso/head bones; the shoulder/elbow/wrist bones and their attached
// equipment remain native and continue to receive tracked controller poses.
static void HideNativeHeadAndTorsoBones(float* palette) {
    if (!palette) return;
    static const uint32_t hiddenBones[] = {0, 1, 2, 3, 4, 5};
    for (uint32_t bone : hiddenBones) {
        // The mask is a visibility contract, not a mode flag.  In particular,
        // mask 0x01 hides Eddie's head bone while leaving the shoulder/elbow/
        // wrist chain and the torso passes untouched.  The previous code
        // ignored the mask and collapsed all six bones, which could erase the
        // same skinned submission that carries the controller-driven arms.
        if ((g_HeadOnlyArmsMask & (1u << bone)) == 0) continue;
        const uint32_t base = bone * 12;
        palette[base + 0] = 0.0f;
        palette[base + 1] = 0.0f;
        palette[base + 2] = 0.0f;
        palette[base + 3] = 0.0f;
        palette[base + 4] = 0.0f;
        palette[base + 5] = 0.0f;
        palette[base + 6] = 0.0f;
        palette[base + 7] = -1000.0f;
        palette[base + 8] = 0.0f;
        palette[base + 9] = 0.0f;
        palette[base + 10] = 0.0f;
        palette[base + 11] = 0.0f;
    }
    ++g_HeadOnlyArmsApplyCount;
    if (g_HeadOnlyArmsApplyCount <= 3 || (g_HeadOnlyArmsApplyCount % 1000) == 0) {
        BLVR::Log("BLVR arms-only palette: collapsed head/torso bones #%u", g_HeadOnlyArmsApplyCount);
    }
}

static void ProbePaletteOwner(
    IDirect3DDevice9* device,
    const float* palette,
    uintptr_t caller,
    uintptr_t callerEdi,
    uintptr_t callerEsi,
    uintptr_t callerEbx,
    uintptr_t callerFrame
) {
    if (!palette) return;

    g_PaletteUploadCount++;
    bool knownCaller = false;
    for (uint32_t index = 0; index < g_PaletteOwnerLogCount; ++index) {
        if (g_PaletteOwnerCallers[index] == caller) {
            knownCaller = true;
            break;
        }
    }

    const ArmVector3 head = PaletteBonePosition(palette, 0);
    const ArmVector3 leftShoulder = PaletteBonePosition(palette, 6);
    const ArmVector3 leftWrist = PaletteBonePosition(palette, 8);
    const ArmVector3 rightShoulder = PaletteBonePosition(palette, 10);
    const ArmVector3 rightWrist = PaletteBonePosition(palette, 12);
    const bool candidatePhase = head.x > -0.25f && head.x < 0.25f &&
        head.y > 1.50f && head.y < 1.85f && head.z > -0.25f && head.z < 0.25f;

    if (!knownCaller && g_PaletteOwnerLogCount < 32) {
        g_PaletteOwnerCallers[g_PaletteOwnerLogCount++] = caller;
        const uintptr_t executableBase = reinterpret_cast<uintptr_t>(GetModuleHandleA(NULL));
        const uintptr_t callerRva = executableBase && caller >= executableBase
            ? caller - executableBase : 0;
        uintptr_t ownerDevice = 0;
        uintptr_t ownerMeta = 0;
        uintptr_t ownerEntries = 0;
        uint32_t ownerState = 0;
        uintptr_t frameEntries = 0;
        uint32_t frameCount = 0;
        uintptr_t frameEntry = 0;
        uintptr_t frameArgument = 0;
        __try {
            ownerDevice = *reinterpret_cast<uintptr_t*>(callerEdi + 0x04);
            ownerMeta = *reinterpret_cast<uintptr_t*>(callerEdi + 0x08);
            ownerEntries = *reinterpret_cast<uintptr_t*>(callerEdi + 0x20);
            ownerState = *reinterpret_cast<uint32_t*>(callerEdi + 0x2c);
            frameEntries = *reinterpret_cast<uintptr_t*>(callerFrame - 0x08);
            frameCount = *reinterpret_cast<uint32_t*>(callerFrame - 0x04);
            frameEntry = *reinterpret_cast<uintptr_t*>(callerFrame - 0x10);
            frameArgument = *reinterpret_cast<uintptr_t*>(callerFrame + 0x08);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            ownerDevice = 0;
            ownerMeta = 0;
            ownerEntries = 0;
            ownerState = 0;
            frameEntries = 0;
            frameCount = 0;
            frameEntry = 0;
            frameArgument = 0;
        }
        BLVR::Log(
            "BLVR palette owner #%u caller=%p rva=0x%08X device=%p data=%p head=(%.2f,%.2f,%.2f) "
            "L=(%.2f,%.2f,%.2f)->(%.2f,%.2f,%.2f) R=(%.2f,%.2f,%.2f)->(%.2f,%.2f,%.2f) "
            "world=%s(%.2f,%.2f,%.2f) regs=edi:%p esi:%p ebx:%p owner(+4)=%p(+8)=%p(+20)=%p(+2c)=0x%08X "
            "frame=%p entries=%p count=%u entry=%p arg=%p",
            g_PaletteOwnerLogCount, reinterpret_cast<void*>(caller), static_cast<unsigned>(callerRva),
            device, palette, head.x, head.y, head.z,
            leftShoulder.x, leftShoulder.y, leftShoulder.z,
            leftWrist.x, leftWrist.y, leftWrist.z,
            rightShoulder.x, rightShoulder.y, rightShoulder.z,
            rightWrist.x, rightWrist.y, rightWrist.z,
            g_HasLastObjectWorld ? "yes" : "no",
            g_HasLastObjectWorld ? g_LastObjectWorld[12] : 0.0f,
            g_HasLastObjectWorld ? g_LastObjectWorld[13] : 0.0f,
            g_HasLastObjectWorld ? g_LastObjectWorld[14] : 0.0f,
            reinterpret_cast<void*>(callerEdi),
            reinterpret_cast<void*>(callerEsi),
            reinterpret_cast<void*>(callerEbx),
            reinterpret_cast<void*>(ownerDevice),
            reinterpret_cast<void*>(ownerMeta),
            reinterpret_cast<void*>(ownerEntries),
            ownerState,
            reinterpret_cast<void*>(callerFrame),
            reinterpret_cast<void*>(frameEntries),
            frameCount,
            reinterpret_cast<void*>(frameEntry),
            reinterpret_cast<void*>(frameArgument));
    }
    if (candidatePhase && g_PaletteCandidateLogCount < 8) {
        g_PaletteCandidateLogCount++;
        BLVR::Log(
            "BLVR palette candidate #%u upload=%u caller=%p head=(%.2f,%.2f,%.2f) "
            "L=(%.2f,%.2f,%.2f)->(%.2f,%.2f,%.2f) R=(%.2f,%.2f,%.2f)->(%.2f,%.2f,%.2f) "
            "world12=(%.2f,%.2f,%.2f) worldRow=(%.2f,%.2f,%.2f) worldW=(%.2f,%.2f,%.2f,%.2f)",
            g_PaletteCandidateLogCount, g_PaletteUploadCount,
            reinterpret_cast<void*>(caller), head.x, head.y, head.z,
            leftShoulder.x, leftShoulder.y, leftShoulder.z,
            leftWrist.x, leftWrist.y, leftWrist.z,
            rightShoulder.x, rightShoulder.y, rightShoulder.z,
            rightWrist.x, rightWrist.y, rightWrist.z,
            g_HasLastObjectWorld ? g_LastObjectWorld[12] : 0.0f,
            g_HasLastObjectWorld ? g_LastObjectWorld[13] : 0.0f,
            g_HasLastObjectWorld ? g_LastObjectWorld[14] : 0.0f,
            g_HasLastObjectWorld ? g_LastObjectWorld[3] : 0.0f,
            g_HasLastObjectWorld ? g_LastObjectWorld[7] : 0.0f,
            g_HasLastObjectWorld ? g_LastObjectWorld[11] : 0.0f,
            g_HasLastObjectWorld ? g_LastObjectWorld[0] : 0.0f,
            g_HasLastObjectWorld ? g_LastObjectWorld[5] : 0.0f,
            g_HasLastObjectWorld ? g_LastObjectWorld[10] : 0.0f,
            g_HasLastObjectWorld ? g_LastObjectWorld[15] : 0.0f);
        if (g_ArmBoneTraceEnabled && g_PaletteCandidateLogCount == 8 && !g_ArmBoneTraceLogged) {
            g_ArmBoneTraceLogged = true;
            for (uint32_t bone = 0; bone < 32; ++bone) {
                const ArmVector3 position = PaletteBonePosition(palette, bone);
                BLVR::Log("BLVR arm bone[%u]=(%.3f,%.3f,%.3f)",
                          bone, position.x, position.y, position.z);
            }
        }
    } else if ((g_PaletteUploadCount % 10000) == 0) {
        BLVR::Log("BLVR palette uploads=%u owners=%u lastCaller=%p",
                  g_PaletteUploadCount, g_PaletteOwnerLogCount,
                  reinterpret_cast<void*>(caller));
    }
}

static HRESULT WINAPI Hook_SetVertexShaderConstantF(
    IDirect3DDevice9* thisPtr,
    UINT startRegister,
    const float* data,
    UINT vector4fCount
) {
    const bool gameplay = BLVR::CameraHook_IsActive();
    if (gameplay && !g_ConstantTraceConfigured) {
        char traceEnv[16] = {};
        GetEnvironmentVariableA("BLVR_TRACE_CONSTANTS", traceEnv, sizeof(traceEnv));
        g_ConstantTraceEnabled = traceEnv[0] == '1' || _stricmp(traceEnv, "true") == 0;
        g_ConstantTraceConfigured = true;
        if (g_ConstantTraceEnabled) {
            BLVR::Log("D3D9 constant trace enabled (first 160 uploads, including sub-matrix calls)");
        }
    }
    if (gameplay && !g_ForceOpaqueConfigured) {
        char opaqueEnv[16] = {};
        GetEnvironmentVariableA("BLVR_FORCE_OPAQUE", opaqueEnv, sizeof(opaqueEnv));
        g_ForceOpaqueEnabled = opaqueEnv[0] == '1' || _stricmp(opaqueEnv, "true") == 0;
        g_ForceOpaqueConfigured = true;
        BLVR::Log("BLVR force opaque arm/body=%d", g_ForceOpaqueEnabled ? 1 : 0);
    }
    if (gameplay && g_ConstantTraceEnabled && data && vector4fCount >= 1 &&
        g_ConstantTraceCount < 160) {
        bool finite = true;
        for (UINT index = 0; index < vector4fCount * 4; ++index) {
            if (!std::isfinite(data[index])) {
                finite = false;
                break;
            }
        }
        if (finite) {
            ++g_ConstantTraceCount;
            BLVR::Log("D3D9 constant trace #%u caller=%p start=%u count=%u v0=(%.3f,%.3f,%.3f,%.3f) v1=(%.3f,%.3f,%.3f,%.3f) v2=(%.3f,%.3f,%.3f,%.3f) v3=(%.3f,%.3f,%.3f,%.3f)",
                      g_ConstantTraceCount, _ReturnAddress(), startRegister, vector4fCount,
                      data[0], data[1], data[2], data[3],
                      vector4fCount > 1 ? data[4] : 0.0f, vector4fCount > 1 ? data[5] : 0.0f,
                      vector4fCount > 1 ? data[6] : 0.0f, vector4fCount > 1 ? data[7] : 0.0f,
                      vector4fCount > 2 ? data[8] : 0.0f, vector4fCount > 2 ? data[9] : 0.0f,
                      vector4fCount > 2 ? data[10] : 0.0f, vector4fCount > 2 ? data[11] : 0.0f,
                      vector4fCount > 3 ? data[12] : 0.0f, vector4fCount > 3 ? data[13] : 0.0f,
                      vector4fCount > 3 ? data[14] : 0.0f, vector4fCount > 3 ? data[15] : 0.0f);
        }
    }
    uintptr_t callerEdi = 0;
    uintptr_t callerEsi = 0;
    uintptr_t callerEbx = 0;
    uintptr_t callerFrame = reinterpret_cast<uintptr_t>(_AddressOfReturnAddress()) + 0x20;
    __asm {
        mov callerEdi, edi
        mov callerEsi, esi
        mov callerEbx, ebx
    }
    if (gameplay && !g_BodyObjectAnchorConfigured) {
        char anchorEnv[16] = {};
        GetEnvironmentVariableA("BLVR_BODY_OBJECT_ANCHOR", anchorEnv, sizeof(anchorEnv));
        g_BodyObjectAnchorEnabled = anchorEnv[0] == '1' || _stricmp(anchorEnv, "true") == 0;
        g_BodyObjectAnchorConfigured = true;
        BLVR::Log("BLVR body object anchor=%d", g_BodyObjectAnchorEnabled ? 1 : 0);
    }
    if (gameplay && !g_BodyWorldOffsetConfigured) {
        char offsetEnv[32] = {};
        GetEnvironmentVariableA("BLVR_BODY_WORLD_X_OFFSET", offsetEnv, sizeof(offsetEnv));
        if (offsetEnv[0]) g_BodyWorldOffset[0] = static_cast<float>(atof(offsetEnv));
        offsetEnv[0] = '\0';
        GetEnvironmentVariableA("BLVR_BODY_WORLD_Y_OFFSET", offsetEnv, sizeof(offsetEnv));
        if (offsetEnv[0]) g_BodyWorldOffset[1] = static_cast<float>(atof(offsetEnv));
        offsetEnv[0] = '\0';
        GetEnvironmentVariableA("BLVR_BODY_WORLD_Z_OFFSET", offsetEnv, sizeof(offsetEnv));
        if (offsetEnv[0]) g_BodyWorldOffset[2] = static_cast<float>(atof(offsetEnv));
        g_BodyWorldOffsetConfigured = true;
        BLVR::Log("BLVR body world offset=(%.3f,%.3f,%.3f)",
                  g_BodyWorldOffset[0], g_BodyWorldOffset[1], g_BodyWorldOffset[2]);
    }
    if (gameplay && data && startRegister == 0 && vector4fCount == 4) {
        bool finite = true;
        for (int index = 0; index < 16; ++index) {
            finite = finite && std::isfinite(data[index]);
        }
        if (finite && fabsf(data[15] - 1.0f) < 0.01f) {
            memcpy(g_LastObjectWorld, data, sizeof(g_LastObjectWorld));
            g_HasLastObjectWorld = true;
            const bool bodyWorldCandidate =
                fabsf(data[12]) < 1.0f && data[13] > -3.25f && data[13] < -1.25f &&
                fabsf(data[14]) < 1.0f;
            if (bodyWorldCandidate &&
                (fabsf(g_BodyWorldOffset[0]) > 0.0001f ||
                 fabsf(g_BodyWorldOffset[1]) > 0.0001f ||
                 fabsf(g_BodyWorldOffset[2]) > 0.0001f)) {
                float correctedWorld[16];
                memcpy(correctedWorld, data, sizeof(correctedWorld));
                correctedWorld[12] += g_BodyWorldOffset[0];
                correctedWorld[13] += g_BodyWorldOffset[1];
                correctedWorld[14] += g_BodyWorldOffset[2];
                memcpy(g_LastObjectWorld, correctedWorld, sizeof(g_LastObjectWorld));
                static uint32_t bodyWorldOffsetCount = 0;
                ++bodyWorldOffsetCount;
                if (bodyWorldOffsetCount <= 3 || (bodyWorldOffsetCount % 1000) == 0) {
                    BLVR::Log("BLVR body world offset applied #%u from=(%.2f,%.2f,%.2f) to=(%.2f,%.2f,%.2f)",
                              bodyWorldOffsetCount, data[12], data[13], data[14],
                              correctedWorld[12], correctedWorld[13], correctedWorld[14]);
                }
                return g_Original_SetVertexShaderConstantF(thisPtr, startRegister, correctedWorld, vector4fCount);
            }
            if (g_BodyObjectAnchorEnabled && g_BodyObjectAnchorPending && !g_BodyObjectAnchorLocked) {
                const float objectX = data[12];
                const float objectY = data[13];
                const float objectZ = data[14];
                const float objectDistance = sqrtf(objectX * objectX + objectY * objectY + objectZ * objectZ);
                if (objectDistance > 20.0f && objectDistance < 70.0f) {
                    const float headX =
                        g_BodyObjectAnchorHead[0] * data[0] +
                        g_BodyObjectAnchorHead[1] * data[4] +
                        g_BodyObjectAnchorHead[2] * data[8] + data[12];
                    const float headY =
                        g_BodyObjectAnchorHead[0] * data[1] +
                        g_BodyObjectAnchorHead[1] * data[5] +
                        g_BodyObjectAnchorHead[2] * data[9] + data[13];
                    const float headZ =
                        g_BodyObjectAnchorHead[0] * data[2] +
                        g_BodyObjectAnchorHead[1] * data[6] +
                        g_BodyObjectAnchorHead[2] * data[10] + data[14];
                    BLVR::CameraHook_SetNativeBodyHeadFromSceneOffset(
                        -headX,
                        -headY,
                        -headZ);
                    g_BodyObjectAnchorLocked = true;
                    g_BodyObjectAnchorPending = false;
                    BLVR::Log("BLVR body object anchor locked on world upload headOffset=(%.2f,%.2f,%.2f) object=(%.2f,%.2f,%.2f)",
                              headX, headY, headZ, objectX, objectY, objectZ);
                }
            }
        }
    }
    if (gameplay && data && startRegister == 12 && vector4fCount == 96 && !g_ArmRigConfigured) {
        char armEnv[16] = {};
        GetEnvironmentVariableA("BLVR_ARM_RIG", armEnv, sizeof(armEnv));
        g_ArmRigEnabled = armEnv[0] == '1' || _stricmp(armEnv, "true") == 0;
        g_ArmRigConfigured = true;
        BLVR::Log("BLVR arm rig=%d", g_ArmRigEnabled ? 1 : 0);
    }
    if (gameplay && !g_ArmBodyYOffsetConfigured) {
        char offsetEnv[32] = {};
        GetEnvironmentVariableA("BLVR_ARM_BODY_Y_OFFSET", offsetEnv, sizeof(offsetEnv));
        if (offsetEnv[0]) g_ArmBodyYOffset = static_cast<float>(atof(offsetEnv));
        g_ArmBodyYOffsetConfigured = true;
        BLVR::Log("BLVR arm/body palette Y offset=%.3f", g_ArmBodyYOffset);
    }
    if (gameplay && !g_ArmBodyXOffsetConfigured) {
        char offsetEnv[32] = {};
        GetEnvironmentVariableA("BLVR_ARM_BODY_X_OFFSET", offsetEnv, sizeof(offsetEnv));
        if (offsetEnv[0]) g_ArmBodyXOffset = static_cast<float>(atof(offsetEnv));
        g_ArmBodyXOffsetConfigured = true;
        BLVR::Log("BLVR arm/body palette X offset=%.3f", g_ArmBodyXOffset);
    }
    if (gameplay && !g_ArmBoneTraceConfigured) {
        char traceEnv[16] = {};
        GetEnvironmentVariableA("BLVR_TRACE_ARM_BONES", traceEnv, sizeof(traceEnv));
        g_ArmBoneTraceEnabled = traceEnv[0] == '1' || _stricmp(traceEnv, "true") == 0;
        g_ArmBoneTraceConfigured = true;
        if (g_ArmBoneTraceEnabled) BLVR::Log("BLVR arm bone trace enabled");
    }
    if (gameplay && !g_ArmHeadXOffsetConfigured) {
        char offsetEnv[32] = {};
        GetEnvironmentVariableA("BLVR_ARM_HEAD_X_OFFSET", offsetEnv, sizeof(offsetEnv));
        if (offsetEnv[0]) g_ArmHeadXOffset = static_cast<float>(atof(offsetEnv));
        g_ArmHeadXOffsetConfigured = true;
        BLVR::Log("BLVR arm/head palette X offset=%.3f", g_ArmHeadXOffset);
    }
    if (gameplay && !g_ArmWorldAttachConfigured) {
        char attachEnv[16] = {};
        GetEnvironmentVariableA("BLVR_ARM_WORLD_ATTACH", attachEnv, sizeof(attachEnv));
        g_ArmWorldAttachEnabled = attachEnv[0] == '1' || _stricmp(attachEnv, "true") == 0;
        g_ArmWorldAttachConfigured = true;
        BLVR::Log("BLVR arm world attach=%d", g_ArmWorldAttachEnabled ? 1 : 0);
    }
    if (gameplay && !g_ForceTwoSidedConfigured) {
        char cullEnv[16] = {};
        GetEnvironmentVariableA("BLVR_FORCE_TWO_SIDED", cullEnv, sizeof(cullEnv));
        g_ForceTwoSidedEnabled = cullEnv[0] == '1' || _stricmp(cullEnv, "true") == 0;
        g_ForceTwoSidedConfigured = true;
        BLVR::Log("BLVR force two-sided=%d", g_ForceTwoSidedEnabled ? 1 : 0);
    }
    if (gameplay && data && startRegister == 12 && vector4fCount == 96 && g_ArmRigEnabled) {
        ProbePaletteOwner(thisPtr, data, reinterpret_cast<uintptr_t>(_ReturnAddress()),
                          callerEdi, callerEsi, callerEbx, callerFrame);
        float armPalette[384];
        memcpy(armPalette, data, sizeof(armPalette));
        if (ApplyTrackedArmRig(armPalette, vector4fCount)) {
            if (g_HeadOnlyArmsEnabled) {
                HideNativeHeadAndTorsoBones(armPalette);
            }
            if (g_ForceTwoSidedEnabled) {
                thisPtr->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
            }
            ApplyOpaqueArmRenderStates(thisPtr);
            if (g_ArmWorldAttachEnabled && g_HasLastObjectWorld) {
                float correction[16];
                float correctedWorld[16];
                if (BLVR::CameraHook_GetViewCorrectionMatrix(correction)) {
                    MultiplyArmMatrix(g_LastObjectWorld, correction, correctedWorld);
                    memcpy(g_LastObjectWorld, correctedWorld, sizeof(g_LastObjectWorld));
                    g_Original_SetVertexShaderConstantF(thisPtr, 0, correctedWorld, 4);
                }
            }
            g_ArmPaletteSequence++;
            g_ArmPaletteDrawsSinceUpload = 0;
            g_ArmPaletteReadyForDraw = true;
            return g_Original_SetVertexShaderConstantF(thisPtr, startRegister, armPalette, vector4fCount);
        }
    }
    if (!g_WorldMatrixOverrideConfigured) {
        char overrideEnv[16] = {};
        GetEnvironmentVariableA("BLVR_WORLD_MATRIX_OVERRIDE", overrideEnv, sizeof(overrideEnv));
        if (overrideEnv[0]) {
            if (overrideEnv[0] == '0' || _stricmp(overrideEnv, "false") == 0) {
                g_WorldMatrixOverrideMode = 0;
            } else if (overrideEnv[0] >= '1' && overrideEnv[0] <= '9') {
                g_WorldMatrixOverrideMode = atoi(overrideEnv);
            }
        }
        g_WorldMatrixOverrideConfigured = true;
        BLVR::Log("D3D9 world matrix override mode=%d", g_WorldMatrixOverrideMode);
    }
    const bool cameraRegisterMode = g_WorldMatrixOverrideMode > 0;
    const bool shouldOverride = g_WorldMatrixOverrideMode != 0 &&
        (!cameraRegisterMode || startRegister == static_cast<UINT>(g_WorldMatrixOverrideMode));
    if (shouldOverride && gameplay && data && vector4fCount == 4 &&
        (cameraRegisterMode || startRegister == 0)) {
        if (cameraRegisterMode && startRegister == static_cast<UINT>(g_WorldMatrixOverrideMode)) {
            float cameraWorld[16];
            if (BLVR::CameraHook_GetWorldMatrix(cameraWorld)) {
                g_VertexMatrixOverrideCount++;
                return g_Original_SetVertexShaderConstantF(thisPtr, startRegister, cameraWorld, vector4fCount);
            }
        }

        if (cameraRegisterMode) {
            return g_Original_SetVertexShaderConstantF(thisPtr, startRegister, data, vector4fCount);
        }

        const float row0Length = data[0] * data[0] + data[1] * data[1] + data[2] * data[2];
        const float row1Length = data[4] * data[4] + data[5] * data[5] + data[6] * data[6];
        const float row2Length = data[8] * data[8] + data[9] * data[9] + data[10] * data[10];
        if (row0Length > 0.01f && row1Length > 0.01f && row2Length > 0.01f &&
            data[15] > 0.8f && data[15] < 1.2f) {
            float correction[16];
            if (BLVR::CameraHook_GetViewCorrectionMatrix(correction)) {
                float corrected[16];
                for (int row = 0; row < 4; ++row) {
                    for (int column = 0; column < 4; ++column) {
                        corrected[row * 4 + column] =
                            data[row * 4 + 0] * correction[0 * 4 + column] +
                            data[row * 4 + 1] * correction[1 * 4 + column] +
                            data[row * 4 + 2] * correction[2 * 4 + column] +
                            data[row * 4 + 3] * correction[3 * 4 + column];
                    }
                }
                g_VertexMatrixOverrideCount++;
                if (g_VertexMatrixOverrideCount <= 3 || (g_VertexMatrixOverrideCount % 10000) == 0) {
                    BLVR::Log("D3D9 world matrix override #%u translation=(%.2f,%.2f,%.2f)",
                              g_VertexMatrixOverrideCount, corrected[12], corrected[13], corrected[14]);
                }
                return g_Original_SetVertexShaderConstantF(thisPtr, startRegister, corrected, vector4fCount);
            }
        }
    }
    return g_Original_SetVertexShaderConstantF(thisPtr, startRegister, data, vector4fCount);
}

static LRESULT CALLBACK CaptureWindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    // Ignore transitions away from the game so VR focus changes do not pause
    // it, but forward activation back into the retail window procedure.  The
    // old unconditional filter swallowed WM_ACTIVATEAPP(TRUE) and
    // WM_ACTIVATE(WA_ACTIVE), leaving gameplay permanently paused after the
    // OpenXR host took focus during startup.
    const bool losingAppActivation = message == WM_ACTIVATEAPP && !wParam;
    const bool losingWindowActivation = message == WM_ACTIVATE && LOWORD(wParam) == WA_INACTIVE;
    if (g_IgnoreFocusPause &&
        (losingAppActivation || losingWindowActivation || message == WM_KILLFOCUS)) {
        return 0;
    }

    return g_OriginalGameWndProc
        ? CallWindowProcA(g_OriginalGameWndProc, hWnd, message, wParam, lParam)
        : DefWindowProcA(hWnd, message, wParam, lParam);
}

static void ConfigureCaptureWindow(HWND hWnd) {
    if (!hWnd) return;

    char windowedEnv[16] = {};
    char noFocusEnv[16] = {};
    GetEnvironmentVariableA("BLVR_WINDOWED", windowedEnv, sizeof(windowedEnv));
    GetEnvironmentVariableA("BLVR_NOFOCUS_PAUSE", noFocusEnv, sizeof(noFocusEnv));

    bool forceWindowed = windowedEnv[0] == '1' || _stricmp(windowedEnv, "true") == 0;
    g_IgnoreFocusPause = noFocusEnv[0] == '1' || _stricmp(noFocusEnv, "true") == 0;
    if (!forceWindowed && !g_IgnoreFocusPause) return;

    if (forceWindowed) {
        LONG_PTR style = GetWindowLongPtrA(hWnd, GWL_STYLE);
        style &= ~static_cast<LONG_PTR>(WS_POPUP);
        style |= WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_THICKFRAME;
        SetWindowLongPtrA(hWnd, GWL_STYLE, style);

        RECT clientRect{0, 0, static_cast<LONG>(blvr_quality::width()), static_cast<LONG>(blvr_quality::height())};
        AdjustWindowRect(&clientRect, static_cast<DWORD>(style), FALSE);
        int width = clientRect.right - clientRect.left;
        int height = clientRect.bottom - clientRect.top;
        SetWindowPos(hWnd, HWND_NOTOPMOST, 40, 40, width, height,
                     SWP_NOACTIVATE | SWP_NOZORDER | SWP_FRAMECHANGED);
    }

    if (g_IgnoreFocusPause && !g_OriginalGameWndProc) {
        g_OriginalGameWndProc = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrA(hWnd, GWLP_WNDPROC,
                              reinterpret_cast<LONG_PTR>(CaptureWindowProc)));
    }

    BLVR::Log("CaptureWindow: windowed=%d ignoreFocusPause=%d hwnd=0x%p",
              forceWindowed ? 1 : 0, g_IgnoreFocusPause ? 1 : 0, hWnd);
}

// GPU Check bypass at RVA 0x26c360
static const uintptr_t RVA_GPU_CHECK = 0x26c360;
typedef bool (__cdecl* FnCheckGpuSpec)();
static FnCheckGpuSpec g_Original_CheckGpuSpec = nullptr;

static bool __cdecl Hook_CheckGpuSpec() {
    BLVR::Log("Hook_CheckGpuSpec: Shader model 3.0 capability check bypassed (reporting GPU compatible).");
    return true;
}

// Controller Poll hook at RVA 0x7788a0
static const uintptr_t RVA_POLL_CONTROLLER = 0x7788a0;
typedef void (__cdecl* FnPollController)(void* pObj);
static FnPollController g_Original_PollController = nullptr;

static bool XrControllerConnected(const BLVR::ControllerState& controllers) {
    return controllers.runtimeActive &&
           (controllers.left.tracked || controllers.right.tracked);
}

// Keep the retail input manager polling while XR owns the controls.
static const uintptr_t RVA_FRONTEND_INPUT_UPDATE = 0x8c750;
typedef void (__thiscall* FnFrontEndInputUpdate)(void* pFrontEnd, float deltaSeconds);
static FnFrontEndInputUpdate g_Original_FrontEndInputUpdate = nullptr;
static void* g_TargetFrontEndInputUpdate = nullptr;
static void* g_CurrentFrontEndInstance = nullptr;
static bool TryArmTitleEventOnly(uintptr_t exeBase);

static void __fastcall Hook_FrontEndInputUpdate(
    void* pFrontEnd, void*, float deltaSeconds) {
    g_CurrentFrontEndInstance = pFrontEnd;
    // VA 0x48c7d7 gates both native input-manager updates on FE+0x29.
    // A live XR input session must process input and simulation when the
    // desktop window is inactive too. No OS focus change is involved.
    uint8_t* pollEnabled = pFrontEnd ? reinterpret_cast<uint8_t*>(pFrontEnd) + 0x29 : nullptr;
    static bool xrOwnsPoll = false;
    static uint8_t savedDesktopPoll = 0;
    BLVR::ControllerState xrInput{};
    const bool xrInputActive = BLVR::XrHost::Get().HasRealXr() &&
        BLVR::XrHost::Get().GetControllerState(xrInput) && xrInput.runtimeActive;
    if (pollEnabled && xrInputActive) {
        if (!xrOwnsPoll) savedDesktopPoll = *pollEnabled;
        xrOwnsPoll = true;
        *pollEnabled = 1;
    } else if (pollEnabled && xrOwnsPoll) {
        *pollEnabled = savedDesktopPoll;
        xrOwnsPoll = false;
    }
    if (g_Original_FrontEndInputUpdate) {
        g_Original_FrontEndInputUpdate(pFrontEnd, deltaSeconds);
    }
}

static const uintptr_t RVA_FRONTEND_ACCEPT_EVENT = 0x81e0;
typedef void (__thiscall* FnFrontEndAcceptEvent)(void* pControllerState);
static FnFrontEndAcceptEvent g_Original_FrontEndAcceptEvent = nullptr;
static void* g_TargetFrontEndAcceptEvent = nullptr;
static volatile LONG g_FrontEndAcceptEventCount = 0;
static volatile LONG g_FrontEndAcceptProbe = 0;
static const uintptr_t RVA_FRONTEND_ACTION_GATE = 0x373600;
typedef bool (__cdecl* FnFrontEndActionGate)();
static FnFrontEndActionGate g_Original_FrontEndActionGate = nullptr;
static void* g_TargetFrontEndActionGate = nullptr;
static void LogFrontEndActionGateDetails(uintptr_t exeBase, LONG probe,
                                         bool allowed);

static bool __cdecl Hook_FrontEndActionGate() {
    const bool allowed = g_Original_FrontEndActionGate &&
                         g_Original_FrontEndActionGate();
    if (InterlockedCompareExchange(&g_FrontEndAcceptProbe, 0, 0) != 0) {
        static LONG s_ProbeCount = 0;
        const LONG probe = InterlockedIncrement(&s_ProbeCount);
        if (probe <= 16) {
            LogFrontEndActionGateDetails(
                reinterpret_cast<uintptr_t>(GetModuleHandleA(NULL)),
                probe, allowed);
        }
    }
    return allowed;
}

struct FrontEndAcceptSnapshot {
    bool ready = false;
    uintptr_t root = 0;
    uintptr_t registry = 0;
    uintptr_t context = 0;
    uintptr_t manager = 0;
    uintptr_t game = 0;
    uint32_t registryMode = 0;
    int32_t registryIndex = -1;
    uint32_t managerState = UINT32_MAX;
    uint32_t previousState = UINT32_MAX;
    uint32_t managerBusy = UINT32_MAX;
    uint8_t managerFlag937 = 0xff;
    uint8_t managerFlag938 = 0xff;
    uint32_t gameFlag2bc = UINT32_MAX;
    uint32_t gameFlag2c0 = UINT32_MAX;
    uint32_t frontEndLock = UINT32_MAX;
    int32_t activePlayer = -2;
};

static bool TryReadRetailU32(uintptr_t address, uint32_t& value) {
    if (!address || IsBadReadPtr(reinterpret_cast<const void*>(address), sizeof(value))) {
        return false;
    }
    value = *reinterpret_cast<const uint32_t*>(address);
    return true;
}

static bool TryReadRetailU8(uintptr_t address, uint8_t& value) {
    if (!address || IsBadReadPtr(reinterpret_cast<const void*>(address), sizeof(value))) {
        return false;
    }
    value = *reinterpret_cast<const uint8_t*>(address);
    return true;
}

// Read the current retail engine context using the same registry walk as
// 0x47e610/0x47e650. This is diagnostic only: it never changes engine state.
static uintptr_t ResolveCurrentRetailEngineContext(
    uintptr_t root, uint32_t& registryMode, uintptr_t& registryOut,
    int32_t& registryIndexOut) {
    uint32_t registry = 0;
    uint32_t indexBits = 0;
    uint32_t countBits = 0;
    uint32_t entries = 0;
    if (!TryReadRetailU32(root + 0x44, registry) || !registry ||
        !TryReadRetailU32(root + 0x48, registryMode) ||
        !TryReadRetailU32(registry + 0x38, indexBits) ||
        !TryReadRetailU32(registry + 0x20, countBits) ||
        !TryReadRetailU32(registry + 0x28, entries)) {
        return 0;
    }

    registryOut = registry;
    const int32_t currentIndex = static_cast<int32_t>(indexBits);
    registryIndexOut = currentIndex;
    if (!entries || currentIndex < 0) return 0;
    const uint32_t count = countBits >> 6;

    for (int32_t i = currentIndex; i >= 0; --i) {
        if (static_cast<uint32_t>(i) >= count) continue;
        uint32_t candidate = 0;
        if (!TryReadRetailU32(static_cast<uintptr_t>(entries) +
                                  static_cast<uintptr_t>(i) * sizeof(uint32_t),
                              candidate) || !candidate) {
            continue;
        }

        uint32_t object = 0;
        if (!TryReadRetailU32(static_cast<uintptr_t>(candidate) + 8, object) || !object) {
            continue;
        }

        if (registryMode == 3) {
            uint32_t typedObject = 0;
            uint32_t objectType = 0;
            if (!TryReadRetailU32(static_cast<uintptr_t>(object) + 0x10, typedObject) ||
                !typedObject ||
                !TryReadRetailU32(static_cast<uintptr_t>(typedObject) + 8, typedObject) ||
                !typedObject ||
                !TryReadRetailU32(static_cast<uintptr_t>(typedObject) + 8, objectType) ||
                objectType != 5) {
                continue;
            }
        } else {
            uint32_t objectLevel = 0;
            if (!TryReadRetailU32(static_cast<uintptr_t>(object) + 0x38, objectLevel) ||
                objectLevel < 5) {
                continue;
            }
        }
        return candidate;
    }
    return 0;
}

static FrontEndAcceptSnapshot CaptureFrontEndAcceptSnapshot(uintptr_t exeBase) {
    FrontEndAcceptSnapshot snapshot{};
    uint32_t root = 0;
    if (TryReadRetailU32(exeBase + 0xc09cb0, root) && root) {
        snapshot.root = root;
        snapshot.context = ResolveCurrentRetailEngineContext(
            root, snapshot.registryMode, snapshot.registry, snapshot.registryIndex);
        uint32_t manager = 0;
        if (snapshot.context &&
            TryReadRetailU32(snapshot.context + 0x14, manager)) {
            snapshot.manager = manager;
            snapshot.ready = manager != 0;
        }
    }

    uint32_t game = 0;
    if (TryReadRetailU32(exeBase + 0xc09cb4, game) && game) {
        snapshot.game = game;
        TryReadRetailU32(static_cast<uintptr_t>(game) + 0x2bc,
                         snapshot.gameFlag2bc);
        TryReadRetailU32(static_cast<uintptr_t>(game) + 0x2c0,
                         snapshot.gameFlag2c0);
    }
    uint32_t activePlayer = 0;
    if (TryReadRetailU32(exeBase + 0xc12130, activePlayer)) {
        snapshot.activePlayer = static_cast<int32_t>(activePlayer);
    }
    if (snapshot.manager) {
        TryReadRetailU32(snapshot.manager + 0x8e4, snapshot.managerState);
        TryReadRetailU32(snapshot.manager + 0x8e8, snapshot.previousState);
        TryReadRetailU32(snapshot.manager + 0x910, snapshot.managerBusy);
        TryReadRetailU8(snapshot.manager + 0x937, snapshot.managerFlag937);
        TryReadRetailU8(snapshot.manager + 0x938, snapshot.managerFlag938);
    }
    if (g_CurrentFrontEndInstance) {
        TryReadRetailU32(reinterpret_cast<uintptr_t>(g_CurrentFrontEndInstance) + 0x128,
                         snapshot.frontEndLock);
    }
    return snapshot;
}

// VA 0x40c3b0 is a retail state-reset callback. Its single cdecl argument is
// an 8-byte-entry container; an empty container makes it clear the front-end
// manager's stale state and lock through the game's own transition path.
static bool TryArmTitleEventOnly(uintptr_t exeBase) {
    if (!TitleEventOnlyEnabled() || BLVR::CameraHook_IsActive()) {
        return TitleEventOnlyArmed();
    }

    const FrontEndAcceptSnapshot before = CaptureFrontEndAcceptSnapshot(exeBase);
    if (!before.ready || before.managerState != 2 || before.managerBusy != 0 ||
        before.frontEndLock != 0) {
        return TitleEventOnlyArmed();
    }

    static uint32_t s_LastAttemptPresent = UINT32_MAX;
    static uint32_t s_Attempts = 0;
    if (s_LastAttemptPresent == g_PresentCount) {
        return TitleEventOnlyArmed();
    }
    s_LastAttemptPresent = g_PresentCount;
    ++s_Attempts;

    uint32_t emptyEvents[4] = {};
    bool callCompleted = false;
    __try {
        typedef void (__cdecl* FnResetFrontEndState)(void* emptyContainer);
        reinterpret_cast<FnResetFrontEndState>(exeBase + 0xc3b0)(emptyEvents);
        callCompleted = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        BLVR::Log("FrontEnd native reset callback raised 0x%08X",
                  GetExceptionCode());
    }

    const FrontEndAcceptSnapshot after = CaptureFrontEndAcceptSnapshot(exeBase);
    if (s_Attempts <= 10 || (s_Attempts % 300) == 0) {
        BLVR::Log("FrontEnd native reset attempt=%u completed=%d before=%u/%u/%u after=%u/%u/%u manager=%p",
                  s_Attempts, callCompleted ? 1 : 0, before.managerState,
                  before.managerBusy, before.frontEndLock, after.managerState,
                  after.managerBusy, after.frontEndLock,
                  reinterpret_cast<void*>(after.manager));
    }
    if (callCompleted && after.ready && after.managerState == 0 &&
        after.managerBusy == 0 && after.frontEndLock == 0) {
        if (!TitleEventOnlyArmed()) {
            InterlockedExchange(&g_TitleEventOnlyArmed, 1);
            BLVR::Log("FrontEnd XR title gate armed after native registry initialization (registry=%p context=%p player=%d)",
                      reinterpret_cast<void*>(after.registry),
                      reinterpret_cast<void*>(after.context), after.activePlayer);
        }
        return true;
    }
    return false;
}

// Mirror only the lookup inputs used by VA 0x773600. Both retail lookups are
// read-only; reporting their results identifies why the native accept path
// declines an otherwise valid XR edge.
static uintptr_t CallRetailMenuItemLookup(
    uintptr_t functionAddress, uintptr_t menuArray, uint32_t selector) {
    uintptr_t item = 0;
    __try {
        __asm {
            push edi
            mov eax, menuArray
            mov edi, selector
            mov ecx, functionAddress
            call ecx
            mov item, eax
            pop edi
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        BLVR::Log("FrontEnd menu item lookup raised 0x%08X",
                  GetExceptionCode());
    }
    return item;
}

static void LogFrontEndActionGateDetails(
    uintptr_t exeBase, LONG probe, bool allowed) {
    const FrontEndAcceptSnapshot snapshot = CaptureFrontEndAcceptSnapshot(exeBase);
    uintptr_t menuArray = 0;
    uint32_t selector = UINT32_MAX;
    uint32_t menuPackedCount = 0;
    uintptr_t menuEntries = 0;
    uint32_t menuEntriesAddress = 0;
    uintptr_t selectedItem = 0;
    uint32_t itemFlags350 = UINT32_MAX;
    uint32_t itemFlags354 = UINT32_MAX;
    uint8_t itemFlag360 = 0xff;

    TryReadRetailU32(exeBase + 0xc182c4, selector);
    if (snapshot.manager) {
        __try {
            typedef void* (__stdcall* FnGetFrontEndMenuArray)(void* manager);
            menuArray = reinterpret_cast<uintptr_t>(
                reinterpret_cast<FnGetFrontEndMenuArray>(exeBase + 0xa7700)(
                    reinterpret_cast<void*>(snapshot.manager)));
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            BLVR::Log("FrontEnd menu array lookup raised 0x%08X",
                      GetExceptionCode());
        }
    }
    if (menuArray && !IsBadReadPtr(reinterpret_cast<void*>(menuArray), 0x48)) {
        TryReadRetailU32(menuArray + 0x3c, menuPackedCount);
        TryReadRetailU32(menuArray + 0x44, menuEntriesAddress);
        menuEntries = menuEntriesAddress;
        if (selector != UINT32_MAX && menuEntries &&
            (menuPackedCount >> 6) != 0) {
            selectedItem = CallRetailMenuItemLookup(
                exeBase + 0xb9ef0, menuArray, selector);
        }
    }
    if (selectedItem &&
        !IsBadReadPtr(reinterpret_cast<void*>(selectedItem + 0x360), 1)) {
        TryReadRetailU32(selectedItem + 0x350, itemFlags350);
        TryReadRetailU32(selectedItem + 0x354, itemFlags354);
        TryReadRetailU8(selectedItem + 0x360, itemFlag360);
    }

    BLVR::Log("FrontEnd accept interaction gate #%ld -> %d state=%u manager=%p menu=%p packedCount=%u items=%p selector=%u item=%p itemFlags=%08X/%08X itemFlag360=%u",
              probe, allowed ? 1 : 0, snapshot.managerState,
              reinterpret_cast<void*>(snapshot.manager),
              reinterpret_cast<void*>(menuArray), menuPackedCount,
              reinterpret_cast<void*>(menuEntries), selector,
              reinterpret_cast<void*>(selectedItem), itemFlags350,
              itemFlags354, itemFlag360);
}

static void LogFrontEndAcceptSnapshot(
    const char* phase, LONG count, const void* controllerState,
    uint8_t eventByte, uint8_t connectedByte,
    const FrontEndAcceptSnapshot& snapshot) {
    BLVR::Log("FrontEnd accept %s #%ld state=%p event=%u connected=%u ready=%d root=%p registry=%p mode=%u index=%d context=%p manager=%p stateId=%u previous=%u busy=%u flags=%u/%u game=%p gameFlags=%u/%u FE=%p FElock=%u player=%d",
        phase, count, controllerState, eventByte, connectedByte,
        snapshot.ready ? 1 : 0, reinterpret_cast<void*>(snapshot.root),
        reinterpret_cast<void*>(snapshot.registry), snapshot.registryMode,
        snapshot.registryIndex, reinterpret_cast<void*>(snapshot.context),
        reinterpret_cast<void*>(snapshot.manager), snapshot.managerState,
        snapshot.previousState, snapshot.managerBusy, snapshot.managerFlag937,
        snapshot.managerFlag938, reinterpret_cast<void*>(snapshot.game),
        snapshot.gameFlag2bc, snapshot.gameFlag2c0, g_CurrentFrontEndInstance,
        snapshot.frontEndLock, snapshot.activePlayer);
}

// Recover the last known-working retail title handoff only after the native
// XR Menu edge has selected a player and the manager is in its ready state.
// This keeps the bootstrap from advancing an unjoined or already-busy menu.
static void SafeCallFrontEndAdvance(uintptr_t exeBase, const char* source) {
    static uint32_t s_LastPresent = UINT32_MAX;
    static uint32_t s_AdvanceCalls = 0;
    const uint32_t present = g_PresentCount;
    if (s_AdvanceCalls >= 12 || present < 90 || (present % 60) != 0 ||
        present == s_LastPresent) {
        return;
    }
    s_LastPresent = present;

    const FrontEndAcceptSnapshot snapshot = CaptureFrontEndAcceptSnapshot(exeBase);
    if (!snapshot.ready || snapshot.managerState == UINT32_MAX ||
        snapshot.managerBusy != 0 || snapshot.frontEndLock != 0 ||
        snapshot.activePlayer < 0) {
        static LONG s_NotReadySkips = 0;
        const LONG skipped = InterlockedIncrement(&s_NotReadySkips);
        if (skipped <= 4 || (skipped % 120) == 0) {
            BLVR::Log("%s: deferred FrontEnd transition; managerReady=%d state=%u busy=%u FElock=%u player=%d",
                      source, snapshot.ready ? 1 : 0, snapshot.managerState,
                      snapshot.managerBusy, snapshot.frontEndLock,
                      snapshot.activePlayer);
        }
        return;
    }

    uint8_t** ppFE = reinterpret_cast<uint8_t**>(exeBase + 0xc09ad0);
    if (!ppFE || IsBadReadPtr(ppFE, sizeof(void*))) return;
    void* pFE = *ppFE;
    if (!pFE || IsBadReadPtr(pFE, 0x200)) return;

    ++s_AdvanceCalls;
    BLVR::Log("%s: retail FrontEnd update #%u at Present #%u (pFE=0x%p ready=%d player=%d state=%u)",
              source, s_AdvanceCalls, present, pFE, snapshot.ready ? 1 : 0,
              snapshot.activePlayer, snapshot.managerState);
    __try {
        typedef void (__thiscall* FnFrontEndAdvance)(void* pThis);
        reinterpret_cast<FnFrontEndAdvance>(exeBase + 0x6ee0)(pFE);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        BLVR::Log("%s: retail FrontEnd update raised 0x%08X", source,
                  GetExceptionCode());
    }
}

static void __fastcall Hook_FrontEndAcceptEvent(void* pControllerState, void*) {
    const LONG count = InterlockedIncrement(&g_FrontEndAcceptEventCount);
    uint8_t eventByte = 0;
    uint8_t connectedByte = 0;
    if (pControllerState && !IsBadReadPtr(pControllerState, 0x11f)) {
        const uint8_t* state = reinterpret_cast<const uint8_t*>(pControllerState);
        eventByte = state[0x11d];
        connectedByte = state[0x11e];
    }
    const uintptr_t exeBase = reinterpret_cast<uintptr_t>(GetModuleHandleA(NULL));
    const FrontEndAcceptSnapshot beforeReset = CaptureFrontEndAcceptSnapshot(exeBase);
    if (TitleEventOnlyArmed() && !BLVR::CameraHook_IsActive() && connectedByte &&
        beforeReset.ready && beforeReset.managerState == 2 &&
        beforeReset.managerBusy == 0 && beforeReset.frontEndLock == 0) {
        uint32_t emptyEvents[4] = {};
        bool resetCompleted = false;
        __try {
            typedef void (__cdecl* FnResetFrontEndState)(void* emptyContainer);
            reinterpret_cast<FnResetFrontEndState>(exeBase + 0xc3b0)(emptyEvents);
            resetCompleted = true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            BLVR::Log("FrontEnd accept-edge native reset raised 0x%08X",
                      GetExceptionCode());
        }
        const FrontEndAcceptSnapshot afterReset =
            CaptureFrontEndAcceptSnapshot(exeBase);
        BLVR::Log("FrontEnd accept-edge reset completed=%d manager state=%u->%u connected=%u",
                  resetCompleted ? 1 : 0, beforeReset.managerState,
                  afterReset.managerState, connectedByte);
    }
    const FrontEndAcceptSnapshot before = CaptureFrontEndAcceptSnapshot(exeBase);
    if (count <= 12) {
        LogFrontEndAcceptSnapshot("entry", count, pControllerState,
                                  eventByte, connectedByte, before);
    }
    if (g_Original_FrontEndAcceptEvent) {
        InterlockedExchange(&g_FrontEndAcceptProbe, 1);
        g_Original_FrontEndAcceptEvent(pControllerState);
        InterlockedExchange(&g_FrontEndAcceptProbe, 0);
    }
    if (count <= 12) {
        const FrontEndAcceptSnapshot after = CaptureFrontEndAcceptSnapshot(exeBase);
        LogFrontEndAcceptSnapshot("exit", count, pControllerState,
                                  eventByte, connectedByte, after);
        BLVR::Log("FrontEnd accept handler #%ld gameplay=%d",
                  count, BLVR::CameraHook_IsActive() ? 1 : 0);
    }
}

static void __cdecl Hook_PollController(void* pObj) {
    if (!BLVR::XrHost::Get().IsSimMode()) {
        if (g_Original_PollController) {
            g_Original_PollController(pObj);
        }
        return;
    }

    uint8_t* p = reinterpret_cast<uint8_t*>(pObj);
    if (p) {
        // Clear disconnected flag for simulator mode
        p[0x30] = 0;
        p[0x31] = 0;
        uint8_t** ppDev = reinterpret_cast<uint8_t**>(p + 0x28);
        if (ppDev) {
            if (!*ppDev) {
                static uint8_t s_VirtualDev[4096] = {0};
                s_VirtualDev[0xc4e] = 1; // Force isXInput = 1
                s_VirtualDev[0xc4c] = 0; // Not disconnected
                s_VirtualDev[0xc4d] = 0; // Connected
                *ppDev = s_VirtualDev;
            } else {
                uint8_t* pDev = *ppDev;
                pDev[0xc4e] = 1; // Force isXInput = 1
                pDev[0xc4c] = 0; // Not disconnected
                pDev[0xc4d] = 0; // Connected
            }
        }
    }
    if (g_Original_PollController) {
        g_Original_PollController(pObj);
    }

    if (p) {
        uintptr_t exeBase = reinterpret_cast<uintptr_t>(GetModuleHandleA(NULL));
        if (AutomationEnabled() && !BLVR::CameraHook_IsActive()) {
            bool isStart = false;
            if (RetailAutomationShouldPress(isStart)) {
                p[0x134 + 0x176] = isStart ? 1 : 0; // BUTTON_Start
                p[0x134 + 0x172] = isStart ? 0 : 1; // BUTTON_A
            } else {
                p[0x134 + 0x176] = 0;
                p[0x134 + 0x172] = 0;
            }
        } else if (AutomationEnabled() && BLVR::CameraHook_IsActive()) {
            // In an automated simulator run: zero native DInput state so the
            // explicit XR IPC sequence is the only gameplay input source.
            *reinterpret_cast<int16_t*>(p + 0x134 + 0x166) = 0;
            *reinterpret_cast<int16_t*>(p + 0x134 + 0x168) = 0;
            p[0x134 + 0x172] = 0;
            p[0x134 + 0x173] = 0;
            p[0x134 + 0x174] = 0;
            p[0x134 + 0x175] = 0;
            p[0x134 + 0x178] = 0;
        }
    }
}

// Hook KeyboardDevice::Poll at RVA 0x2826d0
static const uintptr_t RVA_KEYBOARD_POLL = 0x2826d0;
typedef void (__fastcall* FnKeyboardPoll)(void* thisPtr, void* dummyEdx);
static FnKeyboardPoll g_Original_KeyboardPoll = nullptr;
static uint32_t g_KbdPollCount = 0;
static void* g_AutomationKeyboardDevice = nullptr;
static bool g_KeyboardDeviceScanComplete = false;
static uint32_t g_KeyboardDeviceScanCount = 0;

static void __fastcall Hook_KeyboardPoll(void* thisPtr, void* dummyEdx) {
    if (!g_AutomationKeyboardDevice) {
        g_AutomationKeyboardDevice = thisPtr;
        BLVR::Log("XR input captured retail keyboard device=%p", thisPtr);
    }
    ++g_KbdPollCount;
    if (g_Original_KeyboardPoll) {
        g_Original_KeyboardPoll(thisPtr, dummyEdx);
    }
    // The native pad path already creates menu edges from XR actions.
    // Do not deliver the same button a second time as a keyboard event.
    if (BLVR::XrHost::Get().HasRealXr()) return;

    BLVR::XrHost& host = BLVR::XrHost::Get();
    const bool simMode = host.IsSimMode();
    const bool realXr = host.HasRealXr();
    if (!simMode && !realXr) {
        return;
    }

    if (!AutomationEnabled()) {
        if (!realXr) return;

        BLVR::ControllerState controllers{};
        if (!host.GetControllerState(controllers) || !controllers.runtimeActive) return;
        if (BLVR::CameraHook_IsActive()) return;

        uint8_t* pKeys = reinterpret_cast<uint8_t*>(thisPtr) + 0x20;
        if (!pKeys || IsBadWritePtr(pKeys, 256)) return;
        const uint8_t xrKeys[] = {0x1c, 0x39, 0x9c, 0x01, 0xc8, 0xd0, 0xcb, 0xcd};
        for (const uint8_t key : xrKeys) pKeys[key] = 0;

        // Keep title and loading input available even after Buddha allocates
        // player 0. The native front end still consumes keyboard events until
        // its gameplay camera exists; then the real XR controller path owns it.
        const bool accept = controllers.right.a;
        const bool start = controllers.left.menu || controllers.right.menu;
        const bool back = controllers.right.b;
        if (accept || start) {
            const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(NULL));
            int32_t* player = reinterpret_cast<int32_t*>(base + 0xc12130);
            const uint32_t count = *reinterpret_cast<const uint32_t*>(base + 0xc11a80) >> 6;
            const void* registry = *reinterpret_cast<void**>(base + 0xc11a88);
            if (*player == -1 && count > 0 && registry && !IsBadReadPtr(registry, 0x494)) {
                *player = 0;
                BLVR::Log("XR title input joined existing retail player 0");
            }
        }
        if (accept) pKeys[0x1c] = 0x80;                 // DIK_RETURN
        if (start) {
            pKeys[0x1c] = 0x80;                         // DIK_RETURN
            pKeys[0x39] = 0x80;                         // DIK_SPACE
            pKeys[0x9c] = 0x80;                         // DIK_NUMPADENTER
        }
        if (back) pKeys[0x01] = 0x80;                   // DIK_ESCAPE
        if (controllers.left.stickY > 0.45f) pKeys[0xc8] = 0x80; // DIK_UP
        if (controllers.left.stickY < -0.45f) pKeys[0xd0] = 0x80; // DIK_DOWN
        if (controllers.left.stickX < -0.45f) pKeys[0xcb] = 0x80; // DIK_LEFT
        if (controllers.left.stickX > 0.45f) pKeys[0xcd] = 0x80; // DIK_RIGHT

        ++g_KbdPollCount;
        const uint32_t signature = (accept ? 1u : 0u) | (start ? 2u : 0u) |
            (back ? 4u : 0u) |
            (controllers.left.stickX < -0.45f ? 8u : 0u) |
            (controllers.left.stickX > 0.45f ? 16u : 0u) |
            (controllers.left.stickY < -0.45f ? 32u : 0u) |
            (controllers.left.stickY > 0.45f ? 64u : 0u);
        static uint32_t s_LastTitleKeyboardSignature = UINT32_MAX;
        if (signature != s_LastTitleKeyboardSignature) {
            s_LastTitleKeyboardSignature = signature;
            BLVR::Log("XR front-end keyboard route poll=%u A=%d Menu=%d B=%d stick=(%.2f,%.2f)",
                g_KbdPollCount, accept ? 1 : 0, start ? 1 : 0,
                back ? 1 : 0, controllers.left.stickX, controllers.left.stickY);
        }
        return;
    }

    g_KbdPollCount++;
    if (!g_AutomationKeyboardDevice) {
        g_AutomationKeyboardDevice = thisPtr;
        g_KeyboardDeviceScanComplete = true;
        uintptr_t vtable = 0;
        __try {
            vtable = *reinterpret_cast<uintptr_t*>(thisPtr);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            vtable = 0;
        }
        BLVR::Log("Automation: captured live KeyboardDevice object=%p vtable=%p poll=%u",
                  thisPtr, reinterpret_cast<void*>(vtable), g_KbdPollCount);
    }
    uint8_t* pKeys = reinterpret_cast<uint8_t*>(thisPtr) + 0x20;
    if (!pKeys || IsBadWritePtr(pKeys, 256)) return;

    if (!BLVR::CameraHook_IsActive()) {
        // Menu / Title Screen phase: write the keyboard device state directly
        // so bridge capture can advance without foreground-window input.
        // The retail title can remain on the Bink/front-end quad for much
        // longer than the first few hundred Presents.  Keep the edge pulsing
        // until CameraHook_IsActive() changes state; this writes the game's
        // own DirectInput device buffer and never touches desktop focus or
        // OS keyboard APIs.
        bool down = ((g_PresentCount % 20) < 10);
        if (down) {
            pKeys[0x1C] = 0x80; // DIK_RETURN
            pKeys[0x39] = 0x80; // DIK_SPACE
            pKeys[0x9C] = 0x80; // DIK_NUMPADENTER
        }
        if (g_KbdPollCount % 120 == 1) {
            BLVR::Log("Hook_KeyboardPoll [Menu #%u]: thisPtr=0x%p Present=%u down=%d",
                      g_KbdPollCount, thisPtr, g_PresentCount, down ? 1 : 0);
        }
    } else {
        // 3D Gameplay phase in automated simulator mode: calm, stationary showcase
        pKeys[0x11] = 0; // W
        pKeys[0x1E] = 0; // A
        pKeys[0x20] = 0; // D
        pKeys[0x1F] = 0; // S
        pKeys[0x2C] = 0; // Z
        pKeys[0x10] = 0; // Q
        pKeys[0x2E] = 0; // C
        pKeys[0x39] = 0; // Space
        pKeys[0x12] = 0; // E
    }
}

// Some retail front-end launches never dispatch KeyboardDevice::Poll after
// the Bink/title object is created (the controller path is still present, but
// the input manager stops issuing its virtual call).  Recover the already
// constructed game-owned device by its retail vtable and drive the exact same
// poll method from Present while automation is active.  This is deliberately
// bounded to the game's process memory and never uses desktop focus or OS
// keyboard injection.
static void FindAutomationKeyboardDevice(uintptr_t exeBase) {
    g_KeyboardDeviceScanCount++;
    const uintptr_t keyboardVtable = exeBase + 0xAAD3FC;
    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    uintptr_t address = reinterpret_cast<uintptr_t>(systemInfo.lpMinimumApplicationAddress);
    const uintptr_t maximum = reinterpret_cast<uintptr_t>(systemInfo.lpMaximumApplicationAddress);
    while (address < maximum) {
        MEMORY_BASIC_INFORMATION region{};
        if (VirtualQuery(reinterpret_cast<const void*>(address), &region, sizeof(region)) == 0) break;
        const uintptr_t base = reinterpret_cast<uintptr_t>(region.BaseAddress);
        const uintptr_t end = base + region.RegionSize;
        if (region.State == MEM_COMMIT &&
            (region.Protect & (PAGE_READWRITE | PAGE_WRITECOPY |
                               PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0) {
            const uintptr_t scanStart = (base + 3u) & ~static_cast<uintptr_t>(3u);
            for (uintptr_t candidate = scanStart; candidate + sizeof(uintptr_t) <= end; candidate += 4u) {
                __try {
                    if (*reinterpret_cast<const uintptr_t*>(candidate) == keyboardVtable) {
                        g_AutomationKeyboardDevice = reinterpret_cast<void*>(candidate);
                        g_KeyboardDeviceScanComplete = true;
                        BLVR::Log("Automation: found retail KeyboardDevice object=%p vtable=%p scan=%u",
                            g_AutomationKeyboardDevice,
                            reinterpret_cast<void*>(keyboardVtable),
                            g_KeyboardDeviceScanCount);
                        return;
                    }
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                    // The protection query is authoritative for normal pages;
                    // continue if a page changes while the game allocates.
                }
            }
        }
        if (end <= address) break;
        address = end;
    }
    // Some title states never dispatch the keyboard device's virtual poll.
    // Drive the already-constructed retail object in-process so the normal
    // Hook_KeyboardPoll path can publish the same Start/A edge without any
    // desktop focus or OS-level input injection.
    g_KeyboardDeviceScanComplete = true;
    if (g_KeyboardDeviceScanCount <= 3 || (g_KeyboardDeviceScanCount % 30u) == 0u) {
        BLVR::Log("Automation: retail KeyboardDevice object not found on scan %u", g_KeyboardDeviceScanCount);
    }
}

static void DriveAutomationKeyboardDevice(uintptr_t exeBase) {
    if (BLVR::CameraHook_IsActive() || BLVR::XrHost::Get().HasRealXr()) return;
    if (!AutomationEnabled() && !BLVR::XrHost::Get().HasRealXr()) return;
    // Only replay a device pointer observed through the real retail call.
    // A raw vtable scan can find another object with the same class table but
    // without the input manager's initialized state; never call that object.
    if (!g_AutomationKeyboardDevice || g_KbdPollCount == 0) return;
    if (IsBadReadPtr(g_AutomationKeyboardDevice, sizeof(uintptr_t)) ||
        *reinterpret_cast<uintptr_t*>(g_AutomationKeyboardDevice) != exeBase + 0xAAD3FC) return;

    // Calling the patched target enters Hook_KeyboardPoll, which first runs
    // the retail implementation and then writes the title edge into the
    // device's own DirectInput buffer.
    FnKeyboardPoll poll = reinterpret_cast<FnKeyboardPoll>(exeBase + RVA_KEYBOARD_POLL);
    poll(g_AutomationKeyboardDevice, nullptr);
}

static DWORD WINAPI Hook_XInputGetCapabilities(DWORD dwUserIndex, DWORD dwFlags, void* pCaps);

struct BlvrGamepad {
    WORD wButtons;
    BYTE bLeftTrigger;
    BYTE bRightTrigger;
    SHORT sThumbLX;
    SHORT sThumbLY;
    SHORT sThumbRX;
    SHORT sThumbRY;
};
struct BlvrState {
    DWORD dwPacketNumber;
    BlvrGamepad Gamepad;
};

static BYTE ToTrigger(float value) {
    return static_cast<BYTE>(std::clamp(value, 0.0f, 1.0f) * 255.0f);
}

static void ApplyXrControllerState(BlvrState& state, const BLVR::ControllerState& controllers) {
    state.Gamepad.bLeftTrigger = ToTrigger(controllers.left.trigger);
    state.Gamepad.bRightTrigger = ToTrigger(controllers.right.trigger);
    state.Gamepad.sThumbLX = static_cast<SHORT>(std::clamp(controllers.left.stickX, -1.0f, 1.0f) * 32767.0f);
    state.Gamepad.sThumbLY = static_cast<SHORT>(std::clamp(controllers.left.stickY, -1.0f, 1.0f) * 32767.0f);
    state.Gamepad.sThumbRX = static_cast<SHORT>(std::clamp(controllers.right.stickX, -1.0f, 1.0f) * 32767.0f);
    state.Gamepad.sThumbRY = static_cast<SHORT>(std::clamp(controllers.right.stickY, -1.0f, 1.0f) * 32767.0f);
    if (controllers.right.a) state.Gamepad.wButtons |= 0x1000; // A
    if (controllers.right.b) state.Gamepad.wButtons |= 0x2000; // B
    if (controllers.left.x) state.Gamepad.wButtons |= 0x4000;  // X
    if (controllers.left.y) state.Gamepad.wButtons |= 0x8000;  // Y
    if (controllers.left.squeeze > 0.5f) state.Gamepad.wButtons |= 0x0100;  // LB
    if (controllers.right.squeeze > 0.5f) state.Gamepad.wButtons |= 0x0200; // RB
    if (controllers.left.stickClick) state.Gamepad.wButtons |= 0x0040;
    if (controllers.right.stickClick) state.Gamepad.wButtons |= 0x0080;
    if (controllers.left.menu) state.Gamepad.wButtons |= 0x0010;
    if (controllers.right.menu) state.Gamepad.wButtons |= 0x0020;
}

static uint32_t g_SimInputTick = 0;
typedef DWORD (WINAPI* FnXInputGetState)(DWORD dwUserIndex, void* pState);
static FnXInputGetState g_Real_XInputGetState = nullptr;
typedef DWORD (WINAPI* FnXInputGetCapabilities)(DWORD, DWORD, void*);
static FnXInputGetCapabilities g_Real_XInputGetCapabilities = nullptr;

static DWORD WINAPI Hook_XInputGetState(DWORD dwUserIndex, void* pState) {
    if (BLVR::RetailInputBridgeActive()) {
        // XR already enters the logical builder. Publishing it here as well
        // reintroduces native camera turn and doubles movement on pad records.
        return g_Real_XInputGetState ? g_Real_XInputGetState(dwUserIndex, pState)
                                    : ERROR_DEVICE_NOT_CONNECTED;
    }
    if (g_XrXInputInstalled) {
        const DWORD nativeResult = g_Real_XInputGetState
            ? g_Real_XInputGetState(dwUserIndex, pState) : ERROR_DEVICE_NOT_CONNECTED;
        BLVR::ControllerState controllers{};
        if (!pState || dwUserIndex != 0 ||
            !BLVR::XrHost::Get().GetControllerState(controllers) ||
            !XrControllerConnected(controllers)) return nativeResult;
        BlvrState merged{};
        if (nativeResult == ERROR_SUCCESS) memcpy(&merged, pState, sizeof(merged));
        BlvrState xr{};
        ApplyXrControllerState(xr, controllers);
        merged.dwPacketNumber = ++g_SimInputTick;
        merged.Gamepad.wButtons |= xr.Gamepad.wButtons;
        merged.Gamepad.bLeftTrigger = std::max(merged.Gamepad.bLeftTrigger, xr.Gamepad.bLeftTrigger);
        merged.Gamepad.bRightTrigger = std::max(merged.Gamepad.bRightTrigger, xr.Gamepad.bRightTrigger);
        if (xr.Gamepad.sThumbLX || xr.Gamepad.sThumbLY) {
            merged.Gamepad.sThumbLX = xr.Gamepad.sThumbLX;
            merged.Gamepad.sThumbLY = xr.Gamepad.sThumbLY;
        }
        if (xr.Gamepad.sThumbRX || xr.Gamepad.sThumbRY) {
            merged.Gamepad.sThumbRX = xr.Gamepad.sThumbRX;
            merged.Gamepad.sThumbRY = xr.Gamepad.sThumbRY;
        }
        memcpy(pState, &merged, sizeof(merged));
        g_XrNativePadPolled = true;
        static WORD previousButtons = 0xffff;
        if (merged.Gamepad.wButtons != previousButtons) {
            previousButtons = merged.Gamepad.wButtons;
            BLVR::Log("XR native XInput user=%lu buttons=0x%04X packet=%u",
                dwUserIndex, previousButtons, merged.dwPacketNumber);
        }
        return ERROR_SUCCESS;
    }
    if (pState) {
        if (!AutomationEnabled() && !BLVR::CameraHook_IsActive()) {
            return g_Real_XInputGetState
                ? g_Real_XInputGetState(dwUserIndex, pState)
                : 1167;
        }
        g_SimInputTick++;
        BlvrState* state = reinterpret_cast<BlvrState*>(pState);
        state->dwPacketNumber = g_SimInputTick;
        state->Gamepad.bLeftTrigger = 0;
        state->Gamepad.bRightTrigger = 0;
        state->Gamepad.sThumbLX = 0;
        state->Gamepad.sThumbLY = 0;
        state->Gamepad.sThumbRX = 0;
        state->Gamepad.sThumbRY = 0;
        state->Gamepad.wButtons = 0;

        BLVR::ControllerState xrControllers{};
        const bool haveXrControllers =
            BLVR::XrHost::Get().GetControllerState(xrControllers) &&
            xrControllers.runtimeActive;
        if (haveXrControllers) {
            ApplyXrControllerState(*state, xrControllers);
            // A live controller event must win over the bounded fallback
            // choreography below.  In gameplay this also preserves the
            // user's real buttons and sticks unchanged.
            if (BLVR::CameraHook_IsActive() ||
                xrControllers.left.menu || xrControllers.right.menu ||
                xrControllers.right.a || xrControllers.right.b ||
                xrControllers.left.x || xrControllers.left.y ||
                xrControllers.left.trigger > 0.01f ||
                xrControllers.right.trigger > 0.01f ||
                std::abs(xrControllers.left.stickX) > 0.01f ||
                std::abs(xrControllers.left.stickY) > 0.01f ||
                std::abs(xrControllers.right.stickX) > 0.01f ||
                std::abs(xrControllers.right.stickY) > 0.01f) {
                if ((g_SimInputTick % 60) == 1) {
                    BLVR::Log("Hook_XInputGetState [XR controllers]: leftTracked=%d rightTracked=%d buttons=0x%04X LT=%u RT=%u",
                              xrControllers.left.tracked ? 1 : 0,
                              xrControllers.right.tracked ? 1 : 0,
                              state->Gamepad.wButtons,
                              state->Gamepad.bLeftTrigger,
                              state->Gamepad.bRightTrigger);
                }
                return 0;
            }
        }

        if (!BLVR::CameraHook_IsActive() && AutomationEnabled()) {
            // Title screen auto-advance:
            // First 180 ticks: pulse START (0x0010) to open vinyl album
            // After tick 180: pulse A (0x1000) to select "Continue"
            if ((g_SimInputTick % 24) < 12) {
                if (g_SimInputTick < 180) {
                    state->Gamepad.wButtons = 0x0010; // XINPUT_GAMEPAD_START
                } else {
                    state->Gamepad.wButtons = 0x1000; // XINPUT_GAMEPAD_A
                }
            }
            if ((g_SimInputTick % 60) == 1) {
                BLVR::Log("Hook_XInputGetState [Menu]: tick=%u userIndex=%u wButtons=0x%04X",
                          g_SimInputTick, dwUserIndex, state->Gamepad.wButtons);
            }
        } else if (BLVR::CameraHook_IsActive()) {
            // A runtime-active controller stream with no button edge still
            // falls through to the bounded showcase fallback only when
            // automation is explicitly enabled.
            if (!AutomationEnabled()) {
                return 0;
            }

            // Automation fallback only. A live XR controller stream returns
            // above, preserving the user's actual controller state.
            uint32_t cycle = g_GameplayTick % 600;

            state->Gamepad.sThumbLX = 0;
            state->Gamepad.sThumbLY = 0;
            state->Gamepad.wButtons = 0;
            state->Gamepad.bRightTrigger = 0;

            // Movement: Stand steady for weapon showcase (0-239), then full sprint down causeway (240-420)
            if (cycle >= 240 && cycle < 420) {
                state->Gamepad.sThumbLY = 32000;
            } else if (cycle >= 420 && cycle < 490) {
                state->Gamepad.sThumbLY = 28000;
            }

            // Abilities mapped to controller:
            // Phase 1 (0.8s - 3.8s, cycle 25 - 110): Broadaxe Melee Attack Combos in first-person (X button)
            if ((cycle >= 25 && cycle < 48) || (cycle >= 55 && cycle < 78) || (cycle >= 85 && cycle < 110)) {
                state->Gamepad.wButtons |= 0x4000; // BUTTON_X: Axe chops!
            }
            // Phase 2 (3.8s - 8.0s, cycle 115 - 235): Clementine Guitar Solo & Lightning Pyrotechnics (Y + B)
            else if (cycle >= 115 && cycle < 145) {
                state->Gamepad.wButtons |= 0x8000; // BUTTON_Y: Guitar lightning shock blast!
            } else if (cycle >= 155 && cycle < 235) {
                state->Gamepad.wButtons |= 0x8000 | 0x2000; // BUTTON_Y + BUTTON_B: Sustained lightning & Solo Shred!
                state->Gamepad.bRightTrigger = 255;
            }
            // Phase 3 (14.0s - 17.0s, cycle 420 - 500): Jump & Downward Ground Slam
            else if (cycle >= 420 && cycle < 450) {
                state->Gamepad.wButtons |= 0x1000; // BUTTON_A: High Jump into the air!
            } else if (cycle >= 450 && cycle < 490) {
                state->Gamepad.wButtons |= 0x4000; // BUTTON_X: Downward aerial axe ground slam!
            }
            // Phase 4 (17.0s - 20.0s, cycle 515 - 575): Victory flourish at gate
            else if (cycle >= 515 && cycle < 575) {
                state->Gamepad.wButtons |= 0x8000 | 0x2000; // BUTTON_Y + BUTTON_B
            }

            if (g_GameplayTick % 60 == 1) {
                BLVR::Log("Hook_XInputGetState [Gameplay]: tick=%u cycle=%u wButtons=0x%04X sThumbLY=%d",
                          g_GameplayTick, cycle, state->Gamepad.wButtons, state->Gamepad.sThumbLY);
            }
        }

        return 0;
    }

    if (g_Real_XInputGetState) {
        return g_Real_XInputGetState(dwUserIndex, pState);
    }
    return 1167;
}

static DWORD WINAPI Hook_XInputSetState(DWORD dwUserIndex, void* pVibration) {
    return 0;
}

static DWORD WINAPI Hook_XInputGetCapabilities(DWORD dwUserIndex, DWORD dwFlags, void* pCaps) {
    if (g_XrXInputInstalled) {
        const DWORD nativeResult = g_Real_XInputGetCapabilities
            ? g_Real_XInputGetCapabilities(dwUserIndex, dwFlags, pCaps) : ERROR_DEVICE_NOT_CONNECTED;
        BLVR::ControllerState controllers{};
        if (nativeResult == ERROR_SUCCESS || dwUserIndex != 0 ||
            !BLVR::XrHost::Get().GetControllerState(controllers) ||
            !XrControllerConnected(controllers)) return nativeResult;
    }
    if (pCaps) {
        memset(pCaps, 0, 24);
        uint8_t* bytes = reinterpret_cast<uint8_t*>(pCaps);
        bytes[0] = 1; // Type: XINPUT_DEVTYPE_GAMEPAD
        bytes[1] = 1; // SubType: XINPUT_DEVSUBTYPE_GAMEPAD
        return 0; // ERROR_SUCCESS
    }
    return 1167; // ERROR_DEVICE_NOT_CONNECTED
}

static void InstallXrXInput() {
    if (!BLVR::XrHost::Get().HasRealXr() || g_XrXInputInstalled) return;
    HMODULE module = GetModuleHandleA("xinput1_3.dll");
    if (!module) module = LoadLibraryA("xinput1_3.dll");
    if (!module) return;
    // Retail SDL resolves ordinal 100 (GetStateEx) at VA 0xb78111, not
    // the named GetState export. Hook its real polling entry point.
    void* getState = reinterpret_cast<void*>(GetProcAddress(module, MAKEINTRESOURCEA(100)));
    void* getCaps = reinterpret_cast<void*>(GetProcAddress(module, "XInputGetCapabilities"));
    if (!getState || !getCaps) return;
    const MH_STATUS stateStatus = MH_CreateHook(getState,
        reinterpret_cast<void*>(&Hook_XInputGetState), reinterpret_cast<void**>(&g_Real_XInputGetState));
    const MH_STATUS capsStatus = MH_CreateHook(getCaps,
        reinterpret_cast<void*>(&Hook_XInputGetCapabilities), reinterpret_cast<void**>(&g_Real_XInputGetCapabilities));
    if (stateStatus == MH_OK && capsStatus == MH_OK) {
        g_XrXInputInstalled = true;
        const MH_STATUS enableState = MH_EnableHook(getState);
        const MH_STATUS enableCaps = MH_EnableHook(getCaps);
        if (enableState == MH_OK && enableCaps == MH_OK) {
            BLVR::Log("XR controllers connected through native XInput user 0; retail input edges preserved");
            return;
        }
        MH_DisableHook(getState);
        MH_DisableHook(getCaps);
        g_XrXInputInstalled = false;
        BLVR::Log("XR XInput enable failed state=%d caps=%d", enableState, enableCaps);
    } else {
        BLVR::Log("XR XInput hook unavailable state=%d caps=%d", stateStatus, capsStatus);
    }
    if (stateStatus == MH_OK) MH_RemoveHook(getState);
    if (capsStatus == MH_OK) MH_RemoveHook(getCaps);
}


// Optional controller-state exerciser for local simulator diagnostics.  Live
// Physical XR input enters the retail logical input update in retail_input_bridge.cpp.
static DWORD WINAPI AutoAdvanceThread(LPVOID lpParam) {
    BLVR::Log("AutoAdvanceThread: Started (bounded retail front-end bootstrap). Waiting 2s for engine initialization...");
    Sleep(2000);

    uintptr_t exeBase = reinterpret_cast<uintptr_t>(GetModuleHandleA(NULL));
    int32_t* pActivePlayer = reinterpret_cast<int32_t*>(exeBase + 0xc12130);
    uint8_t** ppControllers = reinterpret_cast<uint8_t**>(exeBase + 0xc11a88);
    uint8_t* pGlobalCtrl1 = reinterpret_cast<uint8_t*>(exeBase + 0xc11a8c);
    uint8_t* pGlobalCtrl2 = reinterpret_cast<uint8_t*>(exeBase + 0xc11ddc);
    uint8_t* pGlobalCtrl3 = reinterpret_cast<uint8_t*>(exeBase + 0xc12134);
    const DWORD advanceStart = GetTickCount();

    // Phase 1: advance menus through the same controller and keyboard paths
    // the retail front end consumes.  The only window touched is the game's
    // own captured HWND.
    while (!BLVR::CameraHook_IsActive()) {
        if (GetTickCount() - advanceStart > 45000) {
            BLVR::Log("AutoAdvanceThread: hard timeout after 45s without an active gameplay camera");
            break;
        }

        if (pActivePlayer && !IsBadWritePtr(pActivePlayer, sizeof(*pActivePlayer))) {
            *pActivePlayer = 0;
        }

        bool isStart = false;
        const bool shouldPress = RetailAutomationShouldPress(isStart);
        uint8_t* globals[] = { pGlobalCtrl1, pGlobalCtrl2, pGlobalCtrl3 };
        for (uint8_t* ctrl : globals) {
            if (ctrl && !IsBadWritePtr(ctrl, 0x200)) {
                if (shouldPress) {
                    ctrl[0x176] = isStart ? 1 : 0; // start/confirm
                    ctrl[0x172] = isStart ? 0 : 1; // A/accept
                } else {
                    ctrl[0x176] = 0;
                    ctrl[0x172] = 0;
                }
            }
        }
        if (ppControllers && !IsBadReadPtr(ppControllers, sizeof(void*)) && *ppControllers) {
            for (int c = 0; c < 4; c++) {
                uint8_t* ctrl = (*ppControllers) + c * 0x494;
                if (!IsBadWritePtr(ctrl, 0x200)) {
                    if (shouldPress) {
                        ctrl[0x134 + 0x176] = isStart ? 1 : 0;
                        ctrl[0x134 + 0x172] = isStart ? 0 : 1;
                    } else {
                        ctrl[0x134 + 0x176] = 0;
                        ctrl[0x134 + 0x172] = 0;
                    }
                }
            }
        }

        SafeCallFrontEndAdvance(exeBase, "AutoAdvanceThread");

        Sleep(250);
    }
    const bool gameplayReached = BLVR::CameraHook_IsActive();
    if (!gameplayReached && AutomationEnabled()) {
        InterlockedExchange(&g_AutomationEnabled, 0);
        BLVR::Log("AutoAdvanceThread: startup input bootstrap disarmed after timeout; XR input remains live");
    }
    BLVR::Log("AutoAdvanceThread: %s after %.1fs (camera updates=%u)",
              gameplayReached ? "gameplay camera is active" : "ended before gameplay became active",
              static_cast<double>(GetTickCount() - advanceStart) / 1000.0,
              BLVR::CameraHook_GetUpdateCount());
    return 0;
}

static HRESULT WINAPI Hook_Present(
    IDirect3DDevice9* thisPtr,
    const RECT* pSourceRect,
    const RECT* pDestRect,
    HWND hDestWindowOverride,
    const RGNDATA* pDirtyRegion
) {
    blvr_perf::Scope hookCost(blvr_perf::PresentHook);
    { blvr_perf::Scope paceCost(blvr_perf::Pacing); PaceD3D9Present(); }
    const LONG presentDepth = InterlockedIncrement(&g_PresentDepth);
    g_PresentCount++;
    g_GameplayFocusLock = BLVR::CameraHook_IsActive();
    // The arm palette is uploaded during the draw phase before Present. Do
    // not let an old character draw state leak into the next frame; the
    // palette hook re-arms this just before the next arm/body draw.
    g_ArmPaletteReadyForDraw = false;

    uintptr_t exeBase = reinterpret_cast<uintptr_t>(GetModuleHandleA(NULL));

    if (AutomationEnabled() &&
        (BLVR::XrHost::Get().IsSimMode() || BLVR::XrHost::Get().HasRealXr())) {
        if (!BLVR::CameraHook_IsActive()) {
            DriveAutomationKeyboardDevice(exeBase);
        }
        // Direct controller-state choreography remains opt-in for local
        // simulator diagnostics; ordinary XR input is routed above from the
        // live OpenXR controller state.
        uint8_t** ppControllers = reinterpret_cast<uint8_t**>(exeBase + 0xc11a88);
        int32_t* pActivePlayer = reinterpret_cast<int32_t*>(exeBase + 0xc12130);
        uint8_t* pGlobalCtrl1 = reinterpret_cast<uint8_t*>(exeBase + 0xc11a8c);
        uint8_t* pGlobalCtrl2 = reinterpret_cast<uint8_t*>(exeBase + 0xc11ddc);
        uint8_t* pGlobalCtrl3 = reinterpret_cast<uint8_t*>(exeBase + 0xc12134);

        if (!BLVR::CameraHook_IsActive() && g_PresentCount >= 30) {
            if (pActivePlayer && !IsBadWritePtr(pActivePlayer, sizeof(*pActivePlayer))) {
                *pActivePlayer = 0;
            }
            bool isStart = false;
            const bool shouldPress = RetailAutomationShouldPress(isStart);
            uint8_t* globals[] = { pGlobalCtrl1, pGlobalCtrl2, pGlobalCtrl3 };
            for (uint8_t* ctrl : globals) {
                if (ctrl && !IsBadWritePtr(ctrl, 0x200)) {
                    if (shouldPress) {
                        ctrl[0x176] = isStart ? 1 : 0; // Start
                        ctrl[0x172] = isStart ? 0 : 1; // A
                    } else {
                        ctrl[0x176] = 0;
                        ctrl[0x172] = 0;
                    }
                }
            }
            if (ppControllers && !IsBadReadPtr(ppControllers, sizeof(void*)) && *ppControllers) {
                for (int c = 0; c < 4; c++) {
                    uint8_t* ctrl = (*ppControllers) + c * 0x494;
                    if (!IsBadWritePtr(ctrl, 0x200)) {
                        if (shouldPress) {
                            ctrl[0x134 + 0x176] = isStart ? 1 : 0;
                            ctrl[0x134 + 0x172] = isStart ? 0 : 1;
                        } else {
                            ctrl[0x134 + 0x176] = 0;
                            ctrl[0x134 + 0x172] = 0;
                        }
                    }
                }
            }

            SafeCallFrontEndAdvance(exeBase, "Hook_Present");

        } else if (BLVR::CameraHook_IsActive()) {
            if (AutomationEnabled()) {
                InterlockedExchange(&g_AutomationEnabled, 0);
                BLVR::Log("XR gameplay camera active; startup bootstrap off, live controller input now authoritative");
            }
            g_GameplayFocusLock = true;
            if (BLVR::VideoCapture_IsStarted()) {
                g_GameplayTick = static_cast<uint32_t>(BLVR::VideoCapture_GetRecordedFrames() * 2);
            } else {
                g_GameplayTick = 0;
            }
            BLVR::XrHost::Get().SetGameplayTick(g_GameplayTick);
            uint32_t cycle = g_GameplayTick % 1200;

            // The simulator/real-XR controller poll writes the retail pad
            // object before Present.  Do not erase that live state here: the
            // old fallback choreography was running after the poll and could
            // turn a valid XR walk/weapon command back into neutral input.
            BLVR::ControllerState liveControllers{};
            const bool liveXrControllers =
                BLVR::XrHost::Get().GetControllerState(liveControllers) &&
                liveControllers.runtimeActive;
            if (!liveXrControllers &&
                ppControllers && !IsBadReadPtr(ppControllers, sizeof(void*)) && *ppControllers) {
                for (int c = 0; c < 4; c++) {
                    uint8_t* ctrl = (*ppControllers) + c * 0x494;
                    if (IsBadWritePtr(ctrl, 0x200)) continue;

                    // No live XR stream is available: keep the bounded local
                    // choreography as a recoverable simulator fallback.
                    *reinterpret_cast<int16_t*>(ctrl + 0x134 + 0x168) = 0; // Left Stick Y
                    *reinterpret_cast<int16_t*>(ctrl + 0x134 + 0x166) = 0; // Left Stick X
                    ctrl[0x134 + 0x172] = 0; // BUTTON_A
                    ctrl[0x134 + 0x173] = 0; // BUTTON_B
                    ctrl[0x134 + 0x174] = 0; // BUTTON_X
                    ctrl[0x134 + 0x175] = 0; // BUTTON_Y
                    ctrl[0x134 + 0x178] = 0; // Right Trigger

                    // Phase 3 (7.2s - 7.7s, cycle 430 - 460): Gentle Clementine guitar strum while looking at left arm
                    if (cycle >= 430 && cycle < 460) {
                        ctrl[0x134 + 0x175] = 1; // BUTTON_Y
                    }
                    // Phase 4 (10.2s - 10.7s, cycle 610 - 640): Single clean Broadaxe swing while looking at right arm
                    else if (cycle >= 610 && cycle < 640) {
                        ctrl[0x134 + 0x174] = 1; // BUTTON_X
                    }
                    // Phase 6 (15.0s - 18.0s, cycle 900 - 1080): Calm, measured walk forward down center of causeway
                    else if (cycle >= 900 && cycle < 1080) {
                        *reinterpret_cast<int16_t*>(ctrl + 0x134 + 0x168) = 14000; // Gentle walk
                    }
                }
            }
        }
    }

    // Sample OpenXR/FNVXR exactly once after the frame's simulator/gameplay
    // state has been committed.  Camera callbacks in the next render consume
    // this stable snapshot instead of advancing the XR source repeatedly.
    BLVR::CameraHook_OnPresent();

    if (g_PresentCount % 120 == 1) {
        BLVR::Log("D3D9 Present #%u rendered (CameraActive=%d, CamUpdates=%u, VideoStarted=%d, Frames=%d)",
                  g_PresentCount,
                  BLVR::CameraHook_IsActive() ? 1 : 0,
                  BLVR::CameraHook_GetUpdateCount(),
                  BLVR::VideoCapture_IsStarted() ? 1 : 0,
                  BLVR::VideoCapture_GetRecordedFrames());
    }

    // Recording is opt-in, including simulator sessions.
    char recordEnv[16] = {};
    GetEnvironmentVariableA("BLVR_RECORD", recordEnv, sizeof(recordEnv));
    bool enableRecord = recordEnv[0] == '1' || _stricmp(recordEnv, "true") == 0;
    if (enableRecord && !BLVR::VideoCapture_IsStarted() && !BLVR::VideoCapture_IsFinished()) {
        if (BLVR::CameraHook_IsActive() && BLVR::CameraHook_GetUpdateCount() >= 5 &&
            BLVR::CameraHook_GetGameplayRenderCount() >= 10) {
            if (!g_RecordDelayConfigured) {
                char delayEnv[24] = {};
                GetEnvironmentVariableA("BLVR_RECORD_DELAY_PRESENTS", delayEnv, sizeof(delayEnv));
                g_RecordDelayPresents = delayEnv[0]
                    ? static_cast<uint32_t>(std::max(0, atoi(delayEnv))) : 0u;
                g_RecordDelayConfigured = true;
            }
            if (!g_RecordDelayArmed) {
                g_RecordEligiblePresent = g_PresentCount + g_RecordDelayPresents;
                g_RecordDelayArmed = true;
                BLVR::Log("Hook_Present: VideoCapture armed for Present #%u (delay=%u, gameplayRenders=%u)",
                          g_RecordEligiblePresent, g_RecordDelayPresents,
                          BLVR::CameraHook_GetGameplayRenderCount());
            }
            if (g_PresentCount >= g_RecordEligiblePresent) {
                BLVR::Log("Hook_Present: Triggering VideoCapture! (CameraActive=%d, CamUpdates=%u, GameplayRenders=%u, PresentCount=%u)",
                          BLVR::CameraHook_IsActive() ? 1 : 0,
                          BLVR::CameraHook_GetUpdateCount(),
                          BLVR::CameraHook_GetGameplayRenderCount(),
                          g_PresentCount);
                BLVR::VideoCapture_Start(0, 0);
            }
        }
    }

    // Capture the backbuffer or active world target before Present so that
    // the rendered contents have not been swapped or discarded by the driver.
    char captureTimingEnv[16] = {};
    GetEnvironmentVariableA("BLVR_CAPTURE_BEFORE_PRESENT", captureTimingEnv, sizeof(captureTimingEnv));
    const bool captureBeforePresent = captureTimingEnv[0]
        ? (captureTimingEnv[0] == '1' || _stricmp(captureTimingEnv, "true") == 0)
        : true;
    const bool captureFrame = (g_PresentCount % 2 == 0);
    const bool cinema = BLVR::CameraHook_IsCinematic();
    if (captureFrame && (!BLVR::CameraHook_IsActive() || cinema)) {
        BLVR::VideoCapture_PublishUiFrame(thisPtr, g_PresentCount);
    }
    if (!cinema) { blvr_perf::Scope mirrorCost(blvr_perf::Mirror); BLVR::VideoCapture_PresentMirror(thisPtr); }
    if (g_PresentCount % 60 == 0) BLVR::VideoCapture_PollDiagnosticSnapshot(thisPtr);
    if (captureBeforePresent && captureFrame &&
        BLVR::VideoCapture_IsStarted()) {
        BLVR::VideoCapture_OnFrame(thisPtr);
    }

    blvr_perf::Scope driverCost(blvr_perf::PresentDriver);
    const HRESULT presentResult = g_Original_Present(
        thisPtr, pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion);
    driverCost.stop();

    // Sample every second presented frame for a stable 30 FPS proof stream
    // when after-present was explicitly requested.
    if (!captureBeforePresent && captureFrame &&
        BLVR::VideoCapture_IsStarted()) {
        BLVR::VideoCapture_OnFrame(thisPtr);
    }

    if (BLVR::Telemetry_Enabled()) {
        char fields[1500] = {};
        std::snprintf(fields, sizeof(fields),
            "\"present_count\":%u,\"present_depth\":%ld,\"camera_updates\":%u,"
            "\"pose_count\":%u,\"gameplay_tick\":%u,\"present_hr\":%lu,"
            "\"capture_started\":%d,\"capture_finished\":%d,\"capture_frames\":%d,"
            "\"arm_ready\":%d,\"arm_palette\":%u,\"arm_draws\":%u,\"arm_rig_overrides\":%u,"
            "\"head_mask\":%u,\"last_world_valid\":%d,\"last_world\":[%.4f,%.4f,%.4f],"
            "\"reentrant\":%d",
            g_PresentCount, presentDepth,
            BLVR::CameraHook_GetUpdateCount(), BLVR::XrHost::Get().GetPoseUpdateCount(),
            g_GameplayTick, static_cast<unsigned long>(presentResult),
            BLVR::VideoCapture_IsStarted() ? 1 : 0,
            BLVR::VideoCapture_IsFinished() ? 1 : 0,
            BLVR::VideoCapture_GetRecordedFrames(),
            g_ArmPaletteReadyForDraw ? 1 : 0, g_ArmPaletteSequence,
            g_ArmPaletteDrawsSinceUpload, g_ArmRigOverrideCount, g_HeadOnlyArmsMask,
            g_HasLastObjectWorld ? 1 : 0,
            g_HasLastObjectWorld ? g_LastObjectWorld[12] : 0.0f,
            g_HasLastObjectWorld ? g_LastObjectWorld[13] : 0.0f,
            g_HasLastObjectWorld ? g_LastObjectWorld[14] : 0.0f,
            presentDepth > 1 ? 1 : 0);
        BLVR::Telemetry_Write("present", fields);
    }

    InterlockedDecrement(&g_PresentDepth);
    hookCost.stop();
    static uint32_t lastPerfDraws=0;
    const uint32_t perfDraws=BLVR_GetD3D9DrawCallCount();
    blvr_perf::recorder().finish("game",g_PresentCount,BLVR::CameraHook_GetStereoSourceFrame(),
        cinema?2u:BLVR::CameraHook_IsActive()?1u:0u,perfDraws-lastPerfDraws);
    lastPerfDraws=perfDraws;

    return presentResult;
}

static HRESULT WINAPI Hook_Reset(
    IDirect3DDevice9* thisPtr,
    D3DPRESENT_PARAMETERS* pPresentationParameters
) {
    BLVR::Log("D3D9 Reset called");
    BLVR::CameraHook_ResetProfiler();
    BLVR::VideoCapture_OnDeviceReset();
    char xrBridge[8]{};
    GetEnvironmentVariableA("BLVR_XR_BRIDGE",xrBridge,sizeof(xrBridge));
    if(pPresentationParameters && (xrBridge[0]=='1'||_stricmp(xrBridge,"true")==0)) {
        pPresentationParameters->PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
        if(pPresentationParameters->Windowed) {
            pPresentationParameters->BackBufferWidth=blvr_quality::width();
            pPresentationParameters->BackBufferHeight=blvr_quality::height();
        }
    }
    const HRESULT hr = g_Original_Reset(thisPtr, pPresentationParameters);
    BLVR::Log("D3D9 Reset completed hr=0x%08X", hr);
    return hr;
}

static HRESULT WINAPI Hook_CreateDevice(
    IDirect3D9* thisPtr,
    UINT Adapter,
    D3DDEVTYPE DeviceType,
    HWND hFocusWindow,
    DWORD BehaviorFlags,
    D3DPRESENT_PARAMETERS* pPresentationParameters,
    IDirect3DDevice9** ppReturnedDeviceInterface
) {
    UINT adapterCount = thisPtr->GetAdapterCount();
    BLVR::Log("Hook_CreateDevice: Total D3D9 Adapters = %u", adapterCount);

    UINT bestAdapter = Adapter;
    bool foundHardwareGpu = false;

    for (UINT i = 0; i < adapterCount; i++) {
        D3DADAPTER_IDENTIFIER9 ident{};
        if (SUCCEEDED(thisPtr->GetAdapterIdentifier(i, 0, &ident))) {
            BLVR::Log("  Adapter %u: %s (Vendor=0x%04X, Device=0x%04X)",
                      i, ident.Description, ident.VendorId, ident.DeviceId);
            if (!foundHardwareGpu) {
                bestAdapter = i;
                foundHardwareGpu = true;
                BLVR::Log("  -> Using Adapter %u (%s)", i, ident.Description);
            }
        }
    }

    D3DDISPLAYMODE mode{};
    HRESULT modeHr = thisPtr->GetAdapterDisplayMode(bestAdapter, &mode);
    BLVR::Log("Using Adapter %u: GetAdapterDisplayMode hr=0x%08X, %ux%u @ %uHz, Format=%d",
              bestAdapter, modeHr, mode.Width, mode.Height, mode.RefreshRate, mode.Format);

    D3DCAPS9 caps{};
    HRESULT capsHr = thisPtr->GetDeviceCaps(bestAdapter, DeviceType, &caps);
    BLVR::Log("DeviceCaps on Adapter %u: hr=0x%08X, VS=0x%08X, PS=0x%08X",
              bestAdapter, capsHr, caps.VertexShaderVersion, caps.PixelShaderVersion);

    HWND targetHwnd = hFocusWindow ? hFocusWindow : (pPresentationParameters ? pPresentationParameters->hDeviceWindow : NULL);
    g_GameHwnd = targetHwnd;
    ConfigureCaptureWindow(targetHwnd);
    BLVR::Log("CreateDevice: targetHwnd=0x%p (hFocusWindow=0x%p, pPresParams->hDeviceWindow=0x%p)",
              g_GameHwnd, hFocusWindow, pPresentationParameters ? pPresentationParameters->hDeviceWindow : NULL);

    char winEnv[16] = {};
    GetEnvironmentVariableA("BLVR_WINDOWED", winEnv, sizeof(winEnv));
    char simEnv[16] = {};
    GetEnvironmentVariableA("BLVR_SIM", simEnv, sizeof(simEnv));
    bool simActive = (simEnv[0] == '1' || _stricmp(simEnv, "true") == 0) &&
                     (GetFileAttributesA("blvr_sim.txt") != INVALID_FILE_ATTRIBUTES);
    bool forceWindowed = (winEnv[0] == '1' || _stricmp(winEnv, "true") == 0 || simActive);

    D3DPRESENT_PARAMETERS winParams{};
    if (pPresentationParameters) {
        winParams = *pPresentationParameters;
    }
    if (forceWindowed) {
        winParams.Windowed = TRUE;
        winParams.BackBufferWidth = blvr_quality::width();
        winParams.BackBufferHeight = blvr_quality::height();
    }
    winParams.BackBufferFormat = (mode.Format != D3DFMT_UNKNOWN) ? mode.Format : D3DFMT_X8R8G8B8;
    winParams.BackBufferCount = 1;
    winParams.MultiSampleType = D3DMULTISAMPLE_NONE;
    winParams.MultiSampleQuality = 0;
    winParams.SwapEffect = D3DSWAPEFFECT_DISCARD;
    winParams.hDeviceWindow = targetHwnd;
    winParams.FullScreen_RefreshRateInHz = 0;
    winParams.PresentationInterval = D3DPRESENT_INTERVAL_DEFAULT;
    char xrBridge[8]{};
    GetEnvironmentVariableA("BLVR_XR_BRIDGE",xrBridge,sizeof(xrBridge));
    if(xrBridge[0]=='1'||_stricmp(xrBridge,"true")==0) {
        winParams.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
        BLVR::Log("XR mirror uses immediate Present; OpenXR owns headset pacing");
        BLVR::Log("VR source render: %ux%u per eye",winParams.BackBufferWidth,winParams.BackBufferHeight);
    }

    DWORD behaviors[] = {
        D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE,
        BehaviorFlags,
        (BehaviorFlags & ~D3DCREATE_PUREDEVICE),
        D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE
    };

    HRESULT hr = E_FAIL;
    for (DWORD b : behaviors) {
        hr = g_Original_CreateDevice(thisPtr, bestAdapter, DeviceType, targetHwnd, b, &winParams, ppReturnedDeviceInterface);
        if (SUCCEEDED(hr)) {
            BLVR::Log("CreateDevice SUCCEEDED on Adapter %u with Behav=0x%x!", bestAdapter, b);
            if (pPresentationParameters) *pPresentationParameters = winParams;
            break;
        } else {
            BLVR::Log("CreateDevice attempt failed on Adapter %u with Behav=0x%x, hr=0x%08X", bestAdapter, b, hr);
        }
    }

    if (SUCCEEDED(hr) && ppReturnedDeviceInterface && *ppReturnedDeviceInterface) {
        IDirect3DDevice9* pDevice = *ppReturnedDeviceInterface;
        g_LastD3D9Device = pDevice;
        BLVR::Log("IDirect3DDevice9 created successfully at 0x%p", pDevice);

        if (!g_HooksInstalled) {
            void** vtable = *reinterpret_cast<void***>(pDevice);
            void* pReset = vtable[16];
            void* pPresent = vtable[17];
            void* pSetTransform = vtable[44];
            void* pSetRenderState = vtable[57];
            void* pDrawIndexedPrimitive = vtable[82];
            void* pSetVertexShaderConstantF = vtable[94];
            if(BLVR::Telemetry_Enabled()) {
                const MH_STATUS queryStatus=MH_CreateHook(vtable[118],reinterpret_cast<void*>(&Hook_CreateQuery),reinterpret_cast<void**>(&g_OriginalCreateQuery));
                const MH_STATUS enabled=queryStatus==MH_OK?MH_EnableHook(vtable[118]):queryStatus;
                char fields[96]{};
                std::snprintf(fields,sizeof(fields),"\"enabled\":%s,\"hook_status\":%d",enabled==MH_OK?"true":"false",static_cast<int>(enabled));
                BLVR::Telemetry_Write("gpu_query_observer",fields);
            }
            char shaderDumpDirectory[MAX_PATH]{};
            if(GetEnvironmentVariableA("BLVR_SHADER_DUMP_DIR",shaderDumpDirectory,sizeof(shaderDumpDirectory))) {
                if(MH_CreateHook(vtable[106],reinterpret_cast<void*>(&Hook_CreatePixelShader),reinterpret_cast<void**>(&g_OriginalCreatePixelShader))==MH_OK)
                    MH_EnableHook(vtable[106]);
                if(MH_CreateHook(vtable[91],reinterpret_cast<void*>(&Hook_CreateVertexShader),reinterpret_cast<void**>(&g_OriginalCreateVertexShader))==MH_OK)
                    MH_EnableHook(vtable[91]);
            }

            BLVR::Log("Hooking IDirect3DDevice9::Present (vtable[17]=0x%p), Reset (vtable[16]=0x%p), SetTransform (vtable[44]=0x%p), SetRenderState (vtable[57]=0x%p), DrawIndexedPrimitive (vtable[82]=0x%p), SetVertexShaderConstantF (vtable[94]=0x%p)...", pPresent, pReset, pSetTransform, pSetRenderState, pDrawIndexedPrimitive, pSetVertexShaderConstantF);

            MH_STATUS s1 = MH_CreateHook(pPresent, (void*)&Hook_Present, (void**)&g_Original_Present);
            MH_STATUS s2 = MH_CreateHook(pReset, (void*)&Hook_Reset, (void**)&g_Original_Reset);
            MH_STATUS s3 = MH_CreateHook(pSetTransform, (void*)&Hook_SetTransform, (void**)&g_Original_SetTransform);
            MH_STATUS s4 = MH_CreateHook(pSetRenderState, (void*)&Hook_SetRenderState, (void**)&g_Original_SetRenderState);
            MH_STATUS s5 = MH_CreateHook(pDrawIndexedPrimitive, (void*)&Hook_DrawIndexedPrimitive, (void**)&g_Original_DrawIndexedPrimitive);
            MH_STATUS s6 = MH_CreateHook(pSetVertexShaderConstantF, (void*)&Hook_SetVertexShaderConstantF, (void**)&g_Original_SetVertexShaderConstantF);

            if (s1 == MH_OK && s2 == MH_OK && s3 == MH_OK && s4 == MH_OK && s5 == MH_OK && s6 == MH_OK) {
                MH_EnableHook(pPresent);
                MH_EnableHook(pReset);
                MH_EnableHook(pSetTransform);
                MH_EnableHook(pSetRenderState);
                MH_EnableHook(pDrawIndexedPrimitive);
                MH_EnableHook(pSetVertexShaderConstantF);
                BLVR::Log("D3D9 Present, Reset, SetTransform, SetRenderState, DrawIndexedPrimitive, and SetVertexShaderConstantF hooks installed successfully!");
            } else {
                BLVR::Log("Warning: MinHook failed on device vtable: s1=%d, s2=%d, s3=%d, s4=%d, s5=%d, s6=%d", (int)s1, (int)s2, (int)s3, (int)s4, (int)s5, (int)s6);
            }

            // Initialize OpenXR, Camera Hooks, and Video Capture
            BLVR::XrHost::Get().Init();
            if (BLVR::XrHost::Get().HasRealXr()) {
                BLVR::Log("Retail logical XR input bridge installed=%d",
                    BLVR::InstallRetailInputBridge() ? 1 : 0);
            }
            BLVR::CameraHook_Init();
            BLVR::VideoCapture_Init();

            char automationEnv[16] = {};
            GetEnvironmentVariableA("BLVR_AUTOMATION", automationEnv, sizeof(automationEnv));
            // BLVR_AUTOMATION previously generated repeated Start/A presses,
            // including Pause after level loads. Never synthesize startup
            // buttons in a real OpenXR session; the user's actions own input.
            const LONG automationEnabled = BLVR::XrHost::Get().IsSimMode() &&
                (automationEnv[0] == '1' || _stricmp(automationEnv, "true") == 0) ? 1 : 0;
            InterlockedExchange(&g_AutomationEnabled, automationEnabled);
            BLVR::Log("BLVR automation input=%d (normal XR play leaves retail input authoritative)",
                      automationEnabled);

            char titleEventOnlyEnv[16] = {};
            GetEnvironmentVariableA("BLVR_TITLE_EVENT_ONLY", titleEventOnlyEnv,
                                    sizeof(titleEventOnlyEnv));
            const LONG titleEventOnly =
                titleEventOnlyEnv[0] == '1' || _stricmp(titleEventOnlyEnv, "true") == 0 ? 1 : 0;
            InterlockedExchange(&g_TitleEventOnly, titleEventOnly);
            BLVR::Log("BLVR title event-only controller gate=%d", titleEventOnly);

            // Spawn the bounded controller-state exerciser only for an
            // explicit local diagnostics run.
            if (AutomationEnabled() &&
                (BLVR::XrHost::Get().IsSimMode() || BLVR::XrHost::Get().HasRealXr()) &&
                !g_hAutoAdvanceThread) {
                g_hAutoAdvanceThread = CreateThread(NULL, 0, AutoAdvanceThread, NULL, 0, NULL);
            }

            g_HooksInstalled = true;
        }
    } else {
        BLVR::Log("Hook_CreateDevice all attempts failed with hr=0x%08X", hr);
    }

    return hr;
}

extern "C" IDirect3D9* WINAPI Proxy_Direct3DCreate9(UINT SDKVersion) {
    BLVR::LogInit();
    BLVR::Log("Proxy_Direct3DCreate9 called with SDKVersion=0x%08X", SDKVersion);

    if (!g_RealD3D9Module) {
        // Try local dxvk_d3d9.dll first for modern Vulkan-accelerated D3D9 HAL
        g_RealD3D9Module = LoadLibraryA("dxvk_d3d9.dll");
        if (g_RealD3D9Module) {
            BLVR::Log("Loaded hardware-accelerated Vulkan backend from dxvk_d3d9.dll!");
        } else {
            char sysDir[MAX_PATH];
            GetSystemDirectoryA(sysDir, MAX_PATH);
            strcat_s(sysDir, "\\d3d9.dll");
            BLVR::Log("Loading real system d3d9.dll from: %s", sysDir);
            g_RealD3D9Module = LoadLibraryA(sysDir);
        }

        if (!g_RealD3D9Module) {
            BLVR::Log("CRITICAL: Failed to load D3D9 backend DLL! Error: %lu", GetLastError());
            return nullptr;
        }
        g_Real_Direct3DCreate9 = (FnDirect3DCreate9)GetProcAddress(g_RealD3D9Module, "Direct3DCreate9");
        if (!g_Real_Direct3DCreate9) {
            BLVR::Log("CRITICAL: Failed to get Direct3DCreate9 proc address!");
            return nullptr;
        }
    }

    IDirect3D9* pD3D = g_Real_Direct3DCreate9(SDKVersion);
    if (!pD3D) {
        BLVR::Log("Real Direct3DCreate9 returned null!");
        return nullptr;
    }

    BLVR::Log("Real Direct3DCreate9 returned IDirect3D9 = 0x%p", pD3D);

    // Initialize MinHook if not already done
    MH_Initialize();

    uintptr_t exeBase = reinterpret_cast<uintptr_t>(GetModuleHandleA(NULL));

    // Hook GPU check at RVA 0x26c360
    void* targetGpuCheck = reinterpret_cast<void*>(exeBase + RVA_GPU_CHECK);
    BLVR::Log("Proxy_Direct3DCreate9: ExeBase=0x%p, Hooking GPU check at 0x%p...", (void*)exeBase, targetGpuCheck);

    MH_STATUS smStatus = MH_CreateHook(targetGpuCheck, (void*)&Hook_CheckGpuSpec, (void**)&g_Original_CheckGpuSpec);
    if (smStatus == MH_OK) {
        MH_EnableHook(targetGpuCheck);
        BLVR::Log("Hooked 0x%p (GPU capability check) successfully!", targetGpuCheck);
    } else {
        BLVR::Log("Warning: Failed to hook GPU capability check: %d", (int)smStatus);
    }

    // Hook Controller Poll function at RVA 0x7788a0
    void* targetPoll = reinterpret_cast<void*>(exeBase + RVA_POLL_CONTROLLER);
    BLVR::Log("Proxy_Direct3DCreate9: Hooking Controller Poll at 0x%p...", targetPoll);
    MH_STATUS pollStatus = MH_CreateHook(targetPoll, (void*)&Hook_PollController, (void**)&g_Original_PollController);
    if (pollStatus == MH_OK) {
        MH_EnableHook(targetPoll);
        BLVR::Log("Hooked 0x%p (Controller Poll function) successfully!", targetPoll);
    } else {
        BLVR::Log("Warning: Failed to hook Controller Poll function: %d", (int)pollStatus);
    }

    // Hook KeyboardDevice::Poll at RVA 0x2826d0 (exeBase + 0x2826d0)
    void* targetKbd = reinterpret_cast<void*>(exeBase + RVA_KEYBOARD_POLL);
    BLVR::Log("Proxy_Direct3DCreate9: Hooking KeyboardDevice::Poll at 0x%p...", targetKbd);
    MH_STATUS kbdStatus = MH_CreateHook(targetKbd, (void*)&Hook_KeyboardPoll, (void**)&g_Original_KeyboardPoll);
    if (kbdStatus == MH_OK) {
        MH_EnableHook(targetKbd);
        BLVR::Log("Hooked 0x%p (KeyboardDevice::Poll) successfully!", targetKbd);
    } else {
        BLVR::Log("Warning: Failed to hook KeyboardDevice::Poll: %d", (int)kbdStatus);
    }

    // Build-bound hook: this retail FE update refreshes controller state and
    // returns immediately before FrontEnd::Update tests the +0x11d event bit.
    // Check the executable prologue before enabling the post-refresh XR pulse.
    g_TargetFrontEndInputUpdate = reinterpret_cast<void*>(
        exeBase + RVA_FRONTEND_INPUT_UPDATE);
    static const uint8_t expectedFrontEndInputPrologue[] = {
        0x55, 0x8b, 0xec, 0x83, 0xe4, 0xf8, 0x83, 0xec, 0x1c, 0x53
    };
    if (memcmp(g_TargetFrontEndInputUpdate, expectedFrontEndInputPrologue,
               sizeof(expectedFrontEndInputPrologue)) == 0) {
        MH_STATUS frontEndStatus = MH_CreateHook(
            g_TargetFrontEndInputUpdate,
            (void*)&Hook_FrontEndInputUpdate,
            (void**)&g_Original_FrontEndInputUpdate);
        if (frontEndStatus == MH_OK) {
            frontEndStatus = MH_EnableHook(g_TargetFrontEndInputUpdate);
        }
        BLVR::Log("FrontEnd input-refresh hook at 0x%p status=%d",
                  g_TargetFrontEndInputUpdate, static_cast<int>(frontEndStatus));
    } else {
        BLVR::Log("FrontEnd input-refresh signature mismatch at 0x%p; skipped",
                  g_TargetFrontEndInputUpdate);
    }

    // This retail predicate is the acceptance gate used by VA 0x4081e0. Log
    // its result only while an XR accept edge is inside the native handler.
    g_TargetFrontEndActionGate = reinterpret_cast<void*>(
        exeBase + RVA_FRONTEND_ACTION_GATE);
    uint8_t expectedFrontEndActionGatePrologue[] = {
        0xa1, 0x00, 0x00, 0x00, 0x00, 0x57, 0x85, 0xc0
    };
    const uint32_t relocatedRootAddress =
        static_cast<uint32_t>(exeBase + 0xc09cb0);
    memcpy(expectedFrontEndActionGatePrologue + 1, &relocatedRootAddress,
           sizeof(relocatedRootAddress));
    if (memcmp(g_TargetFrontEndActionGate, expectedFrontEndActionGatePrologue,
               sizeof(expectedFrontEndActionGatePrologue)) == 0) {
        MH_STATUS actionGateStatus = MH_CreateHook(
            g_TargetFrontEndActionGate,
            (void*)&Hook_FrontEndActionGate,
            (void**)&g_Original_FrontEndActionGate);
        if (actionGateStatus == MH_OK) {
            actionGateStatus = MH_EnableHook(g_TargetFrontEndActionGate);
        }
        BLVR::Log("FrontEnd accept interaction-gate hook at 0x%p status=%d",
                  g_TargetFrontEndActionGate, static_cast<int>(actionGateStatus));
    } else {
        BLVR::Log("FrontEnd accept interaction-gate signature mismatch at 0x%p; skipped",
                  g_TargetFrontEndActionGate);
    }

    g_TargetFrontEndAcceptEvent = reinterpret_cast<void*>(
        exeBase + RVA_FRONTEND_ACCEPT_EVENT);
    static const uint8_t expectedFrontEndAcceptPrologue[] = {
        0x55, 0x8b, 0xec, 0x83, 0xe4, 0xf8, 0x56, 0x57
    };
    if (memcmp(g_TargetFrontEndAcceptEvent, expectedFrontEndAcceptPrologue,
               sizeof(expectedFrontEndAcceptPrologue)) == 0) {
        MH_STATUS acceptStatus = MH_CreateHook(
            g_TargetFrontEndAcceptEvent,
            (void*)&Hook_FrontEndAcceptEvent,
            (void**)&g_Original_FrontEndAcceptEvent);
        if (acceptStatus == MH_OK) {
            acceptStatus = MH_EnableHook(g_TargetFrontEndAcceptEvent);
        }
        BLVR::Log("FrontEnd accept handler hook at 0x%p status=%d",
                  g_TargetFrontEndAcceptEvent, static_cast<int>(acceptStatus));
    } else {
        BLVR::Log("FrontEnd accept handler signature mismatch at 0x%p; skipped",
                  g_TargetFrontEndAcceptEvent);
    }

    // Hook IDirect3D9::CreateDevice (vtable index 16)
    void** vtable = *reinterpret_cast<void***>(pD3D);
    void* pCreateDevice = vtable[16];

    if (!g_Original_CreateDevice) {
        BLVR::Log("Hooking IDirect3D9::CreateDevice (vtable[16]=0x%p)...", pCreateDevice);
        MH_STATUS status = MH_CreateHook(pCreateDevice, (void*)&Hook_CreateDevice, (void**)&g_Original_CreateDevice);
        if (status == MH_OK) {
            MH_EnableHook(pCreateDevice);
            BLVR::Log("IDirect3D9::CreateDevice hook enabled!");
        } else {
            BLVR::Log("Warning: Failed to hook CreateDevice via MinHook: %d", (int)status);
        }
    }

    return pD3D;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        BLVR::LogInit();
        BLVR::Log("BLVR: DLL_PROCESS_ATTACH (Module=0x%p)", hModule);
        break;
    case DLL_PROCESS_DETACH:
        BLVR::Log("BLVR: DLL_PROCESS_DETACH");
        BLVR::VideoCapture_Stop();
        BLVR::CameraHook_Shutdown();
        BLVR::XrHost::Get().Shutdown();
        MH_Uninitialize();
        if (g_RealD3D9Module) {
            FreeLibrary(g_RealD3D9Module);
            g_RealD3D9Module = nullptr;
        }
        break;
    }
    return TRUE;
}
