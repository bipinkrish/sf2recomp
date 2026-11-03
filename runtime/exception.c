#include "constants/core.h"
// Small guest exception bridge for translated call sites with LSDA catch metadata.
#include <setjmp.h>
#include <string.h>

#include "hle.h"

typedef struct GuestEHFrame {
    jmp_buf env;
    struct GuestEHFrame *previous;
    Cpu saved;
    uint64_t site;
    volatile uint64_t landing_pad;
    volatile uint64_t selector;
    volatile uint64_t exception_object;
} GuestEHFrame;

static __thread GuestEHFrame *guest_eh_top;

void rt_guest_eh_reset(void) {
    guest_eh_top = NULL;
}

bool rt_guest_try_call(Cpu *c, uint64_t site, uint64_t target, GuestFn function) {
    GuestEHFrame frame;
    memset(&frame, 0, sizeof frame);
    frame.previous = guest_eh_top;
    frame.site = site;

    // A BL/BLR writes its return address before entering the callee. Keep the translated
    // register state at that point so a landing pad sees the caller's live values and SP.
    c->x[SF2C_EXCEPTION_N_30_624B] = site + SF2C_EXCEPTION_N_4_4B22;
    frame.saved = *c;
    guest_eh_top = &frame;

    if (setjmp(frame.env) == 0) {
        c->pc = target;
        if (function) function(c);
        else rt_dispatch(c);
        guest_eh_top = frame.previous;
        return false;
    }

    *c = frame.saved;
    c->x[0] = frame.exception_object;
    c->x[1] = frame.selector;
    c->pc = frame.landing_pad;
    return true;
}

bool rt_guest_raise(uint64_t exception_object, uint64_t type_info) {
    for (GuestEHFrame *frame = guest_eh_top; frame; frame = frame->previous) {
        uint64_t pad = 0, selector = 0;
        if (!main_eh_lookup(frame->site, type_info, &pad, &selector) &&
            !sdk_eh_lookup(frame->site, type_info, &pad, &selector))
            continue;

        frame->landing_pad = pad;
        frame->selector = selector;
        frame->exception_object = exception_object;
        guest_eh_top = frame->previous;  // discard deeper translated frames before longjmp
        LOG(SF2C_EXCEPTION_S_EH_CAUGHT_TYPE_LLX_AT_CALL_L_C10C,
            (unsigned long long)type_info, (unsigned long long)frame->site,
            (unsigned long long)pad, (unsigned long long)selector);
        longjmp(frame->env, 1);
    }
    return false;
}

// The generated IL2CPP wrapper catch expects __cxa_begin_catch to return the object passed
// to __cxa_throw. The exception bridge supplies that object in x0; the SDK unwinder cannot
// own its bookkeeping because it did not initiate this translated unwind.
HLE_FUNC(SF2C_EXCEPTION_S_CXA_BEGIN_CATCH_E934, hle_cxa_begin_catch) {
    (void)c;
}

HLE_FUNC(SF2C_EXCEPTION_S_CXA_END_CATCH_CE3B, hle_cxa_end_catch) {
    (void)c;
}
