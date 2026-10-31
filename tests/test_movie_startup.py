"""FFmpeg must be able to write audio before producing its first video frame."""
import pathlib
import subprocess
import tempfile
import unittest
from test_settings import ROOT, sdl_flags


class MovieStartupTests(unittest.TestCase):
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
