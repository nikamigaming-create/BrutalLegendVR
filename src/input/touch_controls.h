#pragma once
#include "control_bindings.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace BLVR {
// Reserved bridge words remain pointer-free: primary hand publishes selection
// and physical gestures while both original analog triggers remain intact.
constexpr uint32_t RigActionMetadata = 1u<<30;
constexpr uint32_t SoloNotesMetadata = 1u<<29;
constexpr uint32_t SoloRadialMetadata = 1u<<28;
constexpr uint32_t DrivingMetadata = 1u<<27;
constexpr uint32_t WheelSteeringMetadata = 1u<<26;
constexpr uint32_t HostRadialMetadata = 1u<<25;
constexpr uint32_t HostAcceptMetadata = 1u<<24;
constexpr uint32_t HostRadialAxesMetadata = 1u<<23;
constexpr uint32_t HostConfirmMetadata = 1u<<22;
constexpr uint32_t HostOpeningAxesMetadata = 1u<<21;
constexpr uint32_t BuildRadialMetadata = 1u<<20;
constexpr uint32_t GuitarFretMetadata = 1u<<19;
struct NativeControls {
    uint8_t buttons[NativeActionCount]{};
    uint8_t analog[2]{};
    bool command=false,radial=false,targeting=false,build=false;
};
inline bool ActionInScope(NativeAction action,bool command,const ControlBindings& bindings) {
    const bool global=action==UiStart||action==Journal;
    return bindings.actions[action].command?command:(!command||global);
}
inline bool ScopedActionDown(const TouchControls& in,NativeAction action,bool command,const ControlBindings& bindings) {
    return ActionInScope(action,command,bindings)&&TouchValue(in,bindings.actions[action].input)>.5f;
}
inline float ScopedInputStrength(const TouchControls& in,NativeAction action,bool command,const ControlBindings& bindings) {
    return ActionInScope(action,command,bindings)?InputStrength(in,bindings.actions[action].input):0.f;
}
inline NativeControls MapTouch(const TouchControls& in,const ControlBindings& bindings=ActiveBindings()) {
    NativeControls out;
    if(RecenterHeld(in,bindings))return out;
    out.command=CommandHeld(in,bindings);
    const auto down=[&](NativeAction action){
        return ScopedActionDown(in,action,out.command,bindings);
    };
    const auto press=[&](NativeAction action,bool value){out.buttons[action]=value?255:0;};
    const bool pause=down(UiStart)||down(Journal);
    // Native feedback distinguishes a real held build wheel from front-end
    // menus, where the same configured input can also change a shoulder tab.
    out.build=in.buildRadial&&!pause&&!in.driving&&!in.soloNotes;
    out.radial=(down(RockStance)||in.soloRadial||in.hostRadial)&&!pause&&!out.command&&!in.driving;
    out.targeting=down(Target)&&!out.command&&!in.driving&&!in.guitarFretting;
    const bool normal=!out.command;
    for(unsigned id=0;id<NativeActionCount;++id) {
        const auto action=static_cast<NativeAction>(id);
        press(action,down(action)&&(bindings.actions[id].command?out.command:normal));
    }
    const bool attack=normal&&!out.radial&&!in.soloNotes&&!in.driving&&!pause;
    const bool combo=attack&&!in.guitarFretting&&EarthshakerHeld(in,bindings);
    press(Axe,attack&&(combo||(down(Axe)&&(!in.rigMetadata||in.weapon==1))||in.physical==1));
    press(Guitar,attack&&(combo||(down(Guitar)&&in.rigMetadata&&in.weapon==2)||
        (!in.rigMetadata&&down(Target))||in.physical==2));
    press(UiY,out.radial||(normal&&down(UiY)));
    press(UiStart,down(UiStart));press(Journal,down(Journal));
    if(out.command) {
        press(UiUp,down(UiUp)||down(OrderCharge));press(UiDown,down(UiDown)||down(OrderDefend));
        press(UiRight,down(UiRight)||down(OrderMove));press(UiLeft,down(UiLeft)||down(OrderFollow));
    }
    press(Target,out.targeting);press(RockStance,out.radial);
    if(in.hostConfirm&&normal) {press(UiA,true);press(Accept,true);}
    press(RadialAccept,normal&&(down(RadialAccept)||TouchValue(in,bindings.soloAcceptAlternate)>.5f||in.hostAccept));
    const bool strum=normal&&in.soloNotes&&in.physical==2;
    const bool fretting=in.soloNotes&&TouchValue(in,bindings.soloFret)>.5f;
    const unsigned note=in.soloStrumNote>=1&&in.soloStrumNote<=3?in.soloStrumNote:down(SoloNote3)?3u:down(SoloNote2)?2u:1u;
    for(unsigned n=0;n<3;++n) {
        const auto action=static_cast<NativeAction>(SoloNote1+n);
        press(action,(!fretting&&!strum&&normal&&down(action))||(strum&&note==n+1));
    }
    press(Boost,(normal&&down(Boost))||(out.command&&TouchValue(in,bindings.commandBoost)>.5f));
    const auto analog=[](float value){return static_cast<uint8_t>(std::clamp(value,0.f,1.f)*255.f);};
    const auto vehicleAnalog=[&](NativeAction action) {
        return normal&&ActionInScope(action,out.command,bindings)?analog(TouchValue(in,bindings.actions[action].input)):uint8_t(0);
    };
    out.analog[0]=vehicleAnalog(PrimaryVehicleAttack);
    out.analog[1]=vehicleAnalog(SecondaryVehicleAttack);
    if(out.build) {
        // Retail build consumer 0x900a80 requests recruitment with action25,
        // queued cancellation with35 and research upgrades separately with16.
        // Confirmation cannot reuse any input required to hold the wheel open:
        // those inputs are already down when native modal feedback arrives.
        const bool held=out.buttons[BuildMenu]!=0;
        const auto modalDown=[&](NativeAction action) {
            const auto& binding=bindings.actions[action];
            return !BuildInputHeldByChord(binding.input,bindings)&&(!binding.command||out.command)&&TouchValue(in,binding.input)>.5f;
        };
        const bool alternate=!BuildInputHeldByChord(bindings.soloAcceptAlternate,bindings)&&TouchValue(in,bindings.soloAcceptAlternate)>.5f;
        const bool researchAlternate=!BuildInputHeldByChord(bindings.buildResearchAlternate,bindings)&&TouchValue(in,bindings.buildResearchAlternate)>.5f;
        for(unsigned action=0;action<NativeActionCount;++action)
            if(action!=BuildMenu&&action!=UiStart&&action!=Journal&&action!=Cancel)out.buttons[action]=0;
        press(RadialAccept,held&&(modalDown(RadialAccept)||alternate));
        press(CancelBuildItem,held&&modalDown(CancelBuildItem));
        press(Use,held&&(modalDown(Use)||researchAlternate));
        out.radial=out.targeting=false;
        out.analog[0]=out.analog[1]=0;
    }
    return out;
}
}
