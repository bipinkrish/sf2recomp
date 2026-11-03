#include "../constants/platform.h"
// CRT command-line quoting, independent of Windows headers for host-side tests.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static inline bool win_append_arg(char *out, size_t cap, const char *arg) {
    size_t n = strlen(out);
    bool quote = !*arg || strpbrk(arg, SF2C_WINDOWS_CMDLINE_S_T_04EF) != NULL;
#define WIN_PUT(c) do { if (n + 1 >= cap) return false; out[n++] = (c); out[n] = 0; } while (0)
    if (n) WIN_PUT(SF2C_WINDOWS_CMDLINE_CH_EMPTY_730D);
    if (quote) WIN_PUT(SF2C_WINDOWS_CMDLINE_S_EMPTY_3251);
    for (const char *p = arg; *p;) {
        size_t slashes = 0;
        while (*p == SF2C_WINDOWS_CMDLINE_CH_EMPTY_6672) { slashes++; p++; }
        size_t count = !*p ? (quote ? SF2C_WINDOWS_CMDLINE_N_2_D473 * slashes : slashes) : *p == SF2C_WINDOWS_CMDLINE_S_EMPTY_3251 ? SF2C_WINDOWS_CMDLINE_N_2_D473 * slashes + 1 : slashes;
        for (size_t i = 0; i < count; i++) WIN_PUT(SF2C_WINDOWS_CMDLINE_CH_EMPTY_6672);
        if (*p) WIN_PUT(*p++);
    }
    if (quote) WIN_PUT(SF2C_WINDOWS_CMDLINE_S_EMPTY_3251);
#undef WIN_PUT
    return true;
}
