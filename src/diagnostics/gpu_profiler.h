#pragma once
#include "frame_profiler.h"
#include <wrl/client.h>
#if defined(BLVR_PERF_D3D9)
#include <d3d9.h>
#else
#include <d3d11.h>
#endif

namespace blvr_perf {
// Timestamp elapsed spans, not GPU busy counters. Poll without FLUSH and skip
// samples if the bounded query ring is full; profiling never waits for the GPU.
class GpuProfiler {
#if defined(BLVR_PERF_D3D9)
    using Device=IDirect3DDevice9;
    using Query=IDirect3DQuery9;
    using Context=IDirect3DDevice9;
#else
    using Device=ID3D11Device;
    using Query=ID3D11Query;
    using Context=ID3D11DeviceContext;
#endif
    struct Slot {
        Microsoft::WRL::ComPtr<Query> start,end,disjoint,frequency;
        uint64_t frame=0;
        unsigned stage=0;
        bool pending=false;
    };
    std::array<Slot,12> slots{};
    unsigned cursor=0;
    FILE* file=nullptr;
    unsigned rows=0;
    const char* role;
    void write(uint64_t frame,unsigned stage,double milliseconds) {
        if(!file) {
            char directory[MAX_PATH]{}; GetEnvironmentVariableA("BLVR_PERF_DIR",directory,sizeof(directory));
            char path[MAX_PATH+80]{};
            std::snprintf(path,sizeof(path),"%s%sblvr-%s-gpu-%lu.csv",directory,directory[0]?"\\":"",role,GetCurrentProcessId());
            file=_fsopen(path,"w",_SH_DENYNO);
            if(file) { std::setvbuf(file,nullptr,_IOFBF,32768); std::fprintf(file,"frame,stage,gpu_elapsed_ms\n"); }
        }
        if(file) {
            std::fprintf(file,"%llu,%u,%.4f\n",static_cast<unsigned long long>(frame),stage,milliseconds);
            if(++rows%60==0)std::fflush(file);
        }
    }
public:
    explicit GpuProfiler(const char* name):role(name) {}
    ~GpuProfiler() { if(file)std::fclose(file); }
    void reset() { for(auto& slot:slots)slot=Slot{}; cursor=0; }
    int begin(Device* device,Context* context,uint64_t frame,unsigned stage) {
        if(!recorder().enabled)return -1;
        for(auto& slot:slots)if(slot.pending) {
            UINT64 first=0,last=0,frequency=0;
            bool ready=false,valid=false;
#if defined(BLVR_PERF_D3D9)
            BOOL disjoint=TRUE;
            ready=slot.end->GetData(&last,sizeof(last),0)==S_OK
                &&slot.start->GetData(&first,sizeof(first),0)==S_OK
                &&slot.frequency->GetData(&frequency,sizeof(frequency),0)==S_OK
                &&slot.disjoint->GetData(&disjoint,sizeof(disjoint),0)==S_OK;
            valid=!disjoint&&frequency;
#else
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint{};
            ready=context->GetData(slot.end.Get(),&last,sizeof(last),D3D11_ASYNC_GETDATA_DONOTFLUSH)==S_OK
                &&context->GetData(slot.start.Get(),&first,sizeof(first),D3D11_ASYNC_GETDATA_DONOTFLUSH)==S_OK
                &&context->GetData(slot.disjoint.Get(),&disjoint,sizeof(disjoint),D3D11_ASYNC_GETDATA_DONOTFLUSH)==S_OK;
            frequency=disjoint.Frequency; valid=!disjoint.Disjoint&&frequency;
#endif
            if(ready) { if(valid&&last>=first)write(slot.frame,slot.stage,double(last-first)*1000.0/double(frequency)); slot.pending=false; }
        }
        const unsigned index=cursor++%static_cast<unsigned>(slots.size());
        auto& slot=slots[index];
        if(slot.pending)return -1;
        if(!slot.start) {
#if defined(BLVR_PERF_D3D9)
            if(FAILED(device->CreateQuery(D3DQUERYTYPE_TIMESTAMP,slot.start.GetAddressOf()))
                ||FAILED(device->CreateQuery(D3DQUERYTYPE_TIMESTAMP,slot.end.GetAddressOf()))
                ||FAILED(device->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT,slot.disjoint.GetAddressOf()))
                ||FAILED(device->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,slot.frequency.GetAddressOf()))) { slot=Slot{};return -1; }
#else
            D3D11_QUERY_DESC desc{D3D11_QUERY_TIMESTAMP,0};
            if(FAILED(device->CreateQuery(&desc,slot.start.GetAddressOf()))||FAILED(device->CreateQuery(&desc,slot.end.GetAddressOf()))) { slot=Slot{};return -1; }
            desc.Query=D3D11_QUERY_TIMESTAMP_DISJOINT;
            if(FAILED(device->CreateQuery(&desc,slot.disjoint.GetAddressOf()))) { slot=Slot{};return -1; }
#endif
        }
        slot.frame=frame;slot.stage=stage;
#if defined(BLVR_PERF_D3D9)
        (void)context;
        slot.disjoint->Issue(D3DISSUE_BEGIN);slot.start->Issue(D3DISSUE_END);
#else
        context->Begin(slot.disjoint.Get());context->End(slot.start.Get());
#endif
        return static_cast<int>(index);
    }
    void end(Context* context,int index) {
        if(index<0)return;
        auto& slot=slots[static_cast<unsigned>(index)];
#if defined(BLVR_PERF_D3D9)
        (void)context;
        slot.end->Issue(D3DISSUE_END);slot.frequency->Issue(D3DISSUE_END);slot.disjoint->Issue(D3DISSUE_END);
#else
        context->End(slot.end.Get());context->End(slot.disjoint.Get());
#endif
        slot.pending=true;
    }
};
}
