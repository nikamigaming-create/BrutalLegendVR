#include "../src/diagnostics/telemetry.h"
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

static void Check(bool ok,const char* why) {
    if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}
}
int main(int argc,char** argv) {
    const bool oversized=argc>1&&std::string(argv[1])=="oversized";
    const unsigned limit=oversized?4096u:32768u;
    const auto path=std::filesystem::temp_directory_path()/
        ("blvr-telemetry-budget-"+std::to_string(GetCurrentProcessId())+".jsonl");
    SetEnvironmentVariableA("BLVR_TELEMETRY","1");
    SetEnvironmentVariableA("BLVR_TELEMETRY_PATH",path.string().c_str());
    SetEnvironmentVariableA("BLVR_TELEMETRY_MAX_BYTES",std::to_string(limit).c_str());
    BLVR::Telemetry_Init();
    BLVR::Telemetry_Write("discrete","\"accepted_before_limit\":1");
    if(oversized) {
        const std::string fields="\"oversized\":\""+std::string(10000,'x')+"\"";
        BLVR::Telemetry_Write("large",fields.c_str());
    } else {
        std::vector<std::thread> writers;
        for(int i=0;i<4;++i)writers.emplace_back([] {
            for(int row=0;row<2000;++row)
                BLVR::Telemetry_Write("camera","\"pose_count\":1,\"root\":[1,2,3]");
        });
        for(auto& writer:writers)writer.join();
    }
    BLVR::Telemetry_Flush();
    const auto stopped=std::filesystem::file_size(path);
    for(int row=0;row<1000;++row)BLVR::Telemetry_Write("after","\"must_not_append\":1");
    BLVR::Telemetry_Flush();
    Check(std::filesystem::file_size(path)==stopped,"logging remains stopped after its budget");
    BLVR::Telemetry_Shutdown();
    Check(stopped<=limit,"whole file stays within the byte budget");
    std::ifstream file(path,std::ios::binary);
    const std::string data((std::istreambuf_iterator<char>(file)),{});
    Check(data.find("accepted_before_limit")!=std::string::npos,"discrete event retained before stop");
    const auto marker=data.find("\"kind\":\"telemetry_stop\"");
    Check(marker!=std::string::npos&&data.find("telemetry_stop",marker+10)==std::string::npos,
        "exactly one terminal budget record");
    Check(data.find("must_not_append")==std::string::npos,"events beyond cap are not written");
    if(oversized)Check(data.find("\"oversized\"")==std::string::npos,"oversized row rejected whole");
    Check(!data.empty()&&data.back()=='\n',"terminal complete JSON line");
    file.close();std::filesystem::remove(path);
    std::printf("PASS: %s telemetry cap (%llu/%u bytes), complete rows, no growth after stop\n",
        oversized?"oversized":"concurrent",static_cast<unsigned long long>(stopped),limit);
}
