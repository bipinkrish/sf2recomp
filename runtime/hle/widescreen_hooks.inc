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
    guest_call(c, 0x7000c91c70ull); // Fight.CurrentFight (also used later in Render.Init)
    uint64_t fight = c->x[0];
    uint64_t list = fight ? LD64(fight + 0x30) : 0;
    bool pvp = list && LD32(list + 0x18) == 18;
    *c = saved;
    if (!pvp) return;
    widescreen_pvp_render = c->x[19];
    float location_scale;
    uint32_t scale_bits = LD32(c->x[19] + 0x78);
    memcpy(&location_scale, &scale_bits, sizeof location_scale);
    c->x[0] = 0; c->x[1] = 0;
    guest_call(c, 0x70000ad270ull); // SystemProperties.ScreenHeight, as above in Render.Init
    uint32_t height = (uint32_t)c->x[0];
    *c = saved;
    // Match native integer-pixel -> float -> location-scale division order.
    // The original next instructions store ScreenWidth and derive MinScale;
    // subsequent scale, center, facing-sensitive margins and clamps stay native.
    float width = (float)(height * 16u / 9u) / location_scale;
    uint32_t bits;
    memcpy(&bits, &width, sizeof bits);
    c->v[0] = bits;
}

static bool widescreen_backdrop(uint64_t layer) {
    if (g_settings.aspect != ASPECT_32_9 || !layer) return false;
    uint32_t type = LD32(layer + 0x34);
    return type == 1 || type == 3;
}

// Zooming foreground scenery around zero moves its authored floor below the
// fighters. Translate by the extra scaled floor offset so both share a baseline.
// The camera passes an absolute scale on every update; offsets never accumulate.
static void widescreen_floor(Cpu *c, uint64_t layer, uint64_t location, float scale) {
    if (!widescreen_backdrop(layer) || !LD8(layer + 0x30)) return;
    float height, margin;
    memcpy(&height, (void *)(uintptr_t)(location + 0x44), sizeof height);
    memcpy(&margin, (void *)(uintptr_t)(location + 0x34), sizeof margin);
    // RenderContainer.Init places model feet at margin - height/2. The stage
    // parent flips Y; offset the scenery by the opposite authored coordinate.
    float y = (height * .5f - margin) * scale;
    Cpu saved = *c;
    c->x[0] = layer; c->x[1] = 0;
    memcpy(&c->v[0], &y, sizeof y);
    guest_call(c, 0x7001033f40ull); // LocationSelector.SetPositionY
    *c = saved;
}

// Stage artwork was authored for 16:9. Cover 32:9 by enlarging only scenery
// uniformly (cropping vertically); LayerGame contains the fighters and effects.
// SetScale receives an absolute scale, so camera updates never accumulate zoom.
static void widescreen_backgrounds(Cpu *c) {
    if (g_settings.aspect != ASPECT_32_9) return;
    Cpu saved = *c;
    uint64_t location = LD64(c->x[19] + 0xa0);
    uint64_t list = LD64(location + 0x70), items = LD64(list + 0x10);
    uint32_t count = LD32(list + 0x18);
    unsigned covered = 0;
    for (uint32_t i = 0; i < count; ++i) {
        uint64_t layer = LD64(items + 0x20 + i * 8);
        if (!widescreen_backdrop(layer)) continue;
        float scale = 1;
        widescreen_floor(c, layer, location, scale);
        c->x[0] = layer; c->x[1] = 0;
        memcpy(&c->v[0], &scale, sizeof scale);
        guest_call(c, 0x7001033e70ull); // LocationSelector.SetScale; hook below applies cover.
        covered++;
        *c = saved;
    }
    LOG("[wide] 32:9 background cover: %u scenery layers at 2x\n", covered);
    *c = saved;
}

static uint64_t widescreen_invoke2(Cpu *c,uint64_t method,uint64_t object,uint64_t argument) {
    Cpu saved=*c;c->x[0]=object;c->x[1]=argument;guest_call(c,method);
    uint64_t result=c->x[0];*c=saved;return result;
}
static bool widescreen_named(Cpu *c,uint64_t object,const char *name) {
    uint64_t text=widescreen_invoke2(c,0x700122e460ull,object,0);
    if(!text || LD32(text+0x10)!=strlen(name))return false;
    for(unsigned i=0;name[i];++i)if(LD16(text+0x14+i*2)!=(unsigned char)name[i])return false;
    return true;
}

// Sparring's scenery consists of two authored halves under this transform.
// Cover the wider canvas without resizing the roster, selectors or headers.
static void widescreen_sparring_background(Cpu *c,uint64_t module) {
    if(!settings_custom_aspect())return;
    uint64_t root=widescreen_invoke2(c,0x700121e6b0ull,module,0);
    for(unsigned i=0;i<4 && root;++i) {
        if(widescreen_named(c,root,"[PvpScene]"))break;
        root=widescreen_invoke2(c,0x70015d8be0ull,root,0);
    }
    if(!root || !widescreen_named(c,root,"[PvpScene]"))return;
    unsigned count=widescreen_invoke2(c,0x70015d9e00ull,root,0);
    for(unsigned i=0;i<count;++i) {
        uint64_t background=widescreen_invoke2(c,0x70015da1f0ull,root,i);
        if(!widescreen_named(c,background,"Background"))continue;
        float scale[]={fmaxf(1,settings_screen_width(1080)/1920.0f),1,1};
        Cpu saved=*c;c->x[0]=background;c->x[1]=0;
        for(unsigned j=0;j<3;++j)memcpy(&c->v[j],scale+j,4);
        guest_call(c,0x70015d8ae0ull);*c=saved;
        return;
    }
}

static int widescreen_hook(Cpu *c, uint64_t site) {
    switch (site) {
    case 0x7000e43000ull:
        widescreen_sparring_background(c,c->x[0]);return 0;
    case 0x7000ead4c0ull: {
        float scale;
        memcpy(&scale, &c->v[0], sizeof scale);
        if (g_settings.aspect == ASPECT_32_9)
            widescreen_floor(c, c->x[0], LD64(c->x[19] + 0xa0), scale);
        return 0;
    }
    case 0x7000eabf0cull:
        widescreen_combat_framing(c);
        return 0;
    case 0x7000eabfe0ull:
        // Render uses screen width to choose its fight zoom and minimum scale.
        // Expanding that reference also zooms the fighters vertically, pushing
        // their feet and ground markers below the viewport. Keep the authored
        // 16:9 fight framing while the actual camera/output reveal more sides.
        if (settings_custom_aspect() && settings_screen_width(1080)>1920 &&
            c->x[19] != widescreen_pvp_render) {
            float factor = 1920.0f / settings_screen_width(1080);
            float width, minimum;
            memcpy(&width, (void *)(uintptr_t)(c->x[19] + 0x84), sizeof width);
            memcpy(&minimum, (void *)(uintptr_t)(c->x[19] + 0x88), sizeof minimum);
            width *= factor; minimum *= factor;
            memcpy((void *)(uintptr_t)(c->x[19] + 0x84), &width, sizeof width);
            memcpy((void *)(uintptr_t)(c->x[19] + 0x88), &minimum, sizeof minimum);
            LOG("[wide] fight framing: width %.3f, minimum scale %.3f\n", width, minimum);
        }
        widescreen_backgrounds(c);
        return 0;
    case 0x7001033e70ull:
        if (widescreen_backdrop(c->x[0])) {
            float scale;
            memcpy(&scale, &c->v[0], sizeof scale); scale *= 2;
            memcpy(&c->v[0], &scale, sizeof scale);
        }
        return 0;
    case 0x70015c8948ull:
        if (settings_custom_aspect()) {
            float aspect = settings_screen_width(1080) / 1080.0f;
            memcpy(&c->v[1], &aspect, sizeof aspect);
        }
        return 0; // Run's original non-pillarboxed path hides both border objects.
    case 0x70019016c0ull:
        if (!settings_custom_aspect()) return 0;
        ST32(c->x[0], settings_screen_width(1080));
        ST32(c->x[1], 1080);
        return 1;
    case 0x700190160cull:
        if (settings_custom_aspect()) c->x[2] = settings_screen_width(1080);
        return 0;
    case 0x700191177cull:
    case 0x7001911810ull:
    case 0x70019119ccull:
        if (settings_custom_aspect()) c->x[1] = settings_screen_width(1080);
        return 0;
    case 0x70015fbb24ull:
        if (settings_custom_aspect()) c->x[9] = settings_screen_width(720);
        return 0;
    case 0x70015fbcf0ull:
        if (settings_custom_aspect()) {
            c->x[3] = settings_screen_width(c->x[4]);
            LOG("[wide] display mode %llu x %llu\n", c->x[3], c->x[4]);
        }
        return 0;
    default: return -1;
    }
}
