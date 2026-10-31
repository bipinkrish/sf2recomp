// Root prefixes in normalized paths. Kept OS-independent for path regression tests.
#pragma once
#include <stddef.h>
#include <string.h>
static inline size_t plat_path_root(const char *path, int windows) {
    if (windows && path[0] && path[1] == ':' && path[2] == '/') return 3;
    if (windows && path[0] == '/' && path[1] == '/') {
        const char *server = strchr(path + 2, '/');
        if (!server || server == path + 2 || !server[1]) return 0;
        const char *share = strchr(server + 1, '/');
        return share ? (size_t)(share - path + 1) : strlen(path);
    }
    return path[0] == '/' ? 1 : 0;
}
