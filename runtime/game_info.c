// NACP fields follow the vendored libpietendo nacp.h layout: display_version
// at 0x3060 (16 UTF-8 bytes), followed by little-endian add_on_content_base_id.
#include "game_info.h"
#include "platform.h"

#include <stdio.h>
#include <string.h>

static char display_version[17];
static char window_title[128] = SF2_APP_NAME;
static char icon_path[4096];
static uint64_t add_on_base_id;

static bool readable_file(const char *root, const char *name, char *out, size_t size) {
    if (snprintf(out, size, "%s/%s", root, name) >= size) return false;
    FILE *f = fopen(out, "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

void game_info_load(const char *game_root) {
    display_version[0] = icon_path[0] = 0;
    add_on_base_id = 0;
    snprintf(window_title, sizeof window_title, "%s", SF2_APP_NAME);
    char exe[4096] = {0}, path[4096];
    if (plat_exe_path(exe, sizeof exe)) {
        char *slash = strrchr(exe, '/');
        if (slash) *slash = 0;
        else exe[0] = 0;
    }
    const char *roots[] = {game_root, exe};
    for (size_t i = 0; i < 2; ++i) {
        if (!roots[i][0] || !readable_file(roots[i], "control.nacp", path, sizeof path)) continue;
        FILE *f = fopen(path, "rb");
        unsigned char fields[24];
        bool ok = f && !fseek(f, 0x3060, SEEK_SET) && fread(fields, 1, sizeof fields, f) == sizeof fields;
        if (f) fclose(f);
        if (!ok) continue;
        memcpy(display_version, fields, 16);
        display_version[16] = 0;
        // Never put control characters from malformed metadata in a native title.
        for (size_t n = 0; display_version[n]; ++n)
            if ((unsigned char)display_version[n] < 0x20 || display_version[n] == 0x7f) {
                display_version[0] = 0;
                break;
            }
        for (unsigned n = 0; n < 8; ++n) add_on_base_id |= (uint64_t)fields[16 + n] << (8 * n);
        break;
    }
    if (display_version[0]) {
        snprintf(window_title, sizeof window_title, "%s - %s", SF2_APP_NAME, display_version);
        fprintf(stderr, "[game] official display version %s (control.nacp)\n", display_version);
    } else fprintf(stderr, "[game] original control metadata unavailable; no display version claimed\n");
    for (size_t i = 0; i < 2; ++i)
        if (roots[i][0] && readable_file(roots[i], "icon.png", icon_path, sizeof icon_path)) return;
    icon_path[0] = 0;
}

const char *game_display_version(void) { return display_version; }
const char *game_window_title(void) { return window_title; }
const char *game_icon_path(void) { return icon_path; }
uint64_t game_add_on_content_base_id(void) { return add_on_base_id; }
uint32_t game_application_version(void) {
    unsigned major, minor, patch;
    char tail;
    if (sscanf(display_version, "%u.%u.%u%c", &major, &minor, &patch, &tail) != 3 ||
        major > 0x3ff || minor > 0x3ff || patch > 0xfff) return 0;
    return (major << 22) | (minor << 12) | patch;
}
