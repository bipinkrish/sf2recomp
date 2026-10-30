#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef union SDL_Event SDL_Event;

bool menu_handle_event(const SDL_Event *e);
bool menu_is_open(void);
void menu_apply_window_requests(void *sdl_window);
void menu_render(void *cmd, uint32_t target_w, uint32_t target_h);

#ifdef __cplusplus
}
#endif
