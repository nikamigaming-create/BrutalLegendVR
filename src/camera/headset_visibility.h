#pragma once
#include "tracked_basis.h"
#include <algorithm>

namespace BLVR {
// Conservative binocular bounds around a head-centered camera. Eight degrees
// cover pose changes between scene collection and submission. A small setback
// includes nearby hands/IPD without widening the actual rendered eye images.
inline bool HeadsetCullTangents(const float headQ[4],const float headP[3],
    const float eyeQ[2][4],const float eyeP[2][3],const float fov[2][4],
    float& tanX,float& tanY) {
    float hr[3],hu[3],hf[3];
    if(!TrackedEyeBasis(headQ,0,-1,hr,hu,hf))return false;
    tanX=tanY=0;
    for(int eye=0;eye<2;++eye) {
        float er[3],eu[3],ef[3];
        if(!TrackedEyeBasis(eyeQ[eye],0,-1,er,eu,ef))return false;
        for(float distance:{.1f,4096.f})for(int x=0;x<2;++x)for(int y=2;y<4;++y) {
            const float tx=std::tan(fov[eye][x]),ty=std::tan(fov[eye][y]);
            if(!std::isfinite(tx)||!std::isfinite(ty))return false;
            float px=0,py=0,pz=0;
            for(int k=0;k<3;++k) {
                const float v=eyeP[eye][k]-headP[k]+distance*(er[k]*tx+eu[k]*ty+ef[k])+3.f*hf[k];
                px+=v*hr[k];py+=v*hu[k];pz+=v*hf[k];
            }
            if(!std::isfinite(pz)||pz<=.001f)return false;
            tanX=(std::max)(tanX,std::fabs(px)/pz);
            tanY=(std::max)(tanY,std::fabs(py)/pz);
        }
    }
    constexpr float margin=.13962634f;
    if(std::atan(tanX)+margin>=1.55334f||std::atan(tanY)+margin>=1.55334f)return false;
    tanX=std::tan(std::atan(tanX)+margin);tanY=std::tan(std::atan(tanY)+margin);
    return tanX>.1f&&tanY>.1f;
}
}
