"""The hidden tutorial menu keeps its native listener at the visible header."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_settings import compile_harness

class AndroidNavigationTests(unittest.TestCase):
    def test_tutorial_target_tracks_header_and_preserves_cpu(self):
        source = r'''
#define __ANDROID__ 1
#include "hle/hooks.c"
#include <assert.h>
static unsigned char main_menu[320], navigation[160];
static unsigned moves, anchors, hidden;
void guest_call(Cpu *c,uint64_t pc) {
    switch(pc) {
    case 0x7000e27660ull:c->x[0]=(uintptr_t)main_menu;return;
    case 0x7001226320ull:assert(c->x[0]==10);c->x[0]=20;return;
    case 0x700121e6b0ull:assert(c->x[0]==30);c->x[0]=40;return;
    case GUEST_STRING_ALLOC:c->x[0]=(uintptr_t)calloc(1,64+c->x[1]*2);return;
    case 0x70008ea5c0ull:c->x[0]=50;return;
    case 0x700121e720ull:assert(c->x[0]==20);c->x[0]=10;return;
    case 0x700121e790ull:assert(c->x[0]==20 && c->x[1]==50);c->x[0]=60;return;
    case 0x7000af7fb0ull:assert(c->x[0]==60 && !(uint32_t)c->v[0]);++hidden;return;
    case 0x7000af8110ull:assert(c->x[0]==60 && !c->x[1]);return;
    case 0x70015d8ec0ull:assert(c->x[0]==20 && c->x[1]==40 && !c->x[2]);++moves;return;
    case 0x700123a050ull:case 0x700123a250ull:case 0x700123a9d0ull:
    case 0x700123a550ull:case 0x700123a7d0ull: {
        float x,y;memcpy(&x,&c->v[0],4);memcpy(&y,&c->v[1],4);
        float expected=pc==0x700123a250ull?1:pc==0x700123a9d0ull?.5f:0;
        assert(c->x[0]==20 && x==expected && y==expected);++anchors;return;
    }
    default:assert(!"unexpected managed call");
    }
}
int main(void) {
    Cpu cpu;memset(&cpu,0xa5,sizeof cpu);Cpu before=cpu;
    mobile_tutorial_menu_target(&cpu,10);
    assert(!moves && !memcmp(&cpu,&before,sizeof cpu));
    ST64(main_menu+0x108,(uintptr_t)navigation);ST64(navigation+0x48,30);
    for(unsigned i=0;i<2;++i) {
        mobile_tutorial_menu_target(&cpu,10);
        assert(moves==i+1 && hidden==i+1 && anchors==5*(i+1));
        assert(!memcmp(&cpu,&before,sizeof cpu));
    }
}
'''
        with tempfile.TemporaryDirectory(prefix="sf2-android-navigation-") as d:
            root=Path(d);(root/"test.c").write_text(source)
            compile_harness(root/"test.c",root/"test")
            subprocess.run([str(root/"test")],check=True,timeout=5)
