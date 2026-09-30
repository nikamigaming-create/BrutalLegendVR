#include "camera_hook.h"
#include "../diagnostics/frame_profiler.h"
#define BLVR_PERF_D3D9
#include "../diagnostics/gpu_profiler.h"
#include "tracked_basis.h"
#include "headset_visibility.h"
#include "vr_navigation.h"
#include "shadow_transform.h"
#include "terrain_projection.h"
#include "camera_relative_effects.h"
#include "player_view_rig.h"
#include "../input/control_bindings.h"
#include "../bridge/eddie_dimensions.h"
#include "../input/retail_input_bridge.h"
#include "../input/vr_prompts.h"
#include "../openxr/xr_host.h"
#include "../diagnostics/log.h"
#include "../diagnostics/telemetry.h"
#include "../bootstrap/video_capture.h"
#include "MinHook.h"
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <intrin.h>

extern "C" uint32_t __cdecl BLVR_GetD3D9DrawCallCount();
extern "C" IDirect3DDevice9* __cdecl BLVR_GetD3D9Device();

namespace BLVR {
static blvr_perf::GpuProfiler gpuProfiler("game");
static void* volatile soloHud=nullptr;

// RVA for CameraController::SetCameraTransform (0x8ee850 - 0x400000 = 0x4ee850)
static const uintptr_t RVA_SET_CAMERA_TRANSFORM = 0x4ee850;
// RVA for SetupSceneCamera (0x6f3480 - 0x400000 = 0x2f3480)
static const uintptr_t RVA_SETUP_SCENE_CAMERA = 0x2f3480;
// Scene render transaction reached immediately after SetupSceneCamera in the
// retail frame path. Its ABI is EAX=scene renderer plus one stdcall argument.
static const uintptr_t RVA_RENDER_SCENE_CANDIDATE = 0x2f6f30;
// RVA for Camera::UpdateWorldTransform (0x488410 - 0x400000 = 0x88410)
static const uintptr_t RVA_CAMERA_UPDATE_WORLD_TRANSFORM = 0x88410;
// CoPhysicsCharacter::UpdateCamera, where the retail head-joint pose is
// converted into the active camera before rendering.
static const uintptr_t RVA_CHARACTER_UPDATE_CAMERA = 0x180a20;
// Native head-pose setter called by UpdateCamera with position in ECX,
// quaternion in EAX, and the camera object as its stack argument.
static const uintptr_t RVA_SET_FROM_VECTORS = 0x88390;
// Joint transform copier used by UpdateCamera: writes world position at +0x00
// and joint quaternion at +0x10 in the caller-provided transform buffer.
static const uintptr_t RVA_GET_JOINT_TRANSFORM = 0x966ed0;
// SetupSceneCamera dispatches its final view/world/projection matrices through
// this helper using ECX/EAX plus one stack argument.
static const uintptr_t RVA_UPLOAD_CAMERA_MATRIX = 0x25e010;

typedef void (__stdcall* FnSetCameraTransform)(
    void* pCameraCtrl,
    float eyeX, float eyeY, float eyeZ,
    float targetX, float targetY, float targetZ
);
static FnSetCameraTransform g_Original_SetCameraTransform = nullptr;
static void* g_TargetSetCameraTransform = nullptr;

typedef void (__stdcall* FnSetupSceneCamera)(void* pSceneCamera, void* pContext);
static FnSetupSceneCamera g_Original_SetupSceneCamera = nullptr;
static void* g_TargetSetupSceneCamera = nullptr;
using BuildRenderCamera = void(__stdcall*)(void*,void*);
static BuildRenderCamera g_OriginalBuildRenderCamera = nullptr;
static void* g_TargetBuildRenderCamera = nullptr;
static void* g_BuildNativeFrustum = nullptr;
using InterpolateScene = void(__stdcall*)(void*,void*,void*,float);
static InterpolateScene g_OriginalInterpolateScene = nullptr;
static void* g_TargetInterpolateScene = nullptr;
struct PresentationSceneStamp { void* scene;void* camera;uint64_t tick; };
static PresentationSceneStamp g_PresentationScenes[32]{};
static unsigned g_PresentationSceneIndex=0;
static SRWLOCK g_PresentationSceneLock=SRWLOCK_INIT;
static void* g_OriginalRenderSceneCandidate = nullptr;
static void* g_RenderFlush = nullptr;
static void* g_RenderLighting = nullptr;
static void* g_RenderWorldComposite = nullptr;
static void* g_RenderWorldEffects = nullptr;
using NativeFlashRender = bool(__thiscall*)(void*, void*);
static NativeFlashRender g_OriginalFlashRender = nullptr;
static void* g_TargetFlashRender = nullptr;
using NativeSoloHudUpdate = void(__stdcall*)(void*, void*);
static NativeSoloHudUpdate g_OriginalSoloHudUpdate = nullptr;
static void* g_TargetSoloHudUpdate = nullptr;
static volatile LONG g_SoloUiFlags = 0;
static volatile LONG g_SoloUiTick = 0;
static void* g_RenderCommonState = nullptr;
using NativePostProcess = void(__stdcall*)(void*, void*, void*, void*, void*);
static NativePostProcess g_RenderPostProcess = nullptr;
static void* g_TargetRenderSceneCandidate = nullptr;
static volatile LONG g_RenderSceneCandidateCount = 0;
static volatile LONG g_RenderSceneCandidateGameplayCount = 0;
static thread_local uint32_t g_RenderCandidateDrawCountBefore = 0;
static thread_local uint32_t g_RenderCandidateGameplayIndex = 0;
static thread_local int g_StereoEyeIndex = -1;
static void* g_LastSceneCamera = nullptr;
static void* g_LastSceneContext = nullptr;
static uint64_t g_StereoSourceFrame = 0;
static uint32_t g_StereoPairLogCount = 0;
static thread_local bool g_RenderingStereoCandidate = false;
bool CameraHook_IsStereoRender() {return g_RenderingStereoCandidate&&g_StereoEyeIndex>=0;}
static uint32_t __cdecl RenderSceneCandidateDispatch(void* renderer, void* context);
extern "C" uint32_t CallOriginalRenderSceneCandidate(void* renderer, void* context);

typedef void (__thiscall* FnCameraUpdateWorldTransform)(void* pCamera);
static FnCameraUpdateWorldTransform g_Original_CameraUpdateWorldTransform = nullptr;
static void* g_TargetCameraUpdateWorldTransform = nullptr;

typedef void (__thiscall* FnCharacterUpdateCamera)(void* pCharacterCamera);
static FnCharacterUpdateCamera g_Original_CharacterUpdateCamera = nullptr;
static void* g_TargetCharacterUpdateCamera = nullptr;
typedef void (__thiscall* FnGetJointTransform)(void* pJoint, void* pOutput);
static FnGetJointTransform g_GetJointTransform = nullptr;
static void* g_TargetUploadCameraMatrix = nullptr;
static void* g_OriginalUploadCameraMatrix = nullptr;
static bool g_UploadCameraMatrixHookInstalled = false;
static void* g_TargetSetFromVectors = nullptr;
static void* g_OriginalSetFromVectors = nullptr;



static uint32_t g_SetCamCount = 0;
static uint32_t g_SetupSceneCamCount = 0;
static float g_FovOverride = 0.0f;
static float g_HeadHeightOverride = blvr_rig::EyeHeightMeters;
static float g_HeadForwardOverride = 0.0f; // Used only by the ground-root fallback before the player rig is available.
static bool g_FirstPersonEnabled = true;
static float g_HeadZoom = 1.0f; // 0 = native chase camera, 1 = head-close native camera

// Shared Eddie tracking state: ground root (feet) and chase target
static float g_EddieRootX = 0.0f;
static float g_EddieRootY = 0.0f;
static float g_EddieRootZ = 0.0f;
static bool g_HasEddieRoot = false;
static const uint8_t* g_GameplayCameraController = nullptr;

static float g_EddieTargetX = 0.0f;
static float g_EddieTargetY = 0.0f;
static float g_EddieTargetZ = 0.0f;
static float g_EddieFwdX = 1.0f;
static float g_EddieFwdZ = 0.0f;
static bool g_HasEddieTransform = false;
static void* g_pPlayerCharacter = nullptr;
struct BodyAnchor {
    float root[3]{};
    float target[3]{};
    float forward[2]{1.0f, 0.0f};
    bool hasRoot = false;
    float roomOffset[2]{};
    float eyes[3]{};
    bool hasEyes=false;
};
static SRWLOCK g_BodyAnchorLock = SRWLOCK_INIT;
static BodyAnchor g_PublishedBodyAnchor;
static VrNavigation g_Navigation;
static Pose6DoF g_NavigationHead{};
static bool g_NavigationHeadValid = false;
static float g_NativeForward[2]{1.0f, 0.0f};
static thread_local BodyAnchor g_StereoBodyAnchor;
static BodyAnchor ReadBodyAnchor() {
    AcquireSRWLockShared(&g_BodyAnchorLock);
    const BodyAnchor value = g_PublishedBodyAnchor;
    ReleaseSRWLockShared(&g_BodyAnchorLock);
    return value;
}

static void MapVrGameplayInput(float& x, float& y, float& rx, float& ry, bool gameplay) {
    AcquireSRWLockExclusive(&g_BodyAnchorLock);
    if (g_Navigation.initialized && g_NavigationHeadValid) {
        if (gameplay && g_Navigation.Snap(rx, g_NavigationHead.pos[0],
                                       g_NavigationHead.pos[2] - g_HeadForwardOverride)) {
            g_PublishedBodyAnchor.forward[0] = g_Navigation.forwardX;
            g_PublishedBodyAnchor.forward[1] = g_Navigation.forwardZ;
            g_PublishedBodyAnchor.roomOffset[0] = g_Navigation.offsetX;
            g_PublishedBodyAnchor.roomOffset[1] = g_Navigation.offsetZ;
        }
        float right[3], up[3], forward[3];
        if (!g_Navigation.riding && TrackedEyeBasis(g_NavigationHead.quat, g_Navigation.forwardX,
                            g_Navigation.forwardZ, right, up, forward)) {
            VrNavigation::MapMovement(forward[0], forward[2],
                                      g_NativeForward[0], g_NativeForward[1], x, y);
        }
        // The native chase controller must not receive the XR turn a second time.
        rx = ry = 0.0f;
    }
    ReleaseSRWLockExclusive(&g_BodyAnchorLock);
}

// Current First-Person State
static float g_FpEyeX = 0.0f;
static float g_FpEyeY = 0.0f;
static float g_FpEyeZ = 0.0f;
static float g_FpLookX = 1.0f;
static float g_FpLookY = 0.0f;
static float g_FpLookZ = 0.0f;
static float g_FpRotation[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
static float g_FlatLookX = 1.0f;
static float g_FlatLookY = 0.0f;
static float g_FlatLookZ = 0.0f;
static bool g_HasFlatLook = false;
static void* g_ActiveCamera = nullptr;
static bool g_InSetCameraTransform = false;
static bool g_InCameraUpdate = false;
static uint32_t g_CameraUpdateCount = 0;
static float g_OriginalCameraEye[3] = {};
static float g_OriginalCameraTarget[3] = {};
static bool g_HasOriginalCameraPose = false;
static float g_OriginalSceneWorld[16] = {};
static float g_OriginalScenePosition[3] = {};
static bool g_HasOriginalSceneWorld = false;
static uint32_t g_CharacterCameraUpdateCount = 0;
static uint32_t g_SetFromVectorsCount = 0;
static uint32_t g_NativeHeadPoseCount = 0;
static uint32_t g_EngineMatrixUploadCount = 0;
static uint32_t g_EngineMatrixReplacementCount = 0;
static bool g_NativeBodyHeadEnabled = false;
static bool g_HasNativeBodyHead = false;
static float g_NativeBodyHead[3] = {};
static bool g_PreserveScenePositionConfigured = false;
static bool g_PreserveScenePosition = false;
static constexpr bool g_ContextEyeOverride = false;
static bool g_W2POverrideConfigured = false;
static bool g_W2POverride = false;
static float g_EngineProjection[16] = {};
static bool g_HasEngineProjection = false;
static float g_NativeShadowView[16]{};
static float g_NativeShadowMatrices[4][16]{};
static bool g_HasNativeShadowView = false;

static void __cdecl ObserveRenderSceneCandidateEntry(void* renderer, void* context) {
    g_RenderCandidateDrawCountBefore = BLVR_GetD3D9DrawCallCount();
    const LONG count = InterlockedIncrement(&g_RenderSceneCandidateCount);
    const bool gameplay = CameraHook_IsActive();
    g_RenderCandidateGameplayIndex = gameplay
        ? static_cast<uint32_t>(InterlockedIncrement(&g_RenderSceneCandidateGameplayCount))
        : 0u;
    if (count <= 3 || (gameplay &&
        (g_RenderCandidateGameplayIndex <= 6 || (g_RenderCandidateGameplayIndex % 120u) == 0u))) {
        Log("RenderSceneCandidate entry #%ld gameplay=%d gameplayPass=%u renderer=%p context=%p drawCount=%u",
            count, gameplay ? 1 : 0, g_RenderCandidateGameplayIndex,
            renderer, context, g_RenderCandidateDrawCountBefore);
    }
}

static void __cdecl ObserveRenderSceneCandidateExit(void* renderer, void* context) {
    const LONG count = g_RenderSceneCandidateCount;
    const bool gameplay = CameraHook_IsActive();
    const uint32_t gameplayIndex = g_RenderCandidateGameplayIndex;
    if (count <= 3 || (gameplay &&
        (gameplayIndex <= 6 || (gameplayIndex % 120u) == 0u))) {
        const uint32_t drawCountAfter = BLVR_GetD3D9DrawCallCount();
        Log("RenderSceneCandidate exit  #%ld gameplay=%d gameplayPass=%u renderer=%p context=%p indexedDraws=%u",
            count, gameplay ? 1 : 0, gameplayIndex, renderer, context,
            drawCountAfter - g_RenderCandidateDrawCountBefore);
    }
    g_RenderCandidateGameplayIndex = 0;
}

// Preserve the retail custom ABI (renderer in EAX, context on the stack) and
// stdcall cleanup while measuring the real D3D9 work inside the candidate.
extern "C" __declspec(naked) void Hook_RenderSceneCandidate() {
    __asm {
        push ebp
        mov ebp, esp
        push ebx
        push esi
        push edi
        sub esp, 8
        mov dword ptr [ebp - 16], eax

        push dword ptr [ebp + 8]
        push dword ptr [ebp - 16]
        call ObserveRenderSceneCandidateEntry
        add esp, 8

        mov eax, dword ptr [ebp - 16]
        push dword ptr [ebp + 8]
        push dword ptr [ebp - 16]
        call RenderSceneCandidateDispatch
        add esp, 8
        mov dword ptr [ebp - 20], eax

        push dword ptr [ebp + 8]
        push dword ptr [ebp - 16]
        call ObserveRenderSceneCandidateExit
        add esp, 8
        mov eax, dword ptr [ebp - 20]

        lea esp, dword ptr [ebp - 12]
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret 4
    }
}

// These values are absolute addresses in the retail image. Convert them to
// RVAs before adding the ASLR-adjusted module base at runtime.
static constexpr uintptr_t DESCRIPTOR_VIEW = 0xb7a860;
static constexpr uintptr_t DESCRIPTOR_WORLD = 0xb7a86c;
static constexpr uintptr_t DESCRIPTOR_PROJECTION = 0xb7a878;
static constexpr uintptr_t DESCRIPTOR_WORLD_TO_PROJECTION = 0xb7a854;
static uintptr_t g_ExecutableBase = 0;

extern "C" __declspec(naked) uint32_t CallOriginalRenderSceneCandidate(void*, void*) {
    __asm {
        push ebp
        mov ebp, esp
        mov eax, dword ptr [ebp + 8]
        push dword ptr [ebp + 12]
        call dword ptr [g_OriginalRenderSceneCandidate]
        mov esp, ebp
        pop ebp
        ret
    }
}

// Continue the same scene through the remaining world stages observed in the
// retail render caller at 0x6d667b..0x6d66b4. The first stage alone only writes
// an auxiliary target and restores an untouched HDR target before returning.
// None of these calls advances simulation or dequeues another scene snapshot.
static void PrepareWorldState(void* renderer, void* scene) {
    // Retail 0x6f31c0 publishes the camera-relative sun, shadow, fog and
    // material globals as well as the view/projection descriptors. Repeating
    // SetupSceneCamera alone leaves these globals from another eye/pass.
    __asm {
        mov eax, renderer
        push scene
        call dword ptr [g_RenderCommonState]
    }
}

static void CompleteWorldRender(void* renderer, void* scene) {
    __asm {
        push esi
        push edi
        mov edi, renderer
        call dword ptr [g_RenderFlush]
        mov esi, renderer
        push scene
        call dword ptr [g_RenderLighting]
        mov eax, renderer
        push scene
        call dword ptr [g_RenderCommonState]
        mov eax, renderer
        call dword ptr [g_RenderWorldEffects]
        mov eax, renderer
        push scene
        call dword ptr [g_RenderWorldComposite]
        mov edi, renderer
        call dword ptr [g_RenderFlush]
        pop edi
        pop esi
    }
    // Finish native exposure, grading and bloom. RVA 0x2f9fc0 is the Flash
    // composite, not color grading: it CONSUMES scene+0x11f0 once and releases
    // its display list. The retail caller executes it after both eye captures.
    // Replaying that stage per eye left all UI in just the first eye.
    auto* packet = static_cast<uint8_t*>(scene);
    g_RenderPostProcess(renderer, scene, packet + 0x404, packet + 0x344, packet + 0x430);
    __asm {
        push edi
        mov edi, renderer
        call dword ptr [g_RenderFlush]
        pop edi
    }
}

static void* ResolveCamera(void* pCameraCtrl, uintptr_t exeBase) {
    if (!pCameraCtrl) return nullptr;
    void* pTableOrObj = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(pCameraCtrl) + 0x10);
    if (!pTableOrObj) return nullptr;
    uint32_t ediVal = *reinterpret_cast<uint32_t*>(exeBase + 0xc0fce4);
    uintptr_t fnAddr = exeBase + 0xb9ef0;

    void* result = nullptr;
    __asm {
        push edi
        push esi
        push ebx
        mov eax, pTableOrObj
        mov edi, ediVal
        mov ecx, fnAddr
        call ecx
        mov result, eax
        pop ebx
        pop esi
        pop edi
    }
    return result;
}

static void MatrixMultiply(const float* A, const float* B, float* out) {
    MultiplyCameraMatrices(A, B, out);
}

static bool ApplyOpenXrEyeProjection(float* matrix, int eyeIndex) {
    if (!matrix || eyeIndex < 0 || eyeIndex > 1) return false;
    EyeView eyeView{};
    if (!XrHost::Get().GetEyeView(static_cast<uint32_t>(eyeIndex), eyeView)) return false;

    const float left = tanf(eyeView.fov[0]);
    const float right = tanf(eyeView.fov[1]);
    const float top = tanf(eyeView.fov[2]);
    const float bottom = tanf(eyeView.fov[3]);
    const float horizontal = right - left;
    const float vertical = top - bottom;
    if (!std::isfinite(left) || !std::isfinite(right) ||
        !std::isfinite(top) || !std::isfinite(bottom) ||
        !(horizontal > 0.1f) || !(vertical > 0.1f)) {
        return false;
    }

    // The retail projection is a row-major D3D right-handed matrix. Keep its
    // native near/far depth terms; replace only the asymmetric OpenXR frustum.
    matrix[0] = 2.0f / horizontal;
    matrix[5] = 2.0f / vertical;
    matrix[8] = (right + left) / horizontal;
    matrix[9] = (top + bottom) / vertical;
    return std::isfinite(matrix[0]) && std::isfinite(matrix[5]) &&
           std::isfinite(matrix[8]) && std::isfinite(matrix[9]);
}


struct FirstPersonCameraState {
    float eye[3];
    float forward[3];
    float up[3];
    float right[3];
    float viewToCamWorld[16];
    float camWorldToView[16];
};

static bool ComputeFirstPersonCamera(FirstPersonCameraState& outState) {
    if (!g_FirstPersonEnabled || !CameraHook_IsActive() || CameraHook_IsCinematic()) return false;

    // XR is latched at the start of the stereo render transaction. This function can be
    // reached several times while Buddha builds one render frame (camera
    // update, scene setup, view-matrix upload, and D3D state overrides).  A
    // live read here used to advance OpenXR/FNVXR independently for each of
    // those callbacks, producing pose-age mismatches that looked like the
    // game had taken over Eddie's head.
    Pose6DoF hmdPose{};
    if (!XrHost::Get().GetHeadPose(hmdPose)) return false;
    Pose6DoF renderPose = hmdPose;
    if (g_StereoEyeIndex >= 0) {
        EyeView eyeView{};
        if (!XrHost::Get().GetEyeView(static_cast<uint32_t>(g_StereoEyeIndex), eyeView)) {
            return false;
        }
        renderPose = eyeView.pose;
    }

    // Body horizontal forward and right
    BodyAnchor anchor = g_RenderingStereoCandidate ? g_StereoBodyAnchor : ReadBodyAnchor();
    if(!g_RenderingStereoCandidate) {
        void* actor=PlayerViewRig_ResolveActor(g_pPlayerCharacter);
        const bool mounted=PlayerViewRig_ResolveMount(actor)!=nullptr;
        anchor.hasEyes=PlayerViewRig_ReadLiveEyeAnchor(actor,mounted,anchor.eyes);
    }
    float bodyFwdX = anchor.forward[0];
    float bodyFwdZ = anchor.forward[1];
    float bodyFwdLen = sqrtf(bodyFwdX * bodyFwdX + bodyFwdZ * bodyFwdZ);
    if (bodyFwdLen > 0.0001f) {
        bodyFwdX /= bodyFwdLen;
        bodyFwdZ /= bodyFwdLen;
    } else {
        bodyFwdX = 1.0f;
        bodyFwdZ = 0.0f;
    }
    float bodyRightX = -bodyFwdZ;
    float bodyRightZ = bodyFwdX;

    // Eye anchor in engine units above the camera target actor's ground root.
    // The tracked translation is relative to the user's seated/worn origin.
    float anchorX, anchorY, anchorZ;
    if (anchor.hasEyes) {
        anchorX=anchor.eyes[0]; anchorY=anchor.eyes[1]; anchorZ=anchor.eyes[2];
    } else if (anchor.hasRoot) {
        anchorX = anchor.root[0] + bodyFwdX * g_HeadForwardOverride;
        anchorY = anchor.root[1] + g_HeadHeightOverride;
        anchorZ = anchor.root[2] + bodyFwdZ * g_HeadForwardOverride;
    } else {
        // Fallback before root is sampled: chase target is 2.30m above ground root
        anchorX = anchor.target[0] + bodyFwdX * g_HeadForwardOverride;
        anchorY = anchor.target[1] - 2.30f + g_HeadHeightOverride;
        anchorZ = anchor.target[2] + bodyFwdZ * g_HeadForwardOverride;
    }
    if (g_NativeBodyHeadEnabled && g_HasNativeBodyHead) {
        anchorX = g_NativeBodyHead[0];
        anchorY = g_NativeBodyHead[1];
        anchorZ = g_NativeBodyHead[2];
    }

    // Positional tracking already contains the real neck/eye translation.
    // Adding a synthetic pitch-driven neck arc moved the world a second time.
    outState.eye[0] = anchorX + anchor.roomOffset[0] + bodyRightX * renderPose.pos[0] - bodyFwdX * renderPose.pos[2];
    outState.eye[1] = anchorY + renderPose.pos[1];
    outState.eye[2] = anchorZ + anchor.roomOffset[1] + bodyRightZ * renderPose.pos[0] - bodyFwdZ * renderPose.pos[2];

    if (!TrackedEyeBasis(renderPose.quat, bodyFwdX, bodyFwdZ,
                         outState.right, outState.up, outState.forward)) return false;
    const float rx=outState.right[0], ry=outState.right[1], rz=outState.right[2];
    const float ux=outState.up[0], uy=outState.up[1], uz=outState.up[2];
    const float fx=outState.forward[0], fy=outState.forward[1], fz=outState.forward[2];

    // ViewToCamWorld (+0x750)
    // Row 0: Right
    // Row 1: Up
    // Row 2: -Forward (camera looks down -Z in view space)
    // Row 3: [0, 0, 0, 1]
    outState.viewToCamWorld[0]  = rx;   outState.viewToCamWorld[1]  = ry;   outState.viewToCamWorld[2]  = rz;   outState.viewToCamWorld[3]  = 0.0f;
    outState.viewToCamWorld[4]  = ux;   outState.viewToCamWorld[5]  = uy;   outState.viewToCamWorld[6]  = uz;   outState.viewToCamWorld[7]  = 0.0f;
    outState.viewToCamWorld[8]  = -fx;  outState.viewToCamWorld[9]  = -fy;  outState.viewToCamWorld[10] = -fz;  outState.viewToCamWorld[11] = 0.0f;
    outState.viewToCamWorld[12] = 0.0f; outState.viewToCamWorld[13] = 0.0f; outState.viewToCamWorld[14] = 0.0f; outState.viewToCamWorld[15] = 1.0f;

    // CamWorldToView (+0x790) - transpose of ViewToCamWorld
    outState.camWorldToView[0]  = rx;   outState.camWorldToView[1]  = ux;   outState.camWorldToView[2]  = -fx;  outState.camWorldToView[3]  = 0.0f;
    outState.camWorldToView[4]  = ry;   outState.camWorldToView[5]  = uy;   outState.camWorldToView[6]  = -fy;  outState.camWorldToView[7]  = 0.0f;
    outState.camWorldToView[8]  = rz;   outState.camWorldToView[9]  = uz;   outState.camWorldToView[10] = -fz;  outState.camWorldToView[11] = 0.0f;
    outState.camWorldToView[12] = 0.0f; outState.camWorldToView[13] = 0.0f; outState.camWorldToView[14] = 0.0f; outState.camWorldToView[15] = 1.0f;

    // Update global state
    g_FpEyeX = outState.eye[0];
    g_FpEyeY = outState.eye[1];
    g_FpEyeZ = outState.eye[2];
    g_FpLookX = fx;
    g_FpLookY = fy;
    g_FpLookZ = fz;

    // Compute camera rotation quaternion for pCamera (+0x50)
    // Rotation matrix columns: [right, up, -forward]
    float bx = -fx, by = -fy, bz = -fz;
    float trace = rx + uy + bz;
    if (trace > 0.0f) {
        float s = 0.5f / sqrtf(trace + 1.0f);
        g_FpRotation[3] = 0.25f / s;
        g_FpRotation[0] = (uz - by) * s;
        g_FpRotation[1] = (bx - rz) * s;
        g_FpRotation[2] = (ry - ux) * s;
    } else if (rx > uy && rx > bz) {
        float s = 2.0f * sqrtf(1.0f + rx - uy - bz);
        g_FpRotation[3] = (uz - by) / s;
        g_FpRotation[0] = 0.25f * s;
        g_FpRotation[1] = (ux + ry) / s;
        g_FpRotation[2] = (bx + rz) / s;
    } else if (uy > bz) {
        float s = 2.0f * sqrtf(1.0f + uy - rx - bz);
        g_FpRotation[3] = (bx - rz) / s;
        g_FpRotation[0] = (ux + ry) / s;
        g_FpRotation[1] = 0.25f * s;
        g_FpRotation[2] = (by + uz) / s;
    } else {
        float s = 2.0f * sqrtf(1.0f + bz - rx - uy);
        g_FpRotation[3] = (ry - ux) / s;
        g_FpRotation[0] = (bx + rz) / s;
        g_FpRotation[1] = (by + uz) / s;
        g_FpRotation[2] = 0.25f * s;
    }

    return true;
}

static void BuildNativeFrustum(void* output,const float* inverse) {
    __asm {
        push esi
        mov esi,output
        mov ecx,inverse
        call dword ptr [g_BuildNativeFrustum]
        pop esi
    }
}

static void ApplyHeadsetVisibility(void* output) {
    __try {
        FirstPersonCameraState head{};Pose6DoF headPose{};EyeView eyes[2]{};
        if(!ComputeFirstPersonCamera(head)||!XrHost::Get().GetHeadPose(headPose)||
           !XrHost::Get().GetEyeView(0,eyes[0])||!XrHost::Get().GetEyeView(1,eyes[1]))return;
        float qs[2][4],ps[2][3],fovs[2][4],tx=0,ty=0;
        for(int i=0;i<2;++i) {
            std::memcpy(qs[i],eyes[i].pose.quat,sizeof(qs[i]));
            std::memcpy(ps[i],eyes[i].pose.pos,sizeof(ps[i]));
            std::memcpy(fovs[i],eyes[i].fov,sizeof(fovs[i]));
        }
        if(!HeadsetCullTangents(headPose.quat,headPose.pos,qs,ps,fovs,tx,ty))return;
        auto* block=static_cast<uint8_t*>(output);
        float projection[16],inverseProjection[16],world[16],view[16],viewProjection[16],inverseViewProjection[16];
        std::memcpy(projection,block+0xb0,sizeof(projection));
        projection[0]=1/tx;projection[5]=1/ty;projection[8]=projection[9]=0;
        std::memcpy(world,head.viewToCamWorld,sizeof(world));
        for(int k=0;k<3;++k)world[12+k]=head.eye[k]-3.f*head.forward[k];
        if(!InvertCameraMatrix(projection,inverseProjection)||!InvertCameraMatrix(world,view))return;
        MatrixMultiply(view,projection,viewProjection);
        if(!InvertCameraMatrix(viewProjection,inverseViewProjection))return;
        // Native camera-distance fading must retain the chase position. Only
        // visibility volumes change; world/shadow matrices remain mutually
        // consistent until the existing per-eye render override.
        float clipToNativeView[16];
        MatrixMultiply(inverseViewProjection,reinterpret_cast<const float*>(block+0x30),clipToNativeView);
        BuildNativeFrustum(block+0x2e0,inverseViewProjection);
        BuildNativeFrustum(block+0x3a0,clipToNativeView);
        std::memcpy(block+0x460,block+0x2e0,0xc0);
        static unsigned count=0;
        if(++count<=3||count%600==0)Log("Headset visibility: binocular FOV=%.1fx%.1f margin=8deg camera=%p snapshot=%p",
            2*std::atan(tx)*57.2957795f,2*std::atan(ty)*57.2957795f,g_ActiveCamera,output);
    } __except(EXCEPTION_EXECUTE_HANDLER) {Log("Headset visibility snapshot fault");}
}

static void __stdcall Hook_BuildRenderCamera(void* output,void* camera) {
    const uintptr_t caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
    if(caller==g_ExecutableBase+0x2d3351) {
        const void* scene=static_cast<uint8_t*>(output)-0x5e0;
        AcquireSRWLockExclusive(&g_PresentationSceneLock);
        for(auto& stamp:g_PresentationScenes)if(stamp.scene==scene)stamp={};
        ReleaseSRWLockExclusive(&g_PresentationSceneLock);
    }
    g_OriginalBuildRenderCamera(output,camera);
    // Both main-world camera snapshots are copied here before visibility,
    // shadow collection and render interpolation. Never edit the simulation
    // camera or an auxiliary light camera to obtain the headset bounds.
    if(camera!=g_ActiveCamera||!g_FirstPersonEnabled||!CameraHook_IsActive()||CameraHook_IsCinematic())return;
    if(caller==g_ExecutableBase+0x2d3351) {
        // This is the main scene packet, after its native config copy and
        // before mesh collection. Retail 0x6f17a5 / 0x6f1946 use these
        // multipliers for distance fading and LOD selection respectively.
        // A larger scale retains detail farther away, including meshes whose
        // MaxVisibleLOD would otherwise make the actor disappear completely.
        auto* scene=static_cast<uint8_t*>(output)-0x5e0;
        auto& detail=*reinterpret_cast<float*>(scene+0x124);
        auto& meshLod=*reinterpret_cast<float*>(scene+0x138);
        if(std::isfinite(detail)&&std::isfinite(meshLod)&&detail>0&&meshLod>0) {
            detail=std::max(detail,2.0f);
            meshLod=std::max(meshLod,3.0f);
            static bool logged=false;
            if(!logged) {Log("VR scene detail: culling scale=%.2f mesh LOD scale=%.2f",detail,meshLod);logged=true;}
        }
    }
    ApplyHeadsetVisibility(output);
}

static void __stdcall Hook_SetCameraTransform(
    void* pCameraCtrl,
    float eyeX, float eyeY, float eyeZ,
    float targetX, float targetY, float targetZ
) {
    if (g_InSetCameraTransform) {
        g_Original_SetCameraTransform(pCameraCtrl, eyeX, eyeY, eyeZ, targetX, targetY, targetZ);
        return;
    }

    g_InSetCameraTransform = true;
    g_SetCamCount++;

    // This callback belongs to the live chase controller. World altitude is
    // not a gameplay state: the cathedral checkpoint has Eddie at Y=238.
    // Verify the observed retail controller type and its finite root instead.
    float controllerRoot[3] = {};
    bool isGameplay = false;
    const uintptr_t exeBase = reinterpret_cast<uintptr_t>(GetModuleHandleA(NULL));
    __try {
        const uint8_t* ctrl = reinterpret_cast<const uint8_t*>(pCameraCtrl);
        if (ctrl && *reinterpret_cast<const uintptr_t*>(ctrl) == exeBase + 0xad4ffc) {
            memcpy(controllerRoot, ctrl + 0x1254, sizeof(controllerRoot));
            isGameplay = std::isfinite(controllerRoot[0]) &&
                std::isfinite(controllerRoot[1]) && std::isfinite(controllerRoot[2]) &&
                std::isfinite(eyeX) && std::isfinite(eyeY) && std::isfinite(eyeZ) &&
                std::isfinite(targetX) && std::isfinite(targetY) && std::isfinite(targetZ);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        isGameplay = false;
    }

    if (isGameplay) {
        g_GameplayCameraController = reinterpret_cast<const uint8_t*>(pCameraCtrl);
        __try {
            const auto handle = *reinterpret_cast<const uint32_t*>(g_GameplayCameraController + 0x1250);
            const auto table = *reinterpret_cast<uint8_t**>(exeBase + 0xb79d8c);
            void* actor = handle < 0x100000 && table ? *reinterpret_cast<void**>(table + handle*12) : nullptr;
            if (actor != g_pPlayerCharacter) {
                g_pPlayerCharacter = actor;
                if (actor) Log("Player owner: controller=%p handle=%u actor=%p node=%p",
                    pCameraCtrl, handle, actor, *reinterpret_cast<void**>(static_cast<uint8_t*>(actor) + 0x18));
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) { g_pPlayerCharacter=nullptr; }
        g_OriginalCameraEye[0] = eyeX;
        g_OriginalCameraEye[1] = eyeY;
        g_OriginalCameraEye[2] = eyeZ;
        g_OriginalCameraTarget[0] = targetX;
        g_OriginalCameraTarget[1] = targetY;
        g_OriginalCameraTarget[2] = targetZ;
        g_HasOriginalCameraPose = true;

        // In flat-screen gameplay, the user's mouse and right thumbstick orbit the native chase camera.
        // The vector (target - eye) defines the forward look direction!
        float lookX = targetX - eyeX;
        float lookY = targetY - eyeY;
        float lookZ = targetZ - eyeZ;
        float lookLen = sqrtf(lookX * lookX + lookY * lookY + lookZ * lookZ);
        if (lookLen > 0.001f) {
            g_FlatLookX = lookX / lookLen;
            g_FlatLookY = lookY / lookLen;
            g_FlatLookZ = lookZ / lookLen;
            g_HasFlatLook = true;

            // Native heading is only the reference used by retail movement.
            // Initialize XR once; subsequent auto-centering must not turn VR.
            const float bodyLen = sqrtf(lookX * lookX + lookZ * lookZ);
            if (bodyLen > 0.001f) {
                AcquireSRWLockExclusive(&g_BodyAnchorLock);
                g_NativeForward[0] = lookX / bodyLen;
                g_NativeForward[1] = lookZ / bodyLen;
                g_Navigation.Initialize(g_NativeForward[0], g_NativeForward[1]);
                ReleaseSRWLockExclusive(&g_BodyAnchorLock);
            }
        }

        // The controller owns the live ground root.  Refresh it every valid
        // gameplay camera transaction so level transitions and locomotion do
        // not leave the VR camera chasing a stale root.  Keep the guarded read
        // because this is an engine-private object at a fixed offset.
        if (pCameraCtrl) {
            __try {
                uint8_t* ctrl = reinterpret_cast<uint8_t*>(pCameraCtrl);
                const float* rootPos = reinterpret_cast<const float*>(ctrl + 0x1254);
                const float rootX = rootPos[0];
                const float rootY = rootPos[1];
                const float rootZ = rootPos[2];
                if (std::isfinite(rootX) && std::isfinite(rootY) && std::isfinite(rootZ) &&
                    isGameplay) {
                    g_EddieRootX = rootX;
                    g_EddieRootY = rootY;
                    g_EddieRootZ = rootZ;
                    g_HasEddieRoot = true;
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                Log("SetCameraTransform: guarded Eddie root read fault=0x%08X", GetExceptionCode());
            }
        }

        // Keep chase camera target for reference / fallback
        g_EddieTargetX = targetX;
        g_EddieTargetY = targetY;
        g_EddieTargetZ = targetZ;
        g_HasEddieTransform = true;
        // Simulation may publish another controller update while the render
        // thread draws. Publish root and heading together, then hold one copy
        // throughout both eyes so a turn cannot split their body anchors.
        AcquireSRWLockExclusive(&g_BodyAnchorLock);
        __try {
            void* rider=PlayerViewRig_ResolveActor(g_pPlayerCharacter);
            void* carrier=PlayerViewRig_ResolveMount(rider);
            const bool mounted=carrier!=nullptr;
            float carrierX=0,carrierZ=0;
            if(mounted) {
                const auto* mesh=*reinterpret_cast<const uint8_t* const*>(static_cast<const uint8_t*>(carrier)+0x38);
                if(mesh&&*reinterpret_cast<void* const*>(mesh+0x10)==carrier) {
                    carrierX=*reinterpret_cast<const float*>(mesh+0x90);
                    carrierZ=*reinterpret_cast<const float*>(mesh+0x98);
                }
            }
            g_Navigation.FollowCarrier(mounted,carrierX,carrierZ);
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
        g_EddieFwdX = g_Navigation.forwardX;
        g_EddieFwdZ = g_Navigation.forwardZ;
        g_PublishedBodyAnchor = BodyAnchor{{g_EddieRootX, g_EddieRootY, g_EddieRootZ},
            {targetX, targetY, targetZ}, {g_EddieFwdX, g_EddieFwdZ}, g_HasEddieRoot,
            {g_Navigation.offsetX, g_Navigation.offsetZ}};
        ReleaseSRWLockExclusive(&g_BodyAnchorLock);

        if (g_SetCamCount <= 5 || g_SetCamCount % 60 == 1) {
            Log("SetCameraTransform #%u: Root=(%.2f, %.2f, %.2f) Eye=(%.2f, %.2f, %.2f) Fwd=(%.2f, %.2f) Target=(%.2f, %.2f, %.2f)",
                g_SetCamCount, g_EddieRootX, g_EddieRootY, g_EddieRootZ,
                g_FpEyeX, g_FpEyeY, g_FpEyeZ, g_EddieFwdX, g_EddieFwdZ,
                targetX, targetY, targetZ);
        }
    }

    // Retail uses its chase camera for attack direction as well as movement.
    // Keep a third-person simulation camera, but point its horizontal bearing
    // along the right controller's aim ray. Head motion only owns the view
    // and walking basis; looking aside must not redirect an attack/target.
    // On tracking loss keep the native bearing, never fall back to gaze.
    if (isGameplay && g_FirstPersonEnabled && !CameraHook_IsCinematic()) {
        AcquireSRWLockExclusive(&g_BodyAnchorLock);
        float right[3],up[3],forward[3];
        ControllerState controllers{};
        if (g_Navigation.initialized && !g_Navigation.riding &&
            XrHost::Get().GetControllerState(controllers) && controllers.right.aimTracked &&
            TrackedEyeBasis(controllers.right.aimPose.quat,g_Navigation.forwardX,g_Navigation.forwardZ,right,up,forward)) {
            const float flat=std::hypot(forward[0],forward[2]);
            const float radius=std::hypot(targetX-eyeX,targetZ-eyeZ);
            if (flat>0.1f && radius>0.1f) {
                g_NativeForward[0]=forward[0]/flat;g_NativeForward[1]=forward[2]/flat;
                eyeX=targetX-g_NativeForward[0]*radius;
                eyeZ=targetZ-g_NativeForward[1]*radius;
                g_OriginalCameraEye[0]=eyeX;g_OriginalCameraEye[2]=eyeZ;
            }
        }
        ReleaseSRWLockExclusive(&g_BodyAnchorLock);
    }
    g_Original_SetCameraTransform(pCameraCtrl, eyeX, eyeY, eyeZ, targetX, targetY, targetZ);

    // Set camera FOV and near plane on resolved Camera
    uint8_t* pCamera = reinterpret_cast<uint8_t*>(ResolveCamera(pCameraCtrl, exeBase));
    if (pCamera) {
        if (g_FirstPersonEnabled && isGameplay) {
            g_ActiveCamera = pCamera;
        }

        float* camFov = reinterpret_cast<float*>(pCamera + 0x90);
        if (camFov && isGameplay && !CameraHook_IsCinematic()) {
            const float defaultFov = 95.0f;
            *camFov = (g_FovOverride > 0.0f) ? g_FovOverride : defaultFov;
        }

        float* camNear = reinterpret_cast<float*>(pCamera + 0x9c);
        if (camNear && g_FirstPersonEnabled && isGameplay && !CameraHook_IsCinematic()) {
            *camNear = 0.10f;
        }
    }

    g_InSetCameraTransform = false;
}

static void __fastcall Hook_CameraUpdateWorldTransform(void* pCamera, void* pDummyEdx) {
    g_CameraUpdateCount++;
    // Simulation owns this object. Writing the HMD here feeds the tracked yaw
    // back into the next chase-camera update and produces continuous spinning.
    g_Original_CameraUpdateWorldTransform(pCamera);
}

static void PublishEyeReconstruction(void* scenePointer, void* rendererPointer) {
    if (!scenePointer || !rendererPointer) return;
    auto* scene = static_cast<uint8_t*>(scenePointer);
    const auto* projection = reinterpret_cast<const float*>(scene + 0x690);
    if (std::fabs(projection[0]) < 0.0001f || std::fabs(projection[5]) < 0.0001f) return;
    // Retail 0x6f369e derives UV-to-view rays from two symmetric FOV scalars.
    // In VR the principal point differs per eye. Updating the projection and
    // its inverse alone does not update this separately uploaded shader value.
    float rays[4];
    ProjectionViewRays(projection, rays);
    auto* device = *reinterpret_cast<uint8_t**>(static_cast<uint8_t*>(rendererPointer) + 4);
    auto* cache = *reinterpret_cast<uint8_t**>(device + 0x140);
    auto* parameters = *reinterpret_cast<uint8_t**>(cache + 0x20);
    const uint32_t index = *reinterpret_cast<const uint32_t*>(g_ExecutableBase + 0xb7a8c4);
    auto* parameter = parameters + index*24;
    if (void* destination = *reinterpret_cast<void**>(parameter + 0x14)) {
        memcpy(destination, rays, sizeof(rays));
        *reinterpret_cast<uint16_t*>(parameter + 8) |= 1;
        *reinterpret_cast<uint16_t*>(parameter + 10) |= 1;
    }
}

static void PublishEyeShadowMatrices(void* scenePointer, void* rendererPointer) {
    if (!g_HasNativeShadowView || !scenePointer || !rendererPointer) return;
    auto* scene = static_cast<uint8_t*>(scenePointer);
    if (!scene[0x479]) return;
    // Retail RVA 0x316040 bakes four camera-to-shadow-atlas matrices BEFORE
    // SetupSceneCamera. Their source camera is therefore the native view,
    // even after geometry has switched to an XR eye. Keep the light's maps
    // and change only the receiver coordinate frame for each eye.
    float eyeShadows[4][16];
    for (unsigned cascade=0; cascade<4; ++cascade) {
        RebaseShadowMatrix(reinterpret_cast<const float*>(scene + 0x650),
                           g_NativeShadowView, g_NativeShadowMatrices[cascade], eyeShadows[cascade]);
        memcpy(scene + 0x1280 + cascade*0x290, eyeShadows[cascade], sizeof(eyeShadows[cascade]));
    }
    // Same descriptor and dirty propagation as retail 0x716384..0x7163c8.
    auto* device = *reinterpret_cast<uint8_t**>(static_cast<uint8_t*>(rendererPointer) + 4);
    auto* cache = *reinterpret_cast<uint8_t**>(device + 0x140);
    auto* parameters = *reinterpret_cast<uint8_t**>(cache + 0x20);
    const uint32_t index = *reinterpret_cast<const uint32_t*>(g_ExecutableBase + 0xb7adb4);
    auto* parameter = parameters + index*24;
    auto* destination = *reinterpret_cast<void**>(parameter + 0x14);
    if (destination) {
        memcpy(destination, eyeShadows, sizeof(eyeShadows));
        *reinterpret_cast<uint16_t*>(parameter + 8) |= 1;
        *reinterpret_cast<uint16_t*>(parameter + 10) |= 1;
    }
}

static void __fastcall Hook_CharacterUpdateCamera(void* pCharacterCamera, void* pDummyEdx) {
    g_Original_CharacterUpdateCamera(pCharacterCamera);
    g_CharacterCameraUpdateCount++;

    if (!pCharacterCamera || IsBadReadPtr(reinterpret_cast<uint8_t*>(pCharacterCamera) + 0x234, sizeof(void*))) {
        return;
    }

    void* joint = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(pCharacterCamera) + 0x234);
    if (joint && g_GetJointTransform) {
        alignas(16) uint8_t transform[0x80] = {};
        __try {
            g_GetJointTransform(joint, transform);
            const float* position = reinterpret_cast<const float*>(transform + 0x00);
            const float* rotation = reinterpret_cast<const float*>(transform + 0x10);
            if (std::isfinite(position[0]) && std::isfinite(position[1]) && std::isfinite(position[2]) &&
                std::isfinite(rotation[0]) && std::isfinite(rotation[1]) &&
                std::isfinite(rotation[2]) && std::isfinite(rotation[3])) {
                g_NativeHeadPoseCount++;
                if (g_NativeHeadPoseCount <= 3 || g_NativeHeadPoseCount % 300 == 0) {
                    Log("NativeHeadPose #%u character=%p joint=%p pos=(%.2f,%.2f,%.2f) q=(%.3f,%.3f,%.3f,%.3f)",
                        g_NativeHeadPoseCount, pCharacterCamera, joint,
                        position[0], position[1], position[2],
                        rotation[0], rotation[1], rotation[2], rotation[3]);
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Log("NativeHeadPose read fault=0x%08X character=%p joint=%p", GetExceptionCode(), pCharacterCamera, joint);
        }
    }

    // Deliberately do not promote pCharacterCamera into Eddie's root/heading
    // authority.  This callback is shared by player and NPC cameras, and the
    // published transform can be an identity/transition value.  The native
    // camera-controller transaction above is the only source used by the VR
    // camera, so this hook remains observation-only for diagnostics.
}

static void __cdecl CameraHook_LogEngineMatrixUpload(
    float* matrix, uintptr_t descriptor, void* context) {
    g_EngineMatrixUploadCount++;
    if (g_EngineMatrixUploadCount <= 12) {
        Log("BuddhaMatrixUpload #%u matrix=%p descriptor=0x%p context=%p active=%d first=(%.3f,%.3f,%.3f,%.3f)",
            g_EngineMatrixUploadCount, matrix, reinterpret_cast<void*>(descriptor), context,
            CameraHook_IsActive() ? 1 : 0,
            matrix ? matrix[0] : 0.0f, matrix ? matrix[1] : 0.0f,
            matrix ? matrix[2] : 0.0f, matrix ? matrix[3] : 0.0f);
    }
}

static void __cdecl ObserveSetFromVectors(void* position, void* rotation, void* camera) {
    if (!position || !rotation) return;
    const float* pos = reinterpret_cast<const float*>(position);
    const float* quat = reinterpret_cast<const float*>(rotation);
    if (!std::isfinite(pos[0]) || !std::isfinite(pos[1]) || !std::isfinite(pos[2]) ||
        !std::isfinite(quat[0]) || !std::isfinite(quat[1]) ||
        !std::isfinite(quat[2]) || !std::isfinite(quat[3])) return;
    g_SetFromVectorsCount++;
    if (g_SetFromVectorsCount <= 8 || g_SetFromVectorsCount % 300 == 0) {
        Log("SetFromVectors native-head #%u pos=(%.2f,%.2f,%.2f) q=(%.3f,%.3f,%.3f,%.3f) camera=%p active=%p",
            g_SetFromVectorsCount, pos[0], pos[1], pos[2],
            quat[0], quat[1], quat[2], quat[3], camera, g_ActiveCamera);
    }
}

extern "C" __declspec(naked) void Hook_SetFromVectors() {
    __asm {
        pushfd
        pushad
        mov eax, dword ptr [esp + 24]
        mov ecx, dword ptr [esp + 28]
        mov edx, dword ptr [esp + 40]
        push edx
        push ecx
        push eax
        call ObserveSetFromVectors
        add esp, 12
        popad
        popfd
        jmp dword ptr [g_OriginalSetFromVectors]
    }
}

static void __cdecl PrepareEngineMatrixUpload(float* matrix, uintptr_t descriptor, void* context) {
    (void)context;
    if (!matrix || !g_FirstPersonEnabled || !CameraHook_IsActive()) return;

    const uintptr_t viewDescriptor = g_ExecutableBase + DESCRIPTOR_VIEW;
    const uintptr_t worldDescriptor = g_ExecutableBase + DESCRIPTOR_WORLD;
    const uintptr_t projectionDescriptor = g_ExecutableBase + DESCRIPTOR_PROJECTION;
    const uintptr_t worldToProjectionDescriptor = g_ExecutableBase + DESCRIPTOR_WORLD_TO_PROJECTION;

    if (descriptor == projectionDescriptor) {
        if (g_StereoEyeIndex >= 0) {
            ApplyOpenXrEyeProjection(matrix, g_StereoEyeIndex);
        }
        memcpy(g_EngineProjection, matrix, sizeof(g_EngineProjection));
        g_HasEngineProjection = true;
        if (g_EngineMatrixReplacementCount < 8) {
            Log("BuddhaMatrixUpload projection captured descriptor=%p", reinterpret_cast<void*>(descriptor));
        }
        return;
    }

    if (descriptor == viewDescriptor) {
        float view[16];
        if (CameraHook_GetViewMatrix(view)) {
            memcpy(matrix, view, sizeof(view));
            g_EngineMatrixReplacementCount++;
            if (g_EngineMatrixReplacementCount <= 8) {
                Log("BuddhaMatrixUpload VIEW replaced #%u descriptor=%p", g_EngineMatrixReplacementCount,
                    reinterpret_cast<void*>(descriptor));
            }
        }
        return;
    }

    if (descriptor == worldDescriptor) {
        float world[16];
        if (CameraHook_GetWorldMatrix(world)) {
            memcpy(matrix, world, sizeof(world));
            if (g_EngineMatrixReplacementCount <= 8) {
                Log("BuddhaMatrixUpload WORLD replaced descriptor=%p", reinterpret_cast<void*>(descriptor));
            }
        }
        return;
    }

    // The same descriptor also uploads per-object model-view-projection
    // matrices. Replacing those discards the model transform and blanks the
    // world. Only the scene-owned camera carrier can take a pure view * P.
    if (descriptor == worldToProjectionDescriptor && g_W2POverride && g_HasEngineProjection &&
        g_LastSceneCamera && matrix == reinterpret_cast<float*>(
            reinterpret_cast<uint8_t*>(g_LastSceneCamera) + 0x850)) {
        float view[16];
        if (CameraHook_GetViewMatrix(view)) {
            float combined[16];
            MatrixMultiply(view, g_EngineProjection, combined);
            memcpy(matrix, combined, sizeof(combined));
        }
    }
}

extern "C" __declspec(naked) void Hook_UploadCameraMatrix() {
    __asm {
        push ebp
        mov ebp, esp
        push ebx
        push esi
        push edi
        mov ebx, eax
        mov esi, ecx
        mov edi, dword ptr [ebp + 8]

        push edi
        push ebx
        push esi
        call CameraHook_LogEngineMatrixUpload
        add esp, 12

        push edi
        push ebx
        push esi
        call PrepareEngineMatrixUpload
        add esp, 12

        mov eax, ebx
        mov ecx, esi
        push edi
        call dword ptr [g_OriginalUploadCameraMatrix]

        pop edi
        pop esi
        pop ebx
        pop ebp
        ret 4
    }
}


static bool IsPresentationScene(void* scene) {
    // Flash publishes at the UI tick rate, not the render rate. Requiring a
    // newly transferred Flash root discarded valid intermediate stereo frames.
    // Match the completed interpolation transaction and its active camera.
    const uint64_t now=GetTickCount64();
    bool matched=false;
    AcquireSRWLockShared(&g_PresentationSceneLock);
    for(const auto& stamp:g_PresentationScenes)
        matched|=stamp.scene==scene&&stamp.camera==g_ActiveCamera&&now>=stamp.tick&&now-stamp.tick<1000;
    ReleaseSRWLockShared(&g_PresentationSceneLock);
    if(matched)return true;
    __try {
        return scene && *reinterpret_cast<void**>(static_cast<uint8_t*>(scene)+0x11f0);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static void __stdcall Hook_InterpolateScene(void* output,void* previous,void* next,float alpha) {
    void* owner=nullptr;
    __try {
        // BuildRenderCamera publishes the source Camera at camera-block +528.
        // Interpolation clears it, so capture ownership before calling retail.
        void* a=previous?*reinterpret_cast<void**>(static_cast<uint8_t*>(previous)+0xb08):nullptr;
        void* b=next?*reinterpret_cast<void**>(static_cast<uint8_t*>(next)+0xb08):nullptr;
        if(g_ActiveCamera&&(a==g_ActiveCamera||b==g_ActiveCamera))owner=g_ActiveCamera;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    g_OriginalInterpolateScene(output,previous,next,alpha);
    if(owner && !CameraHook_IsCinematic()) {
        ApplyHeadsetVisibility(static_cast<uint8_t*>(output)+0x5e0);
        ApplyHeadsetVisibility(static_cast<uint8_t*>(output)+0xb10);
    }
    AcquireSRWLockExclusive(&g_PresentationSceneLock);
    for(auto& stamp:g_PresentationScenes)if(stamp.scene==output)stamp={};
    if(owner)g_PresentationScenes[g_PresentationSceneIndex++%32]={output,owner,GetTickCount64()};
    ReleaseSRWLockExclusive(&g_PresentationSceneLock);
}

bool CameraHook_IsCinematic() {
    __try {
        return reinterpret_cast<bool(__cdecl*)()>(g_ExecutableBase+0x191260)();
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static void __stdcall Hook_SetupSceneCamera(void* pSceneCamera, void* pContext) {
    const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    const bool mainWorld = !g_RenderingStereoCandidate &&
        caller == g_ExecutableBase + 0x2d6673 && IsPresentationScene(pSceneCamera);
    const bool stereoWorld = g_RenderingStereoCandidate &&
        pSceneCamera == g_LastSceneCamera && pContext == g_LastSceneContext;
    if (!mainWorld && !stereoWorld) {
        // Auxiliary cameras keep their native view. In particular, an eye
        // camera is not a light camera and must not replace a shadow pass.
        g_Original_SetupSceneCamera(pSceneCamera, pContext);
        return;
    }
    g_SetupSceneCamCount++;
    if (mainWorld) {
        g_LastSceneCamera = pSceneCamera;
        g_LastSceneContext = pContext;
        g_HasNativeShadowView = pSceneCamera != nullptr;
        if (pSceneCamera) {
            const auto* packet = static_cast<const uint8_t*>(pSceneCamera);
            memcpy(g_NativeShadowView, packet + 0x610, sizeof(g_NativeShadowView));
            for (unsigned cascade=0; cascade<4; ++cascade)
                memcpy(g_NativeShadowMatrices[cascade], packet + 0x1280 + cascade*0x290,
                       sizeof(g_NativeShadowMatrices[cascade]));
        }
    }

    if (pSceneCamera) {
        if (g_SetupSceneCamCount <= 3) {
            const float* pos = reinterpret_cast<const float*>(reinterpret_cast<uint8_t*>(pSceneCamera) + 0x5e0);
            const float* fwd = reinterpret_cast<const float*>(reinterpret_cast<uint8_t*>(pSceneCamera) + 0x5ec);
            const float* up  = reinterpret_cast<const float*>(reinterpret_cast<uint8_t*>(pSceneCamera) + 0x5f8);
            const float* rgt = reinterpret_cast<const float*>(reinterpret_cast<uint8_t*>(pSceneCamera) + 0x604);
            const float* m750 = reinterpret_cast<const float*>(reinterpret_cast<uint8_t*>(pSceneCamera) + 0x750);
            const float* m790 = reinterpret_cast<const float*>(reinterpret_cast<uint8_t*>(pSceneCamera) + 0x790);
            const float* m690 = reinterpret_cast<const float*>(reinterpret_cast<uint8_t*>(pSceneCamera) + 0x690);
            const float* m850 = reinterpret_cast<const float*>(reinterpret_cast<uint8_t*>(pSceneCamera) + 0x850);
            Log("SetupSceneCamera #%u NATIVE ENTRY: scene=%p active=%p\n"
                "  Pos=(%.2f, %.2f, %.2f) Fwd=(%.2f, %.2f, %.2f) Up=(%.2f, %.2f, %.2f) Rgt=(%.2f, %.2f, %.2f)\n"
                "  750 (World)=[%.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f]\n"
                "  790 (View) =[%.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f]\n"
                "  690 (Proj) =[%.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f]\n"
                "  850 (W2P)  =[%.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f]",
                g_SetupSceneCamCount, pSceneCamera, g_ActiveCamera,
                pos[0], pos[1], pos[2], fwd[0], fwd[1], fwd[2], up[0], up[1], up[2], rgt[0], rgt[1], rgt[2],
                m750[0], m750[1], m750[2], m750[3], m750[4], m750[5], m750[6], m750[7],
                m750[8], m750[9], m750[10], m750[11], m750[12], m750[13], m750[14], m750[15],
                m790[0], m790[1], m790[2], m790[3], m790[4], m790[5], m790[6], m790[7],
                m790[8], m790[9], m790[10], m790[11], m790[12], m790[13], m790[14], m790[15],
                m690[0], m690[1], m690[2], m690[3], m690[4], m690[5], m690[6], m690[7],
                m690[8], m690[9], m690[10], m690[11], m690[12], m690[13], m690[14], m690[15],
                m850[0], m850[1], m850[2], m850[3], m850[4], m850[5], m850[6], m850[7],
                m850[8], m850[9], m850[10], m850[11], m850[12], m850[13], m850[14], m850[15]);
        }
    }

    // SetupSceneCamera writes the native chase pose itself.  Apply the XR
    // pose both before and after the original: the pre-call write keeps any
    // native calculations that inspect the scene coherent, while the post-call
    // write is the authoritative final state consumed by the renderer.  The
    // old ordering only wrote before the original, so retail immediately
    // restored the third-person camera on every frame.
    auto applyFirstPerson = [&]() {
        if (pSceneCamera && g_FirstPersonEnabled && CameraHook_IsActive()) {
            __try {
                uint8_t* scene = reinterpret_cast<uint8_t*>(pSceneCamera);
                FirstPersonCameraState fpState{};
                if (ComputeFirstPersonCamera(fpState)) {
                    float* position = reinterpret_cast<float*>(scene + 0x5e0);
                    float* forward = reinterpret_cast<float*>(scene + 0x5ec);
                    float* up = reinterpret_cast<float*>(scene + 0x5f8);
                    float* right = reinterpret_cast<float*>(scene + 0x604);
                    float* viewToCamWorld = reinterpret_cast<float*>(scene + 0x750);
                    float* camWorldToView = reinterpret_cast<float*>(scene + 0x790);
                    float* viewToProject = reinterpret_cast<float*>(scene + 0x690);
                    float* camWorldToProject = reinterpret_cast<float*>(scene + 0x850);
                    float* worldToCameraPosition = reinterpret_cast<float*>(scene + 0x7d0);
                    float* cameraToWorldPosition = reinterpret_cast<float*>(scene + 0x810);

                    // Buddha's actor fade/cull path uses the scene camera
                    // position separately from the matrices submitted to the
                    // renderer.  Keep that native value when requested so a
                    // first-person eye does not make Eddie look like a
                    // camera-near object, while the actual view still uses
                    // the tracked eye below.
                    if (g_PreserveScenePosition && g_HasOriginalSceneWorld) {
                        position[0] = g_OriginalScenePosition[0];
                        position[1] = g_OriginalScenePosition[1];
                        position[2] = g_OriginalScenePosition[2];
                    } else {
                        position[0] = fpState.eye[0];
                        position[1] = fpState.eye[1];
                        position[2] = fpState.eye[2];
                    }
                    forward[0]  = fpState.forward[0];
                    forward[1]  = fpState.forward[1];
                    forward[2]  = fpState.forward[2];
                    up[0]       = fpState.up[0];
                    up[1]       = fpState.up[1];
                    up[2]       = fpState.up[2];
                    right[0]    = fpState.right[0];
                    right[1]    = fpState.right[1];
                    right[2]    = fpState.right[2];

                    memcpy(viewToCamWorld, fpState.viewToCamWorld, sizeof(fpState.viewToCamWorld));
                    memcpy(camWorldToView, fpState.camWorldToView, sizeof(fpState.camWorldToView));

                    if (g_StereoEyeIndex >= 0) {
                        ApplyOpenXrEyeProjection(viewToProject, g_StereoEyeIndex);
                    }
                    *reinterpret_cast<float*>(scene + 0x898) = 1.0f/viewToProject[0];
                    *reinterpret_cast<float*>(scene + 0x89c) = 1.0f/viewToProject[5];

                    // Retail keeps camera orientation in the 750/790 pair and
                    // the corresponding world-space translation in a second
                    // pair.  SetupSceneCamera copies these latter matrices to
                    // pContext (+0xd0/+0x110) after the four descriptor
                    // uploads.  Leaving them at the chase pose makes the
                    // renderer stay third-person even though the basis logs
                    // show the XR view.  Publish the tracked eye in both
                    // translation carriers; preserve the native identity
                    // rotation these matrices use for this render path.
                    worldToCameraPosition[12] = -fpState.eye[0];
                    worldToCameraPosition[13] = -fpState.eye[1];
                    worldToCameraPosition[14] = -fpState.eye[2];
                    worldToCameraPosition[15] = 1.0f;
                    cameraToWorldPosition[12] = fpState.eye[0];
                    cameraToWorldPosition[13] = fpState.eye[1];
                    cameraToWorldPosition[14] = fpState.eye[2];
                    cameraToWorldPosition[15] = 1.0f;

                    if (pContext) {
                        uint8_t* context = reinterpret_cast<uint8_t*>(pContext);
                        memcpy(context + 0xd0, worldToCameraPosition, 16 * sizeof(float));
                        memcpy(context + 0x110, cameraToWorldPosition, 16 * sizeof(float));
                    }

                    if (g_W2POverride && viewToProject[0] != 0.0f) {
                        MatrixMultiply(camWorldToView, viewToProject, camWorldToProject);
                        // Native camera layout (RVA 0x2ee050): +0x6d0 is
                        // inverse projection; +0x610/+0x650 carry the full
                        // view/world pair and +0x710 the full view-projection.
                        // Deferred lighting reconstructs positions with these,
                        // so changing only +0x690/+0x850 splits lighting from
                        // the geometry differently for the two eye frusta.
                        InvertCameraMatrix(viewToProject, reinterpret_cast<float*>(scene + 0x6d0));
                        MatrixMultiply(worldToCameraPosition, camWorldToView,
                                       reinterpret_cast<float*>(scene + 0x610));
                        MatrixMultiply(viewToCamWorld, cameraToWorldPosition,
                                       reinterpret_cast<float*>(scene + 0x650));
                        MatrixMultiply(reinterpret_cast<float*>(scene + 0x610), viewToProject,
                                       reinterpret_cast<float*>(scene + 0x710));
                    }

                    PublishEyeShadowMatrices(pSceneCamera, pContext);
                    PublishEyeReconstruction(pSceneCamera, pContext);

                    if (g_SetupSceneCamCount <= 3 || (g_SetupSceneCamCount % 300) == 0) {
                        Log("SetupSceneCamera #%u [FIRST PERSON 6DOF APPLIED]: Eye=(%.2f, %.2f, %.2f) Fwd=(%.2f, %.2f, %.2f) Up=(%.2f, %.2f, %.2f) Right=(%.2f, %.2f, %.2f) W2P0=(%.3f, %.3f, %.3f, %.3f)",
                            g_SetupSceneCamCount,
                            position[0], position[1], position[2],
                            forward[0], forward[1], forward[2],
                            up[0], up[1], up[2],
                            right[0], right[1], right[2],
                            camWorldToProject[0], camWorldToProject[1], camWorldToProject[2], camWorldToProject[3]);
                    }
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                Log("SetupSceneCamera: first-person basis write fault=0x%08X", GetExceptionCode());
            }
        }
    };

    applyFirstPerson();

    g_Original_SetupSceneCamera(pSceneCamera, pContext);
    if (pSceneCamera && g_FirstPersonEnabled && CameraHook_IsActive()) {
        __try {
            const uint8_t* scene = reinterpret_cast<const uint8_t*>(pSceneCamera);
            if (!IsBadReadPtr(scene + 0x750, 16 * sizeof(float)) &&
                !IsBadReadPtr(scene + 0x5e0, 3 * sizeof(float))) {
                memcpy(g_OriginalSceneWorld, scene + 0x750, sizeof(g_OriginalSceneWorld));
                memcpy(g_OriginalScenePosition, scene + 0x5e0, sizeof(g_OriginalScenePosition));
                g_HasOriginalSceneWorld = true;
                if (g_SetupSceneCamCount <= 2) {
                    Log("SetupSceneCamera sceneWorld=[%.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f]",
                        g_OriginalSceneWorld[0], g_OriginalSceneWorld[1], g_OriginalSceneWorld[2], g_OriginalSceneWorld[3],
                        g_OriginalSceneWorld[4], g_OriginalSceneWorld[5], g_OriginalSceneWorld[6], g_OriginalSceneWorld[7],
                        g_OriginalSceneWorld[8], g_OriginalSceneWorld[9], g_OriginalSceneWorld[10], g_OriginalSceneWorld[11],
                        g_OriginalSceneWorld[12], g_OriginalSceneWorld[13], g_OriginalSceneWorld[14], g_OriginalSceneWorld[15]);
                }
                if (g_SetupSceneCamCount <= 3) {
                    const float* m7d0 = reinterpret_cast<const float*>(scene + 0x7d0);
                    const float* m810 = reinterpret_cast<const float*>(scene + 0x810);
                    const float* c0d0 = pContext ? reinterpret_cast<const float*>(reinterpret_cast<const uint8_t*>(pContext) + 0xd0) : nullptr;
                    const float* c110 = pContext ? reinterpret_cast<const float*>(reinterpret_cast<const uint8_t*>(pContext) + 0x110) : nullptr;
                    Log("SetupSceneCamera final native matrices: 7d0=[%.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f] 810=[%.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f]",
                        m7d0[0], m7d0[1], m7d0[2], m7d0[3], m7d0[4], m7d0[5], m7d0[6], m7d0[7],
                        m7d0[8], m7d0[9], m7d0[10], m7d0[11], m7d0[12], m7d0[13], m7d0[14], m7d0[15],
                        m810[0], m810[1], m810[2], m810[3], m810[4], m810[5], m810[6], m810[7],
                        m810[8], m810[9], m810[10], m810[11], m810[12], m810[13], m810[14], m810[15]);
                    if (c0d0 && c110) {
                        Log("SetupSceneCamera copied context: d0=[%.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f] 110=[%.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f]",
                            c0d0[0], c0d0[1], c0d0[2], c0d0[3], c0d0[4], c0d0[5], c0d0[6], c0d0[7],
                            c0d0[8], c0d0[9], c0d0[10], c0d0[11], c0d0[12], c0d0[13], c0d0[14], c0d0[15],
                            c110[0], c110[1], c110[2], c110[3], c110[4], c110[5], c110[6], c110[7],
                            c110[8], c110[9], c110[10], c110[11], c110[12], c110[13], c110[14], c110[15]);
                    }
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            g_HasOriginalSceneWorld = false;
        }
    }
    // The original function has now finished and may have restored the chase
    // pose.  Re-apply the XR camera as the final scene-camera state.
    applyFirstPerson();
    if (g_SetupSceneCamCount <= 3 || (g_SetupSceneCamCount % 300) == 0) {
        __try {
            const uint8_t* scene = reinterpret_cast<const uint8_t*>(pSceneCamera);
            const float* pos = reinterpret_cast<const float*>(scene + 0x5e0);
            const float* fwd = reinterpret_cast<const float*>(scene + 0x5ec);
            const float* view = reinterpret_cast<const float*>(scene + 0x790);
            Log("SetupSceneCamera #%u scene=%p active=%p same=%d Pos=(%.2f,%.2f,%.2f) Fwd=(%.2f,%.2f,%.2f) ViewT=(%.2f,%.2f,%.2f)",
                g_SetupSceneCamCount, pSceneCamera, g_ActiveCamera,
                pSceneCamera == g_ActiveCamera ? 1 : 0,
                pos[0], pos[1], pos[2], fwd[0], fwd[1], fwd[2],
                view[12], view[13], view[14]);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Log("SetupSceneCamera #%u scene-state read fault=0x%08X",
                g_SetupSceneCamCount, GetExceptionCode());
        }
    }
}

static uint32_t __cdecl RenderSceneCandidateDispatch(void* renderer, void* context) {
    auto callOriginal = [&]() -> uint32_t {
        return g_OriginalRenderSceneCandidate
            ? CallOriginalRenderSceneCandidate(renderer, context) : 0u;
    };

    if (g_RenderingStereoCandidate || !CameraHook_IsActive() || CameraHook_IsCinematic() || !IsPresentationScene(context) ||
        !g_Original_SetupSceneCamera || !g_LastSceneCamera ||
        !g_LastSceneContext || g_LastSceneCamera != context ||
        !BLVR_GetD3D9Device()) {
        return callOriginal();
    }

    // Read the host's latest complete pose once, immediately before both eye
    // renders and the tracked rig. Reusing the previous Present's pose added
    // a whole game frame of positional lag (rotation timewarp cannot fix it).
    blvr_perf::Scope stereoCost(blvr_perf::Stereo);
    blvr_perf::Scope rigCost(blvr_perf::Rig);
    XrHost::Get().LatchRenderTracking();
    EyeView eyeViews[2]{};
    uint64_t poseSequence = 0;
    int64_t predictedDisplayTime = 0;
    if (!XrHost::Get().GetEyeView(0u, eyeViews[0]) ||
        !XrHost::Get().GetEyeView(1u, eyeViews[1]) ||
        !XrHost::Get().GetRenderTrackingStamp(poseSequence, predictedDisplayTime) ||
        poseSequence == 0 || predictedDisplayTime == 0) {
        return callOriginal();
    }

    g_StereoBodyAnchor = ReadBodyAnchor();
    void* eddie=PlayerViewRig_ResolveActor(g_pPlayerCharacter);
    const bool mounted=PlayerViewRig_ResolveMount(eddie)!=nullptr;
    g_StereoBodyAnchor.hasEyes=PlayerViewRig_Begin(context,eddie,g_StereoBodyAnchor.eyes,mounted);
    static void* lastRigOwner=nullptr;
    if(eddie!=lastRigOwner) {
        Log("Eddie rig owner: cameraActor=%p eddie=%p mounted=%d",g_pPlayerCharacter,eddie,mounted);
        lastRigOwner=eddie;
    }
    g_RenderingStereoCandidate = true;
    FirstPersonCameraState centerRigView{};
    float headWorld[16]{};
    const bool haveRigView=g_StereoBodyAnchor.hasEyes&&ComputeFirstPersonCamera(centerRigView);
    if(haveRigView) {
        std::memcpy(headWorld,centerRigView.viewToCamWorld,sizeof(headWorld));
        for(int k=0;k<3;++k)headWorld[12+k]=centerRigView.eye[k];
    }
    blvr_xr_bridge::RigFrame rig{};
    if(haveRigView&&
       PlayerViewRig_ReadTracked(poseSequence,predictedDisplayTime,XrHost::Get().GetRenderTrackingEpoch(),rig)) {
        ControllerState hands{};
        TouchControls touch{};
        const bool haveHands=XrHost::Get().GetControllerState(hands);
        touch.lt=hands.left.trigger;touch.rt=hands.right.trigger;touch.lg=hands.left.squeeze;touch.rg=hands.right.squeeze;
        touch.a=hands.right.a;touch.b=hands.right.b;touch.x=hands.left.x;touch.y=hands.left.y;
        touch.leftClick=hands.left.stickClick;touch.rightClick=hands.right.stickClick;touch.menu=hands.left.menu||hands.right.menu;
        const bool wheelGrip=!haveHands||!hands.right.tracked||TouchValue(touch,ActiveBindings().wheelGrip)>.6f;
        PlayerViewRig_ApplyTracked(rig,headWorld,mounted,wheelGrip);
    }
    uint64_t sourceFrameId = ++g_StereoSourceFrame;
    if (sourceFrameId == 0) sourceFrameId = ++g_StereoSourceFrame;
    if(haveRigView)PlayerViewRig_PublishRenderedUi(sourceFrameId,poseSequence,
        predictedDisplayTime,XrHost::Get().GetRenderTrackingEpoch(),headWorld,mounted);
    IDirect3DDevice9* device = BLVR_GetD3D9Device();
    uint32_t renderResult = 0;
    bool leftCaptured = false;
    bool rightCaptured = false;
    uint32_t leftIndexedDraws = 0;
    uint32_t rightIndexedDraws = 0;

    rigCost.stop();
    const int leftGpu=gpuProfiler.begin(device,device,sourceFrameId,0);
    blvr_perf::Scope leftCost(blvr_perf::LeftRender);
    g_StereoEyeIndex = 0;
    Hook_SetupSceneCamera(g_LastSceneCamera, g_LastSceneContext);
    PrepareWorldState(renderer, context);
    uint32_t drawCountBefore = BLVR_GetD3D9DrawCallCount();
    (void)callOriginal();
    CompleteWorldRender(renderer, context);
    leftCost.stop();
    gpuProfiler.end(device,leftGpu);
    leftIndexedDraws = BLVR_GetD3D9DrawCallCount() - drawCountBefore;
    if (leftIndexedDraws > 0) {
        leftCaptured = VideoCapture_CaptureStereoEye(
            device, 0u, sourceFrameId, poseSequence, predictedDisplayTime);
    }

    const int rightGpu=gpuProfiler.begin(device,device,sourceFrameId,1);
    blvr_perf::Scope rightCost(blvr_perf::RightRender);
    g_StereoEyeIndex = 1;
    Hook_SetupSceneCamera(g_LastSceneCamera, g_LastSceneContext);
    PrepareWorldState(renderer, context);
    drawCountBefore = BLVR_GetD3D9DrawCallCount();
    renderResult = callOriginal();
    CompleteWorldRender(renderer, context);
    rightCost.stop();
    gpuProfiler.end(device,rightGpu);
    rightIndexedDraws = BLVR_GetD3D9DrawCallCount() - drawCountBefore;
    if (rightIndexedDraws > 0) {
        rightCaptured = VideoCapture_CaptureStereoEye(
            device, 1u, sourceFrameId, poseSequence, predictedDisplayTime);
    }

    // The retail caller continues post-processing the right-eye depth/color
    // targets after this hook. Leave the matching camera in its packet;
    // switching it to the center eye here splits those remaining passes.
    g_StereoEyeIndex = -1;
    if (leftCaptured && rightCaptured && leftIndexedDraws > 0 && rightIndexedDraws > 0) {
        if (VideoCapture_PublishStereoPair(
                sourceFrameId, poseSequence, predictedDisplayTime)) {
            ++g_StereoPairLogCount;
            if (g_StereoPairLogCount <= 6u || (g_StereoPairLogCount % 120u) == 0u) {
                Log("RenderSceneCandidate: accepted same-tick L/R render #%u frame=%llu pose=%llu draws=(%u,%u)",
                    g_StereoPairLogCount,
                    static_cast<unsigned long long>(sourceFrameId),
                    static_cast<unsigned long long>(poseSequence),
                    leftIndexedDraws, rightIndexedDraws);
            }
        }
    } else if (sourceFrameId <= 6u || sourceFrameId % 120u == 0u) {
        Log("RenderSceneCandidate: stereo frame=%llu not published captured=(%d,%d) indexedDraws=(%u,%u)",
            static_cast<unsigned long long>(sourceFrameId),
            leftCaptured ? 1 : 0, rightCaptured ? 1 : 0,
            leftIndexedDraws, rightIndexedDraws);
    }

    g_StereoEyeIndex = -1;
    g_RenderingStereoCandidate = false;
    PlayerViewRig_End();
    return renderResult;
}

static void __stdcall Hook_SoloHudUpdate(void* hud, void* player) {
    g_OriginalSoloHudUpdate(hud, player);
    // Native bEnableRadial / bShowSoloNotes, after the Flash movie update.
    const auto* bytes = static_cast<const uint8_t*>(hud);
    const LONG flags = player ? (bytes[0x74] ? 1 : 0) | (bytes[0x75] ? 2 : 0) : 0;
    const LONG previous = InterlockedExchange(&g_SoloUiFlags, flags);
    InterlockedExchange(&g_SoloUiTick, static_cast<LONG>(GetTickCount()));
    InterlockedExchangePointer(&soloHud,player?hud:nullptr);
    if (previous != flags && Telemetry_Enabled()) {
        char fields[192];
        sprintf_s(fields, "\"mode\":%ld,\"hud\":%u,\"selected\":%u", flags,
            static_cast<unsigned>(reinterpret_cast<uintptr_t>(hud)),
            *reinterpret_cast<const uint32_t*>(bytes + 0x7c));
        Telemetry_Write("solo", fields);
        Telemetry_Flush();
    }
}

static bool __fastcall Hook_FlashRender(void* snapshot, void*, void* renderer) {
    const bool pending = static_cast<uint8_t*>(snapshot)[0x40] == 0;
    IDirect3DDevice9* device = BLVR_GetD3D9Device();
    // The native desktop composite clears this separate UI target opaque.
    // Transparent black preserves Flash's coverage for composition over the
    // stereo world. Do this before the display list, never key colors later.
    if (pending && device) device->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.0f, 0);
    const bool rendered = g_OriginalFlashRender(snapshot, renderer);
    // A nested display list can draw all the UI while the root returns false.
    // The consumed transition, rather than that return value, identifies it.
    const uint32_t soloFlags = GetTickCount() - static_cast<DWORD>(g_SoloUiTick) < 500u
        ? static_cast<uint32_t>(g_SoloUiFlags) : 0u;
    if (pending) VideoCapture_CaptureNativeUi(device, g_StereoSourceFrame, soloFlags);
    return rendered;
}

bool CameraHook_Init() {
    uintptr_t exeBase = reinterpret_cast<uintptr_t>(GetModuleHandleA(NULL));
    g_ExecutableBase = exeBase;
    g_RenderFlush = reinterpret_cast<void*>(exeBase + 0x2dc860);
    g_RenderLighting = reinterpret_cast<void*>(exeBase + 0x316c20);
    g_RenderWorldComposite = reinterpret_cast<void*>(exeBase + 0x2f6e60);
    g_RenderWorldEffects = reinterpret_cast<void*>(exeBase + 0x2f6ce0);
    g_RenderPostProcess = reinterpret_cast<NativePostProcess>(exeBase + 0x2dc9d0);
    g_TargetFlashRender = reinterpret_cast<void*>(exeBase + 0x73400);
    g_TargetSoloHudUpdate = reinterpret_cast<void*>(exeBase + 0x3a5740);
    g_RenderCommonState = reinterpret_cast<void*>(exeBase + 0x2f31c0);
    g_TargetSetCameraTransform = reinterpret_cast<void*>(exeBase + RVA_SET_CAMERA_TRANSFORM);
    g_TargetSetupSceneCamera = reinterpret_cast<void*>(exeBase + RVA_SETUP_SCENE_CAMERA);
    g_TargetBuildRenderCamera=reinterpret_cast<void*>(exeBase+0x2ee590);
    g_BuildNativeFrustum=reinterpret_cast<void*>(exeBase+0x28ebb0);
    g_TargetInterpolateScene=reinterpret_cast<void*>(exeBase+0x2f0240);
    g_TargetRenderSceneCandidate = reinterpret_cast<void*>(exeBase + RVA_RENDER_SCENE_CANDIDATE);
        g_TargetCameraUpdateWorldTransform = reinterpret_cast<void*>(exeBase + RVA_CAMERA_UPDATE_WORLD_TRANSFORM);
        g_TargetCharacterUpdateCamera = reinterpret_cast<void*>(exeBase + RVA_CHARACTER_UPDATE_CAMERA);
        g_GetJointTransform = reinterpret_cast<FnGetJointTransform>(exeBase + RVA_GET_JOINT_TRANSFORM);
        g_TargetUploadCameraMatrix = reinterpret_cast<void*>(exeBase + RVA_UPLOAD_CAMERA_MATRIX);

    Log("CameraHook: ExeBase=0x%p", (void*)exeBase);
    Log("CameraHook: Hooking CameraController::SetCameraTransform at 0x%p...", g_TargetSetCameraTransform);
    Log("CameraHook: Hooking SetupSceneCamera at 0x%p...", g_TargetSetupSceneCamera);
    Log("CameraHook: Hooking Camera::UpdateWorldTransform at 0x%p...", g_TargetCameraUpdateWorldTransform);
    Log("CameraHook: Hooking CoPhysicsCharacter::UpdateCamera at 0x%p...", g_TargetCharacterUpdateCamera);
    Log("CameraHook: Hooking Buddha camera matrix upload at 0x%p...", g_TargetUploadCameraMatrix);

    char fovEnv[32] = {0};
    GetEnvironmentVariableA("BLVR_FOV", fovEnv, sizeof(fovEnv));
    if (fovEnv[0]) {
        g_FovOverride = (float)atof(fovEnv);
        Log("CameraHook: User FOV override set to %.1f degrees", g_FovOverride);
    }

    char heightEnv[32] = {0};
    GetEnvironmentVariableA("BLVR_HEAD_HEIGHT", heightEnv, sizeof(heightEnv));
    if (heightEnv[0]) {
        g_HeadHeightOverride = (float)atof(heightEnv);
        Log("CameraHook: User HEAD_HEIGHT set to %.2f m", g_HeadHeightOverride);
    }

    char fwdEnv[32] = {0};
    GetEnvironmentVariableA("BLVR_HEAD_FORWARD", fwdEnv, sizeof(fwdEnv));
    if (fwdEnv[0]) {
        g_HeadForwardOverride = (float)atof(fwdEnv);
        Log("CameraHook: User HEAD_FORWARD set to %.2f m", g_HeadForwardOverride);
    }

    char firstPersonEnv[32] = {0};
    GetEnvironmentVariableA("BLVR_FIRST_PERSON", firstPersonEnv, sizeof(firstPersonEnv));
    if (firstPersonEnv[0]) {
        g_FirstPersonEnabled = firstPersonEnv[0] == '1' || _stricmp(firstPersonEnv, "true") == 0;
    }
    Log("CameraHook: First-person camera %s", g_FirstPersonEnabled ? "enabled" : "disabled (third-person default)");

    char zoomEnv[32] = {0};
    GetEnvironmentVariableA("BLVR_HEAD_ZOOM", zoomEnv, sizeof(zoomEnv));
    if (zoomEnv[0]) {
        g_HeadZoom = (float)atof(zoomEnv);
        Log("CameraHook: Native head zoom=%.2f", g_HeadZoom);
    }

    char nativeHeadEnv[16] = {0};
    GetEnvironmentVariableA("BLVR_NATIVE_BODY_HEAD", nativeHeadEnv, sizeof(nativeHeadEnv));
    g_NativeBodyHeadEnabled = nativeHeadEnv[0] == '1' || _stricmp(nativeHeadEnv, "true") == 0;
    Log("CameraHook: Native body head anchor %s", g_NativeBodyHeadEnabled ? "enabled" : "disabled");

    RetailInputBridgeSetGameplayMapper(&MapVrGameplayInput);
    TerrainProjection_Init();
    CameraRelativeEffects_Init();
    Log("CameraHook: native simulation camera preserved; independent XR heading, HMD-relative movement, 45-degree snap turn");

    char scenePositionEnv[16] = {0};
    GetEnvironmentVariableA("BLVR_PRESERVE_SCENE_POSITION", scenePositionEnv, sizeof(scenePositionEnv));
    g_PreserveScenePosition = scenePositionEnv[0] == '1' || _stricmp(scenePositionEnv, "true") == 0;
    g_PreserveScenePositionConfigured = true;
    Log("CameraHook: Native scene camera position %s", g_PreserveScenePosition ? "preserved for culling" : "follows first-person eye");

    // Renderer +0x40 is projection depth bias, not an eye-position vector.
    // Never write an HMD position there, including via legacy environment flags.

    char w2pEnv[16] = {0};
    GetEnvironmentVariableA("BLVR_W2P_OVERRIDE", w2pEnv, sizeof(w2pEnv));
    g_W2POverride = (w2pEnv[0] == '0' || _stricmp(w2pEnv, "false") == 0) ? false : true;
    g_W2POverrideConfigured = true;
    Log("CameraHook: World-to-projection override %s", g_W2POverride ? "enabled" : "disabled (native W2P preserved)");

    MH_STATUS s1 = MH_CreateHook(g_TargetSetCameraTransform,
                                 (void*)&Hook_SetCameraTransform,
                                 (void**)&g_Original_SetCameraTransform);
    MH_STATUS s2 = MH_CreateHook(g_TargetSetupSceneCamera,
                                 (void*)&Hook_SetupSceneCamera,
                                 (void**)&g_Original_SetupSceneCamera);
    MH_STATUS s3 = MH_CreateHook(g_TargetCameraUpdateWorldTransform,
                                 (void*)&Hook_CameraUpdateWorldTransform,
                                 (void**)&g_Original_CameraUpdateWorldTransform);
    MH_STATUS s4 = MH_CreateHook(g_TargetCharacterUpdateCamera,
                                 (void*)&Hook_CharacterUpdateCamera,
                                 (void**)&g_Original_CharacterUpdateCamera);

    if (s1 != MH_OK || s2 != MH_OK || s3 != MH_OK || s4 != MH_OK) {
        Log("CameraHook: MH_CreateHook failed: s1=%d s2=%d s3=%d s4=%d", (int)s1, (int)s2, (int)s3, (int)s4);
        return false;
    }

    MH_EnableHook(g_TargetSetCameraTransform);
    MH_EnableHook(g_TargetSetupSceneCamera);
    MH_EnableHook(g_TargetCameraUpdateWorldTransform);
    MH_EnableHook(g_TargetCharacterUpdateCamera);
    static const uint8_t snapshotPrologue[]={0x55,0x8b,0xec,0x83,0xe4,0xf0,0x81,0xec,0x14,0x02,0x00,0x00};
    if(std::memcmp(g_TargetBuildRenderCamera,snapshotPrologue,sizeof(snapshotPrologue))==0) {
        MH_STATUS visibility=MH_CreateHook(g_TargetBuildRenderCamera,reinterpret_cast<void*>(&Hook_BuildRenderCamera),
            reinterpret_cast<void**>(&g_OriginalBuildRenderCamera));
        if(visibility==MH_OK)visibility=MH_EnableHook(g_TargetBuildRenderCamera);
        Log("CameraHook: headset visibility snapshot hook status=%d",int(visibility));
    }
    static const uint8_t interpolationPrologue[]={0x55,0x8b,0xec,0xd9,0xee,0x83,0xec,0x54};
    if(std::memcmp(g_TargetInterpolateScene,interpolationPrologue,sizeof(interpolationPrologue))==0) {
        MH_STATUS interpolation=MH_CreateHook(g_TargetInterpolateScene,reinterpret_cast<void*>(&Hook_InterpolateScene),
            reinterpret_cast<void**>(&g_OriginalInterpolateScene));
        if(interpolation==MH_OK)interpolation=MH_EnableHook(g_TargetInterpolateScene);
        Log("CameraHook: render interpolation ownership hook status=%d",int(interpolation));
    }

    // Keep this hook build-bound until the runtime counter confirms the
    // candidate is the active scene render transaction for this executable.
    static const uint8_t expectedRenderPrologue[] = {
        0x55, 0x8b, 0xec, 0x83, 0xec, 0x18, 0x53, 0x8b, 0x5d, 0x08
    };
    if (g_TargetRenderSceneCandidate &&
        memcmp(g_TargetRenderSceneCandidate, expectedRenderPrologue,
               sizeof(expectedRenderPrologue)) == 0) {
        MH_STATUS renderStatus = MH_CreateHook(
            g_TargetRenderSceneCandidate,
            (void*)&Hook_RenderSceneCandidate,
            &g_OriginalRenderSceneCandidate);
        if (renderStatus == MH_OK) {
            renderStatus = MH_EnableHook(g_TargetRenderSceneCandidate);
        }
        Log("CameraHook: scene-render candidate hook at 0x%p status=%d",
            g_TargetRenderSceneCandidate, static_cast<int>(renderStatus));
    } else {
        Log("CameraHook: scene-render candidate signature mismatch at 0x%p; skipped",
            g_TargetRenderSceneCandidate);
    }

    static const uint8_t flashPrologue[] = {0x55,0x8b,0xec,0x56,0x8b,0xf1,0x80,0x7e,0x40,0x00};
    MH_STATUS flashStatus = MH_ERROR_UNSUPPORTED_FUNCTION;
    if (memcmp(g_TargetFlashRender, flashPrologue, sizeof(flashPrologue)) == 0) {
        flashStatus = MH_CreateHook(g_TargetFlashRender, reinterpret_cast<void*>(&Hook_FlashRender),
            reinterpret_cast<void**>(&g_OriginalFlashRender));
        if (flashStatus == MH_OK) flashStatus = MH_EnableHook(g_TargetFlashRender);
    }
    Log("CameraHook: native Flash capture hook status=%d (single snapshot for both eyes)", int(flashStatus));
    VrPrompts_Init();
    static const uint8_t soloPrologue[] = {0x55,0x8b,0xec,0x83,0xe4,0xc0,0x83,0xec,0x74};
    MH_STATUS soloStatus = MH_ERROR_UNSUPPORTED_FUNCTION;
    if (memcmp(g_TargetSoloHudUpdate, soloPrologue, sizeof(soloPrologue)) == 0) {
        soloStatus = MH_CreateHook(g_TargetSoloHudUpdate, reinterpret_cast<void*>(&Hook_SoloHudUpdate),
            reinterpret_cast<void**>(&g_OriginalSoloHudUpdate));
        if (soloStatus == MH_OK) soloStatus = MH_EnableHook(g_TargetSoloHudUpdate);
    }
    Log("CameraHook: native solo UI state hook status=%d", int(soloStatus));
    Log("CameraHook: SetCameraTransform and SetupSceneCamera hooks installed successfully!");
    return true;
}

void CameraHook_Shutdown() {
    TerrainProjection_Shutdown();
    CameraRelativeEffects_Shutdown();
    if(g_TargetBuildRenderCamera)MH_DisableHook(g_TargetBuildRenderCamera);
    if(g_TargetInterpolateScene)MH_DisableHook(g_TargetInterpolateScene);
    if (g_TargetSoloHudUpdate) MH_DisableHook(g_TargetSoloHudUpdate);
    if (g_TargetFlashRender) MH_DisableHook(g_TargetFlashRender);
    if (g_TargetSetCameraTransform) {
        MH_DisableHook(g_TargetSetCameraTransform);
    }
    if (g_TargetSetupSceneCamera) {
        MH_DisableHook(g_TargetSetupSceneCamera);
    }
    if (g_TargetRenderSceneCandidate) {
        MH_DisableHook(g_TargetRenderSceneCandidate);
    }
    if (g_TargetCameraUpdateWorldTransform) {
        MH_DisableHook(g_TargetCameraUpdateWorldTransform);
    }
    if (g_TargetCharacterUpdateCamera) {
        MH_DisableHook(g_TargetCharacterUpdateCamera);
    }
    if (g_UploadCameraMatrixHookInstalled && g_TargetUploadCameraMatrix) {
        MH_DisableHook(g_TargetUploadCameraMatrix);
        g_UploadCameraMatrixHookInstalled = false;
    }
}

void CameraHook_OnPresent() {
    // Refresh navigation/menu input here. The stereo transaction latches a
    // newer coherent host snapshot just before rendering, then freezes it
    // across both eyes, shader reconstruction and the tracked rig.
    XrHost::Get().Update();
    Pose6DoF navigationHead{};
    const bool navigationHeadValid = XrHost::Get().GetHeadPose(navigationHead);
    AcquireSRWLockExclusive(&g_BodyAnchorLock);
    g_NavigationHead = navigationHead;
    g_NavigationHeadValid = navigationHeadValid;
    ReleaseSRWLockExclusive(&g_BodyAnchorLock);

    // The camera-matrix helper is also used during the title/front-end path,
    // where its caller ABI is not live yet.  Install the proven bridge only
    // after the retail gameplay camera has published a valid Eddie root.  The
    // helper then receives the final view/world/projection descriptors that
    // feed Buddha's D3D9 renderer.
    char matrixUploadEnv[8] = {};
    GetEnvironmentVariableA("BLVR_MATRIX_UPLOAD_OVERRIDE", matrixUploadEnv, sizeof(matrixUploadEnv));
    bool enableMatrixUpload = (matrixUploadEnv[0] == '1' || _stricmp(matrixUploadEnv, "true") == 0);
    if (enableMatrixUpload && !g_UploadCameraMatrixHookInstalled &&
        g_TargetUploadCameraMatrix && CameraHook_IsActive()) {
        MH_STATUS uploadStatus = MH_CreateHook(g_TargetUploadCameraMatrix,
                                               (void*)&Hook_UploadCameraMatrix,
                                               &g_OriginalUploadCameraMatrix);
        if (uploadStatus == MH_OK) {
            MH_STATUS enableStatus = MH_EnableHook(g_TargetUploadCameraMatrix);
            if (enableStatus == MH_OK) {
                g_UploadCameraMatrixHookInstalled = true;
                Log("CameraHook: Buddha camera-matrix upload hook enabled after gameplay activation");
            } else {
                Log("CameraHook: Buddha camera-matrix upload enable failed: %d", (int)enableStatus);
            }
        } else {
            Log("CameraHook: Buddha camera-matrix upload hook creation failed: %d", (int)uploadStatus);
        }
    }

    static uint32_t lastLoggedPoseCount = 0;
    const uint32_t poseCount = XrHost::Get().GetPoseUpdateCount();
    if (poseCount != 0 && (poseCount <= 3 || poseCount == lastLoggedPoseCount + 120)) {
        lastLoggedPoseCount = poseCount;
        Log("XR pose transaction #%u mode=%s active=%d",
            poseCount,
            XrHost::Get().HasRealXr() ? "real" : (XrHost::Get().IsSimMode() ? "sim" : "none"),
            CameraHook_IsActive() ? 1 : 0);
    }

    if (Telemetry_Enabled()) {
        // Re-evaluate from the immutable pose transaction so this record
        // describes the same eye that subsequent camera hooks consume.
        FirstPersonCameraState fpState{};
        const bool fpValid = CameraHook_IsActive() && ComputeFirstPersonCamera(fpState);
        Pose6DoF hmdPose{};
        const bool hmdValid = XrHost::Get().GetHeadPose(hmdPose);

        static bool haveLastRoot = false;
        static bool haveLastEye = false;
        static float lastRoot[3] = {};
        static float lastEye[3] = {};
        float rootDelta = 0.0f;
        float eyeDelta = 0.0f;
        if (haveLastRoot && g_HasEddieRoot) {
            const float dx = g_EddieRootX - lastRoot[0];
            const float dy = g_EddieRootY - lastRoot[1];
            const float dz = g_EddieRootZ - lastRoot[2];
            rootDelta = sqrtf(dx * dx + dy * dy + dz * dz);
        }
        if (haveLastEye && fpValid) {
            const float dx = fpState.eye[0] - lastEye[0];
            const float dy = fpState.eye[1] - lastEye[1];
            const float dz = fpState.eye[2] - lastEye[2];
            eyeDelta = sqrtf(dx * dx + dy * dy + dz * dz);
        }
        if (g_HasEddieRoot) {
            lastRoot[0] = g_EddieRootX;
            lastRoot[1] = g_EddieRootY;
            lastRoot[2] = g_EddieRootZ;
            haveLastRoot = true;
        }
        if (fpValid) {
            lastEye[0] = fpState.eye[0];
            lastEye[1] = fpState.eye[1];
            lastEye[2] = fpState.eye[2];
            haveLastEye = true;
        }

        const char* anomaly = "none";
        if (!hmdValid) anomaly = "hmd_invalid";
        else if (!CameraHook_IsActive()) anomaly = "camera_inactive";
        else if (rootDelta > 10.0f || eyeDelta > 10.0f) anomaly = "origin_jump";
        else if (g_PreserveScenePosition && g_HasOriginalSceneWorld && g_HasEddieRoot) {
            const float sx = g_OriginalScenePosition[0] - g_EddieRootX;
            const float sy = g_OriginalScenePosition[1] - g_EddieRootY;
            const float sz = g_OriginalScenePosition[2] - g_EddieRootZ;
            if (sqrtf(sx * sx + sy * sy + sz * sz) > 25.0f) anomaly = "scene_root_divergence";
        }

        char fields[2200] = {};
        std::snprintf(fields, sizeof(fields),
            "\"pose_count\":%u,\"camera_updates\":%u,\"set_camera\":%u,\"setup_scene\":%u,"
            "\"active\":%d,\"fp_valid\":%d,\"hmd_valid\":%d,"
            "\"hmd_pos\":[%.5f,%.5f,%.5f],\"hmd_euler\":[%.5f,%.5f,%.5f],"
            "\"root\":[%.4f,%.4f,%.4f],\"target\":[%.4f,%.4f,%.4f],"
            "\"eye\":[%.4f,%.4f,%.4f],\"look\":[%.5f,%.5f,%.5f],"
            "\"body_fwd\":[%.5f,%.5f],\"scene_pos\":[%.4f,%.4f,%.4f],"
            "\"root_delta\":%.5f,\"eye_delta\":%.5f,"
            "\"has_root\":%d,\"has_transform\":%d,\"has_scene\":%d,"
            "\"preserve_scene\":%d,\"context_eye\":%d,\"w2p\":%d,"
            "\"engine_uploads\":%u,\"engine_replacements\":%u,\"anomaly\":\"%s\"",
            poseCount, g_CameraUpdateCount, g_SetCamCount, g_SetupSceneCamCount,
            CameraHook_IsActive() ? 1 : 0, fpValid ? 1 : 0, hmdValid ? 1 : 0,
            hmdPose.pos[0], hmdPose.pos[1], hmdPose.pos[2],
            hmdPose.yaw, hmdPose.pitch, hmdPose.roll,
            g_EddieRootX, g_EddieRootY, g_EddieRootZ,
            g_EddieTargetX, g_EddieTargetY, g_EddieTargetZ,
            g_FpEyeX, g_FpEyeY, g_FpEyeZ,
            g_FpLookX, g_FpLookY, g_FpLookZ,
            g_EddieFwdX, g_EddieFwdZ,
            g_OriginalScenePosition[0], g_OriginalScenePosition[1], g_OriginalScenePosition[2],
            rootDelta, eyeDelta,
            g_HasEddieRoot ? 1 : 0, g_HasEddieTransform ? 1 : 0,
            g_HasOriginalSceneWorld ? 1 : 0,
            g_PreserveScenePosition ? 1 : 0,
            g_ContextEyeOverride ? 1 : 0,
            g_W2POverride ? 1 : 0,
            g_EngineMatrixUploadCount, g_EngineMatrixReplacementCount, anomaly);
        Telemetry_Write("camera", fields);
    }
}

bool CameraHook_IsActive() {
    if (!g_ActiveCamera || !g_GameplayCameraController ||
        (!g_HasEddieRoot && !g_HasEddieTransform)) return false;
    // Pausing legitimately stops controller updates. Retain the camera while
    // its retail owner lives, without re-entering the title bootstrap.
    __try {
        return *reinterpret_cast<const uintptr_t*>(g_GameplayCameraController) ==
            reinterpret_cast<uintptr_t>(GetModuleHandleA(NULL)) + 0xad4ffc;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void CameraHook_SetNativeBodyHead(float x, float y, float z) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return;
    g_NativeBodyHead[0] = x;
    g_NativeBodyHead[1] = y;
    g_NativeBodyHead[2] = z;
    g_HasNativeBodyHead = true;
}

void CameraHook_SetNativeBodyHeadLocal(float x, float y, float z) {
    if ((!g_HasEddieRoot && !g_HasEddieTransform) || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return;
    const float rightX = -g_EddieFwdZ;
    const float rightZ = g_EddieFwdX;
    const float baseX = g_HasEddieRoot ? g_EddieRootX : g_EddieTargetX;
    const float baseY = g_HasEddieRoot ? g_EddieRootY : (g_EddieTargetY - 2.30f);
    const float baseZ = g_HasEddieRoot ? g_EddieRootZ : g_EddieTargetZ;
    CameraHook_SetNativeBodyHead(
        baseX + rightX * x + g_EddieFwdX * z,
        baseY + y,
        baseZ + rightZ * x + g_EddieFwdZ * z);
}

void CameraHook_SetNativeBodyHeadFromSceneOffset(float x, float y, float z) {
    if (!g_HasEddieTransform || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return;
    CameraHook_SetNativeBodyHead(g_FpEyeX + x, g_FpEyeY + y, g_FpEyeZ + z);
}

bool CameraHook_GetViewMatrix(float outMatrix[16]) {
    if (!outMatrix || !g_FirstPersonEnabled || !CameraHook_IsActive()) return false;
    FirstPersonCameraState fpState{};
    if (!ComputeFirstPersonCamera(fpState)) return false;
    memcpy(outMatrix, fpState.camWorldToView, sizeof(fpState.camWorldToView));
    return true;
}

bool CameraHook_GetWorldMatrix(float outMatrix[16]) {
    if (!outMatrix || !g_FirstPersonEnabled || !CameraHook_IsActive()) return false;
    FirstPersonCameraState fpState{};
    if (!ComputeFirstPersonCamera(fpState)) return false;
    memcpy(outMatrix, fpState.viewToCamWorld, sizeof(fpState.viewToCamWorld));
    return true;
}

bool CameraHook_GetViewCorrectionMatrix(float outMatrix[16]) {
    if (!outMatrix || !g_HasOriginalCameraPose || !CameraHook_GetViewMatrix(outMatrix)) return false;
    if (!g_HasOriginalSceneWorld) return false;

    // The engine uploads camera-relative model matrices. Convert them from
    // the original camera frame into the desired first-person frame:
    // model * inverse(oldCameraWorld) * desiredView.
    float oldWorld[16];
    memcpy(oldWorld, g_OriginalSceneWorld, sizeof(oldWorld));
    oldWorld[12] = g_OriginalScenePosition[0];
    oldWorld[13] = g_OriginalScenePosition[1];
    oldWorld[14] = g_OriginalScenePosition[2];
    oldWorld[15] = 1.0f;

    float oldView[16] = {
        oldWorld[0], oldWorld[4], oldWorld[8], 0.0f,
        oldWorld[1], oldWorld[5], oldWorld[9], 0.0f,
        oldWorld[2], oldWorld[6], oldWorld[10], 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    oldView[12] = -(oldWorld[12] * oldWorld[0] + oldWorld[13] * oldWorld[4] + oldWorld[14] * oldWorld[8]);
    oldView[13] = -(oldWorld[12] * oldWorld[1] + oldWorld[13] * oldWorld[5] + oldWorld[14] * oldWorld[9]);
    oldView[14] = -(oldWorld[12] * oldWorld[2] + oldWorld[13] * oldWorld[6] + oldWorld[14] * oldWorld[10]);

    float desiredView[16];
    if (!CameraHook_GetViewMatrix(desiredView)) return false;
    MatrixMultiply(oldView, desiredView, outMatrix);
    return true;
}

uint32_t CameraHook_GetUpdateCount() {
    return g_SetCamCount;
}

uint32_t CameraHook_GetGameplayRenderCount() {
    return static_cast<uint32_t>(g_RenderSceneCandidateGameplayCount);
}

uint64_t CameraHook_GetStereoSourceFrame() { return g_StereoSourceFrame; }
void CameraHook_ResetProfiler() { gpuProfiler.reset(); }

unsigned CameraHook_GetSoloNextNote() {
    if(!(g_SoloUiFlags&2)||GetTickCount()-static_cast<DWORD>(g_SoloUiTick)>500)return 0;
    // Retail HUD 0x7a5d42 and 0x7a61d1: selected solo has a packed note
    // count at +44 and 24-byte note records at +4c, beginning with action ID.
    // Read authored notes only. Retail retains ownership of timing and success.
    __try {
        const auto* hud=static_cast<const uint8_t*>(InterlockedCompareExchangePointer(&soloHud,nullptr,nullptr));
        if(!hud||!hud[0x75]||hud[0x77])return 0;
        const uint32_t selected=*reinterpret_cast<const uint32_t*>(hud+0x7c);
        const uint32_t played=*reinterpret_cast<const uint32_t*>(hud+0x4c);
        if(selected>=64||played>=128)return 0;
        const auto* owner=*reinterpret_cast<const uint8_t* const*>(hud+0x14);
        if(!owner)return 0;
        const auto* solos=*reinterpret_cast<const uint8_t* const*>(owner+8);
        if(!solos)return 0;
        const auto* solo=solos+selected*0x64;
        const uint32_t count=*reinterpret_cast<const uint32_t*>(solo+0x44)>>6;
        if(!count||count>128||played>=count)return 0;
        const auto* notes=*reinterpret_cast<const uint8_t* const*>(solo+0x4c);
        if(!notes)return 0;
        const uint32_t action=*reinterpret_cast<const uint32_t*>(notes+played*24);
        return action>=26&&action<=28?action-25:0;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return 0;}
}

} // namespace BLVR
