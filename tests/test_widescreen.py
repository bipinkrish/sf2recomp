"""Opt-in native screen dimensions, backing storage, presentation and video fit."""
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_settings import compile_harness


class WidescreenTests(unittest.TestCase):
    def test_phone_aspect_uses_even_guest_width_and_safe_fallback(self):
        source = r"""
#include <assert.h>
#include "settings.h"
int main(void) {
    g_settings.aspect=ASPECT_NATIVE;
    assert(settings_custom_aspect() && settings_screen_width(1080)==1920);
    g_settings.native_width=2772;g_settings.native_height=1280;
    assert(settings_screen_width(1080)==2338 && settings_screen_width(720)==1560);
    g_settings.native_width=2424;g_settings.native_height=1080;
    assert(settings_screen_width(1080)==2424 && settings_screen_width(720)==1616);
    g_settings.native_height=0;assert(settings_screen_width(1080)==1920);
}
"""
        with tempfile.TemporaryDirectory(prefix="sf2-phone-aspect-") as directory:
            root=Path(directory);(root/"test.c").write_text(source)
            compile_harness(root/"test.c",root/"test")
            subprocess.run([str(root/"test")],check=True)

    def test_combat_framing_preserves_native_units_and_other_fight_modes(self):
        source = r'''
#include <assert.h>
#include "hle.h"
#include "settings.h"
#include "hle/widescreen_hooks.inc"
static unsigned height, calls;
static uint64_t current_fight;
void guest_call(Cpu *c, uint64_t pc) {
    assert(c->x[0]==0 && c->x[1]==0);
    assert(pc==0x70000ad270ull || pc==0x7000c91c70ull);
    memset(c,0x17,sizeof *c);
    c->x[0]=pc==0x70000ad270ull ? height : current_fight;++calls;
}
int main(void) {
    Cpu c, before;
    memset(&c,0xa5,sizeof c);before=c; // native aspect must not dereference anything
    g_settings.aspect=ASPECT_ORIGINAL;
    assert(widescreen_hook(&c,0x7000eabf0cull)==0);
    assert(!memcmp(&c,&before,sizeof c) && !calls);
    unsigned char fight[0x80]={0},list[0x20]={0},render[0xb0]={0};
    current_fight=(uintptr_t)fight;
    ST64(fight+0x30,(uintptr_t)list);ST32(list+0x18,18);
    const int aspects[]={ASPECT_16_10,ASPECT_21_9,ASPECT_32_9};
    // Different native pixel heights and authored location heights: division
    // must have the same rounding as native Render.Init at the original aspect.
    const unsigned heights[]={720,1080};
    const float authored[]={512,800,1080,1234.5f};
    for(unsigned h=0;h<2;++h)for(unsigned a=0;a<3;++a)for(unsigned l=0;l<4;++l) {
        height=heights[h];g_settings.aspect=aspects[a];
        float scale=(float)height/authored[l];
        memcpy(render+0x78,&scale,4);
        float visible_width=9876.5f;memcpy(render+0x84,&visible_width,4);
        unsigned char unchanged[sizeof render];memcpy(unchanged,render,sizeof render);
        memset(&c,0xa5,sizeof c);c.x[19]=(uintptr_t)render;before=c;
        float native=(float)(height==720?1280:1920)/scale;
        uint32_t bits;memcpy(&bits,&native,4);before.v[0]=bits;
        assert(widescreen_hook(&c,0x7000eabf0cull)==0);
        assert(!memcmp(&c,&before,sizeof c));
        assert(!memcmp(render,unchanged,sizeof render));
    }
    // Main's end-of-init zoom fix must not rescale the PvP reference again.
    unsigned char location[0x80]={0}, layers[0x20]={0};
    ST64(render+0xa0,(uintptr_t)location);ST64(location+0x70,(uintptr_t)layers);
    float width=600,minimum=.8f;
    memcpy(render+0x84,&width,4);memcpy(render+0x88,&minimum,4);
    assert(widescreen_hook(&c,0x7000eabfe0ull)==0);
    float after;memcpy(&after,render+0x84,4);assert(after==width);
    memcpy(&after,render+0x88,4);assert(after==minimum);
    assert(calls==48);
    ST32(list+0x18,17);before=c;
    assert(widescreen_hook(&c,0x7000eabf0cull)==0);
    assert(!memcmp(&c,&before,sizeof c) && calls==49);
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix="sf2-combat-width-") as directory:
            root = Path(directory)
            (root / 'test.c').write_text(source)
            compile_harness(root / 'test.c', root / 'test')
            subprocess.run([str(root / 'test')], check=True)

    def test_background_cover_preserves_fighters_and_caller_state(self):
        source = r'''
#include <assert.h>
#include <math.h>
#include "hle.h"
#include "settings.h"
#include "hle/widescreen_hooks.inc"

static unsigned calls, floor_calls;
static float floor_expected=176;
static uint64_t scenery[2];
void guest_call(Cpu *c, uint64_t pc) {
    if (pc==0x7001033f40ull) {
        assert(c->x[0]==scenery[0] || c->x[0]==scenery[1]);
        assert(c->x[1]==0);
        float y; memcpy(&y,&c->v[0],4); assert(y==floor_expected);
        floor_calls++;
        memset(c,0x17,sizeof *c);
        return;
    }
    assert(pc==0x7001033e70ull && c->x[1]==0);
    assert(c->x[0]==scenery[calls%2]);
    float scale;
    memcpy(&scale,&c->v[0],sizeof scale);
    assert(scale==1); // absolute authored scale, even on repeated initialization
    Cpu expected=*c;
    scale=2; memcpy(&expected.v[0],&scale,sizeof scale);
    assert(widescreen_hook(c,pc)==0 && !memcmp(c,&expected,sizeof *c));
    calls++;
    memset(c,0x17,sizeof *c); // guest methods may overwrite volatile CPU state
}

int main(void) {
    Cpu c, before;
    const int unchanged[]={ASPECT_ORIGINAL,ASPECT_16_10};
    for (unsigned i=0;i<sizeof unchanged/sizeof *unchanged;i++) {
        g_settings.aspect=unchanged[i];
        memset(&c,0xa5,sizeof c); before=c; // deliberately invalid pointers
        assert(widescreen_hook(&c,0x7000eabfe0ull)==0 && !memcmp(&c,&before,sizeof c));
        assert(widescreen_hook(&c,0x7001033e70ull)==0 && !memcmp(&c,&before,sizeof c));
    }
    g_settings.aspect=ASPECT_21_9;
    memset(&c,0xa5,sizeof c); before=c;
    assert(widescreen_hook(&c,0x7000ead4c0ull)==0 && !memcmp(&c,&before,sizeof c));
    g_settings.aspect=ASPECT_32_9;
    unsigned char render[0xb0]={0}, location[0x80]={0}, list[0x20]={0}, items[0x40]={0};
    unsigned char layers[4][0x50]={{0}};
    const unsigned types[]={1,2,3,0}; // backdrop, fighters, static scenery, unknown
    for (int i=0;i<4;i++) {
        ST32(layers[i]+0x34,types[i]);
        ST8(layers[i]+0x30,1);
        ST64(items+0x20+i*8,(uintptr_t)layers[i]);
    }
    scenery[0]=(uintptr_t)layers[0]; scenery[1]=(uintptr_t)layers[2];
    float height=512, margin=80;
    memcpy(location+0x44,&height,4); memcpy(location+0x34,&margin,4);
    ST64(render+0xa0,(uintptr_t)location); ST64(location+0x70,(uintptr_t)list);
    ST64(list+0x10,(uintptr_t)items); ST32(list+0x18,4);
    memset(&c,0xa5,sizeof c); c.x[19]=(uintptr_t)render; before=c;
    for (int i=0;i<2;i++) {
        float width=1200, minimum=1.6f;
        memcpy(render+0x84,&width,4); memcpy(render+0x88,&minimum,4);
        assert(widescreen_hook(&c,0x7000eabfe0ull)==0);
        assert(!memcmp(&c,&before,sizeof c));
        assert(calls==2*(i+1) && floor_calls==2*(i+1));
        memcpy(&width,render+0x84,4); memcpy(&minimum,render+0x88,4);
        assert(width==600 && minimum==.8f);
    }
    // 21:9 keeps the same vertical fight framing without scenery enlargement.
    g_settings.aspect=ASPECT_21_9;
    float width=787.5f, minimum=1.05f;
    memcpy(render+0x84,&width,4); memcpy(render+0x88,&minimum,4);
    assert(widescreen_hook(&c,0x7000eabfe0ull)==0);
    assert(!memcmp(&c,&before,sizeof c) && calls==4);
    memcpy(&width,render+0x84,4); memcpy(&minimum,render+0x88,4);
    assert(fabsf(width-600)<.001f && fabsf(minimum-.8f)<.00001f);
    g_settings.aspect=ASPECT_32_9;
    // Camera scale updates cover scenery only and preserve every other register.
    for (int i=0;i<4;i++) {
        memset(&c,0xa5,sizeof c); c.x[0]=(uintptr_t)layers[i];
        float input=.75f, expected=(types[i]==1 || types[i]==3)?1.5f:.75f;
        memcpy(&c.v[0],&input,sizeof input); before=c;
        memcpy(&before.v[0],&expected,sizeof expected);
        assert(widescreen_hook(&c,0x7001033e70ull)==0);
        assert(!memcmp(&c,&before,sizeof c));
    }
    // Scenery remains grounded as the original fight camera changes zoom.
    unsigned previous=floor_calls;
    for (int i=0;i<4;i++) {
        memset(&c,0xa5,sizeof c); c.x[19]=(uintptr_t)render;
        c.x[0]=(uintptr_t)layers[i];
        float scale=.75f; memcpy(&c.v[0],&scale,4); before=c;
        floor_expected=132;
        assert(widescreen_hook(&c,0x7000ead4c0ull)==0);
        assert(!memcmp(&c,&before,sizeof c));
    }
    assert(floor_calls==previous+2); // fighters and unknown layer are untouched
    ST8(layers[0]+0x30,0); c.x[0]=(uintptr_t)layers[0]; before=c;
    assert(widescreen_hook(&c,0x7000ead4c0ull)==0);
    assert(floor_calls==previous+2 && !memcmp(&c,&before,sizeof c));
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix="sf2-background-cover-") as directory:
            root = Path(directory)
            (root / "test.c").write_text(source)
            compile_harness(root / "test.c", root / "test")
            subprocess.run([str(root / "test")], check=True)

    def test_config_native_interfaces_and_aspect_preservation(self):
        source = r'''
#include <assert.h>
#include <math.h>
#include "hle.h"
#include "settings.h"
void guest_call(Cpu *c, uint64_t pc) { abort(); }
#include "hle/widescreen_hooks.inc"
#include "nvn/output_scale.c"

static void close_to(double a, double b) { assert(fabs(a-b) < 1e-6); }

int main(int argc, char **argv) {
    assert(argc == 7 && g_settings.aspect == ASPECT_ORIGINAL);
    const unsigned native_width = atoi(argv[5]), screen_width = atoi(argv[6]);
    const uint64_t sites[] = {0x70019016c0ull, 0x700190160cull, 0x700191177cull,
        0x7001911810ull, 0x70019119ccull, 0x70015fbb24ull, 0x70015fbcf0ull,
        0x7000eabfe0ull, 0x7001033e70ull, 0x7000ead4c0ull};
    Cpu c, before;
    memset(&c, 0xa5, sizeof c); before = c;
    // Disabled hooks must not even dereference the intentionally invalid pointers.
    for (size_t i=0; i<sizeof sites/sizeof *sites; ++i) {
        assert(widescreen_hook(&c, sites[i]) == 0);
        assert(!memcmp(&c, &before, sizeof c));
    }
    assert(widescreen_hook(&c, 0x70015c8948ull)==0 && !memcmp(&c,&before,sizeof c));
    assert(widescreen_hook(&c, 0) == -1);
    assert(settings_load_file(argv[1]) && settings_custom_aspect());
    int selected = g_settings.aspect;
    assert(settings_load_file(argv[2]) && g_settings.aspect == selected); // reject invalid ratio
    memset(&c, 0xa5, sizeof c); before=c;
    float aspect=native_width/1080.0f;
    memcpy(&before.v[1],&aspect,sizeof aspect);
    assert(widescreen_hook(&c, 0x70015c8948ull)==0 && !memcmp(&c,&before,sizeof c));

    unsigned char widths[16], heights[16];
    memset(widths, 0x5a, sizeof widths); memset(heights, 0x5a, sizeof heights);
    c.x[0]=(uintptr_t)(widths+4); c.x[1]=(uintptr_t)(heights+4); before=c;
    assert(widescreen_hook(&c, sites[0]) == 1);
    assert(LD32(widths+4)==native_width && LD32(heights+4)==1080);
    assert(!memcmp(&c, &before, sizeof c));
    for (int i=0; i<16; ++i) if (i<4 || i>=8) assert(widths[i]==0x5a && heights[i]==0x5a);

    const int slots[] = {2,1,1,1,9,3};
    const uint64_t expected[] = {native_width,native_width,native_width,native_width,screen_width,native_width};
    for (int i=0; i<6; ++i) {
        memset(&c, 0xa5, sizeof c); c.x[4]=1080; before=c;
        before.x[slots[i]]=expected[i];
        assert(widescreen_hook(&c, sites[i+1])==0);
        assert(!memcmp(&c, &before, sizeof c));
    }
    c.x[4]=720;
    assert(widescreen_hook(&c, sites[6])==0 && c.x[3]==screen_width);

    g_settings.res_mode=RES_MATCH;
    OutScale out;
    output_scale(native_width,1080,false,1,-1,-1,&out);
    close_to(out.ax,1); close_to(out.ay,1);
    assert(out.vx==0 && out.vy==0 && out.vw==native_width && out.vh==1080);
    if (native_width==2520) {
        output_scale(3440,1440,false,2,-1,-1,&out);
        close_to(out.vw,3360); close_to(out.vx,40); close_to(out.vh,1440);
        close_to(out.sx,out.sy); close_to(out.sx,1440.0/1080);
    }
    output_scale(1920,1080,false,3,-1,-1,&out);
    close_to(out.vw/out.vh,native_width/1080.0);
    close_to(out.vx,(1920-out.vw)/2);
    close_to(out.vy,(1080-out.vh)/2);
    assert(out.vw<=1920.000001 && out.vh<=1080.000001);
    assert(fabs(out.vw-1920)<1e-6 || fabs(out.vh-1080)<1e-6);
    close_to(out.sx,out.sy); close_to(out.shape,1);
    output_scale(screen_width,720,false,4,-1,-1,&out);
    close_to(out.sx,2.0/3); close_to(out.sy,2.0/3);
    close_to(out.vw,screen_width); close_to(out.vh,720);
    g_settings.res_mode=RES_Q50;
    output_scale(native_width,1080,false,5,-1,-1,&out);
    close_to(out.sx,.5); close_to(out.sy,.5); assert(out.vw==native_width);
    g_settings.res_mode=8; g_gpu_integrated=false;
    output_scale(native_width,1080,false,6,-1,-1,&out);
    assert(out.ax*native_width<=16384.000001); close_to(out.ax,out.ay);

    OutputRect movie;
    output_fit(3440,1440,1920,1080,&movie);
    assert(movie.x==440 && movie.y==0 && movie.w==2560 && movie.h==1440);
    output_fit(2520,1080,1920,1080,&movie);
    assert(movie.x==300 && movie.y==0 && movie.w==1920 && movie.h==1080);
    output_fit(2520,1080,720,400,&movie); // the shipped intro's actual dimensions
    close_to(movie.x,288); close_to(movie.y,0); close_to(movie.w,1944); close_to(movie.h,1080);
    output_fit(native_width,1080,720,400,&movie);
    close_to(movie.w/movie.h,1.8);
    close_to(movie.x,(native_width-movie.w)/2); close_to(movie.y,(1080-movie.h)/2);
    assert(movie.w<=native_width+1e-6 && movie.h<=1080.000001);

    assert(settings_load_file(argv[3]) && g_settings.aspect==ASPECT_ORIGINAL);
    g_settings.res_mode=RES_MATCH;
    output_scale(3440,1440,false,7,-1,-1,&out);
    assert(out.vx==440 && out.vw==2560 && out.vh==1440);
    assert(settings_load_file(argv[4]) && g_settings.aspect==ASPECT_ORIGINAL);
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix="sf2-wide-") as directory:
            root = Path(directory)
            (root / "test.c").write_text(source)
            configs = []
            for name, value in (("invalid", "5:4"), ("original", "original"), ("alias", "16:9")):
                path = root / f"{name}.ini"
                path.write_text(f"[video]\naspect_ratio = {value}\n")
                configs.append(str(path))
            compile_harness(root / "test.c", root / "test")
            for ratio, native, initial in (("16:10", 1728, 1152), ("21:9", 2520, 1680), ("32:9", 3840, 2560)):
                with self.subTest(ratio=ratio):
                    wide = root / "wide.ini"
                    wide.write_text(f"[video]\naspect_ratio = {ratio}\n")
                    subprocess.run([str(root / "test"), str(wide), *configs, str(native), str(initial)], check=True)
