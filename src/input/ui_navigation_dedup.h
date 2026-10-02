#pragma once
#include "control_bindings.h"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <initializer_list>

namespace BLVR {
constexpr unsigned NativeUiInputBytes=0x350;
// Retail 0x67bbd0: first tick, delay crossing, then integral repeat boundaries.
inline bool NativeUiRepeatDue(float elapsed,float dt,float delay=.5f,float rate=4.f) {
    if(!std::isfinite(elapsed)||!std::isfinite(dt)||elapsed<0||dt<0||elapsed>3600||dt>1)return false;
    // The x87 routine retains elapsed-dt at extended precision for the delay
    // comparison, but explicitly rounds elapsed-delay to float before the
    // two integer conversions. Preserve that native rounding order.
    const double prior=static_cast<double>(elapsed)-dt;
    const float sinceDelay=elapsed-delay;
    return std::fabs(static_cast<float>(prior))<=.00001f || (elapsed>delay &&
        (prior<delay || static_cast<int>((static_cast<double>(sinceDelay)-dt)*rate)!=
                        static_cast<int>(static_cast<double>(sinceDelay)*rate)));
}
inline unsigned NativeUiDuplicateDirections(const uint8_t* packet,unsigned injected) {
    if(!packet||packet[0x65])return 0; // Native relative mouse axis mode.
    float axis[2]{},timer[2]{},dt=0;
    std::memcpy(axis,packet+0x6c,sizeof(axis));
    std::memcpy(timer,packet+0x9c,sizeof(timer));
    std::memcpy(&dt,packet+0x104,sizeof(dt));
    unsigned mask=0;
    for(unsigned axisId=0;axisId<2;++axisId) {
        if(!std::isfinite(axis[axisId])||axis[axisId]==0||!NativeUiRepeatDue(timer[axisId],dt))continue;
        const auto action=axisId==0?(axis[0]>0?UiRight:UiLeft):(axis[1]>0?UiUp:UiDown);
        if((injected&(1u<<action))&&packet[0x172+action])mask|=1u<<action;
    }
    return mask;
}
inline void SuppressNativeUiDuplicateDirections(uint8_t* copy,unsigned mask) {
    for(unsigned action : {unsigned(UiUp),unsigned(UiDown),unsigned(UiLeft),unsigned(UiRight)})
        if(mask&(1u<<action))copy[0x172+action]=0;
}
} // namespace BLVR
