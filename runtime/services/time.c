#include "../constants/services.h"
// time:u / time:a / time:s
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "../hle.h"
#include "../ipc.h"

static const uint8_t clock_uuid[SF2C_TIME_N_16_B17E] = {SF2C_TIME_N_0X4C_C1BA, SF2C_TIME_N_0X41_BE08, SF2C_TIME_N_0X52_EE58, SF2C_TIME_N_0X65_0237, SF2C_TIME_N_0X63_A6C6, SF2C_TIME_N_0X6F_F59F, SF2C_TIME_N_0X6D_CAE4, SF2C_TIME_N_0X70_B0F6, 1, SF2C_TIME_N_2_D473, SF2C_TIME_N_3_4E07, SF2C_TIME_N_4_4B22, SF2C_TIME_N_5_EF2D, SF2C_TIME_N_6_E7F6, SF2C_TIME_N_7_7902, SF2C_TIME_N_8_2C62};

static int64_t steady_seconds(void) { return (int64_t)(rt_ticks() / SF2C_TIME_N_19200000ULL_50AC); }
static int64_t unix_now(void) { return (int64_t)time(NULL); }

static void write_system_context(uint8_t *p) {  // SystemClockContext (0x20)
    int64_t steady = steady_seconds();
    *(int64_t *)p = unix_now() - steady;
    *(int64_t *)(p + SF2C_TIME_N_8_2C62) = steady;
    memcpy(p + SF2C_TIME_N_0X10_BF9C, clock_uuid, SF2C_TIME_N_16_B17E);
}

static void time_shmem_map(KSharedMem *s) {
    uint8_t *m = (uint8_t *)(uintptr_t)s->guest_addr;
    LOG(SF2C_TIME_S_TIME_SHARED_MEMORY_MAPPED_AT_2B99, (unsigned long long)s->guest_addr);
    // 0x00: LockFreeAtomicType<SteadyClockContext{u64 internal_offset, uuid}>
    *(uint32_t *)m = 0;
    *(uint64_t *)(m + SF2C_TIME_N_0X08_1EC8) = 0;
    memcpy(m + SF2C_TIME_N_0X10_BF9C, clock_uuid, SF2C_TIME_N_16_B17E);
    // 0x38 local system clock, 0x80 network system clock
    *(uint32_t *)(m + SF2C_TIME_N_0X38_AE4F) = 0;
    write_system_context(m + SF2C_TIME_N_0X40_1AF3);
    *(uint32_t *)(m + SF2C_TIME_N_0X80_D41C) = 0;
    write_system_context(m + SF2C_TIME_N_0X88_AC70);
    // 0xC8 automatic correction enabled
    *(uint32_t *)(m + SF2C_TIME_N_0XC8_D7A0) = 0;
    m[SF2C_TIME_N_0XCC_83B6] = 0;
}

static void system_clock(Service *s, IpcReq *req, IpcResp *resp) {
    (void)s;
    switch (req->cmd) {
    case 0: OUT(int64_t, 0) = unix_now(); resp_size(resp, SF2C_TIME_N_8_2C62); break;  // GetCurrentTime
    case SF2C_TIME_N_2_D473: write_system_context(resp->data); resp_size(resp, SF2C_TIME_N_0X20_A766); break;  // GetSystemClockContext
    default: LOG(SF2C_TIME_S_TIME_ISYSTEMCLOCK_CMD_U_OK_N_EBF0, req->cmd);
    }
}

static void steady_clock(Service *s, IpcReq *req, IpcResp *resp) {
    (void)s;
    switch (req->cmd) {
    case 0:  // GetCurrentTimePoint
        OUT(int64_t, 0) = steady_seconds();
        memcpy(resp->data + SF2C_TIME_N_8_2C62, clock_uuid, SF2C_TIME_N_16_B17E);
        resp_size(resp, SF2C_TIME_N_0X18_D47C);
        break;
    default: OUT(int64_t, 0) = 0; resp_size(resp, SF2C_TIME_N_8_2C62); LOG(SF2C_TIME_S_TIME_ISTEADYCLOCK_CMD_U_OK_N_60DD, req->cmd);
    }
}

static void to_calendar(int64_t t, IpcResp *resp, int utc) {
    time_t tt = (time_t)t;
    struct tm tm;
    if (utc) gmtime_r(&tt, &tm); else localtime_r(&tt, &tm);
    uint8_t *o = resp->data;
    *(uint16_t *)o = (uint16_t)(tm.tm_year + SF2C_TIME_N_1900_E41D);
    o[SF2C_TIME_N_2_D473] = (uint8_t)(tm.tm_mon + 1); o[SF2C_TIME_N_3_4E07] = (uint8_t)tm.tm_mday;
    o[SF2C_TIME_N_4_4B22] = (uint8_t)tm.tm_hour; o[SF2C_TIME_N_5_EF2D] = (uint8_t)tm.tm_min; o[SF2C_TIME_N_6_E7F6] = (uint8_t)tm.tm_sec;
    *(uint32_t *)(o + SF2C_TIME_N_8_2C62) = (uint32_t)tm.tm_wday;
    *(uint32_t *)(o + SF2C_TIME_N_12_6B51) = (uint32_t)tm.tm_yday;
#ifdef _WIN32  // no tm_zone / tm_gmtoff: offset = local broken-down time read back as UTC, minus now
    struct tm g;
    gmtime_r(&tt, &g);
    long gmtoff = (long)difftime(_mkgmtime(&tm), _mkgmtime(&g));
    memcpy(o + SF2C_TIME_N_16_B17E, utc ? SF2C_TIME_S_UTC_6810 : SF2C_TIME_S_LCL_136F, SF2C_TIME_N_4_4B22);
#else
    long gmtoff = tm.tm_gmtoff;
    memcpy(o + SF2C_TIME_N_16_B17E, utc ? SF2C_TIME_S_UTC_6810 : tm.tm_zone, SF2C_TIME_N_4_4B22);
#endif
    *(uint32_t *)(o + SF2C_TIME_N_24_C235) = (uint32_t)tm.tm_isdst;
    *(int32_t *)(o + SF2C_TIME_N_28_59E1) = utc ? 0 : (int32_t)gmtoff;
    resp_size(resp, SF2C_TIME_N_0X20_A766);
}

static void timezone_service(Service *s, IpcReq *req, IpcResp *resp) {
    (void)s;
    switch (req->cmd) {
    case 0: memcpy(resp->data, SF2C_TIME_S_UTC_6810, SF2C_TIME_N_4_4B22); resp_size(resp, SF2C_TIME_N_0X24_EB21); break;  // GetDeviceLocationName
    case SF2C_TIME_N_100_AD57: to_calendar(IN(int64_t, 0), resp, 1); break;  // ToCalendarTime(rule buffer)
    case SF2C_TIME_N_101_16DC: to_calendar(IN(int64_t, 0), resp, 0); break;  // ToCalendarTimeWithMyRule
    case SF2C_TIME_N_201_4397: {  // ToPosixTimeWithMyRule -> count + buffer of posix times
        const uint8_t *in = req->data;
        struct tm tm = {0};
        tm.tm_year = *(uint16_t *)in - SF2C_TIME_N_1900_E41D; tm.tm_mon = in[SF2C_TIME_N_2_D473] - 1; tm.tm_mday = in[SF2C_TIME_N_3_4E07];
        tm.tm_hour = in[SF2C_TIME_N_4_4B22]; tm.tm_min = in[SF2C_TIME_N_5_EF2D]; tm.tm_sec = in[SF2C_TIME_N_6_E7F6]; tm.tm_isdst = -1;
        int64_t t = mktime(&tm);
        if (req->nc) *(int64_t *)(uintptr_t)req->c[0].addr = t;
        else if (req->nb) *(int64_t *)(uintptr_t)req->b[0].addr = t;
        OUT(uint32_t, 0) = 1;
        resp_size(resp, SF2C_TIME_N_4_4B22);
        break;
    }
    default: LOG(SF2C_TIME_S_TIME_ITIMEZONESERVICE_CMD_U__988A, req->cmd); resp_size(resp, SF2C_TIME_N_0X10_BF9C);
    }
}

static uint32_t g_shmem_handle;
RT_ON_RESET(time_reset) { g_shmem_handle = 0; }

static void static_service(Service *s, IpcReq *req, IpcResp *resp) {
    (void)s;
    switch (req->cmd) {
    case 0: case 1: case SF2C_TIME_N_4_4B22: case SF2C_TIME_N_5_EF2D:
        ipc_return_object(resp, service_new(SF2C_TIME_S_ISYSTEMCLOCK_1A0A, system_clock, NULL));
        break;
    case SF2C_TIME_N_2_D473: ipc_return_object(resp, service_new(SF2C_TIME_S_ISTEADYCLOCK_7270, steady_clock, NULL)); break;
    case SF2C_TIME_N_3_4E07: ipc_return_object(resp, service_new(SF2C_TIME_S_ITIMEZONESERVICE_7A60, timezone_service, NULL)); break;
    case SF2C_TIME_N_20_F5CA: {  // GetSharedMemoryNativeHandle
        if (!g_shmem_handle) kernel_create_shared_memory(SF2C_TIME_N_0X1000_6ABE, time_shmem_map, &g_shmem_handle);
        resp->copy_handles[resp->ncopy++] = g_shmem_handle;
        break;
    }
    case SF2C_TIME_N_100_AD57: case SF2C_TIME_N_101_16DC: OUT(uint8_t, 0) = 0; resp_size(resp, 1); break;
    default: LOG(SF2C_TIME_S_TIME_ISTATICSERVICE_CMD_U_OK_529E, req->cmd); resp_size(resp, SF2C_TIME_N_0X10_BF9C);
    }
}

SERVICE(SF2C_TIME_S_TIME_U_69B5, time_u) { static_service(s, req, resp); }
SERVICE(SF2C_TIME_S_TIME_A_D7DA, time_a) { static_service(s, req, resp); }
SERVICE(SF2C_TIME_S_TIME_S_46BB, time_s) { static_service(s, req, resp); }

// nn::time::StandardUserSystemClock::GetCurrentTime(PosixTime*): answered directly from the host clock. The SDK's
// shared-memory path aborts (svcBreak via nn::diag::Abort, result 0xcc74) if the local clock's source id ever
// differs from the steady clock's; that happened once in the room sweep on an autosave (entering Eagle's Tower) and
// could not be reproduced, so the date stamp no longer depends on it.
HLE_FUNC(SF2C_TIME_S_ZN2NN4TIME23STANDARDUSERSYST_91D1, time_std_user_now) {
    static int n;
    if (n++ < SF2C_TIME_N_3_4E07) LOG(SF2C_TIME_S_TIME_STANDARDUSERSYSTEMCLOCK_B73F);
    if (c->x[0]) *(int64_t *)(uintptr_t)c->x[0] = unix_now();
    c->x[0] = 0;  // ResultSuccess
}
