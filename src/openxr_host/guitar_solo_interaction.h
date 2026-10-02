#pragma once
#include "attached_ui.h"
#include "../input/pose_controls.h"
#include <cmath>
#include <string>

namespace blvr_xr_host {
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
        const bool command=BLVR::CommandHeld(touch,bindings);
        const bool pressed=BLVR::ScopedActionDown(touch,BLVR::RockStance,command,bindings);
        const bool chord=BLVR::RecenterHeld(touch,bindings)||command||
            BLVR::ScopedActionDown(touch,BLVR::UiStart,command,bindings)||
            BLVR::ScopedActionDown(touch,BLVR::Journal,command,bindings);
        if(pressed&&!wasPressed_&&!chord) {
            open_=!open_;openedAt_=now;acceptUntil_=0;armed_=true;
            selectionX_=selectionY_=0;
        }
        wasPressed_=pressed;
        if(flags&blvr_ui_bridge::SoloNotes) { playing_=true;open_=true; }
        else if(playing_ && !(flags&blvr_ui_bridge::SoloRadial)) { playing_=false;open_=false; }
        if(BLVR::ScopedActionDown(touch,BLVR::Cancel,command,bindings)||chord) { open_=false;playing_=false; }
        if(open_&&!playing_&&now-openedAt_>1500&&!(flags&blvr_ui_bridge::SoloRadial)) open_=false;
        if(acceptUntil_&&now>=acceptUntil_) acceptUntil_=0;
        // Publish semantic UI state, preserving physical inputs for remapping.
        if(open_) {
            right.activeFlags|=BLVR::HostRadialMetadata;
            if(!playing_&&(BLVR::ScopedActionDown(touch,BLVR::RadialAccept,command,bindings)||
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
        return touch(frame,std::array<UiMounts,2>{mounts,mounts},panels,flags,now);
    }
    bool touch(blvr_xr_bridge::PoseBridge& frame,const std::array<UiMounts,2>& mounts,
        const std::vector<AttachedUiPanel>& panels,uint32_t flags,uint64_t now) {
        if(!open_||!(flags&blvr_ui_bridge::SoloRadial)||
            !(mounts[0].validMask&(1u<<GuitarHeadstock))||!(mounts[0].tipMask&2)||
            !(mounts[1].validMask&(1u<<GuitarHeadstock))||!(mounts[1].tipMask&2)) {
            hoverAt_=0;return false;
        }
        for(const auto& panel:panels) if(panel.mount==GuitarHeadstock) {
            if(!std::isfinite(panel.width)||!std::isfinite(panel.height)||panel.width<=0||panel.height<=0) {
                hoverAt_=0;return false;
            }
            XrVector3f local[2]{};float x[2]{},y[2]{},radius[2]{};
            for(unsigned eye=0;eye<2;++eye) {
                const auto& pose=mounts[eye].poses[GuitarHeadstock];const auto& q=pose.orientation;
                const float norm=q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w;
                if(!std::isfinite(norm)||std::fabs(norm-1.f)>.001f) {hoverAt_=0;return false;}
                local[eye]=uiLocalPoint(pose,mounts[eye].indexTips[1]);
                if(!std::isfinite(local[eye].x)||!std::isfinite(local[eye].y)||!std::isfinite(local[eye].z)) {hoverAt_=0;return false;}
                const float u=panel.uv.x+(local[eye].x/panel.width+.5f)*(panel.uv.z-panel.uv.x);
                const float v=panel.uv.y+(.5f-local[eye].y/panel.height)*(panel.uv.w-panel.uv.y);
                x[eye]=(u-.494f)/.236f;y[eye]=(.417f-v)/.278f;radius[eye]=std::hypot(x[eye],y[eye]);
            }
            // Both submitted eyes must show the same reachable contact. A
            // temporal pair with more than 2 cm of relative finger movement
            // cannot borrow a hit from only one of its different images.
            const float dx=local[0].x-local[1].x,dy=local[0].y-local[1].y,dz=local[0].z-local[1].z;
            if(dx*dx+dy*dy+dz*dz>.02f*.02f) {hoverAt_=0;return false;}
            if(local[0].z>.08f&&local[1].z>.08f) armed_=true;
            // Source-native 1280x720 wheel center/radii, normalized so capture
            // scaling and panel cropping cannot change which wedge is chosen.
            bool hover=true,contact=true;
            for(unsigned eye=0;eye<2;++eye) {
                hover&=radius[eye]>.55f&&radius[eye]<1.13f&&local[eye].z>-.045f&&local[eye].z<.16f;
                contact&=local[eye].z<=.025f&&local[eye].z>=-.035f;
            }
            if(!hover) {hoverAt_=0;return false;}
            if(!hoverAt_) hoverAt_=now;
            auto& right=frame.controllers[1];
            selectionX_=x[0]/radius[0];selectionY_=y[0]/radius[0];
            frame.controllers[0].reserved[1]=BLVR::PackRadialAxes(selectionX_,selectionY_);
            right.activeFlags|=BLVR::HostRadialAxesMetadata;
            if(armed_&&now-hoverAt_>=90&&contact) {
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
    // RTS pause is a complete horizontal native row. Preserve its source
    // extent/aspect and put all four corners inside BOTH asymmetric frustums,
    // including a modest palm translation/yaw. The former close left mount
    // clips the beginning of this row, more severely in the right eye.
    std::vector<uint8_t> pausePixels(1280*720*4);
    for(unsigned y=320;y<384;++y)for(unsigned x=96;x<1184;++x)
        pausePixels[(y*1280+x)*4+3]=255;
    const auto pausePanels=layoutAttachedUi(pausePixels.data(),1280,720,0);
    if(pausePanels.size()!=1||pausePanels[0].mount!=AbovePalm||
       pausePanels[0].uv.x>96.f/1280||pausePanels[0].uv.z<1184.f/1280||
       std::fabs(pausePanels[0].height/pausePanels[0].width-64.f/1088)>.0001f) {
        failure="native pause row lost content or source aspect";return false;
    }
    const auto insideBoth=[&](const AttachedUiPanel& panel,const XrPosef& mount) {
        const auto pose=attachedUiPanelPose(panel,mount);
        for(unsigned eye=0;eye<2;++eye) {
            const float yaw=(eye==0?-2.f:2.f)*.01745329252f;
            const XrPosef view{{0,std::sin(yaw*.5f),0,std::cos(yaw*.5f)},
                {eye==0?-.032f:.032f,1.7f,0}};
            const float left=std::tan((eye==0?-50.f:-42.f)*.01745329252f);
            const float right=std::tan((eye==0?42.f:50.f)*.01745329252f);
            for(float x:{-.5f,.5f})for(float y:{-.5f,.5f}) {
                const auto corner=rotateUiVector(pose.orientation,{x*panel.width,y*panel.height,0});
                const auto local=uiLocalPoint(view,{pose.position.x+corner.x,pose.position.y+corner.y,pose.position.z+corner.z});
                const float distance=-local.z;
                if(distance<.05f||local.x<left*distance||local.x>right*distance||
                   local.y<-distance||local.y>distance)return false;
            }
        }
        return true;
    };
    const XrPosef neutralPalm{{0,0,0,1},{-.28f,1.4f,-.32f}};
    auto originalPause=pausePanels[0];originalPause.localOffset={};
    if(insideBoth(originalPause,neutralPalm)||!insideBoth(pausePanels[0],neutralPalm)) {
        failure="native pause stand-off did not repair both-eye clipping";return false;
    }
    const float palmYaw=5.f*.01745329252f;
    const XrPosef movedPalm{{0,std::sin(palmYaw*.5f),0,std::cos(palmYaw*.5f)},
        {-.30f,1.42f,-.34f}};
    if(!insideBoth(pausePanels[0],movedPalm)) {
        failure="native pause row clipped under modest palm movement";return false;
    }
    const auto movedPanel=attachedUiPanelPose(pausePanels[0],movedPalm);
    const auto localOffset=uiLocalPoint(movedPalm,movedPanel.position);
    if(std::fabs(localOffset.x-.20f)>.00001f||std::fabs(localOffset.y)>.00001f||
       std::fabs(localOffset.z+.30f)>.00001f||
       movedPanel.orientation.y!=movedPalm.orientation.y||movedPanel.orientation.w!=movedPalm.orientation.w) {
        failure="native popup offset did not follow the rendered palm";return false;
    }
    panels=layoutAttachedUi(pixels.data(),1280,720,blvr_ui_bridge::SoloRadial);
    if(panels.size()!=1||panels[0].mount!=GuitarHeadstock) {failure="solo was not attached to guitar";return false;}
    const auto buildPanels=layoutAttachedUi(pixels.data(),1280,720,blvr_ui_bridge::BuildRadial|blvr_ui_bridge::SoloRadial);
    if(buildPanels.size()!=1||buildPanels[0].mount!=AbovePalm) {
        failure="native build wheel did not override the solo attachment";return false;
    }
    for(const auto& fixed:{buildPanels[0],panels[0]})
        if(fixed.localOffset.x||fixed.localOffset.y||fixed.localOffset.z) {
            failure="popup stand-off moved native build or solo geometry";return false;
        }
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
    GuitarSoloInteraction paired(BLVR::DefaultBindings);
    frame={};frame.controllers[1].buttons=ControllerThumbstickPressed;paired.prepare(frame,0,5000);
    frame.controllers[1].buttons=0;paired.prepare(frame,blvr_ui_bridge::SoloRadial,5010);
    tipAt(.12f);std::array<UiMounts,2> pair{mounts,mounts};
    if(paired.touch(frame,pair,panels,blvr_ui_bridge::SoloRadial,5100)) {
        failure="Both-eye hover selected without contact";return false;
    }
    tipAt(.01f);pair={mounts,mounts};pair[1].tipMask=0;
    if(paired.touch(frame,pair,panels,blvr_ui_bridge::SoloRadial,5210)) {
        failure="A single native pointer selected the both-eye guitar";return false;
    }
    pair={mounts,mounts};pair[1].poses[GuitarHeadstock].position.x+=.4f;
    if(paired.touch(frame,pair,panels,blvr_ui_bridge::SoloRadial,5220)) {
        failure="A different host contact plane borrowed the other eye's hit";return false;
    }
    pair={mounts,mounts};
    if(paired.touch(frame,pair,panels,blvr_ui_bridge::SoloRadial,5230)||
        !paired.touch(frame,pair,panels,blvr_ui_bridge::SoloRadial,5330)) {
        failure="Regained exact native contact did not require a fresh dwell";return false;
    }
    GuitarSoloInteraction depthPair(BLVR::DefaultBindings);
    frame={};frame.controllers[1].buttons=ControllerThumbstickPressed;depthPair.prepare(frame,0,6000);
    frame.controllers[1].buttons=0;depthPair.prepare(frame,blvr_ui_bridge::SoloRadial,6010);
    tipAt(.035f);pair={mounts,mounts};
    if(depthPair.touch(frame,pair,panels,blvr_ui_bridge::SoloRadial,6100)) {
        failure="Near-surface hover selected without contact";return false;
    }
    tipAt(.02f);pair[0]=mounts;
    if(depthPair.touch(frame,pair,panels,blvr_ui_bridge::SoloRadial,6210)) {
        failure="One eye outside the contact depth selected a wedge";return false;
    }
    pair[1]=mounts;
    if(!depthPair.touch(frame,pair,panels,blvr_ui_bridge::SoloRadial,6220)) {
        failure="Both-eye native contact failed after coherent depth recovery";return false;
    }
    // A command-bound pause/cancel input must not swallow the same button's
    // ordinary equipment/menu use before its required modifier is held.
    for(const auto action:{BLVR::UiStart,BLVR::Journal,BLVR::Cancel}) {
        auto scoped=BLVR::DefaultBindings;scoped.actions[action]={BLVR::TI::X,true};
        GuitarSoloInteraction scopedMenu(scoped);
        frame={};frame.controllers[1].buttons=ControllerThumbstickPressed;
        scopedMenu.prepare(frame,0,7000);
        frame.controllers[1].buttons=0;frame.controllers[0].buttons=ControllerPrimaryClick;
        scopedMenu.prepare(frame,blvr_ui_bridge::SoloRadial,7010);
        if(!(frame.controllers[1].activeFlags&BLVR::HostRadialMetadata)||
            BLVR::MapTouch(BLVR::TouchFromPose(frame),scoped).buttons[action]) {
            failure="Command-bound pause/cancel swallowed an ordinary solo button";return false;
        }
        frame.controllers[0].squeeze=1;frame.controllers[0].buttons|=ControllerThumbstickPressed;
        scopedMenu.prepare(frame,blvr_ui_bridge::SoloRadial,7020);
        if((frame.controllers[1].activeFlags&BLVR::HostRadialMetadata)||
            !BLVR::MapTouch(BLVR::TouchFromPose(frame),scoped).buttons[action]) {
            failure="Host and native pause/cancel disagreed on the required command chord";return false;
        }
    }
    auto scoped=BLVR::DefaultBindings;
    scoped.actions[BLVR::RockStance].command=true;
    GuitarSoloInteraction scopedOpen(scoped);
    frame={};frame.controllers[1].buttons=ControllerThumbstickPressed;
    scopedOpen.prepare(frame,0,7100);
    if((frame.controllers[1].activeFlags&BLVR::HostRadialMetadata)||
        BLVR::MapTouch(BLVR::TouchFromPose(frame),scoped).buttons[BLVR::RockStance]) {
        failure="Command-bound solo opening leaked without its modifier";return false;
    }
    scoped=BLVR::DefaultBindings;scoped.actions[BLVR::RadialAccept].command=true;
    scoped.soloAcceptAlternate=BLVR::TI::None;
    GuitarSoloInteraction scopedAccept(scoped);
    frame={};frame.controllers[1].buttons=ControllerThumbstickPressed;
    scopedAccept.prepare(frame,0,7200);
    frame.controllers[1].buttons=ControllerPrimaryClick;
    scopedAccept.prepare(frame,blvr_ui_bridge::SoloRadial,7210);
    if(!(frame.controllers[1].activeFlags&BLVR::HostRadialMetadata)||
        (frame.controllers[1].activeFlags&BLVR::HostAcceptMetadata)||
        BLVR::MapTouch(BLVR::TouchFromPose(frame),scoped).buttons[BLVR::RadialAccept]) {
        failure="Command-bound solo accept manufactured an ordinary confirmation";return false;
    }
    scoped=BLVR::DefaultBindings;
    for(const auto action:{BLVR::UiStart,BLVR::Journal,BLVR::Cancel})scoped.actions[action].input=BLVR::TI::None;
    GuitarSoloInteraction unboundPause(scoped);
    frame={};frame.controllers[1].buttons=ControllerThumbstickPressed;
    unboundPause.prepare(frame,0,7300);
    frame.controllers[1].buttons=ControllerSecondaryClick;
    frame.controllers[0].buttons=ControllerPrimaryClick|ControllerMenuClick;
    unboundPause.prepare(frame,blvr_ui_bridge::SoloRadial,7310);
    if(!(frame.controllers[1].activeFlags&BLVR::HostRadialMetadata)||
        BLVR::MapTouch(BLVR::TouchFromPose(frame),scoped).buttons[BLVR::Cancel]) {
        failure="An unbound pause/cancel action retained a hidden host button";return false;
    }
    return true;
}
}
