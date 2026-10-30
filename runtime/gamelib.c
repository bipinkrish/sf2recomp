// Original game functions and their source build IDs, generated locally.
#include "gamelib.h"
#include <stdio.h>
#include <string.h>

extern const uint64_t main_fn_addrs[], sdk_fn_addrs[], main_fn_count, sdk_fn_count;
extern const GuestFn main_fn_ptrs[], sdk_fn_ptrs[];
extern const uint8_t main_nso_build_id[32], sdk_nso_build_id[32];

static const GameLibTables tables = {
    main_fn_addrs, sdk_fn_addrs, main_fn_ptrs, sdk_fn_ptrs,
    &main_fn_count, &sdk_fn_count
};

bool gamelib_load(const char *game_dir, char *why, size_t why_size) {
    const char *names[] = {"main", "sdk"};
    const uint8_t *expected[] = {main_nso_build_id, sdk_nso_build_id};
    for (size_t i = 0; i < 2; ++i) {
        char path[4096];
        uint8_t header[0x60];
        if (snprintf(path, sizeof path, "%s/exefs/%s", game_dir, names[i]) >= sizeof path) {
            snprintf(why, why_size, "game directory path is too long");
            return false;
        }
        FILE *f = fopen(path, "rb");
        bool valid = f && fread(header, 1, sizeof header, f) == sizeof header && !memcmp(header, "NSO0", 4);
        if (f) fclose(f);
        if (!valid) {
            snprintf(why, why_size, "exefs/%s is missing or is not a valid NSO", names[i]);
            return false;
        }
        if (memcmp(header + 0x40, expected[i], 32)) {
            snprintf(why, why_size, "exefs/%s does not match this executable's translated build; "
                     "regenerate and rebuild from these game files", names[i]);
            return false;
        }
    }
    return true;
}

const GameLibTables *gamelib_tables(void) { return &tables; }
