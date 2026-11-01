"""NVN presentation timing and window-size requests reach the runtime boundary."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_settings import sdl_flags

ROOT = Path(__file__).resolve().parents[1]


class PresentationTests(unittest.TestCase):
    def test_movie_dismissal_refreshes_without_another_guest_frame(self):
        # Compile the actual UI scheduling helper without initializing Vulkan.
        device = (ROOT / "runtime/nvn/vulkan/device.c").read_text()
        start = device.index("static void backend_movie_present(bool dismissed)")
        end = device.index("// ================================================================== UI thread", start)
        source = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
typedef struct { int unused; } NvnTexture;
static _Atomic uint64_t total_frames = 11, last_game_present_ns;
static _Atomic(NvnTexture *) g_last_presented;
static bool active, queue_on = true, busy;
static unsigned attempts, presents;
static NvnTexture *shown;
bool movie_overlay_active(void) { return active; }
bool rq_on(void) { return queue_on; }
uint64_t SDL_GetTicksNS(void) { return 1000000000; }
bool rq_try_present(NvnTexture *t, uint64_t frame) {
    assert(frame == 11); // UI presents never advance the guest's frame counter
    attempts++;
    if (busy) return false;
    presents++; shown = t; return true;
}
''' + device[start:end] + r'''
int main(void) {
    NvnTexture menu;
    atomic_store(&g_last_presented, &menu);
    active = true;
    backend_movie_present(false);
    assert(presents == 1 && !shown); // movie-only present during loading
    active = false; busy = true;
    backend_movie_present(true);
    assert(attempts == 2 && presents == 1);
    busy = false;
    backend_movie_present(false);
    assert(presents == 2 && shown == &menu); // retry without any guest progress
    backend_movie_present(false);
    assert(presents == 2); // refresh only once
    // Skip before the UI's first movie tick must also trigger a refresh.
    backend_movie_present(true);
    assert(presents == 3 && shown == &menu);
    // Recent guest presents already carry the movie; avoid competing with them.
    active = true; atomic_store(&last_game_present_ns, SDL_GetTicksNS());
    backend_movie_present(false);
    assert(presents == 3);
    active = false;
    backend_movie_present(false); // natural movie completion also reveals the game
    assert(presents == 4 && shown == &menu);
    queue_on = false;
    backend_movie_present(true);
    assert(presents == 4); // Vulkan stays on its owner thread without the queue
}
'''
        with tempfile.TemporaryDirectory(prefix="sf2-movie-present-") as folder:
            root = Path(folder)
            (root / "test.c").write_text(source)
            subprocess.run(["clang", "-O2", str(root / "test.c"), "-o", str(root / "test")], check=True)
            subprocess.run([str(root / "test")], check=True)

    def test_present_cap_intervals_resize_and_match_display(self):
        source = r'''
#include <assert.h>
#include <math.h>
#include "nvn/nvn.h"
// Call actual NVN entry points without retaining every unrelated registered HLE.
#undef NVN
#define NVN(name) static void hle_##name(Cpu *c)
#include "nvn/objects.c"
#include "nvn/output_scale.c"
#include "ui/menu_stubs.c"

Settings g_settings = {.target_fps=60, .res_mode=RES_MATCH};
bool g_gpu_integrated = true;
uint32_t g_window_resize_request;
static uint64_t now=1000000000ull, presented_at;
static unsigned presents, sleeps, resizes;
static int window_w,window_h;
void lan_menu_update_capture(void) {}
void developer_update_text_input(void *window) { assert(window == (void *)1); }
uint64_t plat_now_ns(void) { return now; }
void plat_sleep_until_ns(uint64_t deadline) { assert(deadline > now); now=deadline; sleeps++; }
void backend_present(NvnTexture *t) { assert(t->width==1920); presented_at=now; presents++; }
const char *renderer_stats(void) { return "test"; }
bool SDL_SetWindowSize(SDL_Window *window, int w, int h) {
    assert(window==(SDL_Window *)1); window_w=w; window_h=h; resizes++; return true;
}

int main(void) {
    NvnTexture texture={.width=1920,.height=1080};
    uint64_t guest_texture=(uintptr_t)&texture;
    NvnWindow window={.num=1,.active=1,.interval=1,.textures={(uintptr_t)&guest_texture}};
    uint64_t guest_window=(uintptr_t)&window;
    Cpu c={0}; c.x[1]=(uintptr_t)&guest_window;
    hle_nvnQueuePresentTexture(&c); assert(presents==1 && sleeps==0);
    uint64_t first=presented_at;
    for (int i=1;i<=120;i++) {
        now+=1000000; // one millisecond of guest/render work per frame
        hle_nvnQueuePresentTexture(&c);
        assert(presented_at==first+(uint64_t)i*16666666ull);
    }
    assert(sleeps==120); // a fast GPU cannot run the game at hundreds of FPS
    now+=5000000000ull; uint64_t resumed=now;
    hle_nvnQueuePresentTexture(&c); assert(presented_at==resumed && sleeps==120);
    hle_nvnQueuePresentTexture(&c); assert(presented_at==resumed+16666666ull);

    c.x[0]=(uintptr_t)&guest_window; c.x[1]=2;
    hle_nvnWindowSetPresentInterval(&c); assert(window.interval==2);
    c.x[1]=(uintptr_t)&guest_window;
    hle_nvnQueuePresentTexture(&c); first=presented_at;
    hle_nvnQueuePresentTexture(&c); assert(presented_at-first==33333333ull);
    window.interval=0;
    hle_nvnQueuePresentTexture(&c); first=presented_at;
    hle_nvnQueuePresentTexture(&c); assert(presented_at-first==16666666ull);
    g_settings.target_fps=120;
    hle_nvnQueuePresentTexture(&c); first=presented_at;
    hle_nvnQueuePresentTexture(&c); assert(presented_at-first==8333333ull);
    g_settings.target_fps=0; // invalid/uninitialized settings use the default
    hle_nvnQueuePresentTexture(&c); first=presented_at;
    hle_nvnQueuePresentTexture(&c); assert(presented_at-first==16666666ull);
    unsigned before=presents; c.x[2]=2;
    hle_nvnQueuePresentTexture(&c); assert(presents==before);

    g_window_resize_request=(2560u<<16)|1440;
    menu_apply_window_requests((void *)1);
    assert(resizes==1 && window_w==2560 && window_h==1440 && !g_window_resize_request);
    menu_apply_window_requests((void *)1); assert(resizes==1);

    OutScale out;
    output_scale(2560,1440,false,1,-1,-1,&out);
    assert(fabs(out.ax-2560/1920.0)<1e-6 && fabs(out.ay-1440/1080.0)<1e-6);
    assert(out.vw==2560 && out.vh==1440);
    output_scale(960,540,false,2,-1,-1,&out);
    assert(out.ax==.5 && out.ay==.5);
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix="sf2-presentation-") as folder:
            root = Path(folder)
            (root / "test.c").write_text(source)
            cflags, _ = sdl_flags()
            subprocess.run(["clang", "-O2", "-Wno-format", "-ffunction-sections", "-fdata-sections",
                            "-I", str(ROOT / "runtime"), *cflags, str(root / "test.c"),
                            "-Wl,--gc-sections", "-lm", "-pthread", "-o", str(root / "test")], check=True)
            subprocess.run([str(root / "test")], check=True)


if __name__ == "__main__":
    unittest.main()
