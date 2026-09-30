#pragma once
#include <cstring>

namespace BLVR {
inline void ProjectionViewRays(const float projection[16], float rays[4]) {
    // Buddha's g_vCameraUnitScreen is {offset.xy, scale.xy}. The retail
    // receivers reconstruct view position as (offset + uv*scale, 1)*viewZ,
    // where viewZ is NEGATIVE. This is not {scale, offset} at positive depth.
    rays[0] = (1.0f - projection[8]) / projection[0];
    rays[1] = -(1.0f + projection[9]) / projection[5];
    rays[2] = -2.0f / projection[0];
    rays[3] = 2.0f / projection[5];
}
inline void MultiplyCameraMatrices(const float* a, const float* b, float* output) {
    float value[16]{};
    for (int r=0;r<4;++r) for (int c=0;c<4;++c)
        for (int k=0;k<4;++k) value[r*4+c] += a[r*4+k]*b[k*4+c];
    std::memcpy(output,value,sizeof(value));
}
inline void RebaseShadowMatrix(const float eyeWorld[16], const float sourceView[16],
                              const float sourceShadow[16], float output[16]) {
    float sourceFromEye[16];
    MultiplyCameraMatrices(eyeWorld,sourceView,sourceFromEye);
    MultiplyCameraMatrices(sourceFromEye,sourceShadow,output);
}
} // namespace BLVR
