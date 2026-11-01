"""SDL mouse events through the Unity menu bridge, without proprietary files/GPU."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_settings import compile_harness


class MouseMenuTests(unittest.TestCase):
    def test_pointer_coordinates_edges_capture_and_guest_abi(self):
        source = r'''
#include <assert.h>
#include <SDL3/SDL.h>
#include "hle/input.c"
#include "hle/hooks.c"

bool menu_is_open(void) { return developer_console_open(); }
bool menu_input_captured(void) { return developer_console_open(); }
bool movie_overlay_active(void) { return false; }
void movie_request_skip(void) {}
void menu_request_quit(void) {}
bool menu_quit_requested(void) { return false; }
uint64_t plat_now_ns(void) { return 1000000; }
uint64_t renderer_present_count(void) { return 1; }
volatile int g_inject_plus;
void *hle_alloc(uint64_t size) { return calloc(1, size); }
bool rt_probe_u64(uint64_t a, uint64_t *v) { return false; }
bool nvn_texture_region_color(const NvnTexture *t, uint32_t x, uint32_t y,
                              uint32_t w, uint32_t h, float rgb[3]) { return false; }
static unsigned screen_w = 1920, screen_h = 1080;
void guest_call(Cpu *c, uint64_t pc) {
    assert(pc == 0x70015ccb90ull || pc == 0x70015ccc00ull);
    memset(c, 0x5a, sizeof *c);
    c->x[0] = pc == 0x70015ccb90ull ? screen_w : screen_h;
}
static SDL_Window *window;
static void motion(float x, float y) {
    SDL_Event e = {0}; e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.windowID = SDL_GetWindowID(window); e.motion.x = x; e.motion.y = y;
    input_handle_event(&e);
}
static void button(Uint32 type, Uint8 button, float x, float y) {
    SDL_Event e = {0}; e.type = type; e.button.button = button;
    e.button.windowID = SDL_GetWindowID(window); e.button.x = x; e.button.y = y;
    input_handle_event(&e);
}
static void event(Uint32 type) {
    SDL_Event e = {0}; e.type = type; input_handle_event(&e);
}
static void frame(void) {
    Cpu cpu, saved; memset(&cpu, 0xa5, sizeof cpu); saved = cpu;
    assert(rt_hook(&cpu, 0x70000fd740ull) == 0);
    assert(!memcmp(&cpu, &saved, sizeof cpu));
}
static bool read_button(uint64_t site, unsigned button) {
    Cpu cpu = {0}; cpu.x[0] = 0x1234; cpu.x[1] = button;
    cpu.sp = 0x5678; cpu.x[19] = 0xabcd;
    assert(rt_hook(&cpu, site) == 1);
    assert(cpu.sp == 0x5678 && cpu.x[19] == 0xabcd && cpu.x[1] == button);
    return cpu.x[0];
}
static void position(float x, float y) {
    Cpu cpu = {0}; assert(rt_hook(&cpu, 0x70000faab0ull) == 1);
    float actual_x, actual_y;
    memcpy(&actual_x, &cpu.v[0], 4); memcpy(&actual_y, &cpu.v[1], 4);
    assert(fabsf(actual_x - x) < .01f && fabsf(actual_y - y) < .01f);
}
int main(void) {
    assert(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD));
    window = SDL_CreateWindow("mouse menu test", 1000, 1000, 0); assert(window);
    g_settings.aspect = ASPECT_ORIGINAL;
    motion(250, 375); frame(); position(480, 780);
    assert(read_button(0x70000fa8f0ull, 0)); // module works before the first click
    assert(!read_button(0x70000fabe0ull, 0)); // mouse path, not Switch touch emulation
    assert(!read_button(0x70000fac50ull, 0)); // inactive Switch slots cannot suppress mouse processing

    // A complete click between guest frames remains visible to every getter.
    button(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT, 250, 375);
    button(SDL_EVENT_MOUSE_BUTTON_UP, SDL_BUTTON_LEFT, 250, 375);
    frame();
    assert(read_button(0x70000fa960ull, 0));
    assert(read_button(0x70000fa9d0ull, 0));
    assert(!read_button(0x70000faa40ull, 0));
    assert(read_button(0x70000fa960ull, 0));
    assert(!read_button(0x70000fa960ull, 3));
    assert(!read_button(0x70000fa960ull, UINT32_MAX));
    frame(); assert(!read_button(0x70000fa960ull, 0));
    assert(!read_button(0x70000fa9d0ull, 0));

    button(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_RIGHT, 500, 500);
    button(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_MIDDLE, 500, 500);
    frame(); assert(read_button(0x70000faa40ull, 1) && read_button(0x70000faa40ull, 2));
    frame(); assert(!read_button(0x70000fa960ull, 1) && read_button(0x70000faa40ull, 1));
    event(SDL_EVENT_WINDOW_FOCUS_LOST); frame(); position(-10000, -10000);
    assert(read_button(0x70000fa9d0ull, 1) && !read_button(0x70000faa40ull, 1));

    // Bars and positions beyond the window must not be clamped onto buttons.
    motion(500, 0); frame(); position(960, 1500);
    motion(1100, 500); frame(); position(2112, 540);
    event(SDL_EVENT_WINDOW_MOUSE_LEAVE); frame(); position(-10000, -10000);

    motion(500, 500); button(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT, 500, 500);
    frame(); assert(read_button(0x70000faa40ull, 0));
    developer_set_console_open(true); frame(); position(-10000, -10000);
    assert(!read_button(0x70000fa960ull, 0) && !read_button(0x70000faa40ull, 0));
    assert(read_button(0x70000fa9d0ull, 0));
    developer_set_console_open(false); frame(); assert(!read_button(0x70000fa960ull, 0));

    // Keyboard/controller navigation removes hover until the mouse is used again.
    motion(500, 500); frame(); position(960, 540);
    SDL_Event key = {0}; key.type = SDL_EVENT_KEY_DOWN; key.key.scancode = SDL_SCANCODE_K;
    input_handle_event(&key); frame(); position(-10000, -10000);
    motion(500, 500); frame(); position(960, 540);

    // Resizing and custom aspect map into the guest's actual Screen dimensions.
    assert(SDL_SetWindowSize(window, 1600, 900));
    motion(400, 225); frame(); position(480, 810);
    g_settings.aspect = ASPECT_32_9; screen_w = 3840;
    motion(400, 337.5); frame(); position(960, 810);
    SDL_Event wheel = {0}; wheel.type = SDL_EVENT_MOUSE_WHEEL;
    wheel.wheel.windowID = SDL_GetWindowID(window);
    wheel.wheel.x = 2; wheel.wheel.y = 3; wheel.wheel.direction = SDL_MOUSEWHEEL_FLIPPED;
    input_handle_event(&wheel); frame();
    Cpu cpu = {0}; assert(rt_hook(&cpu, 0x70000fab70ull) == 1);
    float sx, sy; memcpy(&sx, &cpu.v[0], 4); memcpy(&sy, &cpu.v[1], 4);
    assert(sx == -2 && sy == -3);
    frame(); assert(menu_mouse.scroll_x == 0 && menu_mouse.scroll_y == 0);
    SDL_DestroyWindow(window); SDL_Quit();
}
'''
        with tempfile.TemporaryDirectory(prefix="sf2-mouse-menus-") as directory:
            root = Path(directory)
            (root / "test.c").write_text(source)
            compile_harness(root / "test.c", root / "test")
            subprocess.run([str(root / "test")], check=True, timeout=10,
                           env={**os.environ, "SDL_VIDEODRIVER": "dummy"})


if __name__ == "__main__":
    unittest.main()
