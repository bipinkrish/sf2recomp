"""Modal LAN input follows remapped keys/pad buttons and does not leak to fights."""
from pathlib import Path
import subprocess,tempfile,unittest
from test_settings import compile_harness
class LanMenuInputTests(unittest.TestCase):
    def test_modal_navigation_remapping_and_gameplay_passthrough(self):
        source=r"""
#include <assert.h>
#include <SDL3/SDL.h>
#include "input_bind.h"
#include "ui/lan_menu.h"
static int resets;
void input_reset(void) { ++resets; }
int input_pad_family(void) { return 1; }
int input_family_of_type(int type) { return 1; }
int main(void) {
    SDL_Event e={0};e.type=SDL_EVENT_KEY_DOWN;e.key.scancode=SDL_SCANCODE_ESCAPE;
    assert(!lan_menu_handle_event(&e));
    lan_menu_publish(true);lan_menu_update_capture();assert(resets==1);
    assert(lan_menu_handle_event(&e) && lan_menu_requests()==LAN_MENU_BACK);
    e.key.scancode=SDL_SCANCODE_RETURN;
    assert(lan_menu_handle_event(&e) && lan_menu_requests()==LAN_MENU_CONFIRM);
    e.key.repeat=true;assert(lan_menu_handle_event(&e) && !lan_menu_requests());e.key.repeat=false;
    g_bind_key[BIND_B]=SDL_SCANCODE_F5;e.key.scancode=SDL_SCANCODE_F5;
    assert(lan_menu_handle_event(&e) && lan_menu_requests()==LAN_MENU_BACK);
    e.key.scancode=g_bind_key[BIND_LS_UP];
    assert(lan_menu_handle_event(&e) && lan_menu_requests()==LAN_MENU_UP);
    e.type=SDL_EVENT_GAMEPAD_BUTTON_DOWN;g_bind_pad[BIND_B]=SDL_GAMEPAD_BUTTON_NORTH;e.gbutton.button=SDL_GAMEPAD_BUTTON_NORTH;
    assert(lan_menu_handle_event(&e) && lan_menu_requests()==LAN_MENU_BACK);
    e.type=SDL_EVENT_GAMEPAD_AXIS_MOTION;e.gaxis.axis=SDL_GAMEPAD_AXIS_LEFTY;e.gaxis.value=24000;
    assert(lan_menu_handle_event(&e) && lan_menu_requests()==LAN_MENU_DOWN);
    assert(lan_menu_handle_event(&e) && !lan_menu_requests());
    e.gaxis.value=0;lan_menu_handle_event(&e);e.gaxis.value=-24000;
    assert(lan_menu_handle_event(&e) && lan_menu_requests()==LAN_MENU_UP);
    e.type=SDL_EVENT_MOUSE_BUTTON_DOWN;assert(!lan_menu_handle_event(&e));
    lan_menu_publish(false);lan_menu_update_capture();
    assert(resets==2 && !lan_menu_capture() && !lan_menu_requests());
    e.type=SDL_EVENT_KEY_DOWN;assert(!lan_menu_handle_event(&e));
}
"""
        with tempfile.TemporaryDirectory(prefix='sf2-lan-modal-input-') as d:
            root=Path(d);(root/'test.c').write_text(source);compile_harness(root/'test.c',root/'test')
            subprocess.run([str(root/'test')],check=True)
