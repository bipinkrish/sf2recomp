// Original game control metadata, read from the user's extraction at startup.
#pragma once
#include <stdint.h>

#define SF2_APP_NAME "Shadow Fight 2 (sf2recomp)"
#define SF2_APP_ID "org.sf2recomp.ShadowFight2"

void game_info_load(const char *game_root);
const char *game_display_version(void);  // empty if original NACP unavailable
const char *game_window_title(void);
const char *game_icon_path(void);        // empty if original icon unavailable
uint32_t game_application_version(void); // Vulkan's packed numeric version, or 0
uint64_t game_add_on_content_base_id(void);
