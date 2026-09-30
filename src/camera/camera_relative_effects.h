#pragma once
namespace BLVR {
bool CameraRelativeEffects_Init();
void CameraRelativeEffects_Shutdown();
// Particle snapshots retain their world-space emitter position as well as a
// translation baked against the collection camera. Only XYZ is positional.
inline void EffectTranslationForEye(const float world[3],const float eye[3],float translation[3]) {
    for(int k=0;k<3;++k)translation[k]=world[k]-eye[k];
}
}
