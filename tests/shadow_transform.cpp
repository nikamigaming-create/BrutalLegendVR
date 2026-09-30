#include "../src/camera/shadow_transform.h"
#include "../src/camera/tracked_basis.h"
#include <cstdio>
#include <cstdlib>

static void Check(bool okay,const char* name) {
    if (!okay) { std::fprintf(stderr,"FAIL: %s\n",name);std::exit(1); }
}
static void Transform(const float* p,const float* m,float* out) {
    for(int c=0;c<4;++c) {out[c]=0;for(int k=0;k<4;++k)out[c]+=p[k]*m[k*4+c];}
}
int main() {
    const float oldWorld[16]={0,0,1,0, 0,1,0,0, -1,0,0,0, 8,2,-6,1};
    const float light[16]={.2f,.1f,.3f,0, 0,.4f,.2f,0, -.3f,0,.1f,0, .8f,.2f,.4f,1};
    const float worldPoint[4]={10,0,-3,1};
    float oldView[16],nativeShadow[16],expected[4];
    BLVR::InvertCameraMatrix(oldWorld,oldView);
    BLVR::MultiplyCameraMatrices(oldWorld,light,nativeShadow);
    Transform(worldPoint,light,expected);
    float brokenError=0;
    for(int pose=0;pose<100;++pose) for(int eye=0;eye<2;++eye) {
        const float a=pose*.07f,c=std::cos(a),s=std::sin(a);
        const float world[16]={c,0,s,0, 0,1,0,0, -s,0,c,0,
            10+(eye? .032f:-.032f),2.15f,-2+pose*.01f,1};
        float view[16],shadow[16],viewPoint[4],actual[4],broken[4];
        BLVR::InvertCameraMatrix(world,view);
        BLVR::RebaseShadowMatrix(world,oldView,nativeShadow,shadow);
        Transform(worldPoint,view,viewPoint);Transform(viewPoint,shadow,actual);
        Transform(viewPoint,nativeShadow,broken);
        for(int k=0;k<4;++k) {
            Check(std::fabs(actual[k]-expected[k])<.00001f,"same world point, same shadow in both moving eyes");
            brokenError+=std::fabs(broken[k]-expected[k]);
        }
    }
    Check(brokenError>10,"negative fixture detects unre-based native shadow matrix");
    float symmetricRayError=0,oldPackingError=0;
    for (int eye=0;eye<2;++eye) {
        const float left=eye ? -0.8f : -1.2f, right=eye ? 1.2f : .8f;
        const float down=-.9f, up=1.1f;
        const float p[16]={2/(right-left),0,0,0, 0,2/(up-down),0,0,
            (right+left)/(right-left),(up+down)/(up-down),-1,-1, 0,0,-.1f,0};
        float rays[4]; BLVR::ProjectionViewRays(p,rays);
        for (int x=0;x<=10;++x) for(int y=0;y<=10;++y) {
            const float u=x*.1f,v=y*.1f,depth=5;
            const float source[4]={(left+(right-left)*u)*depth,(up+(down-up)*v)*depth,-depth,1};
            float projected[4];Transform(source,p,projected);
            const float screenU=(projected[0]/projected[3]+1)*.5f;
            const float screenV=(1-projected[1]/projected[3])*.5f;
            // Match the retail receiver shader's observed contract:
            // mad ray.xy, unitScreen.zw, screenUv, unitScreen.xy;
            // mul viewPosition.xyz, (ray.xy,1), -clipW.
            const float viewZ=-depth;
            Check(std::fabs((rays[0]+screenU*rays[2])*viewZ-source[0])<.00001f,"retail horizontal receiver ray reconstructs geometry");
            Check(std::fabs((rays[1]+screenV*rays[3])*viewZ-source[1])<.00001f,"retail vertical receiver ray reconstructs geometry");
            symmetricRayError+=std::fabs((screenU*2-1)*depth-source[0]);
            const float oldRays[4]={2/p[0],-2/p[5],(p[8]-1)/p[0],(p[9]+1)/p[5]};
            oldPackingError+=std::fabs((oldRays[0]+screenU*oldRays[2])*viewZ-source[0]);
        }
    }
    Check(symmetricRayError>100,"negative fixture detects native symmetric reconstruction");
    Check(oldPackingError>100,"negative fixture rejects the former scale-first, positive-depth packing");
    std::puts("PASS: shadow world-space invariance across both eyes, head motion and chase change; negative fixture rejected");
}
