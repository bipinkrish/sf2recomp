// Instruction hooks needed by the native runtime. Game initialization, scene
// loading, object construction and exception behavior remain in the original code.
#include "hle.h"
#include "../settings.h"
#include "../network.h"
#include "../replay.h"
#include "../input_bind.h"
#include "../nvn/texture_color.h"
#include "../ui/menu.h"
#include "../ui/mouse.h"
#include "../ui/touch.h"
#include "movie.h"
#include <SDL3/SDL_scancode.h>
#include <math.h>

#include <limits.h>
#include <stdio.h>
#include <string.h>

enum {
    HINT_A = 0, HINT_B = 1, HINT_X = 2, HINT_Y = 3,
    HINT_DPAD_HORIZONTAL = 4, HINT_DPAD_VERTICAL = 5, HINT_DPAD_ALL = 6,
    HINT_LEFT_STICK = 7, HINT_RIGHT_STICK = 8, HINT_BACK = 9, HINT_START = 10,
    HINT_L1 = 11, HINT_L2 = 12, HINT_R1 = 13, HINT_R2 = 14, HINT_ANY_STICK = 15,
    HINT_SL = 100, HINT_SR = 101, HINT_ARROW_UP = 102, HINT_ARROW_DOWN = 103,
    HINT_ARROW_LEFT = 104, HINT_ARROW_RIGHT = 105
};

enum {
    HINT_COMPONENT_CAP = 1024
};

enum {
    GUEST_TEXT_GET = 0x70005dcd50ull,
    GUEST_TEXT_SET = 0x70005dcd60ull,
    GUEST_STRING_ALLOC = 0x70003f6870ull,
    GUEST_BEHAVIOUR_GET_ENABLED = 0x7001219970ull,
    GUEST_BEHAVIOUR_SET_ENABLED = 0x70012199e0ull
};

typedef struct {
    uint64_t component;
    uint64_t original_text;
    uint64_t owner;
    bool original_enabled;
} HintTextState;

typedef struct {
    uint64_t component;
    uint64_t keyboard_text, keyboard_layout, keyboard_outline;
    uint64_t sprite;
    float artwork_rgb[3];
    float outline_width, outline_height;
    int font_size;
    bool circle, centered_layout;
    bool original_enabled;
} HintImageState;

typedef struct {
    HintTextState texts[HINT_COMPONENT_CAP];
    HintImageState images[HINT_COMPONENT_CAP];
    size_t images_used;
    uint64_t text_type, rect_type, type_type, layout_type, image_type;
    uint64_t button_type, horizontal_type;
} KeyboardHintState;

static KeyboardHintState *hints;
uint32_t rt_hook_features;

static bool ensure_keyboard_hint_state(void) {
    if (!hints) hints = calloc(1, sizeof *hints);
    return hints != NULL;
}

// Invoke one recompiled guest method while preserving the caller's CPU state.
static uint64_t guest_invoke4(Cpu *caller, uint64_t pc, uint64_t x0, uint64_t x1, uint64_t x2, uint64_t x3) {
    Cpu saved = *caller;
    caller->x[0] = x0; caller->x[1] = x1; caller->x[2] = x2; caller->x[3] = x3;
    caller->x[4] = 0;
    guest_call(caller, pc);
    uint64_t result = caller->x[0];
    *caller = saved;
    return result;
}

static uint64_t guest_invoke2(Cpu *caller, uint64_t pc, uint64_t x0, uint64_t x1) {
    return guest_invoke4(caller, pc, x0, x1, 0, 0);
}

static void guest_set_vec2(Cpu *caller, uint64_t pc, uint64_t object, float x, float y) {
    Cpu saved = *caller;
    caller->x[0] = object; caller->x[1] = 0;
    memcpy(&caller->v[0], &x, sizeof x);
    memcpy(&caller->v[1], &y, sizeof y);
    guest_call(caller, pc);
    *caller = saved;
}

static float guest_get_float(Cpu *caller, uint64_t pc, uint64_t object) {
    Cpu saved = *caller;
    caller->x[0] = object; caller->x[1] = 0;
    guest_call(caller, pc);
    float result;
    memcpy(&result, &caller->v[0], sizeof result);
    *caller = saved;
    return result;
}

static bool keyboard_prompts_active(void) {
    if (!g_settings.experimental_keyboard_prompts) return false;
    if (g_settings.prompts == PROMPTS_KEYBOARD) return true;
    return g_settings.prompts == PROMPTS_AUTO && !input_any_gamepad_connected();
}

static const char *key_name_for_slot(int slot) {
    static const char *const numpad[] = {"Num1", "Num2", "Num3", "Num4", "Num5", "Num6", "Num7", "Num8", "Num9"};
    if (g_bind_key[slot] >= SDL_SCANCODE_KP_1 && g_bind_key[slot] <= SDL_SCANCODE_KP_9)
        return numpad[g_bind_key[slot] - SDL_SCANCODE_KP_1];
    switch (g_bind_key[slot]) {
    case SDL_SCANCODE_KP_0: return "Num0";
    case SDL_SCANCODE_RETURN: return "Enter";
    case SDL_SCANCODE_ESCAPE: return "Esc";
    case SDL_SCANCODE_BACKSPACE: return "Bksp";
    case SDL_SCANCODE_LCTRL: return "LCtrl";
    case SDL_SCANCODE_RCTRL: return "RCtrl";
    case SDL_SCANCODE_LSHIFT: return "LShift";
    case SDL_SCANCODE_RSHIFT: return "RShift";
    default: break;
    }
    const char *name = bind_key_label(g_bind_key[slot]);
    return name && *name && strcmp(name, "(none)") ? name : "Unbound";
}

static bool format_slots(char *out, size_t capacity, const int *slots, size_t count) {
    size_t used = 0;
    if (!capacity) return false;
    out[0] = 0;
    for (size_t i = 0; i < count; ++i) {
        int n = snprintf(out + used, capacity - used, "%s%s", i ? "/" : "", key_name_for_slot(slots[i]));
        if (n < 0 || (size_t)n >= capacity - used) {
            out[capacity - 1] = 0;
            return false;
        }
        used += (size_t)n;
    }
    return count != 0;
}

static bool format_one(char *out, size_t capacity, int slot) {
    return format_slots(out, capacity, &slot, 1);
}

static bool format_directions(char *out, size_t capacity, const int directions[4]) {
    if (g_bind_key[directions[0]] == SDL_SCANCODE_UP && g_bind_key[directions[1]] == SDL_SCANCODE_LEFT &&
        g_bind_key[directions[2]] == SDL_SCANCODE_DOWN && g_bind_key[directions[3]] == SDL_SCANCODE_RIGHT) {
        return snprintf(out, capacity, "Arrows") < (int)capacity;
    }
    bool numpad = true;
    for (int i = 0; i < 4; ++i)
        numpad &= g_bind_key[directions[i]] >= SDL_SCANCODE_KP_1 && g_bind_key[directions[i]] <= SDL_SCANCODE_KP_9;
    if (numpad) {
        if (capacity < 8) return false;
        memcpy(out, "Num", 3);
        for (int i = 0; i < 4; ++i) out[3 + i] = '1' + g_bind_key[directions[i]] - SDL_SCANCODE_KP_1;
        out[7] = 0;
        return true;
    }
    bool letters = true;
    for (int i = 0; i < 4; ++i) letters &= strlen(key_name_for_slot(directions[i])) == 1;
    if (!letters) return format_slots(out, capacity, directions, 4);
    if (capacity < 5) return false;
    for (int i = 0; i < 4; ++i) out[i] = key_name_for_slot(directions[i])[0];
    out[4] = 0;
    return true;
}

static bool format_stick(char *out, size_t capacity, int up, int left, int down, int right, int click) {
    const int directions[] = {up, left, down, right};
    bool bound = false;
    for (size_t i = 0; i < sizeof directions / sizeof directions[0]; ++i)
        bound |= g_bind_key[directions[i]] != 0;
    return bound ? format_directions(out, capacity, directions)
                 : format_one(out, capacity, click);
}

static bool hint_key_label(int id, char *out, size_t capacity) {
    static const int horizontal[] = {BIND_LEFT, BIND_RIGHT};
    static const int vertical[] = {BIND_UP, BIND_DOWN};
    static const int dpad[] = {BIND_UP, BIND_LEFT, BIND_DOWN, BIND_RIGHT};
    static const int sticks[] = {BIND_LSTICK, BIND_RSTICK};
    switch (id) {
    case HINT_A: return format_one(out, capacity, BIND_A);
    case HINT_B: return format_one(out, capacity, BIND_B);
    case HINT_X: return format_one(out, capacity, BIND_X);
    case HINT_Y: return format_one(out, capacity, BIND_Y);
    case HINT_DPAD_HORIZONTAL: return format_slots(out, capacity, horizontal, sizeof horizontal / sizeof horizontal[0]);
    case HINT_DPAD_VERTICAL: return format_slots(out, capacity, vertical, sizeof vertical / sizeof vertical[0]);
    case HINT_DPAD_ALL: return format_directions(out, capacity, dpad);
    case HINT_LEFT_STICK: return format_stick(out, capacity, BIND_LS_UP, BIND_LS_LEFT, BIND_LS_DOWN, BIND_LS_RIGHT, BIND_LSTICK);
    case HINT_RIGHT_STICK: return format_stick(out, capacity, BIND_RS_UP, BIND_RS_LEFT, BIND_RS_DOWN, BIND_RS_RIGHT, BIND_RSTICK);
    case HINT_BACK: return format_one(out, capacity, BIND_MINUS);
    case HINT_START: return format_one(out, capacity, BIND_PLUS);
    case HINT_L1: case HINT_SL: return format_one(out, capacity, BIND_L);
    case HINT_L2: return format_one(out, capacity, BIND_ZL);
    case HINT_R1: case HINT_SR: return format_one(out, capacity, BIND_R);
    case HINT_R2: return format_one(out, capacity, BIND_ZR);
    case HINT_ANY_STICK: return format_slots(out, capacity, sticks, sizeof sticks / sizeof sticks[0]);
    case HINT_ARROW_UP: return format_one(out, capacity, BIND_UP);
    case HINT_ARROW_DOWN: return format_one(out, capacity, BIND_DOWN);
    case HINT_ARROW_LEFT: return format_one(out, capacity, BIND_LEFT);
    case HINT_ARROW_RIGHT: return format_one(out, capacity, BIND_RIGHT);
    default: return false;  // Separators and unknown IDs keep their original art.
    }
}

static bool hint_directional(int id) {
    return (id >= HINT_DPAD_HORIZONTAL && id <= HINT_RIGHT_STICK) || id == HINT_ANY_STICK ||
        (id >= HINT_ARROW_UP && id <= HINT_ARROW_RIGHT);
}

static uint32_t utf8_next(const unsigned char **cursor) {
    const unsigned char *s = *cursor;
    uint32_t cp = s[0];
    if (cp < 0x80) {
        if (cp) *cursor = s + 1;
        return cp;
    }
    unsigned need = cp >= 0xc2 && cp <= 0xdf ? 2 : cp >= 0xe0 && cp <= 0xef ? 3 : cp >= 0xf0 && cp <= 0xf4 ? 4 : 0;
    if (!need) { *cursor = s + 1; return 0xfffd; }
    for (unsigned i = 1; i < need; ++i)
        if ((s[i] & 0xc0) != 0x80) { *cursor = s + 1; return 0xfffd; }
    cp = need == 2 ? (s[0] & 0x1f) : need == 3 ? (s[0] & 0x0f) : (s[0] & 0x07);
    for (unsigned i = 1; i < need; ++i) cp = (cp << 6) | (s[i] & 0x3f);
    if ((need == 3 && cp < 0x800) || (need == 4 && cp < 0x10000) || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) {
        *cursor = s + 1;
        return 0xfffd;
    }
    *cursor = s + need;
    return cp;
}

static uint64_t guest_string(Cpu *caller, const char *utf8) {
    size_t units = 0;
    const unsigned char *cursor = (const unsigned char *)utf8;
    while (*cursor) {
        uint32_t cp = utf8_next(&cursor);
        units += cp > 0xffff ? 2 : 1;
        if (units > 256 || units > INT_MAX) return 0;
    }

    uint64_t string = guest_invoke2(caller, GUEST_STRING_ALLOC, 0, units);
    if (!string) return 0;
    ST32(string + 0x10, (uint32_t)units);
    uint64_t chars = string + 0x14;
    cursor = (const unsigned char *)utf8;
    size_t index = 0;
    while (*cursor) {
        uint32_t cp = utf8_next(&cursor);
        if (cp <= 0xffff) ST16(chars + index++ * 2, (uint16_t)cp);
        else {
            cp -= 0x10000;
            ST16(chars + index++ * 2, (uint16_t)(0xd800 + (cp >> 10)));
            ST16(chars + index++ * 2, (uint16_t)(0xdc00 + (cp & 0x3ff)));
        }
    }
    ST16(chars + index * 2, 0);
    return string;
}

static HintTextState *hint_text_state(Cpu *c, uint64_t text, bool create) {
    if (!hints) return NULL;
    HintTextState *empty = NULL;
    for (size_t i = 0; i < HINT_COMPONENT_CAP; ++i) {
        if (hints->texts[i].component == text) return &hints->texts[i];
        if (!hints->texts[i].component && !empty) empty = &hints->texts[i];
    }
    if (!create || !empty) return NULL;
    empty->component = text;
    empty->original_text = guest_invoke2(c, GUEST_TEXT_GET, text, 0);
    empty->original_enabled = guest_invoke2(c, GUEST_BEHAVIOUR_GET_ENABLED, text, 0) != 0;
    return empty;
}

static HintImageState *hint_image_state(Cpu *c, uint64_t image, bool create) {
    if (!hints) return NULL;
    HintImageState *empty = NULL;
    for (size_t i = 0; i < HINT_COMPONENT_CAP; ++i) {
        if (hints->images[i].component == image) return &hints->images[i];
        if (!hints->images[i].component && !empty) empty = &hints->images[i];
    }
    if (!create || !empty) return NULL;
    empty->component = image;
    for (int i = 0; i < 3; ++i) empty->artwork_rgb[i] = 1;
    size_t used = (size_t)(empty - hints->images) + 1;
    if (used > hints->images_used) hints->images_used = used;
    empty->original_enabled = guest_invoke2(c, GUEST_BEHAVIOUR_GET_ENABLED, image, 0) != 0;
    return empty;
}

static void hide_hint_image(Cpu *c, uint64_t image) {
    if (!image) return;
    HintImageState *state = hint_image_state(c, image, true);
    if (state && state->original_enabled)
        guest_invoke2(c, GUEST_BEHAVIOUR_SET_ENABLED, image, false);
}

// Create a plain UI child without cloning localization scripts or scaling.
static uint64_t create_hint_graphic(Cpu *c, uint64_t source, uint64_t parent, uint64_t type, const char *name) {
    if (!hints->rect_type) hints->rect_type = guest_invoke2(c, 0x70008ea5c0ull, 0,
        guest_string(c, "UnityEngine.RectTransform, UnityEngine"));
    if (!hints->type_type) hints->type_type = guest_invoke2(c, 0x70008ea5c0ull, 0,
        guest_string(c, "System.Type"));
    if (!type || !hints->rect_type || !hints->type_type) return 0;
    uint64_t source_go = guest_invoke2(c, 0x700121e720ull, source, 0);
    uint64_t go = guest_invoke2(c, 0x7000b5b110ull, LD64(source_go), 0);
    uint64_t types = guest_invoke4(c, 0x70006e6740ull, 0, hints->type_type, 1, 0);
    if (!go || !types) return 0;
    ST64(types + 0x20, hints->rect_type);
    guest_invoke4(c, 0x7001225f80ull, go, guest_string(c, name), types, 0);
    uint64_t graphic = guest_invoke2(c, 0x70012260e0ull, go, type);
    if (!graphic) return 0;
    uint64_t rect = guest_invoke2(c, 0x700121e6b0ull, graphic, 0);
    guest_invoke4(c, 0x70015d8ec0ull, rect, parent, 0, 0);
    guest_set_vec2(c, 0x700123a050ull, rect, 0.5f, 0.5f); // anchorMin
    guest_set_vec2(c, 0x700123a250ull, rect, 0.5f, 0.5f); // anchorMax
    guest_set_vec2(c, 0x700123a550ull, rect, 0, 0);       // anchoredPosition
    guest_set_vec2(c, 0x700123a9d0ull, rect, 0.5f, 0.5f); // pivot
    guest_invoke2(c, 0x7000114ff0ull, graphic, 0); // no raycast target
    return graphic;
}

static uint64_t create_hint_label(Cpu *c, uint64_t image) {
    if (!hints->text_type) hints->text_type = guest_invoke2(c, 0x70008ea5c0ull, 0,
        guest_string(c, "UnityEngine.UI.Text, UnityEngine.UI"));
    if (!hints->text_type) return 0;
    uint64_t transform = guest_invoke2(c, 0x700121e6b0ull, image, 0);
    uint64_t parent = transform, prototype = 0;
    for (int depth = 0; parent && depth < 8 && !prototype; ++depth) {
        parent = guest_invoke2(c, 0x70015d8be0ull, parent, 0);
        if (parent) prototype = guest_invoke4(c, 0x700121e970ull, parent, hints->text_type, 1, 0);
    }
    if (!prototype) return 0;
    uint64_t font = guest_invoke2(c, 0x70005bce40ull, prototype, 0);
    if (!font) return 0;
    uint64_t text = create_hint_graphic(c, image, transform, hints->text_type, "Keyboard hint");
    if (!text) return 0;
    guest_invoke2(c, 0x70005dcc40ull, text, font);
    uint64_t font_size = guest_invoke2(c, 0x70005dd1e0ull, prototype, 0);
    guest_invoke2(c, 0x70005dd1f0ull, text, font_size);
    guest_invoke2(c, 0x70005c0a40ull, text, 1); // horizontal overflow
    guest_invoke2(c, 0x70005dd290ull, text, 1); // vertical overflow
    guest_invoke2(c, 0x70005dd0f0ull, text, 4); // middle center
    guest_invoke2(c, 0x70005dcf40ull, text, 0); // fixed font size
    return text;
}

static void read_hint_artwork(Cpu *c, HintImageState *state) {
    uint64_t sprite = guest_invoke2(c, 0x700011d4a0ull, state->component, 0); // overrideSprite
    if (sprite == state->sprite) return;
    state->sprite = sprite;
    for (int i = 0; i < 3; ++i) state->artwork_rgb[i] = 1;
    if (!sprite) return;
    uint64_t texture = guest_invoke2(c, 0x70015cf640ull, sprite, 0);
    if (!texture) return;
    static uint64_t native_ptr_call;
    if (!native_ptr_call) {
        const char *call_name = "UnityEngine.Texture::GetNativeTexturePtr";
        char *name = hle_alloc(strlen(call_name) + 1);
        strcpy(name, call_name);
        native_ptr_call = guest_invoke2(c, 0x7000b7cd80ull, (uintptr_t)name, 0);
    }
    uint64_t native = native_ptr_call ? guest_invoke2(c, native_ptr_call, texture, 0) : 0;
    uint64_t host = 0;
    if (native < GUEST_LO || native >= GUEST_HI || !rt_probe_u64(native, &host) || !host) return;
    Cpu saved = *c;
    c->x[0] = sprite; c->x[1] = 0;
    guest_call(c, 0x70015cf720ull); // Sprite.get_textureRect, HFA x/y/w/h
    float rect[4];
    for (int i = 0; i < 4; ++i) memcpy(&rect[i], &c->v[i], sizeof(float));
    *c = saved;
    for (int i = 0; i < 4; ++i)
        if (!isfinite(rect[i]) || rect[i] < 0 || rect[i] > 65536) return;
    nvn_texture_region_color((void *)(uintptr_t)host, rect[0], rect[1], rect[2], rect[3], state->artwork_rgb);
}

static void set_hint_color(Cpu *c, const HintImageState *state, uint64_t text) {
    for (int i = 0; i < 3; ++i) {
        float tint;
        memcpy(&tint, &c->v[i], sizeof tint);
        tint *= state->artwork_rgb[i];
        memcpy(&c->v[i], &tint, sizeof tint);
    }
    Cpu colored = *c;
    c->x[0] = text; c->x[1] = 0;
    guest_call(c, 0x7000114f90ull);
    if (state->keyboard_outline) {
        *c = colored;
        c->x[0] = state->keyboard_outline; c->x[1] = 0;
        guest_call(c, 0x7000114f90ull);
    }
}

static void copy_hint_color(Cpu *c, const HintImageState *state, uint64_t text) {
    Cpu saved = *c;
    c->x[0] = state->component; c->x[1] = 0;
    guest_call(c, 0x7000114f80ull); // Graphic.get_color returns RGBA in v0..v3
    set_hint_color(c, state, text);
    *c = saved;
}

// Follow the replaced graphic itself, including later tint and alpha changes.
// The neighbouring caption supplies only the font, never the label color.
static void update_hint_color(Cpu *c) {
    uint64_t source = c->x[0];
    for (size_t i = 0; i < hints->images_used; ++i) {
        HintImageState *state = &hints->images[i];
        if (state->component != source || !state->keyboard_text) continue;
        Cpu saved = *c;
        set_hint_color(c, state, state->keyboard_text);
        *c = saved;
    }
}

static bool hint_inside_action_button(Cpu *c, uint64_t owner) {
    if (!hints->button_type) hints->button_type = guest_invoke2(c, 0x70008ea5c0ull, 0,
        guest_string(c, "UnityEngine.UI.Button, UnityEngine.UI"));
    if (!hints->image_type) hints->image_type = guest_invoke2(c, 0x70008ea5c0ull, 0,
        guest_string(c, "UnityEngine.UI.Image, UnityEngine.UI"));
    uint64_t button = hints->button_type ? guest_invoke2(c, 0x700121ead0ull, owner, hints->button_type) : 0;
    if (!button || !hints->image_type) return false;
    uint64_t background = guest_invoke2(c, 0x70005d8220ull, button, 0); // Selectable.targetGraphic
    // Footer legends also have Button components, but no separate visible art.
    if (!background || background == owner ||
        guest_invoke2(c, 0x700121e790ull, background, hints->image_type) != background ||
        !guest_invoke2(c, 0x700011d4a0ull, background, 0) ||
        !guest_invoke2(c, GUEST_BEHAVIOUR_GET_ENABLED, background, 0)) return false;
    Cpu saved = *c;
    c->x[0] = background; c->x[1] = 0;
    guest_call(c, 0x7000114f80ull);
    float alpha;
    memcpy(&alpha, &c->v[3], sizeof alpha);
    *c = saved;
    return alpha > 0;
}

static float layout_hint_badge(Cpu *c, HintImageState *group, uint64_t owner, const char *label) {
    if (!group->font_size)
        group->font_size = guest_invoke2(c, 0x70005dd1e0ull, group->keyboard_text, 0);
    bool enclosed = hint_inside_action_button(c, owner);
    int size = enclosed ? (int)(group->font_size * 0.8f) : group->font_size;
    guest_invoke2(c, 0x70005dd1f0ull, group->keyboard_text, size);
    float width = guest_get_float(c, 0x70005dde50ull, group->keyboard_text);
    // Keep unusual/custom key names from consuming an entire dialog row.
    float max_width = group->font_size * 3.5f;
    if (width > max_width && max_width > 0) {
        size = (int)fmaxf(1, floorf(size * max_width / width));
        guest_invoke2(c, 0x70005dd1f0ull, group->keyboard_text, size);
        width = guest_get_float(c, 0x70005dde50ull, group->keyboard_text);
    }
    group->outline_width = group->outline_height = 0;
    if (enclosed) {
        if (!group->keyboard_outline) {
            uint64_t parent = guest_invoke2(c, 0x700121e6b0ull, group->keyboard_text, 0);
            group->keyboard_outline = create_hint_graphic(c, owner, parent, hints->image_type, "Key outline");
        }
        if (group->keyboard_outline) {
            group->circle = strlen(label) == 1;
            group->outline_height = group->font_size * 1.15f;
            group->outline_width = group->circle ? fmaxf(group->outline_height, width + size * 0.3f)
                : fmaxf(group->outline_height, width + size * 0.65f);
            // A wide single glyph still needs a true circle.
            if (group->circle) group->outline_height = group->outline_width;
            uint64_t rect = guest_invoke2(c, 0x700121e6b0ull, group->keyboard_outline, 0);
            guest_set_vec2(c, 0x700123a7d0ull, rect, group->outline_width, group->outline_height);
            width = group->outline_width;
        }
    }
    if (group->keyboard_outline)
        guest_invoke2(c, GUEST_BEHAVIOUR_SET_ENABLED, group->keyboard_outline, enclosed);
    return width + 24;
}

// Outline-only Image geometry uses Unity's white UI texture. Keeping it in the
// normal canvas mesh preserves clipping, transforms, batching and CanvasGroup fades.
static int populate_hint_outline(Cpu *c) {
    HintImageState *state = NULL;
    for (size_t i = 0; i < hints->images_used; ++i)
        if (hints->images[i].keyboard_outline == c->x[0]) { state = &hints->images[i]; break; }
    if (!state || !c->x[0]) return 0;
    Cpu saved = *c;
    uint64_t mesh = c->x[1];
    guest_invoke2(c, 0x7000aed470ull, mesh, 0); // VertexHelper.Clear
    if (state->outline_width > 0 && state->outline_height > 0 && keyboard_prompts_active()) {
        guest_call(c, 0x7000114f80ull); // outline Graphic.get_color
        uint32_t color = 0;
        for (int i = 0; i < 4; ++i) {
            float channel;
            memcpy(&channel, &c->v[i], sizeof channel);
            color |= (uint32_t)lroundf(fminf(1, fmaxf(0, channel)) * 255) << (8 * i);
        }
        float hw = state->outline_width * 0.5f, hh = state->outline_height * 0.5f;
        float radius = state->circle ? hh : hh * 0.45f;
        float stroke = fmaxf(2, state->font_size * 0.045f);
        float feather = fmaxf(1, state->font_size * 0.02f);
        int segments = state->circle ? 48 : 36;
        for (int i = 0; i < segments; ++i) {
            float angle, cx = 0, cy = 0;
            if (state->circle) angle = i * (6.28318530718f / segments);
            else {
                int corner = i / 9;
                angle = (corner + (i % 9) / 8.0f) * 1.57079632679f;
                cx = (corner == 0 || corner == 3 ? 1 : -1) * (hw - radius);
                cy = (corner < 2 ? 1 : -1) * (hh - radius);
            }
            for (int ring = 0; ring < 4; ++ring) {
                float r = radius + (ring == 0 ? feather : ring == 1 ? 0 : ring == 2 ? -stroke : -stroke - feather);
                float vertex[] = {cx + cosf(angle) * r, cy + sinf(angle) * r, 0, 0.5f, 0.5f};
                *c = saved;
                c->x[0] = mesh; c->x[1] = ring == 0 || ring == 3 ? color & 0x00ffffff : color;
                c->x[2] = 0;
                for (int k = 0; k < 5; ++k) memcpy(&c->v[k], &vertex[k], sizeof(float));
                guest_call(c, 0x7000aedd60ull); // AddVert(Vector3, Color32, Vector2)
            }
        }
        for (int i = 0; i < segments; ++i) {
            int a = i * 4, b = ((i + 1) % segments) * 4;
            for (int ring = 0; ring < 3; ++ring) {
                guest_invoke4(c, 0x7000aedf00ull, mesh, a + ring, b + ring, a + ring + 1);
                guest_invoke4(c, 0x7000aedf00ull, mesh, a + ring + 1, b + ring, b + ring + 1);
            }
        }
    }
    *c = saved;
    return 1;
}

// Reserve width on the direct child of a horizontal layout, rather than an
// icon nested inside that child. Otherwise longer names overlap the caption.
static uint64_t hint_layout_owner(Cpu *c, uint64_t owner, bool *centered) {
    if (!hints->horizontal_type) hints->horizontal_type = guest_invoke2(c, 0x70008ea5c0ull, 0,
        guest_string(c, "UnityEngine.UI.HorizontalLayoutGroup, UnityEngine.UI"));
    uint64_t child = guest_invoke2(c, 0x700121e6b0ull, owner, 0);
    for (int depth = 0; child && hints->horizontal_type && depth < 4; ++depth) {
        uint64_t parent = guest_invoke2(c, 0x70015d8be0ull, child, 0);
        if (!parent) break;
        if (guest_invoke2(c, 0x700121e790ull, parent, hints->horizontal_type)) {
            *centered = true;
            return child;
        }
        child = parent;
    }
    *centered = false;
    return owner;
}

static void apply_image_group(Cpu *c, uint64_t image, int id, uint64_t owner, bool sprite_art) {
    char label[128];
    if (!image || !hint_key_label(id, label, sizeof label)) return;
    bool keyboard = keyboard_prompts_active();
    HintImageState *state = hint_image_state(c, image, keyboard);
    if (!state) return;
    if (!keyboard) {
        if (state->keyboard_text)
            guest_invoke2(c, GUEST_BEHAVIOUR_SET_ENABLED, state->keyboard_text, 0);
        if (state->keyboard_layout)
            guest_invoke2(c, GUEST_BEHAVIOUR_SET_ENABLED, state->keyboard_layout, 0);
        if (state->keyboard_outline)
            guest_invoke2(c, GUEST_BEHAVIOUR_SET_ENABLED, state->keyboard_outline, 0);
        guest_invoke2(c, GUEST_BEHAVIOUR_SET_ENABLED, image, state->original_enabled);
        return;
    }
    HintImageState *group = owner == image ? state : hint_image_state(c, owner, true);
    if (!group) return;
    if (!group->keyboard_text) {
        group->keyboard_text = create_hint_label(c, owner);
    }
    state->keyboard_text = group->keyboard_text;
    if (!state->keyboard_text) return; // keep original art if no usable font exists yet
    uint64_t text = state->keyboard_text;
    uint64_t rect = guest_invoke2(c, 0x700121e6b0ull, text, 0);
    guest_invoke2(c, GUEST_TEXT_SET, text, guest_string(c, label));
    // Let the original layout group reserve the width of the mapped label.
    if (!hints->layout_type) hints->layout_type = guest_invoke2(c, 0x70008ea5c0ull, 0,
        guest_string(c, "UnityEngine.UI.LayoutElement, UnityEngine.UI"));
    if (!group->keyboard_layout && hints->layout_type) {
        uint64_t layout_owner = hint_layout_owner(c, owner, &group->centered_layout);
        uint64_t go = guest_invoke2(c, 0x700121e720ull, layout_owner, 0);
        group->keyboard_layout = guest_invoke2(c, 0x70012260e0ull, go, hints->layout_type);
        guest_invoke2(c, 0x70005c2800ull, group->keyboard_layout, 10);
    }
    float width = layout_hint_badge(c, group, owner, label);
    guest_set_vec2(c, 0x700123a7d0ull, rect, width, fmaxf(group->font_size * 1.5f, group->outline_height));
    // Layouts center the expanded slot; fixed slots keep the badge's left edge.
    guest_set_vec2(c, 0x700123a550ull, rect,
        group->centered_layout ? 0 : (group->outline_width - group->outline_height) * 0.5f, 0);
    state->keyboard_outline = group->keyboard_outline;
    state->outline_width = group->outline_width;
    state->outline_height = group->outline_height;
    state->font_size = group->font_size;
    state->circle = group->circle;
    state->keyboard_layout = group->keyboard_layout;
    if (state->keyboard_layout) {
        guest_set_vec2(c, 0x70005c2380ull, state->keyboard_layout, width, 0);
        guest_invoke2(c, GUEST_BEHAVIOUR_SET_ENABLED, state->keyboard_layout, 1);
    }
    if (sprite_art) read_hint_artwork(c, state);
    copy_hint_color(c, state, text);
    guest_invoke2(c, GUEST_BEHAVIOUR_SET_ENABLED, text, group->original_enabled);
    guest_invoke2(c, GUEST_BEHAVIOUR_SET_ENABLED, image, 0);
    static int logged;
    if (logged++ < 12) LOG("[hints] image %llx id %d -> %s\n", (unsigned long long)image, id, label);
}

static void apply_image_label(Cpu *c, uint64_t image, int id) {
    apply_image_group(c, image, id, image, true);
}

static void apply_hint_text(Cpu *c) {
    uint64_t owner = c->x[0], text = c->x[1];
    if (!owner || !text) return;
    int id = (int32_t)LD32(owner + 0x18);
    char label[128];
    if (!hint_key_label(id, label, sizeof label)) return;

    // Inline directional shorthand belongs to the game's sentence layout.
    bool keyboard = keyboard_prompts_active() && !hint_directional(id);
    HintTextState *state = hint_text_state(c, text, keyboard);
    if (state) state->owner = owner;
    if (keyboard) {
        uint64_t value = guest_string(c, label);
        if (!value) return;
        guest_invoke2(c, GUEST_TEXT_SET, text, value);
        guest_invoke2(c, GUEST_BEHAVIOUR_SET_ENABLED, text, 1);
    } else if (state) {
        guest_invoke2(c, GUEST_TEXT_SET, text, state->original_text);
        guest_invoke2(c, GUEST_BEHAVIOUR_SET_ENABLED, text, state->original_enabled);
    }
}

static void apply_hint_image(Cpu *c, bool sprite_art) {
    if (!c->x[0] || !c->x[1]) return;
    if (!sprite_art) {
        // RefreshGraphics accepts any Graphic. Only Images have a sprite.
        if (!hints->image_type) hints->image_type = guest_invoke2(c, 0x70008ea5c0ull, 0,
            guest_string(c, "UnityEngine.UI.Image, UnityEngine.UI"));
        sprite_art = hints->image_type && guest_invoke2(c, 0x700121e790ull, c->x[1], hints->image_type) == c->x[1];
    }
    apply_image_group(c, c->x[1], (int32_t)LD32(c->x[0] + 0x18), c->x[0], sprite_art);
}

static void apply_toggle_hint(Cpu *c) {
    uint64_t graphic = LD64(c->x[0] + 0x118);  // SFToggle._SwitchGraphic
    if (!graphic) return;
    bool keyboard = keyboard_prompts_active();
    if (keyboard) hide_hint_image(c, graphic);
    else {
        HintImageState *state = hint_image_state(c, graphic, false);
        if (state) guest_invoke2(c, GUEST_BEHAVIOUR_SET_ENABLED, graphic, state->original_enabled);
    }
}

static void forget_hint_component(uint64_t component) {
    uint64_t text = 0;
    for (size_t i = 0; i < HINT_COMPONENT_CAP; ++i)
        if (hints->images[i].component == component) text = hints->images[i].keyboard_text;
    for (size_t i = 0; i < HINT_COMPONENT_CAP; ++i) {
        if (hints->images[i].component == component || (text && hints->images[i].keyboard_text == text))
            hints->images[i] = (HintImageState){0};
        if (hints->texts[i].component == component || hints->texts[i].owner == component)
            hints->texts[i] = (HintTextState){0};
    }
}

RT_ON_RESET(reset_keyboard_hint_state) {
    free(hints);
    hints = NULL;
}

// Call only before guest threads start (or after they have stopped). INI
// experiments require a restart; guest UI objects are never hot-unhooked.
static void reset_developer_state(void);
static void reset_lan_ui(void);
void rt_hooks_configure(void) {
    LOG("[network] %s\n", g_settings.network_enabled ? "original network paths enabled" : "disabled (offline policy)");
    reset_keyboard_hint_state();
    reset_developer_state();
    reset_lan_ui();
    replay_init();
    rt_hook_features =
#ifdef __ANDROID__
                       RT_HOOK_MOBILE_UI |
#endif
                       (g_settings.experimental_keyboard_prompts ? RT_HOOK_KEYBOARD_PROMPTS : 0) |
                       (replay_enabled() ? RT_HOOK_REPLAY : 0) |
                       (g_settings.allow_sparring_lan_multiplayer ? 4u : 0);
}

// The Switch standard-stream getters return null. Corlib FileStream expects a
// live position/length handle even when the host has no guest console stream.
static int console_handle(Cpu *c) {
    static uint64_t handle;
    if (!handle) handle = (uint64_t)(uintptr_t)hle_alloc(0x40);
    c->x[0] = handle;
    return 1;
}

// PvpSettings.UpdateCharactersLock operates on the sparring roster, including
// inlined save checks. Override only its transient PvpCharacter.isOpened flags;
// leave MainSettings and the saved UnlockedCharacters list untouched.
static int unlock_sparring_characters(Cpu *c) {
    if (!g_settings.unlock_all_sparring_characters || !c->x[0]) return 0;
    uint64_t characters = LD64(c->x[0] + 0x18);
    if (!characters) return 0;
    uint64_t count = LD64(characters + 0x18);
    for (uint64_t i = 0; i < count; ++i) {
        uint64_t character = LD64(characters + 0x20 + i * 8);
        if (character) ST8(character + 0x58, 1);
    }
    return 1;
}

static int keyboard_hint_hook(Cpu *c, uint64_t site) {
    if (!(rt_hook_features & RT_HOOK_KEYBOARD_PROMPTS)) return 0;
    if (!ensure_keyboard_hint_state()) return 0;
    switch (site) {
    case 0x700011efb0ull:  // Image.OnPopulateMesh: only our generated outline Images
        return populate_hint_outline(c);
    case 0x7000114f90ull:  // Graphic.set_color
        update_hint_color(c);
        return 0;
    case 0x7000cd7130ull:  // ButtonHintUI.RefreshHintIcon (a separate image-only prefab)
        if (c->x[0]) apply_image_label(c, LD64(c->x[0] + 0x18), (int32_t)LD32(c->x[0] + 0x20));
        return 0;
    case 0x7000cdb690ull: {  // HintIDExtension.GetButtonHintText(HintID)
        if (!keyboard_prompts_active() || hint_directional((int32_t)c->x[1])) return 0;
        char label[128];
        if (!hint_key_label((int32_t)c->x[1], label, sizeof label)) return 0;
        uint64_t value = guest_string(c, label);
        if (!value) return 0;
        c->x[0] = value;
        return 1;
    }
    case 0x7000ce0dd0ull:  // HintUI.OnDestroy
    case 0x7000d9aec0ull:  // ResolutionImage.OnDestroy
        forget_hint_component(c->x[0]);
        return 0;
    case 0x7000ce0f40ull:  // HintUI.RefreshGraphics (e.g. the + Menu hint)
        apply_hint_image(c, false);
        return 0;
    case 0x7000ce0de0ull:  // HintUI.RefreshImage
        apply_hint_image(c, true);
        return 0;
    case 0x7000ce10a0ull:  // HintUI.RefreshText
        apply_hint_text(c);
        return 0;
    case 0x7000f9a060ull:  // SFToggle.RefreshControlHint
        apply_toggle_hint(c);
        return 0;
    default:
        return 0;
    }
}

static bool guest_object_named(Cpu *c, uint64_t object, const char *expected) {
    if (!object) return false;
    uint64_t name = guest_invoke2(c, 0x700122e460ull, object, 0);
    size_t length = strlen(expected);
    if (!name || LD32(name + 0x10) != length) return false;
    for (size_t i = 0; i < length; ++i)
        if (LD16(name + 0x14 + i * 2) != (unsigned char)expected[i]) return false;
    return true;
}

#include "developer_hooks.inc"
#include "lan_ui_hooks.inc"
#include "platform_ui_hooks.inc"

// The shipped MainSection has a fully wired, localized Exit item. Reveal it
// before GeneralMenuList.Awake collects active buttons and installs navigation.
static void enable_desktop_exit(Cpu *c, uint64_t list) {
    uint64_t root = guest_invoke2(c, 0x700121e6b0ull, list, 0);
    if (!guest_object_named(c, root, "MainSection")) return;
    int children = guest_invoke2(c, 0x70015d9e00ull, root, 0);
    for (int i = 0; i < children; ++i) {
        uint64_t content = guest_invoke2(c, 0x70015da1f0ull, root, i);
        if (!guest_object_named(c, content, "ListContent")) continue;
        int items = guest_invoke2(c, 0x70015d9e00ull, content, 0);
        for (int j = 0; j < items; ++j) {
            uint64_t item = guest_invoke2(c, 0x70015da1f0ull, content, j);
            if (!guest_object_named(c, item, "MenuItemButton_exit")) continue;
            uint64_t object = guest_invoke2(c, 0x700121e720ull, item, 0);
            if (object) {
                lan_capture_menu_item(c,item);
                guest_invoke2(c, 0x7001226480ull, object, 1);
                LOG("[menu] enabled desktop Exit item\n");
            }
            return;
        }
    }
}

#include "widescreen_hooks.inc"

static MenuMouse menu_mouse;
RT_ON_RESET(reset_menu_mouse_frame) { memset(&menu_mouse, 0, sizeof menu_mouse); }

// Hook BaseInput rather than global UnityEngine.Input: this enables the existing
// menu raycasters, pointer enter/exit, click and drag handlers without changing
// the Switch controller input used by fights.
static int menu_mouse_hook(Cpu *c, uint64_t site) {
    switch (site) {
    case 0x70000fd740ull: {  // current EventSystem.Update, before TickModules
        lan_ui_pump(c);
        unsigned width = guest_invoke2(c, 0x70015ccb90ull, 0, 0);
        unsigned height = guest_invoke2(c, 0x70015ccc00ull, 0, 0);
        MenuMouse previous = menu_mouse;
        menu_mouse = menu_mouse_frame(width, height, developer_console_open());
#ifdef __ANDROID__
        android_touch_count = menu_touch_frame(width, height, developer_console_open(), android_touches);
        // The Sparring roster uses a custom cell hit-test rather than Unity
        // Buttons. Feed only that helper the first real contact; the actual
        // EventSystem receives all contacts through UnityEngine.Input below.
        MenuMouse touch_mouse = {.x = -10000, .y = -10000};
        for (unsigned i = 0; i < android_touch_count; ++i) {
            MenuTouch *t = &android_touches[i];
            if (t->finger != 0) continue;
            touch_mouse.x = t->x; touch_mouse.y = t->y;
            touch_mouse.pressed = t->phase == TOUCH_BEGAN;
            touch_mouse.released = t->phase == TOUCH_ENDED;
            touch_mouse.held = t->phase < TOUCH_ENDED;
        }
        android_primary_touch=android_touch_count?touch_mouse:menu_mouse;
        sparring_mouse_frame(c, android_touch_count ? &touch_mouse : &menu_mouse);
#else
        sparring_mouse_frame(c,&menu_mouse);
#endif
        if (ENV("LARECOMP_MOUSE_TRACE") &&
            (previous.x != menu_mouse.x || previous.y != menu_mouse.y || menu_mouse.pressed || menu_mouse.released))
            LOG("[mouse] screen %ux%u pointer %.1f/%.1f held=%x down=%x up=%x\n", width, height,
                menu_mouse.x, menu_mouse.y, menu_mouse.held, menu_mouse.pressed, menu_mouse.released);
        return 0;
    }
    case 0x70000fa8f0ull: c->x[0] = 1; return 1;  // mousePresent: enable StandaloneInputModule
    // The Switch backend reports 16 touch slots even without contact. Unity's
    // ProcessTouchEvents then suppresses ProcessMouseEvent on every update.
    // Override only UI BaseInput; raw game/touch/controller APIs stay original.
    case 0x70000fabe0ull: case 0x70000fac50ull:
#ifdef __ANDROID__
        c->x[0] = site == 0x70000fabe0ull ? 1 : android_touch_count;
#else
        c->x[0] = 0;
#endif
        return 1;
    case 0x70000fa960ull: case 0x70000fa9d0ull: case 0x70000faa40ull: {
        unsigned button = (uint32_t)c->x[1];
        unsigned bits = site == 0x70000fa960ull ? menu_mouse.pressed :
                        site == 0x70000fa9d0ull ? menu_mouse.released : menu_mouse.held;
        c->x[0] = button < 3 && (bits & (1u << button)) != 0;
        return 1;
    }
    case 0x70000faab0ull: case 0x70000fab70ull: {
        float x = site == 0x70000faab0ull ? menu_mouse.x : menu_mouse.scroll_x;
        float y = site == 0x70000faab0ull ? menu_mouse.y : menu_mouse.scroll_y;
        c->v[0] = c->v[1] = 0;
        memcpy(&c->v[0], &x, sizeof x);
        memcpy(&c->v[1], &y, sizeof y);
        return 1;
    }
    default: return -1;
    }
}

int rt_hook(Cpu *c, uint64_t site) {
    int platform_result = platform_ui_hook(c, site);
    if (platform_result >= 0) return platform_result;
    int lan_result = lan_ui_hook(c,site);
    if(lan_result>=0)return lan_result;
    int mouse_result = menu_mouse_hook(c, site);
    if (mouse_result >= 0) return mouse_result;
    int wide_result = widescreen_hook(c, site);
    if (wide_result >= 0) return wide_result;
    if (network_hook(c, site)) return 1;
    switch (site) {
    case 0x7000c99e30ull:
    case 0x7000c9dd50ull:
    case 0x7000ca7c00ull:
    case 0x7001177d10ull:
    case 0x7001177d20ull:
    case 0x7001237b50ull:
    case 0x7001237bc0ull:
    case 0x70010ed6a0ull:
    case 0x70010f1450ull:
    case 0x70010f1550ull:
    case 0x70010f1600ull:
    case 0x70010f1710ull:
    case 0x70010f17d0ull:
    case 0x70010f18e0ull:
    case 0x70010f19a0ull:
    case 0x70010f1ab0ull:
    case 0x70010f1b80ull:
    case 0x70010f1c50ull:
    case 0x70010f1d60ull:
    case 0x70010f1e80ull:
    case 0x70010f1f90ull:
    case 0x70010f2de0ull:
    case 0x7000ca7e80ull:
    case 0x7000ca6df0ull:
    case 0x7000d961f0ull:
    case 0x7000c9de18ull:
    case 0x7000c9de40ull:
    case 0x7000ff7af0ull:
    case 0x7000ff7c40ull:
        return replay_hook(c, site);
    case 0x7000fc9230ull:  // PvpSettings.UpdateCharactersLock
        return unlock_sparring_characters(c);
    case 0x7000e6b820ull:
        enable_desktop_exit(c, c->x[0]);
        dev_ensure_ui(c);
        return 0;
    case 0x7000e6ef20ull:  // GeneralMenuScene.ShowExit
        menu_request_quit();
        return 1;
    case 0x7001217700ull: {  // UnityEngine.Application.set_targetFrameRate(int)
        int requested = (int32_t)c->x[1];
        int configured = g_settings.target_fps;
        if (requested != configured) {
            static int logged;
            if (logged++ < 8)
                LOG("[fps] Unity targetFrameRate %d -> configured %d\n", requested, configured);
            c->x[1] = (uint32_t)configured;
        }
        return 0;  // keep the game's original setter; replace its requested rate
    }
    case 0x7000844c30ull:
    case 0x7000844c40ull:
    case 0x7000844c50ull:
    case 0x7000386660ull:
    case 0x7000386650ull:
    case 0x7000386670ull:
        return console_handle(c);
    default:
        { int result = developer_hook(c, site); if (result >= 0) return result; }
        return keyboard_hint_hook(c, site);
    }
}
