// Switch screen modes, graphics-device descriptors and NVN backing storage must
// agree before Unity creates cameras/canvases. Every site checks its original
// instruction in patches.py. Disabled mode preserves all guest state.
static bool widescreen_backdrop(uint64_t layer) {
    if (g_settings.aspect != ASPECT_32_9 || !layer) return false;
    uint32_t type = LD32(layer + 0x34);
    return type == 1 || type == 3;
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
        c->x[0] = layer; c->x[1] = 0;
        memcpy(&c->v[0], &scale, sizeof scale);
        guest_call(c, 0x7001033e70ull); // LocationSelector.SetScale; hook below applies cover.
        covered++;
        *c = saved;
    }
    LOG("[wide] 32:9 background cover: %u scenery layers at 2x\n", covered);
    *c = saved;
}

static int widescreen_hook(Cpu *c, uint64_t site) {
    switch (site) {
    case 0x7000eabfe0ull:
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
