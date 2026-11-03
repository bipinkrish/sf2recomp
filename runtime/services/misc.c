#include "../constants/services.h"
// Small services: parental control, add-on content, and other mostly-stubbed interfaces.
#include <string.h>

#include "../hle.h"
#include "../game_info.h"
#include "../ipc.h"

static void ok_logger(Service *s, IpcReq *req, IpcResp *resp) {
    LOG(SF2C_MISC_S_SVC_S_CMD_U_OK_N_7DB2, s->name, req->cmd);
    resp_size(resp, SF2C_MISC_N_0X10_BF9C);
}

static uint32_t new_event_handle(void) {
    uint32_t w, r;
    kernel_create_event_pair(&w, &r);
    return r;
}

// ------------------------------------------------------------------ pctl
static void pctl_service(Service *s, IpcReq *req, IpcResp *resp) {
    (void)s;
    switch (req->cmd) {
    case SF2C_MISC_N_1031_3E34: case SF2C_MISC_N_1035_F467: case SF2C_MISC_N_1018_9989: OUT(uint8_t, 0) = 0; resp_size(resp, 1); break;  // restrictions off
    case SF2C_MISC_N_1032_340A: OUT(uint32_t, 0) = 0; resp_size(resp, SF2C_MISC_N_4_4B22); break;
    default: LOG(SF2C_MISC_S_PCTL_IPARENTALCONTROLSERVICE_5350, req->cmd); resp_size(resp, SF2C_MISC_N_0X10_BF9C);
    }
}

static void pctl_root(Service *s, IpcReq *req, IpcResp *resp) {
    (void)s;
    if (req->cmd == 0 || req->cmd == 1)
        ipc_return_object(resp, service_new(SF2C_MISC_S_IPARENTALCONTROLSERVICE_F170, pctl_service, NULL));
}
SERVICE(SF2C_MISC_S_PCTL_5C6F, pctl) { pctl_root(s, req, resp); }
SERVICE(SF2C_MISC_S_PCTL_S_E49C, pctl_s) { pctl_root(s, req, resp); }
SERVICE(SF2C_MISC_S_PCTL_A_EE4D, pctl_a) { pctl_root(s, req, resp); }
SERVICE(SF2C_MISC_S_PCTL_R_51B1, pctl_r) { pctl_root(s, req, resp); }

// ------------------------------------------------------------------ aoc:u (no DLC)
SERVICE(SF2C_MISC_S_AOC_U_65AF, aoc_u) {
    (void)s;
    switch (req->cmd) {
    case SF2C_MISC_N_2_D473: OUT(uint32_t, 0) = 0; resp_size(resp, SF2C_MISC_N_4_4B22); break;   // CountAddOnContent
    case SF2C_MISC_N_3_4E07: OUT(uint32_t, 0) = 0; resp_size(resp, SF2C_MISC_N_4_4B22); break;   // ListAddOnContent
    case SF2C_MISC_N_5_EF2D: OUT(uint64_t, 0) = game_add_on_content_base_id(); resp_size(resp, SF2C_MISC_N_8_2C62); break;  // GetAddOnContentBaseId
    case SF2C_MISC_N_8_2C62: resp->copy_handles[resp->ncopy++] = new_event_handle(); break;
    default: ok_logger(s, req, resp);
    }
}

// ------------------------------------------------------------------ vi:m / vi:s / vi:u
static void vi_app_display_service(Service *s, IpcReq *req, IpcResp *resp) {
    (void)s;
    LOG(SF2C_MISC_S_VI_IAPPLICATIONDISPLAYSERVIC_937D, req->cmd);
    switch (req->cmd) {
    case SF2C_MISC_N_100_AD57: // GetRelayService
        ipc_return_object(resp, service_new(SF2C_MISC_S_IHOSBINDERDRIVER_DAE3, ok_logger, NULL));
        break;
    case SF2C_MISC_N_101_16DC: // GetSystemDisplayService
        ipc_return_object(resp, service_new(SF2C_MISC_S_ISYSTEMDISPLAYSERVICE_9641, ok_logger, NULL));
        break;
    case SF2C_MISC_N_102_3783: // GetManagerDisplayService
        ipc_return_object(resp, service_new(SF2C_MISC_S_IMANAGERDISPLAYSERVICE_D4F0, ok_logger, NULL));
        break;
    case SF2C_MISC_N_1010_7A5D: // OpenDisplay
    case SF2C_MISC_N_1011_3DD9: // OpenDefaultDisplay
        OUT(uint64_t, 0) = 1; // display id
        resp_size(resp, SF2C_MISC_N_8_2C62);
        break;
    case SF2C_MISC_N_1102_2773: // GetDisplayResolution
        OUT(uint64_t, 0) = SF2C_MISC_N_1280_6F13; // width
        OUT(uint64_t, SF2C_MISC_N_8_2C62) = SF2C_MISC_N_720_D829;  // height
        resp_size(resp, SF2C_MISC_N_16_B17E);
        break;
    case SF2C_MISC_N_2020_73A2: // OpenLayer
    case SF2C_MISC_N_2030_8E1F: // CreateStrayLayer
        OUT(uint64_t, 0) = 1; // layer id
        resp_size(resp, SF2C_MISC_N_8_2C62);
        break;
    default:
        resp_size(resp, SF2C_MISC_N_0X20_A766);
        break;
    }
}

static void vi_root(Service *s, IpcReq *req, IpcResp *resp) {
    (void)s;
    LOG(SF2C_MISC_S_VI_S_CMD_U_N_08C2, s->name, req->cmd);
    switch (req->cmd) {
    case SF2C_MISC_N_2_D473: // GetDisplayService(service_context) -> IApplicationDisplayService
        ipc_return_object(resp, service_new(SF2C_MISC_S_IAPPLICATIONDISPLAYSERVICE_97AB, vi_app_display_service, NULL));
        break;
    default:
        ok_logger(s, req, resp);
        break;
    }
}
SERVICE(SF2C_MISC_S_VI_M_4450, vi_m) { vi_root(s, req, resp); }
SERVICE(SF2C_MISC_S_VI_S_462D, vi_s) { vi_root(s, req, resp); }
SERVICE(SF2C_MISC_S_VI_U_11EB, vi_u) { vi_root(s, req, resp); }
