#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audiopolicy.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstdint>
#include <vector>
#include <algorithm>
using Microsoft::WRL::ComPtr;
#pragma pack(push,1)
struct Wav {char riff[4]={'R','I','F','F'};uint32_t size=36;char wave[4]={'W','A','V','E'},fmt[4]={'f','m','t',' '};uint32_t fmtSize=16;uint16_t pcm=1,channels=2;uint32_t rate=48000,bytes=192000;uint16_t align=4,bits=16;char data[4]={'d','a','t','a'};uint32_t length=0;};
#pragma pack(pop)
// Read-only WASAPI loopback of the game's render endpoint; never captures a microphone.
int wmain(int argc,wchar_t** argv) {
    if(argc!=5)return 2;const DWORD pid=wcstoul(argv[1],nullptr,10);const double seconds=_wtof(argv[3]);
    if(FAILED(CoInitializeEx(nullptr,COINIT_MULTITHREADED)))return 3;
    ComPtr<IMMDeviceEnumerator> enumerator;ComPtr<IMMDeviceCollection> devices;ComPtr<IMMDevice> device;
    if(FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&enumerator)))||FAILED(enumerator->EnumAudioEndpoints(eRender,DEVICE_STATE_ACTIVE,&devices)))return 4;
    UINT count=0;devices->GetCount(&count);
    for(UINT i=0;i<count&&!device;++i) {
        ComPtr<IMMDevice> candidate;devices->Item(i,&candidate);ComPtr<IAudioSessionManager2> manager;ComPtr<IAudioSessionEnumerator> sessions;
        if(FAILED(candidate->Activate(__uuidof(IAudioSessionManager2),CLSCTX_ALL,nullptr,&manager))||FAILED(manager->GetSessionEnumerator(&sessions)))continue;
        int n=0;sessions->GetCount(&n);
        for(int j=0;j<n;++j) {ComPtr<IAudioSessionControl> control;ComPtr<IAudioSessionControl2> control2;DWORD owner=0;
            if(SUCCEEDED(sessions->GetSession(j,&control))&&SUCCEEDED(control.As(&control2))&&SUCCEEDED(control2->GetProcessId(&owner))&&owner==pid){device=candidate;break;}}
    }
    if(!device)return 5;
    ComPtr<IAudioClient> client;ComPtr<IAudioCaptureClient> capture;WAVEFORMATEX* format=nullptr;
    if(FAILED(device->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,&client))||FAILED(client->GetMixFormat(&format)))return 6;
    const bool floating=format->wFormatTag==WAVE_FORMAT_IEEE_FLOAT||(format->wFormatTag==WAVE_FORMAT_EXTENSIBLE&&reinterpret_cast<WAVEFORMATEXTENSIBLE*>(format)->SubFormat.Data1==WAVE_FORMAT_IEEE_FLOAT);
    if((!floating&&format->wBitsPerSample!=16)||(floating&&format->wBitsPerSample!=32))return 7;
    if(FAILED(client->Initialize(AUDCLNT_SHAREMODE_SHARED,AUDCLNT_STREAMFLAGS_LOOPBACK,2000000,0,format,nullptr))||FAILED(client->GetService(IID_PPV_ARGS(&capture))))return 8;
    FILE* file=nullptr;_wfopen_s(&file,argv[2],L"wb");if(!file)return 9;
    Wav header;header.channels=std::min<uint16_t>(2,format->nChannels);header.rate=format->nSamplesPerSec;header.align=header.channels*2;header.bytes=header.rate*header.align;
    fwrite(&header,1,sizeof(header),file);client->Start();
    LARGE_INTEGER frequency{},started{},now{};QueryPerformanceFrequency(&frequency);QueryPerformanceCounter(&started);
    uint64_t firstQpc=0;uint32_t discontinuities=0;
    while(true) {
        QueryPerformanceCounter(&now);if(double(now.QuadPart-started.QuadPart)/frequency.QuadPart>=seconds||GetFileAttributesW(argv[4])!=INVALID_FILE_ATTRIBUTES)break;
        UINT packet=0;if(FAILED(capture->GetNextPacketSize(&packet)))break;
        while(packet) {BYTE* data=nullptr;UINT frames=0;DWORD flags=0;UINT64 qpc=0;
            if(FAILED(capture->GetBuffer(&data,&frames,&flags,nullptr,&qpc)))break;
            if(!firstQpc)firstQpc=qpc;if(flags&AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY)++discontinuities;
            std::vector<int16_t> pcm(size_t(frames)*header.channels,0);
            if(!(flags&AUDCLNT_BUFFERFLAGS_SILENT))for(UINT f=0;f<frames;++f)for(UINT c=0;c<header.channels;++c){
                const size_t index=size_t(f)*format->nChannels+c;
                pcm[size_t(f)*header.channels+c]=floating?int16_t(std::clamp(reinterpret_cast<float*>(data)[index],-1.f,1.f)*32767):reinterpret_cast<int16_t*>(data)[index];}
            fwrite(pcm.data(),2,pcm.size(),file);header.length+=uint32_t(pcm.size()*2);
            capture->ReleaseBuffer(frames);if(FAILED(capture->GetNextPacketSize(&packet)))break;
        }
        Sleep(5);
    }
    client->Stop();header.size=header.length+36;fseek(file,0,SEEK_SET);fwrite(&header,1,sizeof(header),file);fclose(file);
    printf("{\"first_qpc_seconds\":%.9f,\"seconds\":%.6f,\"discontinuities\":%u,\"rate\":%u}\n",double(firstQpc)/1e7,double(header.length)/header.bytes,discontinuities,header.rate);
    CoTaskMemFree(format);return header.length?0:10;
}
