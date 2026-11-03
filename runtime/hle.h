#include "constants/core.h"
// Registration of host implementations for guest symbols.
#pragma once
#include <stdlib.h>
// Environment lookups that run every frame / per call: looked up once per call site (the variables don't change
// while the game runs). ENV("LARECOMP_X") -> const char * or NULL.
#define ENV(name) ({ static const char *env_v_; static int env_i_; if (!env_i_) { env_v_ = getenv(name); env_i_ = 1; } env_v_; })

#include "rt.h"

typedef struct { const char *name; HostFn fn; } HleImplEntry;

// HLE_FUNC("mangled_symbol", c_function) binds a guest import to a host function.
#define HLE_CAT2(a, b) a##b
#define HLE_CAT(a, b) HLE_CAT2(a, b)
#ifdef __APPLE__
#define HLE_SECTION SF2C_HLE_HLE_SECTION
#elif defined(__linux__)
#define HLE_SECTION SF2C_HLE_S_HLE_IMPLS_3066  // ELF: the linker defines __start_ / __stop_ for C-identifier section names
#else
#define HLE_SECTION SF2C_HLE_S_HLE_M_FE1A  // between the .hle$a / .hle$z markers in runtime/dispatch.c
#endif
#define HLE_FUNC(sym, fn)                                                                   \
    static void fn(Cpu *c);                                                                 \
    __attribute__((used, section(HLE_SECTION))) static const HleImplEntry                   \
        HLE_CAT(hle_entry_, __LINE__) = {sym, fn};                                          \
    static void fn(Cpu *c)

// Per-boot host state. RT_ON_RESET(fn) { ... } runs when the game is torn down for an in-place reboot (switching
// seeds / vanilla, back to the start screen: runtime/main.c), after every guest thread has stopped and before
// guest memory is wiped: drop guest pointers, kernel handles, seed data and lazily-initialised state, so the next
// boot starts as from a fresh process. (Settings, the window, the GPU device and ENV() lookups stay.)
typedef void (*ResetFn)(void);
#ifdef __APPLE__
#define RESET_SECTION SF2C_HLE_RESET_SECTION_8FA56F34
#elif defined(__linux__)
#define RESET_SECTION SF2C_HLE_RESET_SECTION_C31C6D41
#else
#define RESET_SECTION SF2C_HLE_S_RST_M_30FC  // between the .rst$a / .rst$z markers in runtime/main.c
#endif
#define RT_ON_RESET(fn)                                                                     \
    static void fn(void);                                                                   \
    __attribute__((used, section(RESET_SECTION))) static const ResetFn                      \
        HLE_CAT(reset_entry_, __LINE__) = fn;                                               \
    static void fn(void)

#define RES_OK SF2C_HLE_RES_OK
#define MAKE_RESULT(module, desc) ((uint32_t)(module) | ((uint32_t)(desc) << SF2C_HLE_N_9_1958))
