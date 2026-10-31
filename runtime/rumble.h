// Approximate Switch HD Rumble with a conventional two-motor gamepad.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <math.h>

typedef struct { float low_amp, low_hz, high_amp, high_hz; } RumbleValue;
_Static_assert(sizeof(RumbleValue) == 16, "HID vibration wire layout");

static inline uint16_t rumble_amplitude(float a) {
    if (!isfinite(a) || a <= 0) return 0;
    if (a >= 1) return 65535;
    return (uint16_t)(a * 65535.0f + 0.5f);
}

enum { RUMBLE_PLAYER_COUNT = 2, RUMBLE_SIDES_PER_PLAYER = 2, RUMBLE_VALUE_COUNT = 4 };

// Current HLE uses native HID handles: style | player<<8 | side<<16.
// Accept the older synthetic 0x03000000 | player<<8 | side for compatibility.
static inline int rumble_index(uint32_t h) {
    uint32_t player, side;
    if ((h & 0xffff00feu) == 0x03000000u) {  // older handle with style in the high byte
        player = (h >> 8) & 255;
        side = h & 1;
    } else if ((h & 0xff000000u) == 0 && (h & 255) >= 3 && (h & 255) <= 5 &&
               ((h >> 8) & 255) < RUMBLE_PLAYER_COUNT && ((h >> 16) & 255) < RUMBLE_SIDES_PER_PLAYER) {
        player = (h >> 8) & 255;
        side = (h >> 16) & 255;
    } else {
        return -1;
    }
    if (player >= RUMBLE_PLAYER_COUNT) return -1;
    return (int)(player * RUMBLE_SIDES_PER_PLAYER + side);
}

static inline int rumble_side(uint32_t h) {
    int index = rumble_index(h);
    return index < 0 ? -1 : index % RUMBLE_SIDES_PER_PLAYER;
}

// No frequency control exists in SDL_RumbleGamepad: low/high amplitudes map
// linearly to its large/small motors. Mix L/R devices by maximum, not sum,
// avoiding clipping when the game sends the same effect to both Joy-Cons.
static inline void rumble_mix(const RumbleValue v[RUMBLE_VALUE_COUNT], int player, uint16_t *lo, uint16_t *hi) {
    int first = player * RUMBLE_SIDES_PER_PLAYER;
    uint16_t a = rumble_amplitude(v[first].low_amp), b = rumble_amplitude(v[first + 1].low_amp);
    *lo = a > b ? a : b;
    a = rumble_amplitude(v[first].high_amp); b = rumble_amplitude(v[first + 1].high_amp);
    *hi = a > b ? a : b;
}

void input_rumble_send(uint32_t handle, const RumbleValue *value);
RumbleValue input_rumble_get(uint32_t handle);
void input_rumble_permit(bool permitted);
bool input_rumble_permitted(void);
void input_rumble_poll(void);  // UI thread, same owner as pad open/close
