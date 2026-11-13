#include "../../constants/nvn_vulkan.h"
// Vulkan textures: NVN formats, images, change-detected uploads from guest memory, views and samplers
// (counterpart of the texture half of metal.m and the view helpers of renderer.m).
#include <SDL3/SDL_timer.h>
#include <float.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../rqueue.h"
#include "vk.h"
#include "record.h"
#include "../astc.h"
#include "../tex_hash.h"
#include "../renderer.h"
#include "../../settings.h"

// ================================================================== formats
typedef struct { VkFormat vf; uint8_t expand_rgb, alpha_one; } VkFmt;

// DEPTH24_STENCIL8 as itself where the GPU has it for everything the game does with depth (NVIDIA, Intel): half the
// bytes of D32_SFLOAT_S8_UINT (4 instead of 8 per pixel) for every depth test, clear and copy - the game copies its
// full-size depth twice a frame. AMD has no D24S8 attachments: D32S8 there (and on Metal). SF2RECOMP_VK_D24=0: D32S8.
static VkFormat depth24_stencil8(void) {
    static VkFormat f;
    if (!f) {
        const char *e = getenv(SF2C_TEXTURES_S_SF2RECOMP_VK_D24_4C0A);
        VkFormatProperties p;
        vkGetPhysicalDeviceFormatProperties(g_vk.phys, VK_FORMAT_D24_UNORM_S8_UINT, &p);
        VkFormatFeatureFlags want = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                                    VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT |
                                    VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT;
        bool ok = (p.optimalTilingFeatures & want) == want && !(e && !atoi(e));
        f = ok ? VK_FORMAT_D24_UNORM_S8_UINT : VK_FORMAT_D32_SFLOAT_S8_UINT;
        LOG(SF2C_TEXTURES_S_VK_DEPTH24_STENCIL8_S_N_2B24, ok ? SF2C_TEXTURES_S_D24_UNORM_S8_UINT_7136 : SF2C_TEXTURES_S_D32_SFLOAT_S8_UINT_0C23);
    }
    return f;
}

static VkFmt fmt_of(uint32_t f) {
    switch (f) {
    case SF2C_TEXTURES_N_0X01_B51C: return (VkFmt){VK_FORMAT_R8_UNORM};
    case SF2C_TEXTURES_N_0X02_E6BD: return (VkFmt){VK_FORMAT_R8_SNORM};
    case SF2C_TEXTURES_N_0X03_9D1B: return (VkFmt){VK_FORMAT_R8_UINT};
    case SF2C_TEXTURES_N_0X04_B03A: return (VkFmt){VK_FORMAT_R8_SINT};
    case SF2C_TEXTURES_N_0X05_4ED3: return (VkFmt){VK_FORMAT_R16_SFLOAT};
    case SF2C_TEXTURES_N_0X06_9AE3: return (VkFmt){VK_FORMAT_R16_UNORM};
    case SF2C_TEXTURES_N_0X07_A022: return (VkFmt){VK_FORMAT_R16_SNORM};
    case SF2C_TEXTURES_N_0X08_1EC8: return (VkFmt){VK_FORMAT_R16_UINT};
    case SF2C_TEXTURES_N_0X09_59B7: return (VkFmt){VK_FORMAT_R16_SINT};
    case SF2C_TEXTURES_N_0X0A_AF2D: return (VkFmt){VK_FORMAT_R32_SFLOAT};
    case SF2C_TEXTURES_N_0X0B_C106: return (VkFmt){VK_FORMAT_R32_UINT};
    case SF2C_TEXTURES_N_0X0C_49AA: return (VkFmt){VK_FORMAT_R32_SINT};
    case SF2C_TEXTURES_N_0X0D_106A: return (VkFmt){VK_FORMAT_R8G8_UNORM};
    case SF2C_TEXTURES_N_0X0E_D851: return (VkFmt){VK_FORMAT_R8G8_SNORM};
    case SF2C_TEXTURES_N_0X0F_1F1D: return (VkFmt){VK_FORMAT_R8G8_UINT};
    case SF2C_TEXTURES_N_0X10_BF9C: return (VkFmt){VK_FORMAT_R8G8_SINT};
    case SF2C_TEXTURES_N_0X11_A3D6: return (VkFmt){VK_FORMAT_R16G16_SFLOAT};
    case SF2C_TEXTURES_N_0X12_9AE3: return (VkFmt){VK_FORMAT_R16G16_UNORM};
    case SF2C_TEXTURES_N_0X13_3ABB: return (VkFmt){VK_FORMAT_R16G16_SNORM};
    case SF2C_TEXTURES_N_0X14_F942: return (VkFmt){VK_FORMAT_R16G16_UINT};
    case SF2C_TEXTURES_N_0X15_5FB5: return (VkFmt){VK_FORMAT_R16G16_SINT};
    case SF2C_TEXTURES_N_0X16_D7D4: return (VkFmt){VK_FORMAT_R32G32_SFLOAT};
    case SF2C_TEXTURES_N_0X17_A99E: return (VkFmt){VK_FORMAT_R32G32_UINT};
    case SF2C_TEXTURES_N_0X18_D47C: return (VkFmt){VK_FORMAT_R32G32_SINT};
    case SF2C_TEXTURES_N_0X19_6E00: return (VkFmt){VK_FORMAT_R8G8B8A8_UNORM, 1, 1};  // RGB8: expanded on upload
    case SF2C_TEXTURES_N_0X25_2611: return (VkFmt){VK_FORMAT_R8G8B8A8_UNORM};
    case SF2C_TEXTURES_N_0X26_50D1: return (VkFmt){VK_FORMAT_R8G8B8A8_SNORM};
    case SF2C_TEXTURES_N_0X27_D7EC: return (VkFmt){VK_FORMAT_R8G8B8A8_UINT};
    case SF2C_TEXTURES_N_0X28_EAB9: return (VkFmt){VK_FORMAT_R8G8B8A8_SINT};
    case SF2C_TEXTURES_N_0X29_F941: return (VkFmt){VK_FORMAT_R16G16B16A16_SFLOAT};
    case SF2C_TEXTURES_N_0X2A_9EC1: return (VkFmt){VK_FORMAT_R16G16B16A16_UNORM};
    case SF2C_TEXTURES_N_0X2B_28A6: return (VkFmt){VK_FORMAT_R16G16B16A16_SNORM};
    case SF2C_TEXTURES_N_0X2C_8A63: return (VkFmt){VK_FORMAT_R16G16B16A16_UINT};
    case SF2C_TEXTURES_N_0X2D_A7B5: return (VkFmt){VK_FORMAT_R16G16B16A16_SINT};
    case SF2C_TEXTURES_N_0X2E_E0C5: return (VkFmt){VK_FORMAT_R32G32B32A32_SFLOAT};
    case SF2C_TEXTURES_N_0X2F_21A6: return (VkFmt){VK_FORMAT_R32G32B32A32_UINT};
    case SF2C_TEXTURES_N_0X30_8BAD: return (VkFmt){VK_FORMAT_R32G32B32A32_SINT};
    case SF2C_TEXTURES_N_0X31_9121: return (VkFmt){VK_FORMAT_S8_UINT};
    case SF2C_TEXTURES_N_0X32_8B2B: return (VkFmt){VK_FORMAT_D16_UNORM};
    case SF2C_TEXTURES_N_0X33_8FFB: return (VkFmt){VK_FORMAT_D32_SFLOAT};  // DEPTH24 (as on Metal: no 24-bit depth there)
    case SF2C_TEXTURES_N_0X34_806E: return (VkFmt){VK_FORMAT_D32_SFLOAT};
    case SF2C_TEXTURES_N_0X35_B555: return (VkFmt){depth24_stencil8()};
    case SF2C_TEXTURES_N_0X36_BBAE: return (VkFmt){VK_FORMAT_D32_SFLOAT_S8_UINT};
    case SF2C_TEXTURES_N_0X37_031A: return (VkFmt){VK_FORMAT_R8G8B8A8_SRGB, 0, 1};
    case SF2C_TEXTURES_N_0X38_AE4F: return (VkFmt){VK_FORMAT_R8G8B8A8_SRGB};
    case SF2C_TEXTURES_N_0X39_F666: return (VkFmt){VK_FORMAT_R4G4B4A4_UNORM_PACK16};   // Metal ABGR4Unorm
    case SF2C_TEXTURES_N_0X3A_096D: return (VkFmt){VK_FORMAT_R5G6B5_UNORM_PACK16};     // Metal B5G6R5Unorm
    case SF2C_TEXTURES_N_0X3B_F55A: return (VkFmt){VK_FORMAT_R5G5B5A1_UNORM_PACK16};   // Metal A1BGR5Unorm
    case SF2C_TEXTURES_N_0X3C_E42C: return (VkFmt){VK_FORMAT_R5G6B5_UNORM_PACK16};
    case SF2C_TEXTURES_N_0X3D_4B02: return (VkFmt){VK_FORMAT_A2B10G10R10_UNORM_PACK32};
    case SF2C_TEXTURES_N_0X3E_D948: return (VkFmt){VK_FORMAT_A2B10G10R10_UINT_PACK32};
    case SF2C_TEXTURES_N_0X3F_AD9F: return (VkFmt){VK_FORMAT_B10G11R11_UFLOAT_PACK32};
    case SF2C_TEXTURES_N_0X40_1AF3: return (VkFmt){VK_FORMAT_E5B9G9R9_UFLOAT_PACK32};
    case SF2C_TEXTURES_N_0X41_BE08: case SF2C_TEXTURES_N_0X42_1114: return (VkFmt){VK_FORMAT_BC1_RGBA_UNORM_BLOCK};
    case SF2C_TEXTURES_N_0X43_6CE1: return (VkFmt){VK_FORMAT_BC2_UNORM_BLOCK};
    case SF2C_TEXTURES_N_0X44_E723: return (VkFmt){VK_FORMAT_BC3_UNORM_BLOCK};
    case SF2C_TEXTURES_N_0X45_17A0: case SF2C_TEXTURES_N_0X46_E7C6: return (VkFmt){VK_FORMAT_BC1_RGBA_SRGB_BLOCK};
    case SF2C_TEXTURES_N_0X47_F559: return (VkFmt){VK_FORMAT_BC2_SRGB_BLOCK};
    case SF2C_TEXTURES_N_0X48_0EF0: return (VkFmt){VK_FORMAT_BC3_SRGB_BLOCK};
    case SF2C_TEXTURES_N_0X49_823D: return (VkFmt){VK_FORMAT_BC4_UNORM_BLOCK};
    case SF2C_TEXTURES_N_0X4A_E097: return (VkFmt){VK_FORMAT_BC4_SNORM_BLOCK};
    case SF2C_TEXTURES_N_0X4B_6716: return (VkFmt){VK_FORMAT_BC5_UNORM_BLOCK};
    // 0x4C = signed RGTC2; SF2RECOMP_BC5_UNORM=1 decodes it unsigned (see metal.m / notes/PLAN.md)
    case SF2C_TEXTURES_N_0X4C_CEDB: {
        static int unorm = -1;  // (per texture lookup: getenv scans the environment, notably on Windows)
        if (unorm < 0) unorm = getenv(SF2C_TEXTURES_S_SF2RECOMP_BC5_UNORM_5171) != NULL;
        return (VkFmt){unorm ? VK_FORMAT_BC5_UNORM_BLOCK : VK_FORMAT_BC5_SNORM_BLOCK};
    }
    case SF2C_TEXTURES_N_0X4D_844B: return (VkFmt){VK_FORMAT_BC7_UNORM_BLOCK};
    case SF2C_TEXTURES_N_0X4E_20FE: return (VkFmt){VK_FORMAT_BC7_SRGB_BLOCK};
    case SF2C_TEXTURES_N_0X4F_23E4: return (VkFmt){VK_FORMAT_BC6H_SFLOAT_BLOCK};
    case SF2C_TEXTURES_N_0X50_4171: return (VkFmt){VK_FORMAT_BC6H_UFLOAT_BLOCK};
    case SF2C_TEXTURES_N_0X6D_9F92: return (VkFmt){VK_FORMAT_R8G8B8A8_UNORM, 0, 1};
    case SF2C_TEXTURES_N_0X71_BFAA: return (VkFmt){VK_FORMAT_R16G16B16A16_SFLOAT, 0, 1};
    case SF2C_TEXTURES_N_0X76_3ABA: return (VkFmt){VK_FORMAT_R32G32B32A32_SFLOAT, 0, 1};
    case SF2C_TEXTURES_N_0X79_A3F8: return (VkFmt){VK_FORMAT_ASTC_4x4_UNORM_BLOCK};
    case SF2C_TEXTURES_N_0X7A_61B5: return (VkFmt){VK_FORMAT_ASTC_5x4_UNORM_BLOCK};
    case SF2C_TEXTURES_N_0X7B_CA99: return (VkFmt){VK_FORMAT_ASTC_5x5_UNORM_BLOCK};
    case SF2C_TEXTURES_N_0X7C_D6E9: return (VkFmt){VK_FORMAT_ASTC_6x5_UNORM_BLOCK};
    case SF2C_TEXTURES_N_0X7D_8D40: return (VkFmt){VK_FORMAT_ASTC_6x6_UNORM_BLOCK};
    case SF2C_TEXTURES_N_0X7E_D4D4: return (VkFmt){VK_FORMAT_ASTC_8x5_UNORM_BLOCK};
    case SF2C_TEXTURES_N_0X7F_C234: return (VkFmt){VK_FORMAT_ASTC_8x6_UNORM_BLOCK};
    case SF2C_TEXTURES_N_0X80_D41C: return (VkFmt){VK_FORMAT_ASTC_8x8_UNORM_BLOCK};
    case SF2C_TEXTURES_N_0X81_C29D: return (VkFmt){VK_FORMAT_ASTC_10x5_UNORM_BLOCK};
    case SF2C_TEXTURES_N_0X82_09A5: return (VkFmt){VK_FORMAT_ASTC_10x6_UNORM_BLOCK};
    case SF2C_TEXTURES_N_0X83_7230: return (VkFmt){VK_FORMAT_ASTC_10x8_UNORM_BLOCK};
    case SF2C_TEXTURES_N_0X84_7CD7: return (VkFmt){VK_FORMAT_ASTC_10x10_UNORM_BLOCK};
    case SF2C_TEXTURES_N_0X85_124B: return (VkFmt){VK_FORMAT_ASTC_12x10_UNORM_BLOCK};
    case SF2C_TEXTURES_N_0X86_0DF0: return (VkFmt){VK_FORMAT_ASTC_12x12_UNORM_BLOCK};
    case SF2C_TEXTURES_N_0X87_1A28: return (VkFmt){VK_FORMAT_ASTC_4x4_SRGB_BLOCK};
    case SF2C_TEXTURES_N_0X88_AC70: return (VkFmt){VK_FORMAT_ASTC_5x4_SRGB_BLOCK};
    case SF2C_TEXTURES_N_0X89_72E9: return (VkFmt){VK_FORMAT_ASTC_5x5_SRGB_BLOCK};
    case SF2C_TEXTURES_N_0X8A_A44A: return (VkFmt){VK_FORMAT_ASTC_6x5_SRGB_BLOCK};
    case SF2C_TEXTURES_N_0X8B_9798: return (VkFmt){VK_FORMAT_ASTC_6x6_SRGB_BLOCK};
    case SF2C_TEXTURES_N_0X8C_9F7A: return (VkFmt){VK_FORMAT_ASTC_8x5_SRGB_BLOCK};
    case SF2C_TEXTURES_N_0X8D_2C22: return (VkFmt){VK_FORMAT_ASTC_8x6_SRGB_BLOCK};
    case SF2C_TEXTURES_N_0X8E_28A7: return (VkFmt){VK_FORMAT_ASTC_8x8_SRGB_BLOCK};
    case SF2C_TEXTURES_N_0X8F_00B4: return (VkFmt){VK_FORMAT_ASTC_10x5_SRGB_BLOCK};
    case SF2C_TEXTURES_N_0X90_4255: return (VkFmt){VK_FORMAT_ASTC_10x6_SRGB_BLOCK};
    case SF2C_TEXTURES_N_0X91_BC34: return (VkFmt){VK_FORMAT_ASTC_10x8_SRGB_BLOCK};
    case SF2C_TEXTURES_N_0X92_E9B0: return (VkFmt){VK_FORMAT_ASTC_10x10_SRGB_BLOCK};
    case SF2C_TEXTURES_N_0X93_ED86: return (VkFmt){VK_FORMAT_ASTC_12x10_SRGB_BLOCK};
    case SF2C_TEXTURES_N_0X94_EB0B: return (VkFmt){VK_FORMAT_ASTC_12x12_SRGB_BLOCK};
    case SF2C_TEXTURES_N_0X95_ADEA: case SF2C_TEXTURES_N_0X96_14FD: case SF2C_TEXTURES_N_0X97_6139: return (VkFmt){VK_FORMAT_R5G5B5A1_UNORM_PACK16};
    case SF2C_TEXTURES_N_0X98_14A1: return (VkFmt){VK_FORMAT_B8G8R8A8_UNORM, 0, 1};
    case SF2C_TEXTURES_N_0X99_093C: return (VkFmt){VK_FORMAT_B8G8R8A8_UNORM};
    case SF2C_TEXTURES_N_0X9A_9E72: return (VkFmt){VK_FORMAT_B8G8R8A8_SRGB, 0, 1};
    case SF2C_TEXTURES_N_0X9B_3373: return (VkFmt){VK_FORMAT_B8G8R8A8_SRGB};
    default: return (VkFmt){VK_FORMAT_R8G8B8A8_UNORM};
    }
}

bool renderer_format_is_depth(uint32_t f) { return f >= SF2C_TEXTURES_N_0X31_9121 && f <= SF2C_TEXTURES_N_0X36_BBAE; }
bool renderer_format_has_stencil(uint32_t f) { return f == SF2C_TEXTURES_N_0X31_9121 || f == SF2C_TEXTURES_N_0X35_B555 || f == SF2C_TEXTURES_N_0X36_BBAE; }
VkFormat vk_format(uint32_t f) { return fmt_of(f).vf; }
bool vk_format_alpha_one(uint32_t f) { return fmt_of(f).alpha_one; }
bool renderer_format_alpha_one(uint32_t f) { return fmt_of(f).alpha_one; }

static VkImageAspectFlags aspect_of(uint32_t f) {
    if (f == SF2C_TEXTURES_N_0X31_9121) return VK_IMAGE_ASPECT_STENCIL_BIT;
    if (f == SF2C_TEXTURES_N_0X35_B555 || f == SF2C_TEXTURES_N_0X36_BBAE) return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    if (renderer_format_is_depth(f)) return VK_IMAGE_ASPECT_DEPTH_BIT;
    return VK_IMAGE_ASPECT_COLOR_BIT;
}

static bool format_supports(VkFormat f, VkFormatFeatureFlags want) {
    VkFormatProperties p;
    vkGetPhysicalDeviceFormatProperties(g_vk.phys, f, &p);
    return (p.optimalTilingFeatures & want) == want;
}

// (shader preparation) the format and aspects an image of this NVN format gets as a render target
// (create_backend_image: a format the device can't sample becomes RGBA8; compressed formats are never targets)
VkFormat vk_image_format(uint32_t nvn, VkImageAspectFlags *aspect) {
    VkFormat vf = vk_format(nvn);
    if (!format_supports(vf, VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT)) vf = VK_FORMAT_R8G8B8A8_UNORM;
    if (aspect) *aspect = renderer_format_is_depth(nvn) ? aspect_of(nvn) : VK_IMAGE_ASPECT_COLOR_BIT;
    return vf;
}

// ================================================================== output resolution / per-frame clears
// Same rules as metal.m (see the comments there).
static float g_rs_x = 1, g_rs_y = 1;   // the render scale: what the frame is drawn at
static float g_al_x = 1, g_al_y = 1;   // the allocation scale: the size of scalable textures (Dynamic: the frame uses a part)
void renderer_get_scale(float *sx, float *sy) { *sx = g_rs_x; *sy = g_rs_y; }
void renderer_set_scale(float sx, float sy) { g_rs_x = sx; g_rs_y = sy; }
void renderer_set_alloc_scale(float ax, float ay) { g_al_x = ax; g_al_y = ay; }
// The part of a scalable texture its contents occupy (Dynamic: the render scale over the allocation scale; 1 otherwise).
void renderer_texture_fraction(NvnTexture *t, float *fx, float *fy) {
    TexBackend *b = t ? (TexBackend *)t->backend : NULL;
    *fx = *fy = 1;
    if (b && b->scalable && b->ax > 0 && b->ay > 0) { *fx = b->sx / b->ax; *fy = b->sy / b->ay; }
}
uint64_t renderer_present_count(void);

static bool tex_scalable(const NvnTexture *t) {
    static int off = -1;  // (per texture lookup: cached)
    if (off < 0) off = getenv(SF2C_TEXTURES_S_SF2RECOMP_NO_SCALE_224E) != NULL;
    if (off) return false;
    if (t->target != 1 || t->samples > 1 || !(t->flags & SF2C_TEXTURES_N_8_2C62) || t->width < SF2C_TEXTURES_N_32_E29C || t->height < SF2C_TEXTURES_N_16_B17E) return false;
    // Screen buffers have an exact native size in every aspect mode. Artwork
    // can also be 16:9 (including 2048x1152 uploads), but its CPU copies and
    // texture coordinates use native texels and must not be resolution-scaled.
    return t->width == settings_screen_width(SF2C_TEXTURES_N_1080_32EB) && t->height == SF2C_TEXTURES_N_1080_32EB;
}

bool renderer_texture_first_write(NvnTexture *t) {
    TexBackend *b = t ? (TexBackend *)t->backend : NULL;
    extern uint64_t renderer_exec_frame(void);
    uint64_t total_frames = renderer_exec_frame();  // (the frame being translated)
    if (!b || !b->presented || (b->sx == b->sy) || b->touched_frame == total_frames + 1) return false;
    b->touched_frame = total_frames + 1;
    return true;
}

bool renderer_texture_frame_fresh(NvnTexture *t) {
    TexBackend *b = t ? (TexBackend *)t->backend : NULL;
    extern uint64_t renderer_exec_frame(void);
    uint64_t total_frames = renderer_exec_frame();  // (the frame being translated)
    if (!b || !b->scalable || b->frame_cleared == total_frames + 1) return false;
    // SF2RECOMP_NO_FRAME_CLEAR skips the clear only while the contents cover the whole target: below 1x a frame draws
    // into part of it, and the rest would show what an earlier frame left there (issue #205: the upper part flickered)
    // (Dynamic: the contents cover the whole target only at the factor 1 - its render scale is the allocation's)
    if (g_vk.no_frame_clear && b->sx >= SF2C_TEXTURES_N_1_0F_CA71 && b->sy >= SF2C_TEXTURES_N_1_0F_CA71 && b->sx >= b->ax && b->sy >= b->ay) return false;
    b->frame_cleared = total_frames + 1;
    return true;
}

void renderer_texture_mark_presented(NvnTexture *t) {
    TexBackend *b = t ? (TexBackend *)t->backend : NULL;
    if (b) b->presented = true;
}

bool renderer_texture_presented(NvnTexture *t) {
    TexBackend *b = t ? (TexBackend *)t->backend : NULL;
    return b && b->presented;
}

void renderer_texture_scale(NvnTexture *t, float *sx, float *sy) {
    TexBackend *b = t ? (TexBackend *)t->backend : NULL;
    if (b && b->scalable) { *sx = b->sx; *sy = b->sy; }
    else { *sx = 1; *sy = 1; }
}

// ================================================================== images
void vk_image_init_layout(VkCommandBuffer cb, VkImage img, VkImageAspectFlags aspect) {
    VkImageMemoryBarrier2 b = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                               .srcStageMask = VK_PIPELINE_STAGE_2_NONE, .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                               .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                               .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_GENERAL, .image = img,
                               .subresourceRange = {aspect, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS},
                               .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED};
    VkDependencyInfo di = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &b};
    rec_CmdPipelineBarrier2(cb, &di);
}

// Images destroyed by the guest are kept until the submissions that may use them have completed.
typedef struct { VkImage img; VmaAllocation mem; VkImageView view; VkBuffer buf; VkDeviceMemory bmem; uint64_t seq; } Dead;
static Dead *dead;
static int ndead, cap_dead;
static void views_forget(TexBackend *b);

void vk_collect_dead(void) {
    uint64_t done = vk_completed_seq();
    int k = 0;
    for (int i = 0; i < ndead; i++) {
        if (dead[i].seq > done) { dead[k++] = dead[i]; continue; }
        if (dead[i].view) vkDestroyImageView(g_vk.dev, dead[i].view, NULL);
        else if (dead[i].buf) { vkDestroyBuffer(g_vk.dev, dead[i].buf, NULL); vkFreeMemory(g_vk.dev, dead[i].bmem, NULL); }
        else vmaDestroyImage(g_vk.vma, dead[i].img, dead[i].mem);
    }
    ndead = k;
}

// The sRGB / UNORM counterpart of a colour format (VK_FORMAT_UNDEFINED: none). (g_vk.lean_usage format lists)
static VkFormat srgb_twin(VkFormat f) {
    switch (f) {
    case VK_FORMAT_R8G8B8A8_UNORM: return VK_FORMAT_R8G8B8A8_SRGB;
    case VK_FORMAT_R8G8B8A8_SRGB: return VK_FORMAT_R8G8B8A8_UNORM;
    case VK_FORMAT_B8G8R8A8_UNORM: return VK_FORMAT_B8G8R8A8_SRGB;
    case VK_FORMAT_B8G8R8A8_SRGB: return VK_FORMAT_B8G8R8A8_UNORM;
    case VK_FORMAT_BC1_RGBA_UNORM_BLOCK: return VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
    case VK_FORMAT_BC1_RGBA_SRGB_BLOCK: return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
    case VK_FORMAT_BC2_UNORM_BLOCK: return VK_FORMAT_BC2_SRGB_BLOCK;
    case VK_FORMAT_BC2_SRGB_BLOCK: return VK_FORMAT_BC2_UNORM_BLOCK;
    case VK_FORMAT_BC3_UNORM_BLOCK: return VK_FORMAT_BC3_SRGB_BLOCK;
    case VK_FORMAT_BC3_SRGB_BLOCK: return VK_FORMAT_BC3_UNORM_BLOCK;
    case VK_FORMAT_BC7_UNORM_BLOCK: return VK_FORMAT_BC7_SRGB_BLOCK;
    case VK_FORMAT_BC7_SRGB_BLOCK: return VK_FORMAT_BC7_UNORM_BLOCK;
    default:
        if (f >= VK_FORMAT_ASTC_4x4_UNORM_BLOCK && f <= VK_FORMAT_ASTC_12x12_SRGB_BLOCK)  // (UNORM, SRGB alternate)
            return (f - VK_FORMAT_ASTC_4x4_UNORM_BLOCK) & 1 ? f - 1 : f + 1;
        return VK_FORMAT_UNDEFINED;
    }
}

VkFormat vk_view_format(TexBackend *b, VkFormat want) {
    if (!b || !b->nview_fmt || want == b->format) return want;
    for (int i = 0; i < b->nview_fmt; i++) if (b->view_fmt[i] == want) return want;
    static int nlog;
    if (nlog++ < SF2C_TEXTURES_N_10_4A44) LOG(SF2C_TEXTURES_S_VK_LEAN_USAGE_VIEW_FORMAT_D__8536, want, b->format);
    return b->format;
}

static void create_image(NvnTexture *t, TexBackend *b) {
    VkFmt mf = fmt_of(t->format);
    b->ax = b->scalable ? g_al_x : 1;
    b->ay = b->scalable ? g_al_y : 1;
    b->sx = b->scalable ? g_rs_x : 1;
    b->sy = b->scalable ? g_rs_y : 1;
    uint32_t tw = t->width ? t->width : 1, th = t->height ? t->height : 1;
    if (b->scalable) {
        tw = (uint32_t)lroundf(tw * b->ax); th = (uint32_t)lroundf(th * b->ay);
        if (tw < 1) tw = 1;
        if (th < 1) th = 1;
    }
    VkImageType type = VK_IMAGE_TYPE_2D;
    uint32_t layers = 1, depth = 1;
    switch (t->target) {
    case 0: type = VK_IMAGE_TYPE_1D; th = 1; break;
    case SF2C_TEXTURES_N_2_D473: type = VK_IMAGE_TYPE_3D; depth = t->depth ? t->depth : 1; break;
    case SF2C_TEXTURES_N_3_4E07: type = VK_IMAGE_TYPE_1D; layers = t->height ? t->height : 1; th = 1; break;
    case SF2C_TEXTURES_N_4_4B22: case SF2C_TEXTURES_N_6_E7F6: layers = t->depth ? t->depth : 1; break;
    case SF2C_TEXTURES_N_8_2C62: layers = SF2C_TEXTURES_N_6_E7F6; break;
    case SF2C_TEXTURES_N_9_1958: layers = t->depth ? t->depth : SF2C_TEXTURES_N_6_E7F6; break;
    default: break;
    }
    uint32_t levels = t->levels ? t->levels : 1;
    {   // never more levels than the size allows (scaled targets, odd guest values)
        uint32_t m = 1, sz = tw > th ? tw : th;
        if (depth > sz) sz = depth;
        while (sz > 1) { sz >>= 1; m++; }
        if (levels > m) levels = m;
    }
    uint32_t samples = t->samples > 1 ? t->samples : 1;
    if (samples > 1) levels = 1;
    VkFormat vf = mf.vf;
    VkFormatFeatureFlags need = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    b->astc_bw = b->astc_bh = 0;
    if (!format_supports(vf, need)) {
        bool astc = vf >= VK_FORMAT_ASTC_4x4_UNORM_BLOCK && vf <= VK_FORMAT_ASTC_12x12_SRGB_BLOCK;
        const FormatInfo *fi = t->layout.fmt;
        if (astc && fi && fi->bpb == SF2C_TEXTURES_N_16_B17E) {  // desktop GPUs: decoded on the CPU into an RGBA8 image of the same colour space
            b->astc_bw = fi->bw;
            b->astc_bh = fi->bh;
            b->astc_srgb = t->format >= SF2C_TEXTURES_N_0X87_1A28;
            vf = b->astc_srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
        } else {
            static int nlog;
            if (nlog++ < SF2C_TEXTURES_N_20_F5CA) LOG(SF2C_TEXTURES_S_VK_FORMAT_D_NVN_X_NOT_SUPPOR_8D1A, vf, t->format);
            vf = VK_FORMAT_R8G8B8A8_UNORM;
        }
    }
    bool depthfmt = renderer_format_is_depth(t->format);
    VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (depthfmt) usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    else if (format_supports(vf, VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)) usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    VkImageCreateFlags flags = 0;
    if (!depthfmt) flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;  // format views (NVN texture views, raw sRGB views)
    if (type == VK_IMAGE_TYPE_2D && layers >= SF2C_TEXTURES_N_6_E7F6 && tw == th && samples == 1) flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    // g_vk.lean_usage (integrated GPUs): colour images may only be viewed as their own format and its sRGB / UNORM
    // twin, listed in a VkImageFormatListCreateInfo (no twin: not mutable at all). Mesa's AMD / Intel drivers keep
    // DCC / CCS compression for such images; with MUTABLE_FORMAT and no list they turn it off. Views in any other
    // format fall back to the image's own (vk_view_format, logged).
    VkImageFormatListCreateInfo fl = {.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO};
    b->nview_fmt = 0;
    if (!depthfmt && g_vk.lean_usage) {
        VkFormat twin = srgb_twin(vf);
        b->view_fmt[0] = vf;
        b->view_fmt[1] = twin;
        b->nview_fmt = twin != VK_FORMAT_UNDEFINED ? SF2C_TEXTURES_N_2_D473 : 1;
        if (b->nview_fmt == 1) flags &= ~VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
        else fl.viewFormatCount = SF2C_TEXTURES_N_2_D473, fl.pViewFormats = b->view_fmt;
    }
    VkImageCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .pNext = fl.viewFormatCount ? &fl : NULL,
                            .flags = flags, .imageType = type, .format = vf,
                            .extent = {tw, th, depth}, .mipLevels = levels, .arrayLayers = layers,
                            .samples = (VkSampleCountFlagBits)samples, .tiling = VK_IMAGE_TILING_OPTIMAL, .usage = usage,
                            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
    VmaAllocationCreateInfo ai = {.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
#ifdef __linux__
    // Work around NVIDIA copy-engine faults when a small image reuses pooled
    // memory from a destroyed texture (observed on 595.99.02 entering Sparring).
    // A dedicated allocation avoids that reuse without retaining dead textures.
    if (g_vk.props.vendorID == SF2C_TEXTURES_N_0X10DE_112D && type == VK_IMAGE_TYPE_2D && tw <= SF2C_TEXTURES_N_16_B17E && th <= SF2C_TEXTURES_N_16_B17E &&
        depth == 1 && layers == 1 && samples == 1)
        ai.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
#endif
    VkResult r = vmaCreateImage(g_vk.vma, &ci, &ai, &b->image, &b->mem, NULL);
    if (r != VK_SUCCESS) {
        // (the Vulkan side too: a driver can refuse a format / usage / tiling combination with "out of device memory",
        // issue #45 - a GTX 1060 failing a 1920x1080 image with plenty of memory free)
        LOG(SF2C_TEXTURES_S_VK_IMAGE_CREATION_FAILED_D_N_E5DD
            SF2C_TEXTURES_S_U_LEVELS_U_LAYERS_U_SAMPLES__93E9, r, t->format, t->width, t->height, t->depth, t->target, ci.format,
            ci.usage, ci.flags, ci.mipLevels, ci.arrayLayers, (unsigned)ci.samples);
        vk_log_memory(SF2C_TEXTURES_S_IMAGE_CREATION_FAILED_4B76);
        ci.format = vf = VK_FORMAT_R8G8B8A8_UNORM;
        ci.pNext = NULL;  // (g_vk.lean_usage: the fallback image is mutable as before, no format list)
        ci.flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
        b->nview_fmt = 0;
        ci.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                   VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        ci.flags &= ~VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        VK_CHECK(vmaCreateImage(g_vk.vma, &ci, &ai, &b->image, &b->mem, NULL));
        depthfmt = false;
    }
    if (getenv(SF2C_TEXTURES_S_SF2RECOMP_VK_DEVLOST_TRACE_28EE) && tw <= SF2C_TEXTURES_N_16_B17E && th <= SF2C_TEXTURES_N_16_B17E)
        LOG(SF2C_TEXTURES_S_VK_SMALL_IMAGE_P_UX_UX_U_TYP_6090,
            (void *)b->image, tw, th, depth, type, layers, samples, vf, ai.flags);
    b->format = vf;
    b->width = tw; b->height = th; b->depth = depth; b->layers = layers; b->levels = levels; b->samples = samples;
    b->aspect = depthfmt ? aspect_of(t->format) : VK_IMAGE_ASPECT_COLOR_BIT;
    b->base_type = type == VK_IMAGE_TYPE_1D ? (layers > 1 ? VK_IMAGE_VIEW_TYPE_1D_ARRAY : VK_IMAGE_VIEW_TYPE_1D)
                 : type == VK_IMAGE_TYPE_3D ? VK_IMAGE_VIEW_TYPE_3D
                 : t->target == SF2C_TEXTURES_N_8_2C62 ? VK_IMAGE_VIEW_TYPE_CUBE : t->target == SF2C_TEXTURES_N_9_1958 ? VK_IMAGE_VIEW_TYPE_CUBE_ARRAY
                 : layers > 1 || t->target == SF2C_TEXTURES_N_4_4B22 || t->target == SF2C_TEXTURES_N_6_E7F6 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
    vk_image_init_layout(vk_upload_cmd(), b->image, b->aspect);
    // New depth / stencil images start zeroed. The game builds its depth pyramid with depth-only copies, so the stencil
    // of those images was never written: whatever the memory held, different per image and per run, then failed or
    // passed the depth-of-field passes' stencil test in patches (flicker that came and went between runs, every other
    // frame, below 1x internal resolution).
    // Scalable colour targets too (as Metal's, metal.m clear_new_target): Dynamic resolution draws into their top-left
    // part only, and the game's blurs sample a few texels past it, where a new image holds whatever its memory held -
    // NaN / infinity in a float target among it, which the bloom chain spreads and keeps (a pure green band down the
    // right edge, issue #205; every window resize reallocates these images). Copies into them write only the contents'
    // part, and with SF2RECOMP_NO_FRAME_CLEAR the per-frame clear is skipped. SF2RECOMP_NEW_TARGET_FILL=<r>,<g>,<b>,<a>
    // (test): that colour instead of zeros.
    bool clear_color = !depthfmt && b->scalable && (ci.usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
    if (depthfmt || clear_color) {
        VkCommandBuffer cb = vk_upload_cmd();
        VkImageSubresourceRange rg = {b->aspect, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS};
        if (depthfmt) {
            VkClearDepthStencilValue zero = {SF2C_TEXTURES_N_0_0F_6895, 0};
            rec_CmdClearDepthStencilImage(cb, b->image, VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &rg);
        } else {
            static int fill = -1;
            static VkClearColorValue fc;
            if (fill < 0) {
                const char *e = getenv(SF2C_TEXTURES_S_SF2RECOMP_NEW_TARGET_FILL_112D);
                float *c = fc.float32;
                fill = e && sscanf(e, SF2C_TEXTURES_S_F_F_F_F_60DF, &c[0], &c[1], &c[SF2C_TEXTURES_N_2_D473], &c[SF2C_TEXTURES_N_3_4E07]) == SF2C_TEXTURES_N_4_4B22;
            }
            VkClearColorValue zero = {{0}};
            rec_CmdClearColorImage(cb, b->image, VK_IMAGE_LAYOUT_GENERAL, fill ? &fc : &zero, 1, &rg);
        }
        VkMemoryBarrier2 mb = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2, .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                               .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT, .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                               .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT};
        VkDependencyInfo di = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .memoryBarrierCount = 1, .pMemoryBarriers = &mb};
        rec_CmdPipelineBarrier2(cb, &di);
    }
}

static void defer(Dead d) {
    if (ndead == cap_dead) { cap_dead = cap_dead ? cap_dead * SF2C_TEXTURES_N_2_D473 : SF2C_TEXTURES_N_256_51E8; dead = realloc(dead, cap_dead * sizeof *dead); }
    d.seq = vk_pending_seq();
    dead[ndead++] = d;
}

void vk_defer_image(VkImage img, VmaAllocation mem) { defer((Dead){.img = img, .mem = mem}); }
void vk_defer_view(VkImageView v) { defer((Dead){.view = v}); }
void vk_defer_buffer(VkBuffer b, VkDeviceMemory m) { defer((Dead){.buf = b, .bmem = m}); }  // (raw memory, not VMA)

void vk_dl_note_upload(void *img, uint32_t w, uint32_t h, uint32_t lv, VkFormat fmt, uint64_t off, uint32_t row);

static uint32_t level_w(const TexBackend *b, uint32_t lv) { uint32_t v = b->width >> lv; return v ? v : 1; }
static uint32_t level_h(const TexBackend *b, uint32_t lv) { uint32_t v = b->height >> lv; return v ? v : 1; }

static void destroy_image(TexBackend *b) {
    views_forget(b);
    if (b->image || b->mem) defer((Dead){.img = b->image, .mem = b->mem});  // (never the same allocation twice)
    b->image = VK_NULL_HANDLE;
    b->mem = VK_NULL_HANDLE;
}

// Live game textures: an in-place reboot (vk_textures_reset) destroys them all.
static NvnTexture **g_live;
static int g_nlive, g_cap_live;
static void live_add(NvnTexture *t, TexBackend *b) {
    if (g_nlive == g_cap_live) { g_cap_live = g_cap_live ? g_cap_live * SF2C_TEXTURES_N_2_D473 : SF2C_TEXTURES_N_1024_E39E; g_live = realloc(g_live, g_cap_live * sizeof *g_live); }
    g_live[g_nlive++] = t;
    b->reg = g_nlive;
}
static void live_remove(TexBackend *b) {
    if (!b->reg) return;
    NvnTexture *last = g_live[--g_nlive];
    g_live[b->reg - 1] = last;
    if (last->backend) ((TexBackend *)last->backend)->reg = b->reg;
    b->reg = 0;
}

static TexBackend *tex_backend(NvnTexture *t) {
    TexBackend *b = t->backend;
    if (b) {
        if (b->scalable && (b->ax != g_al_x || b->ay != g_al_y)) {  // output resolution changed: reallocate
            destroy_image(b);
            create_image(t, b);
        }
        return b;
    }
    vk_collect_dead();
    b = calloc(1, sizeof *b);
    b->scalable = tex_scalable(t);
    create_image(t, b);
    t->backend = b;
    live_add(t, b);
    return b;
}

void backend_texture_destroyed(NvnTexture *t) {
    TexBackend *b = t->backend;
    if (b) {
        extern void vk_alias_texture_destroyed(NvnTexture *t);  // (exec.c copy aliases: a source's contents handed over)
        vk_alias_texture_destroyed(t);
        live_remove(b);
        destroy_image(b);
        free(b->pending_rgba);
        free(b);
    }
    t->backend = NULL;
}

// Reboot (device.c backend_reset; GPU idle): every game texture and its views go.
void vk_textures_reset(void) {
    int n = g_nlive;
    extern void vk_alias_reset(void);
    vk_alias_reset();
    while (g_nlive > 0) backend_texture_destroyed(g_live[g_nlive - 1]);
    vk_collect_dead();
    LOG(SF2C_TEXTURES_S_VK_REBOOT_D_TEXTURES_RELEASE_BF52, n);
}

// ================================================================== uploads (same change detection as metal.m)
static uint64_t full_hash_budget, full_hash_budget_frame;

// Four independent lanes (one serial multiply chain ran at ~1.5 bytes/cycle; dynamic textures are hashed every
// frame). Values are only compared within a run.
static uint64_t hash_full(const uint8_t *p, uint64_t n) {
    uint64_t h0 = SF2C_TEXTURES_N_0X9E3779B97F4A7C15ULL_8CC5 ^ n, h1 = SF2C_TEXTURES_N_0XC2B2AE3D27D4EB4FULL_ACE8, h2 = SF2C_TEXTURES_N_0X165667B19E3779F9ULL_99AC, h3 = SF2C_TEXTURES_N_0X27D4EB2F165667C5ULL_8690;
    uint64_t i = 0;
    for (; i + SF2C_TEXTURES_N_32_E29C <= n; i += SF2C_TEXTURES_N_32_E29C) {
        uint64_t v[SF2C_TEXTURES_N_4_4B22];
        memcpy(v, p + i, SF2C_TEXTURES_N_32_E29C);
        h0 = (h0 ^ v[0]) * SF2C_TEXTURES_N_0X100000001B3ULL_3E10; h0 ^= h0 >> SF2C_TEXTURES_N_29_3513;
        h1 = (h1 ^ v[1]) * SF2C_TEXTURES_N_0X100000001B3ULL_3E10; h1 ^= h1 >> SF2C_TEXTURES_N_29_3513;
        h2 = (h2 ^ v[SF2C_TEXTURES_N_2_D473]) * SF2C_TEXTURES_N_0X100000001B3ULL_3E10; h2 ^= h2 >> SF2C_TEXTURES_N_29_3513;
        h3 = (h3 ^ v[SF2C_TEXTURES_N_3_4E07]) * SF2C_TEXTURES_N_0X100000001B3ULL_3E10; h3 ^= h3 >> SF2C_TEXTURES_N_29_3513;
    }
    uint64_t h = h0 ^ (h1 * SF2C_TEXTURES_N_0X9E3779B97F4A7C15ULL_8CC5) ^ (h2 * SF2C_TEXTURES_N_0XC2B2AE3D27D4EB4FULL_ACE8) ^ (h3 * SF2C_TEXTURES_N_0X165667B19E3779F9ULL_99AC);
    for (; i < n; i++) h = (h ^ p[i]) * SF2C_TEXTURES_N_0X100000001B3ULL_3E10;
    return h ^ (h >> SF2C_TEXTURES_N_31_EB1E);
}

static uint64_t fnv_sample(const uint8_t *p, uint64_t n) {
    uint64_t h = SF2C_TEXTURES_N_1469598103934665603ULL_7B90;
    uint64_t step = n > SF2C_TEXTURES_N_4096_8B92 ? n / SF2C_TEXTURES_N_4096_8B92 : 1;
    for (uint64_t i = 0; i < n; i += step) { h ^= p[i]; h *= SF2C_TEXTURES_N_1099511628211ULL_D3BD; }
    return h ^ n;
}

static pthread_mutex_t pending_lock = PTHREAD_MUTEX_INITIALIZER;
static NvnTexture movie_texture;
static bool movie_texture_initialized;
static pthread_mutex_t movie_frame_lock = PTHREAD_MUTEX_INITIALIZER;
static uint8_t *movie_frame_pending;
static bool movie_frame_clear_pending;
static uint8_t movie_black_nv12[RENDERER_MOVIE_WIDTH * RENDERER_MOVIE_HEIGHT * SF2C_TEXTURES_N_3_4E07 / SF2C_TEXTURES_N_2_D473];
static bool movie_black_initialized;

static void upload_pending(TexBackend *b) {
    pthread_mutex_lock(&pending_lock);
    void *px = b->pending_rgba;
    uint32_t w = b->pending_w, h = b->pending_h, stride = b->pending_stride, uv_off = b->pending_uv_off;
    bool nv12 = b->pending_nv12;
    b->pending_rgba = NULL;
    pthread_mutex_unlock(&pending_lock);
    if (!px) return;
    if (nv12) { vk_convert_nv12(b, px, w, h, stride, uv_off); free(px); return; }
    if (w > b->width) w = b->width;
    if (h > b->height) h = b->height;
    VkSpan s = vk_transient_copy(px, (uint64_t)stride * h, SF2C_TEXTURES_N_16_B17E);
    free(px);
    VkBufferImageCopy rg = {.bufferOffset = s.off, .bufferRowLength = stride / SF2C_TEXTURES_N_4_4B22,
                            .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .imageExtent = {w, h, 1}};
    VkCommandBuffer cb = vk_upload_cmd();
    vk_dl_note_upload((void *)b->image, rg.imageExtent.width, rg.imageExtent.height, rg.imageSubresource.mipLevel,
                      b->format, rg.bufferOffset, rg.bufferRowLength);
    rec_CmdCopyBufferToImage(cb, s.buf, b->image, VK_IMAGE_LAYOUT_GENERAL, 1, &rg);
}

uint64_t prof_tex_checks, prof_tex_samples, prof_tex_full, prof_tex_full_bytes, prof_tex_dyn, prof_tex_watch_skips;
static void upload_texture(NvnTexture *t, TexBackend *b) {
    if (!t->addr || t->target == SF2C_TEXTURES_N_10_4A44) return;
    uint64_t frame = vk_frame_counter();
    if (b->uploaded && b->checked_frame == frame) return;
    if (b->uploaded && !b->dynamic && ((frame + ((uintptr_t)b >> SF2C_TEXTURES_N_6_E7F6)) & SF2C_TEXTURES_N_7_7902)) return;
    const FormatInfo *fi = t->layout.fmt;
    if (!fi || !fi->bpb) return;
    VkFmt mf = fmt_of(t->format);
    uint64_t total = tegra_total_size(&t->layout);
    b->checked_frame = frame;
    prof_tex_checks++;
    if (t->flags & SF2C_TEXTURES_N_0X10_BF9C) b->dynamic = true;
    if (full_hash_budget_frame != frame) { full_hash_budget_frame = frame; full_hash_budget = SF2C_TEXTURES_N_4_4B22 << SF2C_TEXTURES_N_20_F5CA; }
    const uint8_t *mem = (const uint8_t *)(uintptr_t)t->addr;
    static int always_full = -1;
    if (always_full < 0) always_full = getenv(SF2C_TEXTURES_S_SF2RECOMP_TEX_FULLHASH_DEFD) ? 1 : 0;
    bool full = b->dynamic || !b->uploaded || always_full;
    bool periodic = frame - b->full_frame >= SF2C_TEXTURES_N_120_2ABA && full_hash_budget >= total;  // (safety net: a full hash now and then)
    if (!full && periodic) { full = true; full_hash_budget -= total; }
    // The OS's page write tracking (exec.c vk_pages_changed, shared with the vertex cache): texture memory the CPU
    // hasn't written since the last check is not hashed at all (dynamic textures were fully hashed every frame,
    // ~2.4 MB a frame in the overworld). The periodic full hash stays as a safety net. SF2RECOMP_VK_TEX_WATCH=0: off.
    static int watch = -1;
    if (watch < 0) { const char *e = getenv(SF2C_TEXTURES_S_SF2RECOMP_VK_TEX_WATCH_599C); watch = !e || atoi(e) != 0; }
    if (watch && !always_full) {
        extern int vk_pages_changed(uint64_t addr, uint64_t size, uint32_t since, uint32_t *now);
        uint32_t now = 0;
        int ch = vk_pages_changed(t->addr, total, b->wgen, &now);
        if (ch >= 0) {
            b->wgen = now;
            if (!ch && b->uploaded) {
                if (!periodic) { prof_tex_watch_skips++; return; }
                if (b->dynamic) full_hash_budget -= total;  // (the others took theirs above)
            }
        }
    }
    bool fast = tex_fast_hash();  // (runtime/nvn/tex_hash.h; SF2RECOMP_TEX_FASTHASH=0: the previous hashes)
    if (!full) {
        prof_tex_samples++;
        uint64_t sh = fast ? tex_hash_sample(mem, total) : fnv_sample(mem, total);
        if (sh == b->sample_hash) return;
    }
    prof_tex_full++; prof_tex_full_bytes += total; if (b->dynamic) prof_tex_dyn++;
    extern void slow_note_full_hash(void), slow_note_upload(uint64_t bytes);  // (slowframe.h)
    slow_note_full_hash();
    uint64_t h = fast ? tex_hash_full(mem, total) : hash_full(mem, total);
    b->full_frame = frame;
    // (the sample is only compared while the texture isn't dynamic, and a dynamic one stays so: none for those)
    if (!fast) b->sample_hash = fnv_sample(mem, total);
    else if (!b->dynamic && !(b->uploaded && h != b->data_hash)) b->sample_hash = tex_hash_sample(mem, total);
    if (b->uploaded && h == b->data_hash) return;
    if (b->uploaded && h != b->data_hash) b->dynamic = true;
    b->data_hash = h;
    b->uploaded = true;
    if (b->format != mf.vf && !b->astc_bw) return;  // unsupported format: nothing sensible to copy
    slow_note_upload(total);
    VkCommandBuffer cb = vk_upload_cmd();
    uint32_t layers = t->layout.layers ? t->layout.layers : 1;
    if (layers > b->layers && b->depth == 1) layers = b->layers;
    uint64_t layer_size = tegra_layer_size(&t->layout);
    bool linear = (t->flags & SF2C_TEXTURES_N_0X10_BF9C) != 0;
    uint32_t levels = t->layout.levels < b->levels ? t->layout.levels : b->levels;
    for (uint32_t layer = 0; layer < layers; layer++) {
        for (uint32_t lv = 0; lv < levels; lv++) {
            LevelLayout l = tegra_level(&t->layout, lv);
            if (b->astc_bw) {  // blocks -> linear block rows -> RGBA8 texels
                const uint8_t *src = (const uint8_t *)(uintptr_t)t->addr + layer * layer_size + l.offset;
                uint8_t *blocks = malloc((uint64_t)l.wb * l.hb * SF2C_TEXTURES_N_16_B17E);
                if (linear) for (uint32_t y = 0; y < l.hb; y++) memcpy(blocks + (uint64_t)y * l.wb * SF2C_TEXTURES_N_16_B17E, src + (uint64_t)y * t->stride, l.wb * SF2C_TEXTURES_N_16_B17E);
                else tegra_deswizzle(src, blocks, l.wb, l.hb, 1, SF2C_TEXTURES_N_16_B17E, l.block_height_log2, l.block_depth_log2);
                VkSpan s = vk_transient((uint64_t)l.w * l.h * SF2C_TEXTURES_N_4_4B22, SF2C_TEXTURES_N_16_B17E);
                astc_decode_ldr(blocks, l.w, l.h, b->astc_bw, b->astc_bh, b->astc_srgb, s.ptr);
                free(blocks);
                VkBufferImageCopy rg = {.bufferOffset = s.off, .bufferRowLength = l.w, .bufferImageHeight = l.h,
                                        .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, lv, layer, 1},
                                        .imageExtent = {l.w < level_w(b, lv) ? l.w : level_w(b, lv), l.h < level_h(b, lv) ? l.h : level_h(b, lv), 1}};
                vk_dl_note_upload((void *)b->image, rg.imageExtent.width, rg.imageExtent.height, rg.imageSubresource.mipLevel,
                                  b->format, rg.bufferOffset, rg.bufferRowLength);
                rec_CmdCopyBufferToImage(cb, s.buf, b->image, VK_IMAGE_LAYOUT_GENERAL, 1, &rg);
                continue;
            }
            uint32_t row = l.wb * fi->bpb;
            uint64_t sz = (uint64_t)row * l.hb * l.d;
            bool expand = mf.expand_rgb && fi->bpb == SF2C_TEXTURES_N_3_4E07;
            uint32_t out_row = expand ? l.wb * SF2C_TEXTURES_N_4_4B22 : row;
            VkSpan s = vk_transient((uint64_t)out_row * l.hb * l.d, SF2C_TEXTURES_N_16_B17E);
            uint8_t *lin = expand ? malloc(sz) : s.ptr;
            const uint8_t *src = (const uint8_t *)(uintptr_t)t->addr + layer * layer_size + l.offset;
            if (linear) {
                for (uint32_t y = 0; y < l.hb; y++) memcpy(lin + (uint64_t)y * row, src + (uint64_t)y * t->stride, row);
            } else {
                tegra_deswizzle(src, lin, l.wb, l.hb, l.d, fi->bpb, l.block_height_log2, l.block_depth_log2);
            }
            if (expand) {  // RGB8 -> RGBA8
                uint8_t *o = s.ptr;
                uint64_t n = (uint64_t)l.wb * l.hb * l.d;
                for (uint64_t i = 0; i < n; i++) { o[i * SF2C_TEXTURES_N_4_4B22] = lin[i * SF2C_TEXTURES_N_3_4E07]; o[i * SF2C_TEXTURES_N_4_4B22 + 1] = lin[i * SF2C_TEXTURES_N_3_4E07 + 1]; o[i * SF2C_TEXTURES_N_4_4B22 + SF2C_TEXTURES_N_2_D473] = lin[i * SF2C_TEXTURES_N_3_4E07 + SF2C_TEXTURES_N_2_D473]; o[i * SF2C_TEXTURES_N_4_4B22 + SF2C_TEXTURES_N_3_4E07] = SF2C_TEXTURES_N_255_9556; }
                free(lin);
            }
            uint32_t w = l.w, hh = l.h, d = l.d;
            // the copy stays inside the image's level: a texture allocated at the internal resolution (16:9, render
            // target sized) can still be filled from the CPU - the save slots' screenshots on the title screen. Its
            // full-size data overran the scaled image (only the width was clamped): GPU writes past the allocation,
            // a device loss below 1x after a seed switch.
            if (w > level_w(b, lv)) w = level_w(b, lv);
            if (hh > level_h(b, lv)) hh = level_h(b, lv);
            VkBufferImageCopy rg = {.bufferOffset = s.off, .bufferRowLength = l.wb * fi->bw, .bufferImageHeight = l.hb * fi->bh,
                                    .imageSubresource = {b->aspect & VK_IMAGE_ASPECT_DEPTH_BIT ? VK_IMAGE_ASPECT_DEPTH_BIT : b->aspect, lv,
                                                         b->depth > 1 ? 0 : layer, 1},
                                    .imageExtent = {w, b->base_type == VK_IMAGE_VIEW_TYPE_1D || b->base_type == VK_IMAGE_VIEW_TYPE_1D_ARRAY ? 1 : hh,
                                                    b->depth > 1 ? d : 1}};
            if (b->aspect == (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) continue;  // packed D/S: no CPU layout
            vk_dl_note_upload((void *)b->image, rg.imageExtent.width, rg.imageExtent.height, rg.imageSubresource.mipLevel,
                              b->format, rg.bufferOffset, rg.bufferRowLength);
            rec_CmdCopyBufferToImage(cb, s.buf, b->image, VK_IMAGE_LAYOUT_GENERAL, 1, &rg);
        }
    }
}

TexBackend *vk_tex(NvnTexture *t, bool for_render) {
    TexBackend *b = tex_backend(t);
    if (b->pending_rgba) upload_pending(b);
    if (for_render) b->gpu_owned = true;
    else if (!b->gpu_owned) {
        extern uint64_t prof_up_ns;
        uint64_t t0 = prof_up_ns != ~SF2C_TEXTURES_N_0ULL_DF1B ? SDL_GetTicksNS() : 0;
        upload_texture(t, b);
        if (prof_up_ns != ~SF2C_TEXTURES_N_0ULL_DF1B) prof_up_ns += SDL_GetTicksNS() - t0;
    }
    return b;
}

// A GPU write (render target, copy destination): the contents are at the current render scale from here on.
void renderer_mark_gpu_owned(NvnTexture *t) {
    TexBackend *b = tex_backend(t);
    b->gpu_owned = true;
    if (b->scalable) { b->sx = g_rs_x; b->sy = g_rs_y; }
}
bool renderer_is_gpu_owned(NvnTexture *t) { return tex_backend(t)->gpu_owned; }
void backend_pool_written(uint64_t addr, uint64_t size) { (void)addr; (void)size; }

// Host-produced texel data (decoded video, any thread): applied on the render thread at the texture's next use.
static void upload_rgba_now(NvnTexture *t, const void *rgba, uint32_t w, uint32_t h, uint32_t stride) {
    TexBackend *b = t->backend;
    if (!b) return;  // the game binds the texture before decoding into it; first frames may arrive earlier
    void *copy = malloc((size_t)stride * h);
    memcpy(copy, rgba, (size_t)stride * h);
    pthread_mutex_lock(&pending_lock);
    free(b->pending_rgba);
    b->pending_rgba = copy;
    b->pending_w = w; b->pending_h = h; b->pending_stride = stride;
    b->pending_nv12 = false;
    b->gpu_owned = true;
    pthread_mutex_unlock(&pending_lock);
}

// Decoded video as NV12 (runtime/hle/movie.c): the planes (half the bytes of RGBA) go to the GPU, which converts them
// into the texture (exec.c vk_convert_nv12) - swscale plus the RGBA upload cost mid-range CPUs most of a frame at the
// movies' 60 fps. false: a texture format this doesn't handle (the caller converts on the CPU).
static bool nv12_format_ok(VkFormat f) {
    return f == VK_FORMAT_R8G8B8A8_UNORM || f == VK_FORMAT_R8G8B8A8_SRGB || f == VK_FORMAT_B8G8R8A8_UNORM || f == VK_FORMAT_B8G8R8A8_SRGB;
}
static bool upload_nv12_now(NvnTexture *t, const void *nv12, uint32_t w, uint32_t h, uint32_t stride, uint32_t uv_off);
// Movie frames (runtime/hle/movie.c, the game thread): with the render thread on they are queued behind the lists
// already submitted (rqueue.c), as a copy.
void renderer_upload_rgba(NvnTexture *t, const void *rgba, uint32_t w, uint32_t h, uint32_t stride) {
    if (!rq_on()) { upload_rgba_now(t, rgba, w, h, stride); return; }
    if (!t->backend) return;
    size_t n = (size_t)stride * h;
    void *copy = malloc(n);
    memcpy(copy, rgba, n);
    rq_upload(t, RQ_UPLOAD_RGBA, copy, w, h, stride, 0);
}
bool renderer_upload_nv12(NvnTexture *t, const void *nv12, uint32_t w, uint32_t h, uint32_t stride, uint32_t uv_off) {
    if (!rq_on()) return upload_nv12_now(t, nv12, w, h, stride, uv_off);
    if (!t->backend) return true;
    if (!nv12_format_ok(vk_format(t->format))) return false;
    size_t n = (size_t)uv_off + (size_t)stride * (h / SF2C_TEXTURES_N_2_D473);
    void *copy = malloc(n);
    memcpy(copy, nv12, n);
    rq_upload(t, RQ_UPLOAD_NV12, copy, w, h, stride, uv_off);
    return true;
}
void backend_upload_now(NvnTexture *t, int kind, void *data, uint32_t w, uint32_t h, uint32_t stride, uint32_t uv_off) {
    if (kind == RQ_UPLOAD_RGBA) upload_rgba_now(t, data, w, h, stride);
    else upload_nv12_now(t, data, w, h, stride, uv_off);
    free(data);
}
static bool upload_nv12_now(NvnTexture *t, const void *nv12, uint32_t w, uint32_t h, uint32_t stride, uint32_t uv_off) {
    TexBackend *b = t->backend;
    if (!b) return true;  // (as renderer_upload_rgba: a frame before the texture is first bound is dropped)
    if (!nv12_format_ok(b->format)) return false;
    size_t n = (size_t)uv_off + (size_t)stride * (h / SF2C_TEXTURES_N_2_D473);
    void *copy = malloc(n);
    memcpy(copy, nv12, n);
    pthread_mutex_lock(&pending_lock);
    free(b->pending_rgba);
    b->pending_rgba = copy;
    b->pending_w = w; b->pending_h = h; b->pending_stride = stride;
    b->pending_nv12 = true;
    b->pending_uv_off = uv_off;
    b->gpu_owned = true;
    pthread_mutex_unlock(&pending_lock);
    return true;
}

bool renderer_movie_submit_nv12(const void *data, uint32_t w, uint32_t h, uint32_t stride, uint32_t uv_off) {
    if (!data || w != RENDERER_MOVIE_WIDTH || h != RENDERER_MOVIE_HEIGHT || stride != w || uv_off != w * h) return false;
    size_t size = (size_t)uv_off + (size_t)stride * (h / SF2C_TEXTURES_N_2_D473);
    void *copy = malloc(size);
    if (!copy) return false;
    memcpy(copy, data, size);
    pthread_mutex_lock(&movie_frame_lock);
    free(movie_frame_pending);
    movie_frame_pending = copy;
    movie_frame_clear_pending = false;
    pthread_mutex_unlock(&movie_frame_lock);
    return true;
}

NvnTexture *renderer_movie_texture(void) {
    if (!movie_texture_initialized) {
        memset(&movie_texture, 0, sizeof movie_texture);
        movie_texture.target = 1;
        movie_texture.format = SF2C_TEXTURES_N_0X25_2611;
        movie_texture.width = RENDERER_MOVIE_WIDTH;
        movie_texture.height = RENDERER_MOVIE_HEIGHT;
        movie_texture.depth = 1;
        movie_texture.levels = 1;
        movie_texture.samples = 1;
        movie_texture.layout.width = movie_texture.width;
        movie_texture.layout.height = movie_texture.height;
        movie_texture.layout.depth = 1;
        movie_texture.layout.layers = 1;
        movie_texture.layout.levels = 1;
        movie_texture.layout.fmt = nvn_format_info(movie_texture.format);
        tegra_layout_init(&movie_texture.layout);
        snprintf(movie_texture.label, sizeof movie_texture.label, SF2C_TEXTURES_S_HOST_MOVIE_FRAME_0978);
        movie_texture_initialized = true;
    }
    tex_backend(&movie_texture);
    void *frame = NULL;
    bool clear = false;
    pthread_mutex_lock(&movie_frame_lock);
    frame = movie_frame_pending;
    movie_frame_pending = NULL;
    clear = movie_frame_clear_pending;
    movie_frame_clear_pending = false;
    pthread_mutex_unlock(&movie_frame_lock);
    if (frame) {
        upload_nv12_now(&movie_texture, frame, RENDERER_MOVIE_WIDTH, RENDERER_MOVIE_HEIGHT,
                        RENDERER_MOVIE_WIDTH, RENDERER_MOVIE_WIDTH * RENDERER_MOVIE_HEIGHT);
        free(frame);
    } else if (clear) {
        if (!movie_black_initialized) {
            memset(movie_black_nv12, SF2C_TEXTURES_N_128_2747, sizeof movie_black_nv12);
            memset(movie_black_nv12, SF2C_TEXTURES_N_16_B17E, RENDERER_MOVIE_WIDTH * RENDERER_MOVIE_HEIGHT);
            movie_black_initialized = true;
        }
        upload_nv12_now(&movie_texture, movie_black_nv12, RENDERER_MOVIE_WIDTH, RENDERER_MOVIE_HEIGHT,
                        RENDERER_MOVIE_WIDTH, RENDERER_MOVIE_WIDTH * RENDERER_MOVIE_HEIGHT);
    }
    return &movie_texture;
}

void renderer_movie_clear(void) {
    pthread_mutex_lock(&movie_frame_lock);
    free(movie_frame_pending);
    movie_frame_pending = NULL;
    movie_frame_clear_pending = true;
    pthread_mutex_unlock(&movie_frame_lock);
}

// ================================================================== views
#define VIEW_TAB SF2C_TEXTURES_VIEW_TAB
typedef struct { TexBackend *b; uint64_t k1, k2; VkImageView view; } ViewEnt;
static ViewEnt view_tab[VIEW_TAB];

// Views may still be referenced by submitted or pending work: destroyed once that has completed.
void vk_views_forget(TexBackend *b) { views_forget(b); }
uint32_t vk_view_gen;  // bumped whenever a cached view may be destroyed (exec.c's per-frame texture memo)
static void views_forget(TexBackend *b) {
    vk_view_gen++;
    for (int i = 0; i < VIEW_TAB; i++)
        if (view_tab[i].b == b) { defer((Dead){.view = view_tab[i].view}); view_tab[i].b = NULL; view_tab[i].view = VK_NULL_HANDLE; }
}

VkImageView vk_image_view(TexBackend *b, VkImageViewType type, VkFormat fmt, uint32_t lv0, uint32_t nlv, uint32_t ly0,
                          uint32_t nly, VkComponentMapping sw, VkImageAspectFlags aspect) {
    fmt = vk_view_format(b, fmt);  // (g_vk.lean_usage: only the listed formats)
    uint64_t k1 = (uint64_t)type | (uint64_t)fmt << SF2C_TEXTURES_N_8_2C62 | (uint64_t)lv0 << SF2C_TEXTURES_N_40_D59E | (uint64_t)nlv << SF2C_TEXTURES_N_48_9801 | (uint64_t)aspect << SF2C_TEXTURES_N_56_7688;
    uint64_t k2 = (uint64_t)ly0 | (uint64_t)nly << SF2C_TEXTURES_N_16_B17E | (uint64_t)sw.r << SF2C_TEXTURES_N_32_E29C | (uint64_t)sw.g << SF2C_TEXTURES_N_40_D59E | (uint64_t)sw.b << SF2C_TEXTURES_N_48_9801 | (uint64_t)sw.a << SF2C_TEXTURES_N_56_7688;
    uint64_t h = ((uint64_t)(uintptr_t)b * SF2C_TEXTURES_N_0X9E3779B97F4A7C15ULL_8CC5) ^ (k1 * SF2C_TEXTURES_N_0XC2B2AE3D27D4EB4FULL_ACE8) ^ (k2 * SF2C_TEXTURES_N_0X165667B19E3779F9ULL_99AC);
    uint32_t slot = (uint32_t)(h ^ (h >> SF2C_TEXTURES_N_32_E29C)) & (VIEW_TAB - 1);
    for (int probe = 0; probe < SF2C_TEXTURES_N_16_B17E; probe++, slot = (slot + 1) & (VIEW_TAB - 1)) {
        ViewEnt *e = &view_tab[slot];
        if (e->b == b && e->k1 == k1 && e->k2 == k2) return e->view;
        if (!e->b) break;
    }
    extern uint64_t prof_views_created;
    prof_views_created++;
    VkImageViewCreateInfo vi = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = b->image, .viewType = type,
                                .format = fmt, .components = sw, .subresourceRange = {aspect, lv0, nlv, ly0, nly}};
    VkImageView v;
    if (vkCreateImageView(g_vk.dev, &vi, NULL, &v) != VK_SUCCESS) {
        static int nfail;
        if (nfail++ < SF2C_TEXTURES_N_20_F5CA) LOG(SF2C_TEXTURES_S_VK_VIEW_FAILED_TYPE_D_FMT_D__8DFA, type, fmt, b->format, lv0, nlv, ly0, nly);
        return VK_NULL_HANDLE;
    }
    ViewEnt *e = &view_tab[slot];
    if (e->b) { defer((Dead){.view = e->view}); vk_view_gen++; }  // evict (16 probes taken)
    *e = (ViewEnt){b, k1, k2, v};
    return v;
}

static VkImageViewType view_type_for(int textype) {
    switch (textype) {
    case TEXTYPE_1D: return VK_IMAGE_VIEW_TYPE_1D;
    case TEXTYPE_3D: return VK_IMAGE_VIEW_TYPE_3D;
    case TEXTYPE_CUBE: return VK_IMAGE_VIEW_TYPE_CUBE;
    case TEXTYPE_2D_ARRAY: return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    case TEXTYPE_CUBE_ARRAY: return VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
    case TEXTYPE_1D_ARRAY: return VK_IMAGE_VIEW_TYPE_1D_ARRAY;
    default: return VK_IMAGE_VIEW_TYPE_2D;
    }
}

static const VkComponentMapping IDENTITY = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                                            VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};

// Format views of a VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT image must stay in the image format's compatibility class:
// ordinary colour formats of the same texel size, a compressed format only with its own class (BCn / ASTC block size:
// unorm, snorm and sRGB twins). Anything else is invalid usage (the Metal side aborted on it: a texture pool slot whose
// texture was finalized and the address reused by another format, runtime/nvn/objects.c).
static int vk_color_bytes(VkFormat f) {  // texel size of an ordinary colour format, 0 = not one
    switch (f) {
    case VK_FORMAT_R8_UNORM: case VK_FORMAT_R8_SNORM: case VK_FORMAT_R8_UINT: case VK_FORMAT_R8_SINT: case VK_FORMAT_R8_SRGB:
        return 1;
    case VK_FORMAT_R16_UNORM: case VK_FORMAT_R16_SNORM: case VK_FORMAT_R16_UINT: case VK_FORMAT_R16_SINT:
    case VK_FORMAT_R16_SFLOAT: case VK_FORMAT_R8G8_UNORM: case VK_FORMAT_R8G8_SNORM: case VK_FORMAT_R8G8_UINT:
    case VK_FORMAT_R8G8_SINT: case VK_FORMAT_R8G8_SRGB: case VK_FORMAT_R4G4B4A4_UNORM_PACK16: case VK_FORMAT_B4G4R4A4_UNORM_PACK16:
    case VK_FORMAT_R5G6B5_UNORM_PACK16: case VK_FORMAT_B5G6R5_UNORM_PACK16: case VK_FORMAT_R5G5B5A1_UNORM_PACK16:
    case VK_FORMAT_B5G5R5A1_UNORM_PACK16: case VK_FORMAT_A1R5G5B5_UNORM_PACK16:
        return SF2C_TEXTURES_N_2_D473;
    case VK_FORMAT_R32_UINT: case VK_FORMAT_R32_SINT: case VK_FORMAT_R32_SFLOAT: case VK_FORMAT_R16G16_UNORM:
    case VK_FORMAT_R16G16_SNORM: case VK_FORMAT_R16G16_UINT: case VK_FORMAT_R16G16_SINT: case VK_FORMAT_R16G16_SFLOAT:
    case VK_FORMAT_R8G8B8A8_UNORM: case VK_FORMAT_R8G8B8A8_SNORM: case VK_FORMAT_R8G8B8A8_UINT: case VK_FORMAT_R8G8B8A8_SINT:
    case VK_FORMAT_R8G8B8A8_SRGB: case VK_FORMAT_B8G8R8A8_UNORM: case VK_FORMAT_B8G8R8A8_SRGB:
    case VK_FORMAT_A8B8G8R8_UNORM_PACK32: case VK_FORMAT_A8B8G8R8_SRGB_PACK32: case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
    case VK_FORMAT_A2B10G10R10_UINT_PACK32: case VK_FORMAT_A2R10G10B10_UNORM_PACK32: case VK_FORMAT_B10G11R11_UFLOAT_PACK32:
    case VK_FORMAT_E5B9G9R9_UFLOAT_PACK32:
        return SF2C_TEXTURES_N_4_4B22;
    case VK_FORMAT_R32G32_UINT: case VK_FORMAT_R32G32_SINT: case VK_FORMAT_R32G32_SFLOAT: case VK_FORMAT_R16G16B16A16_UNORM:
    case VK_FORMAT_R16G16B16A16_SNORM: case VK_FORMAT_R16G16B16A16_UINT: case VK_FORMAT_R16G16B16A16_SINT:
    case VK_FORMAT_R16G16B16A16_SFLOAT:
        return SF2C_TEXTURES_N_8_2C62;
    case VK_FORMAT_R32G32B32A32_UINT: case VK_FORMAT_R32G32B32A32_SINT: case VK_FORMAT_R32G32B32A32_SFLOAT:
        return SF2C_TEXTURES_N_16_B17E;
    default:
        return 0;
    }
}
static int vk_block_class(VkFormat f) {  // compressed formats: one id per compatibility class, 0 = not compressed
    if (f >= VK_FORMAT_BC1_RGB_UNORM_BLOCK && f <= VK_FORMAT_BC7_SRGB_BLOCK) return 1 + (f - VK_FORMAT_BC1_RGB_UNORM_BLOCK) / SF2C_TEXTURES_N_2_D473;
    if (f >= VK_FORMAT_ASTC_4x4_UNORM_BLOCK && f <= VK_FORMAT_ASTC_12x12_SRGB_BLOCK) return SF2C_TEXTURES_N_100_AD57 + (f - VK_FORMAT_ASTC_4x4_UNORM_BLOCK) / SF2C_TEXTURES_N_2_D473;
    return 0;
}
static bool vk_view_compatible(VkFormat image, VkFormat view) {
    if (image == view) return true;
    int ib = vk_color_bytes(image), vb = vk_color_bytes(view);
    if (ib || vb) return ib == vb;
    int ic = vk_block_class(image);
    return ic && ic == vk_block_class(view);
}

// render: attachment view (one level, one layer). Otherwise a sampled view of type want_textype (TEXTYPE_*),
// with the NVN component swizzle, converting between compatible types the way renderer.m does.
VkImageView vk_view(NvnTexture *t, const NvnTextureView *v, bool has_view, bool render, int want_textype) {
    TexBackend *b = vk_tex(t, render);
    if (!has_view) v = NULL;
    VkFormat fmt = b->format;
    if (v && (v->setmask & SF2C_TEXTURES_N_4_4B22) && !(b->aspect & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT))) {
        VkFormat vf = fmt_of(v->format).vf;
        if (vk_format(t->format) == b->format && vf != fmt) {  // (fallback images keep their own format)
            if (vk_view_compatible(fmt, vf)) fmt = vf;
            else {
                static int nlog;
                if (!nlog++)
                    LOG(SF2C_TEXTURES_S_VK_TEXTURE_UX_U_FMT_X_VK_D_C_A9E3
                        SF2C_TEXTURES_S_LOGGED_ONCE_N_F045, t->width, t->height, t->format, b->format, v->format, vf);
            }
        }
    }
    uint32_t lv0 = v && (v->setmask & 1) ? v->base_level : 0;
    uint32_t nlv = v && (v->setmask & 1) && v->num_levels ? v->num_levels : b->levels - (lv0 < b->levels ? lv0 : 0);
    uint32_t ly0 = v && (v->setmask & SF2C_TEXTURES_N_2_D473) ? v->min_layer : 0;
    uint32_t nly = v && (v->setmask & SF2C_TEXTURES_N_2_D473) && v->num_layers ? v->num_layers : b->layers - (ly0 < b->layers ? ly0 : 0);
    if (lv0 >= b->levels) lv0 = b->levels - 1;
    if (lv0 + nlv > b->levels) nlv = b->levels - lv0;
    if (ly0 >= b->layers) ly0 = b->layers - 1;
    if (ly0 + nly > b->layers) nly = b->layers - ly0;
    if (render) {
        VkImageViewType type = b->base_type == VK_IMAGE_VIEW_TYPE_3D ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
        if (b->base_type == VK_IMAGE_VIEW_TYPE_3D) return VK_NULL_HANDLE;  // (3D render targets: not used by the game)
        return vk_image_view(b, type, fmt, lv0, 1, ly0, 1, IDENTITY, b->aspect);
    }
    VkImageAspectFlags aspect = b->aspect & VK_IMAGE_ASPECT_DEPTH_BIT ? VK_IMAGE_ASPECT_DEPTH_BIT : b->aspect;
    VkComponentMapping sw = IDENTITY;
    if (!(b->aspect & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) && b->samples == 1) {
        const uint8_t *s = v && (v->setmask & SF2C_TEXTURES_N_8_2C62) ? v->swizzle : t->swizzle;
        uint8_t c[SF2C_TEXTURES_N_4_4B22];
        for (int i = 0; i < SF2C_TEXTURES_N_4_4B22; i++) c[i] = s[i] <= SF2C_TEXTURES_N_5_EF2D ? s[i] : (uint8_t)(SF2C_TEXTURES_N_2_D473 + i);
        if (c[0] || c[1] || c[SF2C_TEXTURES_N_2_D473] || c[SF2C_TEXTURES_N_3_4E07]) {  // never set: identity
            if (fmt_of(t->format).alpha_one)
                for (int i = 0; i < SF2C_TEXTURES_N_4_4B22; i++) if (c[i] == SF2C_TEXTURES_N_5_EF2D) c[i] = 1;
            // NVN swizzle 0 zero, 1 one, 2..5 r,g,b,a == VkComponentSwizzle - 1
            sw = (VkComponentMapping){(VkComponentSwizzle)(c[0] + 1), (VkComponentSwizzle)(c[1] + 1), (VkComponentSwizzle)(c[SF2C_TEXTURES_N_2_D473] + 1),
                                      (VkComponentSwizzle)(c[SF2C_TEXTURES_N_3_4E07] + 1)};
        } else if (fmt_of(t->format).alpha_one) {
            sw.a = VK_COMPONENT_SWIZZLE_ONE;
        }
    }
    VkImageViewType want = view_type_for(want_textype);
    VkImageViewType have = b->base_type;
    if (b->samples > 1) return vk_dummy_view(want_textype);  // multisampled images can't be sampled as 2D
    if (want == have) {
        if (have == VK_IMAGE_VIEW_TYPE_CUBE) nly = SF2C_TEXTURES_N_6_E7F6;
        return vk_image_view(b, want, fmt, lv0, nlv, ly0, nly, sw, aspect);
    }
    if (want == VK_IMAGE_VIEW_TYPE_2D && (have == VK_IMAGE_VIEW_TYPE_2D_ARRAY || have == VK_IMAGE_VIEW_TYPE_CUBE))
        return vk_image_view(b, want, fmt, lv0, nlv, ly0, 1, sw, aspect);
    if (want == VK_IMAGE_VIEW_TYPE_2D_ARRAY && (have == VK_IMAGE_VIEW_TYPE_2D || have == VK_IMAGE_VIEW_TYPE_CUBE))
        return vk_image_view(b, want, fmt, lv0, nlv, ly0, have == VK_IMAGE_VIEW_TYPE_CUBE ? nly : 1, sw, aspect);
    if (want == VK_IMAGE_VIEW_TYPE_CUBE && have == VK_IMAGE_VIEW_TYPE_2D_ARRAY && b->layers >= SF2C_TEXTURES_N_6_E7F6 && b->width == b->height)
        return vk_image_view(b, want, fmt, lv0, nlv, ly0 + SF2C_TEXTURES_N_6_E7F6 <= b->layers ? ly0 : 0, SF2C_TEXTURES_N_6_E7F6, sw, aspect);
    if (want == VK_IMAGE_VIEW_TYPE_1D_ARRAY && have == VK_IMAGE_VIEW_TYPE_1D)
        return vk_image_view(b, want, fmt, lv0, nlv, 0, 1, sw, aspect);
    static int nlog;
    if (nlog++ < SF2C_TEXTURES_N_30_624B) LOG(SF2C_TEXTURES_S_TEX_TYPE_MISMATCH_WANT_D_HAV_2766, want, have, t->width,
                         t->height, t->depth, t->target, t->format, b->layers);
    return vk_dummy_view(want_textype);
}

// 1x1 black textures for unbound slots, per type
VkImageView vk_dummy_view(int textype) {
    static TexBackend dummies[SF2C_TEXTURES_N_10_4A44];
    static VkImageView views[SF2C_TEXTURES_N_10_4A44];
    if (textype < 0 || textype >= SF2C_TEXTURES_N_10_4A44) textype = TEXTYPE_2D;
    if (views[textype]) return views[textype];
    TexBackend *b = &dummies[textype];
    VkImageViewType vt = view_type_for(textype);
    bool cube = vt == VK_IMAGE_VIEW_TYPE_CUBE || vt == VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
    VkImageCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .flags = cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0,
                            .imageType = vt == VK_IMAGE_VIEW_TYPE_3D ? VK_IMAGE_TYPE_3D
                                       : vt == VK_IMAGE_VIEW_TYPE_1D || vt == VK_IMAGE_VIEW_TYPE_1D_ARRAY ? VK_IMAGE_TYPE_1D : VK_IMAGE_TYPE_2D,
                            .format = VK_FORMAT_R8G8B8A8_UNORM, .extent = {1, 1, 1}, .mipLevels = 1, .arrayLayers = cube ? SF2C_TEXTURES_N_6_E7F6 : 1,
                            .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
                            .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT};
    VmaAllocationCreateInfo ai = {.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
    VK_CHECK(vmaCreateImage(g_vk.vma, &ci, &ai, &b->image, &b->mem, NULL));
    VkCommandBuffer cb = vk_upload_cmd();
    vk_image_init_layout(cb, b->image, VK_IMAGE_ASPECT_COLOR_BIT);
    VkClearColorValue zero = {{0, 0, 0, 0}};
    VkImageSubresourceRange all = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, cube ? SF2C_TEXTURES_N_6_E7F6 : 1};
    rec_CmdClearColorImage(cb, b->image, VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &all);
    b->format = VK_FORMAT_R8G8B8A8_UNORM;
    b->levels = 1;
    b->layers = cube ? SF2C_TEXTURES_N_6_E7F6 : 1;
    views[textype] = vk_image_view(b, vt, b->format, 0, 1, 0, cube ? SF2C_TEXTURES_N_6_E7F6 : 1, IDENTITY, VK_IMAGE_ASPECT_COLOR_BIT);
    return views[textype];
}

// Depth compare (sampler*Shadow) needs a view whose format has SAMPLED_IMAGE_DEPTH_COMPARISON: the RGBA8 dummy above
// has not (VUID-vkCmdDrawIndexed-None-06479). Shadow slots with nothing usable bound get a 1x1 depth image instead,
// cleared to 1.0: the farthest depth, so a less / less-equal compare reads it as lit (no shadow), as an empty shadow
// map would. D32_SFLOAT where it can be compared, else D16_UNORM. 3D (no shadow form) and 1D (depth 1D images are not
// required) types keep the colour dummy.
bool vk_format_depth_compare(VkFormat f) {
    static VkFormat fs[SF2C_TEXTURES_N_16_B17E];
    static int8_t ok[SF2C_TEXTURES_N_16_B17E];
    static int n;
    for (int i = 0; i < n; i++) if (fs[i] == f) return ok[i];
    VkFormatProperties3 p3 = {.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3};
    VkFormatProperties2 p2 = {.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2, .pNext = &p3};
    vkGetPhysicalDeviceFormatProperties2(g_vk.phys, f, &p2);
    bool r = (p3.optimalTilingFeatures & VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_DEPTH_COMPARISON_BIT) != 0;
    if (n < SF2C_TEXTURES_N_16_B17E) { fs[n] = f; ok[n++] = r; }
    return r;
}

// t's sampled image can be read with a depth compare (a depth image of a comparable format)
bool vk_tex_depth_compare(NvnTexture *t) {
    TexBackend *b = t ? (TexBackend *)t->backend : NULL;
    return b && (b->aspect & VK_IMAGE_ASPECT_DEPTH_BIT) && vk_format_depth_compare(b->format);
}

VkImageView vk_dummy_depth_view(int textype) {
    static TexBackend dummies[SF2C_TEXTURES_N_10_4A44];
    static VkImageView views[SF2C_TEXTURES_N_10_4A44];
    if (textype < 0 || textype >= SF2C_TEXTURES_N_10_4A44) textype = TEXTYPE_2D;
    if (views[textype]) return views[textype];
    VkImageViewType vt = view_type_for(textype);
    if (vt != VK_IMAGE_VIEW_TYPE_2D && vt != VK_IMAGE_VIEW_TYPE_2D_ARRAY && vt != VK_IMAGE_VIEW_TYPE_CUBE &&
        vt != VK_IMAGE_VIEW_TYPE_CUBE_ARRAY)
        return vk_dummy_view(textype);
    static VkFormat df;
    if (!df) {
        df = vk_format_depth_compare(VK_FORMAT_D32_SFLOAT) ? VK_FORMAT_D32_SFLOAT : VK_FORMAT_D16_UNORM;
        LOG(SF2C_TEXTURES_S_VK_SHADOW_SLOT_DUMMY_1X1_S_C_3133, df == VK_FORMAT_D32_SFLOAT ? SF2C_TEXTURES_S_D32_SFLOAT_AEF4 : SF2C_TEXTURES_S_D16_UNORM_0448);
    }
    bool cube = vt == VK_IMAGE_VIEW_TYPE_CUBE || vt == VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
    uint32_t layers = cube ? SF2C_TEXTURES_N_6_E7F6 : 1;
    TexBackend *b = &dummies[textype];
    VkImageCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .flags = cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0,
                            .imageType = VK_IMAGE_TYPE_2D, .format = df, .extent = {1, 1, 1}, .mipLevels = 1,
                            .arrayLayers = layers, .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
                            .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT};
    VmaAllocationCreateInfo ai = {.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
    if (vmaCreateImage(g_vk.vma, &ci, &ai, &b->image, &b->mem, NULL) != VK_SUCCESS) {
        LOG(SF2C_TEXTURES_S_VK_SHADOW_SLOT_DUMMY_DEPTH_I_CDAA);
        return views[textype] = vk_dummy_view(textype);
    }
    VkCommandBuffer cb = vk_upload_cmd();
    vk_image_init_layout(cb, b->image, VK_IMAGE_ASPECT_DEPTH_BIT);
    VkClearDepthStencilValue one = {SF2C_TEXTURES_N_1_0F_CA71, 0};
    VkImageSubresourceRange all = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, layers};
    rec_CmdClearDepthStencilImage(cb, b->image, VK_IMAGE_LAYOUT_GENERAL, &one, 1, &all);
    VkMemoryBarrier2 mb = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2, .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                           .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT, .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                           .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT};
    VkDependencyInfo di = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .memoryBarrierCount = 1, .pMemoryBarriers = &mb};
    rec_CmdPipelineBarrier2(cb, &di);
    b->format = df;
    b->levels = 1;
    b->layers = layers;
    b->aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
    views[textype] = vk_image_view(b, vt, df, 0, 1, 0, layers, IDENTITY, VK_IMAGE_ASPECT_DEPTH_BIT);
    if (!views[textype]) views[textype] = vk_dummy_view(textype);
    return views[textype];
}

// Throwaway color attachment for passes that have none (exec.c begin_pass, Intel only): R8, at least w x h (the pass's
// render area: the attachments' level extent, which Dynamic resolution's sub-rect lies inside), never sampled; its
// contents are don't-care. One image per sample count, grown to the largest render area seen (the outgrown one is
// destroyed once the submissions that may use it are done), so changing render scales don't pile up images. Only the
// image is kept: its view comes from the view table on each use, like a game target's (a cached handle could be
// evicted from that table and destroyed under us). Called by the translating thread only (the render thread, or the
// game's GPU thread with SF2RECOMP_RENDER_THREAD=0), as vk_dummy_view and the view table are.
VkImageView vk_dummy_color_view(uint32_t w, uint32_t h, uint32_t samples) {
    static TexBackend imgs[SF2C_TEXTURES_N_5_EF2D];  // samples 1, 2, 4, 8, 16
    int k = 0;
    while (k < SF2C_TEXTURES_N_4_4B22 && (SF2C_TEXTURES_N_1U_B8E6 << k) < samples) k++;
    TexBackend *b = &imgs[k];
    if (!b->image || b->width < w || b->height < h || b->samples != samples) {
        if (b->width > w) w = b->width;
        if (b->height > h) h = b->height;
        VkImageCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D,
                                .format = VK_FORMAT_R8_UNORM, .extent = {w, h, 1}, .mipLevels = 1, .arrayLayers = 1,
                                .samples = (VkSampleCountFlagBits)samples, .tiling = VK_IMAGE_TILING_OPTIMAL,
                                .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT};
        VmaAllocationCreateInfo ai = {.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
        VkImage img;
        VmaAllocation mem;
        if (vmaCreateImage(g_vk.vma, &ci, &ai, &img, &mem, NULL) != VK_SUCCESS) return VK_NULL_HANDLE;
        if (b->image) { views_forget(b); defer((Dead){.img = b->image, .mem = b->mem}); }
        vk_image_init_layout(vk_upload_cmd(), img, VK_IMAGE_ASPECT_COLOR_BIT);
        *b = (TexBackend){.image = img, .mem = mem, .format = VK_FORMAT_R8_UNORM, .base_type = VK_IMAGE_VIEW_TYPE_2D,
                          .width = w, .height = h, .depth = 1, .layers = 1, .levels = 1, .samples = samples,
                          .aspect = VK_IMAGE_ASPECT_COLOR_BIT, .gpu_owned = true};
        LOG(SF2C_TEXTURES_S_VK_PLACEHOLDER_COLOR_ATTACHM_6194, w, h, samples);
    }
    return vk_image_view(b, VK_IMAGE_VIEW_TYPE_2D, b->format, 0, 1, 0, 1, IDENTITY, VK_IMAGE_ASPECT_COLOR_BIT);
}

// ================================================================== samplers
static VkCompareOp compare_op(uint32_t f) {
    switch (f) {
    case 1: return VK_COMPARE_OP_NEVER;
    case SF2C_TEXTURES_N_2_D473: return VK_COMPARE_OP_LESS;
    case SF2C_TEXTURES_N_3_4E07: return VK_COMPARE_OP_EQUAL;
    case SF2C_TEXTURES_N_4_4B22: return VK_COMPARE_OP_LESS_OR_EQUAL;
    case SF2C_TEXTURES_N_5_EF2D: return VK_COMPARE_OP_GREATER;
    case SF2C_TEXTURES_N_6_E7F6: return VK_COMPARE_OP_NOT_EQUAL;
    case SF2C_TEXTURES_N_7_7902: return VK_COMPARE_OP_GREATER_OR_EQUAL;
    default: return VK_COMPARE_OP_ALWAYS;
    }
}

VkSampler vk_sampler(NvnSamplerDesc *d) {
    if (d->backend) return (VkSampler)d->backend;
    VkSamplerCreateInfo si = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.magFilter = d->mag_filter == 1 ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    uint32_t mf = d->min_filter;
    si.minFilter = (mf == 1 || mf == SF2C_TEXTURES_N_3_4E07 || mf == SF2C_TEXTURES_N_5_EF2D) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    si.mipmapMode = mf == SF2C_TEXTURES_N_4_4B22 || mf == SF2C_TEXTURES_N_5_EF2D ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    VkSamplerAddressMode modes[SF2C_TEXTURES_N_3_4E07];
    bool border = false;
    for (int i = 0; i < SF2C_TEXTURES_N_3_4E07; i++) {
        switch (d->wrap[i]) {
        case 0: case SF2C_TEXTURES_N_7_7902: modes[i] = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE; break;
        case 1: modes[i] = VK_SAMPLER_ADDRESS_MODE_REPEAT; break;
        case SF2C_TEXTURES_N_2_D473: case SF2C_TEXTURES_N_3_4E07: modes[i] = VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE; break;
        case SF2C_TEXTURES_N_4_4B22: case SF2C_TEXTURES_N_5_EF2D: modes[i] = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER; border = true; break;
        case SF2C_TEXTURES_N_6_E7F6: modes[i] = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT; break;
        default: modes[i] = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        }
    }
    si.addressModeU = modes[0]; si.addressModeV = modes[1]; si.addressModeW = modes[SF2C_TEXTURES_N_2_D473];
    si.minLod = d->lod_min;
    si.maxLod = d->lod_max > SF2C_TEXTURES_N_1000_4051 ? VK_LOD_CLAMP_NONE : d->lod_max;
    if (mf <= 1) si.maxLod = si.minLod + SF2C_TEXTURES_N_0_25F_17C9;  // not mipmapped: stay on the base level
    if (si.maxLod < si.minLod) si.maxLod = si.minLod;
    si.mipLodBias = d->lod_bias;
    if (d->max_aniso > 1) { si.anisotropyEnable = VK_TRUE; si.maxAnisotropy = fminf(d->max_aniso, SF2C_TEXTURES_N_16_B17E); }
    static float lodmin = -1;
    if (lodmin < 0) lodmin = getenv(SF2C_TEXTURES_S_SF2RECOMP_LOD_MIN_4349) ? (float)atof(getenv(SF2C_TEXTURES_S_SF2RECOMP_LOD_MIN_4349)) : 0;
    if (lodmin > 0 && mf > 1) si.minLod = fmaxf(si.minLod, lodmin);
    static int stest = -1;
    if (stest < 0) stest = getenv(SF2C_TEXTURES_S_SF2RECOMP_SAMPLER_TEST_F5E0) != NULL;
    if (stest && mf > 1) { si.minLod = 0; si.maxLod = VK_LOD_CLAMP_NONE; si.anisotropyEnable = VK_TRUE; si.maxAnisotropy = SF2C_TEXTURES_N_16_B17E; }
    if (d->compare_mode) {
        si.compareEnable = VK_TRUE;
        si.compareOp = getenv(SF2C_TEXTURES_S_SF2RECOMP_NO_SHADOW_1863) ? VK_COMPARE_OP_ALWAYS : compare_op(d->compare_func);
    }
    VkSamplerCustomBorderColorCreateInfoEXT cb = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CUSTOM_BORDER_COLOR_CREATE_INFO_EXT};
    if (border) {
        const float *c = d->border;
        if (c[0] == 0 && c[1] == 0 && c[SF2C_TEXTURES_N_2_D473] == 0 && c[SF2C_TEXTURES_N_3_4E07] == 0) si.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
        else if (c[0] == 0 && c[1] == 0 && c[SF2C_TEXTURES_N_2_D473] == 0 && c[SF2C_TEXTURES_N_3_4E07] == 1) si.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
        else if (c[0] == 1 && c[1] == 1 && c[SF2C_TEXTURES_N_2_D473] == 1 && c[SF2C_TEXTURES_N_3_4E07] == 1) si.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        else if (g_vk.custom_border) {
            memcpy(cb.customBorderColor.float32, c, SF2C_TEXTURES_N_16_B17E);
            si.borderColor = VK_BORDER_COLOR_FLOAT_CUSTOM_EXT;
            si.pNext = &cb;
        } else si.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    }
    VkSampler s;
    VK_CHECK(vkCreateSampler(g_vk.dev, &si, NULL, &s));
    d->backend = (void *)s;
    return s;
}

VkSampler vk_default_sampler(bool linear) {
    static VkSampler s[SF2C_TEXTURES_N_2_D473];
    if (!s[linear]) {
        VkSamplerCreateInfo si = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                                  .magFilter = linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST,
                                  .minFilter = linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST,
                                  .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                  .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                  .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, .maxLod = SF2C_TEXTURES_N_0_25F_17C9};
        VK_CHECK(vkCreateSampler(g_vk.dev, &si, NULL, &s[linear]));
    }
    return s[linear];
}

void backend_texture_view_format(NvnTexture *t, uint32_t format) { (void)t; (void)format; }  // (images are made mutable-format)
