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
inline std::string BuildModalActionLabel(NativeAction action,TouchInput alternate,const ControlBindings& bindings) {
    const auto& binding=bindings.actions[action];
    const auto& build=bindings.actions[BuildMenu];
    std::string label;
    if(binding.input!=TI::None&&!BuildInputHeldByChord(binding.input,bindings)&&(!binding.command||build.command))
        label=TouchLabel(binding.input);
    if(alternate!=TI::None&&!BuildInputHeldByChord(alternate,bindings)&&alternate!=binding.input) {
        if(!label.empty())label+=" or ";
        label+=TouchLabel(alternate);
    } else if(label.empty()&&alternate!=TI::None&&!BuildInputHeldByChord(alternate,bindings))label=TouchLabel(alternate);
    return label.empty()?TouchLabel(TI::None):label;
}
namespace VrHintDetail {
inline bool ReadWeapon(const std::string& text,size_t at,NativeAction& action,size_t& end) {
    const std::pair<const char*,NativeAction> tokens[]{
        {"/AxeAttack/",Axe},{"/GuitarAttack/",Guitar},{"/South/",Axe},{"/West/",Guitar},
        {"/kBI_PrimaryMeleeAttack/",Axe},{"/kBI_SecondaryMeleeAttack/",Guitar}
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
inline std::string VrPromptText(std::string text,const ControlBindings& bindings=ActiveBindings(),
                                bool buildContext=false) {
    if(text.find('/')==std::string::npos)return text;
    // Owned TOSK097 localizes as Accept; the live Flash formatter receives
    // RadialAccept. The wheel reuses this caption for units and stage tiers,
    // both action25. Name both purposes only in these exact full templates.
    // Use16 remains a separate research input.
    if(buildContext&&(text=="/Accept/ TO RECRUIT"||text=="/RadialAccept/ TO RECRUIT"))
        text+=" / UPGRADE";
    const auto replace=[&](const std::string& from,const std::string& to) {
        size_t at=0;while((at=text.find(from,at))!=std::string::npos) {text.replace(at,from.size(),to);at+=to.size();}
    };
    // Owned TETL003 already names the movement input after Dodge. Its exact
    // combo needs one stick label before the standalone Dodge alias expands.
    replace("/Dodge/ and /MovementControls/",PromptActionLabel(Evade,bindings)+
        " + "+StickLabel(bindings.movementStick));
    const std::string axe=TouchLabel(bindings.equipAxe),guitar=TouchLabel(bindings.equipGuitar);
    if(buildContext) {
        const std::string recruit=BuildModalActionLabel(RadialAccept,bindings.soloAcceptAlternate,bindings);
        const std::string research=BuildModalActionLabel(Use,bindings.buildResearchAlternate,bindings);
        const std::string cancel=BuildModalActionLabel(CancelBuildItem,TI::None,bindings);
        // Native TOSK097/TOSK098 use Accept for recruitment/stage upgrade.
        // Keep that confirmation separate from the native Use research path.
        for(const auto* alias:{"/kBI_RadialAccept/","/RadialAccept/","/kBI_Accept/","/Accept/","/South/"})replace(alias,recruit);
        for(const auto* alias:{"/kBI_Use/","/Use/","/Interact/","/Activate/","/Coop/","/North/"})replace(alias,research);
        for(const auto* alias:{"/kBI_CancelBuildItem/","/CancelBuildItem/"})replace(alias,cancel);
        for(const auto* alias:{"/LeftStick/","/BUTTON_StickLeft/"})replace(alias,StickLabel(bindings.radialStick));
    }
    // FrontEnd's native HintBar uses enum spellings rather than the friendly
    // gameplay aliases below. Map the logical action before the retail
    // formatter chooses its keyboard/gamepad glyph; never substitute a
    // physical Xbox letter for a differently configured VR input.
    const std::pair<const char*,NativeAction> nativeButtons[]{
        {"UI_A",UiA},{"UI_B",UiB},{"UI_X",UiX},{"UI_Y",UiY},{"UI_Start",UiStart},{"UI_Back",UiBack},
        {"UI_DPadUp",UiUp},{"UI_DPadDown",UiDown},{"UI_DPadLeft",UiLeft},{"UI_DPadRight",UiRight},
        {"UI_TriggerLeft",UiTriggerLeft},{"UI_TriggerRight",UiTriggerRight},
        {"UI_ShoulderLeft",UiShoulderLeft},{"UI_ShoulderRight",UiShoulderRight},
        {"Accept",Accept},{"Cancel",Cancel},{"Use",Use},{"Map",Map},{"Journal",Journal},
        {"PrimaryMeleeAttack",Axe},{"SecondaryMeleeAttack",Guitar},{"Evade",Evade},{"Block",Block},
        {"ZTarget",Target},{"RockStance",RockStance},{"RadialAccept",RadialAccept},
        {"SoloNote1",SoloNote1},{"SoloNote2",SoloNote2},{"SoloNote3",SoloNote3},
        {"Beacon",Beacon},{"OrderCharge",OrderCharge},{"OrderDefend",OrderDefend},{"OrderMove",OrderMove},
        {"OrderFollow",OrderFollow},{"BuildMenu",BuildMenu},{"CancelBuildItem",CancelBuildItem},
        {"Fly",Fly},{"Descend",Descend},{"Ascend",Ascend},{"Handbrake",Handbrake},{"Boost",Boost},
        {"AlternateCam",AlternateCam},{"PrimaryVehicleAttack",PrimaryVehicleAttack},
        {"SecondaryVehicleAttack",SecondaryVehicleAttack},{"LeftCoopAttack",LeftCoopAttack},
        {"RightCoopAttack",RightCoopAttack},{"PlaylistUI",PlaylistUI},{"PlaylistToggle",PlaylistToggle},
        {"PlaylistNext",PlaylistNext},{"PlaylistPrev",PlaylistPrev},{"PlaylistRewind",PlaylistRewind}
    };
    static_assert(sizeof(nativeButtons)/sizeof(nativeButtons[0])==NativeActionCount);
    replace("/kSI_Move/",StickLabel(bindings.movementStick));
    replace("/kSI_Look/",std::string("head movement and ")+StickLabel(bindings.turnStick)+" snap turn");
    replace("/kSI_SwitchTarget/",std::string(StickLabel(bindings.radialStick))+" while targeting");
    replace("/kSI_RadialMenu/",StickLabel(bindings.radialStick));
    replace("/kAI_Gas/",PromptActionLabel(PrimaryVehicleAttack,bindings));
    replace("/kAI_Brake/",PromptActionLabel(SecondaryVehicleAttack,bindings));
    // Handle whole combos before their component aliases. Both weapons use
    // the trigger, so replacing each icon alone loses the required selection.
    VrHintDetail::WeaponSequences(text,bindings);
    for(const auto& alias:nativeButtons)replace(std::string("/kBI_")+alias.first+"/",PromptActionLabel(alias.second,bindings));
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
    const std::string steering=std::string(TouchLabel(bindings.wheelGrip))+" + hand turn, or "+StickLabel(bindings.movementStick);
    replace("/Steer/",steering);
    replace("/DrivingControls/",steering);
    for(unsigned n=0;n<3;++n)replace("/SoloNote"+std::to_string(n+1)+"/",
        "right-hand strum (or "+PromptActionLabel(static_cast<NativeAction>(SoloNote1+n),bindings)+")");
    replace("/MovementControls/",StickLabel(bindings.movementStick));
    replace("/CameraControls/",std::string("head movement and ")+StickLabel(bindings.turnStick)+" snap turn");
    replace("/RadialSelect/",StickLabel(bindings.radialStick));
    replace("/TargetSwitch/",std::string(StickLabel(bindings.radialStick))+" while targeting");
    const std::pair<const char*,const char*> fixed[]{
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
