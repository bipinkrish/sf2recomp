#include "menu.h"
#include "../developer.h"
#include <SDL3/SDL.h>
#include <stdatomic.h>

static atomic_bool quit_requested;

void menu_request_quit(void) { atomic_store(&quit_requested, true); }
bool menu_quit_requested(void) { return atomic_load(&quit_requested); }

bool menu_handle_event(const SDL_Event *e) { return developer_handle_event(e); }
bool menu_is_open(void) { return developer_console_open(); }
bool menu_input_captured(void) { return developer_console_open(); }
void menu_apply_window_requests(void *sdl_window) {
    developer_update_text_input(sdl_window);
    // Scripted resize requests still need the UI thread after removing ImGui.
    extern uint32_t g_window_resize_request;
    uint32_t size = __atomic_exchange_n(&g_window_resize_request, 0, __ATOMIC_RELAXED);
    if (size) SDL_SetWindowSize(sdl_window, size >> 16, size & 0xffff);
}
void menu_render(void *cmd, uint32_t target_w, uint32_t target_h) {
    (void)cmd; (void)target_w; (void)target_h;
}

const char *larecomp_version(void) { return "1.0.0"; }
int game_in_play(void) { return 1; }
const char *game_level_name(void) { return ""; }
void menu_test_open(void) {}
