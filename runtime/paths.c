// Portable SF2 output: configuration beside the executable, writable state in
// that folder unless a session explicitly selects another data root.
#include "paths.h"
#include "platform.h"

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
    char *slash = strrchr(out, '/');
    if (!slash) return false;
    if (slash == out) slash[1] = 0;
    else *slash = 0;
    return true;
}

static bool absolute_path(const char *path, char *out, size_t size) {
    if (*path == '/') return snprintf(out, size, "%s", path) < (int)size;
    char cwd[PATH_MAX];
    return getcwd(cwd, sizeof cwd) && snprintf(out, size, "%s/%s", cwd, path) < (int)size;
}

static bool mkdirs(const char *path) {
    char copy[PATH_MAX];
    if (snprintf(copy, sizeof copy, "%s", path) >= sizeof copy) return false;
    for (char *p = copy + 1; ; ++p) {
        if (*p && *p != '/') continue;
        char end = *p;
        *p = 0;
        if (mkdir(copy, 0755) && errno != EEXIST) return false;
        struct stat st;
        if (stat(copy, &st) || !S_ISDIR(st.st_mode)) return false;
        *p = end;
        if (!end) break;
    }
    return true;
}

bool rt_data_root_early(char *out, size_t size) {
    const char *root = getenv("LARECOMP_DATA_ROOT");
    if (!root || !*root) root = getenv("LARECOMP_DATA_HOME");
    return root && *root ? absolute_path(root, out, size) : executable_dir(out, size);
}

void rt_paths_init(void) {
    char config[PATH_MAX], exe[PATH_MAX];
    const char *override = getenv("SF2_CONFIG");
    // Resolve a relative override before entering the writable data directory.
    bool ok = override && *override ? absolute_path(override, config, sizeof config) :
        executable_dir(exe, sizeof exe) &&
        snprintf(config, sizeof config, "%s/sf2recomp.ini", exe) < sizeof config;
    if (!ok || setenv("SF2_CONFIG", config, 1) ||
        !rt_data_root_early(g_root, sizeof g_root) || !mkdirs(g_root) || chdir(g_root)) {
        fprintf(stderr, "SF2 Recomp cannot initialize its config/data directory: %s\n", strerror(errno));
        exit(1);
    }
    if (!getcwd(g_root, sizeof g_root)) {
        perror("SF2 Recomp data directory");
        exit(1);
    }
    fprintf(stderr, "[paths] data: %s\n", g_root);
}

const char *rt_data_root(void) { return g_root; }
