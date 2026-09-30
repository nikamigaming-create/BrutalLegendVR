#include "control_config.h"
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <mutex>

namespace BLVR {
const char* const NativeActionNames[NativeActionCount]={
    "UiA","UiB","UiX","UiY","UiStart","UiBack","UiUp","UiDown","UiLeft","UiRight",
    "UiTriggerLeft","UiTriggerRight","UiShoulderLeft","UiShoulderRight","Accept","Cancel",
    "Use","Map","Journal","Axe","Guitar","Evade","Block","Target","RockStance","RadialAccept",
    "SoloNote1","SoloNote2","SoloNote3","Beacon","OrderCharge","OrderDefend","OrderMove",
    "OrderFollow","BuildMenu","CancelBuildItem","Fly","Descend","Ascend","Handbrake","Boost",
    "AlternateCam","PrimaryVehicleAttack","SecondaryVehicleAttack","LeftCoopAttack",
    "RightCoopAttack","PlaylistUI","PlaylistToggle","PlaylistNext","PlaylistPrev","PlaylistRewind"};
const char* const TouchInputNames[20]={
    "a","b","x","y","left_trigger","right_trigger","left_grip","right_grip",
    "left_stick_click","right_stick_click","menu","left_up","left_down","left_left","left_right",
    "right_up","right_down","right_left","right_right","none"};
namespace {
std::string Clean(std::string text) {
    const auto begin=text.find_first_not_of(" \t\r\n");
    if(begin==std::string::npos)return {};
    text=text.substr(begin,text.find_last_not_of(" \t\r\n")-begin+1);
    std::transform(text.begin(),text.end(),text.begin(),[](unsigned char c){return char(std::tolower(c));});
    return text;
}
bool Input(const std::string& value,TouchInput& out) {
    for(unsigned i=0;i<20;++i)if(value==TouchInputNames[i]){out=static_cast<TouchInput>(i);return true;}
    return false;
}
bool Extra(ControlBindings& b,const std::string& name,const std::string& value) {
    struct Key {const char* name;TouchInput ControlBindings::*member;};
    const Key keys[]={
        {"command_grip",&ControlBindings::commandGrip},{"command_click",&ControlBindings::commandClick},
        {"equip_axe",&ControlBindings::equipAxe},{"equip_guitar",&ControlBindings::equipGuitar},
        {"earthshaker_left",&ControlBindings::earthshakerLeft},{"earthshaker_right",&ControlBindings::earthshakerRight},
        {"wheel_grip",&ControlBindings::wheelGrip},{"command_boost",&ControlBindings::commandBoost},
        {"recenter_modifier",&ControlBindings::recenterModifier},{"recenter_click",&ControlBindings::recenterClick},
        {"support_left",&ControlBindings::supportLeft},{"support_right",&ControlBindings::supportRight},
        {"solo_fret",&ControlBindings::soloFret},{"solo_accept_alternate",&ControlBindings::soloAcceptAlternate},
        {"opening_confirm_alternate",&ControlBindings::openingConfirmAlternate}};
    for(const auto& key:keys)if(name==key.name)return Input(value,b.*key.member);
    struct StickKey {const char* name;TouchStick ControlBindings::*member;};
    const StickKey sticks[]={{"movement_stick",&ControlBindings::movementStick},{"turn_stick",&ControlBindings::turnStick},
        {"radial_stick",&ControlBindings::radialStick},{"opening_menu_stick",&ControlBindings::openingMenuStick}};
    for(const auto& key:sticks)if(name==key.name) {
        if(value!="left"&&value!="right")return false;
        b.*key.member=value=="left"?TouchStick::Left:TouchStick::Right;return true;
    }
    return false;
}
}
bool LoadControlBindings(const std::filesystem::path& path,ControlBindings& out,std::string& error) {
    std::ifstream file(path);
    if(!file){error="Cannot read controls file: "+path.u8string();return false;}
    ControlBindings next=DefaultBindings;
    std::string text,section;unsigned line=0;
    while(std::getline(file,text)) {
        ++line;
        if(line==1&&text.compare(0,3,"\xef\xbb\xbf")==0)text.erase(0,3);
        text=Clean(text.substr(0,text.find_first_of(";#")));
        if(text.empty())continue;
        if(text.front()=='['&&text.back()==']') {
            section=Clean(text.substr(1,text.size()-2));
            if(section=="actions"||section=="controls")continue;
        } else {
            const auto split=text.find('=');
            if(split!=std::string::npos) {
                const auto key=Clean(text.substr(0,split)),value=Clean(text.substr(split+1));
                if(section=="controls"&&Extra(next,key,value))continue;
                if(section=="actions") {
                    bool found=false;
                    for(unsigned i=0;i<NativeActionCount;++i)if(key==Clean(NativeActionNames[i])) {
                        std::string input=value;bool command=false;
                        if(input.compare(0,8,"command+")==0){command=true;input=Clean(input.substr(8));}
                        TouchInput parsed{};
                        if(Input(input,parsed)){next.actions[i]={parsed,command};found=true;}
                        break;
                    }
                    if(found)continue;
                }
            }
        }
        error="Invalid controls entry at line "+std::to_string(line)+": "+text;
        return false;
    }
    if(!file.eof()){error="Failed reading controls file";return false;}
    out=next;error.clear();return true;
}
std::filesystem::path ControlsFilePath() {
    wchar_t path[32768]{};
    const DWORD length=GetEnvironmentVariableW(L"BLVR_CONTROLS_FILE",path,32768);
    if(length&&length<32768)return std::filesystem::path(path);
    GetModuleFileNameW(nullptr,path,32768);
    auto root=std::filesystem::path(path).parent_path();
    if(root.filename()==L"tools")root=root.parent_path();
    return root/"controls.ini";
}
ControlBindings ActiveBindings() {
    static std::mutex mutex;
    static ControlBindings current=DefaultBindings;
    static std::filesystem::file_time_type written{};
    static std::filesystem::path loaded;
    static uint64_t checked=0;
    std::lock_guard<std::mutex> lock(mutex);
    const uint64_t now=GetTickCount64();
    if(checked&&now-checked<250)return current;
    checked=now;
    const auto path=ControlsFilePath();std::error_code ec;
    const auto stamp=std::filesystem::last_write_time(path,ec);
    if(!ec&&(path!=loaded||stamp!=written)) {
        ControlBindings next;std::string error;
        if(LoadControlBindings(path,next,error)) {current=next;loaded=path;written=stamp;}
        else OutputDebugStringA(("BLVR controls: retaining previous layout: "+error+"\n").c_str());
    }
    return current;
}
uint64_t ControlsSignature(const ControlBindings& b) {
    uint64_t hash=14695981039346656037ull;
    const auto add=[&](unsigned value){hash^=value;hash*=1099511628211ull;};
    for(const auto& a:b.actions){add(unsigned(a.input));add(a.command?1u:0u);}
    for(auto key:{b.commandGrip,b.commandClick,b.equipAxe,b.equipGuitar,b.earthshakerLeft,b.earthshakerRight,
        b.wheelGrip,b.commandBoost,b.recenterModifier,b.recenterClick,b.supportLeft,b.supportRight,b.soloFret,
        b.soloAcceptAlternate,b.openingConfirmAlternate})add(unsigned(key));
    for(auto stick:{b.movementStick,b.turnStick,b.radialStick,b.openingMenuStick})add(unsigned(stick));
    return hash;
}
}
