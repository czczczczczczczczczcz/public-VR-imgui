#pragma once
#include <cstdint>
#include "imgui.h"

namespace cz {

struct framebuffer {
    int w = 0, h = 0;
    uint8_t* px = nullptr;
};

struct fontatlas {
    int w = 0, h = 0;
    const uint8_t* px = nullptr;
};

struct texentry {
    ImTextureID id;
    int w, h;
    const uint8_t* px;
};

void rasterizedrawdata(ImDrawData* dd, framebuffer& fb, const fontatlas& atlas,
                       const texentry* extra = nullptr, int nextra = 0);

void blitscaled(framebuffer& fb, int dx, int dy, int dw, int dh,
                const uint8_t* src, int sw, int sh);

}
