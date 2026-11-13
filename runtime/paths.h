#include "constants/core.h"
// The converted game is portable: sf2recomp.ini, saves and caches live beside
// its executable. SF2RECOMP_DATA_ROOT/HOME override writable state for testing;
// SF2_CONFIG overrides the INI (relative to the launch directory).
#pragma once
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern SF2C_PATHS_S_C_C2E8 {
#endif
void rt_paths_init(void);
bool rt_data_root_early(char *out, size_t size);
const char *rt_data_root(void);
#ifdef __cplusplus
}
#endif
