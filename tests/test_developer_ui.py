"""Desktop input, scoped trainer gates and configuration of the game's developer UI."""
import pathlib
import subprocess
import tempfile
import unittest

from test_settings import compile_harness


class DeveloperUITests(unittest.TestCase):
    def run_harness(self, source, configs=()):
        with tempfile.TemporaryDirectory(prefix="sf2-developer-") as directory:
            root = pathlib.Path(directory)
            (root / "test.c").write_text(source)
            files = []
            for i, content in enumerate(configs):
                path = root / f"config-{i}.ini"
                path.write_text(content)
                files.append(str(path))
            compile_harness(root / "test.c", root / "test")
            subprocess.run([str(root / "test"), *files], check=True)

    def test_console_event_capture_queue_and_disabled_options(self):
        self.run_harness(r'''
#include <assert.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "developer.h"
#include "settings.h"

static bool key(SDL_Scancode code, bool down) {
    SDL_Event event = {0};
    event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.scancode = code;
    return developer_handle_event(&event);
}
int main(void) {
    DeveloperEvent item;
    assert(!g_settings.developer_console && !g_settings.developer_trainer &&
           !g_settings.developer_shop_cheats && !g_settings.developer_overlays);
    g_settings.developer_console = g_settings.developer_overlays = true;
    developer_input_reset();
    assert(!developer_poll_event(&item));
    assert(!key(SDL_SCANCODE_W, true));
    assert(key(SDL_SCANCODE_F1, true) && developer_console_open());
    assert(developer_poll_event(&item) && item.action == DEV_CONSOLE_TOGGLE);
    assert(key(SDL_SCANCODE_F1, false));
    assert(!developer_poll_event(&item));
    assert(key(SDL_SCANCODE_W, true)); // typing is captured instead of moving the player
    SDL_Event event = {.type = SDL_EVENT_TEXT_INPUT};
    event.text.text = "é漢🙂";
    assert(developer_handle_event(&event));
    assert(developer_poll_event(&item) && item.action == DEV_TEXT && !strcmp(item.text, "é漢🙂"));
    assert(key(SDL_SCANCODE_RETURN, true));
    assert(developer_poll_event(&item) && item.action == DEV_SUBMIT);
    assert(key(SDL_SCANCODE_PAGEUP, true));
    assert(developer_poll_event(&item) && item.action == DEV_SCROLL_UP);
    assert(key(SDL_SCANCODE_ESCAPE, true) && !developer_console_open());
    assert(developer_poll_event(&item) && item.action == DEV_CONSOLE_CLOSE);
    assert(!key(SDL_SCANCODE_W, true));
    assert(key(SDL_SCANCODE_F2, true));
    assert(developer_poll_event(&item) && item.action == DEV_OVERLAYS_TOGGLE);
    g_settings.developer_console = g_settings.developer_overlays = false;
    assert(!key(SDL_SCANCODE_F1, true) && !key(SDL_SCANCODE_GRAVE, true) && !key(SDL_SCANCODE_F2, true));
    assert(!developer_poll_event(&item));
    g_settings.developer_console = true;
    assert(key(SDL_SCANCODE_F1, true));
    assert(developer_poll_event(&item));
    // A full queue must never overwrite earlier commands or flip capture on a lost toggle.
    for (int i = 0; i < 200; ++i) developer_handle_event(&event);
    key(SDL_SCANCODE_F1, true);
    assert(developer_console_open());
    int count = 0;
    while (developer_poll_event(&item)) { assert(item.action == DEV_TEXT); ++count; }
    assert(count == 127);
    developer_input_reset();
    assert(!developer_console_open() && !developer_poll_event(&item));
}
''')

    def test_debug_prefab_preserves_scene_construction_context_and_cpu(self):
        self.run_harness(r'''
#include <assert.h>
#include "hle/hooks.c"
#include "ui/menu_stubs.c"
static uint64_t holder_class[32], holder_slot, fields[3] = {0x1111};
static bool created;
static int instantiations;
bool rt_probe_u64(uint64_t address, uint64_t *out) {
    if (address == 0x70039e9b98ull) *out = (uintptr_t)&holder_slot;
    else if (address == (uintptr_t)&holder_slot) *out = (uintptr_t)holder_class;
    else if (address == (uintptr_t)holder_class + 0xb8) *out = (uintptr_t)fields;
    else return false;
    return true;
}
void guest_call(Cpu *c, uint64_t pc) {
    switch (pc) {
    case GUEST_STRING_ALLOC: c->x[0] = (uintptr_t)calloc(1, 0x16 + 2 * c->x[1]); return;
    case 0x7000ced0f0ull: c->x[0] = created ? 0x4444 : 0; return;
    case 0x7000c7b050ull: return;
    case 0x7000e2f610ull: c->x[0] = fields[0]; return;
    case 0x700123de70ull: {
        char path[128]; assert(dev_read_string(c->x[1], path, sizeof path));
        assert(!strcmp(path, "Prefabs/Debug/DebugCanvas")); c->x[0] = 0x2222; return;
    }
    case 0x70012331f0ull:
        assert(c->x[1] == 0x2222); ++instantiations;
        fields[0] = 0x4444; created = true; c->x[0] = 0x3333; return;
    case 0x70012339d0ull: assert(c->x[1] == 0x3333); return;
    default: assert(0);
    }
}
void *hle_alloc(size_t size) { return calloc(1, size); }
bool input_any_gamepad_connected(void) { return false; }
int input_pad_family(void) { return 1; }
int input_family_of_type(int type) { return 1; }
int main(void) {
    Cpu cpu; memset(&cpu, 0x5a, sizeof cpu); Cpu before = cpu;
    g_settings.developer_console = true;
    dev_ensure_ui(&cpu);
    assert(instantiations == 1 && fields[0] == 0x1111 && !dev_creating);
    assert(!memcmp(&cpu, &before, sizeof cpu));
    dev_ensure_ui(&cpu); // opening additional menu lists must not duplicate the prefab
    assert(instantiations == 1 && fields[0] == 0x1111);
    assert(!memcmp(&cpu, &before, sizeof cpu));
}
''')

    def test_scoped_checks_trainer_and_runtime_internal_configuration(self):
        self.run_harness(r'''
#include <assert.h>
#include "hle/hooks.c"
#include "ui/menu_stubs.c"
uint64_t renderer_present_count(void) { return 1; }
uint64_t plat_now_ns(void) { return 1000000; }
static uint64_t calls[8], values[8];
static int call_count, cheat_id;
static bool fighting;
void guest_call(Cpu *c, uint64_t pc) {
    if (pc == GUEST_STRING_ALLOC) { c->x[0] = (uintptr_t)calloc(1, 0x16 + 2 * c->x[1]); return; }
    if (pc == 0x7000c91c70ull) { c->x[0] = fighting ? 0x1234 : 0; return; }
    if (pc == 0x700102ecd0ull) return;
    if (pc == 0x700102f540ull) { cheat_id = c->x[1]; c->x[0] = 1; return; }
    assert(pc == 0x7000e19c10ull || pc == 0x7000e19d30ull || pc == 0x7000e1a450ull);
    assert(call_count < 8);
    calls[call_count] = pc; values[call_count++] = c->x[1];
    c->x[19] = 0xdeadbeef; c->v[3] = 0xabcdef; // nested guest calls must restore all registers
}
void *hle_alloc(size_t size) { return calloc(1, size); }
bool rt_probe_u64(uint64_t a, uint64_t *v) { return false; }
bool input_any_gamepad_connected(void) { return false; }
int input_pad_family(void) { return 1; }
int input_family_of_type(int type) { return 1; }
int main(int argc, char **argv) {
    assert(argc == 2);
    assert(settings_load_file(argv[1]));
    assert(!g_settings.show_intro && !g_settings.ai_enabled && g_settings.show_time_results);
    assert(g_settings.unlock_all_videos && !g_settings.developer_fps && g_settings.developer_memory);
    Cpu cpu = {0}; cpu.x[19] = 0x12345678; cpu.sp = 0xabcdef;
    Cpu before = cpu;
    assert(rt_hook(&cpu, 0x7000e19adcull) == 0);
    assert(!memcmp(&cpu, &before, sizeof cpu));
    assert(call_count == 3 && values[0] == 0 && values[1] == 0 && values[2] == 1);
    assert(rt_hook(&cpu,0x7000e1a5b0ull)==1 && !cpu.x[0]);
    cpu.x[30]=0x7000e6e49cull; // an explicitly selected gallery video still plays
    assert(rt_hook(&cpu,0x7000e1a5b0ull)==1 && cpu.x[0]);
    cpu=before;
    assert(calls[0] == 0x7000e19c10ull && calls[1] == 0x7000e19d30ull);
    assert(rt_hook(&cpu, 0x70000ad0e0ull) == 0); // global/online debug checks are unchanged
    assert(!memcmp(&cpu, &before, sizeof cpu));
    cpu.x[30] = 0x7000da6ee0ull;
    assert(rt_hook(&cpu, 0x70000ad0e0ull) == 1 && cpu.x[0] == 1);
    cpu.x[30] = 0x7000ec7330ull; // ShopScene.Init must instantiate its original cheat panel
    assert(rt_hook(&cpu, 0x70000ad0e0ull) == 1 && cpu.x[0] == 1);
    g_settings.developer_shop_cheats = false;
    cpu.x[0] = 99;
    assert(rt_hook(&cpu, 0x70000ad0e0ull) == 0 && cpu.x[0] == 99);
    g_settings.developer_trainer = false;
    assert(rt_hook(&cpu, 0x700102ec00ull) == 1 && !cpu.x[0]);
    assert(strstr(dev_trainer(&cpu, "god"), "disabled"));
    g_settings.developer_trainer = true;
    assert(strstr(dev_trainer(&cpu, "god"), "Start a fight"));
    fighting = true; before = cpu;
    assert(!strcmp(dev_trainer(&cpu, "god"), "Trainer action applied.") && cheat_id == 22);
    assert(!memcmp(&cpu, &before, sizeof cpu));
    uint64_t string = guest_string(&cpu, "é漢🙂"); char text[32];
    assert(dev_read_string(string, text, sizeof text) && !strcmp(text, "é漢🙂"));
    free((void *)(uintptr_t)string);
    dev_console_input = 0x1234;
    cpu.x[0] = 0x1234; before = cpu;
    assert(rt_hook(&cpu, 0x70005c07c0ull) == 1 && !memcmp(&cpu, &before, sizeof cpu));
    cpu.x[0] = 0x5678;
    assert(rt_hook(&cpu, 0x70005c07c0ull) == 0); // other game text fields retain their original behavior
    assert(dev_execute(&cpu, "ordinary_game_command") == NULL);
    assert(!strcmp(dev_execute(&cpu, "config ai on"), "Session setting updated."));
    assert(g_settings.ai_enabled && values[3] == 1 && calls[3] == 0x7000e19d30ull);
    g_settings.developer_console = g_settings.developer_overlays = false;
    assert(rt_hook(&cpu, 0x7000ceb230ull) == 1 && !cpu.x[0]);
    assert(rt_hook(&cpu, 0x7000ce9d90ull) == 1 && !cpu.x[0]);
}
''', ["[gameplay]\nshow_intro=false\nai_enabled=false\nshow_time_results=true\nunlock_all_videos=true\n"
      "[developer]\nconsole=true\ntrainer=true\nshop_cheats=true\noverlays=true\nfps=false\nmemory=true\n"])
