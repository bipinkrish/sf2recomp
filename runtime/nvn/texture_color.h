#pragma once
#include "nvn.h"

// Dominant opaque artwork color in a sprite's atlas rectangle. Unsupported
// formats or empty rectangles return false, leaving the caller's tint intact.
bool nvn_texture_region_color(const NvnTexture *t, uint32_t x, uint32_t y,
                              uint32_t w, uint32_t h, float rgb[3]);
