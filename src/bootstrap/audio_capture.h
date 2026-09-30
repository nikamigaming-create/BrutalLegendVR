#pragma once

namespace BLVR {

bool AudioCapture_Start(const char* outputWavPath);
void AudioCapture_Stop();
bool AudioCapture_IsRunning();

} // namespace BLVR
