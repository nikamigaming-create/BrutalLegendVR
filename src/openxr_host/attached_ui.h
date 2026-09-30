#pragma once
#include "eddie_lobby.h"
#include "../bridge/blvr_ui_bridge.h"
#include <algorithm>
#include <array>
#include <vector>

namespace blvr_xr_host {
struct AttachedUiPanel {
    UiMount mount = AbovePalm;
    XrVector4f uv{0,0,1,1};
    float width = .66f, height = .37125f;
};

// Every gameplay pixel goes to an anatomical/weapon attachment. There is no
// head-locked or face-centered fallback. Transparent margins are cropped using
// alpha, never a color key. Source aspect is retained by each physical panel.
inline std::vector<AttachedUiPanel> layoutAttachedUi(const uint8_t* bgra,
    uint32_t width,uint32_t height,uint32_t flags) {
    std::vector<AttachedUiPanel> output;
    if(!bgra||!width||!height) return output;
    const auto append=[&](UiMount mount,unsigned left,unsigned right,float meters) {
        unsigned x0=right,y0=height,x1=left,y1=0;
        for(unsigned y=0;y<height;++y) for(unsigned x=left;x<right;++x)
            if(bgra[(size_t(y)*width+x)*4+3]>12) {
                x0=(std::min)(x0,x);x1=(std::max)(x1,x+1);
                y0=(std::min)(y0,y);y1=(std::max)(y1,y+1);
            }
        if(x0>=x1||y0>=y1) return;
        // Quantized margins avoid tiny glyph/antialiasing changes resizing a
        // screen. Solo interaction uses this same crop to map touches back.
        x0=(std::max)(left,(x0/32)*32);y0=(y0/32)*32;
        x1=(std::min)(right,((x1+31)/32)*32);y1=(std::min)(height,((y1+31)/32)*32);
        AttachedUiPanel panel;
        panel.mount=mount;panel.width=meters;
        panel.height=meters*float(y1-y0)/float(x1-x0);
        if(panel.height>.52f) {panel.width*=.52f/panel.height;panel.height=.52f;}
        panel.uv={float(x0)/width,float(y0)/height,float(x1)/width,float(y1)/height};
        output.push_back(panel);
    };
    if(flags&(blvr_ui_bridge::SoloRadial|blvr_ui_bridge::SoloNotes)) {
        append(GuitarHeadstock,0,width,.52f);
        if((flags&blvr_ui_bridge::SoloRadial)&&!output.empty()) {
            // The native direction needle extends outside its dial. Give the
            // dial/description a stable viewport so moving that needle cannot
            // resize the touch surface or fling content off the instrument.
            auto& panel=output[0];panel.uv={.23f,.10f,.77f,.83f};
            panel.width=.62f;panel.height=.62f*(.73f*height)/(.54f*width);
        } else if((flags&blvr_ui_bridge::SoloNotes)&&!output.empty()) {
            // The native solo adds a translucent full-screen vignette. Its
            // alpha is not a content bound. Keep the authored title/three-lane
            // note track readable on the headstock instead of shrinking it
            // inside the original desktop-sized dark surround.
            auto& panel=output[0];panel.uv={.24f,.07f,.77f,.34f};
            panel.width=.72f;panel.height=.72f*(.27f*height)/(.53f*width);
        }
    } else {
        unsigned center=0;
        for(unsigned y=height/8;y<height*7/8;y+=4)
            for(unsigned x=width/4;x<width*3/4;x+=4)
                if(bgra[(size_t(y)*width+x)*4+3]>32) ++center;
        // Native pause/tutorial cards occupy the central display. Sparse
        // gameplay HUDs are split into forearm stats and palm notifications.
        const bool popup=center>width*height/16/512;
        if(popup) append(AbovePalm,0,width,.70f);
        else {
            append(LeftForearm,0,width/4,.30f);
            append(AbovePalm,width/4,width*3/4,.60f);
            append(RightForearm,width*3/4,width,.30f);
        }
    }
    return output;
}
}
