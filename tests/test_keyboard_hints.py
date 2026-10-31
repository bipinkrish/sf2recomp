"""Image-only hints must gain mapped labels and restore controller artwork."""
import pathlib
import subprocess
import tempfile
import unittest
from test_settings import compile_harness


class KeyboardHintTests(unittest.TestCase):
    def test_image_only_label_and_restore(self):
        source = r'''
#include <assert.h>
#include <stdlib.h>
#include <SDL3/SDL.h>
#include "hle/hooks.c"
static Cpu *active_cpu;
void *hle_alloc(size_t size) { return calloc(1, size); }
bool rt_probe_u64(uint64_t a, uint64_t *v) { *v = 0x9000; return true; }
bool nvn_texture_region_color(const NvnTexture *t, uint32_t x, uint32_t y,
                              uint32_t w, uint32_t h, float rgb[3]) {
    assert((uintptr_t)t == 0x9000 && x == 8 && y == 12 && w == 32 && h == 40);
    rgb[0] = 0.5f; rgb[1] = 0.25f; rgb[2] = 0.125f;
    return true;
}
static bool image_enabled = true, text_enabled, layout_enabled, missing_font, enclosed, outline_enabled, no_background, horizontal_layout, oversized;
static int font_size, vertices, triangles;
static float label_x, layout_width, label_width;
static uint64_t last_go_component, layout_owner;
static float outline_color[4], mesh_xy[256][2];
static unsigned mesh_color[256];
static char displayed[128];
static float label_color[4];
static uint64_t fake_game_object[1] = {0x8000}, fake_types[5];
bool input_any_gamepad_connected(void) { return false; }
void guest_call(Cpu *c, uint64_t pc) {
    assert(c == active_cpu); // nested calls retain the kernel's current CPU
    switch (pc) {
    case 0x700011d4a0ull: c->x[0] = c->x[0] == 0xd100 && no_background ? 0 : 0xa000; return;
    case 0x70015cf640ull: c->x[0] = 0xb000; return;
    case 0x7000b7cd80ull: c->x[0] = 0xc000; return;
    case 0xc000: c->x[0] = GUEST_LO + 0x100; return;
    case 0x70015cf720ull: {
        float rect[] = {8, 12, 32, 40};
        for (int i = 0; i < 4; ++i) memcpy(&c->v[i], &rect[i], sizeof(float));
        return;
    }
    case GUEST_STRING_ALLOC:
        c->x[0] = (uintptr_t)calloc(1, 0x16 + c->x[1] * 2); return;
    case 0x70005d8220ull: c->x[0] = 0xd100; return;
    case 0x700121e790ull:
        c->x[0] = c->x[1] == 0x30 ? c->x[0] : horizontal_layout && c->x[1] == 0x50 && c->x[0] == 0x2000 ? 0xd200 : 0;
        return;
    case 0x700121ead0ull: c->x[0] = enclosed ? 0xd000 : 0; return;
    case GUEST_BEHAVIOUR_GET_ENABLED: c->x[0] = c->x[0] == 0xd100 ? true : image_enabled; return;
    case 0x70008ea5c0ull:
        { unsigned ch = LD16(c->x[1] + 0x14 + 15 * 2);
        c->x[0] = ch == 'L' ? 0x20 : ch == 'I' ? 0x30 : ch == 'B' ? 0x40 : ch == 'H' ? 0x50 : 0x10; }
        return; // LayoutElement vs Text/RectTransform/System.Type
    case 0x700121e6b0ull: c->x[0] += 0x100; return; // transform
    case 0x70015d8be0ull: c->x[0] = c->x[0] == 0x1100 ? 0x2000 : 0; return;
    case 0x700121e970ull: c->x[0] = missing_font ? 0 : 0x3000; return;
    case 0x70005bce40ull: c->x[0] = 0x6000; return;
    case 0x700121e720ull: last_go_component = c->x[0]; c->x[0] = (uintptr_t)fake_game_object; return;
    case 0x7000b5b110ull: c->x[0] = 0x5000; return;
    case 0x70006e6740ull: c->x[0] = (uintptr_t)fake_types; return;
    case 0x7001225f80ull:
        assert(c->x[0] == 0x5000 && c->x[2] == (uintptr_t)fake_types && fake_types[4] == 0x10);
        return;
    case 0x70012260e0ull: if (c->x[1] == 0x20) layout_owner = last_go_component; c->x[0] = c->x[1] == 0x20 ? 0x7000 : c->x[1] == 0x30 ? 0xe000 : 0x4000; return;
    case 0x70005dd1e0ull: c->x[0] = 70; return;
    case 0x70005c2380ull: memcpy(&layout_width, &c->v[0], sizeof layout_width); return;
    case 0x700123a7d0ull:
        if (c->x[0] == 0x4100) memcpy(&label_width, &c->v[0], sizeof label_width);
        return;
    case 0x700123a550ull:
        if (c->x[0] == 0x4100) memcpy(&label_x, &c->v[0], sizeof label_x);
        return;
    case 0x70005dd1f0ull: font_size = c->x[1]; return;
    case 0x70005dde50ull: { float width = oversized ? font_size * 10 : strlen(displayed) == 1 ? 32 : 100; memcpy(&c->v[0], &width, sizeof width); return; }
    case 0x7000114f80ull: {
        float color[] = {0.4f, 0.2f, 0.1f, 1.0f}; // caption has a different theme
        if (c->x[0] == 0x1000) {
            color[0] = 0.15f; color[1] = 0.3f; color[2] = 0.45f; color[3] = 0.8f;
        }
        if (c->x[0] == 0xe000) memcpy(color, outline_color, sizeof color);
        for (int i = 0; i < 4; ++i) memcpy(&c->v[i], &color[i], sizeof(float));
        return;
    }
    case 0x7000114f90ull:
        if (c->x[0] == 0x4000)
            for (int i = 0; i < 4; ++i) memcpy(&label_color[i], &c->v[i], sizeof(float));
        if (c->x[0] == 0xe000)
            for (int i = 0; i < 4; ++i) memcpy(&outline_color[i], &c->v[i], sizeof(float));
        return;
    case 0x7000aed470ull: vertices = triangles = 0; return;
    case 0x7000aedd60ull:
        assert(vertices < 256 && c->x[0] == 0xf000);
        for (int i = 0; i < 2; ++i) memcpy(&mesh_xy[vertices][i], &c->v[i], sizeof(float));
        mesh_color[vertices++] = c->x[1]; return;
    case 0x7000aedf00ull:
        assert(c->x[0] == 0xf000 && c->x[1] < vertices && c->x[2] < vertices && c->x[3] < vertices);
        ++triangles; return;
    case GUEST_TEXT_SET: {
        uint32_t n = LD32(c->x[1] + 0x10);
        assert(n < sizeof displayed);
        for (unsigned i = 0; i < n; i++) displayed[i] = LD16(c->x[1] + 0x14 + i * 2);
        displayed[n] = 0; return;
    }
    case GUEST_BEHAVIOUR_SET_ENABLED:
        if (c->x[0] == 0x1000) image_enabled = c->x[1];
        else if (c->x[0] == 0x4000) text_enabled = c->x[1];
        else if (c->x[0] == 0x7000) layout_enabled = c->x[1];
        else if (c->x[0] == 0xe000) outline_enabled = c->x[1];
        return;
    default: return; // rect, font, and raycast setters
    }
}
int main(void) {
    Cpu cpu = {0}; cpu.x[19] = 0x12345; active_cpu = &cpu;
    g_settings.prompts = PROMPTS_KEYBOARD;
    assert(!g_settings.experimental_keyboard_prompts && !keyboard_prompts_active());
    apply_image_label(&cpu, 0x1000, HINT_A);
    assert(image_enabled && !text_enabled && hint_images_used == 0);
    g_settings.experimental_keyboard_prompts = true;
    g_bind_key[BIND_A] = SDL_SCANCODE_SPACE;
    missing_font = true;
    apply_image_label(&cpu, 0x1000, HINT_A);
    assert(image_enabled); // failure must preserve useful original artwork
    missing_font = false;
    apply_image_label(&cpu, 0x1000, HINT_A);
    assert(!image_enabled && text_enabled && layout_enabled && !strcmp(displayed, "Space"));
    assert(cpu.x[19] == 0x12345);
    assert(label_color[0] == 0.15f * 0.5f && label_color[1] == 0.3f * 0.25f && label_color[2] == 0.45f * 0.125f);
    assert(label_color[3] == 0.8f);
    float fade[] = {0.1f, 0.2f, 0.3f, 0.5f};
    cpu.x[0] = 0x3000;
    for (int i = 0; i < 4; ++i) memcpy(&cpu.v[i], &fade[i], sizeof(float));
    update_hint_color(&cpu); // changing the caption must not recolor the key
    assert(label_color[0] == 0.15f * 0.5f && label_color[3] == 0.8f);
    cpu.x[0] = 0x1000;
    update_hint_color(&cpu); // changing the replaced icon must update the key
    assert(label_color[0] == fade[0] * 0.5f && label_color[1] == fade[1] * 0.25f);
    assert(label_color[2] == fade[2] * 0.125f && label_color[3] == fade[3]);
    assert(cpu.x[0] == 0x1000);
    assert(!outline_enabled && !hint_image_state(&cpu, 0x1000, false)->keyboard_outline);
    enclosed = true; no_background = true;
    apply_image_label(&cpu, 0x1000, HINT_A);
    assert(!outline_enabled); // footer legends have an invisible Button, not visible button art
    no_background = false;
    g_bind_key[BIND_A] = SDL_SCANCODE_K;
    apply_image_label(&cpu, 0x1000, HINT_A);
    HintImageState *state = hint_image_state(&cpu, 0x1000, false);
    assert(outline_enabled && state->circle && state->outline_width == state->outline_height);
    assert(font_size == 56 && label_x == 0 && !memcmp(label_color, outline_color, sizeof label_color));
    cpu.x[0] = 0xe000; cpu.x[1] = 0xf000;
    assert(populate_hint_outline(&cpu) == 1 && vertices == 192 && triangles == 288);
    assert(cpu.x[0] == 0xe000 && cpu.x[1] == 0xf000);
    float outer = state->outline_width * .5f, inner = outer - state->font_size * .045f;
    float feather = state->font_size * .02f;
    float radii[] = {outer + feather, outer, inner, inner - feather};
    for (int i = 0; i < vertices; ++i) {
        float r = hypotf(mesh_xy[i][0], mesh_xy[i][1]);
        assert(fabsf(r - radii[i % 4]) < .001f); // a ring, never a filled disk
        assert((mesh_color[i] & 0xffffff) == (mesh_color[0] & 0xffffff));
        assert((mesh_color[i] >> 24) == ((i % 4 == 0 || i % 4 == 3) ? 0 : (mesh_color[1] >> 24)));
    }
    cpu.x[0] = 0x1000;
    for (int i = 0; i < 4; ++i) memcpy(&cpu.v[i], &fade[i], sizeof(float));
    update_hint_color(&cpu);
    assert(!memcmp(label_color, outline_color, sizeof label_color)); // border follows tint and alpha
    g_bind_key[BIND_A] = SDL_SCANCODE_RETURN;
    apply_image_label(&cpu, 0x1000, HINT_A);
    assert(!strcmp(displayed, "Enter"));
    assert(outline_enabled && !state->circle && state->outline_width > state->outline_height);
    assert(label_x == (state->outline_width - state->outline_height) * .5f);
    cpu.x[0] = 0xe000; cpu.x[1] = 0xf000;
    assert(populate_hint_outline(&cpu) == 1 && vertices == 144 && triangles == 216);
    for (int i = 0; i < vertices; ++i) {
        assert(fabsf(mesh_xy[i][0]) <= state->outline_width * .5f + feather + .001f);
        assert(fabsf(mesh_xy[i][1]) <= state->outline_height * .5f + feather + .001f);
    }
    enclosed = false;
    apply_image_label(&cpu, 0x1000, HINT_A);
    assert(!outline_enabled && font_size == 70 && label_x == 0); // standalone prompts stay plain
    cpu.x[0] = 0x1234;
    assert(populate_hint_outline(&cpu) == 0); // ordinary Images use the original renderer
    g_settings.prompts = PROMPTS_NINTENDO;
    apply_image_label(&cpu, 0x1000, HINT_A);
    assert(image_enabled && !text_enabled && !layout_enabled && !outline_enabled);
    forget_hint_component(0x1000);
    assert(!hint_image_state(&cpu, 0x1000, false));
    g_settings.prompts = PROMPTS_KEYBOARD;
    enclosed = horizontal_layout = true;
    apply_image_label(&cpu, 0x1000, HINT_A);
    state = hint_image_state(&cpu, 0x1000, false);
    assert(state->centered_layout && label_x == 0 && layout_owner == 0x1100);
    g_settings.experimental_keyboard_prompts = false;
    apply_image_label(&cpu, 0x1000, HINT_A);
    assert(image_enabled && !text_enabled && !layout_enabled && !outline_enabled);
    cpu.x[1] = HINT_A;
    assert(rt_hook(&cpu, 0x7000cdb690ull) == 0); // even forced keyboard prompts require opt-in
    g_settings.experimental_keyboard_prompts = true;
    cpu.x[1] = HINT_LEFT_STICK;
    assert(rt_hook(&cpu, 0x7000cdb690ull) == 0); // inline movement shorthand stays native
    cpu.x[1] = HINT_ARROW_UP;
    assert(rt_hook(&cpu, 0x7000cdb690ull) == 0);
    enclosed = false; oversized = true;
    apply_image_label(&cpu, 0x1000, HINT_A);
    assert(font_size < 70 && label_width == layout_width && layout_width <= 70 * 3.5f + 24);
    char label[128];
    g_bind_key[BIND_LS_UP] = SDL_SCANCODE_W; g_bind_key[BIND_LS_LEFT] = SDL_SCANCODE_A;
    g_bind_key[BIND_LS_DOWN] = SDL_SCANCODE_S; g_bind_key[BIND_LS_RIGHT] = SDL_SCANCODE_D;
    assert(hint_key_label(HINT_LEFT_STICK, label, sizeof label) && !strcmp(label, "WASD"));
    g_bind_key[BIND_LS_UP] = SDL_SCANCODE_UP; g_bind_key[BIND_LS_LEFT] = SDL_SCANCODE_LEFT;
    g_bind_key[BIND_LS_DOWN] = SDL_SCANCODE_DOWN; g_bind_key[BIND_LS_RIGHT] = SDL_SCANCODE_RIGHT;
    assert(hint_key_label(HINT_LEFT_STICK, label, sizeof label) && !strcmp(label, "Arrows"));
    assert(!hint_key_label(HINT_LEFT_STICK, label, 4));
    g_bind_key[BIND_RS_UP] = SDL_SCANCODE_KP_8; g_bind_key[BIND_RS_LEFT] = SDL_SCANCODE_KP_4;
    g_bind_key[BIND_RS_DOWN] = SDL_SCANCODE_KP_5; g_bind_key[BIND_RS_RIGHT] = SDL_SCANCODE_KP_6;
    assert(hint_key_label(HINT_RIGHT_STICK, label, sizeof label) && !strcmp(label, "Num8456"));
    g_bind_key[BIND_A] = SDL_SCANCODE_BACKSPACE;
    assert(hint_key_label(HINT_A, label, sizeof label) && !strcmp(label, "Bksp"));
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix='sf2-hint-test-') as directory:
            root = pathlib.Path(directory)
            (root / 'test.c').write_text(source)
            compile_harness(root / 'test.c', root / 'test')
            subprocess.run([str(root / 'test')], check=True, timeout=5)


if __name__ == '__main__':
    unittest.main()
