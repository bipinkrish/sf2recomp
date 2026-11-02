"""Android-specific pointer, movie lifecycle and native boss-control styling."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_settings import compile_harness


class AndroidUITests(unittest.TestCase):
    def test_pointer_completion_and_boss_progress_control(self):
        source=r'''
#include <SDL3/SDL.h>
#define __ANDROID__ 1
#include "hle/hooks.c"
#include <assert.h>
static bool completed,watched;
static unsigned completions,sprites,percentages,enabled_updates;
static uint64_t mobile,actions,boss,fight,raid,original_raid,magic,missile,punch;
static unsigned char cloned_button[400],ring_image[256],icon_image[256];
static unsigned destroys,icon_tints,vertices,triangles;
static bool icon_ready,icon_awake,ring_visible;
static uint64_t assigned_input;

bool movie_playback_completed(void) { return completed; }
bool movie_watched_to_end(void) { return watched; }
void guest_call(Cpu *c,uint64_t pc) {
    switch(pc) {
    case 0x7000c92820ull:assert(c->x[1]==0);c->x[0]=assigned_input;return;
    case 0x70015bca30ull:++completions;ST8(c->x[0]+0x28,0);return;
    case 0x7000c7e300ull:c->x[0]=mobile;return;
    case 0x7000c91c70ull:c->x[0]=fight;return;
    case GUEST_STRING_ALLOC:c->x[0]=(uintptr_t)calloc(1,32+c->x[1]*2);return;
    case 0x700121e6b0ull:case 0x700121e720ull:return;
    case 0x700011d5e0ull:case 0x70001151d0ull:return;
    case 0x700010d820ull:assert(c->x[0]==(uintptr_t)ring_image && c->x[1]==3);return;
    case 0x700011d7d0ull:assert(c->x[0]==(uintptr_t)ring_image && c->x[1]==4);return;
    case 0x70015d5a50ull:c->x[0]=1003;return;
    case 0x700122a5f0ull:assert(c->x[0]==1001 && c->x[1]==1003);return;
    case 0x700011e210ull:c->x[0]=1000;return;
    case 0x70012331f0ull:assert(c->x[1]==1000);c->x[0]=1001;return;
    case 0x700011dbb0ull:c->x[0]=LD64(icon_image+0xb0)?999:1003;return;
    case 0x700011e430ull:assert(c->x[0]==(uintptr_t)icon_image);return;
    case 0x7000aed470ull:return;
    case 0x7000aedd60ull:++vertices;return;
    case 0x7000aedf00ull:++triangles;return;
    case 0x7000114f90ull:
        return;
    case 0x700123a6d0ull: {
        float dim=300;memcpy(&c->v[0],&dim,4);memcpy(&c->v[1],&dim,4);return;
    }
    case 0x70015d8be0ull:assert(c->x[0]==magic);c->x[0]=2000;return;
    case 0x700123a410ull: {
        float x=c->x[0]==magic?-100:c->x[0]==missile?-340:-40;
        float y=c->x[0]==magic?800:c->x[0]==missile?560:480;
        memcpy(&c->v[0],&x,4);memcpy(&c->v[1],&y,4);return;
    }
    case 0x700123a550ull: {
        float x,y;memcpy(&x,&c->v[0],4);memcpy(&y,&c->v[1],4);
        assert((c->x[0]==raid && x==-400 && y==880) || (c->x[0]==(uintptr_t)icon_image && !x && !y));return;
    }
    case 0x70012333a0ull:
        assert(c->x[1]==magic && c->x[2]==2000 && !c->x[3]);
        memcpy(cloned_button,(void *)(uintptr_t)magic,sizeof cloned_button);
        ST64(cloned_button+0xb8,1);ST64(cloned_button+0x128,(uintptr_t)icon_image);
        c->x[0]=raid;return;
    case 0x7001233760ull:
        assert(!c->x[0] && (c->x[1]==raid || c->x[1]==1001));++destroys;return;
    case 0x7000111a30ull:c->x[0]+=42;return;
    case 0x700010d790ull:++sprites;if(c->x[0]==(uintptr_t)icon_image) {ST64(icon_image+0xb0,c->x[1]);icon_ready=c->x[1]!=0;++icon_tints;}return;
    case 0x700011d870ull: {
        float value;memcpy(&value,&c->v[0],4);
        assert(c->x[0]==(uintptr_t)ring_image && (value==.5f || value==0 || value==1 || value==.25f));memcpy(ring_image+0xcc,&value,4);++percentages;return;
    }
    case 0x70005d8310ull:assert(c->x[0]==raid);++enabled_updates;return;
    case 0x7001226480ull:
        assert(c->x[0]==raid || c->x[0]==(uintptr_t)icon_image || c->x[0]==(uintptr_t)ring_image);
        if(c->x[0]==(uintptr_t)icon_image && c->x[1] && !icon_awake) {
            icon_awake=true;ST64(icon_image+0xb0,777); // ResolutionImage.Awake restores the serialized sprite
        }
        if(c->x[0]==(uintptr_t)ring_image)ring_visible=c->x[1]!=0;
        return;
    default:assert(!"unexpected guest call");
    }
}
int main(void) {
    unsigned char control[32]={0},input_data[32]={0};Cpu routing={0};
    assigned_input=(uintptr_t)input_data;
    for(unsigned type=0;type<=2;type+=2) {
        ST32(input_data+0x14,type);ST32(input_data+0x18,7);
        routing.x[0]=(uintptr_t)control;routing.x[19]=999;Cpu before=routing;
        assert(platform_ui_hook(&routing,0x7000c970b0ull)==1 && routing.x[0]==type);
        before.x[0]=type;assert(!memcmp(&routing,&before,sizeof routing));
        assert(LD32(control+0x14)==7);
    }
    assigned_input=0;routing.x[0]=(uintptr_t)control;
    assert(platform_ui_hook(&routing,0x7000c970b0ull)==1 && routing.x[0]==0);
    unsigned char controller[128]={0};Cpu cpu={0};cpu.x[0]=(uintptr_t)controller;
    ST8(controller+0x28,1);Cpu saved=cpu;
    assert(platform_ui_hook(&cpu,0x70015bc860ull)==0 && !completions);
    completed=true;assert(platform_ui_hook(&cpu,0x70015bc860ull)==1);
    assert(completions==1 && !LD8(controller+0x28) && !memcmp(&cpu,&saved,sizeof cpu));
    assert(platform_ui_hook(&cpu,0x70015bc860ull)==0 && completions==1);
    assert(platform_ui_hook(&cpu,0x70015bbf50ull)==1 && !cpu.x[0]);
    watched=true;assert(platform_ui_hook(&cpu,0x70015bbf50ull)==1 && cpu.x[0]==1);
    android_primary_touch=(MenuMouse){.x=123,.y=456,.held=1,.pressed=1};cpu.x[1]=0;
    assert(platform_ui_hook(&cpu,0x7001227dd0ull)==1 && cpu.x[0]);
    assert(platform_ui_hook(&cpu,0x7001227e40ull)==1 && cpu.x[0]);
    assert(platform_ui_hook(&cpu,0x7001227eb0ull)==1 && !cpu.x[0]);
    cpu.x[1]=1;assert(platform_ui_hook(&cpu,0x7001227dd0ull)==1 && !cpu.x[0]);
    assert(platform_ui_hook(&cpu,0x7001227f20ull)==1);
    float x,y;memcpy(&x,&cpu.v[0],4);memcpy(&y,&cpu.v[1],4);
    assert(x==123 && y==456 && !cpu.v[2]);
    unsigned char m[128]={0},a[128]={0},side[128]={0},b[160]={0};
    unsigned char rb[400]={0},mb[400]={0},miss[400]={0},pn[400]={0};
    mobile=(uintptr_t)m;actions=(uintptr_t)a;boss=(uintptr_t)b;
    raid=(uintptr_t)cloned_button;original_raid=(uintptr_t)rb;magic=(uintptr_t)mb;missile=(uintptr_t)miss;punch=(uintptr_t)pn;
    ST64(m+0x30,actions);ST64(controller+0x20,(uintptr_t)side);ST64(side+0x28,boss);
    ST64(a+0x58,original_raid);ST64(a+0x50,magic);ST64(a+0x48,missile);ST64(a+0x40,punch);
    ST64(rb+0xb8,4);ST64(rb+0x90,123);ST32(rb+0x114,13);
    ST64(mb+0xe0,555);ST32(mb+0x114,12);ST32(mb+0x44,0xabcdef);
    ST64(mb+0x120,(uintptr_t)ring_image);ST64(mb+0x128,7);ST64(b+0x38,6);
    unsigned char original_magic[400],original_raid_copy[400];
    memcpy(original_magic,mb,400);memcpy(original_raid_copy,rb,400);
    ST32(b+0x60,0x42480000);ST8(b+0x64,1);
    unsigned char before[sizeof b];memcpy(before,b,sizeof b);
    cpu=saved;assert(!mobile_boss_button(&cpu,(uintptr_t)controller) && !sprites);
    unsigned char f[192]={0},parameters[512]={0};fight=(uintptr_t)f;
    ST64(f+0xa0,(uintptr_t)parameters);
    assert(!mobile_boss_button(&cpu,(uintptr_t)controller) && !sprites);
    ST64(parameters+0x1e8,guest_string(&cpu,"LynxAbil_invisibility"));
    assert(mobile_boss_button(&cpu,(uintptr_t)controller));
    assert(sprites==1 && LD32(cloned_button+0x114)==14 && !memcmp(b,before,sizeof b));
    assert(LD64(cloned_button+0xe0)==555 && !LD64(cloned_button+0x90) && !LD64(cloned_button+0x128));
    assert(LD32(cloned_button+0x44)==0xabcdef && LD64(cloned_button+0x120)==(uintptr_t)ring_image);
    assert(!memcmp(original_magic,mb,400) && !memcmp(original_raid_copy,rb,400));
    assert(!memcmp(&cpu,&saved,sizeof cpu));
    cpu.x[0]=1;cpu.x[1]=9;Cpu mesh_saved=cpu;
    assert(platform_ui_hook(&cpu,0x700011efb0ull)==1);
    assert(vertices==320 && triangles==448 && !memcmp(&cpu,&mesh_saved,sizeof cpu));
    cpu=saved;
    mobile_boss_update(&cpu);assert(percentages==1 && enabled_updates==1 && !icon_ready);
    mobile_boss_update(&cpu);assert(percentages==1 && enabled_updates==1 && !icon_ready);
    assert(icon_awake && !LD64(icon_image+0xb0) && ring_visible);
    assert(LD32(ring_image+0xcc)==0x3f000000); // native 50 percent renders a half ring
    ST32(b+0x60,0);mobile_boss_update(&cpu);assert(percentages==2);
    assert(!ring_visible && !LD32(ring_image+0xcc));
    // Readiness, rather than a latched percentage image, owns the single icon.
    ST32(b+0x60,0x42c80000);ST8(b+0x80,1);mobile_boss_update(&cpu);assert(icon_ready && icon_tints==2);
    ST32(b+0x60,0x41c80000);ST8(b+0x80,0);mobile_boss_update(&cpu);assert(!icon_ready && icon_tints==3);
    ST8(b+0x64,0);mobile_boss_update(&cpu);assert(enabled_updates==3);
    cpu.x[0]=mobile;assert(platform_ui_hook(&cpu,0x7000c90720ull)==0);
    assert(destroys==2 && !mobile_boss_control);
    assert(!memcmp(original_magic,mb,400) && !memcmp(original_raid_copy,rb,400));

}
'''
        with tempfile.TemporaryDirectory(prefix="sf2-android-ui-") as temporary:
            root=Path(temporary)
            harness=root/"test.c";harness.write_text(source)
            compile_harness(harness,root/"test")
            subprocess.run([str(root/"test")],check=True)
