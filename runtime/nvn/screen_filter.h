#include "../constants/nvn.h"
// Screen filter (Cosmetics tab "Screen filter"), live: applied in the present pass, after the upscaler, to the whole
// game image - the game's own HUD included (the original's look), our overlay menus not. The image is gamma-encoded
// there. Shared by the Metal (metal.m present_src) and Vulkan (vulkan/shaders.c present.frag) present shaders: `c` is
// the colour (float3 / vec3), pf.x the mode.
//   1 Game Boy: the original's four greens (#0F380F #306230 #8BAC0F #9BBC0F), chosen by luminance
//   2 Game Boy Color: more saturated and a little brighter, 6 levels a channel (its 15-bit colours, posterised)
//   3 Grayscale: luminance only, no tint, contrast raised a little (a gentle S-curve)
#pragma once
#include "../settings.h"

enum { SCREEN_FILTER_OFF, SCREEN_FILTER_GB, SCREEN_FILTER_GBC, SCREEN_FILTER_GRAY };

// pf for the present shader (x: the mode)
static inline void screen_filter_params(float pf[SF2C_SCREEN_FILTER_N_4_4B22]) {
    pf[0] = (float)g_settings.screen_filter;
    pf[1] = pf[SF2C_SCREEN_FILTER_N_2_D473] = pf[SF2C_SCREEN_FILTER_N_3_4E07] = 0;
}

#define SCREEN_FILTER_SRC(V3)                                                                                          \
    SF2C_SCREEN_FILTER_S_IF_PF_X_0_5_N_ECB2                                                                                            \
    SF2C_SCREEN_FILTER_S_FLOAT_L_DOT_C_A9DE V3 SF2C_SCREEN_FILTER_S_0_299_0_587_0_114_N_89B7                                                             \
    SF2C_SCREEN_FILTER_S_IF_PF_X_1_5_N_E8E0                                                                                          \
    SF2C_SCREEN_FILTER_S_FLOAT_Q_CLAMP_L_0_06_0_66_0__41AA                                                    \
    SF2C_SCREEN_FILTER_S_C_Q_1_0_7360 V3 SF2C_SCREEN_FILTER_S_0_059_0_220_0_059_Q_2_0_51BD V3 SF2C_SCREEN_FILTER_S_0_188_0_384_0_188_N_D2EB                      \
    SF2C_SCREEN_FILTER_S_Q_3_0_327F V3 SF2C_SCREEN_FILTER_S_0_545_0_675_0_059_87C3 V3 SF2C_SCREEN_FILTER_S_0_608_0_737_0_059_N_7FF6                                 \
    SF2C_SCREEN_FILTER_S_ELSE_IF_PF_X_2_5_N_3249                                                                                   \
    SF2C_SCREEN_FILTER_S_C_CLAMP_MIX_5A67 V3 SF2C_SCREEN_FILTER_S_L_C_1_4_1_06_0_0_1_0_N_4EE8                                                      \
    SF2C_SCREEN_FILTER_S_C_FLOOR_C_5_0_0_5_5_0_N_CAEA                                                                          \
    SF2C_SCREEN_FILTER_S_ELSE_N_41A2                                                                                                   \
    SF2C_SCREEN_FILTER_S_L_CLAMP_L_0_5_1_2_0_5_0_0_1__BF23                                                              \
    SF2C_SCREEN_FILTER_S_C_C167 V3 SF2C_SCREEN_FILTER_S_L_L_3_0_2_0_L_0_35_L_0_65_N_4DA5                                                   \
    SF2C_SCREEN_FILTER_S_N_3BF2                                                                                                          \
    SF2C_SCREEN_FILTER_S_N_583B
