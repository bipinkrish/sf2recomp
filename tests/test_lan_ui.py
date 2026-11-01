"""The in-game dialog flow uses native managed widgets and preserves guest CPU."""
from pathlib import Path
import subprocess,tempfile,unittest
from test_settings import compile_harness
HARNESS=r"""
#include <assert.h>
#include <SDL3/SDL.h>
#include "hle/hooks.c"
bool menu_is_open(void) { return false; }
bool input_any_gamepad_connected(void) { return false; }
int input_pad_family(void) { return 1; }
int input_family_of_type(int type) { return 1; }
bool menu_quit_requested(void) { return false; }
void menu_request_quit(void) {}
uint64_t plat_now_ns(void) { return 1000000; }
uint64_t renderer_present_count(void) { return 1; }
bool rt_probe_u64(uint64_t a,uint64_t *v) { return false; }
bool nvn_texture_region_color(const NvnTexture*t,uint32_t x,uint32_t y,uint32_t w,uint32_t h,float rgb[3]) { return false; }
void *hle_alloc(uint64_t n) { return calloc(1,n); }
static uint64_t shown_info,shown_dialog;static unsigned original_entries,closed,back_prompts;
void guest_call(Cpu *c,uint64_t pc) {
    switch(pc) {
    case 0x70005d9e60ull:return;
    case GUEST_STRING_ALLOC:c->x[0]=(uintptr_t)calloc(1,32+c->x[1]*2);return;
    case 0x70008ea5c0ull:c->x[0]=c->x[1];return;
    case 0x700039cb10ull:c->x[0]=(uintptr_t)calloc(1,256);return;
    case 0x70006e6740ull:c->x[0]=(uintptr_t)calloc(1,32+c->x[2]*8);return;
    case 0x7000cf1360ull:
        assert(c->x[1]==15);shown_info=c->x[2];shown_dialog=(uintptr_t)calloc(1,256);
        ST64(shown_dialog+0x50,(uintptr_t)calloc(1,512));
        { uint64_t supplied=LD64(shown_info+0x10);unsigned count=LD32(supplied+0x18);
          uint64_t rows=(uintptr_t)calloc(1,64),items=(uintptr_t)calloc(1,32+count*8);
          ST64(rows+0x10,items);ST32(rows+0x18,count);ST64(shown_dialog+0xe8,rows);
          for(unsigned i=0;i<count;++i) { uint64_t row=(uintptr_t)calloc(1,64),label=(uintptr_t)calloc(1,512);
              ST64(row+0x18,label);ST64(items+0x20+i*8,row); } }
        c->x[0]=shown_dialog;return;
    case 0x7000e73b90ull:assert(c->x[1]==1);ST32(c->x[0]+0x1cc,1);return;
    case 0x7000e72180ull:assert(c->x[1]==80);return;
    case 0x70011cb2f0ull:assert(c->x[1]==1);return;
    case 0x70011cba70ull:assert(c->x[1]==0);return;
    case 0x70011cbb80ull:
        { char text[96];assert(dev_read_string(c->x[1],text,sizeof text));
          assert(!strcmp(text,"BACK [J]"));++back_prompts; }return;
    case 0x7000cee250ull:assert(c->x[0]==shown_dialog);++closed;return;
    case 0x7000e6ed90ull:++original_entries;return;
    default:assert(!"unexpected managed call");
    }
}
static void label(unsigned index,const char *expected) {
    uint64_t list=LD64(shown_info+0x10),array=LD64(list+0x10);
    assert(LD32(list+0x18)==2);
    uint64_t line=LD64(array+0x20+index*8),string=LD64(line+0x10);
    assert(string && LD64(line+0x18));
    uint64_t rows=LD64(shown_dialog+0xe8),items=LD64(rows+0x10),row=LD64(items+0x20+index*8);
    assert(LD32(LD64(row+0x18)+0x1cc)==1);
    assert(LD32(string+0x10)==strlen(expected));
    for(unsigned i=0;i<strlen(expected);++i)assert(LD16(string+0x14+2*i)==expected[i]);
}
static void click(Cpu *c,unsigned index) {
    uint64_t list=LD64(shown_info+0x10),array=LD64(list+0x10),line=LD64(array+0x20+index*8);
    unsigned char button[64]={0};ST64((uintptr_t)button+0x30,LD64(line+0x20));
    Cpu saved=*c;c->x[0]=(uintptr_t)button;
    assert(lan_ui_hook(c,0x7000dc7550ull)==1);
    *c=saved;lan_ui_pump(c);assert(!memcmp(c,&saved,sizeof *c));
}
int main(void) {
    uint64_t array=(uintptr_t)calloc(1,64),entries[3];
    const char *names[]={"fighter", "random", "arena"};ST64(array+0x18,3);
    for(unsigned i=0;i<3;++i) {
        entries[i]=(uintptr_t)calloc(1,32);ST64(array+0x20+i*8,entries[i]);
        Cpu c={0};ST64(entries[i]+0x18,guest_string(&c,names[i]));
    }
    // The placeholder can be in the middle, last, or first; seeds must never
    // resolve back to Random, and every concrete entry remains selectable.
    for(unsigned random=0;random<3;++random) {
        for(unsigned i=0;i<3;++i) { Cpu c={0};ST64(entries[i]+0x18,guest_string(&c,i==random?"random":"fighter")); }
        unsigned seen=0;uint64_t state=123;
        for(unsigned draw=0;draw<100;++draw) {
            unsigned index=lan_concrete_index(array,&state);assert(index<3 && index!=random);seen|=1u<<index;
        }
        assert(seen==(7u&~(1u<<random)));
    }
    for(unsigned i=0;i<3;++i) { Cpu c={0};ST64(entries[i]+0x18,guest_string(&c,"random")); }
    uint64_t empty_state=123;
    assert(lan_concrete_index(array,&empty_state)==UINT32_MAX && empty_state==123);
    // Consecutive choices from the same roster must allow all ordered pairs,
    // including equal fighters. The old seed+constant formula cannot do this.
    for(unsigned i=0;i<3;++i) { Cpu c={0};ST64(entries[i]+0x18,guest_string(&c,i==1?"random":"fighter")); }
    unsigned pairs=0;uint64_t selection=123;
    for(unsigned draw=0;draw<1000;++draw) {
        unsigned first=lan_concrete_index(array,&selection),second=lan_concrete_index(array,&selection);
        pairs|=1u<<((first==2)*2+(second==2));
    }
    assert(pairs==15);
    Cpu cpu,saved;memset(&cpu,0xa5,sizeof cpu);saved=cpu;
    assert(g_settings.allow_sparring_lan_multiplayer);
    assert(lan_ui_hook(&cpu,0x7000e6ed90ull)==1);
    assert(!memcmp(&cpu,&saved,sizeof cpu));
    label(0,"NORMAL");label(1,"LAN MULTIPLAYER");assert(!lan_gui_active() && !lan_enabled());
    click(&cpu,1);label(0,"HOST");label(1,"JOIN");
    SDL_Event back={0};back.type=SDL_EVENT_KEY_DOWN;back.key.scancode=g_bind_key[BIND_B];
    assert(lan_menu_handle_event(&back));cpu=saved;lan_ui_pump(&cpu);
    label(0,"NORMAL");assert(!lan_gui_active() && lan_menu_capture());
    click(&cpu,0);assert(original_entries==1 && closed>=2 && !lan_enabled() && back_prompts==3);
    g_settings.allow_sparring_lan_multiplayer=false;
    assert(lan_ui_hook(&cpu,0x7000e6ed90ull)==-1);
}
"""
class NativeLanUiTests(unittest.TestCase):
    def test_normal_lan_host_join_back_and_disabled_original_path(self):
        with tempfile.TemporaryDirectory(prefix='sf2-native-lan-ui-') as d:
            work=Path(d);(work/'test.c').write_text(HARNESS);compile_harness(work/'test.c',work/'test')
            subprocess.run([str(work/'test')],check=True,timeout=5)
