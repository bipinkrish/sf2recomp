#include "../constants/services.h"
// nfp:user - amiibo. Behaves like a console with NFC available but no amiibo ever touched: the game's own
// "no amiibo" path runs (issue #282: a null IUser used to crash Dampe's amiibo option).
// Command ids follow the public IUser interface layout.
#include <string.h>

#include "../hle.h"
#include "../ipc.h"

enum { NFP_STATE_NONE = 0, NFP_STATE_INITIALIZED = 1 };
enum { DEV_INITIALIZED = 0, DEV_SEARCHING = 1 };

// Result (module 115): no tag in range.
#define NFP_RESULT(desc) (SF2C_NFP_N_0X73U_A45A | ((uint32_t)(desc) << SF2C_NFP_N_9_1958))
#define RESULT_DEVICE_NOT_FOUND NFP_RESULT(SF2C_NFP_N_64_A68B)

typedef struct {
    int state, dev_state;
} NfpUser;

static NfpUser g_nfp;  // one IUser at a time is all a game opens

static uint32_t new_event_handle(void) {
    uint32_t w, r;
    kernel_create_event_pair(&w, &r);
    return r;
}

static void nfp_iuser(Service *s, IpcReq *req, IpcResp *resp) {
    (void)s;
    NfpUser *u = &g_nfp;
    switch (req->cmd) {
    case 0:  // Initialize
        u->state = NFP_STATE_INITIALIZED;
        u->dev_state = DEV_INITIALIZED;
        break;
    case 1:  // Finalize
        u->state = NFP_STATE_NONE;
        u->dev_state = DEV_INITIALIZED;
        break;
    case SF2C_NFP_N_2_D473: {  // ListDevices -> one NFC reader (the controller), so the game can search and time out / cancel
        IpcBuf *b = out_buf(req, 0);
        int n = 0;
        if (b && b->addr && b->size >= SF2C_NFP_N_8_2C62) {
            memset((void *)(uintptr_t)b->addr, 0, SF2C_NFP_N_8_2C62);  // device handle 0 (player 1 controller)
            n = 1;
        }
        OUT(uint32_t, 0) = (uint32_t)n;
        resp_size(resp, SF2C_NFP_N_4_4B22);
        break;
    }
    case SF2C_NFP_N_3_4E07:  // StartDetection
        u->dev_state = DEV_SEARCHING;
        break;
    case SF2C_NFP_N_4_4B22:  // StopDetection
        u->dev_state = DEV_INITIALIZED;
        break;
    case SF2C_NFP_N_5_EF2D: case SF2C_NFP_N_6_E7F6: case SF2C_NFP_N_7_7902: case SF2C_NFP_N_8_2C62: case SF2C_NFP_N_9_1958: case SF2C_NFP_N_10_4A44: case SF2C_NFP_N_11_4FC8: case SF2C_NFP_N_12_6B51:  // Mount .. CreateApplicationArea
    case SF2C_NFP_N_13_3FDB: case SF2C_NFP_N_14_8527: case SF2C_NFP_N_15_E629: case SF2C_NFP_N_16_B17E: case SF2C_NFP_N_24_C235:  // Get*Info, RecreateApplicationArea
        resp->result = RESULT_DEVICE_NOT_FOUND;  // no tag present
        break;
    case SF2C_NFP_N_17_4523: case SF2C_NFP_N_18_4EC9: case SF2C_NFP_N_23_535F:  // Attach{Activate,Deactivate,AvailabilityChange}Event (never signalled)
        resp->copy_handles[resp->ncopy++] = new_event_handle();
        break;
    case SF2C_NFP_N_19_9400:  // GetState
        OUT(uint32_t, 0) = (uint32_t)u->state;
        resp_size(resp, SF2C_NFP_N_4_4B22);
        break;
    case SF2C_NFP_N_20_F5CA:  // GetDeviceState
        OUT(uint32_t, 0) = (uint32_t)u->dev_state;
        resp_size(resp, SF2C_NFP_N_4_4B22);
        break;
    case SF2C_NFP_N_21_6F4B:  // GetNpadId
        OUT(uint32_t, 0) = 0;  // player 1
        resp_size(resp, SF2C_NFP_N_4_4B22);
        break;
    case SF2C_NFP_N_22_785F:  // GetApplicationAreaSize
        OUT(uint32_t, 0) = SF2C_NFP_N_0XD8_93CA;
        resp_size(resp, SF2C_NFP_N_4_4B22);
        break;
    default:
        LOG(SF2C_NFP_S_NFP_IUSER_CMD_U_OK_N_2B46, req->cmd);
        resp_size(resp, SF2C_NFP_N_0X10_BF9C);
    }
    if (req->cmd != SF2C_NFP_N_19_9400 && req->cmd != SF2C_NFP_N_20_F5CA) LOG(SF2C_NFP_S_NFP_IUSER_CMD_U_X_N_9A3D, req->cmd, resp->result);
}

SERVICE(SF2C_NFP_S_NFP_USER_A145, nfp_user) {
    (void)s;
    if (req->cmd == 0) {  // CreateUserInterface
        ipc_return_object(resp, service_new(SF2C_NFP_S_NFP_IUSER_3FB7, nfp_iuser, NULL));
        return;
    }
    LOG(SF2C_NFP_S_NFP_IUSERMANAGER_CMD_U_OK_N_DEE6, req->cmd);
}
