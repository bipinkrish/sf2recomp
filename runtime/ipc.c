#include "constants/core.h"
// IPC message parsing/serialization and session management.
#include <stdlib.h>
#include <string.h>

#include "hle.h"
#include "ipc.h"
#include "network.h"

#ifdef __APPLE__
extern const ServiceDef __start_services[] __asm__(SF2C_IPC_S_SECTION_START_DATA_SERVICES_C3F3);
extern const ServiceDef __stop_services[] __asm__(SF2C_IPC_S_SECTION_END_DATA_SERVICES_AEF2);
#elif defined(__linux__)
extern const ServiceDef __start_services[], __stop_services[];  // (ELF: defined by the linker)
#else
// PE/COFF: .svc$a < .svc$m (entries, ipc.h) < .svc$z (the linker sorts by the text after '$')
__attribute__((section(SF2C_IPC_S_SVC_A_F31B), used)) static const ServiceDef __start_services[1];
__attribute__((section(SF2C_IPC_S_SVC_Z_11E0), used)) static const ServiceDef __stop_services[1];
#endif

static void stub_handler(Service *s, IpcReq *req, IpcResp *resp) {
    LOG(SF2C_IPC_S_IPC_STUB_S_CMD_U_IN_U_BYTES__48C6, s->name, req->cmd, req->data_size, req->nx,
        req->na, req->nb, req->nc);
    resp->result = 0;
    resp_size(resp, SF2C_IPC_N_0X40_1AF3);  // zeroed outputs
}

static void offline_handler(Service *s, IpcReq *req, IpcResp *resp) {
    network_deny_ipc(s->name, req, resp);
}

Service *service_new(const char *name, ServiceHandler h, void *state) {
    Service *s = calloc(1, sizeof *s);
    s->name = strdup(name);
    s->handler = h;
    s->state = state;
    return s;
}

Service *service_lookup(const char *name) {
    if (network_service_blocked(name)) return service_new(name, offline_handler, NULL);
    const ServiceDef *start = __start_services, *stop = __stop_services;
    __asm__(SF2C_IPC_S_EMPTY_12AE : SF2C_IPC_S_R_E649(start), SF2C_IPC_S_R_E649(stop));  // separate objects: keep the compiler from reasoning about them
    for (const ServiceDef *d = start; d < stop; d++)
        if (d->name && !strcmp(d->name, name)) return service_new(name, d->handler, NULL);
    LOG(SF2C_IPC_S_IPC_NO_HLE_IMPLEMENTATION_FO_AFCB, name);
    return service_new(name, stub_handler, NULL);
}

void ipc_return_object(IpcResp *resp, Service *s) { resp->objects[resp->nobjects++] = s; }

static uint32_t session_new(Service *s) {
    KSession *k = calloc(1, sizeof *k);
    k->obj.type = KOBJ_SESSION;
    k->svc = s;
    s->refs++;
    return kernel_new_handle(&k->obj);
}

static void service_release(Service *s) {
    if (s && --s->refs == 0 && s->on_close) s->on_close(s);
}

uint32_t ipc_connect_named_port(const char *name) {
    if (!strcmp(name, SF2C_IPC_S_SM_042D)) return session_new(service_lookup(SF2C_IPC_S_SM_042D));
    LOG(SF2C_IPC_S_IPC_CONNECTTONAMEDPORT_S_UNS_9E3F, name);
    return 0;
}

// ------------------------------------------------------------------ sm:
SERVICE(SF2C_IPC_S_SM_042D, sm_handler) {
    (void)s;
    switch (req->cmd) {
    case 0:  // RegisterClient
        break;
    case 1: {  // GetServiceHandle(name)
        char name[SF2C_IPC_N_9_1958] = {0};
        memcpy(name, req->data, SF2C_IPC_N_8_2C62);
        LOG(SF2C_IPC_S_SM_GETSERVICE_S_N_AD81, name);
        ipc_return_object(resp, service_lookup(name));
        break;
    }
    default:
        LOG(SF2C_IPC_S_SM_UNKNOWN_CMD_U_N_3D72, req->cmd);
    }
}

// ------------------------------------------------------------------ message codec
static uint64_t buf_addr(const uint32_t *w) {
    return w[1] | ((uint64_t)(w[SF2C_IPC_N_2_D473] >> SF2C_IPC_N_28_59E1) << SF2C_IPC_N_32_E29C) | ((uint64_t)((w[SF2C_IPC_N_2_D473] >> SF2C_IPC_N_2_D473) & SF2C_IPC_N_7_7902) << SF2C_IPC_N_36_76A5);
}

int ipc_handle_request(Cpu *c, uint32_t handle) {
    KObject *o = handle_get(handle);
    if (!o || o->type != KOBJ_SESSION) {
        LOG(SF2C_IPC_S_IPC_SENDSYNCREQUEST_ON_BAD_H_19BB, handle);
        return SF2C_IPC_N_0XE401_004F;
    }
    KSession *sess = (KSession *)o;
    uint32_t *msg = (uint32_t *)(uintptr_t)c->tpidrro;
    IpcReq req = {0};
    IpcResp resp = {0};
    req.type = msg[0] & SF2C_IPC_N_0XFFFF_F51F;
    int nx = (msg[0] >> SF2C_IPC_N_16_B17E) & SF2C_IPC_N_0XF_7419, na = (msg[0] >> SF2C_IPC_N_20_F5CA) & SF2C_IPC_N_0XF_7419, nb = (msg[0] >> SF2C_IPC_N_24_C235) & SF2C_IPC_N_0XF_7419;
    uint32_t raw_words = msg[1] & SF2C_IPC_N_0X3FF_5488;
    uint32_t cflags = (msg[1] >> SF2C_IPC_N_10_4A44) & SF2C_IPC_N_0XF_7419;
    uint32_t *p = msg + SF2C_IPC_N_2_D473;
    if (msg[1] & SF2C_IPC_N_0X80000000U_AE05) {
        uint32_t hd = *p++;
        if (hd & 1) { req.pid = p[0] | ((uint64_t)p[1] << SF2C_IPC_N_32_E29C); p += SF2C_IPC_N_2_D473; }
        req.ncopy = (hd >> 1) & SF2C_IPC_N_0XF_7419;
        req.nmove = (hd >> SF2C_IPC_N_5_EF2D) & SF2C_IPC_N_0XF_7419;
        for (int i = 0; i < req.ncopy; i++) req.copy_handles[i] = *p++;
        for (int i = 0; i < req.nmove; i++) req.move_handles[i] = *p++;
    }
    for (int i = 0; i < nx; i++, p += SF2C_IPC_N_2_D473) {
        req.x[i].addr = p[1] | ((uint64_t)((p[0] >> SF2C_IPC_N_12_6B51) & SF2C_IPC_N_0XF_7419) << SF2C_IPC_N_32_E29C) | ((uint64_t)((p[0] >> SF2C_IPC_N_6_E7F6) & SF2C_IPC_N_0X7_ECB4) << SF2C_IPC_N_36_76A5);
        req.x[i].size = p[0] >> SF2C_IPC_N_16_B17E;
    }
    req.nx = nx;
    for (int i = 0; i < na; i++, p += SF2C_IPC_N_3_4E07) {
        req.a[i].size = p[0] | ((uint64_t)((p[SF2C_IPC_N_2_D473] >> SF2C_IPC_N_24_C235) & SF2C_IPC_N_0XF_7419) << SF2C_IPC_N_32_E29C);
        req.a[i].addr = buf_addr(p);
    }
    req.na = na;
    for (int i = 0; i < nb; i++, p += SF2C_IPC_N_3_4E07) {
        req.b[i].size = p[0] | ((uint64_t)((p[SF2C_IPC_N_2_D473] >> SF2C_IPC_N_24_C235) & SF2C_IPC_N_0XF_7419) << SF2C_IPC_N_32_E29C);
        req.b[i].addr = buf_addr(p);
    }
    req.nb = nb;
    uint32_t *raw = p;
    // raw data is aligned to 16 bytes relative to the message start
    uint32_t *aligned = (uint32_t *)(((uintptr_t)raw + SF2C_IPC_N_15_E629) & ~(uintptr_t)SF2C_IPC_N_15_E629);
    if (cflags >= SF2C_IPC_N_2_D473) {
        uint32_t *cdesc = raw + raw_words;
        int ncd = cflags == SF2C_IPC_N_2_D473 ? 1 : (int)cflags - SF2C_IPC_N_2_D473;
        for (int i = 0; i < ncd; i++) {
            req.c[i].addr = cdesc[SF2C_IPC_N_2_D473 * i] | ((uint64_t)(cdesc[SF2C_IPC_N_2_D473 * i + 1] & SF2C_IPC_N_0XFFFF_F51F) << SF2C_IPC_N_32_E29C);
            req.c[i].size = cdesc[SF2C_IPC_N_2_D473 * i + 1] >> SF2C_IPC_N_16_B17E;
        }
        req.nc = ncd;
    }

    if (req.type == SF2C_IPC_N_2_D473) {  // close
        LOG(SF2C_IPC_S_IPC_CLOSE_SESSION_S_HANDLE_X_51E1, sess->svc->name, handle);
        if (!sess->domain) service_release(sess->svc);  // (a domain's objects stay: closed one by one)
        return 0;
    }
    uint8_t *payload = (uint8_t *)aligned;
    bool dom = false;  // response is domain-wrapped
    if (req.type == SF2C_IPC_N_4_4B22 || req.type == SF2C_IPC_N_6_E7F6) {
        if (sess->domain) {
            dom = true;
            uint8_t dcmd = payload[0];
            uint8_t nin = payload[1];
            uint16_t dsize = *(uint16_t *)(payload + SF2C_IPC_N_2_D473);
            req.domain_obj = *(uint32_t *)(payload + SF2C_IPC_N_4_4B22);
            if (dcmd == SF2C_IPC_N_2_D473) {  // close object
                if (req.domain_obj < SF2C_IPC_N_256_51E8) {
                    Service *o = sess->domain->objs[req.domain_obj];
                    sess->domain->objs[req.domain_obj] = NULL;
                    service_release(o);
                }
                goto write_response;
            }
            req.ndomain_in = nin;
            for (int i = 0; i < nin; i++) req.domain_in_objs[i] = *(uint32_t *)(payload + SF2C_IPC_N_16_B17E + dsize + SF2C_IPC_N_4_4B22 * i);
            payload += SF2C_IPC_N_16_B17E;
        }
        if (memcmp(payload, SF2C_IPC_S_SFCI_7A30, SF2C_IPC_N_4_4B22)) {
            LOG(SF2C_IPC_S_IPC_BAD_SFCI_MAGIC_ON_S_DOMA_7BAE, sess->svc->name, sess->domain != NULL);
            for (int i = 0; i < SF2C_IPC_N_24_C235; i++) LOG(SF2C_IPC_S_08X_981A, msg[i]);
            LOG(SF2C_IPC_S_N_D7A5);
        }
        req.cmd = *(uint32_t *)(payload + SF2C_IPC_N_8_2C62);
        req.data = payload + SF2C_IPC_N_16_B17E;
        req.data_size = raw_words * SF2C_IPC_N_4_4B22 > SF2C_IPC_N_32_E29C ? raw_words * SF2C_IPC_N_4_4B22 - SF2C_IPC_N_32_E29C : 0;
        Service *target = sess->svc;
        if (sess->domain) {
            target = req.domain_obj < SF2C_IPC_N_256_51E8 ? sess->domain->objs[req.domain_obj] : NULL;
            if (!target) { LOG(SF2C_IPC_S_IPC_DOMAIN_OBJECT_U_MISSING__BCA0, req.domain_obj); return SF2C_IPC_N_0XE401_004F; }
        }
        // Check the root session too: domain objects may use interface names
        // rather than the original network service's name.
        if (network_service_blocked(sess->svc->name)) network_deny_ipc(sess->svc->name, &req, &resp);
        else target->handler(target, &req, &resp);
    } else if (req.type == SF2C_IPC_N_5_EF2D || req.type == SF2C_IPC_N_7_7902) {  // control
        req.cmd = *(uint32_t *)(payload + SF2C_IPC_N_8_2C62);
        LOG(SF2C_IPC_S_IPC_CONTROL_U_ON_S_HANDLE_X__5A72, req.cmd, sess->svc->name, handle);
        switch (req.cmd) {
        case 0:  // ConvertCurrentObjectToDomain
            sess->domain = calloc(1, sizeof(Domain));
            sess->domain->objs[1] = sess->svc;
            resp_size(&resp, SF2C_IPC_N_4_4B22);
            *(uint32_t *)resp.data = 1;
            break;
        case SF2C_IPC_N_2_D473: case SF2C_IPC_N_4_4B22: {  // CloneCurrentObject(Ex)
            uint32_t h = session_new(sess->svc);
            ((KSession *)handle_get(h))->domain = sess->domain;
            resp.move_handles[resp.nmove++] = h;
            break;
        }
        case SF2C_IPC_N_3_4E07:  // QueryPointerBufferSize
            *(uint16_t *)resp.data = SF2C_IPC_N_0X8000_7702;
            resp_size(&resp, SF2C_IPC_N_4_4B22);
            break;
        default:
            LOG(SF2C_IPC_S_IPC_CONTROL_CMD_U_N_4BF4, req.cmd);
        }
    } else {
        LOG(SF2C_IPC_S_IPC_MESSAGE_TYPE_U_N_6DEB, req.type);
    }

write_response:;
    // returned objects: domain objects or new sessions
    uint32_t domain_out[SF2C_IPC_N_8_2C62];
    int ndomain_out = 0;
    for (int i = 0; i < resp.nobjects; i++) {
        if (dom) {
            int id = 1;
            while (id < SF2C_IPC_N_256_51E8 && sess->domain->objs[id]) id++;
            sess->domain->objs[id] = resp.objects[i];
            resp.objects[i]->refs++;
            domain_out[ndomain_out++] = id;
        } else {
            resp.move_handles[resp.nmove++] = session_new(resp.objects[i]);
        }
    }
    memset(msg, 0, SF2C_IPC_N_0X100_BE6E);
    uint32_t *w = msg;
    int has_handles = resp.ncopy || resp.nmove;
    uint32_t payload_words = SF2C_IPC_N_4_4B22 + (resp.data_size + SF2C_IPC_N_3_4E07) / SF2C_IPC_N_4_4B22;   // SFCO header + data
    if (dom) payload_words += SF2C_IPC_N_4_4B22 + ndomain_out;
    uint32_t raw_size = payload_words + SF2C_IPC_N_4_4B22;  // + alignment padding
    w[0] = 0;  // type 0 response
    w[1] = raw_size | (has_handles ? SF2C_IPC_N_0X80000000U_AE05 : 0);
    uint32_t *q = w + SF2C_IPC_N_2_D473;
    if (has_handles) {
        *q++ = (resp.ncopy << 1) | (resp.nmove << SF2C_IPC_N_5_EF2D);
        for (int i = 0; i < resp.ncopy; i++) *q++ = resp.copy_handles[i];
        for (int i = 0; i < resp.nmove; i++) *q++ = resp.move_handles[i];
    }
    uint32_t *out = (uint32_t *)(((uintptr_t)q + SF2C_IPC_N_15_E629) & ~(uintptr_t)SF2C_IPC_N_15_E629);
    if (dom) {
        out[0] = ndomain_out;
        out += SF2C_IPC_N_4_4B22;
    }
    memcpy(out, SF2C_IPC_S_SFCO_A1D4, SF2C_IPC_N_4_4B22);
    out[1] = 0;
    out[SF2C_IPC_N_2_D473] = resp.result;
    out[SF2C_IPC_N_3_4E07] = 0;
    memcpy(out + SF2C_IPC_N_4_4B22, resp.data, resp.data_size);
    if (dom)
        for (int i = 0; i < ndomain_out; i++) out[SF2C_IPC_N_4_4B22 + (resp.data_size + SF2C_IPC_N_3_4E07) / SF2C_IPC_N_4_4B22 + i] = domain_out[i];
    return 0;
}
