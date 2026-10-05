#pragma once
#include <windows.h>
#include <mmsystem.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <vector>

namespace blvr_xr_host {
// Original plucked-string synthesis. Each voice owns its buffers until the
// Windows audio device returns them; no retail audio is imported or shipped.
class GuitarAudio {
    struct Voice {
        HWAVEOUT device=nullptr;
        WAVEHDR header{};
        std::vector<int16_t> samples;
        ~Voice(){if(device){waveOutReset(device);waveOutUnprepareHeader(device,&header,sizeof(header));waveOutClose(device);}}
    };
    std::array<std::unique_ptr<Voice>,6> voices_;
    size_t next_=0;
public:
    void pluck(unsigned midi) {
        char text[24]{};GetEnvironmentVariableA("BLVR_GUITAR_VOLUME",text,sizeof(text));
        const float volume=text[0]?std::clamp(static_cast<float>(std::atoi(text))*.01f,0.f,1.f):.65f;
        if(volume==0||midi<36||midi>88)return;
        auto voice=std::make_unique<Voice>();
        constexpr unsigned rate=24000;
        WAVEFORMATEX format{};format.wFormatTag=WAVE_FORMAT_PCM;format.nChannels=1;
        format.nSamplesPerSec=rate;format.wBitsPerSample=16;format.nBlockAlign=2;format.nAvgBytesPerSec=rate*2;
        if(waveOutOpen(&voice->device,WAVE_MAPPER,&format,0,0,CALLBACK_NULL)!=MMSYSERR_NOERROR)return;
        const double frequency=440*std::pow(2.,(static_cast<double>(midi)-69)/12.);
        const size_t period=static_cast<size_t>(rate/frequency);
        std::vector<float> delay(period);
        uint32_t noise=0x1234567u+midi;
        for(float& value:delay){noise^=noise<<13;noise^=noise>>17;noise^=noise<<5;value=static_cast<float>(noise&65535u)/32767.5f-1.f;}
        voice->samples.resize(rate);
        float smooth=0;
        for(size_t i=0;i<voice->samples.size();++i){
            const size_t slot=i%period;
            const float sample=delay[slot];
            delay[slot]=.497f*(sample+delay[(slot+1)%period]);
            smooth=.55f*smooth+.45f*sample;
            const float attack=(std::min)(1.f,static_cast<float>(i)/48.f);
            const float release=(std::min)(1.f,static_cast<float>(voice->samples.size()-i)/800.f);
            voice->samples[i]=static_cast<int16_t>(std::clamp(smooth*volume*attack*release*22000.f,-32767.f,32767.f));
        }
        voice->header.lpData=reinterpret_cast<LPSTR>(voice->samples.data());
        voice->header.dwBufferLength=static_cast<DWORD>(voice->samples.size()*sizeof(int16_t));
        if(waveOutPrepareHeader(voice->device,&voice->header,sizeof(voice->header))!=MMSYSERR_NOERROR)return;
        if(waveOutWrite(voice->device,&voice->header,sizeof(voice->header))!=MMSYSERR_NOERROR)return;
        voices_[next_]=std::move(voice);next_=(next_+1)%voices_.size();
    }
};
}
