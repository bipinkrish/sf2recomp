"""Default offline policy survives real guest calls and future host transports."""
import pathlib
import re
import struct
import subprocess
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

from test_settings import ROOT, compile_harness

sys.path.insert(0, str(ROOT / "tools/recompr"))
import gen
import patches
from lifter import Lifter


class NetworkPolicyTests(unittest.TestCase):
    def test_config_and_all_guest_guards_preserve_cpu_and_fail_without_transport(self):
        source = r'''
#include <assert.h>
#include "network.h"
#include "settings.h"
#include "rt.h"
static int errno_calls;
static int guest_herrno;
void guest_call(Cpu *c, uint64_t pc) {
    if (pc == SDK_BASE + 0xd6a80) {
        memset(c, 0xa5, sizeof *c);
        c->x[0] = (uintptr_t)&guest_herrno;
        return;
    }
    assert(pc == SDK_BASE + 0xc65b0 && c->x[0] == 13);
    ++errno_calls;
    memset(c, 0xa5, sizeof *c); // a real guest call may clobber caller registers
}
enum { UNAVAILABLE, VOID, SOCKET, HOST, ADDRINFO, NAMEINFO };
static const struct { uint64_t site; int kind; } sites[] = {
#define NETWORK_HOOK(site, word, kind, desc) {site##ull, kind},
#include "network_hooks.inc"
#undef NETWORK_HOOK
};
int main(int argc, char **argv) {
    assert(argc == 5 && !g_settings.network_enabled);
    assert(settings_load_file(argv[1]) && !g_settings.network_enabled); // older INI
    assert(settings_load_file(argv[2]) && !g_settings.network_enabled); // invalid opt-in
    for (unsigned i = 0; i < sizeof sites / sizeof *sites; ++i) {
        Cpu cpu, expected;
        memset(&cpu, 0x5a, sizeof cpu);
        uint64_t result = 0xdeadbeef;
        char host[4] = "abc", service[4] = "xyz";
        if (sites[i].kind == ADDRINFO) cpu.x[3] = (uintptr_t)&result;
        if (sites[i].kind == NAMEINFO) {
            cpu.x[2] = (uintptr_t)host; cpu.x[3] = sizeof host;
            cpu.x[4] = (uintptr_t)service; cpu.x[5] = sizeof service;
        }
        expected = cpu;
        int calls = errno_calls;
        assert(network_hook(&cpu, sites[i].site) == 1);
        switch (sites[i].kind) {
        case VOID: break;
        case UNAVAILABLE: expected.x[0] = 0; break;
        case SOCKET: expected.x[0] = UINT64_MAX; break;
        case HOST:
            expected.x[0] = 0;
            assert(guest_herrno == 3);
            break;
        case ADDRINFO: expected.x[0] = 4; assert(result == 0); break;
        case NAMEINFO:
            expected.x[0] = 4;
            assert(!host[0] && !service[0] && host[1] == 'b' && service[1] == 'y');
            break;
        }
        assert(!memcmp(&cpu, &expected, sizeof cpu)); // especially SP/LR and vectors
        assert(errno_calls == calls + (sites[i].kind >= SOCKET));
    }
    Cpu cpu = {0}, before = cpu;
    assert(!network_hook(&cpu, 0x1234) && !memcmp(&cpu, &before, sizeof cpu));
    // Optional networking restores every original instruction without side effects.
    assert(settings_load_file(argv[3]) && g_settings.network_enabled);
    for (unsigned i = 0; i < sizeof sites / sizeof *sites; ++i) {
        memset(&cpu, 0x5a, sizeof cpu); before = cpu;
        int calls = errno_calls;
        assert(!network_hook(&cpu, sites[i].site));
        assert(!memcmp(&cpu, &before, sizeof cpu) && calls == errno_calls);
    }
    assert(settings_load_file(argv[4]) && !g_settings.network_enabled);
    assert(network_hook(&cpu, 0x70000aee20) && cpu.x[0] == 0);
}
'''
        self.run_harness(source, ["[general]\nlanguage=en-US\n", "[network]\nenabled=maybe\n",
                                  "[network]\nenabled=true\n", "[network]\nenabled=false\n"])

    def test_future_hle_and_ipc_handlers_cannot_bypass_offline_policy(self):
        source = r'''
#include <assert.h>
#include "hle.h"
#include "network.h"
typedef struct { const char *name; HostFn fn; } HleImpl;
extern const HleImpl __start_hle_impls[], __stop_hle_impls[];
/* LOOKUP_IMPLEMENTATION */
#include "ipc.c"
#include "settings.h"
static int transport_calls;
HLE_FUNC("_ZN2nn6socket6detail6SocketEiii", future_socket) { ++transport_calls; }
HLE_FUNC("_ZN2nn3ssl10Connection4ReadEPiPvj", future_tls) { ++transport_calls; }
HLE_FUNC("local_test", local_test) { c->x[0] = 77; }
SERVICE("bsd:u", future_bsd) { ++transport_calls; }
SERVICE("ssl", future_ssl) { ++transport_calls; }
static KObject *objects[16];
static unsigned next_handle;
uint32_t kernel_new_handle(KObject *o) { objects[++next_handle] = o; return next_handle; }
KObject *handle_get(uint32_t h) { return h <= next_handle ? objects[h] : NULL; }
void guest_call(Cpu *c, uint64_t pc) { assert(0); }
static uint32_t request(Service *root, Service *child, unsigned cmd) {
    _Alignas(16) uint32_t msg[64] = {4, 16};
    KSession session = {.obj.type=KOBJ_SESSION, .svc=root};
    Domain domain = {0};
    uint32_t *payload = msg + 4;
    if (child) {
        domain.objs[1] = child; session.domain = &domain;
        ((uint8_t *)payload)[0] = 1;
        ((uint16_t *)payload)[1] = 16;
        payload[1] = 1;
        payload += 4;
    }
    memcpy(payload, "SFCI", 4); payload[2] = cmd;
    uint32_t handle = kernel_new_handle(&session.obj);
    Cpu cpu = {.tpidrro=(uintptr_t)msg};
    assert(ipc_handle_request(&cpu, handle) == 0);
    payload = msg + 4 + (child ? 4 : 0);
    assert(!memcmp(payload, "SFCO", 4));
    return payload[2];
}
int main(void) {
    assert(!g_settings.network_enabled);
    const char *services[] = {"bsd:u", "bsd:s", "nifm:u", "nifm:s", "nifm:a",
        "ssl", "sfdnsres", "ldn:u", "wlan:inf", "ethc:i", "nsd:u", "ntc"};
    for (unsigned i=0; i<sizeof services/sizeof *services; ++i)
        assert(network_service_blocked(services[i]));
    assert(!network_service_blocked("fsp-srv") && !network_service_blocked("time:u"));
    assert(!hle_lookup_impl("_ZN2nn6socket6detail6SocketEiii"));
    assert(!hle_lookup_impl("_ZN2nn3ssl10Connection4ReadEPiPvj"));
    assert(hle_lookup_impl("local_test") == local_test);
    Service *blocked = service_lookup("bsd:u");
    IpcReq req = {.cmd=0}; IpcResp resp = {0};
    blocked->handler(blocked, &req, &resp);
    assert(resp.result && transport_calls == 0 && !resp.nobjects && !resp.nmove);
    // A previously obtained or domain-cloned service is checked at dispatch too.
    Service *root = service_new("bsd:u", future_bsd, NULL);
    Service *child = service_new("ISocketService", future_bsd, NULL);
    assert(request(root, NULL, 0) && request(root, child, 0));
    assert(transport_calls == 0);
    Service *tls = service_lookup("ssl");
    req.cmd = 5; memset(&resp, 0xff, sizeof resp);
    tls->handler(tls, &req, &resp);
    IpcResp zero = {0};
    assert(!memcmp(&resp, &zero, sizeof resp) && !transport_calls);
    assert(!request(service_new("ssl", future_ssl, NULL), NULL, 5));
    assert(!transport_calls); // SSL version setup is acknowledged without a host call
    g_settings.network_enabled = true;
    assert(hle_lookup_impl("_ZN2nn6socket6detail6SocketEiii") == future_socket);
    assert(hle_lookup_impl("_ZN2nn3ssl10Connection4ReadEPiPvj") == future_tls);
    Service *online = service_lookup("bsd:u");
    req.cmd = 0; online->handler(online, &req, &resp);
    assert(transport_calls == 1);
    assert(!request(root, child, 0) && transport_calls == 2);
}
'''
        dispatch = (ROOT / "runtime/dispatch.c").read_text()
        lookup = dispatch[dispatch.index("HostFn hle_lookup_impl("):dispatch.index("\nstatic void hle_unimplemented(")]
        self.run_harness(source.replace("/* LOOKUP_IMPLEMENTATION */", lookup))

    def test_generated_guards_run_before_the_original_transport_and_opt_in_restores_it(self):
        # Compile real generated code around a stand-in transport instruction.
        # Both main and SDK policies use the same mandatory hook contract.
        functions = []
        for index, site in enumerate((0x70000aee20, 0x70040cb7b0)):
            words = (0x91000400, 0xD65F03C0)  # add x0, x0, #1; ret
            module = gen.Module.__new__(gen.Module)
            module.base = module.lo = site; module.hi = site + 8
            module.words, module.cache = words, {}
            module.nso = types.SimpleNamespace(image=struct.pack("<II", *words))
            module.lifter = Lifter(site, "c")
            module.sym_names, module.eh_calls = {}, {}
            with patch.object(gen, "HOOKS", {site: (words[0], (), "offline test")}):
                code = "\n".join(module.emit_function(site, site + 8, {site}, {}, {}))
            functions.append(code.replace(f"f_{site:x}", f"transport{index}"))
        source = r'''
#include <assert.h>
#include "network.h"
#include "settings.h"
#include "rt.h"
#undef FN_ENTER
#define FN_ENTER(pc) ((void)0)
void guest_call(Cpu *c, uint64_t pc) { assert(pc == SDK_BASE + 0xc65b0); }
int rt_hook(Cpu *c, uint64_t site) { return network_hook(c, site); }
''' + "\n".join(functions) + r'''
int main(void) {
    Cpu cpu = {0}; cpu.x[0] = 40;
    transport0(&cpu); assert(cpu.x[0] == 0); // original add never executes
    transport1(&cpu); assert(cpu.x[0] == UINT64_MAX);
    g_settings.network_enabled = true; cpu.x[0] = 40;
    transport0(&cpu); assert(cpu.x[0] == 41);
    transport1(&cpu); assert(cpu.x[0] == 42);
}
'''
        self.run_harness(source)

    def test_checked_hook_manifest_is_complete_and_matches_local_game_when_available(self):
        entries = re.findall(r'NETWORK_HOOK\((0x[0-9a-f]+), (0x[0-9a-f]+), ([A-Z]+), "([^"]+)"\)',
                             (ROOT / "runtime/network_hooks.inc").read_text())
        self.assertTrue(entries)
        for address, word, kind, description in entries:
            site = int(address, 16)
            table = patches.HOOKS if site < 0x7004000000 else patches.HOOKS_SDK
            self.assertEqual(table[site], (int(word, 16), (), "offline policy: " + description))
        # Optional verification adds native instruction/ABI coverage without
        # requiring copyrighted game files for the source-only test suite.
        from nso import Nso
        for name, base in (("main", 0x7000000000), ("sdk", 0x7004000000)):
            path = ROOT / "game/extracted/update/exefs" / name
            if not path.is_file():
                continue
            nso = Nso(path)
            table = patches.HOOKS if name == "main" else patches.HOOKS_SDK
            for site, hook in table.items():
                self.assertEqual(nso.u32(site - base), hook[0], hex(site))
            if name == "sdk":
                for symbol, _, _, offset, _ in nso.exports():
                    match = re.match(r'_ZN2nn6socket(?:6detail|8resolver)(\d+)', symbol)
                    if not match:
                        continue
                    operation = symbol[match.end():match.end() + int(match[1])]
                    if operation in {"Socket", "SocketExempt", "Connect", "Send", "SendTo", "SendMsg",
                                     "SendMMsg", "Write", "Bind", "Listen", "Accept", "GetAddrInfo",
                                     "GetHostByName", "GetHostByAddr", "GetNameInfo"}:
                        self.assertIn(base + offset, table, symbol)

    def run_harness(self, source, configs=()):
        with tempfile.TemporaryDirectory(prefix="sf2-network-") as directory:
            root = pathlib.Path(directory)
            (root / "test.c").write_text(source)
            paths = []
            for i, config in enumerate(configs):
                path = root / f"{i}.ini"; path.write_text(config); paths.append(str(path))
            compile_harness(root / "test.c", root / "test")
            subprocess.run([str(root / "test"), *paths], check=True)
