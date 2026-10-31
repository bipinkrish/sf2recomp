// Switch screen modes, graphics-device descriptors and NVN backing storage must
// agree before Unity creates cameras/canvases. Every site checks its original
// instruction in patches.py. Disabled mode preserves all guest state.
static int widescreen_hook(Cpu *c, uint64_t site) {
    switch (site) {
    case 0x70015c8948ull:
        if (g_settings.aspect == ASPECT_21_9) {
            float aspect = 7.0f / 3.0f;
            memcpy(&c->v[1], &aspect, sizeof aspect);
        }
        return 0; // Run's original non-pillarboxed path hides both border objects.
    case 0x70019016c0ull:
        if (g_settings.aspect != ASPECT_21_9) return 0;
        ST32(c->x[0], 2520);
        ST32(c->x[1], 1080);
        return 1;
    case 0x700190160cull:
        if (g_settings.aspect == ASPECT_21_9) c->x[2] = 2520;
        return 0;
    case 0x700191177cull:
    case 0x7001911810ull:
    case 0x70019119ccull:
        if (g_settings.aspect == ASPECT_21_9) c->x[1] = 2520;
        return 0;
    case 0x70015fbb24ull:
        if (g_settings.aspect == ASPECT_21_9) c->x[9] = 1680;
        return 0;
    case 0x70015fbcf0ull:
        if (g_settings.aspect == ASPECT_21_9) {
            c->x[3] = c->x[4] * 7 / 3;
            LOG("[wide] display mode %llu x %llu\n", c->x[3], c->x[4]);
        }
        return 0;
    default: return -1;
    }
}
