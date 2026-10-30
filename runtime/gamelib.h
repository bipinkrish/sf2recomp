// Tables compiled locally from the user's original NSOs. The executable embeds
// these translated functions; source releases do not contain generated game code.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "cpu.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const uint64_t *main_addrs, *sdk_addrs;
    const GuestFn *main_fns, *sdk_fns;
    const uint64_t *main_count, *sdk_count;
} GameLibTables;

// Refuse boot when the loaded main/SDK differ from the translated source builds.
bool gamelib_load(const char *game_dir, char *why, size_t why_size);
const GameLibTables *gamelib_tables(void);
#ifdef __cplusplus
}
#endif
