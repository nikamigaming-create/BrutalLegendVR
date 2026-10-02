#pragma once
#include <cstdint>

namespace BLVR {
// The pointer reads remain inside the retail adapter's SEH guard. Keep the
// ownership/freshness decision independent so stale or foreign UI fails shut.
struct NativeBuildUiOwnership {
    uintptr_t actor=0,registeredActor=0,playerOwner=0;
    uintptr_t build=0,confirmedBuild=0,movie=0;
    uint32_t actorHandle=0,controllerHandle=0,capacity=0;
    uint8_t open=0,confirmedOpen=0;
};
inline bool NativeBuildUiOpen(const NativeBuildUiOwnership& state,bool supported,
                             uint32_t ownerAgeMs) {
    return supported&&ownerAgeMs<500u&&state.actor&&
        state.actor==state.registeredActor&&state.actor==state.playerOwner&&
        state.capacity&&state.capacity<=0x100000u&&state.actorHandle<state.capacity&&
        state.controllerHandle==state.actorHandle&&state.build&&
        state.build==state.confirmedBuild&&state.movie&&
        state.open==1u&&state.confirmedOpen==1u;
}

struct NativeBuildStageSelection {
    uintptr_t radial=0,confirmedRadial=0,node=0,confirmedNode=0;
    uintptr_t radialEntries=0,confirmedRadialEntries=0;
    uintptr_t items=0,confirmedItems=0,item=0,confirmedItem=0;
    uint32_t radialCount=0,confirmedRadialCount=0,itemCount=0,confirmedItemCount=0;
    uint32_t index=0,matchingNodes=0;
    uint8_t isTierUpgrade=0,confirmedIsTierUpgrade=0;
};
inline bool NativeBuildStageUpgradeSelected(const NativeBuildUiOwnership& owner,
                                          const NativeBuildStageSelection& selection,
                                          bool supported,uint32_t ownerAgeMs) {
    return NativeBuildUiOpen(owner,supported,ownerAgeMs)&&selection.radial&&
        selection.radial==selection.confirmedRadial&&selection.node&&
        selection.node==selection.confirmedNode&&selection.radialEntries&&
        selection.radialEntries==selection.confirmedRadialEntries&&selection.items&&
        selection.items==selection.confirmedItems&&selection.item&&
        selection.item==selection.confirmedItem&&selection.radialCount&&
        selection.radialCount<=128u&&selection.itemCount&&selection.itemCount<=128u&&
        selection.radialCount==selection.confirmedRadialCount&&
        selection.itemCount==selection.confirmedItemCount&&selection.matchingNodes==1u&&
        selection.index<selection.radialCount&&selection.index<selection.itemCount&&
        selection.isTierUpgrade==1u&&selection.confirmedIsTierUpgrade==1u;
}
}
