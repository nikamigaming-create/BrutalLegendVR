#pragma once
#include <windows.h>
#include <array>
#include <cstdio>
#include <cstdint>
#include <share.h>
#include <atomic>

// Buffered wall-clock measurements. Nested columns are inclusive and must not
// be added together. No GPU waits, per-draw instrumentation or forced flushes.
namespace blvr_perf {
enum Stage { Stereo, Rig, LeftRender, RightRender, LeftCapture, RightCapture,
    Readback, Convert, PairPublish, NativeUi, Mirror, Pacing, PresentDriver,
    PresentHook, XrWait, XrInput, Bridge, Upload, HostRender, XrEnd, TerrainBuild, TerrainPrune, Count };
inline const char* Names[] = {"stereo_ms","rig_ms","left_render_ms","right_render_ms",
    "left_capture_ms","right_capture_ms","readback_ms","convert_ms","pair_publish_ms",
    "native_ui_ms","mirror_ms","pacing_ms","present_driver_ms","present_hook_ms",
    "xr_wait_ms","xr_input_ms","bridge_ms","upload_ms","host_render_ms","xr_end_ms","terrain_build_ms","terrain_prune_ms"};
inline int64_t now() { LARGE_INTEGER value{}; QueryPerformanceCounter(&value); return value.QuadPart; }
struct Recorder {
    bool enabled=false;
    double ticksPerMs=1;
    std::array<std::atomic<int64_t>,Count> values{};
    FILE* file=nullptr;
    int64_t previous=0;
    uint64_t previousThread=0;
    DWORD previousThreadId=0;
    unsigned rows=0;
    Recorder() {
        char option[8]{}; GetEnvironmentVariableA("BLVR_PERF",option,sizeof(option));
        enabled=option[0]=='1'||_stricmp(option,"true")==0;
        LARGE_INTEGER frequency{}; QueryPerformanceFrequency(&frequency);
        ticksPerMs=double(frequency.QuadPart)/1000.0;
    }
    ~Recorder() { if(file) std::fclose(file); }
    void finish(const char* role,uint64_t frame,uint64_t transaction,unsigned mode,unsigned draws=0) {
        if(!enabled) return;
        const int64_t end=now();
        const DWORD threadId=GetCurrentThreadId();
        FILETIME created{},exited{},kernel{},user{};
        GetThreadTimes(GetCurrentThread(),&created,&exited,&kernel,&user);
        const uint64_t thread=(uint64_t(kernel.dwHighDateTime)<<32|kernel.dwLowDateTime)
            +(uint64_t(user.dwHighDateTime)<<32|user.dwLowDateTime);
        if(!file) {
            char directory[MAX_PATH]{}; GetEnvironmentVariableA("BLVR_PERF_DIR",directory,sizeof(directory));
            char path[MAX_PATH+80]{};
            std::snprintf(path,sizeof(path),"%s%sblvr-%s-perf-%lu.csv",directory,
                directory[0]?"\\":"",role,GetCurrentProcessId());
            file=_fsopen(path,"w",_SH_DENYNO);
            if(file) {
                std::setvbuf(file,nullptr,_IOFBF,65536);
                std::fprintf(file,"qpc_s,frame,transaction,mode,draws,frame_ms,thread_cpu_ms");
                for(const char* name:Names) std::fprintf(file,",%s",name);
                std::fputc('\n',file);
            }
        }
        // Explicit profiling still has a fixed disk budget per process.
        if(file&&_ftelli64(file)>=32ll*1024*1024-4096) {
            std::fflush(file);enabled=false;return;
        }
        if(file&&previous) {
            std::fprintf(file,"%.6f,%llu,%llu,%u,%u,%.4f,%.4f",double(end)/ticksPerMs/1000.0,
                static_cast<unsigned long long>(frame),static_cast<unsigned long long>(transaction),mode,draws,
                double(end-previous)/ticksPerMs,
                previousThreadId==threadId&&thread>=previousThread?double(thread-previousThread)/10000.0:0.0);
            for(auto& value:values) std::fprintf(file,",%.4f",double(value.exchange(0,std::memory_order_relaxed))/ticksPerMs);
            std::fputc('\n',file);
            if(++rows%60==0) std::fflush(file);
        }
        if(!file||!previous)for(auto& value:values)value.store(0,std::memory_order_relaxed);
        previous=end; previousThread=thread; previousThreadId=threadId;
    }
};
inline Recorder& recorder() { static Recorder value; return value; }
struct Scope {
    Stage stage; int64_t started;
    explicit Scope(Stage value):stage(value),started(recorder().enabled?now():0) {}
    void stop() { if(started) { recorder().values[stage].fetch_add(now()-started,std::memory_order_relaxed); started=0; } }
    ~Scope() { stop(); }
};
}
