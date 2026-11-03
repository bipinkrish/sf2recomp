#include "constants/core.h"
// Offline by default at both the translated game and host service boundaries.
#include "network.h"
#include "hle.h"
#include "ipc.h"
#include "settings.h"
#include <string.h>

static bool starts_with(const char *name, const char *prefix) {
    return !strncmp(name, prefix, strlen(prefix));
}

bool network_service_blocked(const char *name) {
    if (g_settings.network_enabled) return false;
    // Transport, DNS, TLS, network configuration and local wireless services.
    // Match families too so adding a new privilege variant stays offline.
    static const char *const families[] = {
        SF2C_NETWORK_S_BSD_7724, SF2C_NETWORK_S_NIFM_70EB, SF2C_NETWORK_S_LDN_BEB9, SF2C_NETWORK_S_WLAN_33AE, SF2C_NETWORK_S_ETHC_2BE1, SF2C_NETWORK_S_NSD_E9C9, SF2C_NETWORK_S_NTC_8088,
    };
    for (size_t i = 0; i < sizeof families / sizeof *families; ++i)
        if (starts_with(name, families[i])) return true;
    return !strcmp(name, SF2C_NETWORK_S_SSL_310B) || !strcmp(name, SF2C_NETWORK_S_SFDNSRES_534C) || !strcmp(name, SF2C_NETWORK_S_NTC_B41F);
}

bool network_hle_blocked(const char *name) {
    if (g_settings.network_enabled) return false;
    // While offline, resolve these imports to the guarded guest SDK instead of
    // a host override, even if a transport HLE is registered in a later version.
    static const char *const prefixes[] = {
        SF2C_NETWORK_S_ZN2NN6SOCKET_644B, SF2C_NETWORK_S_ZNK2NN6SOCKET_584A, SF2C_NETWORK_S_NNSOCKET_A5CB,
        SF2C_NETWORK_S_ZN2NN3SSL_726C, SF2C_NETWORK_S_ZNK2NN3SSL_8E05, SF2C_NETWORK_S_NNSSL_8A12,
        SF2C_NETWORK_S_ZN2NN4NIFM_A31F, SF2C_NETWORK_S_ZNK2NN4NIFM_3F29,
        SF2C_NETWORK_S_ZN2NN3LDN_704A, SF2C_NETWORK_S_ZNK2NN3LDN_56C7,
    };
    for (size_t i = 0; i < sizeof prefixes / sizeof *prefixes; ++i)
        if (starts_with(name, prefixes[i])) return true;
    return false;
}

void network_deny_ipc(const char *name, const IpcReq *req, IpcResp *resp) {
    // Unity sets the SSL interface version during boot. Acknowledge only this
    // metadata operation locally; never invoke a service handler for it.
    memset(resp, 0, sizeof *resp);
    if (!strcmp(name, SF2C_NETWORK_S_SSL_310B) && req->cmd == SF2C_NETWORK_N_5_EF2D) return;
    resp->result = SF2C_NETWORK_N_0XFA01_EA29;  // Horizon ResultInvalidState: networking is disabled
}

static int offline_UNAVAILABLE(Cpu *c) { c->x[0] = 0; return 1; }
static int offline_VOID(Cpu *c) { (void)c; return 1; }

static int offline_SOCKET(Cpu *c) {
    // EACCES is 13 in the guest socket ABI, independent of the host errno ABI.
    // Use the SDK setter to update the guest thread's errno without touching
    // its arguments, stack or callee-saved registers.
    Cpu saved = *c;
    c->x[0] = SF2C_NETWORK_N_13_3FDB;
    guest_call(c, SDK_BASE + SF2C_NETWORK_N_0XC65B0_DEBC);  // nn::socket::detail::SetLastErrno(int)
    *c = saved;
    c->x[0] = UINT64_MAX;
    return 1;
}

static int offline_HOST(Cpu *c) {
    offline_SOCKET(c);
    Cpu saved = *c;
    guest_call(c, SDK_BASE + SF2C_NETWORK_N_0XD6A80_29EF);  // resolver::tls::Client::InternalHostErrno()
    if (c->x[0]) ST32(c->x[0], SF2C_NETWORK_N_3_4E07);  // resolver h_errno: NO_RECOVERY
    *c = saved;
    c->x[0] = 0;  // gethostbyname/gethostbyaddr return NULL on failure
    return 1;
}

static int offline_ADDRINFO(Cpu *c) {
    if (c->x[SF2C_NETWORK_N_3_4E07]) ST64(c->x[SF2C_NETWORK_N_3_4E07], 0);  // no address list to free on failure
    offline_SOCKET(c);
    c->x[0] = SF2C_NETWORK_N_4_4B22;  // guest resolver EAI_FAIL (non-recoverable resolution failure)
    return 1;
}

static int offline_NAMEINFO(Cpu *c) {
    if (c->x[SF2C_NETWORK_N_2_D473] && c->x[SF2C_NETWORK_N_3_4E07]) ST8(c->x[SF2C_NETWORK_N_2_D473], 0);
    if (c->x[SF2C_NETWORK_N_4_4B22] && c->x[SF2C_NETWORK_N_5_EF2D]) ST8(c->x[SF2C_NETWORK_N_4_4B22], 0);
    offline_SOCKET(c);
    c->x[0] = SF2C_NETWORK_N_4_4B22;
    return 1;
}

int network_hook(Cpu *c, uint64_t site) {
    if (g_settings.network_enabled) return 0;
    switch (site) {
#define NETWORK_HOOK(site, word, kind, description) case site##ull: return offline_##kind(c);
#include "network_hooks.inc"
#undef NETWORK_HOOK
    default: return 0;
    }
}
