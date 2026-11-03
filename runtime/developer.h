#include "constants/core.h"
#pragma once
#include <stdbool.h>

typedef union SDL_Event SDL_Event;
enum {
    DEV_CONSOLE_TOGGLE = 1, DEV_OVERLAYS_TOGGLE, DEV_TEXT, DEV_BACKSPACE,
    DEV_SUBMIT, DEV_HISTORY_PREV, DEV_HISTORY_NEXT, DEV_CONSOLE_CLOSE,
    DEV_SCROLL_UP, DEV_SCROLL_DOWN
};
typedef struct { int action; char text[SF2C_DEVELOPER_N_256_51E8]; } DeveloperEvent;

bool developer_handle_event(const SDL_Event *event);
bool developer_poll_event(DeveloperEvent *event);
bool developer_console_open(void);
void developer_set_console_open(bool open);
void developer_input_reset(void);
void developer_update_text_input(void *window);
