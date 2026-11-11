// Native launcher for the converter's portable Linux and macOS releases.
#define _XOPEN_SOURCE 700
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <stdint.h>
#endif

static bool release_root(char *root) {
    char executable[PATH_MAX];
#ifdef __APPLE__
    uint32_t size = sizeof executable;
    if (_NSGetExecutablePath(executable, &size)) return false;
#else
    ssize_t size = readlink("/proc/self/exe", executable, sizeof executable - 1);
    if (size < 0 || size >= (ssize_t)sizeof executable - 1) return false;
    executable[size] = 0;
#endif
    if (!realpath(executable, root)) return false;
    char *slash = strrchr(root, '/');
    if (!slash) return false;
    *slash = 0;
#ifdef __APPLE__
    // The Finder copy lives at <release>/<name>.app/Contents/MacOS/.
    static const char bundle[] = ".app/Contents/MacOS";
    size_t length = strlen(root);
    if (length >= sizeof bundle - 1 && !strcmp(root + length - (sizeof bundle - 1), bundle)) {
        char *app = root + length - (sizeof bundle - 1);
        while (app > root && *app != '/') app--;
        *app = 0;
    }
#endif
    return true;
}

int main(int argc, char **argv) {
    char root[PATH_MAX], python[PATH_MAX], script[PATH_MAX];
    if (!release_root(root) ||
        snprintf(python, sizeof python, "%s/python/bin/python3", root) >= (int)sizeof python ||
        snprintf(script, sizeof script, "%s/tools/release_entry.py", root) >= (int)sizeof script) {
        fputs("Cannot locate the SF2Recomp release folder.\n", stderr);
        return 1;
    }
    char **command = calloc((size_t)argc + 6, sizeof *command);
    if (!command) return 1;
    command[0] = python;
    command[1] = "-I";
    command[2] = "-u";
    command[3] = "-X";
    command[4] = "utf8";
    command[5] = script;
    memcpy(command + 6, argv + 1, (size_t)argc * sizeof *argv);
    execv(python, command);
    perror("Cannot start the bundled Python");
    free(command);
    return 1;
}
