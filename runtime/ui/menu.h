#include "../constants/ui.h"
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern SF2C_MENU_S_C_C2E8 {
#endif

typedef union SDL_Event SDL_Event;

bool menu_handle_event(const SDL_Event *e);
bool menu_is_open(void);
void menu_request_quit(void);  // any thread; handled by the UI thread's normal quit path
bool menu_quit_requested(void);
// A single settings save may have an SDL window change in flight.
void menu_request_fullscreen(bool enabled);
bool menu_fullscreen_result(bool *applied);
void menu_apply_window_requests(void *sdl_window);
void menu_render(void *cmd, uint32_t target_w, uint32_t target_h);

#ifdef __cplusplus
}
#endif
