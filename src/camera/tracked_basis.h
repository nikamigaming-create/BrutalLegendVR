#pragma once
#include <cmath>

namespace BLVR {
inline bool InvertCameraMatrix(const float input[16], float output[16]) {
    double rows[4][8]{};
    for (int r=0;r<4;++r) for (int c=0;c<4;++c) {
        rows[r][c]=input[r*4+c]; rows[r][c+4]=r==c ? 1.0 : 0.0;
    }
    for (int c=0;c<4;++c) {
        int pivot=c;
        for (int r=c+1;r<4;++r) if (std::fabs(rows[r][c])>std::fabs(rows[pivot][c])) pivot=r;
        if (!std::isfinite(rows[pivot][c]) || std::fabs(rows[pivot][c])<1e-10) return false;
        for (int k=0;k<8;++k) { const double v=rows[c][k]; rows[c][k]=rows[pivot][k]; rows[pivot][k]=v; }
        const double scale=rows[c][c];
        for (int k=0;k<8;++k) rows[c][k]/=scale;
        for (int r=0;r<4;++r) if (r!=c) {
            const double scale2=rows[r][c];
            for (int k=0;k<8;++k) rows[r][k]-=scale2*rows[c][k];
        }
    }
    for (int r=0;r<4;++r) for (int c=0;c<4;++c) output[r*4+c]=static_cast<float>(rows[r][c+4]);
    return true;
}
// Convert the complete OpenXR orientation into Buddha's world basis. Using
// separate Euler yaw/pitch/roll changes their order and inverts the roll axis.
inline bool TrackedEyeBasis(const float q[4], float forwardX, float forwardZ,
                            float right[3], float up[3], float forward[3]) {
    const float norm = q[0]*q[0]+q[1]*q[1]+q[2]*q[2]+q[3]*q[3];
    const float heading = std::hypot(forwardX, forwardZ);
    if (!std::isfinite(norm) || norm < 0.0001f ||
        !std::isfinite(heading) || heading < 0.0001f) return false;
    const float scale = 1.0f / std::sqrt(norm);
    const float x=q[0]*scale, y=q[1]*scale, z=q[2]*scale, w=q[3]*scale;
    forwardX /= heading; forwardZ /= heading;
    const float xrRight[3] = {1-2*(y*y+z*z), 2*(x*y+w*z), 2*(x*z-w*y)};
    const float xrUp[3] = {2*(x*y-w*z), 1-2*(x*x+z*z), 2*(y*z+w*x)};
    const float xrForward[3] = {-2*(x*z+w*y), -2*(y*z-w*x), -(1-2*(x*x+y*y))};
    const auto world = [forwardX,forwardZ](const float v[3], float out[3]) {
        out[0] = -forwardZ*v[0] - forwardX*v[2];
        out[1] = v[1];
        out[2] = forwardX*v[0] - forwardZ*v[2];
    };
    world(xrRight,right); world(xrUp,up); world(xrForward,forward);
    return true;
}
} // namespace BLVR
