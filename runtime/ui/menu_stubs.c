#include "menu.h"

bool menu_handle_event(const SDL_Event *e) { (void)e; return false; }
bool menu_is_open(void) { return false; }
bool menu_input_captured(void) { return false; }
void menu_apply_window_requests(void *sdl_window) { (void)sdl_window; }
void menu_render(void *cmd, uint32_t target_w, uint32_t target_h) {
    (void)cmd; (void)target_w; (void)target_h;
}

const char *larecomp_version(void) { return "1.0.0"; }
int game_in_play(void) { return 1; }
const char *game_level_name(void) { return ""; }
void menu_test_open(void) {}
