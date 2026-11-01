"""FFmpeg must be able to write audio before producing its first video frame."""
import os
import pathlib
import shutil
import subprocess
import tempfile
import unittest
from test_settings import ROOT, sdl_flags


class MovieStartupTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which("ffmpeg"), "ffmpeg is needed for intro startup integration")
    def test_boot_intro_reuse_early_skip_reset_and_disabled_intro(self):
        source = r'''
#include "hle.h"
#undef HLE_FUNC
#undef RT_ON_RESET
#define HLE_FUNC(sym, fn) static void fn(Cpu *c)
#define RT_ON_RESET(fn) static void fn(void)
#include "hle/movie.c"
#include <assert.h>
const char *g_romfs_dir;
Settings g_settings = {.show_intro = true};
void renderer_movie_clear(void) {}
bool renderer_movie_submit_nv12(const void *data, uint32_t w, uint32_t h, uint32_t stride, uint32_t uv) { return true; }
void audio_movie_set_active(bool active) { assert(!active); }
bool plat_exe_path(char *out, size_t size) { return false; }
int main(int argc, char **argv) {
    assert(argc == 2); g_romfs_dir = argv[1];
    movie_preload_intro();
    assert(movie_overlay_active() && !movie_guest_ready());
    MovieChild decoder = g_movie.child;
    movie_preload_intro(); assert(g_movie.child == decoder); // idempotent
    while (!atomic_load(&g_movie.first_frame_ready)) {
        assert(movie_overlay_active()); movie_sleep_ns(1000000);
    }
    assert(!atomic_load(&g_movie.playback_started));
    movie_sleep_ns(900000000); // graphics startup can outlast the decoded frame queue
    assert(movie_render_tick());
    assert(!atomic_load(&g_movie.playback_started)); // decoded/submitted is not yet presented
    movie_presented(); assert(atomic_load(&g_movie.playback_started));
    uint64_t clock = g_movie.start_ns;
    movie_sleep_ns(200000000);
    pthread_mutex_lock(&g_movie_frames_lock);
    assert(g_movie_frames[0].index == 0); // catch-up cannot overwrite unconsumed opening frames
    pthread_mutex_unlock(&g_movie_frames_lock);
    assert(movie_start("rom:/Data/StreamingAssets/database/intro.mp4"));
    assert(movie_guest_ready() && g_movie.child == decoder);
    assert(g_movie.start_ns == clock && atomic_load(&g_movie.playback_started));
    movie_reset();
    assert(!movie_overlay_active() && !g_movie.child && !movie_guest_ready());

    // A click can arrive while the engine is still starting. Cleanup must not
    // erase that choice: the eventual SetDataSource cannot reopen the intro.
    movie_preload_intro(); movie_request_skip(); assert(movie_poll());
    while (g_movie.monitor_started) { movie_poll(); movie_sleep_ns(1000000); }
    assert(!movie_guest_ready() && !g_movie.child);
    assert(movie_start("database/intro.mp4"));
    assert(movie_guest_ready() && !movie_overlay_active() && !g_movie.child);
    assert(atomic_load(&g_movie.source_ready)); // empty guest tracks still succeed
    movie_reset();

    // Resetting before the engine claims the intro must allow a fresh next boot.
    movie_preload_intro(); assert(!movie_guest_ready()); movie_reset();
    assert(!movie_guest_ready() && !g_movie.child);
    g_settings.show_intro = false; movie_preload_intro();
    assert(!movie_overlay_active() && !g_movie.child);
    g_settings.show_intro = true; g_romfs_dir = "/does/not/exist"; movie_preload_intro();
    assert(!movie_overlay_active() && !movie_guest_ready() && !g_movie.child);
}
'''
        with tempfile.TemporaryDirectory(prefix="sf2-boot-intro-") as directory:
            root = pathlib.Path(directory)
            clips = root / "Data/StreamingAssets/database"
            clips.mkdir(parents=True)
            subprocess.run([shutil.which("ffmpeg"), "-v", "error", "-f", "lavfi", "-i",
                            "color=c=blue:s=64x64:r=25", "-f", "lavfi", "-i",
                            "sine=frequency=440:sample_rate=48000", "-t", "2", "-c:v", "libx264", "-c:a", "aac",
                            str(clips / "intro.mp4")], check=True)
            harness = root / "movie.c"
            harness.write_text(source)
            cflags, libs = sdl_flags()
            subprocess.run(["clang", "-O2", "-mcx16", "-ffunction-sections", "-fdata-sections",
                            "-I", str(ROOT / "runtime"), *cflags, str(harness),
                            "-Wl,--gc-sections", *libs, "-pthread", "-lm", "-ldl",
                            "-o", str(root / "movie")], check=True)
            subprocess.run([str(root / "movie"), str(root)], check=True, timeout=10,
                           env={**os.environ, "LARECOMP_NO_AUDIO": "1", "LARECOMP_FFMPEG": shutil.which("ffmpeg")})

    def test_skip_does_not_wait_for_startup_or_decoder_workers(self):
        source = r'''
#include "hle.h"
#undef HLE_FUNC
#undef RT_ON_RESET
#define HLE_FUNC(sym, fn) static void fn(Cpu *c)
#define RT_ON_RESET(fn) static void fn(void)
#include "hle/movie.c"
#include <assert.h>
static atomic_bool release_monitor;
static unsigned clears;
void renderer_movie_clear(void) { clears++; }
void audio_movie_set_active(bool active) { assert(!active); }
static void *delayed_monitor(void *arg) {
    while (!atomic_load(&release_monitor)) movie_sleep_ns(1000000);
    atomic_store(&g_movie.monitor_done, true);
    return NULL;
}
int main(void) {
    atomic_store(&g_movie_visible, true);
    assert(!pthread_create(&g_movie.monitor_thread, NULL, delayed_monitor, NULL));
    g_movie.monitor_started = true;
    movie_request_skip();
    // The window must remain responsive while SetDataSource holds the lock.
    pthread_mutex_lock(&g_movie_lock);
    assert(!movie_poll());
    assert(atomic_load(&g_movie_skip_requested) && movie_overlay_active());
    pthread_mutex_unlock(&g_movie_lock);
    assert(movie_poll());
    assert(!movie_overlay_active() && atomic_load(&g_movie.stop));
    assert(clears && g_movie.monitor_started && !atomic_load(&g_movie.monitor_done));
    // Repeated polls may not join an unfinished monitor, either.
    for (int i = 0; i < 100; ++i) movie_poll();
    assert(g_movie.monitor_started);
    atomic_store(&release_monitor, true);
    while (!atomic_load(&g_movie.monitor_done)) movie_sleep_ns(1000000);
    movie_poll();
    assert(!g_movie.monitor_started && !atomic_load(&g_movie.stop));
    assert(!atomic_load(&g_movie.monitor_done));
    movie_poll(); // cleanup is idempotent
}
'''
        with tempfile.TemporaryDirectory(prefix="sf2-movie-skip-") as directory:
            root = pathlib.Path(directory)
            harness = root / "movie.c"
            harness.write_text(source)
            cflags, libs = sdl_flags()
            subprocess.run(["clang", "-O2", "-mcx16", "-ffunction-sections", "-fdata-sections",
                            "-I", str(ROOT / "runtime"), *cflags, str(harness),
                            "-Wl,--gc-sections", *libs, "-pthread", "-lm", "-ldl",
                            "-o", str(root / "movie")], check=True)
            subprocess.run([str(root / "movie")], check=True, timeout=5)

    def test_audio_pipe_drains_before_playback_starts(self):
        source = r'''
#include "hle.h"
#undef HLE_FUNC
#undef RT_ON_RESET
#define HLE_FUNC(sym, fn) static void fn(Cpu *c)
#define RT_ON_RESET(fn) static void fn(void)
#include "hle/movie.c"
#include <assert.h>
int main(void) {
    int fds[2];
    assert(pipe(fds) == 0);
    MoviePlayer player = {.audio_fd = fds[0]};
    pthread_t reader;
    assert(pthread_create(&reader, NULL, movie_audio_reader, &player) == 0);
    // More than a Linux pipe can hold. No video/presentation signal is supplied:
    // a decoder writing these samples must still be able to proceed to video.
    char samples[4096] = {0};
    for (int i = 0; i < 64; ++i)
        assert(write(fds[1], samples, sizeof samples) == sizeof samples);
    close(fds[1]);
    pthread_join(reader, NULL);
    assert(atomic_load(&player.audio_done));
    assert(!atomic_load(&player.playback_started));
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix="sf2-movie-test-") as directory:
            root = pathlib.Path(directory)
            harness = root / "movie.c"
            harness.write_text(source)
            cflags, libs = sdl_flags()
            subprocess.run(["clang", "-O2", "-mcx16", "-D_POSIX_C_SOURCE=200809L",
                            "-D_XOPEN_SOURCE=700", "-ffunction-sections", "-fdata-sections",
                            "-I", str(ROOT / "runtime"), *cflags, str(harness),
                            "-Wl,--gc-sections", *libs, "-pthread", "-lm", "-ldl",
                            "-o", str(root / "movie")], check=True)
            subprocess.run([str(root / "movie")], check=True, timeout=5)


if __name__ == "__main__":
    unittest.main()
