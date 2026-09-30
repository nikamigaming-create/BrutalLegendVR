#include "../src/camera/camera_relative_effects.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
static void Check(bool okay,const char* message) {if(!okay){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
int main() {
    // Observed retail VS: packedCenter * systemRange + localCenter +
    // g_vParticleToCamWorld, with the last term emitterWorld - collectionEye.
    const float emitter[]={1234.f,238.5f,-987.f};
    const float packed[]={.25f,.6f,-.2f},range[]={2.f,3.f,4.f},local[]={.3f,.2f,-.1f};
    const float collectionEye[]={1230.f,239.7f,-981.f};
    const float eyes[][3]={{1230.f,239.7f,-981.f},{1229.718f,239.85f,-981.4f},{1229.782f,239.85f,-981.4f},{1230.23f,239.6f,-980.8f}};
    float stale[3];BLVR::EffectTranslationForEye(emitter,collectionEye,stale);
    bool rejectedStale=false;
    for(const auto& eye:eyes) {
        float translated[3];BLVR::EffectTranslationForEye(emitter,eye,translated);
        for(int k=0;k<3;++k) {
            const float renderedWorld=packed[k]*range[k]+local[k]+translated[k]+eye[k];
            const float authoredWorld=packed[k]*range[k]+local[k]+emitter[k];
            Check(std::fabs(renderedWorld-authoredWorld)<.0002f,"particle center stays at its authored world point under head/eye motion");
            const float staleWorld=packed[k]*range[k]+local[k]+stale[k]+eye[k];
            rejectedStale|=std::fabs(staleWorld-authoredWorld)>.1f;
        }
    }
    Check(rejectedStale,"old collection-camera translation demonstrably drifts");
    std::puts("PASS: retail particle equation, both eye origins, head translation and stale-camera negative fixture");
}
