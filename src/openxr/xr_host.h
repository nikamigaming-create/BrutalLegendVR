#pragma once
#include <windows.h>
#include <cstdint>

namespace BLVR {

struct Pose6DoF {
    float pos[3];   // X (right), Y (up), Z (forward = -Z)
    float quat[4];  // X, Y, Z, W
    float yaw;      // radians
    float pitch;    // radians
    float roll;     // radians
};

struct EyeView {
    Pose6DoF pose{};
    float fov[4]{}; // angleLeft, angleRight, angleUp, angleDown (radians)
};

struct HandState {
    bool tracked = false;
    Pose6DoF pose{};
    bool aimTracked = false;
    Pose6DoF aimPose{};
    float trigger = 0.0f;
    float squeeze = 0.0f;
    float stickX = 0.0f;
    float stickY = 0.0f;
    bool a = false;
    bool b = false;
    bool x = false;
    bool y = false;
    bool menu = false;
    bool stickClick = false;
};

struct ControllerState {
    bool runtimeActive = false;
    HandState left{};
    HandState right{};
};

class XrHost {
public:
    static XrHost& Get();

    bool Init();
    void Shutdown();
    void Update();
    void LatchRenderTracking();

    bool GetHeadPose(Pose6DoF& outPose);
    bool GetEyeView(uint32_t eye, EyeView& outView) const;
    bool GetRenderTrackingStamp(uint64_t& frameId, int64_t& predictedDisplayTime) const;
    uint64_t GetRenderTrackingEpoch() const {return m_BlvrPoseInputProducerEpoch;}
    bool GetControllerState(ControllerState& outState) const;
    bool IsSimMode() const { return m_IsSimMode; }
    bool IsRealOpenXR() const { return m_HasRealXr; }
    bool HasRealXr() const { return m_HasRealXr || m_FnvxrBridgeMode || m_BlvrPoseInputBridgeMode; }
    uint32_t GetPoseUpdateCount() const { return m_PoseUpdateCount; }
    void SetGameplayTick(uint32_t tick) { m_GameplayTick = tick; }

private:
    XrHost();
    ~XrHost();

    bool InitOpenXR();
    void UpdateSimPose();
    void UpdateSimHands();
    void UpdateOpenXR();
    void ShutdownOpenXR();
    bool InitBlvrPoseInputBridge();
    void UpdateBlvrPoseInputBridge();
    void ShutdownBlvrPoseInputBridge();
    bool InitFnvxrBridge();
    void UpdateFnvxrBridge();
    void ShutdownFnvxrBridge();

    bool m_Initialized = false;
    bool m_IsSimMode = false;
    bool m_HasRealXr = false;
    uint32_t m_PoseUpdateCount = 0;
    uint32_t m_GameplayTick = 0;

    LARGE_INTEGER m_PerfFrequency{};
    LARGE_INTEGER m_StartTime{};
    
    Pose6DoF m_CurrentPose{};
    ControllerState m_Controllers{};
    void* m_OpenXrContext = nullptr;
    bool m_BlvrPoseInputBridgeMode = false;
    HANDLE m_BlvrPoseInputMapping = nullptr;
    void* m_BlvrPoseInputState = nullptr;
    uint64_t m_BlvrPoseInputLastFrame = 0;
    int32_t m_BlvrPoseInputLastSequence = 0;
    uint32_t m_BlvrPoseInputLastFlags = 0;
    uint64_t m_BlvrPoseInputLastHeartbeat = 0;
    Pose6DoF m_BlvrPoseInputHeadOrigin{};
    EyeView m_BlvrPoseInputEyeViews[2]{};
    int64_t m_BlvrPoseInputPredictedDisplayTime = 0;
    uint32_t m_BlvrPoseInputReferenceSpaceGeneration = 0;
    uint32_t m_BlvrPoseInputRecenterRequestId = 0;
    uint64_t m_BlvrPoseInputProducerEpoch = 0;
    bool m_BlvrPoseInputOriginFocused = false;
    bool m_BlvrPoseInputOriginValid = false;
    bool m_BlvrPoseInputLastReadValid = false;
    bool m_BlvrPoseInputPoseValid = false;
    bool m_BlvrPoseInputViewsValid = false;
    bool m_FnvxrBridgeMode = false;
    HANDLE m_FnvxrPoseMapping = nullptr;
    HANDLE m_FnvxrXInputMapping = nullptr;
    HANDLE m_FnvxrDInputMapping = nullptr;
    void* m_FnvxrPoseState = nullptr;
    void* m_FnvxrXInputState = nullptr;
    void* m_FnvxrDInputState = nullptr;
    uint64_t m_FnvxrLastFrame = 0;
    Pose6DoF m_FnvxrHeadOrigin{};
    uint32_t m_FnvxrOriginReferenceSpaceGeneration = 0;
    uint32_t m_FnvxrOriginRecenterRequestId = 0;
    bool m_FnvxrOriginValid = false;
};

// Math helpers
void QuaternionMultiply(const float q1[4], const float q2[4], float out[4]);
void QuaternionRotateVector(const float q[4], const float v[3], float out[3]);
void EulerToQuaternion(float yaw, float pitch, float roll, float out[4]);

} // namespace BLVR
