#include "../constants/services.h"
// hid (shared memory, input state filled in later) and settings (set, set:sys).
#include <string.h>

#include "../hle.h"
#include "../ipc.h"
#include "../settings.h"
#include "../rumble.h"

static uint32_t new_event_handle(void) {
    uint32_t w, r;
    kernel_create_event_pair(&w, &r);
    return r;
}

// ------------------------------------------------------------------ hid
KSharedMem *g_hid_shmem;
static uint32_t hid_shmem_handle;
RT_ON_RESET(hid_reset) { g_hid_shmem = NULL; hid_shmem_handle = 0; }

static void applet_resource(Service *s, IpcReq *req, IpcResp *resp) {
    (void)s;
    if (req->cmd == 0) {  // GetSharedMemoryHandle
        if (!g_hid_shmem) g_hid_shmem = kernel_create_shared_memory(SF2C_HID_SET_N_0X40000_1443, NULL, &hid_shmem_handle);
        resp->copy_handles[resp->ncopy++] = hid_shmem_handle;
    }
}

SERVICE(SF2C_HID_SET_S_HID_A2DE, hid) {
    (void)s;
    switch (req->cmd) {
    case 0: ipc_return_object(resp, service_new(SF2C_HID_SET_S_IAPPLETRESOURCE_5FB1, applet_resource, NULL)); break;
    case SF2C_HID_SET_N_40_D59E: case SF2C_HID_SET_N_106_482D: resp->copy_handles[resp->ncopy++] = new_event_handle(); break;
    case SF2C_HID_SET_N_101_16DC: OUT(uint32_t, 0) = SF2C_HID_SET_N_0X1F_A571; resp_size(resp, SF2C_HID_SET_N_4_4B22); break;  // GetSupportedNpadStyleSet
    case SF2C_HID_SET_N_121_89AA: OUT(int64_t, 0) = 0; resp_size(resp, SF2C_HID_SET_N_8_2C62); break;      // GetNpadJoyHoldType
    case SF2C_HID_SET_N_129_6566: OUT(int64_t, 0) = 0; resp_size(resp, SF2C_HID_SET_N_8_2C62); break;      // GetNpadHandheldActivationMode
    case SF2C_HID_SET_N_201_4397: {  // SendVibrationValue: handle, four floats, padding, ARUID
        if (req->data_size < SF2C_HID_SET_N_32_E29C) { resp->result = SF2C_HID_SET_N_0XD401_D8B2; break; }
        uint32_t h; RumbleValue v;
        memcpy(&h, req->data, sizeof h);
        memcpy(&v, req->data + SF2C_HID_SET_N_4_4B22, sizeof v);
        input_rumble_send(h, &v);
        break;
    }
    case SF2C_HID_SET_N_202_C17E: {  // GetActualVibrationValue (best-effort requested state)
        if (req->data_size < SF2C_HID_SET_N_16_B17E) { resp->result = SF2C_HID_SET_N_0XD401_D8B2; break; }
        RumbleValue v = input_rumble_get(IN(uint32_t, 0));
        memcpy(resp->data, &v, sizeof v); resp_size(resp, sizeof v);
        break;
    }
    case SF2C_HID_SET_N_204_FC56:
        if (req->data_size < 1) { resp->result = SF2C_HID_SET_N_0XD401_D8B2; break; }
        input_rumble_permit((IN(uint8_t, 0) & 1) != 0); break;
    case SF2C_HID_SET_N_205_F880: OUT(uint8_t, 0) = input_rumble_permitted(); resp_size(resp, 1); break;
    case SF2C_HID_SET_N_211_0934:  // virtual player-1 vibration device is mounted, as advertised by HLE
        OUT(uint8_t, 0) = req->data_size >= SF2C_HID_SET_N_4_4B22 && rumble_side(IN(uint32_t, 0)) >= 0;
        resp_size(resp, 1); break;
    case SF2C_HID_SET_N_206_5CF4: {  // SendVibrationValues: parallel handle/value pointer buffers
        IpcBuf *handles = in_buf(req, 0), *values = in_buf(req, 1);
        if (!handles || !values || handles->size % SF2C_HID_SET_N_4_4B22 || values->size % sizeof(RumbleValue) ||
            handles->size / SF2C_HID_SET_N_4_4B22 != values->size / sizeof(RumbleValue) ||
            handles->size / SF2C_HID_SET_N_4_4B22 > SF2C_HID_SET_N_64_A68B || (handles->size && (!handles->addr || !values->addr))) {
            resp->result = SF2C_HID_SET_N_0XD401_D8B2; break;
        }
        // Validate both lengths before sending anything. memcpy avoids assuming
        // that guest pointer buffers are aligned like host floats/uint32_t.
        for (uint64_t i = 0; i < handles->size / SF2C_HID_SET_N_4_4B22; i++) {
            uint32_t h; RumbleValue v;
            memcpy(&h, (const void *)(uintptr_t)(handles->addr + i * SF2C_HID_SET_N_4_4B22), sizeof h);
            memcpy(&v, (const void *)(uintptr_t)(values->addr + i * sizeof v), sizeof v);
            input_rumble_send(h, &v);
        }
        break;
    }
    case SF2C_HID_SET_N_203_4621: case SF2C_HID_SET_N_207_9680: case SF2C_HID_SET_N_208_8DF6: case SF2C_HID_SET_N_209_83F8: case SF2C_HID_SET_N_210_D29D: break;  // activation HLE'd; GC ERM / permit sessions not implemented
    case SF2C_HID_SET_N_200_27BA: memset(resp->data, 0, SF2C_HID_SET_N_0X10_BF9C); OUT(uint32_t, 0) = 1; resp_size(resp, SF2C_HID_SET_N_0X10_BF9C); break;
    default: break;  // Activate*/Set*: accept
    }
}

SERVICE(SF2C_HID_SET_S_HID_SYS_C256, hid_sys) { (void)s; (void)req; resp_size(resp, SF2C_HID_SET_N_0X10_BF9C); }

// ------------------------------------------------------------------ settings
static const char *const languages[] = {SF2C_HID_SET_S_JA_4A86, SF2C_HID_SET_S_EN_US_C528, SF2C_HID_SET_S_FR_92E5, SF2C_HID_SET_S_DE_FA20, SF2C_HID_SET_S_IT_FF56, SF2C_HID_SET_S_ES_14BA, SF2C_HID_SET_S_ZH_CN_CB6A, SF2C_HID_SET_S_KO_F0B0, SF2C_HID_SET_S_NL_4B79,
                                         SF2C_HID_SET_S_PT_04FA, SF2C_HID_SET_S_RU_A8CA, SF2C_HID_SET_S_ZH_TW_6F1B, SF2C_HID_SET_S_EN_GB_90A2, SF2C_HID_SET_S_FR_CA_5D6D, SF2C_HID_SET_S_ES_419_F8B3, SF2C_HID_SET_S_ZH_HANS_0E73,
                                         SF2C_HID_SET_S_ZH_HANT_64A8, SF2C_HID_SET_S_PT_BR_2EF7};
#define NLANG SF2C_HID_SET_NLANG
static uint64_t lang_code(const char *s) {
    uint64_t v = 0;
    memcpy(&v, s, strnlen(s, sizeof v));
    return v;
}

static void language_list(IpcReq *req, IpcResp *resp, int max) {
    IpcBuf *b = out_buf(req, 0);
    int n = 0;
    if (b) {
        uint64_t *out = (uint64_t *)(uintptr_t)b->addr;
        for (; n < max && n < NLANG && (uint64_t)(n + 1) * SF2C_HID_SET_N_8_2C62 <= b->size; n++) out[n] = lang_code(languages[n]);
    }
    OUT(int32_t, 0) = n;
    resp_size(resp, SF2C_HID_SET_N_4_4B22);
}

SERVICE(SF2C_HID_SET_S_SET_C7F5, set) {
    (void)s;
    switch (req->cmd) {
    case 0: OUT(uint64_t, 0) = lang_code(settings_language()); resp_size(resp, SF2C_HID_SET_N_8_2C62); break;  // GetLanguageCode
    case 1: language_list(req, resp, SF2C_HID_SET_N_15_E629); break;                                   // GetAvailableLanguageCodes
    case SF2C_HID_SET_N_2_D473: {                                                                      // MakeLanguageCode
        uint32_t i = IN(uint32_t, 0);
        OUT(uint64_t, 0) = i < NLANG ? lang_code(languages[i]) : 0;
        resp_size(resp, SF2C_HID_SET_N_8_2C62);
        break;
    }
    case SF2C_HID_SET_N_3_4E07: OUT(int32_t, 0) = SF2C_HID_SET_N_15_E629; resp_size(resp, SF2C_HID_SET_N_4_4B22); break;
    case SF2C_HID_SET_N_4_4B22: OUT(uint32_t, 0) = 1; resp_size(resp, SF2C_HID_SET_N_4_4B22); break;                       // GetRegionCode: USA
    case SF2C_HID_SET_N_5_EF2D: language_list(req, resp, NLANG); break;
    case SF2C_HID_SET_N_6_E7F6: OUT(int32_t, 0) = NLANG; resp_size(resp, SF2C_HID_SET_N_4_4B22); break;
    case SF2C_HID_SET_N_8_2C62: OUT(uint8_t, 0) = 0; resp_size(resp, 1); break;                        // GetQuestFlag
    default: LOG(SF2C_HID_SET_S_SET_CMD_U_OK_N_1544, req->cmd); resp_size(resp, SF2C_HID_SET_N_0X10_BF9C);
    }
}

SERVICE(SF2C_HID_SET_S_SET_SYS_EAF9, set_sys) {
    (void)s;
    switch (req->cmd) {
    case SF2C_HID_SET_N_3_4E07: case SF2C_HID_SET_N_4_4B22: {  // GetFirmwareVersion(2) -> B buffer 0x100
        IpcBuf *b = out_buf(req, 0);
        if (b) {
            uint8_t *v = (uint8_t *)(uintptr_t)b->addr;
            memset(v, 0, SF2C_HID_SET_N_0X100_BE6E);
            v[0] = SF2C_HID_SET_N_8_2C62; v[1] = 1; v[SF2C_HID_SET_N_2_D473] = 0; v[SF2C_HID_SET_N_4_4B22] = SF2C_HID_SET_N_8_2C62; v[SF2C_HID_SET_N_5_EF2D] = 1; v[SF2C_HID_SET_N_6_E7F6] = 0;  // 8.1.0
            strcpy((char *)v + SF2C_HID_SET_N_0X68_40D3, SF2C_HID_SET_S_8_1_0_BE9F);
            strcpy((char *)v + SF2C_HID_SET_N_0X80_D41C, SF2C_HID_SET_S_8_1_0_1_0_A835);
        }
        break;
    }
    case SF2C_HID_SET_N_23_535F: OUT(uint32_t, 0) = 1; resp_size(resp, SF2C_HID_SET_N_4_4B22); break;  // GetColorSetId: dark
    default: LOG(SF2C_HID_SET_S_SET_SYS_CMD_U_OK_N_BA58, req->cmd); resp_size(resp, SF2C_HID_SET_N_0X10_BF9C);
    }
}
