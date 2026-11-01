"""Menu selection draws advance independently and sample eligible entries evenly."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class LanSelectionRandomTests(unittest.TestCase):
    def test_vectors_rejection_and_separate_character_arena_draws(self):
        source = r'''
#include <assert.h>
#include "lan_random.h"
int main(void) {
    uint64_t state=0;
    const uint64_t expected[]={UINT64_C(0xe220a8397b1dcdaf),UINT64_C(0x6e789e6aa1b965f4),UINT64_C(0x06c45d188009454f)};
    for(unsigned i=0;i<3;++i)assert(lan_selection_next(&state)==expected[i]);
    state=123;assert(lan_selection_bounded(&state,0)==UINT32_MAX && state==123);
    // Force a zero output: for count=3 it belongs to the rejected remainder.
    state=-UINT64_C(0x9e3779b97f4a7c15);
    assert(lan_selection_bounded(&state,3)==expected[0]%3);
    assert(state==UINT64_C(0x9e3779b97f4a7c15));
    state=-UINT64_C(0x9e3779b97f4a7c15);
    assert(lan_selection_bounded(&state,1)==0 && state==0);
    // Fixed-seed regression, not a general statistical certification. Every
    // ordered fighter pair/arena triple, including mirror matches, can occur.
    unsigned triples[8][8][5]={0};state=12345;
    for(unsigned i=0;i<32000;++i) {
        unsigned p1=lan_selection_bounded(&state,8),p2=lan_selection_bounded(&state,8),arena=lan_selection_bounded(&state,5);
        assert(p1<8 && p2<8 && arena<5);++triples[p1][p2][arena];
    }
    for(unsigned a=0;a<8;++a)for(unsigned b=0;b<8;++b)for(unsigned c=0;c<5;++c)
        assert(triples[a][b][c]>40 && triples[a][b][c]<165);
}
'''
        with tempfile.TemporaryDirectory(prefix="sf2-lan-selection-") as folder:
            root = Path(folder)
            (root / "test.c").write_text(source)
            subprocess.run(["clang", "-O2", "-I", str(ROOT / "runtime"), str(root / "test.c"), "-o", str(root / "test")], check=True)
            subprocess.run([str(root / "test")], check=True)
