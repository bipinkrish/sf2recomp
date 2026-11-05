"""Decode through the native movie path using only a converted Linux bundle."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import test_convert
from test_settings import ROOT, sdl_flags


@unittest.skipUnless(sys.platform.startswith("linux") and shutil.which("ffmpeg")
                     and (ROOT / "deps/ffmpeg/bin/ffmpeg").is_file(),
                     "Requires Linux, the bundled decoder, and FFmpeg to generate a test clip")
class BundledDecoderTests(unittest.TestCase):
    def setUp(self):
        test_convert.PackageTests.setUp(self)
        self.addCleanup(self.temp.cleanup)

    def test_packaged_native_movie_uses_bundled_executable_and_libraries(self):
        source = r'''
#include "hle.h"
#undef HLE_FUNC
#undef RT_ON_RESET
#define HLE_FUNC(sym, fn) static void fn(Cpu *c)
#define RT_ON_RESET(fn) static void fn(void)
#include "hle/movie.c"
#include <assert.h>
bool plat_exe_path(char *out, size_t size) {
    ssize_t n = readlink("/proc/self/exe", out, size - 1);
    if (n < 0 || (size_t)n >= size - 1) return false;
    out[n] = 0;
    return true;
}
typedef struct { MoviePipe pipe; MoviePlayer *player; const char *path; } Reader;
static void *drain(void *arg) {
    Reader *r = arg;
    FILE *out = fopen(r->path, "wb"); assert(out);
    unsigned char data[16384]; ssize_t n;
    while ((n = movie_pipe_read(r->pipe, data, sizeof data, r->player)) > 0)
        assert(fwrite(data, 1, n, out) == (size_t)n);
    assert(n == 0); fclose(out); return NULL;
}
int main(int argc, char **argv) {
    if (argc == 3 && !strcmp(argv[1], "--check-game")) return 0;
    assert(argc == 4);
    MovieDecoder decoder;
    assert(movie_select_decoder(&decoder));
    assert(decoder.executable[0] == '/' && decoder.library_dir[0] == '/');
    MoviePlayer p = {0};
    assert(movie_spawn_decoder(argv[1], &decoder, &p.child, &p.video_fd, &p.audio_fd));
    Reader readers[] = {{p.video_fd, &p, argv[2]}, {p.audio_fd, &p, argv[3]}};
    pthread_t threads[2];
    for (int i = 0; i < 2; i++) assert(!pthread_create(&threads[i], NULL, drain, &readers[i]));
    for (int i = 0; i < 2; i++) pthread_join(threads[i], NULL);
    assert(movie_child_wait(p.child) == 0);
    FILE *video = fopen(argv[2], "rb");
    assert(video && !fseek(video, 0, SEEK_END) && ftell(video) == MOVIE_FRAME_BYTES * 5);
    fclose(video);
}
'''
        harness = self.base / "movie.c"
        harness.write_text(source)
        cflags, libs = sdl_flags()
        subprocess.run(["clang", "-O2", "-mcx16", "-ffunction-sections", "-fdata-sections",
                        "-I", str(ROOT / "runtime"), *cflags, str(harness), "-Wl,--gc-sections",
                        *libs, "-pthread", "-lm", "-ldl", "-o", str(self.exe)], check=True)
        clip = self.base / "synthetic clip.mp4"
        subprocess.run([shutil.which("ffmpeg"), "-v", "error", "-f", "lavfi", "-i",
                        "color=c=red:s=64x64:r=25", "-f", "lavfi", "-i",
                        "sine=frequency=440:sample_rate=48000", "-t", "0.2",
                        "-c:v", "libx264", "-c:a", "aac", str(clip)], check=True)
        test_convert.convert.package_game(self.game, self.output, self.exe, self.base / "sdl",
                                          ROOT / "deps/ffmpeg")
        # Move the finished game to detect any accidental use of the original paths.
        moved = self.base / "moved game with spaces"
        self.output.rename(moved)
        poison = self.base / "poison PATH"
        poison.mkdir()
        sentinel = self.base / "system-ffmpeg-was-used"
        fake = poison / "ffmpeg"
        fake.write_text(f"#!/bin/sh\nprintf used > '{sentinel}'\nexit 99\n")
        fake.chmod(0o755)
        video, audio = self.base / "video.nv12", self.base / "audio.pcm"
        env = {"PATH": str(poison), "LD_DEBUG": "libs", "LD_DEBUG_OUTPUT": str(self.base / "loader")}
        subprocess.run([str(moved / "sf2"), str(clip), str(video), str(audio)],
                       cwd=self.base, env=env, check=True, timeout=15)
        self.assertFalse(sentinel.exists(), "The system FFmpeg fallback must never run")
        self.assertGreater(audio.stat().st_size, 0)
        self.assertGreater(video.stat().st_size, 0)
        loaded = "\n".join(p.read_text() for p in self.base.glob("loader.*"))
        for name in ("avcodec", "avformat", "avutil", "avfilter", "avdevice", "swscale", "swresample"):
            self.assertIn(f"calling init: {moved}/lib/ffmpeg/lib/lib{name}.so.", loaded)


if __name__ == "__main__":
    unittest.main()
