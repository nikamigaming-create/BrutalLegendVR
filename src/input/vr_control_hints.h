#pragma once
#include "control_bindings.h"
#include <string>
#include <utility>
#include <vector>
namespace BLVR {
inline std::string PromptActionLabel(NativeAction action,const ControlBindings& bindings) {
    const auto& binding=bindings.actions[action];
    return (binding.command?CommandChordLabel(bindings)+" + ":std::string{})+TouchLabel(binding.input);
}
namespace VrHintDetail {
inline bool ReadWeapon(const std::string& text,size_t at,NativeAction& action,size_t& end) {
    const std::pair<const char*,NativeAction> tokens[]{
        {"/AxeAttack/",Axe},{"/GuitarAttack/",Guitar},{"/South/",Axe},{"/West/",Guitar}
    };
    for(const auto& token:tokens) {
        const size_t length=std::char_traits<char>::length(token.first);
        if(text.compare(at,length,token.first)==0) {action=token.second;end=at+length;return true;}
    }
    return false;
}
inline void WeaponSequences(std::string& text,const ControlBindings& bindings) {
    size_t at=0;
    while((at=text.find('/',at))!=std::string::npos) {
        NativeAction action{};size_t end=at;
        if(!ReadWeapon(text,at,action,end)) {++at;continue;}
        std::vector<NativeAction> actions{action};
        bool chordSeparator=false,adjacent=false;
        for(;;) {
            size_t next=end;
            while(next<text.size()&&(text[next]==' '||text[next]=='\t'||text[next]==','||text[next]=='+')) {
                chordSeparator|=text[next]=='+';++next;
            }
            if(text.compare(next,4,"and ")==0) {chordSeparator=true;next+=4;}
            size_t nextEnd=next;
            if(!ReadWeapon(text,next,action,nextEnd))break;
            adjacent|=next==end;actions.push_back(action);end=nextEnd;
        }
        if(actions.size()<2) {at=end;continue;}
        std::string replacement;
        if(actions.size()==2&&actions[0]!=actions[1]&&(chordSeparator||adjacent)) {
            replacement=EarthshakerChordLabel(bindings);
        } else {
            for(size_t i=0;i<actions.size();) {
                const NativeAction step=actions[i];
                size_t endRun=i+1;
                while(endRun<actions.size()&&actions[endRun]==step)++endRun;
                if(!replacement.empty())replacement+="; ";
                replacement+=std::string(step==Axe?"axe (":"guitar (")+
                    TouchLabel(step==Axe?bindings.equipAxe:bindings.equipGuitar)+
                    "): "+PromptActionLabel(step,bindings);
                if(endRun-i>1)replacement+=" x"+std::to_string(endRun-i);
                i=endRun;
            }
        }
        text.replace(at,end-at,replacement);at+=replacement.size();
    }
}
}
inline std::string VrPromptText(std::string text,const ControlBindings& bindings=ActiveBindings()) {
    if(text.find('/')==std::string::npos)return text;
    const auto replace=[&](const std::string& from,const std::string& to) {
        size_t at=0;while((at=text.find(from,at))!=std::string::npos) {text.replace(at,from.size(),to);at+=to.size();}
    };
    const std::string axe=TouchLabel(bindings.equipAxe),guitar=TouchLabel(bindings.equipGuitar);
    // Handle whole combos before their component aliases. Both weapons use
    // the trigger, so replacing each icon alone loses the required selection.
    VrHintDetail::WeaponSequences(text,bindings);
    replace("Hold /RockStance/ and select ","Press "+PromptActionLabel(RockStance,bindings)+" and select ");
    replace("Hold /SummonGuitar/ to play guitar.","Press "+guitar+
        " to equip the guitar; strum with your right hand or use "+PromptActionLabel(Guitar,bindings)+".");
    // Legacy face-button aliases are contextual. Menu South means accept;
    // the guitar tutorial's West still means a guitar attack.
    if(text.find("Shocker:")!=std::string::npos||text.find("Pyro:")!=std::string::npos||
       text.find(" for fog")!=std::string::npos)replace("/West/","/GuitarAttack/");
    if(text.find(" to heal")!=std::string::npos)replace("/South/","/Attack/");
    if(text.find("to fire secondary weapons")!=std::string::npos)
        replace("/GuitarAttack/","/SecondaryVehicleAttack/");
    replace("/AxeAttack/",PromptActionLabel(Axe,bindings)+" (axe: "+axe+")");
    replace("/GuitarAttack/",PromptActionLabel(Guitar,bindings)+" (guitar: "+guitar+")");
    const std::pair<const char*,NativeAction> aliases[]{
        {"Accept",Accept},{"Cancel",Cancel},{"Interact",Use},{"Activate",Use},{"Use",Use},{"Coop",Use},
        {"Attack",Axe},{"Axe",Axe},{"AxeAttack",Axe},{"SecondaryAttack",Guitar},{"Guitar",Guitar},{"GuitarAttack",Guitar},
        {"East",Block},{"North",Use},{"Block",Block},{"Evade",Evade},{"Target",Target},{"TargetLock",Target},
        {"RockStance",RockStance},{"RadialAccept",RadialAccept},
        {"DriveForward",PrimaryVehicleAttack},{"Accelerate",PrimaryVehicleAttack},{"Brake",SecondaryVehicleAttack},
        {"EBrake",Handbrake},{"Handbrake",Handbrake},{"Boost",Boost},{"Run",Boost},
        {"Map",Map},{"Journal",Journal},{"Pause",Journal},
        {"Beacon",Beacon},{"Charge",OrderCharge},{"OrderCharge",OrderCharge},{"Defend",OrderDefend},{"OrderDefend",OrderDefend},
        {"Move",OrderMove},{"OrderMove",OrderMove},{"Follow",OrderFollow},{"OrderFollow",OrderFollow},
        {"Recruit",BuildMenu},{"BuildMenu",BuildMenu},{"CancelBuildItem",CancelBuildItem},{"Flight",Fly},{"Fly",Fly},
        {"Ascend",Ascend},{"Descend",Descend},{"VehicleAttack",PrimaryVehicleAttack},{"PrimaryVehicleAttack",PrimaryVehicleAttack},
        {"VehicleAttackAlt",SecondaryVehicleAttack},{"SecondaryVehicleAttack",SecondaryVehicleAttack},
        {"LeftCoopAttack",LeftCoopAttack},{"RightCoopAttack",RightCoopAttack},{"AlternateCam",AlternateCam},
        {"PlaylistUI",PlaylistUI},{"PlaylistToggle",PlaylistToggle},{"PlaylistNext",PlaylistNext},
        {"PlaylistPrev",PlaylistPrev},{"PlaylistRewind",PlaylistRewind},
        {"UiA",UiA},{"UiB",UiB},{"UiX",UiX},{"UiY",UiY},{"UiStart",UiStart},{"UiBack",UiBack},
        {"UiUp",UiUp},{"UiDown",UiDown},{"UiLeft",UiLeft},{"UiRight",UiRight},
        {"UiTriggerLeft",UiTriggerLeft},{"UiTriggerRight",UiTriggerRight},
        {"UiShoulderLeft",UiShoulderLeft},{"UiShoulderRight",UiShoulderRight},
        {"BUTTON_DPadUp",OrderCharge},{"BUTTON_DPadDown",OrderDefend},
        {"BUTTON_DPadLeft",OrderFollow},{"BUTTON_DPadRight",OrderMove},
        {"BUTTON_ShoulderLeft",UiShoulderLeft},{"BUTTON_ShoulderRight",UiShoulderRight}
    };
    for(const auto& alias:aliases)replace(std::string("/")+alias.first+"/",PromptActionLabel(alias.second,bindings));
    replace("/South/",PromptActionLabel(Accept,bindings));
    replace("/West/",PromptActionLabel(CancelBuildItem,bindings));
    replace("/SummonGuitar/",guitar);
    replace("/Dodge/",PromptActionLabel(Evade,bindings)+" + "+StickLabel(bindings.movementStick));
    replace("/Steer/",std::string(TouchLabel(bindings.wheelGrip))+" + hand turn, or left stick");
    replace("/DrivingControls/",std::string(TouchLabel(bindings.wheelGrip))+" + hand turn, or left stick");
    for(unsigned n=0;n<3;++n)replace("/SoloNote"+std::to_string(n+1)+"/",
        "right-hand strum (or "+PromptActionLabel(static_cast<NativeAction>(SoloNote1+n),bindings)+")");
    const std::pair<const char*,const char*> fixed[]{
        {"MovementControls","left stick"},{"CameraControls","head movement and right-stick snap turn"},
        {"RadialSelect","right stick"},{"TargetSwitch","right stick while targeting"},
        {"LeftStick","left stick"},{"RightStick","right stick"},
        {"BUTTON_StickLeft","left stick"},{"BUTTON_StickRight","right stick"}
    };
    for(const auto& alias:fixed)replace(std::string("/")+alias.first+"/",alias.second);
    const std::pair<const char*,TouchInput> physical[]{
        {"LeftTrigger",TI::LeftTrigger},{"RightTrigger",TI::RightTrigger},
        {"BUTTON_TriggerLeft",TI::LeftTrigger},{"BUTTON_TriggerRight",TI::RightTrigger},
        {"BUTTON_ClickLeft",TI::LeftClick},{"BUTTON_ClickRight",TI::RightClick},
        {"BUTTON_A",TI::A},{"BUTTON_B",TI::B},{"BUTTON_X",TI::X},{"BUTTON_Y",TI::Y},
        {"BUTTON_Start",TI::Menu},{"BUTTON_Back",TI::Menu}
    };
    for(const auto& alias:physical)replace(std::string("/")+alias.first+"/",TouchLabel(alias.second));
    return text;
}
}
