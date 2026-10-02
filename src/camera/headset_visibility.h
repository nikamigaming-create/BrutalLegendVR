#pragma once
#include "tracked_basis.h"
#include <algorithm>

namespace BLVR {
inline constexpr float HeadsetCullSetbackMeters=3.0f;
// Conservative binocular bounds around a head-centered camera. Eight degrees
// cover pose changes between scene collection and submission. A small setback
// includes nearby hands/IPD without widening the actual rendered eye images.
inline bool HeadsetCullBounds(const float headQ[4],const float headP[3],
    const float eyeQ[2][4],const float eyeP[2][3],const float fov[2][4],
    float nearDistance,float farDistance,float& tanX,float& tanY,
    float& nearestDepth,float& farthestDepth) {
    if(!std::isfinite(nearDistance)||!std::isfinite(farDistance)||
       !(nearDistance>0)||!(farDistance>=nearDistance))return false;
    for(int k=0;k<3;++k)if(!std::isfinite(headP[k]))return false;
    float hr[3],hu[3],hf[3];
    if(!TrackedEyeBasis(headQ,0,-1,hr,hu,hf))return false;
    float boundX=0,boundY=0,minimumDepth=farDistance+HeadsetCullSetbackMeters,maximumDepth=0;
    for(int eye=0;eye<2;++eye) {
        for(int k=0;k<3;++k)if(!std::isfinite(eyeP[eye][k]))return false;
        for(int k=0;k<4;++k)if(!std::isfinite(fov[eye][k])||
            std::fabs(fov[eye][k])>=1.57079632f)return false;
        if(!(fov[eye][0]<fov[eye][1])||!(fov[eye][3]<fov[eye][2]))return false;
        float er[3],eu[3],ef[3];
        if(!TrackedEyeBasis(eyeQ[eye],0,-1,er,eu,ef))return false;
        for(float distance:{nearDistance,farDistance})for(int x=0;x<2;++x)for(int y=2;y<4;++y) {
            const float tx=std::tan(fov[eye][x]),ty=std::tan(fov[eye][y]);
            if(!std::isfinite(tx)||!std::isfinite(ty))return false;
            float px=0,py=0,pz=0;
            for(int k=0;k<3;++k) {
                const float v=eyeP[eye][k]-headP[k]+distance*(er[k]*tx+eu[k]*ty+ef[k])+HeadsetCullSetbackMeters*hf[k];
                px+=v*hr[k];py+=v*hu[k];pz+=v*hf[k];
            }
            if(!std::isfinite(px)||!std::isfinite(py)||!std::isfinite(pz)||pz<=.001f)return false;
            boundX=(std::max)(boundX,std::fabs(px)/pz);
            boundY=(std::max)(boundY,std::fabs(py)/pz);
            minimumDepth=(std::min)(minimumDepth,pz);
            maximumDepth=(std::max)(maximumDepth,pz);
        }
    }
    constexpr float margin=.13962634f;
    if(std::atan(boundX)+margin>=1.55334f||std::atan(boundY)+margin>=1.55334f)return false;
    boundX=std::tan(std::atan(boundX)+margin);boundY=std::tan(std::atan(boundY)+margin);
    if(!(boundX>.1f)||!(boundY>.1f))return false;
    tanX=boundX;tanY=boundY;nearestDepth=minimumDepth;farthestDepth=maximumDepth;
    return true;
}
inline bool HeadsetCullTangents(const float headQ[4],const float headP[3],
    const float eyeQ[2][4],const float eyeP[2][3],const float fov[2][4],
    float& tanX,float& tanY) {
    float nearest=0,farthest=0;
    return HeadsetCullBounds(headQ,headP,eyeQ,eyeP,fov,.1f,4096.f,tanX,tanY,nearest,farthest);
}

// The setback changes depth as well as angular bounds. Keeping the native far
// plane used to reject the last three metres of either eye's visible world;
// canted-eye corners can be still farther from the collection camera. Expand
// only the collection projection, leaving the rendered eye depth untouched.
inline bool HeadsetCullProjection(const float source[16],const float headQ[4],const float headP[3],
    const float eyeQ[2][4],const float eyeP[2][3],const float fov[2][4],float output[16]) {
    for(int k=0;k<16;++k)if(!std::isfinite(source[k]))return false;
    if(source[10]>-1.f||std::fabs(source[11]+1.f)>.0001f||
       std::fabs(source[15])>.0001f||!(source[14]<0))return false;
    const float nearDistance=source[14]/source[10];
    const bool infiniteFar=source[10]==-1.f;
    const float farDistance=infiniteFar?4096.f:source[14]/(source[10]+1.f);
    float tanX=0,tanY=0,minimumDepth=0,maximumDepth=0;
    if(!HeadsetCullBounds(headQ,headP,eyeQ,eyeP,fov,nearDistance,farDistance,
                         tanX,tanY,minimumDepth,maximumDepth))return false;
    const double nearCull=(std::min)(double(nearDistance),double(minimumDepth)*.99999);
    const double farCull=(std::max)(double(farDistance),double(maximumDepth)*1.00001);
    float corrected[16];
    for(int k=0;k<16;++k)corrected[k]=source[k];
    corrected[0]=1/tanX;corrected[5]=1/tanY;corrected[8]=corrected[9]=0;
    corrected[10]=infiniteFar?-1.f:static_cast<float>(-farCull/(farCull-nearCull));
    corrected[14]=infiniteFar?static_cast<float>(-nearCull):
        static_cast<float>(-farCull*nearCull/(farCull-nearCull));
    for(int k=0;k<16;++k)output[k]=corrected[k];
    return true;
}
}
