"""Disabled optional hook sites execute original instructions without callbacks."""
import pathlib
import struct
import subprocess
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/recompr"))
import gen
import patches
from lifter import Lifter
from test_settings import compile_harness


class HookGroupTests(unittest.TestCase):
    def test_removed_prompt_sites_preserve_guest_state(self):
        sites = (0x7000114f90, 0x7000cd7130, 0x7000cdb690,
                 0x7000ce0dd0, 0x7000d9aec0, 0x7000ce0f40,
                 0x7000ce0de0, 0x7000ce10a0, 0x7000f9a060)
        self.assertTrue(all(site not in patches.HOOKS for site in sites))
        harness = r'''
#include <assert.h>
#include "hle/hooks.c"
void guest_call(Cpu *c, uint64_t pc) { assert(!"unexpected guest call"); }
void *hle_alloc(size_t size) { assert(!"unexpected allocation"); return NULL; }
uint64_t plat_now_ns(void) { return 1; }
uint64_t renderer_present_count(void) { return 1; }
void menu_request_quit(void) { assert(!"unexpected quit"); }
bool menu_quit_requested(void) { return false; }
int main(void) {
    Cpu cpu;
    memset(&cpu, 0x5a, sizeof cpu);
    Cpu saved = cpu;
    rt_hook_features = ~0u;
    const uint64_t sites[] = {SITES};
    for (unsigned i = 0; i < sizeof sites / sizeof sites[0]; ++i) {
        assert(rt_hook(&cpu, sites[i]) == 0);
        assert(!memcmp(&cpu, &saved, sizeof cpu));
    }
}
'''.replace("SITES", ", ".join(f"0x{site:x}ull" for site in sites))
        with tempfile.TemporaryDirectory(prefix="sf2-original-prompts-") as directory:
            root = pathlib.Path(directory)
            (root / "test.c").write_text(harness)
            compile_harness(root / "test.c", root / "test")
            subprocess.run([str(root / "test")], check=True)

    def test_generated_gate_preserves_original_instruction(self):
        base = 0x7000000000
        words = (0x91000400, 0xD65F03C0)  # add x0, x0, #1; ret
        module = gen.Module.__new__(gen.Module)
        module.base = module.lo = base
        module.hi = base + 8
        module.words, module.cache = words, {}
        module.nso = types.SimpleNamespace(image=struct.pack("<II", *words))
        module.lifter = Lifter(base, "c")
        module.sym_names, module.eh_calls = {}, {}
        functions = []
        for name, metadata in (("optional", (words[0], (), "test", "RT_HOOK_REPLAY")),
                               ("shared", (words[0], (), "test", "(RT_HOOK_REPLAY | RT_HOOK_MOBILE_UI)")),
                               ("mandatory", (words[0], (), "test")), ("original", None)):
            with patch.object(gen, "HOOKS", {base: metadata} if metadata else {}):
                code = "\n".join(module.emit_function(base, base + 8, {base}, {}, {}))
                functions.append(code.replace(f"f_{base:x}", name))
        harness = r'''
#include <assert.h>
#include "cpu.h"
#include "replay.h"
#undef FN_ENTER
#define FN_ENTER(pc) ((void)0)
uint32_t rt_hook_features;
static int callbacks, handled;
int rt_hook(Cpu *c, uint64_t pc) {
    ++callbacks;
    if (handled) c->x[0] = 99;
    return handled;
}
''' + "\n".join(functions) + r'''
int main(void) {
    Cpu a = {0}, b = {0};
    a.x[0] = b.x[0] = 11;
    optional(&a); original(&b);
    assert(a.x[0] == 12 && !memcmp(&a, &b, sizeof a) && callbacks == 0);
    mandatory(&a);
    assert(a.x[0] == 13 && callbacks == 1); // compatibility hooks stay active
    rt_hook_features = RT_HOOK_REPLAY;
    optional(&a);
    assert(a.x[0] == 14 && callbacks == 2);
    handled = 1;
    optional(&a);
    assert(a.x[0] == 99 && callbacks == 3); // handled hook skips the original
    rt_hook_features = 0;
    optional(&a);
    assert(a.x[0] == 100 && callbacks == 3);
    shared(&a);
    assert(a.x[0] == 101 && callbacks == 3); // neither feature: original
    rt_hook_features = RT_HOOK_MOBILE_UI;
    shared(&a);
    assert(a.x[0] == 99 && callbacks == 4);
    rt_hook_features = RT_HOOK_REPLAY;
    shared(&a);
    assert(a.x[0] == 99 && callbacks == 5);
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix="sf2-hook-gate-") as directory:
            root = pathlib.Path(directory)
            (root / "test.c").write_text(harness)
            subprocess.run(["clang", "-O2", "-I", str(ROOT / "runtime"),
                            str(root / "test.c"), "-o", str(root / "test")], check=True)
            subprocess.run([str(root / "test")], check=True)

    def test_mobile_and_replay_sites_have_separate_optional_groups(self):
        self.assertEqual(patches.HOOKS[0x700011efb0][3], "RT_HOOK_MOBILE_UI")
        replay_sites = {site for site, hook in patches.HOOKS.items() if len(hook) == 4 and hook[3] in ("2u", "6u")}
        self.assertEqual(replay_sites, {
            0x7000da027c,
            0x7000ca7e80, 0x7000ca6df0, 0x7000c99e30, 0x7000c9dd50,
            0x7000ca7c00, 0x7001177d10, 0x7001177d20, 0x7001237b50,
            0x7001237bc0, 0x70010ed6a0, 0x70010f1450, 0x70010f1550,
            0x70010f1600, 0x70010f1710, 0x70010f17d0, 0x70010f18e0,
            0x70010f19a0, 0x70010f1ab0, 0x70010f1b80, 0x70010f1c50,
            0x70010f1d60, 0x70010f1e80, 0x70010f1f90, 0x70010f2de0,
            0x7000d961f0, 0x7000c9de18, 0x7000c9de40, 0x7000ff7af0,
            0x7000ff7c40,
        })
