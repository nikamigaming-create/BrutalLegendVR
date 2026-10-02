#include "../src/camera/camera_relative_effects.h"
#include "../src/camera/effect_attachment_evidence.h"
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
    Check(BLVR::EffectOwnerMatchesPlayer(326,326,53,55,true),"exact current player handle owns an effect");
    Check(BLVR::EffectOwnerMatchesPlayer(53,326,53,55,true)&&
        BLVR::EffectOwnerMatchesPlayer(55,326,53,55,true),"verified reciprocal inventory identifies axe and guitar ownership");
    Check(!BLVR::EffectOwnerMatchesPlayer(53,326,53,55,false)&&
        !BLVR::EffectOwnerMatchesPlayer(55,326,53,55,false),"stale or unrelated inventory cannot confer weapon ownership");
    Check(!BLVR::EffectOwnerMatchesPlayer(54,326,53,55,true),"another actor or weapon handle cannot borrow player evidence");
    Check(!BLVR::EffectOwnerMatchesPlayer(0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,true)&&
        !BLVR::EffectOwnerMatchesPlayer(0,326,0,0,true),"empty native weapon handles never classify unrelated effects");
    Check(BLVR::EffectOwnerMatchesPlayer(0,0,53,55,false),"native entity slot zero may be a verified current player");
    BLVR::EffectAttachmentEvidence source{};
    source.identity={0x1000,0x2000,3,7};source.parent=40;source.owner=40;source.bone=9;source.serial=4;
    source.observedAt=900;source.sourceScene=0x3000;
    for(int k=0;k<3;++k)source.emitter[k]=emitter[k];
    Check(BLVR::EffectEvidenceMatchesSystem(source,source.identity,4,emitter,1000),"exact live event identity and emitter serial join");
    auto reused=source.identity;reused.generation++;
    Check(!BLVR::EffectEvidenceMatchesSystem(source,reused,4,emitter,1000),"same system pointer with reused pool generation is rejected");
    reused=source.identity;reused.index++;
    Check(!BLVR::EffectIdentityMatches(source.identity,reused),"wrong system pool index is rejected");
    reused=source.identity;reused.pool+=4;
    Check(!BLVR::EffectIdentityMatches(source.identity,reused),"different pool with matching index and generation is rejected");
    reused=source.identity;reused.system+=4;
    Check(!BLVR::EffectIdentityMatches(source.identity,reused),"different system pointer is rejected");
    Check(!BLVR::EffectEvidenceMatchesSystem(source,source.identity,5,emitter,1000),"later native emitter update cannot borrow old attachment metadata");
    Check(!BLVR::EffectEvidenceMatchesSystem(source,source.identity,4,emitter,1151)&&
        !BLVR::EffectEvidenceMatchesSystem(source,source.identity,4,emitter,899),"stale or future attachment observation is rejected");
    float displaced[3]={emitter[0]+1,emitter[1],emitter[2]};
    Check(!BLVR::EffectEvidenceMatchesSystem(source,source.identity,4,displaced,1000),"changed emitter cannot silently reuse attachment evidence");
    displaced[0]=NAN;
    Check(!BLVR::EffectEvidenceMatchesPacket(source,displaced),"nonfinite particle packet origin is rejected");
    Check(BLVR::EffectEvidenceMatchesPacket(source,emitter),"copied packet retains exact collection emitter evidence");
    auto missing=source;missing.sourceScene=0;
    Check(!BLVR::EffectEvidenceMatchesPacket(missing,emitter),"packet without collection scene evidence is rejected");
    source.identity.generation=0;
    Check(BLVR::EffectEvidenceMatchesSystem(source,source.identity,4,emitter,1000),"generation zero is valid when native pool identity matches exactly");
    // A weapon ribbon's native appended root is separate from its earlier
    // world trail. Source->tracked rotation and translation move just this
    // newly constructed endpoint, with the authored local offset preserved.
    BLVR::EffectAttachmentEvidence ribbon{};
    ribbon.identity={0x1000,0x2000,4,8};ribbon.parent=ribbon.owner=53;
    ribbon.sourceScene=0x3000;ribbon.sourceEpoch=7;ribbon.flags=0x201e0;ribbon.historyVertices=5;ribbon.observedAt=900;
    ribbon.orientation[3]=1;ribbon.attachment[0]=10;ribbon.attachment[1]=20;ribbon.attachment[2]=30;
    ribbon.authoredOffset[1]=1;
    ribbon.emitter[0]=10;ribbon.emitter[1]=21;ribbon.emitter[2]=30;
    Check(BLVR::EffectRibbonRootEligible(ribbon,53,6,1000),"verified attached axe ribbon exposes one fresh endpoint");
    auto rejected=ribbon;rejected.flags=0x201a0;
    Check(!BLVR::EffectRibbonRootEligible(rejected,53,6,1000),"ribbon without an appended emitter vertex remains native");
    rejected=ribbon;rejected.parent=rejected.owner=55;
    Check(!BLVR::EffectRibbonRootEligible(rejected,53,6,1000),"different weapon cannot borrow current tracked attachment");
    rejected=ribbon;rejected.owner=0xffffffffu;rejected.rebound=326;
    Check(!BLVR::EffectRibbonRootEligible(rejected,53,6,1000),"player rebound/world shot remains native");
    rejected=ribbon;rejected.bone=3;
    Check(!BLVR::EffectRibbonRootEligible(rejected,53,6,1000),"skeletal effect cannot borrow weapon root mapping");
    rejected=ribbon;rejected.emitter[0]+=.01f;
    Check(!BLVR::EffectRibbonRootEligible(rejected,53,6,1000),"event and emitter setter space disagreement fails closed");
    Check(!BLVR::EffectRibbonRootEligible(ribbon,53,5,1000)&&
        !BLVR::EffectRibbonRootEligible(ribbon,53,6,1151),"missing root and aged source metadata fail closed");
    Check(!BLVR::EffectRibbonRootEligible(ribbon,53,6,1000,8),"a copied packet from an earlier host epoch cannot join the current tracked weapon");
    rejected=ribbon;rejected.sourceEpoch=0;
    Check(!BLVR::EffectRibbonRootEligible(rejected,53,6,1000,7),"an unproven source host epoch cannot move a ribbon root");
    float native[16]{},tracked[16]{};
    native[0]=native[5]=native[10]=native[15]=1;
    native[12]=10;native[13]=20;native[14]=30;
    // Ninety-degree rotation about Z, with a nearby tracked world pivot.
    tracked[1]=1;tracked[4]=-1;tracked[10]=tracked[15]=1;
    tracked[12]=10.4f;tracked[13]=20.2f;tracked[14]=29.8f;
    const float normal[]{0,1,0};float rootCenter[3]{},rootDirection[3]{};
    Check(BLVR::EffectRibbonRootDelta(ribbon.emitter,native,tracked,normal,rootCenter,rootDirection),"source weapon attachment remaps to exact tracked root");
    Check(std::fabs(rootCenter[0]+.6f)<.00001f&&std::fabs(rootCenter[1]+.8f)<.00001f&&
        std::fabs(rootCenter[2]+.2f)<.00001f&&std::fabs(rootDirection[0]+1)<.00001f&&
        std::fabs(rootDirection[1])<.00001f,"authored offset and root direction rotate with the tracked weapon");
    float invalid[16];std::memcpy(invalid,tracked,64);invalid[0]=invalid[1]=invalid[2]=0;
    Check(!BLVR::EffectRibbonRootDelta(ribbon.emitter,native,invalid,normal,rootCenter,rootDirection),"collapsed tracked weapon cannot relocate a root");
    std::memcpy(invalid,tracked,64);invalid[12]+=10;
    Check(!BLVR::EffectRibbonRootDelta(ribbon.emitter,native,invalid,normal,rootCenter,rootDirection),"far world effect cannot be pulled to a nearby weapon");
    std::memcpy(invalid,native,64);invalid[5]=NAN;
    Check(!BLVR::EffectRibbonRootDelta(ribbon.emitter,invalid,tracked,normal,rootCenter,rootDirection),"nonfinite source pose cannot move a root");
    Check(BLVR::EffectRibbonRootDelta(ribbon.emitter,native,tracked,normal,rootCenter,rootDirection),"valid root plan recovers after rejected evidence");
    uint8_t vertices[8][64]{},originalVertices[8][64]{};
    for(unsigned i=0;i<5;++i)for(unsigned b=0;b<64;++b)vertices[i][b]=static_cast<uint8_t>(i*31+b+1);
    std::memcpy(vertices[5]+0x30,normal,12);vertices[5][0x28]=73;
    std::memcpy(vertices[6],vertices[5],64);std::memcpy(vertices[7],vertices[5],64);
    std::memcpy(originalVertices,vertices,sizeof(vertices));
    BLVR::EffectRibbonRootBackup backup{};
    Check(BLVR::EffectApplyRibbonRoot(vertices[0],8,5,rootCenter,rootDirection,backup),"fresh root and identical native padding can be patched");
    Check(!std::memcmp(vertices,originalVertices,5*64),"all historical particle/trail bytes remain unchanged");
    for(unsigned i=5;i<8;++i) {
        float center[3];std::memcpy(center,vertices[i],12);
        for(const auto& eye:eyes)for(int k=0;k<3;++k) {
            float translation[3];BLVR::EffectTranslationForEye(ribbon.emitter,eye,translation);
            Check(std::fabs(center[k]+translation[k]+eye[k]-(ribbon.emitter[k]+rootCenter[k]))<.0002f,
                "each stereo eye reconstructs the same tracked endpoint world point");
        }
        for(unsigned b=12;b<64;++b)if(b<0x30||b>=0x3c)
            Check(vertices[i][b]==originalVertices[i][b],"root age/width/color/UV and lifetime bytes stay native");
    }
    BLVR::EffectRestoreRibbonRoot(backup);
    Check(!std::memcmp(vertices,originalVertices,sizeof(vertices))&&!backup.vertices,"endpoint edit restores every byte before another eye or auxiliary pass");
    vertices[6][20]++;
    Check(!BLVR::EffectApplyRibbonRoot(vertices[0],8,5,rootCenter,rootDirection,backup),"nonidentical padding cannot disguise a historical vertex");
    std::memcpy(vertices,originalVertices,sizeof(vertices));vertices[5][0]=1;
    Check(!BLVR::EffectApplyRibbonRoot(vertices[0],8,5,rootCenter,rootDirection,backup),"nonzero root center rejects an unproven packed layout");
    std::puts("PASS: particle world/eye origin; exact attachment lineage; tracked appended ribbon root preserves history, offsets, stereo world point, lifetime bytes and restoration");
}
