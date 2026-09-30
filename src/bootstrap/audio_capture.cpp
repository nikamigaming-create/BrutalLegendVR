#include "audio_capture.h"
#include "../diagnostics/log.h"
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <stdio.h>
#include <string>
#include <vector>
#include <cstdint>
#include <algorithm>

namespace BLVR {

#pragma pack(push, 1)
struct WavHeader {
    char riff[4];          // "RIFF"
    uint32_t fileSize;     // total file size - 8
    char wave[4];          // "WAVE"
    char fmt[4];           // "fmt "
    uint32_t fmtSize;      // 16 for PCM
    uint16_t audioFormat;  // 1 = PCM
    uint16_t numChannels;  // 2 for stereo
    uint32_t sampleRate;   // 44100 or 48000
    uint32_t byteRate;     // sampleRate * numChannels * 2
    uint16_t blockAlign;   // numChannels * 2
    uint16_t bitsPerSample;// 16
    char data[4];          // "data"
    uint32_t dataSize;     // raw audio byte count
};
#pragma pack(pop)

static HANDLE g_hAudioThread = NULL;
static HANDLE g_hStopEvent = NULL;
static bool g_AudioRunning = false;
static std::string g_WavPath;

static DWORD WINAPI AudioCaptureThreadProc(LPVOID lpParam) {
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        Log("AudioCapture: CoInitializeEx failed: 0x%08X", hr);
        return 1;
    }

    IMMDeviceEnumerator* pEnumerator = nullptr;
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
                            __uuidof(IMMDeviceEnumerator), (void**)&pEnumerator);
    if (FAILED(hr) || !pEnumerator) {
        Log("AudioCapture: Failed to create MMDeviceEnumerator: 0x%08X", hr);
        CoUninitialize();
        return 1;
    }

    IMMDevice* pDevice = nullptr;
    hr = pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
    pEnumerator->Release();
    if (FAILED(hr) || !pDevice) {
        Log("AudioCapture: Failed to get default audio render endpoint: 0x%08X", hr);
        CoUninitialize();
        return 1;
    }

    IAudioClient* pAudioClient = nullptr;
    hr = pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&pAudioClient);
    pDevice->Release();
    if (FAILED(hr) || !pAudioClient) {
        Log("AudioCapture: Failed to activate IAudioClient: 0x%08X", hr);
        CoUninitialize();
        return 1;
    }

    WAVEFORMATEX* pwfx = nullptr;
    hr = pAudioClient->GetMixFormat(&pwfx);
    if (FAILED(hr) || !pwfx) {
        Log("AudioCapture: Failed to get mix format: 0x%08X", hr);
        pAudioClient->Release();
        CoUninitialize();
        return 1;
    }

    Log("AudioCapture: MixFormat: Channels=%u, SampleRate=%u, Bits=%u, FormatTag=%u",
        pwfx->nChannels, pwfx->nSamplesPerSec, pwfx->wBitsPerSample, pwfx->wFormatTag);

    // Initialize in loopback mode
    const REFERENCE_TIME hnsBufferDuration = 2000000; // 200ms
    hr = pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                  AUDCLNT_STREAMFLAGS_LOOPBACK,
                                  hnsBufferDuration, 0, pwfx, NULL);
    if (FAILED(hr)) {
        Log("AudioCapture: IAudioClient::Initialize failed: 0x%08X", hr);
        CoTaskMemFree(pwfx);
        pAudioClient->Release();
        CoUninitialize();
        return 1;
    }

    IAudioCaptureClient* pCaptureClient = nullptr;
    hr = pAudioClient->GetService(__uuidof(IAudioCaptureClient), (void**)&pCaptureClient);
    if (FAILED(hr) || !pCaptureClient) {
        Log("AudioCapture: Failed to get IAudioCaptureClient: 0x%08X", hr);
        CoTaskMemFree(pwfx);
        pAudioClient->Release();
        CoUninitialize();
        return 1;
    }

    FILE* fp = fopen(g_WavPath.c_str(), "wb");
    if (!fp) {
        Log("AudioCapture: Failed to open output wav file: %s", g_WavPath.c_str());
        pCaptureClient->Release();
        CoTaskMemFree(pwfx);
        pAudioClient->Release();
        CoUninitialize();
        return 1;
    }

    // Write placeholder WAV header (target: 16-bit stereo PCM @ pwfx->nSamplesPerSec)
    uint32_t sampleRate = pwfx->nSamplesPerSec;
    uint16_t channels = (pwfx->nChannels > 2) ? 2 : pwfx->nChannels;
    WavHeader header;
    memcpy(header.riff, "RIFF", 4);
    header.fileSize = sizeof(WavHeader) - 8;
    memcpy(header.wave, "WAVE", 4);
    memcpy(header.fmt, "fmt ", 4);
    header.fmtSize = 16;
    header.audioFormat = 1; // PCM
    header.numChannels = channels;
    header.sampleRate = sampleRate;
    header.bitsPerSample = 16;
    header.blockAlign = channels * (16 / 8);
    header.byteRate = sampleRate * header.blockAlign;
    memcpy(header.data, "data", 4);
    header.dataSize = 0;
    fwrite(&header, 1, sizeof(header), fp);

    hr = pAudioClient->Start();
    if (FAILED(hr)) {
        Log("AudioCapture: Failed to start audio client: 0x%08X", hr);
        fclose(fp);
        pCaptureClient->Release();
        CoTaskMemFree(pwfx);
        pAudioClient->Release();
        CoUninitialize();
        return 1;
    }

    Log("AudioCapture: WASAPI Loopback stream started successfully -> %s", g_WavPath.c_str());

    uint32_t totalDataBytes = 0;
    bool isFloat = (pwfx->wBitsPerSample == 32);

    while (WaitForSingleObject(g_hStopEvent, 10) == WAIT_TIMEOUT) {
        UINT32 packetLength = 0;
        hr = pCaptureClient->GetNextPacketSize(&packetLength);
        if (FAILED(hr)) break;

        while (packetLength > 0) {
            BYTE* pData = nullptr;
            UINT32 numFramesAvailable = 0;
            DWORD flags = 0;

            hr = pCaptureClient->GetBuffer(&pData, &numFramesAvailable, &flags, NULL, NULL);
            if (FAILED(hr)) break;

            if (numFramesAvailable > 0) {
                // Convert each frame to 16-bit PCM stereo
                std::vector<int16_t> pcmBuffer(numFramesAvailable * channels, 0);

                if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                    // Buffer remains silence
                } else if (isFloat) {
                    const float* floatData = reinterpret_cast<const float*>(pData);
                    for (UINT32 i = 0; i < numFramesAvailable; ++i) {
                        for (uint16_t c = 0; c < channels; ++c) {
                            float val = floatData[i * pwfx->nChannels + c];
                            val = (val < -1.0f) ? -1.0f : ((val > 1.0f) ? 1.0f : val);
                            pcmBuffer[i * channels + c] = static_cast<int16_t>(val * 32767.0f);
                        }
                    }
                } else if (pwfx->wBitsPerSample == 16) {
                    const int16_t* in16 = reinterpret_cast<const int16_t*>(pData);
                    for (UINT32 i = 0; i < numFramesAvailable; ++i) {
                        for (uint16_t c = 0; c < channels; ++c) {
                            pcmBuffer[i * channels + c] = in16[i * pwfx->nChannels + c];
                        }
                    }
                }

                size_t bytesToWrite = pcmBuffer.size() * sizeof(int16_t);
                fwrite(pcmBuffer.data(), 1, bytesToWrite, fp);
                totalDataBytes += static_cast<uint32_t>(bytesToWrite);
            }

            pCaptureClient->ReleaseBuffer(numFramesAvailable);
            hr = pCaptureClient->GetNextPacketSize(&packetLength);
            if (FAILED(hr)) break;
        }
    }

    pAudioClient->Stop();

    // Finalize WAV header with actual data size
    header.dataSize = totalDataBytes;
    header.fileSize = sizeof(WavHeader) - 8 + totalDataBytes;
    fseek(fp, 0, SEEK_SET);
    fwrite(&header, 1, sizeof(header), fp);
    fclose(fp);

    Log("AudioCapture: Stopped. Finalized %u bytes (%.2f s) of audio in %s",
        totalDataBytes, (float)totalDataBytes / (float)header.byteRate, g_WavPath.c_str());

    pCaptureClient->Release();
    CoTaskMemFree(pwfx);
    pAudioClient->Release();
    CoUninitialize();
    return 0;
}

bool AudioCapture_Start(const char* outputWavPath) {
    if (g_AudioRunning) return true;

    g_WavPath = outputWavPath;
    g_hStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!g_hStopEvent) return false;

    g_AudioRunning = true;
    g_hAudioThread = CreateThread(NULL, 0, AudioCaptureThreadProc, NULL, 0, NULL);
    if (!g_hAudioThread) {
        g_AudioRunning = false;
        CloseHandle(g_hStopEvent);
        g_hStopEvent = NULL;
        return false;
    }

    return true;
}

void AudioCapture_Stop() {
    if (!g_AudioRunning) return;

    if (g_hStopEvent) {
        SetEvent(g_hStopEvent);
    }
    if (g_hAudioThread) {
        WaitForSingleObject(g_hAudioThread, 3000);
        CloseHandle(g_hAudioThread);
        g_hAudioThread = NULL;
    }
    if (g_hStopEvent) {
        CloseHandle(g_hStopEvent);
        g_hStopEvent = NULL;
    }
    g_AudioRunning = false;
}

bool AudioCapture_IsRunning() {
    return g_AudioRunning;
}

} // namespace BLVR
