#include "../src/camera/vr_navigation.h"
#include <cstdio>
#include <cstdlib>

static void Check(bool okay, const char* label) {
    if (!okay) { std::fprintf(stderr, "FAIL: %s\n", label); std::exit(1); }
}
static bool Near(float a, float b) { return std::fabs(a-b) < 0.0001f; }

int main() {
    BLVR::VrNavigation nav;
    nav.Initialize(1, 0);
    // Replay arbitrary chase auto-centering, including the rotating headings
    // from the failed physical run. None may change the XR reference frame.
    for (int i=0; i<3600; ++i) nav.Initialize(std::cos(i*.1f), std::sin(i*.1f));
    Check(Near(nav.forwardX,1) && Near(nav.forwardZ,0), "chase camera cannot steer XR");
    Check(!nav.Snap(1,.4f,-.3f), "require neutral on entry");
    nav.Snap(0,.4f,-.3f);
    const float eyeX = .3f, eyeZ = .4f;
    for (int turn=0; turn<8; ++turn) {
        Check(nav.Snap(1,.4f,-.3f), "one right snap after neutral");
        const float x = nav.offsetX-nav.forwardZ*.4f+nav.forwardX*.3f;
        const float z = nav.offsetZ+nav.forwardX*.4f+nav.forwardZ*.3f;
        Check(Near(x,eyeX) && Near(z,eyeZ), "snap keeps room-scale HMD pivot fixed");
        for (int i=0; i<120; ++i) Check(!nav.Snap(1,.4f,-.3f), "held stick cannot spin");
        Check(!nav.Snap(-1,.4f,-.3f), "opposite deflection requires neutral too");
        nav.Snap(.2f,.4f,-.3f);
    }
    Check(Near(nav.forwardX,1) && Near(nav.forwardZ,0), "eight 45 degree turns close");
    for (int i=0; i<360; ++i) {
        const float nx=std::cos(i*.0174532925f), nz=std::sin(i*.0174532925f);
        float x=0,y=1;
        BLVR::VrNavigation::MapMovement(0,1,nx,nz,x,y);
        Check(Near(-nz*x+nx*y,0) && Near(nx*x+nz*y,1),
              "forward follows HMD regardless of chase heading");
        Check(Near(std::hypot(x,y),1), "movement magnitude preserved");
    }
    nav.FollowCarrier(true,0,1);
    Check(nav.riding&&Near(nav.forwardX,0)&&Near(nav.forwardZ,1),"boarding faces the actual vehicle");
    nav.Snap(0,0,0);nav.Snap(1,0,0);
    const float snappedX=nav.forwardX,snappedZ=nav.forwardZ;
    nav.FollowCarrier(true,-1,0);
    Check(Near(nav.forwardX,-snappedZ)&&Near(nav.forwardZ,snappedX),"vehicle yaw preserves the user's relative snap turn");
    nav.Initialize(1,0);
    Check(Near(nav.forwardX,-snappedZ),"chase auto-centering cannot change vehicle view");
    nav.FollowCarrier(false,0,0);
    Check(!nav.riding&&Near(nav.forwardX,-snappedZ),"dismount preserves current heading");
    std::puts("PASS: chase isolation, snap neutral/latch, fixed HMD pivot, head-relative movement, carrier yaw and dismount");
}
