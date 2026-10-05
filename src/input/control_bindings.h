#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace BLVR {
enum NativeAction : unsigned {
    UiA,UiB,UiX,UiY,UiStart,UiBack,UiUp,UiDown,UiLeft,UiRight,
    UiTriggerLeft,UiTriggerRight,UiShoulderLeft,UiShoulderRight,Accept,Cancel,
    Use,Map,Journal,Axe,Guitar,Evade,Block,Target,RockStance,RadialAccept,
    SoloNote1,SoloNote2,SoloNote3,Beacon,OrderCharge,OrderDefend,OrderMove,
    OrderFollow,BuildMenu,CancelBuildItem,Fly,Descend,Ascend,Handbrake,Boost,
    AlternateCam,PrimaryVehicleAttack,SecondaryVehicleAttack,LeftCoopAttack,
    RightCoopAttack,PlaylistUI,PlaylistToggle,PlaylistNext,PlaylistPrev,
    PlaylistRewind,NativeActionCount
};
static_assert(NativeActionCount==51);
struct TouchControls {
    float lx=0,ly=0,rx=0,ry=0,lt=0,rt=0,lg=0,rg=0;
    bool a=false,b=false,x=false,y=false,leftClick=false,rightClick=false,menu=false;
    bool rigMetadata=false,soloNotes=false,soloRadial=false,buildRadial=false,driving=false,hostRadial=false,hostAccept=false,hostConfirm=false;
    unsigned weapon=0,physical=0,soloStrumNote=0;
    bool guitarFretting=false;
};
enum class TouchInput { A,B,X,Y,LeftTrigger,RightTrigger,LeftGrip,RightGrip,
    LeftClick,RightClick,Menu,LeftUp,LeftDown,LeftLeft,LeftRight,
    RightUp,RightDown,RightLeft,RightRight,None };
enum class TouchStick { Left,Right };
struct ActionBinding { TouchInput input; bool command=false; };
struct ControlBindings {
    std::array<ActionBinding,NativeActionCount> actions;
    TouchInput commandGrip=TouchInput::LeftGrip,commandClick=TouchInput::LeftClick;
    TouchInput equipAxe=TouchInput::X,equipGuitar=TouchInput::Y;
    TouchInput earthshakerLeft=TouchInput::LeftGrip,earthshakerRight=TouchInput::RightGrip;
    TouchInput wheelGrip=TouchInput::RightGrip,commandBoost=TouchInput::RightGrip;
    TouchInput recenterModifier=TouchInput::Menu,recenterClick=TouchInput::RightClick;
    TouchInput supportLeft=TouchInput::LeftGrip,supportRight=TouchInput::RightGrip;
    TouchInput soloFret=TouchInput::LeftGrip;
    TouchInput soloAcceptAlternate=TouchInput::RightTrigger,openingConfirmAlternate=TouchInput::RightTrigger;
    TouchInput buildResearchAlternate=TouchInput::LeftTrigger;
    TouchStick movementStick=TouchStick::Left,turnStick=TouchStick::Right;
    TouchStick radialStick=TouchStick::Right,openingMenuStick=TouchStick::Right;
};
using TI=TouchInput;
// The input evaluator and all native hint aliases consume this same table.
inline constexpr ControlBindings DefaultBindings{{{
    {TI::A},{TI::B},{TI::X},{TI::Y},{TI::Menu},{TI::X,true},
    {TI::LeftUp},{TI::LeftDown},{TI::LeftLeft},{TI::LeftRight},
    {TI::LeftTrigger,true},{TI::RightTrigger,true},{TI::B,true},{TI::A,true},
    {TI::A},{TI::B},{TI::A},{TI::X,true},{TI::Menu},
    {TI::RightTrigger},{TI::RightTrigger},{TI::B},{TI::B},{TI::LeftTrigger},
    {TI::RightClick},{TI::A},{TI::A},{TI::X},{TI::Y},
    {TI::B,true},{TI::RightUp,true},{TI::RightDown,true},{TI::RightRight,true},
    {TI::RightLeft,true},{TI::A,true},{TI::X},{TI::Y,true},
    {TI::LeftTrigger,true},{TI::RightTrigger,true},{TI::B},{TI::LeftClick},
    {TI::B,true},{TI::RightTrigger},{TI::LeftTrigger},
    {TI::LeftTrigger,true},{TI::RightTrigger,true},
    {TI::RightUp,true},{TI::RightDown,true},{TI::RightRight,true},{TI::RightLeft,true},{TI::RightClick,true}
}}};
ControlBindings ActiveBindings();
uint64_t ControlsSignature(const ControlBindings&);
inline float TouchValue(const TouchControls& in,TouchInput key) {
    const bool leftVertical=std::fabs(in.ly)>=std::fabs(in.lx);
    const bool vertical=std::fabs(in.ry)>=std::fabs(in.rx);
    const auto analog=[](float value){return std::isfinite(value)?std::clamp(value,0.f,1.f):0.f;};
    switch(key) {
    case TI::A:return in.a?1.f:0.f; case TI::B:return in.b?1.f:0.f;
    case TI::X:return in.x?1.f:0.f; case TI::Y:return in.y?1.f:0.f;
    case TI::LeftTrigger:return analog(in.lt);case TI::RightTrigger:return analog(in.rt);
    case TI::LeftGrip:return analog(in.lg);case TI::RightGrip:return analog(in.rg);
    case TI::LeftClick:return in.leftClick?1.f:0.f;case TI::RightClick:return in.rightClick?1.f:0.f;
    case TI::Menu:return in.menu?1.f:0.f;
    case TI::LeftUp:return leftVertical&&in.ly>.55f?1.f:0.f;case TI::LeftDown:return leftVertical&&in.ly<-.55f?1.f:0.f;
    case TI::LeftLeft:return !leftVertical&&in.lx<-.55f?1.f:0.f;case TI::LeftRight:return !leftVertical&&in.lx>.55f?1.f:0.f;
    case TI::RightUp:return vertical&&in.ry>.55f?1.f:0.f;
    case TI::RightDown:return vertical&&in.ry<-.55f?1.f:0.f;
    case TI::RightLeft:return !vertical&&in.rx<-.55f?1.f:0.f;
    case TI::RightRight:return !vertical&&in.rx>.55f?1.f:0.f;
    case TI::None:return 0;
    }
    return 0;
}
inline const char* TouchLabel(TouchInput key) {
    constexpr const char* labels[]{"A","B","X","Y","left trigger","right trigger","left grip","right grip",
        "left stick click","right stick click","left Menu","left stick up","left stick down","left stick left","left stick right",
        "right stick up","right stick down","right stick left","right stick right","unbound"};
    return labels[static_cast<unsigned>(key)];
}
inline std::string ActionLabel(NativeAction action,const ControlBindings& bindings=ActiveBindings()) {
    const auto& binding=bindings.actions[action];
    return std::string(binding.command?"command + ":"")+TouchLabel(binding.input);
}
inline std::string CommandChordLabel(const ControlBindings& bindings=ActiveBindings()) {
    return std::string(TouchLabel(bindings.commandGrip))+" + "+TouchLabel(bindings.commandClick);
}
inline bool EarthshakerHeld(const TouchControls& in,const ControlBindings& bindings=ActiveBindings()) {
    return TouchValue(in,bindings.earthshakerLeft)>.65f&&TouchValue(in,bindings.earthshakerRight)>.65f;
}
inline std::string EarthshakerChordLabel(const ControlBindings& bindings=ActiveBindings()) {
    return std::string(TouchLabel(bindings.earthshakerLeft))+" + "+TouchLabel(bindings.earthshakerRight)+" together";
}
inline bool CommandHeld(const TouchControls& in,const ControlBindings& bindings=ActiveBindings()) {
    return TouchValue(in,bindings.commandClick)>.5f&&TouchValue(in,bindings.commandGrip)>.65f;
}
inline bool BuildInputHeldByChord(TouchInput input,const ControlBindings& bindings) {
    const auto& build=bindings.actions[BuildMenu];
    return input!=TI::None&&(input==build.input||
        (build.command&&(input==bindings.commandGrip||input==bindings.commandClick)));
}
inline bool RecenterHeld(const TouchControls& in,const ControlBindings& bindings=ActiveBindings()) {
    return TouchValue(in,bindings.recenterModifier)>.5f&&TouchValue(in,bindings.recenterClick)>.5f;
}
inline void StickValues(const TouchControls& in,TouchStick stick,float& x,float& y) {
    x=stick==TouchStick::Left?in.lx:in.rx;y=stick==TouchStick::Left?in.ly:in.ry;
}
inline const char* StickLabel(TouchStick stick) {return stick==TouchStick::Left?"left stick":"right stick";}
inline float InputStrength(const TouchControls& in,TouchInput key) {
    switch(key) {
    case TI::LeftUp:return (std::max)(0.f,in.ly);case TI::LeftDown:return (std::max)(0.f,-in.ly);
    case TI::LeftLeft:return (std::max)(0.f,-in.lx);case TI::LeftRight:return (std::max)(0.f,in.lx);
    case TI::RightUp:return (std::max)(0.f,in.ry);case TI::RightDown:return (std::max)(0.f,-in.ry);
    case TI::RightLeft:return (std::max)(0.f,-in.rx);case TI::RightRight:return (std::max)(0.f,in.rx);
    default:return TouchValue(in,key);
    }
}
}
