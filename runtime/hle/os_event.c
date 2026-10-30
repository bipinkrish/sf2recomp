// Host implementations for nn::os::SystemEvent functions to prevent fatal assertions
// on uninitialized or late-initialized events in background threads (e.g. FMOD).
#include <sched.h>
#include <stdint.h>
#include "../hle.h"

extern void f_70044c1db0(Cpu *c); // nn::os::detail::TryWaitEvent
extern void f_70044c1da0(Cpu *c); // nn::os::detail::TryWaitInterProcessEvent
extern void f_70044c1dd0(Cpu *c); // nn::os::detail::WaitEvent
extern void f_70044c1dc0(Cpu *c); // nn::os::detail::WaitInterProcessEvent

HLE_FUNC("_ZN2nn2os18TryWaitSystemEventEPNS0_15SystemEventTypeE", nn_os_try_wait_system_event) {
    uint64_t ev = c->x[0];
    if (!ev) { c->x[0] = 0; return; }
    uint8_t state = *(uint8_t *)(uintptr_t)(ev + 0x28);
    if (state == 1) {
        f_70044c1db0(c);
    } else if (state == 2) {
        f_70044c1da0(c);
    } else {
        // Event uninitialized: return false (not signaled) without crashing
        c->x[0] = 0;
    }
}

HLE_FUNC("_ZN2nn2os15WaitSystemEventEPNS0_15SystemEventTypeE", nn_os_wait_system_event) {
    uint64_t ev = c->x[0];
    if (!ev) return;
    uint8_t state = *(uint8_t *)(uintptr_t)(ev + 0x28);
    if (state == 1) {
        f_70044c1dd0(c);
    } else if (state == 2) {
        f_70044c1dc0(c);
    } else {
        sched_yield();
    }
}

HLE_FUNC("_ZN2nn2os20TimedWaitSystemEventEPNS0_15SystemEventTypeENS_8TimeSpanE", nn_os_timed_wait_system_event) {
    uint64_t ev = c->x[0];
    if (!ev) { c->x[0] = 0; return; }
    uint8_t state = *(uint8_t *)(uintptr_t)(ev + 0x28);
    if (state == 1) {
        f_70044c1db0(c);
    } else if (state == 2) {
        f_70044c1da0(c);
    } else {
        c->x[0] = 0;
    }
}

HLE_FUNC("_ZN2nn2os18DestroySystemEventEPNS0_15SystemEventTypeE", nn_os_destroy_system_event) {
    uint64_t ev = c->x[0];
    if (ev) {
        *(uint8_t *)(uintptr_t)(ev + 0x28) = 0;
    }
    c->x[0] = 0;
}
