// Supported SF2 source versions, selected by the original NSO build ID.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SF2_BUILD_ID_LEN 16  // the main program's build ID bytes (NSO0 header: a 32-byte field, zero-padded)

typedef struct GameVersion {
    const char *key;       // "update_v131072": the tables' suffix
    const char *label;     // "update v131072": every release with this main program
    const char *build_id;  // the main program's build ID, 32 upper-case hex digits
} GameVersion;

// The version whose main build ID is the first SF2_BUILD_ID_LEN bytes of id, or NULL.
const GameVersion *game_version_by_build_id(const uint8_t *id);
// The version of a main program file (exefs/main), or NULL (missing, unreadable or unknown).
const GameVersion *game_version_of_main(const char *main_path);
// The version of the game files in game_dir (its exefs/main), or NULL.
const GameVersion *game_version_of(const char *game_dir);
// Validate the supported source build and log its version. false: why explains why.
bool game_version_select(const char *game_dir, char *why, size_t why_size);
// The selected version (NULL before game_version_select).
const GameVersion *game_version(void);

#ifdef __cplusplus
}
#endif
