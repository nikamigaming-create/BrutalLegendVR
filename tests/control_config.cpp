#include "../src/input/control_config.h"
#include "../src/input/pose_controls.h"
#include "../src/input/vr_control_hints.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <windows.h>

static void Check(bool ok,const char* why) {if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
int main() {
    using namespace BLVR;
    const auto path=std::filesystem::temp_directory_path()/("blvr-controls-test-"+std::to_string(GetCurrentProcessId())+".ini");
    for(unsigned action=0;action<NativeActionCount;++action) {
        std::ofstream file(path);
        file<<"[actions]\n";
        for(unsigned i=0;i<NativeActionCount;++i)file<<NativeActionNames[i]<<"="<<(i==action?"left_trigger":"none")<<"\n";
        file.close();
        ControlBindings b{};std::string error;
        Check(LoadControlBindings(path,b,error),"load all 51 overridden action names");
        TouchControls touch{};touch.lt=1;touch.rigMetadata=true;touch.weapon=action==Guitar?2:1;
        Check(MapTouch(touch,b).buttons[action]==255,"every native action responds to its configured input");
        Check(ActionLabel(static_cast<NativeAction>(action),b)=="left trigger","every action label matches remapped input");
    }
    {
        std::ofstream file(path);file<<"[controls]\ncommand_grip=right_grip\ncommand_click=right_stick_click\n"
            "earthshaker_left=x\nearthshaker_right=y\nequip_axe=a\nequip_guitar=b\n"
            "movement_stick=right\nturn_stick=left\nradial_stick=left\nopening_menu_stick=left\n"
            "recenter_modifier=x\nrecenter_click=b\nwheel_grip=a\nsupport_left=left_trigger\nsupport_right=right_trigger\n"
            "solo_fret=right_grip\nsolo_accept_alternate=none\nopening_confirm_alternate=x\n"
            "[actions]\nAxe=left_trigger\nGuitar=left_trigger\nAccept=x\nOrderCharge=command+a\n";
    }
    ControlBindings b{};std::string error;Check(LoadControlBindings(path,b,error),"load custom VR controls and command scope");
    TouchControls touch{};touch.rg=1;touch.rightClick=true;touch.a=true;
    Check(CommandHeld(touch,b)&&MapTouch(touch,b).buttons[OrderCharge]&&!MapTouch(touch,b).buttons[Axe],"custom command chord reaches order and suppresses combat");
    Check(VrPromptText("/OrderCharge/",b)=="right grip + right stick click + A","native prompt follows custom command chord");
    touch={};touch.x=touch.y=true;touch.rigMetadata=true;touch.weapon=2;
    const auto combo=MapTouch(touch,b);Check(combo.buttons[Axe]&&combo.buttons[Guitar],"remapped Earthshaker produces both native attacks");
    Check(VrPromptText("/South/ and /West/",b)=="X + Y together","Earthshaker prompt follows remapped chord");
    touch={};touch.x=touch.b=true;Check(RecenterHeld(touch,b)&&!MapTouch(touch,b).buttons[Accept],"custom recenter suppresses game actions");
    touch={};touch.rx=.75f;touch.ry=-.5f;float x=0,y=0;StickValues(touch,b.movementStick,x,y);
    Check(x==.75f&&y==-.5f,"movement stick can swap sides");
    const auto packed=PackRadialAxes(-.45f,.84f);UnpackRadialAxes(packed,x,y);
    Check(std::fabs(x+.45f)<.0001f&&std::fabs(y-.84f)<.0001f,"synthetic solo selection keeps physical stick channels intact");
    const auto before=b;
    {std::ofstream file(path);file<<"[actions]\nAxe=typo\n";}
    Check(!LoadControlBindings(path,b,error)&&b.actions[Axe].input==before.actions[Axe].input,"invalid input rejects entire file without partial application");
    {std::ofstream file(path);file<<"[actions]\nTypoAction=a\n";}
    Check(!LoadControlBindings(path,b,error),"unknown action is rejected");
    SetEnvironmentVariableW(L"BLVR_CONTROLS_FILE",path.c_str());
    {std::ofstream file(path);file<<"[actions]\nAxe=x\n";}
    const auto live1=ActiveBindings();Check(live1.actions[Axe].input==TI::X,"initial live controls load");
    Sleep(300);
    {std::ofstream file(path);file<<"[actions]\nAxe=left_trigger\n";}
    const auto live2=ActiveBindings();Check(live2.actions[Axe].input==TI::LeftTrigger&&ControlsSignature(live1)!=ControlsSignature(live2),"saved controls reload during play");
    Sleep(300);
    {std::ofstream file(path);file<<"[actions]\nAxe=typo\n";}
    Check(ControlsSignature(ActiveBindings())==ControlsSignature(live2),"invalid live edit retains last working layout");
    SetEnvironmentVariableW(L"BLVR_CONTROLS_FILE",nullptr);
    std::filesystem::remove(path);
    std::puts("PASS: all 51 remappable actions, shared hint labels, custom command/Earthshaker/recenter, swapped sticks, scoped input and invalid file rejection");
}
