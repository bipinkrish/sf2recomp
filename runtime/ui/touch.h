#include "../constants/ui.h"
// SDL contacts latched once per Unity EventSystem update, on the guest thread.
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef union SDL_Event SDL_Event;
enum { MENU_TOUCH_MAX = SF2C_TOUCH_N_10_4A44 };
enum { TOUCH_BEGAN, TOUCH_MOVED, TOUCH_STATIONARY, TOUCH_ENDED, TOUCH_CANCELED };

// UnityEngine.Touch's native IL2CPP layout (not a managed object).
typedef struct {
    int32_t finger;
    float x, y, raw_x, raw_y, dx, dy, dt;
    int32_t taps, phase, type;
    float pressure, maximum_pressure, radius, radius_variance, altitude, azimuth;
} MenuTouch;
_Static_assert(sizeof(MenuTouch) == SF2C_TOUCH_N_0X44_E723, SF2C_TOUCH_S_UNITY_TOUCH_ABI_1ABD);

void menu_touch_event(const SDL_Event *event); // SDL thread
void menu_touch_cancel(void);
// Capture movie taps through release, so skipping cannot activate the next menu.
bool menu_touch_movie_event(const SDL_Event *event, bool movie_visible);
unsigned menu_touch_frame(unsigned width, unsigned height, bool captured,
                          MenuTouch out[MENU_TOUCH_MAX]);
