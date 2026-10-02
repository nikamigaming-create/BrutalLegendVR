#pragma once
#include "eddie_lobby.h"
#include "game_bridge.h"
#include <array>
#include <cmath>
#include <string>

namespace blvr_xr_host {
inline bool RequiresRenderedUiGeometry(const GameFrameView& frame) {
    return frame.firstPersonCamera ||
        (frame.presentationMode==blvr_xr_bridge::PresentationMode::WorldStereo &&
         frame.requiresExactCapturePose());
}
// Host-rig mounts describe the tracked solve, not the native animation rendered
// in a first-person game image. Native geometry must join for BOTH eyes before
// a gameplay panel or fingertip is composed. The opening room owns its rig.
inline std::array<UiMounts,2> SelectRenderedUiPair(
    const std::array<UiMounts,2>& host,
    const std::array<UiMounts,2>& rendered,
    const std::array<bool,2>& exact,
    bool requiresRenderedGeometry) {
    if(requiresRenderedGeometry&&(!exact[0]||!exact[1]))return {};
    std::array<UiMounts,2> result{};
    for(unsigned eye=0;eye<2;++eye)result[eye]=exact[eye]?rendered[eye]:host[eye];
    return result;
}

// Contact must use the geometry of the visible game image. Retaining its
// exact pair also lets a fresh input frame reject old pixels after a stall,
// tracking loss, game restart or OpenXR reference-space change.
class RenderedUiInteractionPair {
public:
    void clear() { *this=RenderedUiInteractionPair{}; }
    void capture(const GameFrameView& game,uint64_t hostEpoch,uint32_t referenceSpace,
        const std::array<UiMounts,2>& rendered,const std::array<bool,2>& exact) {
        clear();
        if(game.presentationMode!=blvr_xr_bridge::PresentationMode::WorldStereo||
            !game.requiresExactCapturePose()||!exact[0]||!exact[1]||!hostEpoch||
            !referenceSpace||!game.transactionId||!game.producerPid||!game.producerEpoch)return;
        for(unsigned eye=0;eye<2;++eye)if(!game.sourceFrameId[eye]||!game.poseSequence[eye]||
            game.renderedDisplayTime[eye]<=0)return;
        game_=game;hostEpoch_=hostEpoch;referenceSpace_=referenceSpace;mounts_=rendered;valid_=true;
    }
    bool read(const GameFrameView& game,uint64_t hostEpoch,const blvr_xr_bridge::PoseBridge& input,
        std::array<UiMounts,2>& output) const {
        output={};
        using namespace blvr_xr_bridge;
        constexpr int64_t MaxAgeNanoseconds=150000000;
        constexpr uint32_t RequiredPose=PoseBridgeViewsValid|PoseBridgeHmdValid|PoseBridgeRightControllerValid;
        if(!valid_||hostEpoch!=hostEpoch_||input.referenceSpaceGeneration!=referenceSpace_||
            game.presentationMode!=PresentationMode::WorldStereo||game.producerPid!=game_.producerPid||
            game.producerEpoch!=game_.producerEpoch||game.transactionId!=game_.transactionId||
            (input.flags&RequiredPose)!=RequiredPose||!(input.controllers[1].activeFlags&ControllerGripPose)||
            input.predictedDisplayTime<=0)return false;
        for(unsigned eye=0;eye<2;++eye) {
            if(game.sourceFrameId[eye]!=game_.sourceFrameId[eye]||game.poseSequence[eye]!=game_.poseSequence[eye]||
                game.renderedDisplayTime[eye]!=game_.renderedDisplayTime[eye]||
                input.frameId<game_.poseSequence[eye]||input.predictedDisplayTime<game_.renderedDisplayTime[eye]||
                input.predictedDisplayTime-game_.renderedDisplayTime[eye]>MaxAgeNanoseconds||
                !(mounts_[eye].validMask&(1u<<GuitarHeadstock))||!(mounts_[eye].tipMask&2))return false;
            const auto& pose=mounts_[eye].poses[GuitarHeadstock];
            const auto& q=pose.orientation;
            const float norm=q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w;
            if(!std::isfinite(norm)||std::fabs(norm-1.f)>.001f)return false;
            for(float value:{pose.position.x,pose.position.y,pose.position.z,
                mounts_[eye].indexTips[1].x,mounts_[eye].indexTips[1].y,mounts_[eye].indexTips[1].z})
                if(!std::isfinite(value)||std::fabs(value)>100)return false;
        }
        output=mounts_;return true;
    }
private:
    GameFrameView game_{};
    std::array<UiMounts,2> mounts_{};
    uint64_t hostEpoch_=0;
    uint32_t referenceSpace_=0;
    bool valid_=false;
};

inline bool RenderedUiJoinSelfTest(std::string& failure) {
    GameFrameView world{};
    world.presentationMode=blvr_xr_bridge::PresentationMode::WorldStereo;
    world.sameSimulationTick=true;world.poseSequence[0]=world.poseSequence[1]=1;
    if(!RequiresRenderedUiGeometry(world)) {
        failure="DrawScene world geometry join depended on parent-only camera proof";return false;
    }
    GameFrameView openingFrame{};
    openingFrame.presentationMode=blvr_xr_bridge::PresentationMode::UiQuad;
    if(RequiresRenderedUiGeometry(openingFrame)) {
        failure="A host-owned opening screen required native world geometry";return false;
    }
    std::array<UiMounts,2> host{},rendered{};
    for(unsigned eye=0;eye<2;++eye) {
        host[eye].validMask=15;host[eye].tipMask=3;
        host[eye].poses[RightForearm].position.x=.1f+static_cast<float>(eye);
        rendered[eye]=host[eye];
        rendered[eye].poses[RightForearm].position.x=.8f+static_cast<float>(eye);
        rendered[eye].tipMask=1;
    }
    const auto joined=SelectRenderedUiPair(host,rendered,{true,true},true);
    for(unsigned eye=0;eye<2;++eye)if(joined[eye].poses[RightForearm].position.x!=.8f+static_cast<float>(eye)||joined[eye].tipMask!=1) {
        failure="Exact rendered geometry/pointer validity did not replace the tracked solve";return false;
    }
    const std::array<std::array<bool,2>,3> missingPairs{{{false,false},{true,false},{false,true}}};
    for(const auto& exact:missingPairs) {
        const auto missing=SelectRenderedUiPair(host,rendered,exact,true);
        for(const auto& eye:missing)if(eye.validMask||eye.tipMask) {
            failure="Missing native geometry retained a tracked or single-eye overlay";return false;
        }
    }
    const auto opening=SelectRenderedUiPair(host,rendered,{false,false},false);
    for(unsigned eye=0;eye<2;++eye)if(opening[eye].validMask!=15||opening[eye].tipMask!=3||
        opening[eye].poses[RightForearm].position.x!=.1f+static_cast<float>(eye)) {
        failure="The host-owned room lost its tracked UI mounts";return false;
    }
    GameFrameView contact{};
    contact.presentationMode=blvr_xr_bridge::PresentationMode::WorldStereo;contact.sameSimulationTick=true;
    contact.producerPid=100;contact.producerEpoch=20;contact.transactionId=30;
    for(unsigned eye=0;eye<2;++eye) {
        contact.sourceFrameId[eye]=40;contact.poseSequence[eye]=50;contact.renderedDisplayTime[eye]=1000000000;
        rendered[eye].tipMask=2;rendered[eye].poses[GuitarHeadstock]={{0,0,0,1},{.4f,1.1f,-.5f}};
    }
    blvr_xr_bridge::PoseBridge input{};
    input.frameId=51;input.predictedDisplayTime=1010000000;input.referenceSpaceGeneration=3;
    input.flags=blvr_xr_bridge::PoseBridgeViewsValid|blvr_xr_bridge::PoseBridgeHmdValid|blvr_xr_bridge::PoseBridgeRightControllerValid;
    input.controllers[1].activeFlags=blvr_xr_bridge::ControllerGripPose;
    RenderedUiInteractionPair interaction;
    interaction.capture(contact,7,3,rendered,{true,true});
    std::array<UiMounts,2> contactMounts{};
    if(!interaction.read(contact,7,input,contactMounts)||contactMounts[1].poses[GuitarHeadstock].position.x!=.4f) {
        failure="Contact did not retain the exact both-eye native geometry";return false;
    }
    auto badInput=input;badInput.predictedDisplayTime=1150000001;
    auto badGame=contact;badGame.sourceFrameId[1]++;
    if(interaction.read(contact,7,badInput,contactMounts)||interaction.read(badGame,7,input,contactMounts)||
        interaction.read(contact,8,input,contactMounts)) {
        failure="Stale, mixed-source or different-host contact geometry was accepted";return false;
    }
    badInput=input;badInput.predictedDisplayTime=999999999;
    badGame=contact;badGame.producerPid++;
    if(interaction.read(contact,7,badInput,contactMounts)||interaction.read(badGame,7,input,contactMounts)) {
        failure="Future native geometry or a different game producer remained interactive";return false;
    }
    badGame=contact;badGame.producerEpoch++;
    if(interaction.read(badGame,7,input,contactMounts)) {
        failure="A native game restart retained an earlier contact pair";return false;
    }
    badInput=input;badInput.referenceSpaceGeneration++;
    if(interaction.read(contact,7,badInput,contactMounts)) {
        failure="A reference-space change retained a native contact plane";return false;
    }
    badInput=input;badInput.controllers[1].activeFlags=0;
    if(interaction.read(contact,7,badInput,contactMounts)) {
        failure="A rendered finger remained interactive after fresh tracking loss";return false;
    }
    if(!interaction.read(contact,7,input,contactMounts)) {
        failure="Regained fresh tracking failed to restore exact native interaction geometry";return false;
    }
    for(const auto& exact:missingPairs) {
        interaction.capture(contact,7,3,rendered,exact);
        if(interaction.read(contact,7,input,contactMounts)) {
            failure="Contact fell back to a host or single-eye mount";return false;
        }
    }
    rendered[1].tipMask=0;interaction.capture(contact,7,3,rendered,{true,true});
    if(interaction.read(contact,7,input,contactMounts)) {
        failure="One eye's native fallback hand remained a valid contact pointer";return false;
    }
    return true;
}
}
