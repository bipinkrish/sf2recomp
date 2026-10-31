"""LSDA actions must distinguish cleanup, typed catches and catch-all entries."""
import pathlib
import struct
import subprocess
import tempfile
import sys
import types
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/recompr'))
import analyze
from gen import emit_eh_table
from nso import R_AARCH64_GLOB_DAT

BASE = 0x7000000000


def fixture(actions, types_=(0x100, 0, 0x180), encoding=3):
    # One call site: [function+4, function+12), landing pad function+16.
    records = b''.join(bytes((filt & 127, 1 if i + 1 < len(actions) else 0))
                       for i, filt in enumerate(actions))
    table = b''.join(struct.pack('<I', value) for value in reversed(types_))
    tail = bytes((1, 4, 4, 8, 16, 1)) + records + table
    lsda = bytes((255, encoding, len(tail))) + tail
    image = bytearray(512)
    image[16:16 + len(lsda)] = lsda
    return types.SimpleNamespace(image=image, size=len(image), relocs=[], syms=[], eh_start=0)


class GuestEHTests(unittest.TestCase):
    def handlers(self, n):
        sites = analyze._lsda_handlers(n, 16, 64, BASE)
        if sites:
            self.assertEqual(sites[0][:3], (BASE + 68, BASE + 76, BASE + 80))
            return sites[0][3]
        return []

    def test_cleanup_is_not_catch_all(self):
        self.assertEqual(self.handlers(fixture([0])), [])
        self.assertEqual(self.handlers(fixture([0, 3])), [(BASE + 0x180, 3)])

    def test_selectors_are_type_filters_and_preserve_action_order(self):
        self.assertEqual(self.handlers(fixture([3, 1, 2])),
                         [(BASE + 0x180, 3), (BASE + 0x100, 1), (0, 2)])

    def test_relative_null_type_is_catch_all(self):
        self.assertEqual(self.handlers(fixture([2], encoding=0x13)), [(0, 2)])

    def test_indirect_null_slot_is_catch_all_but_unresolved_symbol_is_not(self):
        n = fixture([1], types_=(0x100,), encoding=0x83)
        self.assertEqual(self.handlers(n), [(0, 1)])
        n.relocs = [(0x100, R_AARCH64_GLOB_DAT, 0, 0)]
        n.syms = [('external_type', 0, 0, 0, 0)]
        del n._eh_relocs
        self.assertEqual(self.handlers(n), [])

    def test_cleanup_without_type_table_has_no_handler(self):
        n = fixture([0])
        n.image[16:24] = bytes((255, 255, 1, 4, 4, 8, 16, 0))
        self.assertEqual(self.handlers(n), [])

    def test_aligned_pointer_and_invalid_type_offset(self):
        image = bytearray(40)
        struct.pack_into('<Q', image, 24, 0x1234)
        self.assertEqual(analyze._encoded(image, 17, 0x50), (0x1234, 32, False))
        self.assertEqual(self.handlers(fixture([63])), [])

    def test_generated_lookup_and_native_unwind(self):
        table = emit_eh_table('main', [
            (0x100, 0x104, 0x200, [(0xabc, 3)]),
            (0x300, 0x304, 0x400, [(0, 7), (0xabc, 1)]),
        ])
        harness = r'''
#include <assert.h>
#include "rt.h"
bool sdk_eh_lookup(uint64_t s, uint64_t t, uint64_t *p, uint64_t *v) { return false; }
void rt_dispatch(Cpu *c) { assert(0); }
static void throw_leaf(Cpu *c) {
    c->x[19] = 999;
    assert(rt_guest_raise(0x1234, 0xabc));
    assert(0);
}
static void nested(Cpu *c) {
    // An inner site without a matching handler must propagate to its outer caller.
    rt_guest_try_call(c, 0x500, 0x600, throw_leaf);
    assert(0);
}
int main(void) {
    uint64_t pad, selector;
    assert(main_eh_lookup(0x300, 0xabc, &pad, &selector));
    assert(pad == 0x400 && selector == 7); // first catch-all wins
    assert(!main_eh_lookup(0x104, 0xabc, &pad, &selector));
    assert(!main_eh_lookup(0x100, 0xdef, &pad, &selector));
    Cpu c = {0}; c.sp = 0x10000; c.x[19] = 42;
    assert(rt_guest_try_call(&c, 0x100, 0x600, nested));
    assert(c.pc == 0x200 && c.x[0] == 0x1234 && c.x[1] == 3);
    assert(c.sp == 0x10000 && c.x[19] == 42 && c.x[30] == 0x104);
    assert(!rt_guest_raise(0x1234, 0xabc)); // no stale host frames remain
    rt_guest_eh_reset();
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix='sf2-eh-test-') as directory:
            root = pathlib.Path(directory)
            (root / 'table.c').write_text(table)
            (root / 'test.c').write_text(harness)
            subprocess.run(['clang', '-O2', '-mcx16', '-ffunction-sections', '-fdata-sections',
                            '-I', str(ROOT / 'runtime'), str(root / 'table.c'),
                            str(ROOT / 'runtime/exception.c'), str(root / 'test.c'),
                            '-Wl,--gc-sections', '-pthread', '-o', str(root / 'test')], check=True)
            subprocess.run([str(root / 'test')], check=True, timeout=5)


if __name__ == '__main__':
    unittest.main()
