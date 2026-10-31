// CRT command-line quoting, independent of Windows headers for host-side tests.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static inline bool win_append_arg(char *out, size_t cap, const char *arg) {
    size_t n = strlen(out);
    bool quote = !*arg || strpbrk(arg, " \t\"") != NULL;
#define WIN_PUT(c) do { if (n + 1 >= cap) return false; out[n++] = (c); out[n] = 0; } while (0)
    if (n) WIN_PUT(' ');
    if (quote) WIN_PUT('"');
    for (const char *p = arg; *p;) {
        size_t slashes = 0;
        while (*p == '\\') { slashes++; p++; }
        size_t count = !*p ? (quote ? 2 * slashes : slashes) : *p == '"' ? 2 * slashes + 1 : slashes;
        for (size_t i = 0; i < count; i++) WIN_PUT('\\');
        if (*p) WIN_PUT(*p++);
    }
    if (quote) WIN_PUT('"');
#undef WIN_PUT
    return true;
}
