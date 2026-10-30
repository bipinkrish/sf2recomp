// Small services: parental control, add-on content, and other mostly-stubbed interfaces.
#include <string.h>

#include "../hle.h"
#include "../game_info.h"
#include "../ipc.h"

static void ok_logger(Service *s, IpcReq *req, IpcResp *resp) {
    LOG("[svc] %s cmd %u (ok)\n", s->name, req->cmd);
    resp_size(resp, 0x10);
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
    case 1031: case 1035: case 1018: OUT(uint8_t, 0) = 0; resp_size(resp, 1); break;  // restrictions off
    case 1032: OUT(uint32_t, 0) = 0; resp_size(resp, 4); break;
    default: LOG("[pctl] IParentalControlService cmd %u (ok)\n", req->cmd); resp_size(resp, 0x10);
    }
}

static void pctl_root(Service *s, IpcReq *req, IpcResp *resp) {
    (void)s;
    if (req->cmd == 0 || req->cmd == 1)
        ipc_return_object(resp, service_new("IParentalControlService", pctl_service, NULL));
}
SERVICE("pctl", pctl) { pctl_root(s, req, resp); }
SERVICE("pctl:s", pctl_s) { pctl_root(s, req, resp); }
SERVICE("pctl:a", pctl_a) { pctl_root(s, req, resp); }
SERVICE("pctl:r", pctl_r) { pctl_root(s, req, resp); }

// ------------------------------------------------------------------ aoc:u (no DLC)
SERVICE("aoc:u", aoc_u) {
    (void)s;
    switch (req->cmd) {
    case 2: OUT(uint32_t, 0) = 0; resp_size(resp, 4); break;   // CountAddOnContent
    case 3: OUT(uint32_t, 0) = 0; resp_size(resp, 4); break;   // ListAddOnContent
    case 5: OUT(uint64_t, 0) = game_add_on_content_base_id(); resp_size(resp, 8); break;  // GetAddOnContentBaseId
    case 8: resp->copy_handles[resp->ncopy++] = new_event_handle(); break;
    default: ok_logger(s, req, resp);
    }
}

// ------------------------------------------------------------------ vi:m / vi:s / vi:u
static void vi_app_display_service(Service *s, IpcReq *req, IpcResp *resp) {
    (void)s;
    LOG("[vi] IApplicationDisplayService cmd %u\n", req->cmd);
    switch (req->cmd) {
    case 100: // GetRelayService
        ipc_return_object(resp, service_new("IHOSBinderDriver", ok_logger, NULL));
        break;
    case 101: // GetSystemDisplayService
        ipc_return_object(resp, service_new("ISystemDisplayService", ok_logger, NULL));
        break;
    case 102: // GetManagerDisplayService
        ipc_return_object(resp, service_new("IManagerDisplayService", ok_logger, NULL));
        break;
    case 1010: // OpenDisplay
    case 1011: // OpenDefaultDisplay
        OUT(uint64_t, 0) = 1; // display id
        resp_size(resp, 8);
        break;
    case 1102: // GetDisplayResolution
        OUT(uint64_t, 0) = 1280; // width
        OUT(uint64_t, 8) = 720;  // height
        resp_size(resp, 16);
        break;
    case 2020: // OpenLayer
    case 2030: // CreateStrayLayer
        OUT(uint64_t, 0) = 1; // layer id
        resp_size(resp, 8);
        break;
    default:
        resp_size(resp, 0x20);
        break;
    }
}

static void vi_root(Service *s, IpcReq *req, IpcResp *resp) {
    (void)s;
    LOG("[vi] %s cmd %u\n", s->name, req->cmd);
    switch (req->cmd) {
    case 2: // GetDisplayService(service_context) -> IApplicationDisplayService
        ipc_return_object(resp, service_new("IApplicationDisplayService", vi_app_display_service, NULL));
        break;
    default:
        ok_logger(s, req, resp);
        break;
    }
}
SERVICE("vi:m", vi_m) { vi_root(s, req, resp); }
SERVICE("vi:s", vi_s) { vi_root(s, req, resp); }
SERVICE("vi:u", vi_u) { vi_root(s, req, resp); }
