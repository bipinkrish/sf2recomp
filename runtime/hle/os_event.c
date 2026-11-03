#include "../constants/hle.h"
// Host implementations for nn::os::SystemEvent functions to prevent fatal assertions
// on uninitialized or late-initialized events in background threads (e.g. FMOD).
#include <sched.h>
#include <stdint.h>
#include "../hle.h"

extern void f_70044c1db0(Cpu *c); // nn::os::detail::TryWaitEvent
extern void f_70044c1da0(Cpu *c); // nn::os::detail::TryWaitInterProcessEvent
extern void f_70044c1dd0(Cpu *c); // nn::os::detail::WaitEvent
extern void f_70044c1dc0(Cpu *c); // nn::os::detail::WaitInterProcessEvent

HLE_FUNC(SF2C_OS_EVENT_S_ZN2NN2OS18TRYWAITSYSTEMEVENT_9084, nn_os_try_wait_system_event) {
    uint64_t ev = c->x[0];
    if (!ev) { c->x[0] = 0; return; }
    uint8_t state = *(uint8_t *)(uintptr_t)(ev + SF2C_OS_EVENT_N_0X28_EAB9);
    if (state == 1) {
        f_70044c1db0(c);
    } else if (state == SF2C_OS_EVENT_N_2_D473) {
        f_70044c1da0(c);
    } else {
        // Event uninitialized: return false (not signaled) without crashing
        c->x[0] = 0;
    }
}

HLE_FUNC(SF2C_OS_EVENT_S_ZN2NN2OS15WAITSYSTEMEVENTEPN_7916, nn_os_wait_system_event) {
    uint64_t ev = c->x[0];
    if (!ev) return;
    uint8_t state = *(uint8_t *)(uintptr_t)(ev + SF2C_OS_EVENT_N_0X28_EAB9);
    if (state == 1) {
        f_70044c1dd0(c);
    } else if (state == SF2C_OS_EVENT_N_2_D473) {
        f_70044c1dc0(c);
    } else {
        sched_yield();
    }
}

HLE_FUNC(SF2C_OS_EVENT_S_ZN2NN2OS20TIMEDWAITSYSTEMEVE_0E9D, nn_os_timed_wait_system_event) {
    uint64_t ev = c->x[0];
    if (!ev) { c->x[0] = 0; return; }
    uint8_t state = *(uint8_t *)(uintptr_t)(ev + SF2C_OS_EVENT_N_0X28_EAB9);
    if (state == 1) {
        f_70044c1db0(c);
    } else if (state == SF2C_OS_EVENT_N_2_D473) {
        f_70044c1da0(c);
    } else {
        c->x[0] = 0;
    }
}

HLE_FUNC(SF2C_OS_EVENT_S_ZN2NN2OS18DESTROYSYSTEMEVENT_8217, nn_os_destroy_system_event) {
    uint64_t ev = c->x[0];
    if (ev) {
        *(uint8_t *)(uintptr_t)(ev + SF2C_OS_EVENT_N_0X28_EAB9) = 0;
    }
    c->x[0] = 0;
}
