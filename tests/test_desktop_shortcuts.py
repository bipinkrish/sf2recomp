"""Desktop fallbacks exercise real SDL input, bindings, video skip and hold/release."""
from pathlib import Path
import os, subprocess, tempfile, unittest
from test_settings import compile_harness

class DesktopShortcutTests(unittest.TestCase):
    def test_shortcuts_binding_conflicts_holds_and_video_context(self):
        source = r'''
#include <assert.h>
#include <SDL3/SDL.h>
#include "hle/input.c"
bool menu_is_open(void) { return false; }
bool menu_input_captured(void) { return false; }
static bool movie, skipped;
bool movie_overlay_active(void) { return movie; }
void movie_request_skip(void) { skipped=true; }
uint64_t renderer_present_count(void) { return 1; }
volatile int g_inject_plus;
static void key(int sc,bool down) {
    SDL_Event e={0};e.type=down?SDL_EVENT_KEY_DOWN:SDL_EVENT_KEY_UP;e.key.scancode=sc;
    input_handle_event(&e);
}
static void held(uint64_t expected) { assert(key_buttons==expected); }
int main(void) {
    assert(SDL_Init(SDL_INIT_GAMEPAD));g_settings.player_setup=PLAYER_SETUP_SOLO;
    assert(g_bind_key[BIND_PLUS]==SDL_SCANCODE_TAB);
    input_menu_navigation(true);
    key(SDL_SCANCODE_RETURN,true);held(1ull<<BIND_A);
    key(SDL_SCANCODE_K,true);held(1ull<<BIND_A);
    key(SDL_SCANCODE_RETURN,false);held(1ull<<BIND_A);
    key(SDL_SCANCODE_K,false);held(0);
    key(SDL_SCANCODE_ESCAPE,true);held(1ull<<BIND_B);
    key(SDL_SCANCODE_J,true);key(SDL_SCANCODE_ESCAPE,false);held(1ull<<BIND_B);
    key(SDL_SCANCODE_J,false);held(0);
    // Explicit keyboard actions (including analog directions) own the key.
    g_bind_key[BIND_X]=SDL_SCANCODE_ESCAPE;
    key(SDL_SCANCODE_ESCAPE,true);held(1ull<<BIND_X);key(SDL_SCANCODE_ESCAPE,false);held(0);
    g_bind_key[BIND_LS_RIGHT]=SDL_SCANCODE_RETURN;
    key(SDL_SCANCODE_RETURN,true);held(0);assert(key_lx==32767);
    key(SDL_SCANCODE_RETURN,false);assert(!key_lx);
    g_bind_key[BIND_LS_RIGHT]=SDL_SCANCODE_D;g_bind_key[BIND_X]=SDL_SCANCODE_L;
    // A custom old-style Enter=Pause mapping takes priority over Confirm.
    g_bind_key[BIND_PLUS]=SDL_SCANCODE_RETURN;
    key(SDL_SCANCODE_RETURN,true);held(1ull<<BIND_PLUS);key(SDL_SCANCODE_RETURN,false);held(0);
    g_bind_key[BIND_PLUS]=SDL_SCANCODE_TAB;
    g_bind_key[BIND_B]=SDL_SCANCODE_RETURN;
    key(SDL_SCANCODE_RETURN,true);held(1ull<<BIND_B);key(SDL_SCANCODE_RETURN,false);
    g_bind_key[BIND_B]=SDL_SCANCODE_J;
    input_menu_navigation(false);
    key(SDL_SCANCODE_ESCAPE,true);held(1ull<<BIND_PLUS);key(SDL_SCANCODE_ESCAPE,false);held(0);
    key(SDL_SCANCODE_RETURN,true);held(0);key(SDL_SCANCODE_RETURN,false);
    input_menu_navigation(true);
    key(SDL_SCANCODE_SPACE,true);held(0);assert(!skipped);key(SDL_SCANCODE_SPACE,false);
    movie=true;key(SDL_SCANCODE_SPACE,true);assert(skipped);held(0);
    movie=false;key(SDL_SCANCODE_SPACE,false);held(0);
    movie=true;skipped=false;g_bind_key[BIND_X]=SDL_SCANCODE_SPACE;
    key(SDL_SCANCODE_SPACE,true);held(1ull<<BIND_X);assert(!skipped);
    key(SDL_SCANCODE_SPACE,false);movie=false;
    // Capture/focus resets release both physical and fallback holds.
    key(SDL_SCANCODE_ESCAPE,true);input_reset();held(0);
    SDL_Quit();
}
'''
        with tempfile.TemporaryDirectory(prefix="sf2-desktop-shortcuts-") as d:
            root=Path(d);(root/"test.c").write_text(source);compile_harness(root/"test.c",root/"test")
            subprocess.run([str(root/"test")],check=True,timeout=10,env={**os.environ,"SDL_VIDEODRIVER":"dummy"})
