#include "constants/core.h"
// NACP fields follow the vendored libpietendo nacp.h layout: display_version
// at 0x3060 (16 UTF-8 bytes), followed by little-endian add_on_content_base_id.
#include "game_info.h"
#include "platform.h"

#include <stdio.h>
#include <string.h>

static char display_version[SF2C_GAME_INFO_N_17_4523];
static char window_title[SF2C_GAME_INFO_N_128_2747] = SF2_APP_NAME;
static char icon_path[SF2C_GAME_INFO_N_4096_8B92];
static uint64_t add_on_base_id;

static bool readable_file(const char *root, const char *name, char *out, size_t size) {
    if (snprintf(out, size, SF2C_GAME_INFO_S_S_S_22D8, root, name) >= size) return false;
    FILE *f = fopen(out, SF2C_GAME_INFO_S_RB_46CA);
    if (!f) return false;
    fclose(f);
    return true;
}

void game_info_load(const char *game_root) {
    display_version[0] = icon_path[0] = 0;
    add_on_base_id = 0;
    snprintf(window_title, sizeof window_title, SF2C_GAME_INFO_S_S_0030, SF2_APP_NAME);
    char exe[SF2C_GAME_INFO_N_4096_8B92] = {0}, path[SF2C_GAME_INFO_N_4096_8B92];
    if (plat_exe_path(exe, sizeof exe)) {
        char *slash = strrchr(exe, SF2C_GAME_INFO_CH_EMPTY_0825);
        if (slash) *slash = 0;
        else exe[0] = 0;
    }
    const char *roots[] = {game_root, exe};
    for (size_t i = 0; i < SF2C_GAME_INFO_N_2_D473; ++i) {
        if (!roots[i][0] || !readable_file(roots[i], SF2C_GAME_INFO_S_CONTROL_NACP_CE21, path, sizeof path)) continue;
        FILE *f = fopen(path, SF2C_GAME_INFO_S_RB_46CA);
        unsigned char fields[SF2C_GAME_INFO_N_24_C235];
        bool ok = f && !fseek(f, SF2C_GAME_INFO_N_0X3060_FE40, SEEK_SET) && fread(fields, 1, sizeof fields, f) == sizeof fields;
        if (f) fclose(f);
        if (!ok) continue;
        memcpy(display_version, fields, SF2C_GAME_INFO_N_16_B17E);
        display_version[SF2C_GAME_INFO_N_16_B17E] = 0;
        // Never put control characters from malformed metadata in a native title.
        for (size_t n = 0; display_version[n]; ++n)
            if ((unsigned char)display_version[n] < SF2C_GAME_INFO_N_0X20_A766 || display_version[n] == SF2C_GAME_INFO_N_0X7F_0AFB) {
                display_version[0] = 0;
                break;
            }
        for (unsigned n = 0; n < SF2C_GAME_INFO_N_8_2C62; ++n) add_on_base_id |= (uint64_t)fields[SF2C_GAME_INFO_N_16_B17E + n] << (SF2C_GAME_INFO_N_8_2C62 * n);
        break;
    }
    if (display_version[0]) {
        snprintf(window_title, sizeof window_title, SF2C_GAME_INFO_S_S_S_33DC, SF2_APP_NAME, display_version);
        fprintf(stderr, SF2C_GAME_INFO_S_GAME_OFFICIAL_DISPLAY_VERSIO_3688, display_version);
    } else fprintf(stderr, SF2C_GAME_INFO_S_GAME_ORIGINAL_CONTROL_METADA_67B2);
    for (size_t i = 0; i < SF2C_GAME_INFO_N_2_D473; ++i)
        if (roots[i][0] && readable_file(roots[i], SF2C_GAME_INFO_S_ICON_PNG_267C, icon_path, sizeof icon_path)) return;
    icon_path[0] = 0;
}

const char *game_display_version(void) { return display_version; }
const char *game_window_title(void) { return window_title; }
const char *game_icon_path(void) { return icon_path; }
uint64_t game_add_on_content_base_id(void) { return add_on_base_id; }
uint32_t game_application_version(void) {
    unsigned major, minor, patch;
    char tail;
    if (sscanf(display_version, SF2C_GAME_INFO_S_U_U_U_C_F5C2, &major, &minor, &patch, &tail) != SF2C_GAME_INFO_N_3_4E07 ||
        major > SF2C_GAME_INFO_N_0X3FF_6900 || minor > SF2C_GAME_INFO_N_0X3FF_6900 || patch > SF2C_GAME_INFO_N_0XFFF_905E) return 0;
    return (major << SF2C_GAME_INFO_N_22_785F) | (minor << SF2C_GAME_INFO_N_12_6B51) | patch;
}
