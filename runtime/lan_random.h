// Shared integer mixer; menu choices have their own advancing host-only stream.
#pragma once
#include <stdint.h>

// SplitMix64 finalizer: avalanche all input bits before using a scoped seed.
static inline uint64_t lan_random_mix(uint64_t value) {
    value=(value^(value>>30))*UINT64_C(0xbf58476d1ce4e5b9);
    value=(value^(value>>27))*UINT64_C(0x94d049bb133111eb);
    return value^(value>>31);
}
static inline uint64_t lan_selection_next(uint64_t *state) {
    return lan_random_mix(*state+=UINT64_C(0x9e3779b97f4a7c15));
}
static inline uint32_t lan_selection_bounded(uint64_t *state,uint32_t count) {
    if(!count)return UINT32_MAX;
    // Reject the short remainder so every eligible entry has equal weight.
    uint64_t threshold=(-(uint64_t)count)%count,value;
    do { value=lan_selection_next(state); } while(value<threshold);
    return (uint32_t)(value%count);
}
