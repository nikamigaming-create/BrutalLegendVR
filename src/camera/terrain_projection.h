#pragma once
#include <cstring>

namespace BLVR {
// Terrain vertices are world-aligned but relative to the current camera.
// Retail bakes its source camera into the texture matrix during collection.
// Change that origin without changing the authored world-to-texture mapping.
inline void RebaseTerrainProjection(const float sourceEye[3], const float eye[3],
                                    const float source[16], float output[16]) {
    float corrected[16];
    std::memcpy(corrected,source,sizeof(corrected));
    for(int c=0;c<4;++c) {
        double value=source[12+c];
        for(int k=0;k<3;++k)value+=(double(eye[k])-sourceEye[k])*source[k*4+c];
        corrected[12+c]=static_cast<float>(value);
    }
    std::memcpy(output,corrected,sizeof(corrected));
}

bool TerrainProjection_Init();
void TerrainProjection_Shutdown();
}
