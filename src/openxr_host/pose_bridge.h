#pragma once

#include "../bridge/blvr_xr_pose_bridge.h"
#include "../bridge/blvr_rig_bridge.h"

#include <functional>
#include <string>

namespace blvr_xr_host
{
class PoseBridgePublisher
{
public:
    using LogFunction = std::function<void(const std::string&)>;

    PoseBridgePublisher();
    ~PoseBridgePublisher();
    PoseBridgePublisher(const PoseBridgePublisher&) = delete;
    PoseBridgePublisher& operator=(const PoseBridgePublisher&) = delete;

    bool initialize(LogFunction logger);
    void publish(blvr_xr_bridge::PoseBridge value,const blvr_xr_bridge::RigFrame* rig=nullptr);
    uint64_t epoch() const;
    void reset();

private:
    struct Implementation;
    Implementation* implementation_ = nullptr;
};
}
