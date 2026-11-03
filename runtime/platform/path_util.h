#include "../constants/platform.h"
// Root prefixes in normalized paths. Kept OS-independent for path regression tests.
#pragma once
#include <stddef.h>
#include <string.h>
static inline size_t plat_path_root(const char *path, int windows) {
    if (windows && path[0] && path[1] == SF2C_PATH_UTIL_CH_EMPTY_4266 && path[SF2C_PATH_UTIL_N_2_D473] == SF2C_PATH_UTIL_CH_EMPTY_0825) return SF2C_PATH_UTIL_N_3_4E07;
    if (windows && path[0] == SF2C_PATH_UTIL_CH_EMPTY_0825 && path[1] == SF2C_PATH_UTIL_CH_EMPTY_0825) {
        const char *server = strchr(path + SF2C_PATH_UTIL_N_2_D473, SF2C_PATH_UTIL_CH_EMPTY_0825);
        if (!server || server == path + SF2C_PATH_UTIL_N_2_D473 || !server[1]) return 0;
        const char *share = strchr(server + 1, SF2C_PATH_UTIL_CH_EMPTY_0825);
        return share ? (size_t)(share - path + 1) : strlen(path);
    }
    return path[0] == SF2C_PATH_UTIL_CH_EMPTY_0825 ? 1 : 0;
}
