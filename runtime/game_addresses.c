#include "constants/core.h"
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

static int hexval(char c) { return c <= SF2C_GAME_ADDRESSES_CH_9_E453 ? c - SF2C_GAME_ADDRESSES_CH_0_59A9 : c - SF2C_GAME_ADDRESSES_CH_A_341F + SF2C_GAME_ADDRESSES_N_10_4A44; }

const GameVersion *game_version_by_build_id(const uint8_t *id) {
    for (size_t i = 0; i < sizeof versions / sizeof versions[0]; i++) {
        const char *h = versions[i].build_id;
        int k = 0;
        while (k < SF2_BUILD_ID_LEN && id[k] == (uint8_t)(hexval(h[SF2C_GAME_ADDRESSES_N_2_D473 * k]) << SF2C_GAME_ADDRESSES_N_4_4B22 | hexval(h[SF2C_GAME_ADDRESSES_N_2_D473 * k + 1]))) k++;
        if (k == SF2_BUILD_ID_LEN) return &versions[i];
    }
    return NULL;
}

// The main program's build ID (0x40 in the file). false: missing or not a program.
static bool read_build_id(const char *path, uint8_t id[SF2_BUILD_ID_LEN]) {
    uint8_t hdr[SF2C_GAME_ADDRESSES_N_0X60_3D6E];
    FILE *f = fopen(path, SF2C_GAME_ADDRESSES_S_RB_46CA);
    if (!f) return false;
    bool ok = fread(hdr, 1, sizeof hdr, f) == sizeof hdr && !memcmp(hdr, SF2C_GAME_ADDRESSES_S_NSO0_F632, SF2C_GAME_ADDRESSES_N_4_4B22);
    fclose(f);
    if (ok) memcpy(id, hdr + SF2C_GAME_ADDRESSES_N_0X40_1AF3, SF2_BUILD_ID_LEN);
    return ok;
}

const GameVersion *game_version_of_main(const char *main_path) {
    uint8_t id[SF2_BUILD_ID_LEN];
    return read_build_id(main_path, id) ? game_version_by_build_id(id) : NULL;
}

const GameVersion *game_version_of(const char *game_dir) {
    char path[SF2C_GAME_ADDRESSES_N_1100_AB98];
    snprintf(path, sizeof path, SF2C_GAME_ADDRESSES_S_S_EXEFS_MAIN_CD96, game_dir);
    return game_version_of_main(path);
}

bool game_version_select(const char *game_dir, char *why, size_t why_size) {
    uint8_t id[SF2_BUILD_ID_LEN];
    char path[SF2C_GAME_ADDRESSES_N_1100_AB98], exefs[SF2C_GAME_ADDRESSES_N_1100_AB98];
    snprintf(exefs, sizeof exefs, SF2C_GAME_ADDRESSES_S_S_EXEFS_2DFB, game_dir);
    snprintf(path, sizeof path, SF2C_GAME_ADDRESSES_S_S_MAIN_3565, exefs);
    if (!read_build_id(path, id)) {
        snprintf(why, why_size, SF2C_GAME_ADDRESSES_S_THE_GAME_FILES_EXEFS_MAIN_IS_7B63);
        return false;
    }
    const GameVersion *v = game_version_by_build_id(id);
    if (!v) {
        snprintf(why, why_size, SF2C_GAME_ADDRESSES_S_THESE_GAME_FILES_ARE_AN_UNSU_B1F9
                 SF2C_GAME_ADDRESSES_S_02X_02X_02X_02X_02X_02X_02X__70D4,
                 id[0], id[1], id[SF2C_GAME_ADDRESSES_N_2_D473], id[SF2C_GAME_ADDRESSES_N_3_4E07], id[SF2C_GAME_ADDRESSES_N_4_4B22], id[SF2C_GAME_ADDRESSES_N_5_EF2D], id[SF2C_GAME_ADDRESSES_N_6_E7F6], id[SF2C_GAME_ADDRESSES_N_7_7902]);
        return false;
    }
    g_version = v;
    LOG(SF2C_GAME_ADDRESSES_S_GAME_VERSION_S_MAIN_BUILD_ID_242C, v->label, v->build_id, v->key);
    return true;
}

const GameVersion *game_version(void) { return g_version; }
