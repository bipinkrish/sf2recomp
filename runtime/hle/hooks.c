// Instruction hooks needed by the native runtime. Game initialization, scene
// loading, object construction and exception behavior remain in the original code.
#include "hle.h"

// The Switch standard-stream getters return null. Corlib FileStream expects a
// live position/length handle even when the host has no guest console stream.
static int console_handle(Cpu *c) {
    static uint64_t handle;
    if (!handle) handle = (uint64_t)(uintptr_t)hle_alloc(0x40);
    c->x[0] = handle;
    return 1;
}

int rt_hook(Cpu *c, uint64_t site) {
    switch (site) {
    case 0x7000844c30ull:
    case 0x7000844c40ull:
    case 0x7000844c50ull:
    case 0x7000386660ull:
    case 0x7000386650ull:
    case 0x7000386670ull:
        return console_handle(c);
    default:
        return 0;
    }
}
