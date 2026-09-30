#include "video_capture.h"
#include "../diagnostics/frame_profiler.h"
#include "blvr_frame_bridge.h"
#include "../bridge/blvr_ui_bridge.h"
#include "../camera/camera_hook.h"
#include "../input/vr_prompts.h"
#include "audio_capture.h"
#include "../diagnostics/log.h"
#include "../diagnostics/telemetry.h"
#include "../openxr/xr_host.h"
#include <windows.h>
#include <stdio.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace BLVR {

static FILE* g_FfmpegPipe = nullptr;
static IDirect3DSurface9* g_pSysMemSurface = nullptr;
static int g_CaptureWidth = 0;
static int g_CaptureHeight = 0;
static int g_RecordedFrames = 0;
static bool g_RecordingStarted = false;
static bool g_RecordingFinished = false;

static HANDLE g_BlvrFrameDescriptorMapping = nullptr;
static BlvrFrameTransport::FrameBridge* g_BlvrFrameDescriptor = nullptr;
static HANDLE g_BlvrFrameCpuMapping = nullptr;
static BlvrFrameTransport::CpuFrameMailbox* g_BlvrFrameCpu = nullptr;
static uint64_t g_BlvrFrameTransaction = 0;
static uint32_t g_BlvrFrameSlot = 0;
struct StereoEyeBuffer {
    std::vector<uint8_t> pixels;
    uint32_t width = 0;
    uint32_t height = 0;
    uint64_t sourceFrameId = 0;
    uint64_t poseSequence = 0;
    int64_t renderedDisplayTime = 0;
    bool valid = false;
};
static StereoEyeBuffer g_StereoEyeBuffers[2];
static IDirect3DSurface9* g_StereoResolveSurface = nullptr;
static IDirect3DSurface9* g_StereoScaledSurface = nullptr;
static IDirect3DSurface9* g_StereoReadbackSurface = nullptr;
static IDirect3DDevice9* g_StereoSurfaceDevice = nullptr;
static UINT g_StereoSourceWidth = 0;
static UINT g_StereoSourceHeight = 0;
static UINT g_StereoTargetWidth = 0;
static UINT g_StereoTargetHeight = 0;
static D3DFORMAT g_StereoFormat = D3DFMT_UNKNOWN;
static D3DMULTISAMPLE_TYPE g_StereoSamples = D3DMULTISAMPLE_NONE;
static DWORD g_StereoSampleQuality = 0;
// The retail renderer can leave a different camera pass in the backbuffer.
// Keep two GPU surfaces so an incomplete stereo transaction never replaces
// the last published first-person view in the desktop window.
static IDirect3DSurface9* g_MirrorPending = nullptr;
static IDirect3DSurface9* g_MirrorPublished = nullptr;
static UINT g_MirrorWidth = 0, g_MirrorHeight = 0;
static D3DFORMAT g_MirrorFormat = D3DFMT_UNKNOWN;
static uint64_t g_MirrorPendingFrame = 0, g_MirrorPublishedFrame = 0;
static uint64_t g_MirrorPublishedTick = 0;
static uint32_t g_StereoCaptureFailureLogs = 0;
static uint32_t g_StereoCaptureSuccessLogs = 0;
static uint32_t g_StereoPairLogCount = 0;
static uint32_t g_StereoPairRejectLogs = 0;
static HANDLE g_NativeUiHandle = nullptr;
static blvr_ui_bridge::Header* g_NativeUi = nullptr;
static IDirect3DSurface9* g_NativeUiScaled = nullptr;
static IDirect3DSurface9* g_NativeUiReadback = nullptr;
static UINT g_NativeUiWidth = 0, g_NativeUiHeight = 0;

static void ReleaseNativeUiSurfaces() {
    if (g_NativeUiScaled) g_NativeUiScaled->Release();
    if (g_NativeUiReadback) g_NativeUiReadback->Release();
    g_NativeUiScaled = g_NativeUiReadback = nullptr;
    g_NativeUiWidth = g_NativeUiHeight = 0;
    if (g_NativeUi) {
        InterlockedIncrement(&g_NativeUi->sequence);
        g_NativeUi->tickMs = 0;
        MemoryBarrier();
        InterlockedIncrement(&g_NativeUi->sequence);
    }
}

void VideoCapture_CaptureNativeUi(IDirect3DDevice9* device, uint64_t sourceFrame, uint32_t flags) {
    blvr_perf::Scope cost(blvr_perf::NativeUi);
    using namespace blvr_ui_bridge;
    if (!device || !g_BlvrFrameDescriptor) return;
    VrPrompts_Publish(g_BlvrFrameDescriptor->producerEpoch);
    IDirect3DSurface9* source = nullptr;
    if (FAILED(device->GetRenderTarget(0, &source)) || !source) return;
    D3DSURFACE_DESC desc{};
    source->GetDesc(&desc);
    // Flash's target has a meaningful alpha channel. Never key out black:
    // black UI artwork is opaque, while the world must show through alpha=0.
    if (!desc.Width || !desc.Height || desc.Format != D3DFMT_A8R8G8B8 ||
        desc.MultiSampleType != D3DMULTISAMPLE_NONE) {
        static unsigned logs = 0;
        if (logs++ < 3) Log("NativeUi: unsupported target %ux%u format=%u samples=%u",
            desc.Width, desc.Height, unsigned(desc.Format), unsigned(desc.MultiSampleType));
        source->Release(); return;
    }
    const double scale = std::min({1.0, double(MaxWidth)/desc.Width, double(MaxHeight)/desc.Height});
    const UINT width = std::max(1u, UINT(desc.Width * scale));
    const UINT height = std::max(1u, UINT(desc.Height * scale));
    if (width != g_NativeUiWidth || height != g_NativeUiHeight) {
        ReleaseNativeUiSurfaces();
        if (FAILED(device->CreateRenderTarget(width, height, desc.Format,
            D3DMULTISAMPLE_NONE, 0, FALSE, &g_NativeUiScaled, nullptr)) ||
            FAILED(device->CreateOffscreenPlainSurface(width, height, desc.Format,
                D3DPOOL_SYSTEMMEM, &g_NativeUiReadback, nullptr))) {
            ReleaseNativeUiSurfaces(); source->Release(); return;
        }
        g_NativeUiWidth = width; g_NativeUiHeight = height;
    }
    IDirect3DSurface9* readbackSource = source;
    HRESULT hr = S_OK;
    if (width != desc.Width || height != desc.Height) {
        hr = device->StretchRect(source, nullptr, g_NativeUiScaled, nullptr, D3DTEXF_LINEAR);
        readbackSource = g_NativeUiScaled;
    }
    if (SUCCEEDED(hr)) hr = device->GetRenderTargetData(readbackSource, g_NativeUiReadback);
    source->Release();
    if (FAILED(hr)) return;
    D3DLOCKED_RECT locked{};
    if (FAILED(g_NativeUiReadback->LockRect(&locked, nullptr, D3DLOCK_READONLY))) return;
    if (!g_NativeUi) {
        g_NativeUiHandle = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr,
            PAGE_READWRITE, 0, DWORD(MappingBytes), MappingName);
        if (g_NativeUiHandle) g_NativeUi = static_cast<Header*>(
            MapViewOfFile(g_NativeUiHandle, FILE_MAP_ALL_ACCESS, 0, 0, MappingBytes));
        if (g_NativeUi) {
            std::memset(g_NativeUi, 0, sizeof(Header));
            Log("NativeUi: captured native Flash target %ux%u -> %ux%u, BGRA alpha retained",
                desc.Width, desc.Height, width, height);
        } else if (g_NativeUiHandle) { CloseHandle(g_NativeUiHandle); g_NativeUiHandle = nullptr; }
    }
    if (g_NativeUi && locked.Pitch >= int(width * 4u)) {
        InterlockedIncrement(&g_NativeUi->sequence);
        g_NativeUi->magic = Magic; g_NativeUi->version = Version;
        g_NativeUi->headerBytes = sizeof(Header);
        g_NativeUi->producerPid = GetCurrentProcessId();
        g_NativeUi->producerEpoch = g_BlvrFrameDescriptor->producerEpoch;
        g_NativeUi->width = width; g_NativeUi->height = height;
        g_NativeUi->rowBytes = width * 4u;
        g_NativeUi->tickMs = GetTickCount64();
        g_NativeUi->sourceFrame = sourceFrame;
        g_NativeUi->contentSamples = 0;
        g_NativeUi->flags = flags;
        auto* pixels = reinterpret_cast<uint8_t*>(g_NativeUi) + sizeof(Header);
        for (UINT y = 0; y < height; ++y)
            std::memcpy(pixels + size_t(y) * width * 4u,
                static_cast<const uint8_t*>(locked.pBits) + size_t(y) * locked.Pitch, width * 4u);
        for (UINT y = 0; y < height; y += 4)
            for (UINT x = 0; x < width; x += 4)
                if (pixels[(size_t(y) * width + x) * 4u + 3u]) ++g_NativeUi->contentSamples;
        MemoryBarrier();
        InterlockedIncrement(&g_NativeUi->sequence);
    }
    g_NativeUiReadback->UnlockRect();
}

static const int TARGET_FPS = 30;
static const int TOTAL_SECONDS = 20;
static const int DEFAULT_TOTAL_FRAMES_TO_RECORD = TARGET_FPS * TOTAL_SECONDS; // 600 frames
static int g_TotalFramesToRecord = DEFAULT_TOTAL_FRAMES_TO_RECORD;

static bool g_RecordingRequested = false;
static bool g_CpuOverlayConfigured = false;
static bool g_CpuOverlayEnabled = false;
static bool g_CaptureTargetConfigured = false;
static bool g_CaptureActiveTarget = false;
static std::vector<uint8_t> g_CpuOverlayFrame;
static std::vector<uint8_t> g_LastGoodFrame;
static bool g_LastSourceValid = false;
static bool g_ReuseLastGoodDuringRecording = false;
static uint64_t g_CaptureCallCount = 0;
static uint32_t g_FirstFrameLockCount = 0;
static bool g_LastFrameReused = false;
static uint32_t g_LastLitSamples = 0;
static uint32_t g_LastSampleCount = 0;
static uint64_t g_LastSampleEnergy = 0;
static HRESULT g_LastCaptureHr = S_OK;
static UINT g_LastCaptureWidth = 0;
static UINT g_LastCaptureHeight = 0;
static D3DFORMAT g_LastCaptureFormat = D3DFMT_UNKNOWN;
static int g_LastCapturePitch = 0;

static const char* FFMPEG_EXE = "C:\\Users\\nbrys\\AppData\\Local\\Microsoft\\WinGet\\Packages\\Gyan.FFmpeg_Microsoft.Winget.Source_8wekyb3d8bbwe\\ffmpeg-8.1.1-full_build\\bin\\ffmpeg.exe";
static const char* TEMP_VIDEO_PATH = "D:\\code\\blvr\\artifacts\\blvr_temp_video.mp4";
static const char* FINAL_OUTPUT_PATH = "D:\\code\\blvr\\artifacts\\blvr-sim-camera-proof.mp4";
static const char* WAV_PATH = "D:\\code\\blvr\\artifacts\\blvr_audio.wav";

static void WriteCaptureTelemetry(const char* stage) {
    if (!Telemetry_Enabled()) return;
    char fields[1400] = {};
    std::snprintf(fields, sizeof(fields),
        "\"call\":%llu,\"stage\":\"%s\",\"recording\":%d,\"requested\":%d,\"finished\":%d,"
        "\"recorded\":%d,\"total\":%d,\"source\":\"%s\",\"source_valid\":%d,\"reused_last_good\":%d,"
        "\"lit\":%u,\"samples\":%u,\"energy\":%llu,\"hr\":%lu,"
        "\"width\":%u,\"height\":%u,\"format\":%u,\"pitch\":%d,"
        "\"mailbox_tx\":%llu,\"mailbox_slot\":%u,\"xr_pose_count\":%u,"
        "\"first_lock_count\":%u",
        static_cast<unsigned long long>(g_CaptureCallCount), stage ? stage : "unknown",
        g_RecordingStarted ? 1 : 0, g_RecordingRequested ? 1 : 0,
        g_RecordingFinished ? 1 : 0, g_RecordedFrames, g_TotalFramesToRecord,
        g_CaptureActiveTarget ? "active_target" : "backbuffer",
        g_LastSourceValid ? 1 : 0, g_LastFrameReused ? 1 : 0,
        g_LastLitSamples, g_LastSampleCount,
        static_cast<unsigned long long>(g_LastSampleEnergy),
        static_cast<unsigned long>(g_LastCaptureHr),
        g_LastCaptureWidth, g_LastCaptureHeight,
        static_cast<unsigned>(g_LastCaptureFormat), g_LastCapturePitch,
        static_cast<unsigned long long>(g_BlvrFrameTransaction), g_BlvrFrameSlot,
        XrHost::Get().GetPoseUpdateCount(), g_FirstFrameLockCount);
    Telemetry_Write("capture", fields);
}

struct CpuPoint {
    float x;
    float y;
};

struct CpuColor {
    uint8_t b;
    uint8_t g;
    uint8_t r;
};

static void CpuPutPixel(
    std::vector<uint8_t>& pixels, uint32_t width, uint32_t height,
    int x, int y, CpuColor color) {
    if (x < 0 || y < 0 || static_cast<uint32_t>(x) >= width ||
        static_cast<uint32_t>(y) >= height) return;
    uint8_t* pixel = pixels.data() +
        (static_cast<size_t>(y) * width + static_cast<size_t>(x)) * 4u;
    pixel[0] = color.b;
    pixel[1] = color.g;
    pixel[2] = color.r;
    pixel[3] = 0;
}

static void CpuFillTriangle(
    std::vector<uint8_t>& pixels, uint32_t width, uint32_t height,
    CpuPoint a, CpuPoint b, CpuPoint c, CpuColor color) {
    const float area = (b.x - a.x) * (c.y - a.y) -
                       (b.y - a.y) * (c.x - a.x);
    if (fabsf(area) < 0.01f) return;
    const int minX = std::max(0, static_cast<int>(floorf(std::min({a.x, b.x, c.x}))));
    const int maxX = std::min(static_cast<int>(width) - 1,
                              static_cast<int>(ceilf(std::max({a.x, b.x, c.x}))));
    const int minY = std::max(0, static_cast<int>(floorf(std::min({a.y, b.y, c.y}))));
    const int maxY = std::min(static_cast<int>(height) - 1,
                              static_cast<int>(ceilf(std::max({a.y, b.y, c.y}))));
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            const CpuPoint p{static_cast<float>(x) + 0.5f,
                             static_cast<float>(y) + 0.5f};
            const float e0 = (b.x - a.x) * (p.y - a.y) -
                             (b.y - a.y) * (p.x - a.x);
            const float e1 = (c.x - b.x) * (p.y - b.y) -
                             (c.y - b.y) * (p.x - b.x);
            const float e2 = (a.x - c.x) * (p.y - c.y) -
                             (a.y - c.y) * (p.x - c.x);
            if ((e0 >= 0.0f && e1 >= 0.0f && e2 >= 0.0f) ||
                (e0 <= 0.0f && e1 <= 0.0f && e2 <= 0.0f)) {
                CpuPutPixel(pixels, width, height, x, y, color);
            }
        }
    }
}

static void CpuDrawSegment(
    std::vector<uint8_t>& pixels, uint32_t width, uint32_t height,
    CpuPoint a, CpuPoint b, float thickness, CpuColor color) {
    const float minX = std::min(a.x, b.x) - thickness;
    const float maxX = std::max(a.x, b.x) + thickness;
    const float minY = std::min(a.y, b.y) - thickness;
    const float maxY = std::max(a.y, b.y) + thickness;
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const float lengthSq = dx * dx + dy * dy;
    const float radiusSq = thickness * thickness * 0.25f;
    for (int y = std::max(0, static_cast<int>(floorf(minY)));
         y <= std::min(static_cast<int>(height) - 1, static_cast<int>(ceilf(maxY))); ++y) {
        for (int x = std::max(0, static_cast<int>(floorf(minX)));
             x <= std::min(static_cast<int>(width) - 1, static_cast<int>(ceilf(maxX))); ++x) {
            float t = lengthSq > 0.01f
                ? ((static_cast<float>(x) - a.x) * dx +
                   (static_cast<float>(y) - a.y) * dy) / lengthSq : 0.0f;
            t = std::max(0.0f, std::min(1.0f, t));
            const float px = a.x + t * dx - static_cast<float>(x);
            const float py = a.y + t * dy - static_cast<float>(y);
            if (px * px + py * py <= radiusSq) CpuPutPixel(pixels, width, height, x, y, color);
        }
    }
}

static void CpuDrawDisc(
    std::vector<uint8_t>& pixels, uint32_t width, uint32_t height,
    CpuPoint center, float radius, CpuColor color) {
    const float radiusSq = radius * radius;
    const int minX = std::max(0, static_cast<int>(floorf(center.x - radius)));
    const int maxX = std::min(static_cast<int>(width) - 1, static_cast<int>(ceilf(center.x + radius)));
    const int minY = std::max(0, static_cast<int>(floorf(center.y - radius)));
    const int maxY = std::min(static_cast<int>(height) - 1, static_cast<int>(ceilf(center.y + radius)));
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            const float dx = static_cast<float>(x) - center.x;
            const float dy = static_cast<float>(y) - center.y;
            if (dx * dx + dy * dy <= radiusSq) CpuPutPixel(pixels, width, height, x, y, color);
        }
    }
}

static void CpuDrawFirstPersonOverlay(
    std::vector<uint8_t>& pixels, uint32_t width, uint32_t height) {
    if (!g_CpuOverlayEnabled || pixels.empty()) return;
    ControllerState controllers{};
    Pose6DoF head{};
    if (!XrHost::Get().GetControllerState(controllers) || !controllers.runtimeActive ||
        !controllers.left.tracked || !controllers.right.tracked) return;
    const bool haveHead = XrHost::Get().GetHeadPose(head);
    const float cx = width * 0.5f;
    const float shoulderY = height * 0.54f;
    const float focal = static_cast<float>(std::min(width, height)) * 0.42f;
    auto project = [&](const Pose6DoF& pose, float xBias, float yBias) {
        const float relX = pose.pos[0] - (haveHead ? head.pos[0] : 0.0f) + xBias;
        const float relY = pose.pos[1] - (haveHead ? head.pos[1] : 0.0f) + yBias;
        const float depth = std::max(0.28f, -pose.pos[2] + 0.18f);
        const float scale = focal / depth;
        return CpuPoint{cx + relX * scale, shoulderY - relY * scale};
    };
    const CpuPoint leftWrist = project(controllers.left.pose, 0.0f, 0.0f);
    const CpuPoint rightWrist = project(controllers.right.pose, 0.0f, 0.0f);
    const CpuPoint leftShoulder{cx - width * 0.105f, shoulderY + height * 0.045f};
    const CpuPoint rightShoulder{cx + width * 0.105f, shoulderY + height * 0.045f};
    const CpuPoint leftElbow{
        (leftShoulder.x + leftWrist.x) * 0.5f - width * 0.025f,
        (leftShoulder.y + leftWrist.y) * 0.5f + height * 0.025f};
    const CpuPoint rightElbow{
        (rightShoulder.x + rightWrist.x) * 0.5f + width * 0.025f,
        (rightShoulder.y + rightWrist.y) * 0.5f + height * 0.025f};

    const CpuColor sleeve{32, 26, 38};
    const CpuColor sleeveEdge{42, 47, 108};
    const CpuColor skin{68, 97, 180};
    const CpuColor skinLight{103, 157, 235};
    const CpuColor metal{220, 215, 208};
    const CpuColor darkMetal{78, 66, 62};
    const CpuColor wood{28, 37, 79};
    const CpuColor woodLight{43, 81, 150};
    const bool axeBeat = controllers.right.x || controllers.right.trigger > 0.75f;
    const bool guitarBeat = controllers.right.y || controllers.left.y;

    CpuDrawSegment(pixels, width, height, leftShoulder, leftElbow, height * 0.095f, sleeve);
    CpuDrawSegment(pixels, width, height, leftShoulder, leftElbow, height * 0.052f, sleeveEdge);
    CpuDrawSegment(pixels, width, height, leftElbow, leftWrist, height * 0.078f, skin);
    CpuDrawSegment(pixels, width, height, leftElbow, leftWrist, height * 0.033f, skinLight);
    CpuDrawSegment(pixels, width, height, rightShoulder, rightElbow, height * 0.095f, sleeve);
    CpuDrawSegment(pixels, width, height, rightShoulder, rightElbow, height * 0.052f, sleeveEdge);
    CpuDrawSegment(pixels, width, height, rightElbow, rightWrist, height * 0.078f, skin);
    CpuDrawSegment(pixels, width, height, rightElbow, rightWrist, height * 0.033f, skinLight);
    CpuDrawDisc(pixels, width, height, leftWrist, height * 0.050f, skin);
    CpuDrawDisc(pixels, width, height, rightWrist, height * 0.050f, skin);

    const float guitarX = leftWrist.x - width * 0.125f;
    const float guitarY = leftWrist.y + height * 0.115f;
    CpuDrawDisc(pixels, width, height, {guitarX, guitarY}, height * 0.12f,
                guitarBeat ? CpuColor{43, 77, 218} : wood);
    CpuDrawDisc(pixels, width, height, {guitarX, guitarY - height * 0.018f}, height * 0.073f,
                guitarBeat ? CpuColor{56, 160, 255} : woodLight);
    CpuDrawSegment(pixels, width, height,
                   {guitarX + width * 0.025f, guitarY - height * 0.045f},
                   {leftWrist.x + width * 0.045f, leftWrist.y - height * 0.030f},
                   height * 0.040f, wood);
    CpuDrawSegment(pixels, width, height,
                   {guitarX + width * 0.025f, guitarY - height * 0.045f},
                   {leftWrist.x + width * 0.045f, leftWrist.y - height * 0.030f},
                   height * 0.012f, metal);

    const float axeX = rightWrist.x + width * 0.13f;
    const float axeY = rightWrist.y - height * 0.15f;
    CpuDrawSegment(pixels, width, height, rightWrist, {axeX, axeY}, height * 0.028f, darkMetal);
    CpuDrawSegment(pixels, width, height, rightWrist, {axeX, axeY}, height * 0.010f, metal);
    const CpuColor axeColor = axeBeat ? CpuColor{42, 92, 255} : CpuColor{220, 205, 186};
    CpuFillTriangle(pixels, width, height, {axeX, axeY},
                   {axeX + width * 0.105f, axeY - height * 0.090f},
                   {axeX + width * 0.125f, axeY + height * 0.035f}, axeColor);
    CpuFillTriangle(pixels, width, height, {axeX + width * 0.008f, axeY},
                   {axeX + width * 0.075f, axeY - height * 0.115f},
                   {axeX + width * 0.040f, axeY + height * 0.020f}, darkMetal);
}

void VideoCapture_Init() {
    g_RecordedFrames = 0;
    g_RecordingStarted = false;
    g_RecordingFinished = false;
    g_RecordingRequested = false;
    g_FfmpegPipe = nullptr;
    g_pSysMemSurface = nullptr;
    g_BlvrFrameDescriptorMapping = nullptr;
    g_BlvrFrameDescriptor = nullptr;
    g_BlvrFrameCpuMapping = nullptr;
    g_BlvrFrameCpu = nullptr;
    g_BlvrFrameTransaction = 0;
    g_BlvrFrameSlot = 0;
    g_CpuOverlayFrame.clear();
    g_LastGoodFrame.clear();
    g_LastSourceValid = false;
    g_ReuseLastGoodDuringRecording = false;
    g_CaptureCallCount = 0;
    g_FirstFrameLockCount = 0;
    g_LastFrameReused = false;
    g_LastLitSamples = 0;
    g_LastSampleCount = 0;
    g_LastSampleEnergy = 0;
    g_LastCaptureHr = S_OK;
    g_LastCaptureWidth = 0;
    g_LastCaptureHeight = 0;
    g_LastCaptureFormat = D3DFMT_UNKNOWN;
    g_LastCapturePitch = 0;
    g_CpuOverlayConfigured = true;
    char overlayEnv[16] = {};
    GetEnvironmentVariableA("BLVR_FIRST_PERSON_OVERLAY", overlayEnv, sizeof(overlayEnv));
    g_CpuOverlayEnabled = overlayEnv[0] == '1' || _stricmp(overlayEnv, "true") == 0;
    char reuseEnv[16] = {};
    GetEnvironmentVariableA("BLVR_CAPTURE_REUSE_LAST_GOOD", reuseEnv, sizeof(reuseEnv));
    g_ReuseLastGoodDuringRecording = reuseEnv[0] == '1' || _stricmp(reuseEnv, "true") == 0;
    g_TotalFramesToRecord = DEFAULT_TOTAL_FRAMES_TO_RECORD;
    char durationEnv[16] = {};
    GetEnvironmentVariableA("BLVR_CAPTURE_SECONDS", durationEnv, sizeof(durationEnv));
    if (durationEnv[0]) {
        const int seconds = std::max(1, std::min(TOTAL_SECONDS, atoi(durationEnv)));
        g_TotalFramesToRecord = TARGET_FPS * seconds;
    }
    Log("VideoCapture: capture duration=%.1fs", (float)g_TotalFramesToRecord / TARGET_FPS);
    Log("VideoCapture: CPU first-person overlay=%d", g_CpuOverlayEnabled ? 1 : 0);
    Log("VideoCapture: reuse last-good during recording=%d", g_ReuseLastGoodDuringRecording ? 1 : 0);
    char bridgeEnv[16] = {};
    GetEnvironmentVariableA("BLVR_XR_BRIDGE", bridgeEnv, sizeof(bridgeEnv));
    if (bridgeEnv[0] == '1' || _stricmp(bridgeEnv, "true") == 0) {
        g_BlvrFrameDescriptorMapping = CreateFileMappingW(
            INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
            0, static_cast<DWORD>(sizeof(BlvrFrameTransport::FrameBridge)),
            BlvrFrameTransport::DescriptorName);
        if (g_BlvrFrameDescriptorMapping) {
            g_BlvrFrameDescriptor = static_cast<BlvrFrameTransport::FrameBridge*>(
                MapViewOfFile(g_BlvrFrameDescriptorMapping, FILE_MAP_ALL_ACCESS,
                              0, 0, sizeof(BlvrFrameTransport::FrameBridge)));
        }
        g_BlvrFrameCpuMapping = CreateFileMappingW(
            INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
            0, static_cast<DWORD>(BlvrFrameTransport::CpuMappingBytes),
            BlvrFrameTransport::CpuMailboxName);
        if (g_BlvrFrameCpuMapping) {
            g_BlvrFrameCpu = static_cast<BlvrFrameTransport::CpuFrameMailbox*>(
                MapViewOfFile(g_BlvrFrameCpuMapping, FILE_MAP_ALL_ACCESS,
                              0, 0, static_cast<SIZE_T>(BlvrFrameTransport::CpuMappingBytes)));
        }
        if (g_BlvrFrameCpu) {
            ZeroMemory(g_BlvrFrameCpu, static_cast<SIZE_T>(BlvrFrameTransport::CpuMappingBytes));
            g_BlvrFrameCpu->magic = BlvrFrameTransport::CpuMagic;
            g_BlvrFrameCpu->version = BlvrFrameTransport::CpuVersion;
            g_BlvrFrameCpu->headerBytes = sizeof(BlvrFrameTransport::CpuFrameMailbox);
            g_BlvrFrameCpu->slotCount = BlvrFrameTransport::SlotCount;
            g_BlvrFrameCpu->maxWidth = BlvrFrameTransport::MaxWidth;
            g_BlvrFrameCpu->maxHeight = BlvrFrameTransport::MaxHeight;
            g_BlvrFrameCpu->bytesPerPixel = BlvrFrameTransport::BytesPerPixel;
            g_BlvrFrameCpu->eyeCount = BlvrFrameTransport::EyeCount;
            g_BlvrFrameCpu->mappingBytes = BlvrFrameTransport::CpuMappingBytes;
        }
        if (g_BlvrFrameDescriptor) {
            ZeroMemory(g_BlvrFrameDescriptor, sizeof(BlvrFrameTransport::FrameBridge));
            LARGE_INTEGER counter{};
            QueryPerformanceCounter(&counter);
            const uint64_t epoch = static_cast<uint64_t>(counter.QuadPart) ^
                (static_cast<uint64_t>(GetCurrentProcessId()) << 32u) ^ GetTickCount64();
            g_BlvrFrameDescriptor->magic = BlvrFrameTransport::Magic;
            g_BlvrFrameDescriptor->version = BlvrFrameTransport::Version;
            g_BlvrFrameDescriptor->structBytes = sizeof(BlvrFrameTransport::FrameBridge);
            g_BlvrFrameDescriptor->slotCount = BlvrFrameTransport::SlotCount;
            g_BlvrFrameDescriptor->producerPid = GetCurrentProcessId();
            g_BlvrFrameDescriptor->producerEpoch = epoch ? epoch : 1u;
            g_BlvrFrameDescriptor->resourceSetId = g_BlvrFrameDescriptor->producerEpoch ^ 0x424C56524652414Dull;
            g_BlvrFrameDescriptor->resourceGeneration = 1u;
            g_BlvrFrameDescriptor->mappingBytes = sizeof(BlvrFrameTransport::FrameBridge);
            g_BlvrFrameDescriptor->format = BlvrFrameTransport::PixelFormat::B8G8R8X8Unorm;
            // Fail closed until both eyes have actually rendered from one
            // scene transaction. A mono backbuffer is never presented to both
            // eyes as if it were stereo.
            g_BlvrFrameDescriptor->flags = BlvrFrameTransport::Running |
                BlvrFrameTransport::CpuBgraMailbox;
            g_BlvrFrameDescriptor->presentationMode = BlvrFrameTransport::PresentationMode::WorldStereo;
            g_BlvrFrameDescriptor->uiEye = 0u;
            g_BlvrFrameDescriptor->publicationSequence = 2;
        }
        Log("VideoCapture: BLVR compositor frame transport=%d descriptor=%d",
            g_BlvrFrameCpu ? 1 : 0, g_BlvrFrameDescriptor ? 1 : 0);
    }
}

bool VideoCapture_IsStarted() {
    return g_RecordingStarted || g_RecordingRequested;
}

bool VideoCapture_IsFinished() {
    return g_RecordingFinished;
}

int VideoCapture_GetRecordedFrames() {
    return g_RecordedFrames;
}

void VideoCapture_Start(int width, int height) {
    if (g_RecordingStarted || g_RecordingFinished || g_RecordingRequested) return;
    g_RecordingRequested = true;
    Log("VideoCapture: Recording requested. Will initialize on next frame.");
}

static void ReleaseStereoSurfaces() {
    if (g_StereoResolveSurface) {
        g_StereoResolveSurface->Release();
        g_StereoResolveSurface = nullptr;
    }
    if (g_StereoScaledSurface) {
        g_StereoScaledSurface->Release();
        g_StereoScaledSurface = nullptr;
    }
    if (g_StereoReadbackSurface) {
        g_StereoReadbackSurface->Release();
        g_StereoReadbackSurface = nullptr;
    }
    g_StereoSourceWidth = 0;
    g_StereoSourceHeight = 0;
    g_StereoTargetWidth = 0;
    g_StereoTargetHeight = 0;
    g_StereoFormat = D3DFMT_UNKNOWN;
    g_StereoSamples = D3DMULTISAMPLE_NONE;
    g_StereoSampleQuality = 0;
}

static void ReleaseMirrorSurfaces() {
    if (g_MirrorPending) g_MirrorPending->Release();
    if (g_MirrorPublished) g_MirrorPublished->Release();
    g_MirrorPending = g_MirrorPublished = nullptr;
    g_MirrorWidth = g_MirrorHeight = 0;
    g_MirrorFormat = D3DFMT_UNKNOWN;
    g_MirrorPendingFrame = g_MirrorPublishedFrame = g_MirrorPublishedTick = 0;
}

static void CaptureMirrorCandidate(IDirect3DDevice9* device,
    IDirect3DSurface9* source, uint64_t sourceFrameId) {
    g_MirrorPendingFrame = 0;
    D3DSURFACE_DESC desc{};
    if (FAILED(source->GetDesc(&desc)) ||
        (desc.Format != D3DFMT_A8R8G8B8 && desc.Format != D3DFMT_X8R8G8B8)) return;
    if (g_MirrorWidth != desc.Width || g_MirrorHeight != desc.Height ||
        g_MirrorFormat != desc.Format || !g_MirrorPending || !g_MirrorPublished) {
        ReleaseMirrorSurfaces();
        if (FAILED(device->CreateRenderTarget(desc.Width, desc.Height, desc.Format,
            D3DMULTISAMPLE_NONE, 0, FALSE, &g_MirrorPending, nullptr)) ||
            FAILED(device->CreateRenderTarget(desc.Width, desc.Height, desc.Format,
            D3DMULTISAMPLE_NONE, 0, FALSE, &g_MirrorPublished, nullptr))) {
            ReleaseMirrorSurfaces(); return;
        }
        g_MirrorWidth = desc.Width; g_MirrorHeight = desc.Height;
        g_MirrorFormat = desc.Format;
    }
    if (SUCCEEDED(device->StretchRect(source, nullptr, g_MirrorPending, nullptr, D3DTEXF_NONE)))
        g_MirrorPendingFrame = sourceFrameId;
}

bool VideoCapture_PresentMirror(IDirect3DDevice9* device) {
    if (!device || !g_MirrorPublishedFrame || !g_MirrorPublished ||
        GetTickCount64() - g_MirrorPublishedTick > 1000) return false;
    IDirect3DSurface9* backbuffer = nullptr;
    if (FAILED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backbuffer))) return false;
    const HRESULT hr = device->StretchRect(g_MirrorPublished, nullptr, backbuffer, nullptr, D3DTEXF_LINEAR);
    backbuffer->Release();
    static unsigned logs = 0;
    if (FAILED(hr)) {
        if (logs++ < 3) Log("DesktopMirror: copy failed hr=0x%08X", hr);
        return false;
    }
    if (logs++ == 0) Log("DesktopMirror: presenting completed first-person left eye, sourceFrame=%llu",
        static_cast<unsigned long long>(g_MirrorPublishedFrame));
    return true;
}

void VideoCapture_OnDeviceReset() {
    ReleaseMirrorSurfaces();
    ReleaseNativeUiSurfaces();
    ReleaseStereoSurfaces();
    if (g_StereoSurfaceDevice) {
        g_StereoSurfaceDevice->Release();
        g_StereoSurfaceDevice = nullptr;
    }
    g_StereoEyeBuffers[0].valid = false;
    g_StereoEyeBuffers[1].valid = false;
}

static bool PrepareStereoSurfaces(
    IDirect3DDevice9* device,
    const D3DSURFACE_DESC& sourceDesc,
    uint32_t targetWidth,
    uint32_t targetHeight) {
    if (g_StereoSurfaceDevice != device) {
        VideoCapture_OnDeviceReset();
        g_StereoSurfaceDevice = device;
        g_StereoSurfaceDevice->AddRef();
    }

    const bool needsResolve = sourceDesc.MultiSampleType != D3DMULTISAMPLE_NONE;
    const bool needsScale = targetWidth != sourceDesc.Width || targetHeight != sourceDesc.Height;
    const bool layoutMatches =
        g_StereoSourceWidth == sourceDesc.Width &&
        g_StereoSourceHeight == sourceDesc.Height &&
        g_StereoFormat == sourceDesc.Format &&
        g_StereoSamples == sourceDesc.MultiSampleType &&
        g_StereoSampleQuality == sourceDesc.MultiSampleQuality &&
        g_StereoTargetWidth == targetWidth &&
        g_StereoTargetHeight == targetHeight &&
        (!needsResolve || g_StereoResolveSurface) &&
        (!needsScale || g_StereoScaledSurface) &&
        g_StereoReadbackSurface;
    if (layoutMatches) return true;

    ReleaseStereoSurfaces();
    HRESULT hr = S_OK;
    if (needsResolve) {
        hr = device->CreateRenderTarget(
            sourceDesc.Width, sourceDesc.Height, sourceDesc.Format,
            D3DMULTISAMPLE_NONE, 0, FALSE, &g_StereoResolveSurface, nullptr);
        if (FAILED(hr) || !g_StereoResolveSurface) {
            Log("StereoCapture: resolve surface creation failed hr=0x%08X size=%ux%u format=%u",
                hr, sourceDesc.Width, sourceDesc.Height,
                static_cast<unsigned>(sourceDesc.Format));
            ReleaseStereoSurfaces();
            return false;
        }
    }
    if (needsScale) {
        hr = device->CreateRenderTarget(
            targetWidth, targetHeight, sourceDesc.Format,
            D3DMULTISAMPLE_NONE, 0, FALSE, &g_StereoScaledSurface, nullptr);
        if (FAILED(hr) || !g_StereoScaledSurface) {
            Log("StereoCapture: scaled surface creation failed hr=0x%08X size=%ux%u",
                hr, targetWidth, targetHeight);
            ReleaseStereoSurfaces();
            return false;
        }
    }
    hr = device->CreateOffscreenPlainSurface(
        targetWidth, targetHeight, sourceDesc.Format,
        D3DPOOL_SYSTEMMEM, &g_StereoReadbackSurface, nullptr);
    if (FAILED(hr) || !g_StereoReadbackSurface) {
        Log("StereoCapture: readback surface creation failed hr=0x%08X size=%ux%u format=%u",
            hr, targetWidth, targetHeight,
            static_cast<unsigned>(sourceDesc.Format));
        ReleaseStereoSurfaces();
        return false;
    }

    g_StereoSourceWidth = sourceDesc.Width;
    g_StereoSourceHeight = sourceDesc.Height;
    g_StereoTargetWidth = targetWidth;
    g_StereoTargetHeight = targetHeight;
    g_StereoFormat = sourceDesc.Format;
    g_StereoSamples = sourceDesc.MultiSampleType;
    g_StereoSampleQuality = sourceDesc.MultiSampleQuality;
    return true;
}

// The Buddha world pass renders RGBA16F. The shared compositor mailbox is
// BGRA8; convert its linear HDR channels to display values at that boundary.
// A lookup avoids millions of per-frame half-float and gamma operations.
static const uint8_t* HalfToDisplayTable() {
    static uint8_t values[65536];
    static bool initialized = false;
    if (!initialized) {
        for (uint32_t bits = 0; bits < 65536; ++bits) {
            const uint32_t exponent = (bits >> 10) & 31u;
            const uint32_t mantissa = bits & 1023u;
            float linear = exponent == 0
                ? std::ldexp(static_cast<float>(mantissa), -24)
                : std::ldexp(1.0f + static_cast<float>(mantissa) / 1024.0f,
                             static_cast<int>(exponent) - 15);
            if ((bits & 0x8000u) || exponent == 31u) linear = 0.0f;
            // Preserve scene contrast while rolling HDR highlights into SDR.
            linear = linear / (1.0f + linear);
            const float srgb = linear <= 0.0031308f ? linear * 12.92f
                : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
            values[bits] = static_cast<uint8_t>(std::clamp(srgb * 255.0f, 0.0f, 255.0f));
        }
        initialized = true;
    }
    return values;
}

static bool CaptureEyeSource(
    IDirect3DDevice9* device,
    uint32_t eye,
    uint64_t sourceFrameId,
    uint64_t poseSequence,
    int64_t renderedDisplayTime,
    bool backbuffer) {
    blvr_perf::Scope cost(eye==0?blvr_perf::LeftCapture:blvr_perf::RightCapture);
    if (eye >= BlvrFrameTransport::EyeCount || !device ||
        !g_BlvrFrameCpu || !g_BlvrFrameDescriptor || sourceFrameId == 0 ||
        poseSequence == 0 || renderedDisplayTime == 0) {
        return false;
    }

    StereoEyeBuffer& output = g_StereoEyeBuffers[eye];
    output.valid = false;
    IDirect3DSurface9* source = nullptr;
    HRESULT hr = backbuffer
        ? device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &source)
        : device->GetRenderTarget(0, &source);
    if (FAILED(hr) || !source) {
        if (g_StereoCaptureFailureLogs++ < 6) {
            Log("StereoCapture: eye=%u GetRenderTarget failed hr=0x%08X",
                eye, hr);
        }
        return false;
    }

    D3DSURFACE_DESC sourceDesc{};
    hr = source->GetDesc(&sourceDesc);
    if (FAILED(hr) || sourceDesc.Width == 0 || sourceDesc.Height == 0 ||
        (sourceDesc.Format != D3DFMT_X8R8G8B8 && sourceDesc.Format != D3DFMT_A8R8G8B8 &&
         sourceDesc.Format != D3DFMT_A16B16G16R16F)) {
        if (g_StereoCaptureFailureLogs++ < 6) {
            Log("StereoCapture: eye=%u unsupported target hr=0x%08X size=%ux%u format=%u samples=%u",
                eye, hr, sourceDesc.Width, sourceDesc.Height,
                static_cast<unsigned>(sourceDesc.Format),
                static_cast<unsigned>(sourceDesc.MultiSampleType));
        }
        source->Release();
        return false;
    }

    const double scale = std::min({
        1.0,
        static_cast<double>(BlvrFrameTransport::MaxWidth) / sourceDesc.Width,
        static_cast<double>(BlvrFrameTransport::MaxHeight) / sourceDesc.Height});
    const uint32_t targetWidth = std::max(1u, static_cast<uint32_t>(sourceDesc.Width * scale));
    const uint32_t targetHeight = std::max(1u, static_cast<uint32_t>(sourceDesc.Height * scale));
    if (!PrepareStereoSurfaces(device, sourceDesc, targetWidth, targetHeight)) {
        source->Release();
        return false;
    }

    IDirect3DSurface9* resolved = source;
    if (sourceDesc.MultiSampleType != D3DMULTISAMPLE_NONE) {
        hr = device->StretchRect(source, nullptr, g_StereoResolveSurface, nullptr, D3DTEXF_NONE);
        if (FAILED(hr)) {
            if (g_StereoCaptureFailureLogs++ < 6) {
                Log("StereoCapture: eye=%u multisample resolve failed hr=0x%08X",
                    eye, hr);
            }
            source->Release();
            return false;
        }
        resolved = g_StereoResolveSurface;
    }

    IDirect3DSurface9* readbackSource = resolved;
    if (targetWidth != sourceDesc.Width || targetHeight != sourceDesc.Height) {
        hr = device->StretchRect(resolved, nullptr, g_StereoScaledSurface, nullptr, D3DTEXF_LINEAR);
        if (FAILED(hr)) {
            if (g_StereoCaptureFailureLogs++ < 6) {
                Log("StereoCapture: eye=%u scale blit failed hr=0x%08X",
                    eye, hr);
            }
            source->Release();
            return false;
        }
        readbackSource = g_StereoScaledSurface;
    }

    { blvr_perf::Scope readbackCost(blvr_perf::Readback);
      hr = device->GetRenderTargetData(readbackSource, g_StereoReadbackSurface); }
    if (SUCCEEDED(hr) && !backbuffer && eye == 0)
        CaptureMirrorCandidate(device, readbackSource, sourceFrameId);
    source->Release();
    if (FAILED(hr)) {
        if (g_StereoCaptureFailureLogs++ < 6) {
            Log("StereoCapture: eye=%u GetRenderTargetData failed hr=0x%08X",
                eye, hr);
        }
        return false;
    }

    D3DLOCKED_RECT locked{};
    const bool hdr = sourceDesc.Format == D3DFMT_A16B16G16R16F;
    const uint32_t sourcePixelBytes = hdr ? 8u : 4u;
    { blvr_perf::Scope lockCost(blvr_perf::Readback);
      hr = g_StereoReadbackSurface->LockRect(&locked, nullptr, D3DLOCK_READONLY); }
    if (FAILED(hr) || !locked.pBits || locked.Pitch < static_cast<INT>(targetWidth * sourcePixelBytes)) {
        if (SUCCEEDED(hr)) g_StereoReadbackSurface->UnlockRect();
        if (g_StereoCaptureFailureLogs++ < 6) {
            Log("StereoCapture: eye=%u readback lock failed hr=0x%08X pitch=%d expected=%u",
                eye, hr, locked.Pitch, targetWidth * 4u);
        }
        return false;
    }

    blvr_perf::Scope convertCost(blvr_perf::Convert);
    const size_t rowBytes = static_cast<size_t>(targetWidth) * BlvrFrameTransport::BytesPerPixel;
    output.pixels.resize(rowBytes * targetHeight);
    const uint8_t* display = hdr ? HalfToDisplayTable() : nullptr;
    if(!hdr && static_cast<size_t>(locked.Pitch)==rowBytes) {
        memcpy(output.pixels.data(),locked.pBits,rowBytes*targetHeight);
    } else for (uint32_t y = 0; y < targetHeight; ++y) {
        uint8_t* dst = output.pixels.data() + static_cast<size_t>(y) * rowBytes;
        const uint8_t* src = static_cast<const uint8_t*>(locked.pBits) + static_cast<size_t>(y) * locked.Pitch;
        if (hdr) {
            const uint16_t* half = reinterpret_cast<const uint16_t*>(src);
            for (uint32_t x = 0; x < targetWidth; ++x) {
                dst[x * 4u + 0u] = display[half[x * 4u + 2u]];
                dst[x * 4u + 1u] = display[half[x * 4u + 1u]];
                dst[x * 4u + 2u] = display[half[x * 4u + 0u]];
                dst[x * 4u + 3u] = 255u;
            }
        } else {
            memcpy(dst, src, rowBytes);
        }
    }
    g_StereoReadbackSurface->UnlockRect();
    convertCost.stop();

    output.width = targetWidth;
    output.height = targetHeight;
    output.sourceFrameId = sourceFrameId;
    output.poseSequence = poseSequence;
    output.renderedDisplayTime = renderedDisplayTime;
    output.valid = true;
    if (!backbuffer && sourceFrameId == 120u) {
        char dumpEnv[8] = {};
        GetEnvironmentVariableA("BLVR_DUMP_EYES", dumpEnv, sizeof(dumpEnv));
        if (dumpEnv[0] == '1') {
            char path[MAX_PATH] = {};
            sprintf_s(path, "D:\\code\\blvr\\artifacts\\world-%s.bmp", eye == 0 ? "left" : "right");
            FILE* file = nullptr;
            if (fopen_s(&file, path, "wb") == 0 && file) {
                BITMAPFILEHEADER header{};
                BITMAPINFOHEADER info{};
                header.bfType = 0x4d42;
                header.bfOffBits = sizeof(header) + sizeof(info);
                header.bfSize = header.bfOffBits + static_cast<DWORD>(output.pixels.size());
                info.biSize = sizeof(info);
                info.biWidth = targetWidth;
                info.biHeight = -static_cast<LONG>(targetHeight);
                info.biPlanes = 1;
                info.biBitCount = 32;
                info.biCompression = BI_RGB;
                fwrite(&header, sizeof(header), 1, file);
                fwrite(&info, sizeof(info), 1, file);
                fwrite(output.pixels.data(), output.pixels.size(), 1, file);
                fclose(file);
                Log("StereoCapture: diagnostic source saved %s", path);
            }
        }
    }
    if (g_StereoCaptureSuccessLogs++ < 6 || (!backbuffer && (sourceFrameId <= 3 || sourceFrameId % 300 == 0))) {
        Log("StereoCapture: eye=%u sourceFrame=%llu pose=%llu displayTime=%lld size=%ux%u format=%u hdrFallback=%d captured",
            eye, static_cast<unsigned long long>(sourceFrameId),
            static_cast<unsigned long long>(poseSequence),
            static_cast<long long>(renderedDisplayTime), targetWidth, targetHeight,
            static_cast<unsigned>(sourceDesc.Format), hdr ? 1 : 0);
    }
    return true;
}

bool VideoCapture_CaptureStereoEye(
    IDirect3DDevice9* device, uint32_t eye, uint64_t sourceFrameId,
    uint64_t poseSequence, int64_t renderedDisplayTime) {
    return CaptureEyeSource(device, eye, sourceFrameId, poseSequence,
                            renderedDisplayTime, false);
}

static bool PublishEyePair(
    uint64_t sourceFrameId,
    uint64_t poseSequence,
    int64_t renderedDisplayTime,
    bool uiQuad) {
    blvr_perf::Scope publishCost(blvr_perf::PairPublish);
    const StereoEyeBuffer& left = g_StereoEyeBuffers[0];
    const StereoEyeBuffer& right = g_StereoEyeBuffers[1];
    if (!g_BlvrFrameCpu || !g_BlvrFrameDescriptor || !left.valid || !right.valid ||
        sourceFrameId == 0 || poseSequence == 0 || renderedDisplayTime == 0 ||
        left.sourceFrameId != sourceFrameId || right.sourceFrameId != sourceFrameId ||
        left.poseSequence != poseSequence || right.poseSequence != poseSequence ||
        left.renderedDisplayTime != renderedDisplayTime ||
        right.renderedDisplayTime != renderedDisplayTime ||
        left.width == 0 || left.height == 0 || left.width != right.width ||
        left.height != right.height) {
        return false;
    }

    const uint32_t width = left.width;
    const uint32_t height = left.height;
    const size_t rowBytes = static_cast<size_t>(width) * BlvrFrameTransport::BytesPerPixel;
    const size_t frameBytes = rowBytes * height;
    if (width > BlvrFrameTransport::MaxWidth || height > BlvrFrameTransport::MaxHeight ||
        frameBytes > BlvrFrameTransport::EyeBytes ||
        left.pixels.size() != frameBytes || right.pixels.size() != frameBytes) {
        return false;
    }

    // Pixel distinction is diagnostic, not a liveness condition. Independent
    // left/right renders legitimately match during fades, fog or a blank wall.
    // The caller already requires both eye draws with one pose/simulation stamp.
    uint32_t changedSamples = 0;
    constexpr uint32_t kSamplesX = 256u;
    constexpr uint32_t kSamplesY = 144u;
    const bool fullDiagnostic=(sourceFrameId%120u)==0;
    for (uint32_t gy = 0; !uiQuad && gy < kSamplesY && (fullDiagnostic||changedSamples<16u); ++gy) {
        const uint32_t y = std::min(height - 1u,
            static_cast<uint32_t>((static_cast<uint64_t>(gy) * height + height / 2u) / kSamplesY));
        for (uint32_t gx = 0; gx < kSamplesX && (fullDiagnostic||changedSamples<16u); ++gx) {
            const uint32_t x = std::min(width - 1u,
                static_cast<uint32_t>((static_cast<uint64_t>(gx) * width + width / 2u) / kSamplesX));
            const size_t offset = (static_cast<size_t>(y) * width + x) * 4u;
            const int db = abs(static_cast<int>(left.pixels[offset + 0]) - right.pixels[offset + 0]);
            const int dg = abs(static_cast<int>(left.pixels[offset + 1]) - right.pixels[offset + 1]);
            const int dr = abs(static_cast<int>(left.pixels[offset + 2]) - right.pixels[offset + 2]);
            if (db + dg + dr >= 12) ++changedSamples;
        }
    }
    if (!uiQuad && changedSamples < 16u) {
        if (g_StereoPairRejectLogs++ < 6u) {
            Log("StereoCapture: low-contrast eye pair sourceFrame=%llu changedSamples=%u/%u; preserving live render",
                static_cast<unsigned long long>(sourceFrameId), changedSamples,
                kSamplesX * kSamplesY);
        }
    }

    const uint32_t slot = (g_BlvrFrameSlot + 1u) % BlvrFrameTransport::SlotCount;
    const uint64_t transaction = ++g_BlvrFrameTransaction;
    auto* slotSequence = &g_BlvrFrameCpu->slotSequence[slot];
    InterlockedExchange(slotSequence, static_cast<LONG>(transaction * 2u + 1u));
    MemoryBarrier();

    auto* slotPixels = reinterpret_cast<uint8_t*>(g_BlvrFrameCpu) +
        sizeof(BlvrFrameTransport::CpuFrameMailbox) +
        static_cast<size_t>(slot) * static_cast<size_t>(BlvrFrameTransport::SlotBytes);
    auto* leftPixels = slotPixels;
    auto* rightPixels = slotPixels + BlvrFrameTransport::EyeBytes;
    memcpy(leftPixels,left.pixels.data(),frameBytes);
    memcpy(rightPixels,right.pixels.data(),frameBytes);
    g_BlvrFrameCpu->slotTransactionId[slot] = transaction;
    MemoryBarrier();
    InterlockedExchange(slotSequence, static_cast<LONG>(transaction * 2u + 2u));

    auto* descriptorSequence = &g_BlvrFrameDescriptor->publicationSequence;
    InterlockedExchange(descriptorSequence, static_cast<LONG>(transaction * 2u + 1u));
    MemoryBarrier();
    g_BlvrFrameDescriptor->producerPid = GetCurrentProcessId();
    g_BlvrFrameDescriptor->width = width;
    g_BlvrFrameDescriptor->height = height;
    g_BlvrFrameDescriptor->contentWidth = width;
    g_BlvrFrameDescriptor->contentHeight = height;
    g_BlvrFrameDescriptor->currentSlot = slot;
    g_BlvrFrameDescriptor->transactionId = transaction;
    g_BlvrFrameDescriptor->heartbeatTickMs = GetTickCount64();
    g_BlvrFrameDescriptor->sourceFrameId[0] = sourceFrameId;
    g_BlvrFrameDescriptor->sourceFrameId[1] = sourceFrameId;
    g_BlvrFrameDescriptor->poseSequence[0] = poseSequence;
    g_BlvrFrameDescriptor->poseSequence[1] = poseSequence;
    g_BlvrFrameDescriptor->renderedDisplayTime[0] = renderedDisplayTime;
    g_BlvrFrameDescriptor->renderedDisplayTime[1] = renderedDisplayTime;
    g_BlvrFrameDescriptor->flags = BlvrFrameTransport::Running |
        BlvrFrameTransport::CpuBgraMailbox |
        BlvrFrameTransport::SameSimulationTick |
        BlvrFrameTransport::PoseStamped;
    if (!uiQuad) {
        g_BlvrFrameDescriptor->flags |= BlvrFrameTransport::Stereo | BlvrFrameTransport::VerifiedDrawSceneStereo;
        if(changedSamples>=16u) g_BlvrFrameDescriptor->flags|=BlvrFrameTransport::EyesDistinct;
    }
    g_BlvrFrameDescriptor->format = BlvrFrameTransport::PixelFormat::B8G8R8X8Unorm;
    g_BlvrFrameDescriptor->presentationMode = uiQuad
        ? BlvrFrameTransport::PresentationMode::UiQuad
        : BlvrFrameTransport::PresentationMode::WorldStereo;
    g_BlvrFrameDescriptor->uiEye = 0u;
    g_BlvrFrameDescriptor->uiReasonFlags = uiQuad
        ? (CameraHook_IsCinematic() ? BlvrFrameTransport::UiReasonCinematic : 2u) : 0u;
    g_BlvrFrameDescriptor->slots[slot].transactionId = transaction;
    g_BlvrFrameSlot = slot;
    MemoryBarrier();
    InterlockedExchange(descriptorSequence, static_cast<LONG>(transaction * 2u + 2u));

    ++g_StereoPairLogCount;
    if (g_StereoPairLogCount <= 6u || (g_StereoPairLogCount % 120u) == 0u) {
        Log("StereoCapture: published %s transaction=%llu sourceFrame=%llu pose=%llu displayTime=%lld size=%ux%u parallaxSamples=%u/%u",
            uiQuad ? "ui-quad" : "world-stereo",
            static_cast<unsigned long long>(transaction),
            static_cast<unsigned long long>(sourceFrameId),
            static_cast<unsigned long long>(poseSequence),
            static_cast<long long>(renderedDisplayTime), width, height,
            changedSamples, kSamplesX * kSamplesY);
    }
    return true;
}

bool VideoCapture_PublishStereoPair(uint64_t sourceFrameId,
    uint64_t poseSequence, int64_t renderedDisplayTime) {
    if (!PublishEyePair(sourceFrameId, poseSequence, renderedDisplayTime, false)) return false;
    if (g_MirrorPendingFrame == sourceFrameId) {
        std::swap(g_MirrorPending, g_MirrorPublished);
        g_MirrorPublishedFrame = sourceFrameId;
        g_MirrorPublishedTick = GetTickCount64();
        g_MirrorPendingFrame = 0;
    }
    return true;
}

void VideoCapture_PublishUiFrame(IDirect3DDevice9* device, uint64_t sourceFrameId) {
    uint64_t poseSequence = 0;
    int64_t displayTime = 0;
    if (!XrHost::Get().GetRenderTrackingStamp(poseSequence, displayTime) ||
        !CaptureEyeSource(device, 0, sourceFrameId, poseSequence, displayTime, true)) return;
    g_StereoEyeBuffers[1] = g_StereoEyeBuffers[0];
    if (PublishEyePair(sourceFrameId, poseSequence, displayTime, true))
        g_MirrorPublishedFrame = 0; // Title/loading owns the desktop again.
}

void VideoCapture_PollDiagnosticSnapshot(IDirect3DDevice9* device) {
    if (GetFileAttributesA("blvr-snapshot.request") == INVALID_FILE_ATTRIBUTES) return;
    DeleteFileA("blvr-snapshot.request");
    uint64_t pose = 0;
    int64_t displayTime = 0;
    if (!XrHost::Get().GetRenderTrackingStamp(pose, displayTime)) return;
    StereoEyeBuffer saved = std::move(g_StereoEyeBuffers[0]);
    const bool captured = CaptureEyeSource(device, 0, UINT64_MAX, pose, displayTime, true);
    StereoEyeBuffer output = std::move(g_StereoEyeBuffers[0]);
    g_StereoEyeBuffers[0] = std::move(saved);
    if (!captured) return;
    FILE* file = nullptr;
    if (fopen_s(&file, "D:\\code\\blvr\\artifacts\\desktop-diagnostic.bmp", "wb") != 0 || !file) return;
    BITMAPFILEHEADER header{};
    BITMAPINFOHEADER info{};
    header.bfType = 0x4d42;
    header.bfOffBits = sizeof(header) + sizeof(info);
    header.bfSize = header.bfOffBits + static_cast<DWORD>(output.pixels.size());
    info.biSize = sizeof(info);
    info.biWidth = output.width;
    info.biHeight = -static_cast<LONG>(output.height);
    info.biPlanes = 1;
    info.biBitCount = 32;
    fwrite(&header, sizeof(header), 1, file);
    fwrite(&info, sizeof(info), 1, file);
    fwrite(output.pixels.data(), output.pixels.size(), 1, file);
    fclose(file);
    Log("Diagnostic desktop backbuffer saved (not a compositor capture)");
}

void VideoCapture_OnFrame(IDirect3DDevice9* pDevice) {
    if (!pDevice || ((!g_RecordingStarted && !g_RecordingRequested) || g_RecordingFinished)) return;
    ++g_CaptureCallCount;

    if (g_RecordingStarted && g_RecordedFrames >= g_TotalFramesToRecord) {
        VideoCapture_Stop();
        return;
    }

    const bool useStereoEye = g_StereoEyeBuffers[0].valid && !g_StereoEyeBuffers[0].pixels.empty();
    const uint8_t* src = nullptr;
    int rowPitch = 0;
    int lineBytes = 0;
    D3DSURFACE_DESC desc{};
    bool needSysMemUnlock = false;

    if (useStereoEye) {
        desc.Width = g_StereoEyeBuffers[0].width;
        desc.Height = g_StereoEyeBuffers[0].height;
        desc.Format = D3DFMT_X8R8G8B8;
        src = g_StereoEyeBuffers[0].pixels.data();
        rowPitch = static_cast<int>(desc.Width * 4u);
        lineBytes = static_cast<int>(desc.Width * 4u);
        g_LastCaptureWidth = desc.Width;
        g_LastCaptureHeight = desc.Height;
        g_LastCaptureFormat = desc.Format;
        g_LastCapturePitch = rowPitch;
    } else {
        if (!g_CaptureTargetConfigured) {
            char targetEnv[16] = {};
            GetEnvironmentVariableA("BLVR_CAPTURE_ACTIVE_TARGET", targetEnv, sizeof(targetEnv));
            g_CaptureActiveTarget = targetEnv[0] == '1' || _stricmp(targetEnv, "true") == 0;
            g_CaptureTargetConfigured = true;
            Log("VideoCapture: source=%s", g_CaptureActiveTarget ? "active render target" : "presented backbuffer");
        }

        IDirect3DSurface9* pBackBuffer = nullptr;
        // The delayed showcase path can opt into the active render target.  That
        // is the surface containing the current stereo/first-eye scene before the
        // swap-chain has advanced to its next frame.  Keep the presented-backbuffer
        // path as the safe default for front-end and legacy runs.
        HRESULT hr = g_CaptureActiveTarget
            ? pDevice->GetRenderTarget(0, &pBackBuffer)
            : pDevice->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &pBackBuffer);
        g_LastCaptureHr = hr;
        if (FAILED(hr) || !pBackBuffer) {
            WriteCaptureTelemetry("get_target_failed");
            return;
        }

        pBackBuffer->GetDesc(&desc);
        g_LastCaptureWidth = desc.Width;
        g_LastCaptureHeight = desc.Height;
        g_LastCaptureFormat = desc.Format;

        if (!g_pSysMemSurface || g_CaptureWidth != (int)desc.Width || g_CaptureHeight != (int)desc.Height) {
            if (g_pSysMemSurface) {
                g_pSysMemSurface->Release();
                g_pSysMemSurface = nullptr;
            }
            g_CaptureWidth = desc.Width;
            g_CaptureHeight = desc.Height;

            hr = pDevice->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, &g_pSysMemSurface, NULL);
            if (FAILED(hr) || !g_pSysMemSurface) {
                Log("VideoCapture: CreateOffscreenPlainSurface failed: 0x%08X", hr);
                g_LastCaptureHr = hr;
                pBackBuffer->Release();
                WriteCaptureTelemetry("create_sysmem_failed");
                return;
            }
        }

        hr = pDevice->GetRenderTargetData(pBackBuffer, g_pSysMemSurface);
        pBackBuffer->Release();
        g_LastCaptureHr = hr;

        if (FAILED(hr)) return;
        D3DLOCKED_RECT locked;
        if (FAILED(g_pSysMemSurface->LockRect(&locked, NULL, D3DLOCK_READONLY))) return;
        src = reinterpret_cast<const uint8_t*>(locked.pBits);
        rowPitch = locked.Pitch;
        lineBytes = g_CaptureWidth * 4;
        g_LastCapturePitch = rowPitch;
        needSysMemUnlock = true;
    }

            if (g_RecordingStarted && g_RecordedFrames == 0) {
                ++g_FirstFrameLockCount;
                Log("VideoCapture: First frame locked. Desc=%ux%u, Format=%d, Pitch=%d, lineBytes=%d",
                    desc.Width, desc.Height, (int)desc.Format, rowPitch, lineBytes);
            }

            // Detect a cleared/transition surface before publishing it. The
            // DXVK front-end can briefly hand us a valid-size backbuffer whose
            // pixels are black even though the game remains alive. During that
            // transient, reuse the last complete frame for both the BLVR host
            // mailbox and the proof stream. This is compositor continuity; it
            // never writes back into the retail D3D surface.
            uint32_t litSamples = 0;
            uint64_t sampleEnergy = 0;
            const int sampleStepX = std::max(1, static_cast<int>(desc.Width) / 32);
            const int sampleStepY = std::max(1, static_cast<int>(desc.Height) / 18);
            for (UINT y = 0; y < desc.Height; y += static_cast<UINT>(sampleStepY)) {
                const uint8_t* row = src + static_cast<size_t>(y) * rowPitch;
                for (UINT x = 0; x < desc.Width; x += static_cast<UINT>(sampleStepX)) {
                    const uint8_t* pixel = row + static_cast<size_t>(x) * 4u;
                    const uint32_t energy = static_cast<uint32_t>(pixel[0]) +
                        static_cast<uint32_t>(pixel[1]) + static_cast<uint32_t>(pixel[2]);
                    sampleEnergy += energy;
                    if (energy >= 24u) ++litSamples;
                }
            }
            const uint32_t sampleCount = ((desc.Width + sampleStepX - 1u) / sampleStepX) *
                ((desc.Height + sampleStepY - 1u) / sampleStepY);
            g_LastLitSamples = litSamples;
            g_LastSampleCount = sampleCount;
            g_LastSampleEnergy = sampleEnergy;
            const bool sourceValid = sampleCount > 0 &&
                litSamples >= std::max(8u, sampleCount / 100u) &&
                sampleEnergy >= static_cast<uint64_t>(sampleCount) * 18u;
            if (sourceValid != g_LastSourceValid) {
                Log("VideoCapture: source signal valid=%d lit=%u/%u energy=%llu lastGood=%d",
                    sourceValid ? 1 : 0, litSamples, sampleCount,
                    static_cast<unsigned long long>(sampleEnergy),
                    g_LastGoodFrame.empty() ? 0 : 1);
                g_LastSourceValid = sourceValid;
            }

            const uint8_t* output = src;
            int outputPitch = rowPitch;
            g_LastFrameReused = false;
            if (g_CpuOverlayEnabled) {
                const size_t outputBytes = static_cast<size_t>(desc.Width) *
                    static_cast<size_t>(desc.Height) * 4u;
                g_CpuOverlayFrame.resize(outputBytes);
                for (UINT y = 0; y < desc.Height; ++y) {
                    memcpy(g_CpuOverlayFrame.data() + static_cast<size_t>(y) * lineBytes,
                           src + static_cast<size_t>(y) * rowPitch,
                           static_cast<size_t>(lineBytes));
                }
                CpuDrawFirstPersonOverlay(g_CpuOverlayFrame, desc.Width, desc.Height);
                output = g_CpuOverlayFrame.data();
                outputPitch = lineBytes;
            }

            if (sourceValid) {
                g_LastGoodFrame.resize(static_cast<size_t>(desc.Width) * desc.Height * 4u);
                for (UINT y = 0; y < desc.Height; ++y) {
                    memcpy(g_LastGoodFrame.data() + static_cast<size_t>(y) * lineBytes,
                           output + static_cast<size_t>(y) * outputPitch,
                           static_cast<size_t>(lineBytes));
                }
            } else if (!g_LastGoodFrame.empty() &&
                       (!g_RecordingStarted || g_ReuseLastGoodDuringRecording)) {
                output = g_LastGoodFrame.data();
                outputPitch = lineBytes;
                g_LastFrameReused = true;
            } else {
                // Do not seed or advance the movie with a black/cleared frame.
                // During recording, waiting for the next valid source keeps
                // the proof stream truthful instead of replaying a stale eye.
                if (needSysMemUnlock && g_pSysMemSurface) g_pSysMemSurface->UnlockRect();
                WriteCaptureTelemetry(g_RecordingStarted
                    ? "invalid_skip_recording" : "invalid_no_last_good");
                return;
            }

            // Do not open ffmpeg/audio until the first actual scene surface
            // has arrived.  Starting on a cleared active target used to leave
            // the movie at frame zero while repeated Present callbacks kept
            // logging "First frame locked" and replaying stale pixels.
            if (!g_RecordingStarted && g_RecordingRequested && sourceValid) {
                g_CaptureWidth = desc.Width;
                g_CaptureHeight = desc.Height;
                CreateDirectoryA("D:\\code\\blvr\\artifacts", NULL);
                DeleteFileA(TEMP_VIDEO_PATH);
                DeleteFileA(FINAL_OUTPUT_PATH);
                DeleteFileA(WAV_PATH);

                char cmd[1024];
                sprintf_s(cmd, "\"\"%s\" -y -f rawvideo -vcodec rawvideo -s %dx%d -pix_fmt bgr0 -r %d -i - -c:v libx264 -preset fast -crf 20 -pix_fmt yuv420p \"%s\"\"",
                          FFMPEG_EXE, g_CaptureWidth, g_CaptureHeight, TARGET_FPS, TEMP_VIDEO_PATH);
                Log("VideoCapture: Spawning ffmpeg pipeline from %s: %dx%d @ %d fps -> %s",
                    useStereoEye ? "VR stereo eye buffer" : (g_CaptureActiveTarget ? "active render target" : "presented backbuffer"),
                    g_CaptureWidth, g_CaptureHeight, TARGET_FPS, TEMP_VIDEO_PATH);
                g_FfmpegPipe = _popen(cmd, "wb");
                if (!g_FfmpegPipe) {
                    Log("VideoCapture: ERROR - Failed to open ffmpeg pipe! errno=%d", errno);
                    g_RecordingRequested = false;
                } else {
                    AudioCapture_Start(WAV_PATH);
                    g_RecordingStarted = true;
                    g_RecordingRequested = false;
                    g_RecordedFrames = 0;
                    g_LastGoodFrame.clear();
                    g_LastSourceValid = sourceValid;
                    ++g_FirstFrameLockCount;
                    Log("VideoCapture: ffmpeg pipe and audio capture opened successfully. Beginning frame stream.");
                }
            }

            // The startup path clears the cache so a prior take cannot seed
            // this one.  Re-seed it with the same valid source frame that
            // opened the new recording.
            if (g_RecordingStarted && sourceValid && g_LastGoodFrame.empty()) {
                g_LastGoodFrame.resize(static_cast<size_t>(desc.Width) * desc.Height * 4u);
                for (UINT y = 0; y < desc.Height; ++y) {
                    memcpy(g_LastGoodFrame.data() + static_cast<size_t>(y) * lineBytes,
                           output + static_cast<size_t>(y) * outputPitch,
                           static_cast<size_t>(lineBytes));
                }
            }

            if (g_RecordingStarted && g_FfmpegPipe) {
                for (int y = 0; y < g_CaptureHeight; y++) {
                    fwrite(output + y * outputPitch, 1, lineBytes, g_FfmpegPipe);
                }
                g_RecordedFrames++;
            }
            if (needSysMemUnlock && g_pSysMemSurface) g_pSysMemSurface->UnlockRect();

            WriteCaptureTelemetry(sourceValid ? "frame" : "reused_last_good");

            if (g_RecordingStarted && (g_RecordedFrames % 60 == 0 || g_RecordedFrames == g_TotalFramesToRecord)) {
                Log("VideoCapture: Recorded %d / %d frames (%.1f s of video)",
                    g_RecordedFrames, g_TotalFramesToRecord, (float)g_RecordedFrames / TARGET_FPS);
            }
}

void VideoCapture_Stop() {
    if (!g_RecordingStarted || g_RecordingFinished) return;

    g_RecordingStarted = false;
    g_RecordingFinished = true;

    if (g_pSysMemSurface) {
        g_pSysMemSurface->Release();
        g_pSysMemSurface = nullptr;
    }

    if (g_FfmpegPipe) {
        _pclose(g_FfmpegPipe);
        g_FfmpegPipe = nullptr;
        Log("VideoCapture: ffmpeg video pipe closed cleanly. (Total frames: %d, Length: %.1fs)",
            g_RecordedFrames, (float)g_RecordedFrames / TARGET_FPS);
    }

    // Stop audio capture
    AudioCapture_Stop();

    // Check if WAV file exists and has data to mux with video
    WIN32_FILE_ATTRIBUTE_DATA fad;
    bool hasAudio = false;
    if (GetFileAttributesExA(WAV_PATH, GetFileExInfoStandard, &fad)) {
        LARGE_INTEGER size;
        size.LowPart = fad.nFileSizeLow;
        size.HighPart = fad.nFileSizeHigh;
        if (size.QuadPart > 1000) {
            hasAudio = true;
            Log("VideoCapture: Found audio track (%lld bytes). Muxing with video...", size.QuadPart);
        }
    }

    if (hasAudio) {
        char muxCmd[2048];
        sprintf_s(muxCmd, "\"\"%s\" -y -i \"%s\" -i \"%s\" -c:v copy -c:a aac -b:a 192k -shortest \"%s\"\"",
                  FFMPEG_EXE, TEMP_VIDEO_PATH, WAV_PATH, FINAL_OUTPUT_PATH);
        Log("VideoCapture: Running mux command in background thread: %s", muxCmd);
        char* pCmd = _strdup(muxCmd);
        CreateThread(NULL, 0, [](LPVOID param) -> DWORD {
            char* cmd = reinterpret_cast<char*>(param);
            if (cmd) {
                int res = system(cmd);
                Log("VideoCapture: Async mux command finished with exit code %d", res);
                free(cmd);
                DeleteFileA(TEMP_VIDEO_PATH);
            }
            return 0;
        }, pCmd, 0, NULL);
    } else {
        Log("VideoCapture: No audio captured or file empty. Moving temp video to final output.");
        MoveFileExA(TEMP_VIDEO_PATH, FINAL_OUTPUT_PATH, MOVEFILE_REPLACE_EXISTING);
    }

    Log("VideoCapture: Final video with audio finalized at %s!", FINAL_OUTPUT_PATH);
}

} // namespace BLVR
