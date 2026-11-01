"""INI player presets through real SDL virtual gamepads and guest HID states."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_settings import compile_harness


HARNESS = r'''
#include <assert.h>
#include <stdlib.h>
#include <SDL3/SDL.h>
#include "settings.h"
#include "input_bind.h"
// No native window is needed to test vibration routing.
static SDL_Window *test_focus(void) { return (SDL_Window *)1; }
#define SDL_GetKeyboardFocus test_focus
#include "hle/input.c"
bool menu_input_captured(void) { return false; }
bool menu_is_open(void) { return false; }
bool movie_overlay_active(void) { return false; }
void movie_request_skip(void) {}
static uint64_t frame = 1;
uint64_t renderer_present_count(void) { return frame; }
volatile int g_inject_plus;

static int rumbling;
static bool SDLCALL rumble(void *userdata, Uint16 lo, Uint16 hi) {
    if (lo || hi) rumbling = *(int *)userdata;
    return true;
}
static SDL_JoystickID make_pad(int *number) {
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.vendor_id = 0xfeed; desc.product_id = 0xbeef;
    desc.naxes = 6; desc.nbuttons = 16;
    desc.name = "SF2 test controller";
    desc.userdata = number; desc.Rumble = rumble;
    SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
    assert(id);
    assert(SDL_SetGamepadMapping(id,
        "none,SF2 test controller,a:b0,b:b1,x:b2,y:b3,back:b4,start:b6,"
        "leftshoulder:b9,rightshoulder:b10,dpup:b11,dpdown:b12,dpleft:b13,dpright:b14,"
        "leftx:a0,lefty:a1,rightx:a2,righty:a3,lefttrigger:a4,righttrigger:a5,"));
    return id;
}
static void pump(void) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) input_handle_event(&event);
}
static void key(Uint32 type, SDL_Scancode scancode) {
    SDL_Event event = {0}; event.type = type; event.key.scancode = scancode;
    input_handle_event(&event);
}
static void state(uint32_t player, uint64_t out[5]) {
    Cpu cpu = {0}; cpu.x[0] = (uintptr_t)out; cpu.x[1] = (uintptr_t)&player;
    fill_state(&cpu);
    bool connected = ((uint32_t *)out)[8] != 0;
    cpu.x[0] = (uintptr_t)&player; hid_style_set(&cpu);
    assert((cpu.x[0] != 0) == connected);
}
int main(int argc, char **argv) {
    assert(argc == 4 && g_settings.player_setup == PLAYER_SETUP_SOLO);
    assert(settings_load_file(argv[1]));
    int mode = atoi(argv[3]);
    assert(g_settings.player_setup == mode);
    assert(settings_load_file(argv[2]) && g_settings.player_setup == mode);
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    // Admit only our virtual VID/PID, including when a real pad is plugged in.
    SDL_SetHint(SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT, "0xfeed/0xbeef");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, "0");
    assert(SDL_Init(SDL_INIT_GAMEPAD));
    bind_reset(BIND_DEV_KEY); bind_reset(BIND_DEV_PAD);
    uint64_t p1[5], p2[5], absent[5];
    state(0, p1); state(1, p2); state(32, absent);
    assert((((uint32_t *)p1)[8] != 0) == (mode != PLAYER_SETUP_TWO_CONTROLLERS));
    assert(!((uint32_t *)p2)[8] && !((uint32_t *)absent)[8]);

    // Devices already present at the first input event are assigned at startup.
    int one = 1, two = 2, three = 3;
    SDL_JoystickID first = make_pad(&one);
    pump();
    int slot = mode == PLAYER_SETUP_KEYBOARD_VS_CONTROLLER ? 1 : 0;
    assert(pad_slot_for_id(first) == slot);
    SDL_Joystick *joystick = SDL_OpenJoystick(first); assert(joystick);
    assert(SDL_SetJoystickVirtualAxis(joystick, 4, -32768));
    assert(SDL_SetJoystickVirtualAxis(joystick, 5, -32768));
    pump();

    key(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_K);
    key(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_W);
    // East maps to Switch A by default; virtual b1 is the east position.
    assert(SDL_SetJoystickVirtualButton(joystick, 1, true));
    assert(SDL_SetJoystickVirtualAxis(joystick, 0, 24000));
    pump();
    state(0, p1); state(1, p2);
    assert((slot == 0 ? p1[1] : p2[1]) & (1ull << BIND_A));
    assert(((int32_t *)(slot == 0 ? p1 : p2))[4] == 24000);
    assert(((int32_t *)p1)[5] == (mode == PLAYER_SETUP_TWO_CONTROLLERS ? 0 : 32767));
    if (slot == 1) {
        assert(p1[1] & (1ull << BIND_A));
        assert(((int32_t *)p1)[4] == 0 && ((int32_t *)p2)[5] == 0);
    }
    key(SDL_EVENT_KEY_UP, SDL_SCANCODE_K);
    key(SDL_EVENT_KEY_UP, SDL_SCANCODE_W);
    assert(SDL_SetJoystickVirtualButton(joystick, 1, false));
    assert(SDL_SetJoystickVirtualAxis(joystick, 0, 0)); pump(); ++frame;
    state(0, p1); state(1, p2);
    assert(!(p1[1] & (1ull << BIND_A)) && !(p2[1] & (1ull << BIND_A)));

    // Short controller taps must reach its assigned player only.
    assert(SDL_SetJoystickVirtualButton(joystick, 2, true)); pump();
    assert(SDL_SetJoystickVirtualButton(joystick, 2, false)); pump();
    state(0, p1); state(1, p2);
    assert((slot == 0 ? p1[1] : p2[1]) & (1ull << BIND_Y));
    assert(!((slot == 0 ? p2[1] : p1[1]) & (1ull << BIND_Y)));

    RumbleValue value = {0.5f, 160, 0.5f, 320};
    input_rumble_send(3u | ((uint32_t)slot << 8), &value);
    atomic_store(&rumble_frame_ms, SDL_GetTicks()); input_rumble_poll();
    assert(rumbling == one);

    SDL_JoystickID second = make_pad(&two); pump();
    assert(pad_slot_for_id(second) == (slot == 1 ? -1 : 1));
    SDL_JoystickID third = make_pad(&three); pump();
    assert(pad_slot_for_id(third) == -1);

    // Removing a controller clears held state, taps and rumble without moving P2.
    assert(SDL_SetJoystickVirtualButton(joystick, 1, true)); pump();
    assert(SDL_DetachVirtualJoystick(first)); pump(); SDL_CloseJoystick(joystick);
    ++frame; state((uint32_t)slot, p1);
    assert(p1[1] == 0 && p1[2] == 0 && p1[3] == 0);
    assert(rumble_values[slot * 2].low_amp == 0);
    if (slot == 0) assert(pad_slot_for_id(second) == 1);
    assert(SDL_DetachVirtualJoystick(third)); pump();
    third = make_pad(&three); pump();
    assert(pad_slot_for_id(third) == slot);
    if (slot == 0) assert(pad_slot_for_id(second) == 1);
    state((uint32_t)slot, p1);
    assert(!(p1[1] & (1ull << BIND_A)));
    detach_pad(second); detach_pad(third);
    SDL_DetachVirtualJoystick(second); SDL_DetachVirtualJoystick(third);
    SDL_Quit();
}
'''


class PlayerAssignmentTests(unittest.TestCase):
    def test_presets_route_input_hotplug_and_rumble(self):
        with tempfile.TemporaryDirectory(prefix="sf2-players-") as directory:
            root = Path(directory)
            (root / "test.c").write_text(HARNESS)
            compile_harness(root / "test.c", root / "test")
            invalid = root / "invalid.ini"
            invalid.write_text("[input]\nplayer_setup = typo\n")
            env = {**os.environ, "SDL_JOYSTICK_DEVICE": "/nonexistent/sf2-test-device",
                   "SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT": "0xfeed/0xbeef"}
            for mode, value in enumerate(("solo", "keyboard_vs_controller", "two_controllers")):
                with self.subTest(mode=value):
                    config = root / "players.ini"
                    config.write_text(f"[INPUT]\nPLAYER_SETUP = {value.upper()}\n")
                    result = subprocess.run([str(root / "test"), str(config), str(invalid), str(mode)],
                                            env=env, capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertIn("invalid [input] player_setup", result.stderr)


if __name__ == "__main__":
    unittest.main()
