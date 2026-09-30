#pragma once
#include "attached_ui.h"
#include "../input/pose_controls.h"
#include <cmath>
#include <string>

namespace blvr_xr_host {
inline XrVector3f rotateUiVector(const XrQuaternionf& q,const XrVector3f& v) {
    const XrVector3f t{2*(q.y*v.z-q.z*v.y),2*(q.z*v.x-q.x*v.z),2*(q.x*v.y-q.y*v.x)};
    return {v.x+q.w*t.x+q.y*t.z-q.z*t.y,v.y+q.w*t.y+q.z*t.x-q.x*t.z,v.z+q.w*t.z+q.x*t.y-q.y*t.x};
}
inline XrVector3f uiLocalPoint(const XrPosef& pose,const XrVector3f& world) {
    return rotateUiVector({-pose.orientation.x,-pose.orientation.y,-pose.orientation.z,pose.orientation.w},
        {world.x-pose.position.x,world.y-pose.position.y,world.z-pose.position.z});
}

// A right-stick click opens a latched native RockStance, freeing that hand to
// point. Finger proximity steers the native wheel; contact sends RadialAccept.
// All writes are semantic XR input, never window/mouse input or game state.
class GuitarSoloInteraction {
public:
    GuitarSoloInteraction():bindings_(BLVR::DefaultBindings),liveBindings_(true) {}
    explicit GuitarSoloInteraction(const BLVR::ControlBindings& bindings):bindings_(bindings) {}
    void reset() { const bool live=liveBindings_; *this=GuitarSoloInteraction(bindings_); liveBindings_=live; }
    void prepare(blvr_xr_bridge::PoseBridge& frame,uint32_t flags,uint64_t now) {
        if(liveBindings_) {
            const auto next=BLVR::ActiveBindings();
            if(BLVR::ControlsSignature(next)!=BLVR::ControlsSignature(bindings_)){reset();bindings_=next;}
        }
        using namespace blvr_xr_bridge;
        auto& right=frame.controllers[1];
        right.activeFlags&=~(BLVR::HostRadialMetadata|BLVR::HostAcceptMetadata|BLVR::HostRadialAxesMetadata);
        const auto& bindings=bindings_;const auto touch=BLVR::TouchFromPose(frame);
        const bool pressed=BLVR::TouchValue(touch,bindings.actions[BLVR::RockStance].input)>.5f;
        const bool chord=BLVR::RecenterHeld(touch,bindings)||BLVR::CommandHeld(touch,bindings)||
            BLVR::TouchValue(touch,bindings.actions[BLVR::UiStart].input)>.5f||
            BLVR::TouchValue(touch,bindings.actions[BLVR::Journal].input)>.5f;
        if(pressed&&!wasPressed_&&!chord) {
            open_=!open_;openedAt_=now;acceptUntil_=0;armed_=true;
            selectionX_=selectionY_=0;
        }
        wasPressed_=pressed;
        if(flags&blvr_ui_bridge::SoloNotes) { playing_=true;open_=true; }
        else if(playing_ && !(flags&blvr_ui_bridge::SoloRadial)) { playing_=false;open_=false; }
        if(BLVR::TouchValue(touch,bindings.actions[BLVR::Cancel].input)>.5f||chord) { open_=false;playing_=false; }
        if(open_&&!playing_&&now-openedAt_>1500&&!(flags&blvr_ui_bridge::SoloRadial)) open_=false;
        if(acceptUntil_&&now>=acceptUntil_) acceptUntil_=0;
        // Publish semantic UI state, preserving physical inputs for remapping.
        if(open_) {
            right.activeFlags|=BLVR::HostRadialMetadata;
            if(!playing_&&(BLVR::TouchValue(touch,bindings.actions[BLVR::RadialAccept].input)>.5f||
                BLVR::TouchValue(touch,bindings.soloAcceptAlternate)>.65f)) {
                if(!acceptUntil_) acceptUntil_=now+220;
            }
            if(!playing_) {
                float x=0,y=0;BLVR::StickValues(touch,bindings.radialStick,x,y);
                if(std::hypot(x,y)>.35f) {
                    selectionX_=x;selectionY_=y;
                }
                // Retail accepts on RadialAccept RELEASE (input+0x158).
                // Keep the selected wedge after the finger leaves or the
                // stick centers, including the frame of that release edge.
                frame.controllers[0].reserved[1]=BLVR::PackRadialAxes(selectionX_,selectionY_);
                right.activeFlags|=BLVR::HostRadialAxesMetadata;
            }
        }
        if(acceptUntil_) right.activeFlags|=BLVR::HostAcceptMetadata;
    }
    bool touch(blvr_xr_bridge::PoseBridge& frame,const UiMounts& mounts,
        const std::vector<AttachedUiPanel>& panels,uint32_t flags,uint64_t now) {
        if(!open_||!(flags&blvr_ui_bridge::SoloRadial)||
            !(mounts.validMask&(1u<<GuitarHeadstock))||!(mounts.tipMask&2)) {
            hoverAt_=0;return false;
        }
        for(const auto& panel:panels) if(panel.mount==GuitarHeadstock) {
            const auto local=uiLocalPoint(mounts.poses[GuitarHeadstock],mounts.indexTips[1]);
            if(local.z>.08f) armed_=true;
            const float u=panel.uv.x+(local.x/panel.width+.5f)*(panel.uv.z-panel.uv.x);
            const float v=panel.uv.y+(.5f-local.y/panel.height)*(panel.uv.w-panel.uv.y);
            // Source-native 1280x720 wheel center/radii, normalized so capture
            // scaling and panel cropping cannot change which wedge is chosen.
            const float x=(u-.494f)/.236f,y=(.417f-v)/.278f;
            const float radius=std::hypot(x,y);
            const bool hover=radius>.55f&&radius<1.13f&&local.z>-.045f&&local.z<.16f;
            if(!hover) {hoverAt_=0;return false;}
            if(!hoverAt_) hoverAt_=now;
            auto& right=frame.controllers[1];
            selectionX_=x/radius;selectionY_=y/radius;
            frame.controllers[0].reserved[1]=BLVR::PackRadialAxes(selectionX_,selectionY_);
            right.activeFlags|=BLVR::HostRadialAxesMetadata;
            if(armed_&&now-hoverAt_>=90&&local.z<=.025f&&local.z>=-.035f) {
                armed_=false;acceptUntil_=now+220;
                right.activeFlags|=BLVR::HostAcceptMetadata;
                return true;
            }
            return false;
        }
        return false;
    }
private:
    BLVR::ControlBindings bindings_;
    bool liveBindings_=false;
    bool open_=false,wasPressed_=false,armed_=true,playing_=false;
    uint64_t openedAt_=0,hoverAt_=0,acceptUntil_=0;
    float selectionX_=0,selectionY_=0;
};
inline bool AttachedUiInteractionSelfTest(std::string& failure) {
    using namespace blvr_xr_bridge;
    // Unrelated, simultaneous native stats must not be lost when separating
    // the two arms. Dense central cards instead remain one readable surface.
    std::vector<uint8_t> pixels(1280*720*4);
    pixels[(100*1280+70)*4+3]=255;pixels[(100*1280+1200)*4+3]=255;
    auto panels=layoutAttachedUi(pixels.data(),1280,720,0);
    if(panels.size()!=2||panels[0].mount!=LeftForearm||panels[1].mount!=RightForearm) {
        failure="native stat regions did not reach both forearms";return false;
    }
    for(unsigned y=220;y<500;++y) for(unsigned x=350;x<950;++x) pixels[(y*1280+x)*4+3]=255;
    panels=layoutAttachedUi(pixels.data(),1280,720,0);
    if(panels.size()!=1||panels[0].mount!=AbovePalm) {failure="popup was not attached above palm";return false;}
    panels=layoutAttachedUi(pixels.data(),1280,720,blvr_ui_bridge::SoloRadial);
    if(panels.size()!=1||panels[0].mount!=GuitarHeadstock) {failure="solo was not attached to guitar";return false;}
    auto vignette=pixels;
    for(size_t i=3;i<vignette.size();i+=4)vignette[i]=(std::max)(vignette[i],uint8_t(103));
    const auto notes=layoutAttachedUi(vignette.data(),1280,720,blvr_ui_bridge::SoloNotes);
    if(notes.size()!=1||notes[0].mount!=GuitarHeadstock||notes[0].uv.x<.2f||notes[0].uv.w>.4f||notes[0].width<.7f) {
        failure="native solo vignette shrank the headstock note track";return false;
    }
    GuitarSoloInteraction interaction(BLVR::DefaultBindings);
    PoseBridge frame{};frame.controllers[1].buttons=ControllerThumbstickPressed;
    interaction.prepare(frame,0,1000);
    frame.controllers[1].buttons=0;interaction.prepare(frame,blvr_ui_bridge::SoloRadial,1100);
    if(!(frame.controllers[1].activeFlags&BLVR::HostRadialMetadata)) {failure="menu did not stay open after stick release";return false;}
    UiMounts mounts{};mounts.validMask=1u<<GuitarHeadstock;mounts.tipMask=2;
    // Translated and rotated mount: test selection in the surface frame,
    // including crop, rather than accidentally testing world XY coordinates.
    mounts.poses[GuitarHeadstock]={{0,.70710678f,0,.70710678f},{.4f,1.1f,-.5f}};
    const auto& panel=panels[0];
    const float u=.494f-.236f*.45f,v=.417f-.278f*.77942286f;
    const XrVector3f local{((u-panel.uv.x)/(panel.uv.z-panel.uv.x)-.5f)*panel.width,
        (.5f-(v-panel.uv.y)/(panel.uv.w-panel.uv.y))*panel.height,.08f};
    const auto tipAt=[&](float z) {
        auto p=rotateUiVector(mounts.poses[GuitarHeadstock].orientation,{local.x,local.y,z});
        const auto& origin=mounts.poses[GuitarHeadstock].position;
        mounts.indexTips[1]={p.x+origin.x,p.y+origin.y,p.z+origin.z};
    };
    tipAt(.08f);
    if(interaction.touch(frame,mounts,panels,1,1200)) {failure="hover selected without contact";return false;}
    tipAt(.01f);
    const bool selected=interaction.touch(frame,mounts,panels,1,1310);
    float selectedX=0,selectedY=0;BLVR::UnpackRadialAxes(frame.controllers[0].reserved[1],selectedX,selectedY);
    if(!selected||selectedX>-.45f||selectedY<.8f||frame.controllers[1].buttons) {
        failure="finger contact did not select native upper-left wedge without changing physical buttons";return false;}
    if(interaction.touch(frame,mounts,panels,1,1320)) {failure="held contact repeated selection";return false;}
    frame.controllers[1].buttons=0;
    frame.controllers[1].thumbstickX=frame.controllers[1].thumbstickY=0;
    interaction.prepare(frame,blvr_ui_bridge::SoloRadial,1540);
    BLVR::UnpackRadialAxes(frame.controllers[0].reserved[1],selectedX,selectedY);
    if((frame.controllers[1].activeFlags&BLVR::HostAcceptMetadata)||selectedX>-.45f||
        selectedY<.8f) {failure="accept release lost selected native wedge";return false;}
    interaction.prepare(frame,blvr_ui_bridge::SoloNotes,4000);
    if(!(frame.controllers[1].activeFlags&BLVR::HostRadialMetadata)) {failure="timed notes lost native rock stance";return false;}
    return true;
}
}
