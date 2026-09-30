#pragma once
#include "../bridge/blvr_rig_bridge.h"

namespace BLVR {
// The native chase camera targets the mount while driving. Resolve its rider
// through CoMount instead of treating the vehicle skeleton as Eddie.
void* PlayerViewRig_ResolveActor(void* cameraActor);
// Boss cameras can target Eddie even while he is driving. Resolve the mount
// from CoPlayer's carrier handle and verify the reciprocal CoMount rider.
void* PlayerViewRig_ResolveMount(void* playerActor);
// A bounded edit of the player's render snapshot; simulation and shadow
// snapshots are left alone. Begin returns the anatomical eye midpoint in a
// level root frame, excluding combat animation bob and tilt.
bool PlayerViewRig_Begin(void* scene, void* playerActor, float eyeWorld[3], bool mounted=false);
// Collection must use the same anatomical seat/root policy as stereo rendering.
bool PlayerViewRig_ReadLiveEyeAnchor(void* playerActor,bool mounted,float eyeWorld[3]);
bool PlayerViewRig_ApplyTracked(const blvr_xr_bridge::RigFrame&,const float headWorld[16],bool nativeDrivingPose=false,bool wheelGrip=true);
void PlayerViewRig_PublishRenderedUi(uint64_t sourceFrame,uint64_t poseFrame,
    int64_t displayTime,uint64_t epoch,const float headWorld[16],bool mounted);
bool PlayerViewRig_ReadTracked(uint64_t frame,int64_t displayTime,uint64_t epoch,
                              blvr_xr_bridge::RigFrame&);
void PlayerViewRig_End();
}
