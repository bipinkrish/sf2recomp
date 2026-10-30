// The game-address tables of every supported SF2 version and their selection by build ID (game_addresses.h).
#include "game_addresses.h"

#include <stdio.h>
#include <string.h>

#include "rt.h"

static const GameVersion versions[] = {
#define GA_VERSION(key, label, build_id) {#key, label, build_id},
#define GA_RELEASE(key, label, npdm_sha256)
#include "game_addresses/versions.h"
#undef GA_VERSION
#undef GA_RELEASE
};

static const GameVersion *g_version;

static int hexval(char c) { return c <= '9' ? c - '0' : c - 'A' + 10; }

const GameVersion *game_version_by_build_id(const uint8_t *id) {
    for (size_t i = 0; i < sizeof versions / sizeof versions[0]; i++) {
        const char *h = versions[i].build_id;
        int k = 0;
        while (k < SF2_BUILD_ID_LEN && id[k] == (uint8_t)(hexval(h[2 * k]) << 4 | hexval(h[2 * k + 1]))) k++;
        if (k == SF2_BUILD_ID_LEN) return &versions[i];
    }
    return NULL;
}

// The main program's build ID (0x40 in the file). false: missing or not a program.
static bool read_build_id(const char *path, uint8_t id[SF2_BUILD_ID_LEN]) {
    uint8_t hdr[0x60];
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    bool ok = fread(hdr, 1, sizeof hdr, f) == sizeof hdr && !memcmp(hdr, "NSO0", 4);
    fclose(f);
    if (ok) memcpy(id, hdr + 0x40, SF2_BUILD_ID_LEN);
    return ok;
}

const GameVersion *game_version_of_main(const char *main_path) {
    uint8_t id[SF2_BUILD_ID_LEN];
    return read_build_id(main_path, id) ? game_version_by_build_id(id) : NULL;
}

const GameVersion *game_version_of(const char *game_dir) {
    char path[1100];
    snprintf(path, sizeof path, "%s/exefs/main", game_dir);
    return game_version_of_main(path);
}

bool game_version_select(const char *game_dir, char *why, size_t why_size) {
    uint8_t id[SF2_BUILD_ID_LEN];
    char path[1100], exefs[1100];
    snprintf(exefs, sizeof exefs, "%s/exefs", game_dir);
    snprintf(path, sizeof path, "%s/main", exefs);
    if (!read_build_id(path, id)) {
        snprintf(why, why_size, "the game files' exefs/main is missing or unreadable");
        return false;
    }
    const GameVersion *v = game_version_by_build_id(id);
    if (!v) {
        snprintf(why, why_size, "these game files are an unsupported version of the game (main build ID "
                 "%02X%02X%02X%02X%02X%02X%02X%02X...): the recompiler needs a SF2 update v131072 (v1.1.0)",
                 id[0], id[1], id[2], id[3], id[4], id[5], id[6], id[7]);
        return false;
    }
    g_version = v;
    LOG("[game] version %s (main build ID %.16s, version %s)\n", v->label, v->build_id, v->key);
    return true;
}

const GameVersion *game_version(void) { return g_version; }
