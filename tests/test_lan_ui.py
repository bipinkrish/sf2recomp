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
void *hle_alloc(uint64_t n) { return calloc(1,n); }
static uint64_t prototype,selection_table,selected_rect,main_section,submenu_clone;
static unsigned moved_player,confirmed_id;
static float pointer_y=270;
static bool dialog_blocks,source_destroyed;
static uint64_t shown_info,shown_dialog;static unsigned original_entries,closed,back_prompts;
void guest_call(Cpu *c,uint64_t pc) {
    switch(pc) {
    case 0x70005d9e60ull:return;
    case 0x700121e790ull:assert(!source_destroyed);c->x[0]=prototype;return;
    case 0x700121e6b0ull:case 0x700121e720ull:case 0x7001226480ull:case 0x70012339d0ull:return;
    case 0x70015d8be0ull:c->x[0]=1234;return;
    case 0x7001233760ull:return;
    case 0x70012333a0ull:
        if(main_section && c->x[1]==main_section) {
            submenu_clone=(uintptr_t)calloc(1,64);
            uint64_t list=(uintptr_t)calloc(1,32),items=(uintptr_t)calloc(1,72);
            ST64(submenu_clone+0x18,list);ST64(list+0x10,items);ST32(list+0x18,5);
            for(unsigned i=0;i<5;++i) {
                uint64_t button=(uintptr_t)calloc(1,128);ST64(items+0x20+i*8,button);
                ST64(button+0x28,(uintptr_t)calloc(1,512));
            }
            c->x[0]=submenu_clone;return;
        }
        assert(c->x[0]==0 && (c->x[1]==prototype || c->x[1]==lan_menu_template) && c->x[3]==0);
        c->x[0]=(uintptr_t)calloc(1,128);ST64(c->x[0]+0x28,(uintptr_t)calloc(1,512));return;
    case 0x700123a050ull:case 0x700123a250ull:case 0x700123a550ull:case 0x700123a7d0ull:return;
    case 0x7000e71230ull:ST64(c->x[0]+0x180,c->x[1]);return;
    case 0x7000e70080ull:return;
    case 0x7000e6c080ull:ST8(c->x[0]+0x70,c->x[1]);return;
    case GUEST_BEHAVIOUR_SET_ENABLED:assert(c->x[1]==0);return;
    case 0x7000e43da0ull:case 0x7000e46120ull: {
        moved_player=c->x[1];uint64_t selector=LD64(selection_table+0x18+moved_player*8);
        ST32(selector+0x38,LD32(selector+0x38)+(int32_t)c->x[2]);
        ST32(selector+0x3c,LD32(selector+0x3c)+(int32_t)c->x[3]);return; }
    case 0x7000d94dd0ull:case 0x7000d94e40ull:
        c->x[0]=(uintptr_t)calloc(1,32);ST32(c->x[0]+0x18,pc==0x7000d94dd0ull?7:9);return;
    case 0x7000e44df0ull:case 0x7000d98de0ull:
        assert(lan_native_call && c->x[3]==0);confirmed_id=c->x[2];return;
    case 0x7000cede90ull:
        c->x[0]=(uintptr_t)calloc(1,80);ST64(c->x[0]+0x40,dialog_blocks);return;
    case 0x700121ead0ull:assert(c->x[0]==789);c->x[0]=321;return;
    case 0x7000af7250ull:assert(c->x[0]==321);c->x[0]=123;return;
    case 0x70015d9e00ull:assert(c->x[0]==789);c->x[0]=32;return;
    case 0x70015da1f0ull:assert(c->x[0]==789);c->x[0]=900+c->x[1];return;
    case 0x7000d982c0ull:c->x[0]=1;return;
    case 0x7000af9300ull:
        assert(c->x[0]==0 && c->x[2]==123);bool inside=c->x[1]==selected_rect;
        { float x,y;memcpy(&x,&c->v[0],4);memcpy(&y,&c->v[1],4);assert(x==480 && y==pointer_y); }
        memset(c,0x5a,sizeof *c);c->x[0]=inside;return;
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
    case 0x7000e72180ull:assert(c->x[1]==80 || c->x[1]==55);return;
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
    cpu.x[0]=(uintptr_t)calloc(1,256);saved=cpu;
    assert(lan_ui_hook(&cpu,0x7000e6ed90ull)==1);
    assert(!memcmp(&cpu,&saved,sizeof cpu));
    label(0,"NORMAL");label(1,"LAN MULTIPLAYER");assert(!lan_gui_active() && !lan_enabled());
    click(&cpu,1);label(0,"HOST");label(1,"JOIN");
    SDL_Event back={0};back.type=SDL_EVENT_KEY_DOWN;back.key.scancode=g_bind_key[BIND_B];
    assert(lan_menu_handle_event(&back));cpu=saved;lan_ui_pump(&cpu);
    label(0,"NORMAL");assert(!lan_gui_active() && lan_menu_capture());
    click(&cpu,0);assert(original_entries==1 && closed>=2 && !lan_enabled() && back_prompts==3);
    // Reuse main-menu components and keep pointer hover, exit, click and
    // keyboard focus on the same selected choice without firing source listeners.
    prototype=(uintptr_t)calloc(1,128);lan_capture_menu_item(&cpu,prototype);
    lan_mode_dialog(&cpu);
    assert(lan_menu_template && !memcmp(&cpu,&saved,sizeof cpu));
    // The first error dialog can open after the source scene is gone.
    source_destroyed=true;lan_menu_prototype=0x3000000001ull;
    lan_error_dialog(&cpu);assert(lan_menu_buttons[1]);
    lan_mode_dialog(&cpu);
    assert(lan_menu_buttons[0] && lan_menu_buttons[1]);
    assert(LD8(lan_menu_buttons[0]+0x70)==1 && !LD8(lan_menu_buttons[1]+0x70));
    cpu=saved;cpu.x[0]=lan_menu_buttons[1];assert(lan_ui_hook(&cpu,0x7000e6fd20ull)==1);
    assert(lan_choice==1 && !LD8(lan_menu_buttons[0]+0x70) && LD8(lan_menu_buttons[1]+0x70));
    assert(lan_ui_hook(&cpu,0x7000e6fd30ull)==1 && !LD8(lan_menu_buttons[1]+0x70));
    uint64_t pointer=(uintptr_t)calloc(1,336);cpu.x[1]=pointer;ST32(pointer+0x12c,1);
    assert(lan_ui_hook(&cpu,0x7000e6fca0ull)==1 && !lan_action);
    ST32(pointer+0x12c,0);assert(lan_ui_hook(&cpu,0x7000e6fca0ull)==1 && lan_action==LAN_CHOICE_LAN);
    lan_action=0;lan_close_dialog(&cpu);lan_menu_prototype=0;
    // A click uses native movement + Confirm for only the owning player.
    uint64_t module=(uintptr_t)calloc(1,256),items=(uintptr_t)calloc(1,64);
    selection_table=(uintptr_t)calloc(1,64);ST64(module+0x20,selection_table);ST64(module+0x98,items);
    ST32(module+0x5c,8);ST64(items+0x18,32);
    for(unsigned i=0;i<2;++i)ST64(selection_table+0x18+i*8,(uintptr_t)calloc(1,80));
    cpu=saved;assert(sparring_select_cell(&cpu,module,0,1,26));assert(!memcmp(&cpu,&saved,sizeof cpu));
    uint64_t selector=LD64(selection_table+0x20);
    assert(moved_player==1 && confirmed_id==9 && LD32(selector+0x38)==2 && LD32(selector+0x3c)==3);
    moved_player=99;confirmed_id=0;
    assert(sparring_select_cell(&cpu,module,0,1,26));
    assert(moved_player==99 && confirmed_id==9); // second tap confirms without resetting selection
    assert(!sparring_select_cell(&cpu,module,0,1,32));ST8(module+0x59,1);
    assert(!sparring_select_cell(&cpu,module,0,1,0));
    selected_rect=456;MenuMouse mouse={.x=480,.y=270};
    assert(sparring_contains(&cpu,selected_rect,123,&mouse) && !memcmp(&cpu,&saved,sizeof cpu));
    sparring_module=module;sparring_kind=0;ST64(selection_table+0x28,789);selected_rect=926;
    mouse.pressed=mouse.released=1;sparring_mouse_frame(&cpu,&mouse);
    assert(moved_player==0 && confirmed_id==7 && !memcmp(&cpu,&saved,sizeof cpu));
    ST8(module+0x58,1);ST8(module+0x59,0);moved_player=99;sparring_mouse_frame(&cpu,&mouse);
    assert(moved_player==99 && confirmed_id==9); // shared mouse confirms player 2's already selected cell
    moved_player=99;dialog_blocks=true;sparring_mouse_frame(&cpu,&mouse);assert(moved_player==99);
    dialog_blocks=false;mouse.pressed=0;mouse.released=1;sparring_pressed=-1;
    sparring_mouse_frame(&cpu,&mouse);assert(moved_player==99); // release without a matching press
    // Exercise the actual frame dispatcher, not just the swipe helper. Held
    // motion and wheel frames have no pressed/released edges.
    sparring_view.table=selection_table;sparring_view.content=789;
    ST64(selection_table+0x30,selected_rect);
    mouse=(MenuMouse){.x=480,.y=270,.pressed=1,.held=1};
    sparring_mouse_frame(&cpu,&mouse);assert(sparring_view.pressed);
    mouse.pressed=0;mouse.y=pointer_y=310;
    sparring_mouse_frame(&cpu,&mouse);assert(sparring_view.first_row==1 && sparring_view.dragged);
    mouse.held=0;mouse.released=1;
    sparring_mouse_frame(&cpu,&mouse);assert(!sparring_view.pressed && moved_player==99);
    mouse.released=0;mouse.scroll_y=1;
    sparring_mouse_frame(&cpu,&mouse);assert(sparring_view.first_row==0);
    reset_sparring_roster();pointer_y=270;
    cpu.x[0]=module;
    assert(lan_ui_hook(&cpu,0x7000d985a0ull)==-1 && !sparring_module);
    cpu.x[1]=8;assert(lan_ui_hook(&cpu,0x7000f48ef0ull)==-1);
    assert(input_menu_navigation_active() && input_menu_back_binding()==BIND_PLUS);
    cpu.x[1]=7;assert(lan_ui_hook(&cpu,0x7000f48ef0ull)==-1);
    assert(input_menu_navigation_active() && input_menu_back_binding()==BIND_B);
    cpu.x[1]=5;assert(lan_ui_hook(&cpu,0x7000f48ef0ull)==-1);
    assert(!input_menu_navigation_active());
    // Mode and role replace choices inside the main-menu section. They
    // allocate no modal dialog and leave all original scene/list data intact.
    cpu=saved;lan_menu_scene=(uintptr_t)calloc(1,256);
    uint64_t sections=(uintptr_t)calloc(1,32),section_items=(uintptr_t)calloc(1,40);
    main_section=(uintptr_t)calloc(1,64);
    ST64(lan_menu_scene+0x88,sections);ST64(sections+0x10,section_items);
    ST32(sections+0x18,1);ST64(section_items+0x20,main_section);
    uint64_t previous_dialog=shown_dialog;
    lan_mode_dialog(&cpu);
    assert(!memcmp(&cpu,&saved,sizeof cpu) && lan_menu_root==submenu_clone);
    assert(!lan_dialog && shown_dialog==previous_dialog && lan_menu_capture());
    assert(lan_choice_count==3 && lan_commands[2]==LAN_CHOICE_BACK);
    char text[64];assert(dev_read_string(LD64(LD64(lan_menu_buttons[0]+0x28)+0x180),text,sizeof text));
    assert(!strcmp(text,"NORMAL"));
    lan_action=LAN_CHOICE_LAN;lan_ui_pump(&cpu);
    assert(!lan_dialog && lan_phase==LAN_UI_ROLE && shown_dialog==previous_dialog);
    assert(dev_read_string(LD64(LD64(lan_menu_buttons[0]+0x28)+0x180),text,sizeof text));
    assert(!strcmp(text,"HOST"));
    lan_action=LAN_CHOICE_BACK;lan_ui_pump(&cpu);assert(lan_phase==LAN_UI_MODE);
    lan_action=LAN_CHOICE_BACK;lan_ui_pump(&cpu);
    assert(!lan_menu_capture() && !lan_menu_root && !lan_main_root && lan_phase==LAN_UI_NONE);
    assert(LD64(section_items+0x20)==main_section && LD32(sections+0x18)==1);
    // Physical device count must not bypass the remote player's readiness.
    for(unsigned i=0;i<2;++i) {
        uint64_t site=i?0x7000d98b48ull:0x7000d98aa8ull;
        cpu=saved;cpu.x[0]=1;Cpu before=cpu;
        assert(lan_ui_hook(&cpu,site)==-1 && !memcmp(&cpu,&before,sizeof cpu));
        assert(lan_gui_host(0));
        assert(lan_ui_hook(&cpu,site)==0 && cpu.x[0]==2);
        cpu.x[0]=before.x[0];assert(!memcmp(&cpu,&before,sizeof cpu));
        lan_gui_leave();
    }
    g_settings.allow_sparring_lan_multiplayer=false;
    assert(lan_ui_hook(&cpu,0x7000e6ed90ull)==-1);
}
"""
class NativeLanUiTests(unittest.TestCase):
    def test_normal_lan_host_join_back_and_disabled_original_path(self):
        with tempfile.TemporaryDirectory(prefix='sf2-native-lan-ui-') as d:
            work=Path(d);(work/'test.c').write_text(HARNESS);compile_harness(work/'test.c',work/'test')
            subprocess.run([str(work/'test')],check=True,timeout=5)
