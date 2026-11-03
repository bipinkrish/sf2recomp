#include "../constants/hle.h"
// Switch screen modes, graphics-device descriptors and NVN backing storage must
// agree before Unity creates cameras/canvases. Every site checks its original
// instruction in patches.py. Disabled mode preserves all guest state.
// Render's reference width drives PvP camera scale/center and combat margins.
// Keep that entire framing calculation at the authored 16:9 aspect. Native
// display dimensions and the actual viewport still use the selected aspect.
static uint64_t widescreen_pvp_render;
static void widescreen_combat_framing(Cpu *c) {
    widescreen_pvp_render = 0;
    if (!settings_custom_aspect()) return;
    Cpu saved = *c;
    c->x[0] = 0; c->x[1] = 0;
    guest_call(c, SF2C_WIDESCREEN_HOOKS_N_0X7000C91C70ULL_A0A1); // Fight.CurrentFight (also used later in Render.Init)
    uint64_t fight = c->x[0];
    uint64_t list = fight ? LD64(fight + SF2C_WIDESCREEN_HOOKS_N_0X30_8BAD) : 0;
    bool pvp = list && LD32(list + SF2C_WIDESCREEN_HOOKS_N_0X18_D47C) == SF2C_WIDESCREEN_HOOKS_N_18_4EC9;
    *c = saved;
    if (!pvp) return;
    widescreen_pvp_render = c->x[SF2C_WIDESCREEN_HOOKS_N_19_9400];
    float location_scale;
    uint32_t scale_bits = LD32(c->x[SF2C_WIDESCREEN_HOOKS_N_19_9400] + SF2C_WIDESCREEN_HOOKS_N_0X78_756D);
    memcpy(&location_scale, &scale_bits, sizeof location_scale);
    c->x[0] = 0; c->x[1] = 0;
    guest_call(c, SF2C_WIDESCREEN_HOOKS_N_0X70000AD270ULL_0DAF); // SystemProperties.ScreenHeight, as above in Render.Init
    uint32_t height = (uint32_t)c->x[0];
    *c = saved;
    // Match native integer-pixel -> float -> location-scale division order.
    // The original next instructions store ScreenWidth and derive MinScale;
    // subsequent scale, center, facing-sensitive margins and clamps stay native.
    float width = (float)(height * SF2C_WIDESCREEN_HOOKS_N_16U_1FB3 / SF2C_WIDESCREEN_HOOKS_N_9U_DE66) / location_scale;
    uint32_t bits;
    memcpy(&bits, &width, sizeof bits);
    c->v[0] = bits;
}

static bool widescreen_backdrop(uint64_t layer) {
    if (g_settings.aspect != ASPECT_32_9 || !layer) return false;
    uint32_t type = LD32(layer + SF2C_WIDESCREEN_HOOKS_N_0X34_806E);
    return type == 1 || type == SF2C_WIDESCREEN_HOOKS_N_3_4E07;
}

// Zooming foreground scenery around zero moves its authored floor below the
// fighters. Translate by the extra scaled floor offset so both share a baseline.
// The camera passes an absolute scale on every update; offsets never accumulate.
static void widescreen_floor(Cpu *c, uint64_t layer, uint64_t location, float scale) {
    if (!widescreen_backdrop(layer) || !LD8(layer + SF2C_WIDESCREEN_HOOKS_N_0X30_8BAD)) return;
    float height, margin;
    memcpy(&height, (void *)(uintptr_t)(location + SF2C_WIDESCREEN_HOOKS_N_0X44_E723), sizeof height);
    memcpy(&margin, (void *)(uintptr_t)(location + SF2C_WIDESCREEN_HOOKS_N_0X34_806E), sizeof margin);
    // RenderContainer.Init places model feet at margin - height/2. The stage
    // parent flips Y; offset the scenery by the opposite authored coordinate.
    float y = (height * SF2C_WIDESCREEN_HOOKS_N_5F_33EA - margin) * scale;
    Cpu saved = *c;
    c->x[0] = layer; c->x[1] = 0;
    memcpy(&c->v[0], &y, sizeof y);
    guest_call(c, SF2C_WIDESCREEN_HOOKS_N_0X7001033F40ULL_4F4B); // LocationSelector.SetPositionY
    *c = saved;
}

// Stage artwork was authored for 16:9. Cover 32:9 by enlarging only scenery
// uniformly (cropping vertically); LayerGame contains the fighters and effects.
// SetScale receives an absolute scale, so camera updates never accumulate zoom.
static void widescreen_backgrounds(Cpu *c) {
    if (g_settings.aspect != ASPECT_32_9) return;
    Cpu saved = *c;
    uint64_t location = LD64(c->x[SF2C_WIDESCREEN_HOOKS_N_19_9400] + SF2C_WIDESCREEN_HOOKS_N_0XA0_3862);
    uint64_t list = LD64(location + SF2C_WIDESCREEN_HOOKS_N_0X70_B0F6), items = LD64(list + SF2C_WIDESCREEN_HOOKS_N_0X10_BF9C);
    uint32_t count = LD32(list + SF2C_WIDESCREEN_HOOKS_N_0X18_D47C);
    unsigned covered = 0;
    for (uint32_t i = 0; i < count; ++i) {
        uint64_t layer = LD64(items + SF2C_WIDESCREEN_HOOKS_N_0X20_A766 + i * SF2C_WIDESCREEN_HOOKS_N_8_2C62);
        if (!widescreen_backdrop(layer)) continue;
        float scale = 1;
        widescreen_floor(c, layer, location, scale);
        c->x[0] = layer; c->x[1] = 0;
        memcpy(&c->v[0], &scale, sizeof scale);
        guest_call(c, SF2C_WIDESCREEN_HOOKS_N_0X7001033E70ULL_BDFF); // LocationSelector.SetScale; hook below applies cover.
        covered++;
        *c = saved;
    }
    LOG(SF2C_WIDESCREEN_HOOKS_S_WIDE_32_9_BACKGROUND_COVER_U_E57D, covered);
    *c = saved;
}

static uint64_t widescreen_invoke2(Cpu *c,uint64_t method,uint64_t object,uint64_t argument) {
    Cpu saved=*c;c->x[0]=object;c->x[1]=argument;guest_call(c,method);
    uint64_t result=c->x[0];*c=saved;return result;
}
static bool widescreen_named(Cpu *c,uint64_t object,const char *name) {
    uint64_t text=widescreen_invoke2(c,SF2C_WIDESCREEN_HOOKS_N_0X700122E460ULL_C805,object,0);
    if(!text || LD32(text+SF2C_WIDESCREEN_HOOKS_N_0X10_BF9C)!=strlen(name))return false;
    for(unsigned i=0;name[i];++i)if(LD16(text+SF2C_WIDESCREEN_HOOKS_N_0X14_F942+i*SF2C_WIDESCREEN_HOOKS_N_2_D473)!=(unsigned char)name[i])return false;
    return true;
}

// Sparring's scenery consists of two authored halves under this transform.
// Cover the wider canvas without resizing the roster, selectors or headers.
static void widescreen_sparring_background(Cpu *c,uint64_t module) {
    if(!settings_custom_aspect())return;
    uint64_t root=widescreen_invoke2(c,SF2C_WIDESCREEN_HOOKS_N_0X700121E6B0ULL_59E6,module,0);
    for(unsigned i=0;i<SF2C_WIDESCREEN_HOOKS_N_4_4B22 && root;++i) {
        if(widescreen_named(c,root,SF2C_WIDESCREEN_HOOKS_S_PVPSCENE_1313))break;
        root=widescreen_invoke2(c,SF2C_WIDESCREEN_HOOKS_N_0X70015D8BE0ULL_6A41,root,0);
    }
    if(!root || !widescreen_named(c,root,SF2C_WIDESCREEN_HOOKS_S_PVPSCENE_1313))return;
    unsigned count=widescreen_invoke2(c,SF2C_WIDESCREEN_HOOKS_N_0X70015D9E00ULL_574A,root,0);
    for(unsigned i=0;i<count;++i) {
        uint64_t background=widescreen_invoke2(c,SF2C_WIDESCREEN_HOOKS_N_0X70015DA1F0ULL_3A2D,root,i);
        if(!widescreen_named(c,background,SF2C_WIDESCREEN_HOOKS_S_BACKGROUND_7D67))continue;
        float scale[]={fmaxf(1,settings_screen_width(SF2C_WIDESCREEN_HOOKS_N_1080_32EB)/SF2C_WIDESCREEN_HOOKS_N_1920_0F_EBC7),1,1};
        Cpu saved=*c;c->x[0]=background;c->x[1]=0;
        for(unsigned j=0;j<SF2C_WIDESCREEN_HOOKS_N_3_4E07;++j)memcpy(&c->v[j],scale+j,SF2C_WIDESCREEN_HOOKS_N_4_4B22);
        guest_call(c,SF2C_WIDESCREEN_HOOKS_N_0X70015D8AE0ULL_58A3);*c=saved;
        return;
    }
}

static int widescreen_hook(Cpu *c, uint64_t site) {
    switch (site) {
    case SF2C_WIDESCREEN_HOOKS_N_0X7000E43000ULL_EAF7:
        widescreen_sparring_background(c,c->x[0]);return 0;
    case SF2C_WIDESCREEN_HOOKS_N_0X7000EAD4C0ULL_12E3: {
        float scale;
        memcpy(&scale, &c->v[0], sizeof scale);
        if (g_settings.aspect == ASPECT_32_9)
            widescreen_floor(c, c->x[0], LD64(c->x[SF2C_WIDESCREEN_HOOKS_N_19_9400] + SF2C_WIDESCREEN_HOOKS_N_0XA0_3862), scale);
        return 0;
    }
    case SF2C_WIDESCREEN_HOOKS_N_0X7000EABF0CULL_9F19:
        widescreen_combat_framing(c);
        return 0;
    case SF2C_WIDESCREEN_HOOKS_N_0X7000EABFE0ULL_0239:
        // Render uses screen width to choose its fight zoom and minimum scale.
        // Expanding that reference also zooms the fighters vertically, pushing
        // their feet and ground markers below the viewport. Keep the authored
        // 16:9 fight framing while the actual camera/output reveal more sides.
        if (settings_custom_aspect() && settings_screen_width(SF2C_WIDESCREEN_HOOKS_N_1080_32EB)>SF2C_WIDESCREEN_HOOKS_N_1920_6B5F &&
            c->x[SF2C_WIDESCREEN_HOOKS_N_19_9400] != widescreen_pvp_render) {
            float factor = SF2C_WIDESCREEN_HOOKS_N_1920_0F_EBC7 / settings_screen_width(SF2C_WIDESCREEN_HOOKS_N_1080_32EB);
            float width, minimum;
            memcpy(&width, (void *)(uintptr_t)(c->x[SF2C_WIDESCREEN_HOOKS_N_19_9400] + SF2C_WIDESCREEN_HOOKS_N_0X84_7CD7), sizeof width);
            memcpy(&minimum, (void *)(uintptr_t)(c->x[SF2C_WIDESCREEN_HOOKS_N_19_9400] + SF2C_WIDESCREEN_HOOKS_N_0X88_AC70), sizeof minimum);
            width *= factor; minimum *= factor;
            memcpy((void *)(uintptr_t)(c->x[SF2C_WIDESCREEN_HOOKS_N_19_9400] + SF2C_WIDESCREEN_HOOKS_N_0X84_7CD7), &width, sizeof width);
            memcpy((void *)(uintptr_t)(c->x[SF2C_WIDESCREEN_HOOKS_N_19_9400] + SF2C_WIDESCREEN_HOOKS_N_0X88_AC70), &minimum, sizeof minimum);
            LOG(SF2C_WIDESCREEN_HOOKS_S_WIDE_FIGHT_FRAMING_WIDTH_3F__8260, width, minimum);
        }
        widescreen_backgrounds(c);
        return 0;
    case SF2C_WIDESCREEN_HOOKS_N_0X7001033E70ULL_BDFF:
        if (widescreen_backdrop(c->x[0])) {
            float scale;
            memcpy(&scale, &c->v[0], sizeof scale); scale *= SF2C_WIDESCREEN_HOOKS_N_2_D473;
            memcpy(&c->v[0], &scale, sizeof scale);
        }
        return 0;
    case SF2C_WIDESCREEN_HOOKS_N_0X70015C8948ULL_DC2E:
        if (settings_custom_aspect()) {
            float aspect = settings_screen_width(SF2C_WIDESCREEN_HOOKS_N_1080_32EB) / SF2C_WIDESCREEN_HOOKS_N_1080_0F_5C78;
            memcpy(&c->v[1], &aspect, sizeof aspect);
        }
        return 0; // Run's original non-pillarboxed path hides both border objects.
    case SF2C_WIDESCREEN_HOOKS_N_0X70019016C0ULL_6A69:
        if (!settings_custom_aspect()) return 0;
        ST32(c->x[0], settings_screen_width(SF2C_WIDESCREEN_HOOKS_N_1080_32EB));
        ST32(c->x[1], SF2C_WIDESCREEN_HOOKS_N_1080_32EB);
        return 1;
    case SF2C_WIDESCREEN_HOOKS_N_0X700190160CULL_DF0A:
        if (settings_custom_aspect()) c->x[SF2C_WIDESCREEN_HOOKS_N_2_D473] = settings_screen_width(SF2C_WIDESCREEN_HOOKS_N_1080_32EB);
        return 0;
    case SF2C_WIDESCREEN_HOOKS_N_0X700191177CULL_44EC:
    case SF2C_WIDESCREEN_HOOKS_N_0X7001911810ULL_B9F6:
    case SF2C_WIDESCREEN_HOOKS_N_0X70019119CCULL_1CCD:
        if (settings_custom_aspect()) c->x[1] = settings_screen_width(SF2C_WIDESCREEN_HOOKS_N_1080_32EB);
        return 0;
    case SF2C_WIDESCREEN_HOOKS_N_0X70015FBB24ULL_3732:
        if (settings_custom_aspect()) c->x[SF2C_WIDESCREEN_HOOKS_N_9_1958] = settings_screen_width(SF2C_WIDESCREEN_HOOKS_N_720_D829);
        return 0;
    case SF2C_WIDESCREEN_HOOKS_N_0X70015FBCF0ULL_4B94:
        if (settings_custom_aspect()) {
            c->x[SF2C_WIDESCREEN_HOOKS_N_3_4E07] = settings_screen_width(c->x[SF2C_WIDESCREEN_HOOKS_N_4_4B22]);
            LOG(SF2C_WIDESCREEN_HOOKS_S_WIDE_DISPLAY_MODE_LLU_X_LLU__4AC7, c->x[SF2C_WIDESCREEN_HOOKS_N_3_4E07], c->x[SF2C_WIDESCREEN_HOOKS_N_4_4B22]);
        }
        return 0;
    default: return -1;
    }
}
