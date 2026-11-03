#include "constants/core.h"
// SF2 v1.1.0 replay diagnostics and shared controlled initialization for LAN.
#pragma once
#include "cpu.h"

enum { RT_HOOK_REPLAY = SF2C_REPLAY_N_2U_8C70 };
void replay_init(void);  // before guest threads start; bad options fail closed
bool replay_playback_enabled(void);
bool replay_enabled(void);
int replay_hook(Cpu *cpu, uint64_t site);

void replay_gui_start(void);
void replay_gui_stop(void);
