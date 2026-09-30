#pragma once
#include <windows.h>
#include <cstdlib>
#include <algorithm>
namespace blvr_quality {
inline unsigned dimension(const char* name,unsigned fallback) {
    char text[24]{}; GetEnvironmentVariableA(name,text,sizeof(text));
    if(!text[0])return fallback;
    char* end=nullptr;const unsigned long value=std::strtoul(text,&end,10);
    return end!=text&&!*end?static_cast<unsigned>((std::clamp)(value,720ul,2048ul)):fallback;
}
inline unsigned width() { static const unsigned value=dimension("BLVR_RENDER_WIDTH",1536);return value; }
inline unsigned height() { static const unsigned value=dimension("BLVR_RENDER_HEIGHT",1536);return value; }
inline unsigned projectionLimit() { static const unsigned value=dimension("BLVR_PROJECTION_LIMIT",2048);return value; }
inline bool edgeAa() { static const bool enabled=[] { char text[8]{};GetEnvironmentVariableA("BLVR_EDGE_AA",text,sizeof(text));return text[0]!='0'; }();return enabled; }
}
