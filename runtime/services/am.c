#include "../constants/services.h"
// Applet manager (appletOE), apm, and lm.
#include <stdlib.h>
#include <string.h>

#include "../hle.h"
#include "../ipc.h"
#include "../settings.h"

#define AM_NO_MESSAGE MAKE_RESULT(SF2C_AM_N_128_2747, SF2C_AM_N_3_4E07)
#define AM_NO_DATA MAKE_RESULT(SF2C_AM_N_128_2747, SF2C_AM_N_2_D473)

static uint32_t msg_event_r, msg_event_w;
static KEvent *msg_event;
static uint32_t msg_queue[SF2C_AM_N_16_B17E];
static int msg_count;

static bool preselected_popped;
RT_ON_RESET(am_reset) {  // (reboot: the message queue starts over with FocusStateChanged)
    msg_event = NULL;
    msg_event_r = msg_event_w = 0;
    msg_count = 0;
    preselected_popped = false;
}

static void am_init_once(void) {
    if (msg_event) return;
    msg_event = kernel_create_event_pair(&msg_event_w, &msg_event_r);
    msg_queue[msg_count++] = SF2C_AM_N_15_E629;  // FocusStateChanged
    pthread_mutex_lock(&g_klock);
    kernel_signal(msg_event);
    pthread_mutex_unlock(&g_klock);
}

static uint32_t new_event(void) {
    uint32_t w, r;
    kernel_create_event_pair(&w, &r);
    return r;
}

#define UNHANDLED(iface) LOG(SF2C_AM_S_AM_S_CMD_U_UNHANDLED_OK_N_7C24, iface, req->cmd)

static void common_state_getter(Service *s, IpcReq *req, IpcResp *resp) {
    (void)s;
    switch (req->cmd) {
    case 0: resp->copy_handles[resp->ncopy++] = msg_event_r; break;  // GetEventHandle
    case 1:  // ReceiveMessage
        pthread_mutex_lock(&g_klock);
        if (msg_count) {
            OUT(uint32_t, 0) = msg_queue[0];
            memmove(msg_queue, msg_queue + 1, --msg_count * sizeof(uint32_t));
            if (!msg_count) msg_event->signaled = false;
            resp_size(resp, SF2C_AM_N_4_4B22);
        } else {
            resp->result = AM_NO_MESSAGE;
        }
        pthread_mutex_unlock(&g_klock);
        break;
    case SF2C_AM_N_5_EF2D: OUT(uint8_t, 0) = 1; resp_size(resp, 1); break;   // GetOperationMode: docked
    case SF2C_AM_N_6_E7F6: OUT(uint32_t, 0) = 1; resp_size(resp, SF2C_AM_N_4_4B22); break;  // GetPerformanceMode: boost
    case SF2C_AM_N_8_2C62: OUT(uint8_t, 0) = 0; resp_size(resp, 1); break;   // GetBootMode
    case SF2C_AM_N_9_1958: OUT(uint8_t, 0) = 1; resp_size(resp, 1); break;   // GetCurrentFocusState: in focus
    case SF2C_AM_N_13_3FDB: case SF2C_AM_N_61_D029: resp->copy_handles[resp->ncopy++] = new_event(); break;
    case SF2C_AM_N_60_39FA: {  // GetDefaultDisplayResolution
        int w = getenv(SF2C_AM_S_LARECOMP_DISPLAY_W_2E79) ? atoi(getenv(SF2C_AM_S_LARECOMP_DISPLAY_W_2E79)) : SF2C_AM_N_1920_6B5F;
        LOG(SF2C_AM_S_AM_GETDEFAULTDISPLAYRESOLUTI_50D3, w);
        OUT(int32_t, 0) = w; OUT(int32_t, SF2C_AM_N_4_4B22) = SF2C_AM_N_1080_32EB; resp_size(resp, SF2C_AM_N_8_2C62); break;
    }
    default: UNHANDLED(SF2C_AM_S_ICOMMONSTATEGETTER_4E19);
    }
}

static void self_controller(Service *s, IpcReq *req, IpcResp *resp) {
    (void)s;
    switch (req->cmd) {
    case 0: LOG(SF2C_AM_S_AM_APPLICATION_REQUESTED_EXI_2DF9); exit(0);
    case SF2C_AM_N_9_1958: resp->copy_handles[resp->ncopy++] = new_event(); break;
    case SF2C_AM_N_40_D59E: OUT(uint64_t, 0) = 1; resp_size(resp, SF2C_AM_N_8_2C62); break;  // CreateManagedDisplayLayer
    case SF2C_AM_N_69_C75C: OUT(uint8_t, 0) = 0; resp_size(resp, 1); break;
    default: UNHANDLED(SF2C_AM_S_ISELFCONTROLLER_8E8A);
    }
}

static void window_controller(Service *s, IpcReq *req, IpcResp *resp) {
    (void)s;
    switch (req->cmd) {
    case 1: OUT(uint64_t, 0) = SF2C_AM_N_0X1234_5A07; resp_size(resp, SF2C_AM_N_8_2C62); break;  // GetAppletResourceUserId
    default: UNHANDLED(SF2C_AM_S_IWINDOWCONTROLLER_8831);
    }
}

static void generic_ok(Service *s, IpcReq *req, IpcResp *resp) {
    LOG(SF2C_AM_S_AM_S_CMD_U_OK_N_AC47, s->name, req->cmd);
    resp_size(resp, SF2C_AM_N_0X10_BF9C);
}

// ------------------------------------------------------------------ IStorage / IStorageAccessor
typedef struct { uint8_t *data; uint64_t size; } AmStorage;

static void storage_accessor(Service *s, IpcReq *req, IpcResp *resp) {
    AmStorage *st = s->state;
    switch (req->cmd) {
    case 0: OUT(int64_t, 0) = (int64_t)st->size; resp_size(resp, SF2C_AM_N_8_2C62); break;  // GetSize
    case SF2C_AM_N_10_4A44: {  // Write(offset, A)
        int64_t off = IN(int64_t, 0);
        IpcBuf *a = in_buf(req, 0);
        uint64_t n = a->size;
        if ((uint64_t)off + n > st->size) n = st->size - (uint64_t)off;
        memcpy(st->data + off, (void *)(uintptr_t)a->addr, n);
        break;
    }
    case SF2C_AM_N_11_4FC8: {  // Read(offset, B)
        int64_t off = IN(int64_t, 0);
        IpcBuf *b = out_buf(req, 0);
        uint64_t n = b->size;
        if ((uint64_t)off + n > st->size) n = st->size - (uint64_t)off;
        memcpy((void *)(uintptr_t)b->addr, st->data + off, n);
        break;
    }
    default: LOG(SF2C_AM_S_AM_ISTORAGEACCESSOR_CMD_U_OK_CAE4, req->cmd);
    }
}

static void am_storage(Service *s, IpcReq *req, IpcResp *resp) {
    if (req->cmd == 0) ipc_return_object(resp, service_new(SF2C_AM_S_ISTORAGEACCESSOR_EE2F, storage_accessor, s->state));
    else LOG(SF2C_AM_S_AM_ISTORAGE_CMD_U_OK_N_2BD2, req->cmd);
}

static Service *new_am_storage(const void *data, uint64_t size) {
    AmStorage *st = calloc(1, sizeof *st);
    st->data = calloc(1, size);
    st->size = size;
    if (data) memcpy(st->data, data, size);
    return service_new(SF2C_AM_S_ISTORAGE_A904, am_storage, st);
}

extern const uint8_t g_user_id[SF2C_AM_N_16_B17E];

static void application_functions(Service *s, IpcReq *req, IpcResp *resp) {
    (void)s;
    switch (req->cmd) {
    case 1:  // PopLaunchParameter(kind)
        if (IN(uint32_t, 0) == SF2C_AM_N_2_D473 && !preselected_popped) {  // PreselectedUser
            uint8_t p[SF2C_AM_N_0X88_AC70] = {0};
            *(uint32_t *)p = SF2C_AM_N_0XC79497CA_D92A;
            *(uint32_t *)(p + SF2C_AM_N_4_4B22) = 1;
            memcpy(p + SF2C_AM_N_8_2C62, g_user_id, SF2C_AM_N_16_B17E);
            preselected_popped = true;
            ipc_return_object(resp, new_am_storage(p, sizeof p));
        } else {
            resp->result = AM_NO_DATA;
        }
        break;
    case SF2C_AM_N_20_F5CA: OUT(uint64_t, 0) = 0; resp_size(resp, SF2C_AM_N_8_2C62); break;  // EnsureSaveData
    case SF2C_AM_N_21_6F4B: {  // GetDesiredLanguage: the game's text language (settings: runtime/settings.c, "language")
        char lang[SF2C_AM_N_8_2C62] = {0};
        snprintf(lang, sizeof lang, SF2C_AM_S_S_0030, settings_language());
        memcpy(resp->data, lang, SF2C_AM_N_8_2C62);
        resp_size(resp, SF2C_AM_N_8_2C62);
        break;
    }
    case SF2C_AM_N_23_535F: memcpy(resp->data, SF2C_AM_S_1_0_0_39FE, SF2C_AM_N_6_E7F6); resp_size(resp, SF2C_AM_N_0X10_BF9C); break;
    case SF2C_AM_N_40_D59E: OUT(uint8_t, 0) = 0; resp_size(resp, 1); break;  // NotifyRunning
    case SF2C_AM_N_130_38D6: case SF2C_AM_N_140_DBAE: resp->copy_handles[resp->ncopy++] = new_event(); break;
    default: UNHANDLED(SF2C_AM_S_IAPPLICATIONFUNCTIONS_A66C);
    }
}

static void application_proxy(Service *s, IpcReq *req, IpcResp *resp) {
    (void)s;
    static const struct { int cmd; const char *name; ServiceHandler h; } subs[] = {
        {0, SF2C_AM_S_ICOMMONSTATEGETTER_4E19, common_state_getter},
        {1, SF2C_AM_S_ISELFCONTROLLER_8E8A, self_controller},
        {SF2C_AM_N_2_D473, SF2C_AM_S_IWINDOWCONTROLLER_8831, window_controller},
        {SF2C_AM_N_3_4E07, SF2C_AM_S_IAUDIOCONTROLLER_1E82, generic_ok},
        {SF2C_AM_N_4_4B22, SF2C_AM_S_IDISPLAYCONTROLLER_013B, generic_ok},
        {SF2C_AM_N_10_4A44, SF2C_AM_S_IPROCESSWINDINGCONTROLLER_34C1, generic_ok},
        {SF2C_AM_N_11_4FC8, SF2C_AM_S_ILIBRARYAPPLETCREATOR_A2F7, generic_ok},
        {SF2C_AM_N_20_F5CA, SF2C_AM_S_IAPPLICATIONFUNCTIONS_A66C, application_functions},
        {SF2C_AM_N_1000_4051, SF2C_AM_S_IDEBUGFUNCTIONS_A4C8, generic_ok},
    };
    for (unsigned i = 0; i < sizeof subs / sizeof *subs; i++) {
        if (subs[i].cmd == (int)req->cmd) {
            ipc_return_object(resp, service_new(subs[i].name, subs[i].h, NULL));
            return;
        }
    }
    UNHANDLED(SF2C_AM_S_IAPPLICATIONPROXY_6782);
}

SERVICE(SF2C_AM_S_APPLETOE_FC7E, applet_oe) {
    (void)s;
    am_init_once();
    if (req->cmd == 0) ipc_return_object(resp, service_new(SF2C_AM_S_IAPPLICATIONPROXY_6782, application_proxy, NULL));
    else UNHANDLED(SF2C_AM_S_APPLETOE_FC7E);
}

// ------------------------------------------------------------------ apm
static void apm_session(Service *s, IpcReq *req, IpcResp *resp) {
    (void)s;
    if (req->cmd == 1) { OUT(uint32_t, 0) = SF2C_AM_N_0X92220008_B9AD; resp_size(resp, SF2C_AM_N_4_4B22); }  // GetPerformanceConfiguration
}

SERVICE(SF2C_AM_S_APM_2818, apm) {
    (void)s;
    if (req->cmd == 0) ipc_return_object(resp, service_new(SF2C_AM_S_APM_ISESSION_2560, apm_session, NULL));
    else if (req->cmd == 1) { OUT(uint32_t, 0) = 1; resp_size(resp, SF2C_AM_N_4_4B22); }
}

// ------------------------------------------------------------------ lm (guest log output)
static void lm_logger(Service *s, IpcReq *req, IpcResp *resp) {
    (void)s; (void)resp;
    if (req->cmd != 0) return;
    IpcBuf *b = in_buf(req, 0);
    if (!b || !b->size) return;
    // Log packets are TLV chunks after a 0x18-byte header; print the string chunks.
    const uint8_t *p = (const uint8_t *)(uintptr_t)b->addr, *end = p + b->size;
    p += SF2C_AM_N_0X18_D47C;
    char line[SF2C_AM_N_1024_E39E];
    int n = 0;
    while (p + SF2C_AM_N_2_D473 <= end) {
        uint8_t key = p[0], len = p[1];
        p += SF2C_AM_N_2_D473;
        if (p + len > end) break;
        if (key == SF2C_AM_N_2_D473 || key == SF2C_AM_N_4_4B22 || key == SF2C_AM_N_5_EF2D || key == SF2C_AM_N_6_E7F6 || key == SF2C_AM_N_7_7902) {  // message, file, function, module, thread
            n += snprintf(line + n, sizeof line - n, SF2C_AM_S_S_S_AEC5, n ? SF2C_AM_S_EMPTY_658F : SF2C_AM_S_EMPTY_12AE, len, (const char *)p);
            if (n >= (int)sizeof line) break;
        }
        p += len;
    }
    if (n) LOG(SF2C_AM_S_LM_S_N_AC18, line);
}

SERVICE(SF2C_AM_S_LM_0DBC, lm) {
    (void)s;
    if (req->cmd == 0) ipc_return_object(resp, service_new(SF2C_AM_S_LM_ILOGGER_090B, lm_logger, NULL));
}
