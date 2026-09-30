#pragma once
#include "../bridge/blvr_xr_pose_bridge.h"
#include "../input/pose_controls.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace blvr_xr_host {
// Grab-relative wheel plane in XR LOCAL space. Looking around after grabbing
// never changes its basis. Either a wheel arc or wrist rotation can steer.
class VehicleSteering {
    BLVR::ControlBindings bindings_;
    bool liveBindings_=false;
    bool held_=false;
    float center_[3]{},right_[3]{1,0,0},normal_[3]{0,0,1},start_[4]{0,0,0,1};
    float initialAngle_=0,value_=0;
    uint32_t space_=0,recenter_=0;
public:
    VehicleSteering():bindings_(BLVR::DefaultBindings),liveBindings_(true) {}
    explicit VehicleSteering(const BLVR::ControlBindings& bindings):bindings_(bindings) {}
    bool update(const blvr_xr_bridge::PoseBridge& frame,bool driving,float dt,float& output) {
        if(liveBindings_) {
            const auto next=BLVR::ActiveBindings();
            if(BLVR::ControlsSignature(next)!=BLVR::ControlsSignature(bindings_)){held_=false;value_=0;bindings_=next;}
        }
        using namespace blvr_xr_bridge;
        const auto& hand=frame.controllers[1];
        const bool valid=driving&&(frame.flags&PoseBridgeRightControllerValid)&&
            (frame.flags&PoseBridgeHmdValid)&&(hand.activeFlags&ControllerGripPose)&&
            BLVR::TouchValue(BLVR::TouchFromPose(frame),bindings_.wheelGrip)>.6f;
        bool finite=true;
        for(float x:hand.gripPose.position)finite&=std::isfinite(x);
        for(float x:hand.gripPose.orientation)finite&=std::isfinite(x);
        for(float x:frame.hmdPose.orientation)finite&=std::isfinite(x);
        if(!valid||!finite||space_!=frame.referenceSpaceGeneration||recenter_!=frame.recenterRequestId) {
            held_=false;value_=0;space_=frame.referenceSpaceGeneration;recenter_=frame.recenterRequestId;
            output=0;return false;
        }
        const unsigned moveHand=bindings_.movementStick==BLVR::TouchStick::Left?0u:1u;
        const bool stick=(frame.flags&(moveHand?PoseBridgeRightControllerValid:PoseBridgeLeftControllerValid))&&
            (frame.controllers[moveHand].activeFlags&ControllerThumbstick)&&std::fabs(frame.controllers[moveHand].thumbstickX)>=.18f;
        if(!held_||stick) {
            const auto* q=frame.hmdPose.orientation;
            const float x=1-2*(q[1]*q[1]+q[2]*q[2]),z=2*(q[0]*q[2]-q[1]*q[3]);
            const float length=std::hypot(x,z);
            if(length<.01f){held_=false;output=0;return false;}
            right_[0]=x/length;right_[2]=z/length;
            normal_[0]=-right_[2];normal_[2]=right_[0];
            for(int k=0;k<3;++k)center_[k]=hand.gripPose.position[k]-.18f*right_[k];
            center_[1]-=.15f;
            std::memcpy(start_,hand.gripPose.orientation,sizeof(start_));
            initialAngle_=std::atan2(.15f,.18f);held_=true;value_=0;
            output=0;return !stick;
        }
        const float* p=hand.gripPose.position;
        const float horizontal=(p[0]-center_[0])*right_[0]+(p[2]-center_[2])*right_[2];
        float arc=initialAngle_-std::atan2(p[1]-center_[1],horizontal);
        arc=std::atan2(std::sin(arc),std::cos(arc));
        const auto* q=hand.gripPose.orientation;
        // q * inverse(grab), projected onto the fixed wheel normal.
        const float dx=-q[3]*start_[0]+q[0]*start_[3]-q[1]*start_[2]+q[2]*start_[1];
        const float dz=-q[3]*start_[2]+q[2]*start_[3]-q[0]*start_[1]+q[1]*start_[0];
        const float dw=q[3]*start_[3]+q[0]*start_[0]+q[1]*start_[1]+q[2]*start_[2];
        float twist=-2*std::atan2(dx*normal_[0]+dz*normal_[2],dw);
        twist=std::atan2(std::sin(twist),std::cos(twist));
        const float angle=std::fabs(arc)>std::fabs(twist)?arc:twist;
        constexpr float dead=.05236f,full=1.22173f;
        const float target=std::copysign(std::clamp((std::fabs(angle)-dead)/(full-dead),0.f,1.f),angle);
        value_+=(target-value_)*(1-std::exp(-std::clamp(dt,0.f,.1f)/.035f));
        output=value_;return true;
    }
};

inline bool VehicleSteeringSelfTest(std::string& failure) {
    using namespace blvr_xr_bridge;
    PoseBridge f{};f.flags=PoseBridgeHmdValid|PoseBridgeRightControllerValid|PoseBridgeLeftControllerValid;
    f.hmdPose.orientation[3]=1;auto& hand=f.controllers[1];
    hand.gripPose.orientation[3]=1;hand.gripPose.position[0]=.18f;hand.gripPose.position[1]=1.15f;
    hand.activeFlags=ControllerGripPose|ControllerSqueeze;hand.squeeze=1;
    f.controllers[0].activeFlags=ControllerThumbstick;
    VehicleSteering wheel(BLVR::DefaultBindings);float x=0;
    const auto check=[&](bool ok,const char* why){if(!ok)failure=why;return ok;};
    if(!check(wheel.update(f,true,.1f,x)&&x==0,"grab must start neutral"))return false;
    f.hmdPose.orientation[1]=.7071068f;f.hmdPose.orientation[3]=.7071068f;
    if(!check(wheel.update(f,true,.1f,x)&&std::fabs(x)<.001f,"head turn steered wheel"))return false;
    hand.gripPose.position[1]=.85f;
    if(!check(wheel.update(f,true,.1f,x)&&x>.8f,"right-hand clockwise arc failed"))return false;
    f.controllers[0].thumbstickX=-1;
    if(!check(!wheel.update(f,true,.1f,x)&&x==0,"stick takeover failed"))return false;
    f.controllers[0].thumbstickX=0;
    if(!check(wheel.update(f,true,.1f,x)&&std::fabs(x)<.001f,"stick release jumped"))return false;
    f.flags&=~PoseBridgeRightControllerValid;
    if(!check(!wheel.update(f,true,.1f,x)&&x==0,"tracking loss kept steering"))return false;
    f.flags|=PoseBridgeRightControllerValid;wheel.update(f,true,.1f,x);
    if(!check(!wheel.update(f,false,.1f,x)&&x==0,"dismount kept steering"))return false;
    return true;
}
}
