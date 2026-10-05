#include "../src/input/control_config.h"
#include "../src/input/pose_controls.h"
#include "../src/input/vr_control_hints.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <windows.h>

static void Check(bool ok,const char* why) {if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
int main() {
    using namespace BLVR;
    Check(DefaultBindings.actions[UiX].input==TI::X&&DefaultBindings.actions[UiA].input==TI::A,
        "menu tutorial and select defaults are distinct inputs");
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
            "recenter_modifier=x\nrecenter_click=b\nwheel_grip=a\ncommand_boost=x\nsupport_left=left_trigger\nsupport_right=right_trigger\n"
            "solo_fret=right_grip\nsolo_accept_alternate=none\nbuild_research_alternate=y\nopening_confirm_alternate=x\n"
            "[actions]\nAxe=left_trigger\nGuitar=left_trigger\nAccept=x\nOrderCharge=command+a\n";
    }
    ControlBindings b{};std::string error;Check(LoadControlBindings(path,b,error),"load custom VR controls and command scope");
    Check(b.buildResearchAlternate==TI::Y,"load separately remapped build research alternate");
    auto signatureChange=b;signatureChange.buildResearchAlternate=TI::B;
    Check(ControlsSignature(signatureChange)!=ControlsSignature(b),"research alternate participates in the live layout signature");
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
    {
        auto scoped=DefaultBindings;scoped.actions[UiUp]={TI::A,true};
        TouchControls input{};input.a=true;
        Check(!ScopedInputStrength(input,UiUp,false,scoped)&&!MapTouch(input,scoped).buttons[UiUp],
            "command-bound menu direction cannot leak an analog axis without its chord");
        input.lg=1;input.leftClick=true;
        Check(ScopedInputStrength(input,UiUp,true,scoped)==1.f,"command-bound menu direction accepts its configured chord");
        input={};input.ly=.37f;
        Check(ScopedInputStrength(input,UiUp,false,DefaultBindings)==.37f,"ordinary menu navigation preserves partial stick strength");
        Check(!ScopedActionDown(input,UiUp,false,DefaultBindings),"partial analog menu strength does not become a digital press");
        input.lx=.9f;input.ly=.7f;
        Check(!ScopedActionDown(input,UiUp,false,DefaultBindings)&&ScopedActionDown(input,UiRight,false,DefaultBindings),
            "shared digital readers preserve the native dominant-axis direction");
        auto vehicle=DefaultBindings;
        vehicle.actions[PrimaryVehicleAttack]={TI::RightTrigger,true};
        vehicle.actions[SecondaryVehicleAttack]={TI::LeftTrigger,true};
        input={};input.rt=.75f;input.lt=.25f;
        Check(!MapTouch(input,vehicle).analog[0]&&!MapTouch(input,vehicle).analog[1],
            "vehicle analog inputs cannot bypass their required command scope");
        vehicle.actions[PrimaryVehicleAttack]={TI::LeftTrigger};vehicle.actions[SecondaryVehicleAttack]={TI::RightTrigger};
        const auto partialVehicle=MapTouch(input,vehicle);
        Check(partialVehicle.analog[0]==uint8_t(.25f*255.f)&&partialVehicle.analog[1]==uint8_t(.75f*255.f),
            "remapped vehicle analog inputs preserve partial strength without a digital threshold");
        vehicle.actions[PrimaryVehicleAttack].input=vehicle.actions[SecondaryVehicleAttack].input=TI::None;
        Check(!MapTouch(input,vehicle).analog[0]&&!MapTouch(input,vehicle).analog[1],
            "unbound vehicle analog inputs retain no hidden triggers");
        input={};input.lg=1;input.leftClick=input.a=true;
        Check(!MapTouch(input).build&&MapTouch(input).buttons[UiShoulderRight],
            "build binding does not steal native menu shoulder before native build feedback");
        input.buildRadial=true;
        auto build=MapTouch(input);
        Check(build.build&&build.buttons[BuildMenu]&&!build.buttons[RadialAccept]&&!build.buttons[Use]&&!build.buttons[UiA],
            "default held build input cannot create its own recruitment or research confirmation");
        input.rt=1;input.rx=.7f;input.ry=.9f;
        build=MapTouch(input);
        Check(build.buttons[RadialAccept]&&!build.buttons[Ascend]&&!build.buttons[OrderCharge]&&!build.buttons[PlaylistUI]&&
              !build.buttons[Axe]&&!build.buttons[Guitar]&&!build.buttons[Use],
            "alternate recruitment confirmation owns held build context without unrelated actions");
        input.rt=0;input.lt=1;build=MapTouch(input);
        Check(build.buttons[Use]&&!build.buttons[RadialAccept]&&!build.buttons[Descend]&&!build.buttons[Target],
            "default research alternate is separate from recruitment and ordinary targeting");
        input.buildRadial=false;build=MapTouch(input);
        Check(!build.buttons[Use]&&build.buttons[Descend],"research alternate does not activate outside verified native build context");
        for(const auto modifier:{TI::LeftGrip,TI::LeftClick}) {
            auto conflict=DefaultBindings;
            for(const auto action:{RadialAccept,Use,CancelBuildItem})conflict.actions[action]={modifier};
            conflict.soloAcceptAlternate=conflict.buildResearchAlternate=modifier;
            input={};input.lg=1;input.leftClick=input.a=true;
            build=MapTouch(input,conflict);
            Check(build.buttons[BuildMenu]&&!build.buttons[RadialAccept]&&!build.buttons[Use]&&!build.buttons[CancelBuildItem],
                "required command modifier is neutral before native build feedback");
            input.buildRadial=true;build=MapTouch(input,conflict);
            Check(build.buttons[BuildMenu]&&!build.buttons[RadialAccept]&&!build.buttons[Use]&&!build.buttons[CancelBuildItem],
                "native build feedback cannot turn an already held command modifier into an action");
            Check(VrPromptText("/RadialAccept/ /Use/ /CancelBuildItem/",conflict,true)=="unbound unbound unbound",
                "input and hints reject the same required command-modifier conflicts");
        }
        {
            auto normalBuild=DefaultBindings;normalBuild.actions[BuildMenu]={TI::A};
            normalBuild.actions[RadialAccept]={TI::LeftGrip};normalBuild.soloAcceptAlternate=TI::None;
            input={};input.a=true;input.lg=1;input.buildRadial=true;
            Check(MapTouch(input,normalBuild).buttons[RadialAccept]&&
                VrPromptText("/RadialAccept/",normalBuild,true)=="left grip",
                "build without command scope can use a modifier it does not require");
        }
        auto remapped=DefaultBindings;
        remapped.actions[BuildMenu]={TI::B,true};remapped.actions[RadialAccept]={TI::X};
        remapped.actions[Use]={TI::LeftTrigger};remapped.actions[CancelBuildItem]={TI::Y};remapped.soloAcceptAlternate=TI::None;
        remapped.buildResearchAlternate=TI::None;
        input={};input.buildRadial=true;input.lg=1;input.leftClick=input.b=true;input.x=true;
        build=MapTouch(input,remapped);
        Check(build.buttons[BuildMenu]&&build.buttons[RadialAccept]&&!build.buttons[Use]&&!build.buttons[Map],
            "remapped recruitment input works under held command without map or research");
        input.x=false;input.lt=1;build=MapTouch(input,remapped);
        Check(build.buttons[Use]&&!build.buttons[RadialAccept]&&!build.buttons[Descend]&&!build.buttons[Target],
            "separate remapped research input works under held build without recruitment or descent");
        input.lt=0;input.y=true;build=MapTouch(input,remapped);
        Check(build.buttons[CancelBuildItem]&&!build.buttons[Use]&&!build.buttons[Fly],
            "remapped queued cancellation works under held build without flight");
        remapped.buildResearchAlternate=TI::Y;input.y=true;build=MapTouch(input,remapped);
        Check(build.buttons[Use],"research alternate follows its independent remap");
        remapped.buildResearchAlternate=TI::B;input.y=false;build=MapTouch(input,remapped);
        Check(!build.buttons[Use],"research alternate cannot share the input that keeps build held");
        remapped.buildResearchAlternate=TI::None;
        remapped.actions[BuildMenu]={TI::B};remapped.actions[RadialAccept]={TI::X,true};
        remapped.actions[Use]={TI::LeftTrigger,true};remapped.actions[CancelBuildItem]={TI::Y,true};
        input.lg=0;input.leftClick=false;input.x=true;input.lt=1;
        build=MapTouch(input,remapped);
        Check(build.build&&!build.buttons[RadialAccept]&&!build.buttons[Use]&&!build.buttons[CancelBuildItem],
            "held build modal still honors explicitly required command scopes");
        input={};
        auto leftOrders=DefaultBindings;
        leftOrders.actions[OrderCharge]={TI::LeftUp,true};leftOrders.actions[OrderDefend]={TI::LeftDown,true};
        leftOrders.actions[OrderMove]={TI::LeftRight,true};leftOrders.actions[OrderFollow]={TI::LeftLeft,true};
        input.lg=1;input.leftClick=true;input.lx=.7f;input.ly=.9f;
        const auto order=MapTouch(input,leftOrders);
        Check(order.buttons[OrderCharge]&&!order.buttons[OrderDefend]&&!order.buttons[OrderMove]&&!order.buttons[OrderFollow],
            "remapped left-stick diagonal sends exactly one stage order");
        input={};
        input.lt=std::numeric_limits<float>::quiet_NaN();input.rt=std::numeric_limits<float>::infinity();
        input.lg=-2;input.rg=4;
        const auto sanitized=MapTouch(input,DefaultBindings);
        Check(!sanitized.analog[0]&&!sanitized.analog[1]&&TouchValue(input,TI::LeftGrip)==0&&TouchValue(input,TI::RightGrip)==1,
            "malformed analog values are neutral or clamped before native byte conversion");
        input={};input.lt=input.lg=input.rg=1;input.guitarFretting=true;
        const auto fretting=MapTouch(input,DefaultBindings);
        Check(!fretting.buttons[Target]&&!(fretting.buttons[Axe]&&fretting.buttons[Guitar]),
            "fretting trigger and picking grip cannot target or slam the held chest guitar");
        input.guitarFretting=false;
        const auto released=MapTouch(input,DefaultBindings);
        Check(released.buttons[Axe]&&released.buttons[Guitar],
            "ordinary two-grip Earthshaker remains available after releasing the neck");
    }
    Check(VrPromptText("/MovementControls/ | /CameraControls/ | /RadialSelect/ | /TargetSwitch/ | /Steer/",b)==
        "right stick | head movement and left stick snap turn | left stick | left stick while targeting | A + hand turn, or right stick",
        "semantic movement, camera, wheel and targeting hints follow swapped sticks");
    const auto before=b;
    {std::ofstream file(path);file<<"[actions]\nAxe=typo\n";}
    Check(!LoadControlBindings(path,b,error)&&b.actions[Axe].input==before.actions[Axe].input,"invalid input rejects entire file without partial application");
    {std::ofstream file(path);file<<"[actions]\nTypoAction=a\n";}
    Check(!LoadControlBindings(path,b,error),"unknown action is rejected");
    SetEnvironmentVariableW(L"BLVR_CONTROLS_FILE",path.c_str());
    {std::ofstream file(path);file<<"[actions]\nAxe=x\n";}
    Sleep(300); // Earlier default-mapping checks may have initialized the polling cache.
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
