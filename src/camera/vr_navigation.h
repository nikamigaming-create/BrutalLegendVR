#pragma once
#include <cmath>

namespace BLVR {
// The chase camera is an input reference only. It never owns the XR heading.
struct VrNavigation {
    bool initialized = false;
    bool turnLatched = true; // Require neutral after entering gameplay.
    float forwardX = 1.0f, forwardZ = 0.0f;
    float offsetX = 0.0f, offsetZ = 0.0f;
    bool riding = false;
    float carrierX = 1.0f, carrierZ = 0.0f;

    void FollowCarrier(bool mounted,float x,float z) {
        if(!mounted) {riding=false;return;}
        const float length=std::hypot(x,z);
        if(!std::isfinite(length)||length<.001f)return;
        x/=length;z/=length;
        if(!riding) {
            // Boarding puts the neutral view toward the dashboard. Thereafter
            // only real vehicle yaw, plus the user's own snap turns, rotates it.
            forwardX=x;forwardZ=z;offsetX=offsetZ=0;initialized=true;turnLatched=true;
        } else {
            const float cosine=carrierX*x+carrierZ*z,sine=carrierX*z-carrierZ*x;
            const float previousX=forwardX,previousOffsetX=offsetX;
            forwardX=previousX*cosine-forwardZ*sine;
            forwardZ=previousX*sine+forwardZ*cosine;
            offsetX=previousOffsetX*cosine-offsetZ*sine;
            offsetZ=previousOffsetX*sine+offsetZ*cosine;
        }
        riding=true;carrierX=x;carrierZ=z;
    }

    void Initialize(float x, float z) {
        if (initialized) return;
        const float length = std::hypot(x, z);
        if (!std::isfinite(length) || length < 0.001f) return;
        forwardX = x / length;
        forwardZ = z / length;
        initialized = true;
    }

    bool Snap(float stick, float localEyeX, float localEyeZ) {
        if (!initialized || !std::isfinite(stick)) return false;
        if (std::fabs(stick) < 0.30f) turnLatched = false;
        if (turnLatched || std::fabs(stick) < 0.70f) return false;
        turnLatched = true;
        constexpr float cosine = 0.7071067811865475f;
        const float sine = std::copysign(cosine, stick);
        const float oldX = -forwardZ * localEyeX - forwardX * localEyeZ;
        const float oldZ = forwardX * localEyeX - forwardZ * localEyeZ;
        const float nextX = forwardX * cosine - forwardZ * sine;
        const float nextZ = forwardX * sine + forwardZ * cosine;
        forwardX = nextX;
        forwardZ = nextZ;
        // Turn about the current HMD, including its room-scale translation.
        offsetX += oldX - (-forwardZ * localEyeX - forwardX * localEyeZ);
        offsetZ += oldZ - (forwardX * localEyeX - forwardZ * localEyeZ);
        return true;
    }

    static void MapMovement(float headX, float headZ, float nativeX, float nativeZ,
                            float& x, float& y) {
        const float headLength = std::hypot(headX, headZ);
        const float nativeLength = std::hypot(nativeX, nativeZ);
        if (headLength < 0.001f || nativeLength < 0.001f) return;
        headX /= headLength; headZ /= headLength;
        nativeX /= nativeLength; nativeZ /= nativeLength;
        const float worldX = -headZ * x + headX * y;
        const float worldZ = headX * x + headZ * y;
        x = -nativeZ * worldX + nativeX * worldZ;
        y = nativeX * worldX + nativeZ * worldZ;
    }
};
} // namespace BLVR
