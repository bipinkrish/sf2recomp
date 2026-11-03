#include "constants/core.h"
// Original game functions and their source build IDs, generated locally.
#include "gamelib.h"
#include <stdio.h>
#include <string.h>

extern const uint64_t main_fn_addrs[], sdk_fn_addrs[], main_fn_count, sdk_fn_count;
extern const GuestFn main_fn_ptrs[], sdk_fn_ptrs[];
extern const uint8_t main_nso_build_id[SF2C_GAMELIB_N_32_E29C], sdk_nso_build_id[SF2C_GAMELIB_N_32_E29C];

static const GameLibTables tables = {
    main_fn_addrs, sdk_fn_addrs, main_fn_ptrs, sdk_fn_ptrs,
    &main_fn_count, &sdk_fn_count
};

bool gamelib_load(const char *game_dir, char *why, size_t why_size) {
    const char *names[] = {SF2C_GAMELIB_S_MAIN_E4A9, SF2C_GAMELIB_S_SDK_DE0B};
    const uint8_t *expected[] = {main_nso_build_id, sdk_nso_build_id};
    for (size_t i = 0; i < SF2C_GAMELIB_N_2_D473; ++i) {
        char path[SF2C_GAMELIB_N_4096_8B92];
        uint8_t header[SF2C_GAMELIB_N_0X60_3D6E];
        if (snprintf(path, sizeof path, SF2C_GAMELIB_S_S_EXEFS_S_3F4F, game_dir, names[i]) >= sizeof path) {
            snprintf(why, why_size, SF2C_GAMELIB_S_GAME_DIRECTORY_PATH_IS_TOO_L_427C);
            return false;
        }
        FILE *f = fopen(path, SF2C_GAMELIB_S_RB_46CA);
        bool valid = f && fread(header, 1, sizeof header, f) == sizeof header && !memcmp(header, SF2C_GAMELIB_S_NSO0_F632, SF2C_GAMELIB_N_4_4B22);
        if (f) fclose(f);
        if (!valid) {
            snprintf(why, why_size, SF2C_GAMELIB_S_EXEFS_S_IS_MISSING_OR_IS_NOT_1290, names[i]);
            return false;
        }
        if (memcmp(header + SF2C_GAMELIB_N_0X40_1AF3, expected[i], SF2C_GAMELIB_N_32_E29C)) {
            snprintf(why, why_size, SF2C_GAMELIB_S_EXEFS_S_DOES_NOT_MATCH_THIS__AA5D
                     SF2C_GAMELIB_S_REGENERATE_AND_REBUILD_FROM__59C1, names[i]);
            return false;
        }
    }
    return true;
}

const GameLibTables *gamelib_tables(void) { return &tables; }
