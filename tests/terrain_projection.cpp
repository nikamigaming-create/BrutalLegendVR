#include "../src/camera/terrain_projection.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

static void Check(bool condition,const char* description) {
    if(!condition) {std::fprintf(stderr,"FAIL: %s\n",description);std::exit(1);}
}
static void Project(const float point[3],const float origin[3],const float matrix[16],float uv[4]) {
    for(int c=0;c<4;++c) {
        double value=matrix[12+c];
        for(int k=0;k<3;++k)value+=(double(point[k])-origin[k])*matrix[k*4+c];
        uv[c]=float(value);
    }
}
int main() {
    const float zero[3]{};
    // Oblique, scaled and offset terrain layers at a distant world location.
    // The expected UV comes straight from the authored world mapping.
    const float worldToUv[16]={.0625f,0,.03125f,0, .015625f,1,-.0625f,0,
        -.03125f,0,.125f,0, 71,0,-295,1};
    const float point[3]={-2304,74.25f,2618};
    const float collectedEye[3]={-2295,77,2607};
    float baked[16],expected[4],bad[4];
    BLVR::RebaseTerrainProjection(zero,collectedEye,worldToUv,baked);
    Project(point,zero,worldToUv,expected);
    float oldError=0;
    for(int step=0;step<100;++step)for(int eye=0;eye<2;++eye) {
        const float a=.06f*step;
        const float renderEye[3]={-2301+.25f*std::sin(a)+(eye?.032f:-.032f),
            76.64f+.2f*std::cos(a),2612+.25f*std::cos(a)};
        float corrected[16],actual[4];
        BLVR::RebaseTerrainProjection(collectedEye,renderEye,baked,corrected);
        Project(point,renderEye,corrected,actual);
        Project(point,renderEye,baked,bad);
        for(int c=0;c<4;++c) {
            Check(std::fabs(actual[c]-expected[c])<.0001f,"both moving eyes preserve authored terrain coordinates");
            oldError+=std::fabs(bad[c]-expected[c]);
        }
        BLVR::RebaseTerrainProjection(renderEye,collectedEye,corrected,corrected);
        for(int c=0;c<16;++c)Check(std::fabs(corrected[c]-baked[c])<.0001f,"round trip and in-place rebase preserve source");
    }
    Check(oldError>100,"negative fixture detects the stale native terrain origin");
    std::puts("PASS: terrain world anchoring across both eyes, lean, height and origin changes; stale origin rejected");
}
