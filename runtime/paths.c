#include "constants/core.h"
// Portable SF2 output: configuration beside the executable, writable state in
// that folder unless a session explicitly selects another data root.
#include "paths.h"
#include "platform.h"
#include "platform/path_util.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char g_root[PATH_MAX];

static bool executable_dir(char *out, size_t size) {
    if (!plat_exe_path(out, size)) return false;
    char *slash = strrchr(out, SF2C_PATHS_CH_EMPTY_0825);
    if (!slash) return false;
    if (slash == out || (slash == out + SF2C_PATHS_N_2_D473 && out[1] == SF2C_PATHS_CH_EMPTY_4266)) slash[1] = 0;
    else *slash = 0;
    return true;
}

static bool absolute_path(const char *path, char *out, size_t size) {
#ifdef _WIN32
    // _fullpath also resolves drive-relative C:foo and root-relative \foo.
    if (!_fullpath(out, path, size)) return false;
    for (char *p = out; *p; p++) if (*p == SF2C_PATHS_CH_EMPTY_6672) *p = SF2C_PATHS_CH_EMPTY_0825;
    return true;
#else
    if (*path == SF2C_PATHS_CH_EMPTY_0825) return snprintf(out, size, SF2C_PATHS_S_S_0030, path) < (int)size;
    char cwd[PATH_MAX];
    return getcwd(cwd, sizeof cwd) && snprintf(out, size, SF2C_PATHS_S_S_S_22D8, cwd, path) < (int)size;
#endif
}

static bool mkdirs(const char *path) {
    char copy[PATH_MAX];
    if (snprintf(copy, sizeof copy, SF2C_PATHS_S_S_0030, path) >= sizeof copy) return false;
#ifdef _WIN32
    size_t root = plat_path_root(copy, 1);
#else
    size_t root = plat_path_root(copy, 0);
#endif
    if (!root) { errno = EINVAL; return false; }
    if (root == strlen(copy)) {
        struct stat st;
        return stat(copy, &st) == 0 && S_ISDIR(st.st_mode);
    }
    for (char *p = copy + root; ; ++p) {
        if (*p && *p != SF2C_PATHS_CH_EMPTY_0825) continue;
        char end = *p;
        *p = 0;
        if (mkdir(copy, SF2C_PATHS_N_0755_64A5) && errno != EEXIST) return false;
        struct stat st;
        if (stat(copy, &st) || !S_ISDIR(st.st_mode)) return false;
        *p = end;
        if (!end) break;
    }
    return true;
}

bool rt_data_root_early(char *out, size_t size) {
    const char *root = getenv(SF2C_PATHS_S_LARECOMP_DATA_ROOT_B22A);
    if (!root || !*root) root = getenv(SF2C_PATHS_S_LARECOMP_DATA_HOME_1E74);
    return root && *root ? absolute_path(root, out, size) : executable_dir(out, size);
}

void rt_paths_init(void) {
    char config[PATH_MAX], exe[PATH_MAX];
    const char *override = getenv(SF2C_PATHS_S_SF2_CONFIG_3707);
    // Resolve a relative override before entering the writable data directory.
    bool ok = override && *override ? absolute_path(override, config, sizeof config) :
        executable_dir(exe, sizeof exe) &&
        snprintf(config, sizeof config, SF2C_PATHS_S_S_SF2RECOMP_INI_76B3, exe) < sizeof config;
    if (!ok || setenv(SF2C_PATHS_S_SF2_CONFIG_3707, config, 1) ||
        !rt_data_root_early(g_root, sizeof g_root) || !mkdirs(g_root) || chdir(g_root)) {
        fprintf(stderr, SF2C_PATHS_S_SF2_RECOMP_CANNOT_INITIALIZE_3A6E, strerror(errno));
        exit(1);
    }
    if (!getcwd(g_root, sizeof g_root)) {
        perror(SF2C_PATHS_S_SF2_RECOMP_DATA_DIRECTORY_47C9);
        exit(1);
    }
#ifdef _WIN32
    for (char *p = g_root; *p; p++) if (*p == SF2C_PATHS_CH_EMPTY_6672) *p = SF2C_PATHS_CH_EMPTY_0825;
#endif
    fprintf(stderr, SF2C_PATHS_S_PATHS_DATA_S_N_3917, g_root);
}

const char *rt_data_root(void) { return g_root; }
