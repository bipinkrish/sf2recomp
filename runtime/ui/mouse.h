// SDL window points -> the game's Unity EventSystem (guest update thread).
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef union SDL_Event SDL_Event;

typedef struct {
    float x, y, scroll_x, scroll_y;
    uint8_t held, pressed, released;  // Unity button order: left, right, middle
} MenuMouse;

void menu_mouse_event(const SDL_Event *event);  // SDL/UI thread
void menu_mouse_reset(void);
// Latch once before the current EventSystem updates its modules. All getters in
// that update see the same edges, including a press/release between two frames.
MenuMouse menu_mouse_frame(unsigned width, unsigned height, bool captured);
