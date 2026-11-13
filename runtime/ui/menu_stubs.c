#include "../constants/ui.h"
#include "menu.h"
#include "lan_menu.h"
#include "../developer.h"
#include "../version.h"
#include <SDL3/SDL.h>
#include <stdatomic.h>

static atomic_bool quit_requested;
static atomic_int fullscreen_request;
enum { WINDOW_MODE_IDLE, WINDOW_MODE_WINDOWED, WINDOW_MODE_FULLSCREEN,
       WINDOW_MODE_APPLIED, WINDOW_MODE_FAILED };

void menu_request_quit(void) { atomic_store(&quit_requested, true); }
bool menu_quit_requested(void) { return atomic_load(&quit_requested); }

void menu_request_fullscreen(bool enabled) {
    atomic_store(&fullscreen_request, enabled ? WINDOW_MODE_FULLSCREEN : WINDOW_MODE_WINDOWED);
}

bool menu_fullscreen_result(bool *applied) {
    int state = atomic_load(&fullscreen_request);
    if (state != WINDOW_MODE_APPLIED && state != WINDOW_MODE_FAILED) return false;
    *applied = state == WINDOW_MODE_APPLIED;
    atomic_store(&fullscreen_request, WINDOW_MODE_IDLE);
    return true;
}

bool menu_handle_event(const SDL_Event *e) { return lan_menu_handle_event(e) || developer_handle_event(e); }
bool menu_is_open(void) { return developer_console_open() || lan_menu_capture(); }
bool menu_input_captured(void) { return developer_console_open() || lan_menu_capture(); }
void menu_apply_window_requests(void *sdl_window) {
    developer_update_text_input(sdl_window);
    lan_menu_update_capture();
    // Scripted resize requests still need the UI thread after removing ImGui.
    extern uint32_t g_window_resize_request;
    uint32_t size = __atomic_exchange_n(&g_window_resize_request, 0, __ATOMIC_RELAXED);
    if (size) SDL_SetWindowSize(sdl_window, size >> SF2C_MENU_STUBS_N_16_B17E, size & SF2C_MENU_STUBS_N_0XFFFF_B111);
    int request = atomic_load(&fullscreen_request);
    if (request == WINDOW_MODE_WINDOWED || request == WINDOW_MODE_FULLSCREEN) {
        bool fullscreen = request == WINDOW_MODE_FULLSCREEN;
        bool applied = SDL_SetWindowFullscreen(sdl_window, fullscreen) && SDL_SyncWindow(sdl_window) &&
            ((SDL_GetWindowFlags(sdl_window) & SDL_WINDOW_FULLSCREEN) != 0) == fullscreen;
        atomic_store(&fullscreen_request, applied ? WINDOW_MODE_APPLIED : WINDOW_MODE_FAILED);
    }
}
void menu_render(void *cmd, uint32_t target_w, uint32_t target_h) {
    (void)cmd; (void)target_w; (void)target_h;
}

const char *sf2recomp_version(void) { return SF2RECOMP_VERSION; }
int game_in_play(void) { return 1; }
const char *game_level_name(void) { return SF2C_MENU_STUBS_S_EMPTY_12AE; }
void menu_test_open(void) {}
