#include <windows.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <endpointvolume.h>
#include <functiondiscoverykeys_devpkey.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
using Microsoft::WRL::ComPtr;

// Read-only endpoint/session diagnostics. Does not change system audio routing.
int wmain(int argc,wchar_t** argv) {
    const DWORD wanted=argc>1?wcstoul(argv[1],nullptr,10):0;
    if(FAILED(CoInitializeEx(nullptr,COINIT_MULTITHREADED))) return 2;
    int found=0;
    {
        ComPtr<IMMDeviceEnumerator> enumerator;
        ComPtr<IMMDeviceCollection> devices;
        if(FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&enumerator)))||
           FAILED(enumerator->EnumAudioEndpoints(eRender,DEVICE_STATE_ACTIVE,&devices))) return 3;
        UINT count=0;devices->GetCount(&count);
        for(UINT i=0;i<count;++i) {
            ComPtr<IMMDevice> device;ComPtr<IPropertyStore> properties;
            if(FAILED(devices->Item(i,&device)))continue;
            PROPVARIANT name;PropVariantInit(&name);
            if(SUCCEEDED(device->OpenPropertyStore(STGM_READ,&properties)))properties->GetValue(PKEY_Device_FriendlyName,&name);
            const wchar_t* label=name.vt==VT_LPWSTR?name.pwszVal:L"Unknown";
            ComPtr<IAudioSessionManager2> manager;ComPtr<IAudioSessionEnumerator> sessions;
            if(SUCCEEDED(device->Activate(__uuidof(IAudioSessionManager2),CLSCTX_ALL,nullptr,&manager))&&
               SUCCEEDED(manager->GetSessionEnumerator(&sessions))) {
                int n=0;sessions->GetCount(&n);
                for(int j=0;j<n;++j) {
                    ComPtr<IAudioSessionControl> control;ComPtr<IAudioSessionControl2> control2;
                    ComPtr<ISimpleAudioVolume> volume;ComPtr<IAudioMeterInformation> meter;
                    if(FAILED(sessions->GetSession(j,&control))||FAILED(control.As(&control2)))continue;
                    DWORD pid=0;control2->GetProcessId(&pid);if(wanted&&pid!=wanted)continue;
                    AudioSessionState state{};control->GetState(&state);
                    float level=-1,peak=0;BOOL muted=FALSE;
                    if(SUCCEEDED(control.As(&volume))) {volume->GetMasterVolume(&level);volume->GetMute(&muted);}
                    if(SUCCEEDED(control.As(&meter))) for(int sample=0;sample<25;++sample) {
                        float next=0;meter->GetPeakValue(&next);peak=(std::max)(peak,next);Sleep(20);
                    }
                    wprintf(L"pid=%lu endpoint=\"%ls\" state=%d volume=%.3f muted=%d peak=%.6f\n",pid,label,int(state),level,int(muted),peak);
                    ++found;
                }
            }
            PropVariantClear(&name);
        }
    }
    CoUninitialize();return found?0:1;
}
