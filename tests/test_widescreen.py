"""Opt-in native screen dimensions, backing storage, presentation and video fit."""
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_settings import compile_harness


class WidescreenTests(unittest.TestCase):
    def test_config_native_interfaces_and_aspect_preservation(self):
        source = r'''
#include <assert.h>
#include <math.h>
#include "hle.h"
#include "settings.h"
#include "hle/widescreen_hooks.inc"
#include "nvn/output_scale.c"

static void close_to(double a, double b) { assert(fabs(a-b) < 1e-6); }

int main(int argc, char **argv) {
    assert(argc == 5 && g_settings.aspect == ASPECT_ORIGINAL);
    const uint64_t sites[] = {0x70019016c0ull, 0x700190160cull, 0x700191177cull,
        0x7001911810ull, 0x70019119ccull, 0x70015fbb24ull, 0x70015fbcf0ull};
    Cpu c, before;
    memset(&c, 0xa5, sizeof c); before = c;
    // Disabled hooks must not even dereference the intentionally invalid pointers.
    for (size_t i=0; i<sizeof sites/sizeof *sites; ++i) {
        assert(widescreen_hook(&c, sites[i]) == 0);
        assert(!memcmp(&c, &before, sizeof c));
    }
    assert(widescreen_hook(&c, 0x70015c8948ull)==0 && !memcmp(&c,&before,sizeof c));
    assert(widescreen_hook(&c, 0) == -1);
    assert(settings_load_file(argv[1]) && g_settings.aspect == ASPECT_21_9);
    assert(settings_load_file(argv[2]) && g_settings.aspect == ASPECT_21_9); // reject invalid ratio
    memset(&c, 0xa5, sizeof c); before=c;
    float aspect=7.0f/3.0f;
    memcpy(&before.v[1],&aspect,sizeof aspect);
    assert(widescreen_hook(&c, 0x70015c8948ull)==0 && !memcmp(&c,&before,sizeof c));

    unsigned char widths[16], heights[16];
    memset(widths, 0x5a, sizeof widths); memset(heights, 0x5a, sizeof heights);
    c.x[0]=(uintptr_t)(widths+4); c.x[1]=(uintptr_t)(heights+4); before=c;
    assert(widescreen_hook(&c, sites[0]) == 1);
    assert(LD32(widths+4)==2520 && LD32(heights+4)==1080);
    assert(!memcmp(&c, &before, sizeof c));
    for (int i=0; i<16; ++i) if (i<4 || i>=8) assert(widths[i]==0x5a && heights[i]==0x5a);

    const int slots[] = {2,1,1,1,9,3};
    const uint64_t expected[] = {2520,2520,2520,2520,1680,2520};
    for (int i=0; i<6; ++i) {
        memset(&c, 0xa5, sizeof c); c.x[4]=1080; before=c;
        before.x[slots[i]]=expected[i];
        assert(widescreen_hook(&c, sites[i+1])==0);
        assert(!memcmp(&c, &before, sizeof c));
    }
    c.x[4]=720;
    assert(widescreen_hook(&c, sites[6])==0 && c.x[3]==1680);

    g_settings.res_mode=RES_MATCH;
    OutScale out;
    output_scale(2520,1080,false,1,-1,-1,&out);
    close_to(out.ax,1); close_to(out.ay,1);
    assert(out.vx==0 && out.vy==0 && out.vw==2520 && out.vh==1080);
    output_scale(3440,1440,false,2,-1,-1,&out);
    close_to(out.vw,3360); close_to(out.vx,40); close_to(out.vh,1440);
    close_to(out.sx,out.sy); close_to(out.sx,1440.0/1080);
    output_scale(1920,1080,false,3,-1,-1,&out);
    close_to(out.vw,1920); close_to(out.vh,1920.0*3/7);
    close_to(out.vy,(1080-out.vh)/2);
    output_scale(1680,720,false,4,-1,-1,&out);
    close_to(out.sx,2.0/3); close_to(out.sy,2.0/3);
    assert(out.vw==1680 && out.vh==720);
    g_settings.res_mode=RES_Q50;
    output_scale(2520,1080,false,5,-1,-1,&out);
    close_to(out.sx,.5); close_to(out.sy,.5); assert(out.vw==2520);
    g_settings.res_mode=8; g_gpu_integrated=false;
    output_scale(2520,1080,false,6,-1,-1,&out);
    assert(out.ax*2520<=16384.000001); close_to(out.ax,out.ay);

    OutputRect movie;
    output_fit(3440,1440,1920,1080,&movie);
    assert(movie.x==440 && movie.y==0 && movie.w==2560 && movie.h==1440);
    output_fit(2520,1080,1920,1080,&movie);
    assert(movie.x==300 && movie.y==0 && movie.w==1920 && movie.h==1080);
    output_fit(2520,1080,720,400,&movie); // the shipped intro's actual dimensions
    close_to(movie.x,288); close_to(movie.y,0); close_to(movie.w,1944); close_to(movie.h,1080);

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
            for name, value in (("wide", "21:9"), ("invalid", "32:9"),
                                ("original", "original"), ("alias", "16:9")):
                path = root / f"{name}.ini"
                path.write_text(f"[video]\naspect_ratio = {value}\n")
                configs.append(str(path))
            compile_harness(root / "test.c", root / "test")
            subprocess.run([str(root / "test"), *configs], check=True)
