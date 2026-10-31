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
        "bsd:", "nifm:", "ldn:", "wlan:", "ethc:", "nsd:", "ntc:",
    };
    for (size_t i = 0; i < sizeof families / sizeof *families; ++i)
        if (starts_with(name, families[i])) return true;
    return !strcmp(name, "ssl") || !strcmp(name, "sfdnsres") || !strcmp(name, "ntc");
}

bool network_hle_blocked(const char *name) {
    if (g_settings.network_enabled) return false;
    // While offline, resolve these imports to the guarded guest SDK instead of
    // a host override, even if a transport HLE is registered in a later version.
    static const char *const prefixes[] = {
        "_ZN2nn6socket", "_ZNK2nn6socket", "nnsocket",
        "_ZN2nn3ssl", "_ZNK2nn3ssl", "nnssl",
        "_ZN2nn4nifm", "_ZNK2nn4nifm",
        "_ZN2nn3ldn", "_ZNK2nn3ldn",
    };
    for (size_t i = 0; i < sizeof prefixes / sizeof *prefixes; ++i)
        if (starts_with(name, prefixes[i])) return true;
    return false;
}

void network_deny_ipc(const char *name, const IpcReq *req, IpcResp *resp) {
    // Unity sets the SSL interface version during boot. Acknowledge only this
    // metadata operation locally; never invoke a service handler for it.
    memset(resp, 0, sizeof *resp);
    if (!strcmp(name, "ssl") && req->cmd == 5) return;
    resp->result = 0xFA01;  // Horizon ResultInvalidState: networking is disabled
}

static int offline_UNAVAILABLE(Cpu *c) { c->x[0] = 0; return 1; }
static int offline_VOID(Cpu *c) { (void)c; return 1; }

static int offline_SOCKET(Cpu *c) {
    // EACCES is 13 in the guest socket ABI, independent of the host errno ABI.
    // Use the SDK setter to update the guest thread's errno without touching
    // its arguments, stack or callee-saved registers.
    Cpu saved = *c;
    c->x[0] = 13;
    guest_call(c, SDK_BASE + 0xc65b0);  // nn::socket::detail::SetLastErrno(int)
    *c = saved;
    c->x[0] = UINT64_MAX;
    return 1;
}

static int offline_HOST(Cpu *c) {
    offline_SOCKET(c);
    Cpu saved = *c;
    guest_call(c, SDK_BASE + 0xd6a80);  // resolver::tls::Client::InternalHostErrno()
    if (c->x[0]) ST32(c->x[0], 3);  // resolver h_errno: NO_RECOVERY
    *c = saved;
    c->x[0] = 0;  // gethostbyname/gethostbyaddr return NULL on failure
    return 1;
}

static int offline_ADDRINFO(Cpu *c) {
    if (c->x[3]) ST64(c->x[3], 0);  // no address list to free on failure
    offline_SOCKET(c);
    c->x[0] = 4;  // guest resolver EAI_FAIL (non-recoverable resolution failure)
    return 1;
}

static int offline_NAMEINFO(Cpu *c) {
    if (c->x[2] && c->x[3]) ST8(c->x[2], 0);
    if (c->x[4] && c->x[5]) ST8(c->x[4], 0);
    offline_SOCKET(c);
    c->x[0] = 4;
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
