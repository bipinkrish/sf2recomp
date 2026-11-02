"""Escape leaves campaign screens after native dialogs consume Back first."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_settings import compile_harness


class CampaignNavigationTests(unittest.TestCase):
    def test_back_priority_and_binding_ownership(self):
        source = r'''
#include <SDL3/SDL.h>
#include "hle/hooks.c"
#include <assert.h>
static unsigned char dialogs[80],back[32],controllers[32];
static unsigned exits,closed;
bool menu_quit_requested(void) { return false; }
uint64_t plat_now_ns(void) { return 1; }
void guest_call(Cpu *c,uint64_t pc) {
    switch(pc) {
    case 0x7000cede90ull:c->x[0]=(uintptr_t)dialogs;return;
    case 0x7000f0cce0ull:c->x[0]=(uintptr_t)back;return;
    case 0x7000f0cf40ull:assert(c->x[0]==(uintptr_t)back);++closed;return;
    case 0x7000f48cf0ull:c->x[0]=123;return;
    case 0x7000f48ef0ull:assert(c->x[0]==123 && c->x[1]==7);++exits;return;
    default:assert(!"unexpected guest call");
    }
}
int main(void) {
    ST64(back+0x18,(uintptr_t)controllers);
    Cpu cpu={0};cpu.x[19]=999;Cpu saved=cpu;
    SDL_Event e={0};e.type=SDL_EVENT_KEY_DOWN;e.key.scancode=SDL_SCANCODE_ESCAPE;
    input_menu_navigation(INPUT_NAV_CAMPAIGN);
    assert(lan_menu_handle_event(&e));lan_ui_pump(&cpu);
    assert(exits==1 && !closed && !memcmp(&cpu,&saved,sizeof cpu));
    lan_ui_pump(&cpu);assert(exits==1); // one press, one action
    ST64(dialogs+0x40,1);
    assert(lan_menu_handle_event(&e));lan_ui_pump(&cpu);
    assert(exits==1 && closed==1);
    ST64(dialogs+0x40,0);ST32(controllers+0x18,1);
    assert(lan_menu_handle_event(&e));lan_ui_pump(&cpu);
    assert(exits==1 && closed==2);
    e.key.repeat=true;assert(lan_menu_handle_event(&e));lan_ui_pump(&cpu);assert(closed==2);
    e.key.repeat=false;g_bind_key[BIND_X]=SDL_SCANCODE_ESCAPE;
    assert(!lan_menu_handle_event(&e));
    g_bind_key[BIND_X]=SDL_SCANCODE_L;
    input_menu_navigation(INPUT_NAV_COMBAT);assert(!lan_menu_handle_event(&e));
    input_menu_navigation(INPUT_NAV_SPARRING);assert(!lan_menu_handle_event(&e));
    input_menu_navigation(INPUT_NAV_MENU);assert(!lan_menu_handle_event(&e));
}
'''
        with tempfile.TemporaryDirectory(prefix="sf2-campaign-back-") as temporary:
            root = Path(temporary)
            (root / "test.c").write_text(source)
            compile_harness(root / "test.c", root / "test")
            subprocess.run([str(root / "test")], check=True)
