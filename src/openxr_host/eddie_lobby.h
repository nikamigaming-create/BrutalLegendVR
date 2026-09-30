#pragma once
#include "../bridge/blvr_xr_pose_bridge.h"
#include "../bridge/blvr_rig_bridge.h"
#include <d3d11.h>
#include <openxr/openxr.h>
#include <filesystem>
#include <memory>
#include <string>

namespace blvr_xr_host {
enum UiMount : unsigned { LeftForearm, RightForearm, AbovePalm, GuitarHeadstock, UiMountCount };
struct UiMounts {
    XrPosef poses[UiMountCount]{};
    XrVector3f indexTips[2]{};
    uint32_t validMask = 0;
    uint32_t tipMask = 0;
};
// The opening space owns its geometry and renders it at the current XR pose.
// It is never composited over an older game image or a gameplay camera.
class EddieLobby {
public:
    EddieLobby();
    ~EddieLobby();
    bool initialize(ID3D11Device*, ID3D11DeviceContext*, const std::filesystem::path&, std::string&);
    bool ready() const;
    void update(const blvr_xr_bridge::PoseBridge&, float seconds, bool openingRoom = true, uint32_t nativeSolo = 0, bool driving = false);
    bool confirming() const;
    unsigned selectedWeapon() const; // 0 hands, 1 axe, 2 guitar
    unsigned physicalAction() const; // leased native action pulse
    bool exportRig(blvr_xr_bridge::RigFrame&) const;
    bool exportUiMounts(UiMounts&) const;
    bool readRenderedUi(uint64_t epoch,uint64_t sourceFrame,uint64_t poseFrame,
        int64_t displayTime,const blvr_xr_bridge::Pose& head,UiMounts&,bool& driving) const;
    void render(ID3D11RenderTargetView*, uint32_t width, uint32_t height,
                const XrView&, ID3D11ShaderResourceView* menu);
    static bool visualTest(const std::filesystem::path& assets,
                           const std::filesystem::path& output, std::string& failure);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
