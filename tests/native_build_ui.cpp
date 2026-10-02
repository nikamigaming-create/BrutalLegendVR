#include "../src/camera/native_build_ui.h"
#include <cstdio>

namespace {
int failures=0;
void Check(bool passed,const char* name) {
    if(!passed) {
        std::fprintf(stderr,"Native build UI validation failed: %s\n",name);
        ++failures;
    }
}
}

int main() {
    BLVR::NativeBuildUiOwnership live{0x1000,0x1000,0x1000,0x2000,0x2000,0x3000,7,7,100,1,1};
    Check(BLVR::NativeBuildUiOpen(live,true,0),"fresh owner");
    Check(BLVR::NativeBuildUiOpen(live,true,499),"owner before freshness limit");
    Check(!BLVR::NativeBuildUiOpen(live,true,500),"stale owner");
    Check(!BLVR::NativeBuildUiOpen(live,false,0),"unsupported executable");
    auto other=live;other.registeredActor=0x4000;
    Check(!BLVR::NativeBuildUiOpen(other,true,0),"foreign registered actor");
    other=live;other.playerOwner=0x4000;
    Check(!BLVR::NativeBuildUiOpen(other,true,0),"foreign player owner");
    other=live;other.controllerHandle=8;
    Check(!BLVR::NativeBuildUiOpen(other,true,0),"foreign controller handle");
    other=live;other.confirmedBuild=0x4000;
    Check(!BLVR::NativeBuildUiOpen(other,true,0),"replaced build owner");
    other=live;other.open=0;
    Check(!BLVR::NativeBuildUiOpen(other,true,0),"closed build menu");
    other=live;other.confirmedOpen=0;
    Check(!BLVR::NativeBuildUiOpen(other,true,0),"menu closed during read");
    other=live;other.open=2;
    Check(!BLVR::NativeBuildUiOpen(other,true,0),"invalid open byte");
    other=live;other.movie=0;
    Check(!BLVR::NativeBuildUiOpen(other,true,0),"missing movie");
    other=live;other.capacity=7;
    Check(!BLVR::NativeBuildUiOpen(other,true,0),"out-of-range actor handle");
    other=live;other.capacity=0x100001;
    Check(!BLVR::NativeBuildUiOpen(other,true,0),"invalid registry capacity");
    return failures?1:0;
}
