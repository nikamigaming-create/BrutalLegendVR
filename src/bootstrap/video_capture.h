#pragma once
#include <d3d9.h>
#include <cstdint>

namespace BLVR {

void VideoCapture_Init();
void VideoCapture_Start(int width, int height);
void VideoCapture_OnFrame(IDirect3DDevice9* pDevice);
void VideoCapture_Stop();
bool VideoCapture_IsStarted();
bool VideoCapture_IsFinished();
int VideoCapture_GetRecordedFrames();
void VideoCapture_OnDeviceReset();
void VideoCapture_CaptureNativeUi(IDirect3DDevice9* device, uint64_t sourceFrame, uint32_t flags);
void VideoCapture_PublishUiFrame(IDirect3DDevice9* device, uint64_t sourceFrameId);
void VideoCapture_PollDiagnosticSnapshot(IDirect3DDevice9* device);
bool VideoCapture_PresentMirror(IDirect3DDevice9* device);
bool VideoCapture_CaptureStereoEye(
    IDirect3DDevice9* device,
    uint32_t eye,
    uint64_t sourceFrameId,
    uint64_t poseSequence,
    int64_t renderedDisplayTime);
bool VideoCapture_PublishStereoPair(
    uint64_t sourceFrameId,
    uint64_t poseSequence,
    int64_t renderedDisplayTime);

} // namespace BLVR
