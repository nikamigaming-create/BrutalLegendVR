#pragma once
#include <cstdint>

namespace BLVR {
bool CameraHook_IsBuildUiOpen();
bool CameraHook_IsBuildStageUpgradeSelected();

bool CameraHook_Init();
void CameraHook_Shutdown();
void CameraHook_OnPresent();
bool CameraHook_IsActive();
bool CameraHook_IsCinematic();
bool CameraHook_IsStereoRender();
bool CameraHook_GetViewMatrix(float outMatrix[16]);
bool CameraHook_GetWorldMatrix(float outMatrix[16]);
bool CameraHook_GetViewCorrectionMatrix(float outMatrix[16]);
void CameraHook_SetNativeBodyHead(float x, float y, float z);
void CameraHook_SetNativeBodyHeadLocal(float x, float y, float z);
void CameraHook_SetNativeBodyHeadFromSceneOffset(float x, float y, float z);
uint32_t CameraHook_GetUpdateCount();
uint32_t CameraHook_GetGameplayRenderCount();
uint64_t CameraHook_GetStereoSourceFrame();
void CameraHook_ResetProfiler();
unsigned CameraHook_GetSoloNextNote();

} // namespace BLVR
