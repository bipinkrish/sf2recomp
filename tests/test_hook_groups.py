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


class HookGroupTests(unittest.TestCase):
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
        for name, metadata in (("optional", (words[0], (), "test", "RT_HOOK_KEYBOARD_PROMPTS")),
                               ("mandatory", (words[0], (), "test")), ("original", None)):
            with patch.object(gen, "HOOKS", {base: metadata} if metadata else {}):
                code = "\n".join(module.emit_function(base, base + 8, {base}, {}, {}))
                functions.append(code.replace(f"f_{base:x}", name))
        harness = r'''
#include <assert.h>
#include "cpu.h"
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
    rt_hook_features = RT_HOOK_KEYBOARD_PROMPTS;
    optional(&a);
    assert(a.x[0] == 14 && callbacks == 2);
    handled = 1;
    optional(&a);
    assert(a.x[0] == 99 && callbacks == 3); // handled hook skips the original
    rt_hook_features = 0;
    optional(&a);
    assert(a.x[0] == 100 && callbacks == 3);
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix="sf2-hook-gate-") as directory:
            root = pathlib.Path(directory)
            (root / "test.c").write_text(harness)
            subprocess.run(["clang", "-O2", "-I", str(ROOT / "runtime"),
                            str(root / "test.c"), "-o", str(root / "test")], check=True)
            subprocess.run([str(root / "test")], check=True)

    def test_only_keyboard_sites_are_optional(self):
        optional = {site for site, hook in patches.HOOKS.items() if len(hook) == 4}
        self.assertEqual(optional, {0x700011efb0, 0x7000114f90, 0x7000cd7130, 0x7000cdb690,
                                   0x7000ce0dd0, 0x7000d9aec0, 0x7000ce0f40, 0x7000ce0de0,
                                   0x7000ce10a0, 0x7000f9a060})
        self.assertTrue(all(patches.HOOKS[site][3] == "RT_HOOK_KEYBOARD_PROMPTS" for site in optional))
