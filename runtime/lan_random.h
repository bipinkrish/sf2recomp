#include "constants/core.h"
// Shared integer mixer; menu choices have their own advancing host-only stream.
#pragma once
#include <stdint.h>

// SplitMix64 finalizer: avalanche all input bits before using a scoped seed.
static inline uint64_t lan_random_mix(uint64_t value) {
    value=(value^(value>>SF2C_LAN_RANDOM_N_30_624B))*SF2C_LAN_RANDOM_N_UINT64_C_0XBF58476D1CE4E5B9_B50B;
    value=(value^(value>>SF2C_LAN_RANDOM_N_27_6706))*SF2C_LAN_RANDOM_N_UINT64_C_0X94D049BB133111EB_95E5;
    return value^(value>>SF2C_LAN_RANDOM_N_31_EB1E);
}
static inline uint64_t lan_selection_next(uint64_t *state) {
    return lan_random_mix(*state+=SF2C_LAN_RANDOM_N_UINT64_C_0X9E3779B97F4A7C15_B09C);
}
static inline uint32_t lan_selection_bounded(uint64_t *state,uint32_t count) {
    if(!count)return UINT32_MAX;
    // Reject the short remainder so every eligible entry has equal weight.
    uint64_t threshold=(-(uint64_t)count)%count,value;
    do { value=lan_selection_next(state); } while(value<threshold);
    return (uint32_t)(value%count);
}
