#pragma once
#include "touch_controls.h"
#include "../bridge/blvr_xr_pose_bridge.h"
#include <cstring>

namespace BLVR {
inline TouchControls TouchFromPose(const blvr_xr_bridge::PoseBridge& frame) {
    using namespace blvr_xr_bridge;
    const auto& l=frame.controllers[0];const auto& r=frame.controllers[1];
    TouchControls t{};
    t.lx=l.thumbstickX;t.ly=l.thumbstickY;t.rx=r.thumbstickX;t.ry=r.thumbstickY;
    t.lt=l.trigger;t.rt=r.trigger;t.lg=l.squeeze;t.rg=r.squeeze;
    t.a=(r.buttons&ControllerPrimaryClick)!=0;t.b=(r.buttons&ControllerSecondaryClick)!=0;
    t.x=(l.buttons&ControllerPrimaryClick)!=0;t.y=(l.buttons&ControllerSecondaryClick)!=0;
    t.leftClick=(l.buttons&ControllerThumbstickPressed)!=0;t.rightClick=(r.buttons&ControllerThumbstickPressed)!=0;
    t.menu=((l.buttons|r.buttons)&ControllerMenuClick)!=0;
    t.hostRadial=(r.activeFlags&HostRadialMetadata)!=0;t.hostAccept=(r.activeFlags&HostAcceptMetadata)!=0;
    t.buildRadial=(r.activeFlags&BuildRadialMetadata)!=0;
    return t;
}
inline uint32_t PackRadialAxes(float x,float y) {
    const auto quantize=[](float v){return static_cast<int16_t>(std::clamp(v,-1.f,1.f)*32767.f);};
    return uint32_t(uint16_t(quantize(x)))|(uint32_t(uint16_t(quantize(y)))<<16);
}
inline void UnpackRadialAxes(uint32_t packed,float& x,float& y) {
    x=float(static_cast<int16_t>(packed&0xffff))/32767.f;y=float(static_cast<int16_t>(packed>>16))/32767.f;
}
}
