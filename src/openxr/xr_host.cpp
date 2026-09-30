#include "xr_host.h"
#include "blvr_pose_bridge.h"
#include "../diagnostics/log.h"
#include "../diagnostics/telemetry.h"
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <d3d11.h>
#include <dxgi.h>
#define XR_NO_PROTOTYPES
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <algorithm>
#include <cstring>
#include <vector>

namespace BLVR {

static const float PI = 3.14159265358979323846f;

static constexpr uint32_t FNVXR_POSE_MAGIC = 0x52505646; // FVPR
static constexpr uint32_t FNVXR_POSE_VERSION = 9;
static constexpr uint32_t FNVXR_XINPUT_MAGIC = 0x58564e46; // FNVX
static constexpr uint32_t FNVXR_XINPUT_VERSION = 4;
static constexpr uint32_t FNVXR_DINPUT_MAGIC = 0x49444e46; // FNDI
static constexpr uint32_t FNVXR_DINPUT_VERSION = 10;
static constexpr uint32_t FNVXR_TRACKING_HMD = 1u << 0;
static constexpr uint32_t FNVXR_TRACKING_LEFT_GRIP_CURRENT = 1u << 3;
static constexpr uint32_t FNVXR_TRACKING_RIGHT_GRIP_CURRENT = 1u << 4;
static constexpr uint32_t FNVXR_TRACKING_LEFT_AIM_CURRENT = 1u << 7;
static constexpr uint32_t FNVXR_TRACKING_RIGHT_AIM_CURRENT = 1u << 8;

#pragma pack(push, 8)
struct FnvxrPoseState {
    uint32_t magic;
    uint32_t version;
    volatile LONG sequence;
    uint64_t frame;
    int64_t predictedDisplayTime;
    float hmdRot[4];
    float hmdPos[3];
    float leftRot[4];
    float leftPos[3];
    float rightRot[4];
    float rightPos[3];
    float leftEyeRot[4];
    float leftEyePos[3];
    float rightEyeRot[4];
    float rightEyePos[3];
    float leftFov[4];
    float rightFov[4];
    float leftAimRot[4];
    float leftAimPos[3];
    float rightAimRot[4];
    float rightAimPos[3];
    uint32_t trackingFlags;
    uint32_t referenceSpaceGeneration;
    uint64_t producerEpoch;
    uint32_t recenterRequestId;
    uint32_t reserved;
};

struct FnvxrXInputState {
    uint32_t magic;
    uint32_t version;
    volatile LONG sequence;
    uint32_t packet;
    uint16_t buttons;
    uint8_t leftTrigger;
    uint8_t rightTrigger;
    int16_t leftThumbX;
    int16_t leftThumbY;
    int16_t rightThumbX;
    int16_t rightThumbY;
    uint8_t connected;
    uint8_t reserved[8];
};

struct FnvxrDInputState {
    uint32_t magic;
    uint32_t version;
    volatile LONG sequence;
    uint32_t frame;
    LONG clientX;
    LONG clientY;
    uint32_t pointerActive;
    uint32_t mouseClickPacket;
    uint32_t keyboardAcceptPacket;
    uint32_t menuInputActive;
    uint32_t gameplayControlsActive;
    int32_t leftStickX;
    int32_t leftStickY;
    int32_t rightStickX;
    int32_t rightStickY;
    uint32_t headLookActive;
    int32_t headLookX;
    int32_t headLookY;
    uint32_t gyroLookActive;
    int32_t gyroLookX;
    int32_t gyroLookY;
    int32_t leftGrip;
    int32_t rightGrip;
    uint32_t gameplayFlags;
    uint32_t aimTrigger;
};
#pragma pack(pop)

static_assert(sizeof(FnvxrPoseState) == 288, "FNVXR pose ABI changed");

static constexpr char FNVXR_POSE_MAPPING[] = "Local\\FNVXR_VR_Pose_State_v9";
static constexpr char FNVXR_XINPUT_MAPPING[] = "Local\\FNVXR_XInput_State_v4";
static constexpr char FNVXR_DINPUT_MAPPING[] = "Local\\FNVXR_DInput_State_v10";
static float g_FnvxrHeadPitchOffset = 0.0f;

static void QuaternionToEuler(const float q[4], float& yaw, float& pitch, float& roll);

template <typename T>
static bool ReadSequencedMapping(const T* source, T& snapshot) {
    if (!source) return false;
    for (int attempt = 0; attempt < 8; ++attempt) {
        const LONG before = source->sequence;
        if (before == 0 || (before & 1) != 0) {
            YieldProcessor();
            continue;
        }
        MemoryBarrier();
        memcpy(&snapshot, source, sizeof(snapshot));
        MemoryBarrier();
        const LONG after = source->sequence;
        if (before == after && (after & 1) == 0) return true;
        YieldProcessor();
    }
    return false;
}

static void CopyFnvxrPose(const float rotation[4], const float position[3], Pose6DoF& output) {
    memcpy(output.quat, rotation, sizeof(output.quat));
    memcpy(output.pos, position, sizeof(output.pos));
    QuaternionToEuler(output.quat, output.yaw, output.pitch, output.roll);
}

static void CopyBridgePose(const float rotation[4], const float position[3], Pose6DoF& output) {
    memcpy(output.quat, rotation, sizeof(output.quat));
    memcpy(output.pos, position, sizeof(output.pos));
    QuaternionToEuler(output.quat, output.yaw, output.pitch, output.roll);
}

struct OpenXrContext {
    HMODULE loader = nullptr;
    PFN_xrGetInstanceProcAddr getProc = nullptr;
    XrInstance instance = XR_NULL_HANDLE;
    XrSystemId system = XR_NULL_SYSTEM_ID;
    XrSession session = XR_NULL_HANDLE;
    XrSpace localSpace = XR_NULL_HANDLE;
    XrSpace viewSpace = XR_NULL_HANDLE;
    XrActionSet actionSet = XR_NULL_HANDLE;
    XrAction gripAction = XR_NULL_HANDLE;
    XrAction aimAction = XR_NULL_HANDLE;
    XrAction triggerAction = XR_NULL_HANDLE;
    XrAction squeezeAction = XR_NULL_HANDLE;
    XrAction stickAction = XR_NULL_HANDLE;
    XrAction aAction = XR_NULL_HANDLE;
    XrAction bAction = XR_NULL_HANDLE;
    XrAction xAction = XR_NULL_HANDLE;
    XrAction yAction = XR_NULL_HANDLE;
    XrAction menuAction = XR_NULL_HANDLE;
    XrPath hands[2]{};
    XrSpace gripSpaces[2]{};
    XrSpace aimSpaces[2]{};
    ID3D11Device* device = nullptr;
    bool sessionRunning = false;

#define BLVR_XR_PROC(name) PFN_##name name = nullptr;
    BLVR_XR_PROC(xrEnumerateInstanceExtensionProperties)
    BLVR_XR_PROC(xrCreateInstance)
    BLVR_XR_PROC(xrDestroyInstance)
    BLVR_XR_PROC(xrGetInstanceProperties)
    BLVR_XR_PROC(xrGetSystem)
    BLVR_XR_PROC(xrGetD3D11GraphicsRequirementsKHR)
    BLVR_XR_PROC(xrCreateSession)
    BLVR_XR_PROC(xrDestroySession)
    BLVR_XR_PROC(xrCreateReferenceSpace)
    BLVR_XR_PROC(xrDestroySpace)
    BLVR_XR_PROC(xrStringToPath)
    BLVR_XR_PROC(xrCreateActionSet)
    BLVR_XR_PROC(xrDestroyActionSet)
    BLVR_XR_PROC(xrCreateAction)
    BLVR_XR_PROC(xrDestroyAction)
    BLVR_XR_PROC(xrSuggestInteractionProfileBindings)
    BLVR_XR_PROC(xrCreateActionSpace)
    BLVR_XR_PROC(xrAttachSessionActionSets)
    BLVR_XR_PROC(xrPollEvent)
    BLVR_XR_PROC(xrBeginSession)
    BLVR_XR_PROC(xrEndSession)
    BLVR_XR_PROC(xrWaitFrame)
    BLVR_XR_PROC(xrBeginFrame)
    BLVR_XR_PROC(xrEndFrame)
    BLVR_XR_PROC(xrSyncActions)
    BLVR_XR_PROC(xrGetActionStatePose)
    BLVR_XR_PROC(xrGetActionStateBoolean)
    BLVR_XR_PROC(xrGetActionStateFloat)
    BLVR_XR_PROC(xrGetActionStateVector2f)
    BLVR_XR_PROC(xrLocateSpace)
#undef BLVR_XR_PROC
};

static void QuaternionToEuler(const float q[4], float& yaw, float& pitch, float& roll) {
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    const float sinPitch = std::clamp(2.0f * (w * x - y * z), -1.0f, 1.0f);
    pitch = asinf(sinPitch);
    yaw = atan2f(2.0f * (w * y + x * z), 1.0f - 2.0f * (x * x + y * y));
    roll = atan2f(2.0f * (w * z + x * y), 1.0f - 2.0f * (x * x + z * z));
}

static bool XrSucceeded(XrResult result, const char* operation) {
    if (XR_FAILED(result)) {
        Log("XrHost: %s failed with XrResult=%d", operation, static_cast<int>(result));
        return false;
    }
    return true;
}

template <typename T>
static bool LoadXrProc(OpenXrContext& context, const char* name, T& output) {
    return context.getProc &&
           XR_SUCCEEDED(context.getProc(context.instance, name,
               reinterpret_cast<PFN_xrVoidFunction*>(&output))) && output;
}

static bool ReadXrPose(OpenXrContext& context, XrSpace space, XrTime time, Pose6DoF& output) {
    XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
    if (!XrSucceeded(context.xrLocateSpace(space, context.localSpace, time, &location), "Locate XR pose")) return false;
    constexpr XrSpaceLocationFlags valid = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT |
                                             XR_SPACE_LOCATION_POSITION_VALID_BIT;
    if ((location.locationFlags & valid) != valid) return false;
    output.pos[0] = location.pose.position.x;
    output.pos[1] = location.pose.position.y;
    output.pos[2] = location.pose.position.z;
    output.quat[0] = location.pose.orientation.x;
    output.quat[1] = location.pose.orientation.y;
    output.quat[2] = location.pose.orientation.z;
    output.quat[3] = location.pose.orientation.w;
    QuaternionToEuler(output.quat, output.yaw, output.pitch, output.roll);
    return true;
}

static bool ReadXrBoolean(OpenXrContext& context, XrAction action, XrPath hand) {
    XrActionStateGetInfo info{XR_TYPE_ACTION_STATE_GET_INFO};
    info.action = action;
    info.subactionPath = hand;
    XrActionStateBoolean state{XR_TYPE_ACTION_STATE_BOOLEAN};
    if (!XrSucceeded(context.xrGetActionStateBoolean(context.session, &info, &state), "Read XR button")) return false;
    return state.isActive && state.currentState;
}

static float ReadXrFloat(OpenXrContext& context, XrAction action, XrPath hand) {
    XrActionStateGetInfo info{XR_TYPE_ACTION_STATE_GET_INFO};
    info.action = action;
    info.subactionPath = hand;
    XrActionStateFloat state{XR_TYPE_ACTION_STATE_FLOAT};
    if (!XrSucceeded(context.xrGetActionStateFloat(context.session, &info, &state), "Read XR trigger")) return 0.0f;
    return state.isActive && std::isfinite(state.currentState) ? std::clamp(state.currentState, 0.0f, 1.0f) : 0.0f;
}

static void ReadXrStick(OpenXrContext& context, XrAction action, XrPath hand, float& x, float& y) {
    XrActionStateGetInfo info{XR_TYPE_ACTION_STATE_GET_INFO};
    info.action = action;
    info.subactionPath = hand;
    XrActionStateVector2f state{XR_TYPE_ACTION_STATE_VECTOR2F};
    if (!XrSucceeded(context.xrGetActionStateVector2f(context.session, &info, &state), "Read XR stick") ||
        !state.isActive || !std::isfinite(state.currentState.x) || !std::isfinite(state.currentState.y)) {
        x = y = 0.0f;
        return;
    }
    x = std::clamp(state.currentState.x, -1.0f, 1.0f);
    y = std::clamp(state.currentState.y, -1.0f, 1.0f);
}

void EulerToQuaternion(float yaw, float pitch, float roll, float out[4]) {
    // Yaw around Y, pitch around X, roll around Z
    float cy = cosf(yaw * 0.5f);
    float sy = sinf(yaw * 0.5f);
    float cp = cosf(pitch * 0.5f);
    float sp = sinf(pitch * 0.5f);
    float cr = cosf(roll * 0.5f);
    float sr = sinf(roll * 0.5f);

    out[0] = cy * sp * cr + sy * cp * sr; // X
    out[1] = sy * cp * cr - cy * sp * sr; // Y
    out[2] = cy * cp * sr - sy * sp * cr; // Z
    out[3] = cy * cp * cr + sy * sp * sr; // W
}

void QuaternionMultiply(const float q1[4], const float q2[4], float out[4]) {
    // q = q1 * q2
    float x1 = q1[0], y1 = q1[1], z1 = q1[2], w1 = q1[3];
    float x2 = q2[0], y2 = q2[1], z2 = q2[2], w2 = q2[3];

    out[0] = w1 * x2 + x1 * w2 + y1 * z2 - z1 * y2;
    out[1] = w1 * y2 - x1 * z2 + y1 * w2 + z1 * x2;
    out[2] = w1 * z2 + x1 * y2 - y1 * x2 + z1 * w2;
    out[3] = w1 * w2 - x1 * x2 - y1 * y2 - z1 * z2;
}

void QuaternionRotateVector(const float q[4], const float v[3], float out[3]) {
    // v' = q * (v, 0) * q^(-1)
    float qv[4] = { v[0], v[1], v[2], 0.0f };
    float q_inv[4] = { -q[0], -q[1], -q[2], q[3] };
    float temp[4];
    QuaternionMultiply(q, qv, temp);
    QuaternionMultiply(temp, q_inv, qv);
    out[0] = qv[0];
    out[1] = qv[1];
    out[2] = qv[2];
}

static void NormalizeFnvxrPose(const Pose6DoF& raw, const Pose6DoF& origin, Pose6DoF& output) {
    const float inverseOriginRotation[4] = {
        -origin.quat[0], -origin.quat[1], -origin.quat[2], origin.quat[3]
    };
    const float delta[3] = {
        raw.pos[0] - origin.pos[0],
        raw.pos[1] - origin.pos[1],
        raw.pos[2] - origin.pos[2]
    };
    QuaternionRotateVector(inverseOriginRotation, delta, output.pos);
    QuaternionMultiply(inverseOriginRotation, raw.quat, output.quat);
    QuaternionToEuler(output.quat, output.yaw, output.pitch, output.roll);
}

XrHost& XrHost::Get() {
    static XrHost instance;
    return instance;
}

XrHost::XrHost() {
    QueryPerformanceFrequency(&m_PerfFrequency);
    QueryPerformanceCounter(&m_StartTime);
}

XrHost::~XrHost() {
    Shutdown();
}

bool XrHost::Init() {
    if (m_Initialized) return true;

    Log("XrHost: Initializing VR Subsystem...");

    char simEnv[32] = {0};
    GetEnvironmentVariableA("BLVR_SIM", simEnv, sizeof(simEnv));
    bool simEnvSet = (simEnv[0] == '1' || _stricmp(simEnv, "true") == 0);
    bool simMarkerFile = (GetFileAttributesA("blvr_sim.txt") != INVALID_FILE_ATTRIBUTES);
    bool forceSim = simEnvSet && simMarkerFile;
    if (simEnvSet && !simMarkerFile) {
        Log("XrHost: BLVR_SIM env var detected but blvr_sim.txt marker file is absent. Ignoring sim mode (running normal player mode).");
    }

    char xrBridgeEnv[32] = {0};
    GetEnvironmentVariableA("BLVR_XR_BRIDGE", xrBridgeEnv, sizeof(xrBridgeEnv));
    const bool useBlvrHost = xrBridgeEnv[0] == '1' || _stricmp(xrBridgeEnv, "true") == 0;

    // Brutal Legend is x86 and the Meta simulator runtime is x64.  When the
    // BLVR host is requested, this DLL consumes its pointer-free pose/input
    // mailbox and never attempts to load the runtime in the game process.
    if (useBlvrHost) {
        Log("XrHost: Using BLVR x64 OpenXR pose/input bridge.");
        m_BlvrPoseInputBridgeMode = InitBlvrPoseInputBridge();
        m_HasRealXr = m_BlvrPoseInputBridgeMode;
        m_IsSimMode = false;
        if (!m_BlvrPoseInputBridgeMode) {
            Log("XrHost: BLVR bridge unavailable; running flat until host is present.");
        }
    } else if (forceSim) {
        Log("XrHost: BLVR_SIM=1 and blvr_sim.txt detected. Enabling Headless Simulator Mode.");
        m_IsSimMode = true;
    } else {
        Log("XrHost: Checking for OpenXR runtime...");
        if (InitOpenXR()) {
            Log("XrHost: OpenXR runtime initialized successfully!");
            m_HasRealXr = true;
            m_IsSimMode = false;
        } else {
            Log("XrHost: No active OpenXR runtime found. Running in Flat-Screen First-Person Mode.");
            m_HasRealXr = false;
            m_IsSimMode = false;
        }
    }

    m_Initialized = true;
    Update();
    return true;
}

bool XrHost::InitOpenXR() {
    auto* context = new OpenXrContext();
    auto fail = [&]() {
        if (context->sessionRunning && context->xrEndSession) {
            context->xrEndSession(context->session);
        }
        if (context->xrDestroySpace) {
            for (auto& space : context->gripSpaces) if (space) context->xrDestroySpace(space);
            for (auto& space : context->aimSpaces) if (space) context->xrDestroySpace(space);
            if (context->viewSpace) context->xrDestroySpace(context->viewSpace);
            if (context->localSpace) context->xrDestroySpace(context->localSpace);
        }
        if (context->xrDestroySession && context->session) context->xrDestroySession(context->session);
        if (context->xrDestroyAction) {
            for (auto action : {context->gripAction, context->aimAction, context->triggerAction,
                                context->squeezeAction, context->stickAction, context->aAction,
                                context->bAction, context->xAction, context->yAction,
                                context->menuAction}) {
                if (action) context->xrDestroyAction(action);
            }
        }
        if (context->xrDestroyActionSet && context->actionSet) context->xrDestroyActionSet(context->actionSet);
        if (context->device) context->device->Release();
        if (context->xrDestroyInstance && context->instance) context->xrDestroyInstance(context->instance);
        if (context->loader) FreeLibrary(context->loader);
        delete context;
        return false;
    };

    char loaderPath[MAX_PATH] = {};
    GetEnvironmentVariableA("BLVR_OPENXR_LOADER", loaderPath, sizeof(loaderPath));
    const char* targetLoader = loaderPath[0] ? loaderPath : "openxr_loader.dll";
    Log("XrHost: Loading OpenXR loader from '%s'...", targetLoader);
    context->loader = LoadLibraryA(targetLoader);
    if (!context->loader) {
        Log("XrHost: OpenXR loader is not available (path=%s error=%lu)", targetLoader, GetLastError());
        return fail();
    }
    Log("XrHost: OpenXR loader loaded successfully at 0x%p", context->loader);
    context->getProc = reinterpret_cast<PFN_xrGetInstanceProcAddr>(
        GetProcAddress(context->loader, "xrGetInstanceProcAddr"));
    if (!context->getProc) {
        Log("XrHost: OpenXR loader has no xrGetInstanceProcAddr export");
        return fail();
    }
    Log("XrHost: xrGetInstanceProcAddr found at 0x%p", context->getProc);

    auto loadGlobal = [&](const char* name, PFN_xrVoidFunction* target) {
        return XR_SUCCEEDED(context->getProc(XR_NULL_HANDLE, name, target)) && *target;
    };
    if (!loadGlobal("xrEnumerateInstanceExtensionProperties",
                    reinterpret_cast<PFN_xrVoidFunction*>(&context->xrEnumerateInstanceExtensionProperties)) ||
        !loadGlobal("xrCreateInstance", reinterpret_cast<PFN_xrVoidFunction*>(&context->xrCreateInstance))) {
        Log("XrHost: OpenXR loader global entry points are incomplete");
        return fail();
    }
    Log("XrHost: Creating OpenXR instance with D3D11 extension...");
    const char* enabledExtensions[] = {XR_KHR_D3D11_ENABLE_EXTENSION_NAME};
    XrInstanceCreateInfo instanceInfo{XR_TYPE_INSTANCE_CREATE_INFO};
    strcpy_s(instanceInfo.applicationInfo.applicationName, "BrutalLegendVR");
    strcpy_s(instanceInfo.applicationInfo.engineName, "BLVR OpenXR input");
    instanceInfo.applicationInfo.applicationVersion = 1;
    instanceInfo.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
    instanceInfo.enabledExtensionCount = 1;
    instanceInfo.enabledExtensionNames = enabledExtensions;
    if (!XrSucceeded(context->xrCreateInstance(&instanceInfo, &context->instance), "Create instance")) return fail();

    if (!LoadXrProc(*context, "xrDestroyInstance", context->xrDestroyInstance) ||
        !LoadXrProc(*context, "xrGetSystem", context->xrGetSystem) ||
        !LoadXrProc(*context, "xrGetD3D11GraphicsRequirementsKHR", context->xrGetD3D11GraphicsRequirementsKHR) ||
        !LoadXrProc(*context, "xrCreateSession", context->xrCreateSession) ||
        !LoadXrProc(*context, "xrDestroySession", context->xrDestroySession) ||
        !LoadXrProc(*context, "xrCreateReferenceSpace", context->xrCreateReferenceSpace) ||
        !LoadXrProc(*context, "xrDestroySpace", context->xrDestroySpace) ||
        !LoadXrProc(*context, "xrStringToPath", context->xrStringToPath) ||
        !LoadXrProc(*context, "xrCreateActionSet", context->xrCreateActionSet) ||
        !LoadXrProc(*context, "xrDestroyActionSet", context->xrDestroyActionSet) ||
        !LoadXrProc(*context, "xrCreateAction", context->xrCreateAction) ||
        !LoadXrProc(*context, "xrDestroyAction", context->xrDestroyAction) ||
        !LoadXrProc(*context, "xrSuggestInteractionProfileBindings", context->xrSuggestInteractionProfileBindings) ||
        !LoadXrProc(*context, "xrCreateActionSpace", context->xrCreateActionSpace) ||
        !LoadXrProc(*context, "xrAttachSessionActionSets", context->xrAttachSessionActionSets) ||
        !LoadXrProc(*context, "xrPollEvent", context->xrPollEvent) ||
        !LoadXrProc(*context, "xrBeginSession", context->xrBeginSession) ||
        !LoadXrProc(*context, "xrEndSession", context->xrEndSession) ||
        !LoadXrProc(*context, "xrWaitFrame", context->xrWaitFrame) ||
        !LoadXrProc(*context, "xrBeginFrame", context->xrBeginFrame) ||
        !LoadXrProc(*context, "xrEndFrame", context->xrEndFrame) ||
        !LoadXrProc(*context, "xrSyncActions", context->xrSyncActions) ||
        !LoadXrProc(*context, "xrGetActionStatePose", context->xrGetActionStatePose) ||
        !LoadXrProc(*context, "xrGetActionStateBoolean", context->xrGetActionStateBoolean) ||
        !LoadXrProc(*context, "xrGetActionStateFloat", context->xrGetActionStateFloat) ||
        !LoadXrProc(*context, "xrGetActionStateVector2f", context->xrGetActionStateVector2f) ||
        !LoadXrProc(*context, "xrLocateSpace", context->xrLocateSpace)) {
        Log("XrHost: required OpenXR entry points are unavailable");
        return fail();
    }

    XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
    systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    if (!XrSucceeded(context->xrGetSystem(context->instance, &systemInfo, &context->system), "Get HMD system")) return fail();

    XrGraphicsRequirementsD3D11KHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};
    if (!XrSucceeded(context->xrGetD3D11GraphicsRequirementsKHR(context->instance, context->system, &requirements),
                     "Get D3D11 requirements")) return fail();

    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        Log("XrHost: Could not create DXGI factory");
        return fail();
    }
    IDXGIAdapter1* adapter = nullptr;
    for (UINT index = 0; ; ++index) {
        IDXGIAdapter1* candidate = nullptr;
        if (factory->EnumAdapters1(index, &candidate) == DXGI_ERROR_NOT_FOUND) break;
        if (!candidate) continue;
        DXGI_ADAPTER_DESC1 description{};
        candidate->GetDesc1(&description);
        if (description.AdapterLuid.LowPart == requirements.adapterLuid.LowPart &&
            description.AdapterLuid.HighPart == requirements.adapterLuid.HighPart) {
            adapter = candidate;
            break;
        }
        candidate->Release();
    }
    factory->Release();
    if (!adapter) {
        Log("XrHost: OpenXR GPU adapter was not found");
        return fail();
    }
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    D3D_FEATURE_LEVEL actualLevel{};
    const HRESULT deviceResult = D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
        &context->device, &actualLevel, nullptr);
    adapter->Release();
    if (FAILED(deviceResult) || actualLevel < requirements.minFeatureLevel) {
        Log("XrHost: D3D11 device creation failed or feature level is too low");
        return fail();
    }

    XrGraphicsBindingD3D11KHR graphics{XR_TYPE_GRAPHICS_BINDING_D3D11_KHR};
    graphics.device = context->device;
    XrSessionCreateInfo sessionInfo{XR_TYPE_SESSION_CREATE_INFO};
    sessionInfo.next = &graphics;
    sessionInfo.systemId = context->system;
    if (!XrSucceeded(context->xrCreateSession(context->instance, &sessionInfo, &context->session), "Create session")) return fail();

    XrReferenceSpaceCreateInfo spaceInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    spaceInfo.poseInReferenceSpace = {{0, 0, 0, 1}, {0, 0, 0}};
    spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    if (!XrSucceeded(context->xrCreateReferenceSpace(context->session, &spaceInfo, &context->localSpace),
                     "Create local space")) return fail();
    spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    if (!XrSucceeded(context->xrCreateReferenceSpace(context->session, &spaceInfo, &context->viewSpace),
                     "Create view space")) return fail();

    auto makePath = [&](const char* value, XrPath& result) {
        return XrSucceeded(context->xrStringToPath(context->instance, value, &result), value);
    };
    if (!makePath("/user/hand/left", context->hands[0]) || !makePath("/user/hand/right", context->hands[1])) return fail();
    XrActionSetCreateInfo actionSetInfo{XR_TYPE_ACTION_SET_CREATE_INFO};
    strcpy_s(actionSetInfo.actionSetName, "blvr");
    strcpy_s(actionSetInfo.localizedActionSetName, "Brutal Legend VR");
    actionSetInfo.priority = 0;
    if (!XrSucceeded(context->xrCreateActionSet(context->instance, &actionSetInfo, &context->actionSet),
                     "Create action set")) return fail();
    auto makeAction = [&](const char* name, XrActionType type, XrAction& result) {
        XrActionCreateInfo info{XR_TYPE_ACTION_CREATE_INFO};
        strcpy_s(info.actionName, name);
        strcpy_s(info.localizedActionName, name);
        info.actionType = type;
        info.countSubactionPaths = 2;
        info.subactionPaths = context->hands;
        return XrSucceeded(context->xrCreateAction(context->actionSet, &info, &result), name);
    };
    if (!makeAction("grip_pose", XR_ACTION_TYPE_POSE_INPUT, context->gripAction) ||
        !makeAction("aim_pose", XR_ACTION_TYPE_POSE_INPUT, context->aimAction) ||
        !makeAction("trigger", XR_ACTION_TYPE_FLOAT_INPUT, context->triggerAction) ||
        !makeAction("squeeze", XR_ACTION_TYPE_FLOAT_INPUT, context->squeezeAction) ||
        !makeAction("stick", XR_ACTION_TYPE_VECTOR2F_INPUT, context->stickAction) ||
        !makeAction("a", XR_ACTION_TYPE_BOOLEAN_INPUT, context->aAction) ||
        !makeAction("b", XR_ACTION_TYPE_BOOLEAN_INPUT, context->bAction) ||
        !makeAction("x", XR_ACTION_TYPE_BOOLEAN_INPUT, context->xAction) ||
        !makeAction("y", XR_ACTION_TYPE_BOOLEAN_INPUT, context->yAction) ||
        !makeAction("menu", XR_ACTION_TYPE_BOOLEAN_INPUT, context->menuAction)) return fail();

    std::vector<XrActionSuggestedBinding> bindings;
    auto bind = [&](XrAction action, const char* path) {
        XrPath xrPath{};
        if (makePath(path, xrPath)) bindings.push_back({action, xrPath});
    };
    bind(context->gripAction, "/user/hand/left/input/grip/pose");
    bind(context->gripAction, "/user/hand/right/input/grip/pose");
    bind(context->aimAction, "/user/hand/left/input/aim/pose");
    bind(context->aimAction, "/user/hand/right/input/aim/pose");
    bind(context->triggerAction, "/user/hand/left/input/trigger/value");
    bind(context->triggerAction, "/user/hand/right/input/trigger/value");
    bind(context->squeezeAction, "/user/hand/left/input/squeeze/value");
    bind(context->squeezeAction, "/user/hand/right/input/squeeze/value");
    bind(context->stickAction, "/user/hand/left/input/thumbstick");
    bind(context->stickAction, "/user/hand/right/input/thumbstick");
    bind(context->aAction, "/user/hand/right/input/a/click");
    bind(context->bAction, "/user/hand/right/input/b/click");
    bind(context->xAction, "/user/hand/left/input/x/click");
    bind(context->yAction, "/user/hand/left/input/y/click");
    bind(context->menuAction, "/user/hand/left/input/menu/click");
    bind(context->menuAction, "/user/hand/right/input/system/click");
    XrInteractionProfileSuggestedBinding suggested{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    if (!makePath("/interaction_profiles/oculus/touch_controller", suggested.interactionProfile)) return fail();
    suggested.countSuggestedBindings = static_cast<uint32_t>(bindings.size());
    suggested.suggestedBindings = bindings.data();
    const XrResult bindingResult = context->xrSuggestInteractionProfileBindings(context->instance, &suggested);
    if (XR_FAILED(bindingResult) && bindingResult != XR_ERROR_PATH_UNSUPPORTED) {
        Log("XrHost: controller binding suggestion failed with XrResult=%d", static_cast<int>(bindingResult));
        return fail();
    }

    for (size_t hand = 0; hand < 2; ++hand) {
        XrActionSpaceCreateInfo actionSpace{XR_TYPE_ACTION_SPACE_CREATE_INFO};
        actionSpace.action = context->gripAction;
        actionSpace.subactionPath = context->hands[hand];
        actionSpace.poseInActionSpace = {{0, 0, 0, 1}, {0, 0, 0}};
        if (!XrSucceeded(context->xrCreateActionSpace(context->session, &actionSpace, &context->gripSpaces[hand]),
                         "Create grip space")) return fail();
        actionSpace.action = context->aimAction;
        if (!XrSucceeded(context->xrCreateActionSpace(context->session, &actionSpace, &context->aimSpaces[hand]),
                         "Create aim space")) return fail();
    }
    XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    attach.countActionSets = 1;
    attach.actionSets = &context->actionSet;
    if (!XrSucceeded(context->xrAttachSessionActionSets(context->session, &attach), "Attach action set")) return fail();

    m_OpenXrContext = context;
    Log("XrHost: OpenXR controller input initialized for left/right grip and aim poses");
    return true;
}

bool XrHost::InitBlvrPoseInputBridge() {
    m_BlvrPoseInputMapping = OpenFileMappingA(
        FILE_MAP_READ,
        FALSE,
        BlvrPoseInputBridge::MappingName);
    if (!m_BlvrPoseInputMapping) {
        Log("XrHost: BLVR host pose mapping unavailable (error=%lu)", GetLastError());
        return false;
    }
    m_BlvrPoseInputState = MapViewOfFile(
        m_BlvrPoseInputMapping,
        FILE_MAP_READ,
        0,
        0,
        sizeof(BlvrPoseInputBridge::PoseBridge));
    if (!m_BlvrPoseInputState) {
        Log("XrHost: BLVR host pose mapping could not be mapped (error=%lu)", GetLastError());
        ShutdownBlvrPoseInputBridge();
        return false;
    }
    Log("XrHost: BLVR bridge mapping opened (384-byte pose/input mailbox).");
    return true;
}

void XrHost::ShutdownBlvrPoseInputBridge() {
    if (m_BlvrPoseInputState) {
        UnmapViewOfFile(m_BlvrPoseInputState);
        m_BlvrPoseInputState = nullptr;
    }
    if (m_BlvrPoseInputMapping) {
        CloseHandle(m_BlvrPoseInputMapping);
        m_BlvrPoseInputMapping = nullptr;
    }
    m_BlvrPoseInputLastFrame = 0;
    m_BlvrPoseInputLastSequence = 0;
    m_BlvrPoseInputLastFlags = 0;
    m_BlvrPoseInputLastHeartbeat = 0;
    m_BlvrPoseInputHeadOrigin = {};
    m_BlvrPoseInputEyeViews[0] = {};
    m_BlvrPoseInputEyeViews[1] = {};
    m_BlvrPoseInputPredictedDisplayTime = 0;
    m_BlvrPoseInputReferenceSpaceGeneration = 0;
    m_BlvrPoseInputRecenterRequestId = 0;
    m_BlvrPoseInputProducerEpoch = 0;
    m_BlvrPoseInputOriginFocused = false;
    m_BlvrPoseInputOriginValid = false;
    m_BlvrPoseInputLastReadValid = false;
    m_BlvrPoseInputPoseValid = false;
    m_BlvrPoseInputViewsValid = false;
    m_BlvrPoseInputBridgeMode = false;
}

void XrHost::UpdateBlvrPoseInputBridge() {
    BlvrPoseInputBridge::PoseBridge snapshot{};
    const auto* source = reinterpret_cast<const BlvrPoseInputBridge::PoseBridge*>(m_BlvrPoseInputState);
    const uint64_t previousFrame = m_BlvrPoseInputLastFrame;
    const bool readOk = ReadSequencedMapping(source, snapshot);
    m_BlvrPoseInputLastReadValid = readOk;
    m_BlvrPoseInputLastFrame = snapshot.frameId;
    m_BlvrPoseInputLastSequence = snapshot.sequence;
    m_BlvrPoseInputLastFlags = snapshot.flags;
    m_BlvrPoseInputLastHeartbeat = snapshot.heartbeatTickMs;
    const uint64_t now = GetTickCount64();
    const bool fresh = snapshot.heartbeatTickMs <= now && now - snapshot.heartbeatTickMs < 250;
    const bool bridgeValid = readOk && fresh &&
        snapshot.magic == BlvrPoseInputBridge::Magic &&
        snapshot.version == BlvrPoseInputBridge::Version &&
        snapshot.structBytes >= sizeof(BlvrPoseInputBridge::PoseBridge) &&
        snapshot.frameId != 0 &&
        (snapshot.flags & BlvrPoseInputBridge::SessionRunning) != 0 &&
        (snapshot.flags & BlvrPoseInputBridge::HmdValid) != 0;
    if (!bridgeValid) {
        // A missed mailbox read, loading stall or focus transition invalidates
        // input, not the user's room origin. Relatching on recovery silently
        // zeroed their yaw and position, producing a snap back during play.
        m_BlvrPoseInputPoseValid = false;
        m_BlvrPoseInputViewsValid = false;
        m_Controllers = {};
        m_Controllers.runtimeActive = false;
        return;
    }

    Pose6DoF rawHead{};
    CopyBridgePose(snapshot.hmdPose.orientation, snapshot.hmdPose.position, rawHead);
    const bool inputFocused = (snapshot.flags & BlvrPoseInputBridge::InputFocused) != 0;
    const bool originChanged = !m_BlvrPoseInputOriginValid ||
        snapshot.producerEpoch != m_BlvrPoseInputProducerEpoch ||
        snapshot.referenceSpaceGeneration != m_BlvrPoseInputReferenceSpaceGeneration ||
        snapshot.recenterRequestId != m_BlvrPoseInputRecenterRequestId;
    if (originChanged) {
        m_BlvrPoseInputHeadOrigin = rawHead;
        // Preserve translation at the first frame but recenter yaw so the
        // retail body heading remains the gameplay authority.
        const float originYaw = rawHead.yaw;
        EulerToQuaternion(originYaw, 0.0f, 0.0f, m_BlvrPoseInputHeadOrigin.quat);
        m_BlvrPoseInputReferenceSpaceGeneration = snapshot.referenceSpaceGeneration;
        m_BlvrPoseInputRecenterRequestId = snapshot.recenterRequestId;
        m_BlvrPoseInputProducerEpoch = snapshot.producerEpoch;
        m_BlvrPoseInputOriginFocused = inputFocused;
        m_BlvrPoseInputOriginValid = true;
        Log("XrHost: BLVR host origin latched generation=%u recenter=%u focused=%d raw=(%.3f, %.3f, %.3f)",
            snapshot.referenceSpaceGeneration,
            snapshot.recenterRequestId,
            inputFocused ? 1 : 0,
            rawHead.pos[0], rawHead.pos[1], rawHead.pos[2]);
    }
    NormalizeFnvxrPose(rawHead, m_BlvrPoseInputHeadOrigin, m_CurrentPose);
    m_BlvrPoseInputPoseValid = true;
    m_BlvrPoseInputPredictedDisplayTime = snapshot.predictedDisplayTime;

    m_BlvrPoseInputViewsValid = false;
    if ((snapshot.flags & BlvrPoseInputBridge::ViewsValid) != 0 &&
        snapshot.predictedDisplayTime != 0) {
        EyeView candidateViews[2]{};
        bool viewsFinite = true;
        for (uint32_t eye = 0; eye < 2; ++eye) {
            Pose6DoF rawEye{};
            const auto& sourceEye = snapshot.eyePoses[eye];
            CopyBridgePose(sourceEye.orientation, sourceEye.position, rawEye);
            NormalizeFnvxrPose(rawEye, m_BlvrPoseInputHeadOrigin, candidateViews[eye].pose);
            for (float component : candidateViews[eye].pose.pos) {
                viewsFinite = viewsFinite && std::isfinite(component);
            }
            for (float component : candidateViews[eye].pose.quat) {
                viewsFinite = viewsFinite && std::isfinite(component);
            }
            for (size_t axis = 0; axis < 4; ++axis) {
                candidateViews[eye].fov[axis] = snapshot.eyeFovs[eye][axis];
                viewsFinite = viewsFinite && std::isfinite(candidateViews[eye].fov[axis]);
            }
            viewsFinite = viewsFinite &&
                candidateViews[eye].fov[0] < -0.05f &&
                candidateViews[eye].fov[1] > 0.05f &&
                candidateViews[eye].fov[2] > 0.05f &&
                candidateViews[eye].fov[3] < -0.05f &&
                candidateViews[eye].fov[0] > -1.55f &&
                candidateViews[eye].fov[1] < 1.55f &&
                candidateViews[eye].fov[2] < 1.55f &&
                candidateViews[eye].fov[3] > -1.55f;
        }
        const float eyeDeltaX = candidateViews[1].pose.pos[0] - candidateViews[0].pose.pos[0];
        const float eyeDeltaY = candidateViews[1].pose.pos[1] - candidateViews[0].pose.pos[1];
        const float eyeDeltaZ = candidateViews[1].pose.pos[2] - candidateViews[0].pose.pos[2];
        const float eyeSeparation = sqrtf(eyeDeltaX * eyeDeltaX +
                                          eyeDeltaY * eyeDeltaY +
                                          eyeDeltaZ * eyeDeltaZ);
        viewsFinite = viewsFinite && std::isfinite(eyeSeparation) &&
            eyeSeparation >= 0.01f && eyeSeparation <= 0.10f;
        if (viewsFinite) {
            m_BlvrPoseInputEyeViews[0] = candidateViews[0];
            m_BlvrPoseInputEyeViews[1] = candidateViews[1];
            m_BlvrPoseInputViewsValid = true;
        }
    }

    ControllerState controllers{};
    controllers.runtimeActive = true;
    const auto copyController = [&](size_t hand, bool valid, HandState& destination) {
        const auto& sourceController = snapshot.controllers[hand];
        destination.tracked = valid &&
            (sourceController.activeFlags & BlvrPoseInputBridge::GripPose) != 0;
        destination.aimTracked = valid &&
            (sourceController.activeFlags & BlvrPoseInputBridge::AimPose) != 0;
        Pose6DoF rawGrip{};
        Pose6DoF rawAim{};
        CopyBridgePose(sourceController.gripPose.orientation, sourceController.gripPose.position, rawGrip);
        CopyBridgePose(sourceController.aimPose.orientation, sourceController.aimPose.position, rawAim);
        if (destination.tracked) NormalizeFnvxrPose(rawGrip, m_BlvrPoseInputHeadOrigin, destination.pose);
        if (destination.aimTracked) NormalizeFnvxrPose(rawAim, m_BlvrPoseInputHeadOrigin, destination.aimPose);
        if (sourceController.activeFlags & BlvrPoseInputBridge::Trigger)
            destination.trigger = std::clamp(sourceController.trigger, 0.0f, 1.0f);
        if (sourceController.activeFlags & BlvrPoseInputBridge::Squeeze)
            destination.squeeze = std::clamp(sourceController.squeeze, 0.0f, 1.0f);
        if (sourceController.activeFlags & BlvrPoseInputBridge::Thumbstick) {
            destination.stickX = std::clamp(sourceController.thumbstickX, -1.0f, 1.0f);
            destination.stickY = std::clamp(sourceController.thumbstickY, -1.0f, 1.0f);
        }
        destination.a = hand == 1 && (sourceController.buttons & BlvrPoseInputBridge::PrimaryClick) != 0;
        destination.b = hand == 1 && (sourceController.buttons & BlvrPoseInputBridge::SecondaryClick) != 0;
        destination.x = hand == 0 && (sourceController.buttons & BlvrPoseInputBridge::PrimaryClick) != 0;
        destination.y = hand == 0 && (sourceController.buttons & BlvrPoseInputBridge::SecondaryClick) != 0;
        destination.menu = (sourceController.buttons & BlvrPoseInputBridge::MenuClick) != 0;
        destination.stickClick = (sourceController.buttons & BlvrPoseInputBridge::ThumbstickPressed) != 0;
    };
    copyController(
        0,
        (snapshot.flags & BlvrPoseInputBridge::LeftControllerValid) != 0,
        controllers.left);
    copyController(
        1,
        (snapshot.flags & BlvrPoseInputBridge::RightControllerValid) != 0,
        controllers.right);

    static uint32_t lastLeftButtons = 0;
    static uint32_t lastRightButtons = 0;
    static uint32_t lastLeftActive = 0;
    static uint32_t lastRightActive = 0;
    if (snapshot.controllers[0].buttons != lastLeftButtons ||
        snapshot.controllers[1].buttons != lastRightButtons ||
        snapshot.controllers[0].activeFlags != lastLeftActive ||
        snapshot.controllers[1].activeFlags != lastRightActive) {
        Log("XrHost: BLVR input frame=%llu L(buttons=0x%08X active=0x%08X) R(buttons=0x%08X active=0x%08X)",
            static_cast<unsigned long long>(snapshot.frameId),
            snapshot.controllers[0].buttons,
            snapshot.controllers[0].activeFlags,
            snapshot.controllers[1].buttons,
            snapshot.controllers[1].activeFlags);
        lastLeftButtons = snapshot.controllers[0].buttons;
        lastRightButtons = snapshot.controllers[1].buttons;
        lastLeftActive = snapshot.controllers[0].activeFlags;
        lastRightActive = snapshot.controllers[1].activeFlags;
    }

    m_Controllers = controllers;
    if (snapshot.frameId != previousFrame && (snapshot.frameId % 120u) == 0u) {
        Log("XrHost: BLVR frame=%llu head=1 leftGrip=%d rightGrip=%d leftAim=%d rightAim=%d",
            static_cast<unsigned long long>(snapshot.frameId),
            controllers.left.tracked ? 1 : 0,
            controllers.right.tracked ? 1 : 0,
            controllers.left.aimTracked ? 1 : 0,
            controllers.right.aimTracked ? 1 : 0);
    }
}

bool XrHost::InitFnvxrBridge() {
    m_FnvxrPoseMapping = OpenFileMappingA(FILE_MAP_READ, FALSE, FNVXR_POSE_MAPPING);
    if (!m_FnvxrPoseMapping) {
        Log("XrHost: FNVXR pose mapping unavailable (error=%lu)", GetLastError());
        return false;
    }
    m_FnvxrPoseState = MapViewOfFile(m_FnvxrPoseMapping, FILE_MAP_READ, 0, 0, sizeof(FnvxrPoseState));
    if (!m_FnvxrPoseState) {
        Log("XrHost: FNVXR pose mapping could not be mapped (error=%lu)", GetLastError());
        ShutdownFnvxrBridge();
        return false;
    }

    // The FNVVR host waits for the retail consumer acknowledgement byte before
    // authorizing controller mutation.  BLVR is the retail consumer in this
    // bridge, so it needs read/write access to that one host-owned byte.
    m_FnvxrXInputMapping = OpenFileMappingA(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, FNVXR_XINPUT_MAPPING);
    if (m_FnvxrXInputMapping) {
        m_FnvxrXInputState = MapViewOfFile(m_FnvxrXInputMapping,
                                           FILE_MAP_READ | FILE_MAP_WRITE,
                                           0, 0, sizeof(FnvxrXInputState));
    }
    m_FnvxrDInputMapping = OpenFileMappingA(FILE_MAP_READ, FALSE, FNVXR_DINPUT_MAPPING);
    if (m_FnvxrDInputMapping) {
        m_FnvxrDInputState = MapViewOfFile(m_FnvxrDInputMapping, FILE_MAP_READ, 0, 0, sizeof(FnvxrDInputState));
    }
    Log("XrHost: FNVXR bridge mappings opened pose=1 xinput=%d dinput=%d",
        m_FnvxrXInputState ? 1 : 0, m_FnvxrDInputState ? 1 : 0);
    return true;
}

void XrHost::ShutdownFnvxrBridge() {
    if (m_FnvxrPoseState) {
        UnmapViewOfFile(m_FnvxrPoseState);
        m_FnvxrPoseState = nullptr;
    }
    if (m_FnvxrXInputState) {
        UnmapViewOfFile(m_FnvxrXInputState);
        m_FnvxrXInputState = nullptr;
    }
    if (m_FnvxrDInputState) {
        UnmapViewOfFile(m_FnvxrDInputState);
        m_FnvxrDInputState = nullptr;
    }
    if (m_FnvxrPoseMapping) {
        CloseHandle(m_FnvxrPoseMapping);
        m_FnvxrPoseMapping = nullptr;
    }
    if (m_FnvxrXInputMapping) {
        CloseHandle(m_FnvxrXInputMapping);
        m_FnvxrXInputMapping = nullptr;
    }
    if (m_FnvxrDInputMapping) {
        CloseHandle(m_FnvxrDInputMapping);
        m_FnvxrDInputMapping = nullptr;
    }
    m_FnvxrLastFrame = 0;
    m_FnvxrHeadOrigin = {};
    m_FnvxrOriginReferenceSpaceGeneration = 0;
    m_FnvxrOriginRecenterRequestId = 0;
    m_FnvxrOriginValid = false;
}

void XrHost::UpdateFnvxrBridge() {
    FnvxrPoseState pose{};
    if (!ReadSequencedMapping(reinterpret_cast<const FnvxrPoseState*>(m_FnvxrPoseState), pose) ||
        pose.magic != FNVXR_POSE_MAGIC || pose.version != FNVXR_POSE_VERSION ||
        pose.frame == 0 || (pose.trackingFlags & FNVXR_TRACKING_HMD) == 0) {
        m_Controllers = {};
        m_Controllers.runtimeActive = false;
        return;
    }

    Pose6DoF rawHead{};
    CopyFnvxrPose(pose.hmdRot, pose.hmdPos, rawHead);
    const bool originChanged = !m_FnvxrOriginValid
        || pose.referenceSpaceGeneration != m_FnvxrOriginReferenceSpaceGeneration
        || pose.recenterRequestId != m_FnvxrOriginRecenterRequestId;
    if (originChanged) {
        m_FnvxrHeadOrigin = rawHead;
        const float originYaw = rawHead.yaw;
        EulerToQuaternion(originYaw, 0.0f, 0.0f, m_FnvxrHeadOrigin.quat);
        m_FnvxrOriginReferenceSpaceGeneration = pose.referenceSpaceGeneration;
        m_FnvxrOriginRecenterRequestId = pose.recenterRequestId;
        m_FnvxrOriginValid = true;
        Log("XrHost: FNVXR head origin latched generation=%u recenter=%u raw=(%.3f, %.3f, %.3f)",
            pose.referenceSpaceGeneration, pose.recenterRequestId,
            rawHead.pos[0], rawHead.pos[1], rawHead.pos[2]);
    }
    NormalizeFnvxrPose(rawHead, m_FnvxrHeadOrigin, m_CurrentPose);
    if (g_FnvxrHeadPitchOffset != 0.0f) {
        m_CurrentPose.pitch += g_FnvxrHeadPitchOffset;
        EulerToQuaternion(m_CurrentPose.yaw, m_CurrentPose.pitch,
                          m_CurrentPose.roll, m_CurrentPose.quat);
    }
    ControllerState controllers{};
    controllers.runtimeActive = true;
    controllers.left.tracked = (pose.trackingFlags & FNVXR_TRACKING_LEFT_GRIP_CURRENT) != 0;
    controllers.right.tracked = (pose.trackingFlags & FNVXR_TRACKING_RIGHT_GRIP_CURRENT) != 0;
    controllers.left.aimTracked = (pose.trackingFlags & FNVXR_TRACKING_LEFT_AIM_CURRENT) != 0;
    controllers.right.aimTracked = (pose.trackingFlags & FNVXR_TRACKING_RIGHT_AIM_CURRENT) != 0;
    Pose6DoF rawLeftGrip{};
    Pose6DoF rawRightGrip{};
    Pose6DoF rawLeftAim{};
    Pose6DoF rawRightAim{};
    CopyFnvxrPose(pose.leftRot, pose.leftPos, rawLeftGrip);
    CopyFnvxrPose(pose.rightRot, pose.rightPos, rawRightGrip);
    CopyFnvxrPose(pose.leftAimRot, pose.leftAimPos, rawLeftAim);
    CopyFnvxrPose(pose.rightAimRot, pose.rightAimPos, rawRightAim);
    NormalizeFnvxrPose(rawLeftGrip, m_FnvxrHeadOrigin, controllers.left.pose);
    NormalizeFnvxrPose(rawRightGrip, m_FnvxrHeadOrigin, controllers.right.pose);
    NormalizeFnvxrPose(rawLeftAim, m_FnvxrHeadOrigin, controllers.left.aimPose);
    NormalizeFnvxrPose(rawRightAim, m_FnvxrHeadOrigin, controllers.right.aimPose);

    FnvxrXInputState xinput{};
    if (ReadSequencedMapping(reinterpret_cast<const FnvxrXInputState*>(m_FnvxrXInputState), xinput) &&
        xinput.magic == FNVXR_XINPUT_MAGIC && xinput.version == FNVXR_XINPUT_VERSION &&
        xinput.connected) {
        // This is the same acknowledgement contract used by FNVVR's native
        // XInput proxy.  It enables the host's controller route while keeping
        // the simulator input in shared memory rather than desktop input.
        if (m_FnvxrXInputState && m_FnvxrXInputMapping) {
            auto* writable = reinterpret_cast<FnvxrXInputState*>(m_FnvxrXInputState);
            InterlockedExchange8(reinterpret_cast<volatile char*>(&writable->reserved[0]), 1);
        }
        controllers.left.trigger = static_cast<float>(xinput.leftTrigger) / 255.0f;
        controllers.right.trigger = static_cast<float>(xinput.rightTrigger) / 255.0f;
        controllers.left.stickX = static_cast<float>(xinput.leftThumbX) / 32767.0f;
        controllers.left.stickY = static_cast<float>(xinput.leftThumbY) / 32767.0f;
        controllers.right.stickX = static_cast<float>(xinput.rightThumbX) / 32767.0f;
        controllers.right.stickY = static_cast<float>(xinput.rightThumbY) / 32767.0f;
        controllers.right.a = (xinput.buttons & (1u << 0)) != 0;
        controllers.right.b = (xinput.buttons & (1u << 1)) != 0;
        controllers.left.x = (xinput.buttons & (1u << 2)) != 0;
        controllers.left.y = (xinput.buttons & (1u << 3)) != 0;
        controllers.left.menu = (xinput.buttons & (1u << 4)) != 0;
        controllers.right.menu = (xinput.buttons & (1u << 5)) != 0;
        controllers.left.stickClick = (xinput.buttons & (1u << 6)) != 0;
        controllers.right.stickClick = (xinput.buttons & (1u << 7)) != 0;
    }

    FnvxrDInputState dinput{};
    if (ReadSequencedMapping(reinterpret_cast<const FnvxrDInputState*>(m_FnvxrDInputState), dinput) &&
        dinput.magic == FNVXR_DINPUT_MAGIC && dinput.version == FNVXR_DINPUT_VERSION) {
        controllers.left.squeeze = std::clamp(static_cast<float>(dinput.leftGrip) / 32767.0f, 0.0f, 1.0f);
        controllers.right.squeeze = std::clamp(static_cast<float>(dinput.rightGrip) / 32767.0f, 0.0f, 1.0f);
    }

    m_Controllers = controllers;
    if (pose.frame != m_FnvxrLastFrame && (pose.frame % 120u) == 0u) {
        Log("XrHost: FNVXR frame=%llu head=1 leftGrip=%d rightGrip=%d leftAim=%d rightAim=%d buttons=%d",
            static_cast<unsigned long long>(pose.frame),
            controllers.left.tracked ? 1 : 0, controllers.right.tracked ? 1 : 0,
            controllers.left.aimTracked ? 1 : 0, controllers.right.aimTracked ? 1 : 0,
            xinput.connected ? 1 : 0);
    }
    m_FnvxrLastFrame = pose.frame;
}

void XrHost::Shutdown() {
    if (!m_Initialized) return;
    Log("XrHost: Shutting down VR Subsystem.");
    ShutdownBlvrPoseInputBridge();
    ShutdownFnvxrBridge();
    ShutdownOpenXR();
    m_Initialized = false;
}

void XrHost::LatchRenderTracking() {
    // The native host owns xrWaitFrame/xrLocateViews and publishes HMD,
    // binocular views and hands together. This is a nonblocking mailbox
    // read, not another OpenXR frame or an independent per-eye locate.
    if(m_Initialized&&m_BlvrPoseInputBridgeMode)UpdateBlvrPoseInputBridge();
}

void XrHost::Update() {
    if (!m_Initialized) return;

    if (m_BlvrPoseInputBridgeMode) {
        UpdateBlvrPoseInputBridge();
    } else if (m_FnvxrBridgeMode) {
        UpdateFnvxrBridge();
    } else if (m_IsSimMode) {
        UpdateSimPose();
    } else if (m_HasRealXr) {
        UpdateOpenXR();
    }
    m_PoseUpdateCount++;

    // One compact, frame-correlatable XR record.  The bridge-specific fields
    // make stale pose/input and reference-space resets visible without having
    // to infer them from the rendered movie.
    if (Telemetry_Enabled()) {
        const auto& left = m_Controllers.left;
        const auto& right = m_Controllers.right;
        char fields[2400] = {};
        std::snprintf(fields, sizeof(fields),
            "\"pose_count\":%u,\"mode\":\"%s\",\"runtime_active\":%d,"
            "\"bridge_read\":%d,\"bridge_frame\":%llu,\"bridge_seq\":%d,"
            "\"bridge_flags\":%u,\"bridge_heartbeat\":%llu,"
            "\"bridge_ref_gen\":%u,\"bridge_recenter\":%u,"
            "\"head_pos\":[%.5f,%.5f,%.5f],\"head_euler\":[%.5f,%.5f,%.5f],"
            "\"left\":{\"tracked\":%d,\"aim\":%d,\"pos\":[%.4f,%.4f,%.4f],"
            "\"stick\":[%.4f,%.4f],\"trigger\":%.4f,\"squeeze\":%.4f,\"buttons\":[%d,%d,%d,%d,%d,%d]},"
            "\"right\":{\"tracked\":%d,\"aim\":%d,\"pos\":[%.4f,%.4f,%.4f],"
            "\"stick\":[%.4f,%.4f],\"trigger\":%.4f,\"squeeze\":%.4f,\"buttons\":[%d,%d,%d,%d,%d,%d]}",
            m_PoseUpdateCount,
            m_BlvrPoseInputBridgeMode ? "elliott" : (m_FnvxrBridgeMode ? "fnvxr" : (m_IsSimMode ? "sim" : "openxr")),
            m_Controllers.runtimeActive ? 1 : 0,
            m_BlvrPoseInputLastReadValid ? 1 : 0,
            static_cast<unsigned long long>(m_BlvrPoseInputLastFrame),
            m_BlvrPoseInputLastSequence,
            m_BlvrPoseInputLastFlags,
            static_cast<unsigned long long>(m_BlvrPoseInputLastHeartbeat),
            m_BlvrPoseInputReferenceSpaceGeneration,
            m_BlvrPoseInputRecenterRequestId,
            m_CurrentPose.pos[0], m_CurrentPose.pos[1], m_CurrentPose.pos[2],
            m_CurrentPose.yaw, m_CurrentPose.pitch, m_CurrentPose.roll,
            left.tracked ? 1 : 0, left.aimTracked ? 1 : 0,
            left.pose.pos[0], left.pose.pos[1], left.pose.pos[2],
            left.stickX, left.stickY, left.trigger, left.squeeze,
            left.a ? 1 : 0, left.b ? 1 : 0, left.x ? 1 : 0,
            left.y ? 1 : 0, left.menu ? 1 : 0, left.stickClick ? 1 : 0,
            right.tracked ? 1 : 0, right.aimTracked ? 1 : 0,
            right.pose.pos[0], right.pose.pos[1], right.pose.pos[2],
            right.stickX, right.stickY, right.trigger, right.squeeze,
            right.a ? 1 : 0, right.b ? 1 : 0, right.x ? 1 : 0,
            right.y ? 1 : 0, right.menu ? 1 : 0, right.stickClick ? 1 : 0);
        Telemetry_Write("xr_tick", fields);
    }
}

void XrHost::ShutdownOpenXR() {
    auto* context = reinterpret_cast<OpenXrContext*>(m_OpenXrContext);
    if (!context) return;
    if (context->sessionRunning && context->xrEndSession) {
        context->xrEndSession(context->session);
    }
    if (context->xrDestroySpace) {
        for (auto& space : context->gripSpaces) if (space) context->xrDestroySpace(space);
        for (auto& space : context->aimSpaces) if (space) context->xrDestroySpace(space);
        if (context->viewSpace) context->xrDestroySpace(context->viewSpace);
        if (context->localSpace) context->xrDestroySpace(context->localSpace);
    }
    if (context->xrDestroySession && context->session) context->xrDestroySession(context->session);
    if (context->xrDestroyAction) {
        for (auto action : {context->gripAction, context->aimAction, context->triggerAction,
                            context->squeezeAction, context->stickAction, context->aAction,
                                context->bAction, context->xAction, context->yAction,
                                context->menuAction}) {
            if (action) context->xrDestroyAction(action);
        }
    }
    if (context->xrDestroyActionSet && context->actionSet) context->xrDestroyActionSet(context->actionSet);
    if (context->device) context->device->Release();
    if (context->xrDestroyInstance && context->instance) context->xrDestroyInstance(context->instance);
    if (context->loader) FreeLibrary(context->loader);
    delete context;
    m_OpenXrContext = nullptr;
    m_HasRealXr = false;
    m_Controllers = {};
}

void XrHost::UpdateOpenXR() {
    auto* context = reinterpret_cast<OpenXrContext*>(m_OpenXrContext);
    if (!context) return;

    for (;;) {
        XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
        const XrResult result = context->xrPollEvent(context->instance, &event);
        if (result == XR_EVENT_UNAVAILABLE) break;
        if (XR_FAILED(result)) {
            Log("XrHost: xrPollEvent failed with XrResult=%d", static_cast<int>(result));
            return;
        }
        if (event.type != XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) continue;
        const auto& state = *reinterpret_cast<const XrEventDataSessionStateChanged*>(&event);
        if (state.session != context->session) continue;
        if (state.state == XR_SESSION_STATE_READY && !context->sessionRunning) {
            XrSessionBeginInfo begin{XR_TYPE_SESSION_BEGIN_INFO};
            begin.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
            XrResult beginResult = context->xrBeginSession(context->session, &begin);
            if (XR_FAILED(beginResult)) {
                begin.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_MONO;
                beginResult = context->xrBeginSession(context->session, &begin);
            }
            if (XR_SUCCEEDED(beginResult)) {
                context->sessionRunning = true;
                Log("XrHost: OpenXR session started successfully!");
            } else {
                Log("XrHost: xrBeginSession failed with XrResult=%d", static_cast<int>(beginResult));
            }
        } else if (state.state == XR_SESSION_STATE_STOPPING && context->sessionRunning) {
            context->xrEndSession(context->session);
            context->sessionRunning = false;
        } else if (state.state == XR_SESSION_STATE_EXITING || state.state == XR_SESSION_STATE_LOSS_PENDING) {
            context->sessionRunning = false;
        }
    }

    if (!context->sessionRunning) return;
    XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO};
    XrFrameState frame{XR_TYPE_FRAME_STATE};
    if (!XrSucceeded(context->xrWaitFrame(context->session, &wait, &frame), "Wait XR frame")) return;
    XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO};
    if (!XrSucceeded(context->xrBeginFrame(context->session, &begin), "Begin XR frame")) return;
    XrActiveActionSet active{context->actionSet, XR_NULL_PATH};
    XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO};
    sync.countActiveActionSets = 1;
    sync.activeActionSets = &active;
    if (!XrSucceeded(context->xrSyncActions(context->session, &sync), "Sync XR actions")) return;

    Pose6DoF head{};
    if (ReadXrPose(*context, context->viewSpace, frame.predictedDisplayTime, head)) {
        m_CurrentPose = head;
    }
    ControllerState controllers{};
    controllers.runtimeActive = true;
    for (size_t hand = 0; hand < 2; ++hand) {
        HandState& state = hand == 0 ? controllers.left : controllers.right;
        state.tracked = ReadXrPose(*context, context->gripSpaces[hand], frame.predictedDisplayTime, state.pose);
        state.trigger = ReadXrFloat(*context, context->triggerAction, context->hands[hand]);
        state.squeeze = ReadXrFloat(*context, context->squeezeAction, context->hands[hand]);
        ReadXrStick(*context, context->stickAction, context->hands[hand], state.stickX, state.stickY);
        state.a = ReadXrBoolean(*context, context->aAction, context->hands[hand]);
        state.b = ReadXrBoolean(*context, context->bAction, context->hands[hand]);
        state.x = ReadXrBoolean(*context, context->xAction, context->hands[hand]);
        state.y = ReadXrBoolean(*context, context->yAction, context->hands[hand]);
        state.menu = ReadXrBoolean(*context, context->menuAction, context->hands[hand]);
    }
    m_Controllers = controllers;

    XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};
    end.displayTime = frame.predictedDisplayTime;
    end.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    end.layerCount = 0;
    context->xrEndFrame(context->session, &end);
}

void XrHost::UpdateSimPose() {
    if (m_GameplayTick > 0) {
        // Slow-down, controlled 20.0s (1200 ticks @ 60 FPS) first-person VR demonstration
        const uint32_t cycle = m_GameplayTick % 1200;

        auto SmoothStep = [](float t0, float t1, float t) -> float {
            if (t <= t0) return 0.0f;
            if (t >= t1) return 1.0f;
            float u = (t - t0) / (t1 - t0);
            return 0.5f * (1.0f - cosf(3.14159265f * u));
        };

        if (cycle < 180) {
            // Phase 1 (0.0s - 3.0s): Stand still, looking straight ahead at open world
            m_CurrentPose.pitch = 0.0f;
            m_CurrentPose.yaw = 0.0f;
            m_CurrentPose.roll = 0.0f;
            m_CurrentPose.pos[0] = 0.0f;
            m_CurrentPose.pos[1] = 0.004f * sinf((float)cycle * 0.05f); // Gentle breathing
            m_CurrentPose.pos[2] = 0.0f;
        } else if (cycle < 360) {
            // Phase 2 (3.0s - 6.0s): Stand still, smoothly tilt head down directly to chest & arms
            float a = SmoothStep(180.0f, 360.0f, (float)cycle);
            m_CurrentPose.pitch = -0.78f * a; // Smooth downward gaze to -45 degrees
            m_CurrentPose.yaw = 0.0f;
            m_CurrentPose.roll = 0.0f;
            m_CurrentPose.pos[0] = 0.0f;
            m_CurrentPose.pos[1] = 0.0f;
            m_CurrentPose.pos[2] = 0.0f;
        } else if (cycle < 540) {
            // Phase 3 (6.0s - 9.0s): Stand still, slowly pan left to inspect left arm, spiked bracer & Clementine
            float panLeft = (cycle < 450) ? SmoothStep(360.0f, 430.0f, (float)cycle) : (1.0f - SmoothStep(470.0f, 540.0f, (float)cycle));
            m_CurrentPose.pitch = -0.78f;
            m_CurrentPose.yaw = 0.42f * panLeft; // Turn left to inspect arm
            m_CurrentPose.roll = -0.04f * panLeft;
            m_CurrentPose.pos[0] = -0.015f * panLeft;
            m_CurrentPose.pos[1] = 0.0f;
            m_CurrentPose.pos[2] = 0.0f;
        } else if (cycle < 720) {
            // Phase 4 (9.0s - 12.0s): Stand still, slowly pan right to inspect right arm, spiked bracer & Broadaxe
            float panRight = (cycle < 630) ? SmoothStep(540.0f, 610.0f, (float)cycle) : (1.0f - SmoothStep(650.0f, 720.0f, (float)cycle));
            m_CurrentPose.pitch = -0.78f;
            m_CurrentPose.yaw = -0.42f * panRight; // Turn right to inspect arm
            m_CurrentPose.roll = 0.04f * panRight;
            m_CurrentPose.pos[0] = 0.015f * panRight;
            m_CurrentPose.pos[1] = 0.0f;
            m_CurrentPose.pos[2] = 0.0f;
        } else if (cycle < 900) {
            // Phase 5 (12.0s - 15.0s): Stand still, look down at boots/ground, tilt horizon, and elevate back up
            float a = SmoothStep(720.0f, 800.0f, (float)cycle);
            float b = SmoothStep(800.0f, 900.0f, (float)cycle);
            m_CurrentPose.pitch = -0.78f * (1.0f - a) - 0.95f * a * (1.0f - b); // Dip to -54 deg to see boots, then up to 0
            m_CurrentPose.yaw = 0.0f;
            m_CurrentPose.roll = 0.06f * sinf(b * 3.14159265f); // Gentle horizon tilt
            m_CurrentPose.pos[0] = 0.0f;
            m_CurrentPose.pos[1] = 0.0f;
            m_CurrentPose.pos[2] = 0.0f;
        } else if (cycle < 1080) {
            // Phase 6 (15.0s - 18.0s): Stopped in open causeway, slow panoramic look across landscape
            float a = (float)(cycle - 900) / 180.0f;
            m_CurrentPose.pitch = 0.0f;
            m_CurrentPose.yaw = 0.30f * sinf(a * 3.14159265f); // Gentle, slow panorama scan
            m_CurrentPose.roll = 0.0f;
            m_CurrentPose.pos[0] = 0.0f;
            m_CurrentPose.pos[1] = 0.0f;
            m_CurrentPose.pos[2] = 0.0f;
        } else {
            // Phase 7 (18.0s - 20.0s): Stopped in open causeway, calmly scanning left and right
            float a = (float)(cycle - 1080) / 120.0f;
            m_CurrentPose.pitch = 0.0f;
            m_CurrentPose.yaw = 0.28f * sinf(a * 2.0f * 3.14159265f);
            m_CurrentPose.roll = 0.0f;
            m_CurrentPose.pos[0] = 0.0f;
            m_CurrentPose.pos[1] = 0.0f;
            m_CurrentPose.pos[2] = 0.0f;
        }

        EulerToQuaternion(m_CurrentPose.yaw, m_CurrentPose.pitch, m_CurrentPose.roll, m_CurrentPose.quat);
        UpdateSimHands();
        return;
    }

    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    double elapsed = (double)(now.QuadPart - m_StartTime.QuadPart) / (double)m_PerfFrequency.QuadPart;

    // Default gentle idling before gameplay starts
    m_CurrentPose.yaw = 0.25f * sinf((float)(elapsed * (2.0 * PI / 12.0)));
    m_CurrentPose.pitch = 0.10f * sinf((float)(elapsed * (2.0 * PI / 9.0)));
    m_CurrentPose.roll = 0.02f * sinf((float)(elapsed * (2.0 * PI / 10.0)));
    m_CurrentPose.pos[0] = 0.04f * sinf((float)(elapsed * (2.0 * PI / 7.0)));
    m_CurrentPose.pos[1] = 0.02f * sinf((float)(elapsed * (2.0 * PI / 4.5)));
    m_CurrentPose.pos[2] = 0.05f * cosf((float)(elapsed * (2.0 * PI / 6.0)));

    EulerToQuaternion(m_CurrentPose.yaw, m_CurrentPose.pitch, m_CurrentPose.roll, m_CurrentPose.quat);
    UpdateSimHands();
}

void XrHost::UpdateSimHands() {
    m_Controllers = {};
    m_Controllers.runtimeActive = true;
    m_Controllers.left.tracked = true;
    m_Controllers.right.tracked = true;
    m_Controllers.left.squeeze = 0.75f;
    m_Controllers.right.squeeze = 0.75f;
    m_Controllers.left.pose.pos[0] = -0.28f;
    m_Controllers.left.pose.pos[1] = -0.28f;
    m_Controllers.left.pose.pos[2] = -0.42f;
    m_Controllers.right.pose.pos[0] = 0.28f;
    m_Controllers.right.pose.pos[1] = -0.28f;
    m_Controllers.right.pose.pos[2] = -0.42f;
    m_Controllers.left.pose.quat[3] = 1.0f;
    m_Controllers.right.pose.quat[3] = 1.0f;

    const uint32_t cycle = m_GameplayTick % 1200;
    // Single gentle Clementine guitar strum while looking down-left at guitar
    if (cycle >= 430 && cycle < 460) {
        m_Controllers.right.y = true;
        m_Controllers.right.pose.pos[0] -= 0.04f;
    }
    // Single clean Broadaxe swing while looking down-right at axe
    else if (cycle >= 610 && cycle < 640) {
        m_Controllers.right.x = true;
        m_Controllers.right.pose.pos[1] -= 0.08f;
    }
}

bool XrHost::GetHeadPose(Pose6DoF& outPose) {
    if (!m_Initialized) return false;
    if (m_BlvrPoseInputBridgeMode && !m_BlvrPoseInputPoseValid) return false;
    outPose = m_CurrentPose;
    return true;
}

bool XrHost::GetEyeView(uint32_t eye, EyeView& outView) const {
    if (!m_Initialized || !m_BlvrPoseInputBridgeMode || !m_BlvrPoseInputViewsValid || eye >= 2u) {
        return false;
    }
    outView = m_BlvrPoseInputEyeViews[eye];
    return true;
}

bool XrHost::GetRenderTrackingStamp(uint64_t& frameId, int64_t& predictedDisplayTime) const {
    if (!m_Initialized || !m_BlvrPoseInputBridgeMode || !m_BlvrPoseInputPoseValid ||
        !m_BlvrPoseInputViewsValid || m_BlvrPoseInputLastFrame == 0 ||
        m_BlvrPoseInputPredictedDisplayTime == 0) {
        return false;
    }
    frameId = m_BlvrPoseInputLastFrame;
    predictedDisplayTime = static_cast<int64_t>(m_BlvrPoseInputPredictedDisplayTime);
    return true;
}

bool XrHost::GetControllerState(ControllerState& outState) const {
    if (!m_Initialized) return false;
    outState = m_Controllers;
    return outState.runtimeActive;
}

} // namespace BLVR
