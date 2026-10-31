"""Run the Windows SDL process/pipe movie path on Linux with a synthetic clip."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest
from test_settings import ROOT, sdl_flags


@unittest.skipUnless(shutil.which("ffmpeg"), "ffmpeg is needed for the synthetic movie integration test")
class WindowsMovieTests(unittest.TestCase):
    def test_binary_video_and_audio_pipes_and_cancellation(self):
        source = r'''
#include "hle.h"
#undef HLE_FUNC
#undef RT_ON_RESET
#define HLE_FUNC(sym, fn) static void fn(Cpu *c)
#define RT_ON_RESET(fn) static void fn(void)
#define SF2_MOVIE_SDL_PROCESS 1
#include "hle/movie.c"
#include <assert.h>
typedef struct { MoviePipe pipe; MoviePlayer *player; const char *path; } Reader;
static void *drain(void *arg) {
    Reader *r = arg;
    FILE *out = fopen(r->path, "wb");
    assert(out);
    unsigned char data[16384];
    ssize_t n;
    while ((n = movie_pipe_read(r->pipe, data, sizeof data, r->player)) > 0)
        assert(fwrite(data, 1, n, out) == (size_t)n);
    assert(n == 0);
    fclose(out);
    return NULL;
}
int main(int argc, char **argv) {
    assert(argc == 5);
    MovieDecoder decoder = {0};
    snprintf(decoder.executable, sizeof decoder.executable, "%s", argv[1]);
    MoviePlayer p = {0};
    assert(movie_spawn_decoder(argv[2], &decoder, &p.child, &p.video_fd, &p.audio_fd));
    Reader readers[] = {{p.video_fd, &p, argv[3]}, {p.audio_fd, &p, argv[4]}};
    pthread_t threads[2];
    for (int i = 0; i < 2; i++) assert(!pthread_create(&threads[i], NULL, drain, &readers[i]));
    for (int i = 0; i < 2; i++) pthread_join(threads[i], NULL);
    FILE *video = fopen(argv[3], "rb");
    assert(video && !fseek(video, 0, SEEK_END) && ftell(video) == MOVIE_FRAME_BYTES * 5);
    fclose(video);
    assert(movie_child_wait(p.child) == 0);
    movie_child_destroy(p.child);
    // Cancellation must also work with full/unread output pipes.
    assert(movie_spawn_decoder(argv[2], &decoder, &p.child, &p.video_fd, &p.audio_fd));
    SDL_Delay(100);
    movie_child_kill(p.child);
    movie_child_wait(p.child);
    movie_child_destroy(p.child);
}
'''
        with tempfile.TemporaryDirectory(prefix="sf2-windows-movie-") as directory:
            work = pathlib.Path(directory)
            ffmpeg = shutil.which("ffmpeg")
            clip = work / "synthetic clip.mp4"
            subprocess.run([ffmpeg, "-v", "error", "-f", "lavfi", "-i", "color=c=red:s=64x64:r=25",
                            "-f", "lavfi", "-i", "sine=frequency=440:sample_rate=48000",
                            "-t", "0.2", "-c:v", "libx264", "-c:a", "aac", str(clip)], check=True)
            harness = work / "movie.c"
            harness.write_text(source)
            cflags, libs = sdl_flags()
            subprocess.run(["clang", "-O2", "-mcx16", "-ffunction-sections", "-fdata-sections",
                            "-I", str(ROOT / "runtime"), *cflags, str(harness), "-Wl,--gc-sections",
                            *libs, "-pthread", "-lm", "-ldl", "-o", str(work / "movie")], check=True)
            video, audio = work / "video.nv12", work / "audio.pcm"
            subprocess.run([str(work / "movie"), ffmpeg, str(clip), str(video), str(audio)], check=True, timeout=10)
            reference = work / "reference.pcm"
            subprocess.run([ffmpeg, "-v", "error", "-i", str(clip), "-map", "0:a:0", "-vn",
                            "-c:a", "pcm_s16le", "-ar", "48000", "-ac", "2", "-f", "s16le", str(reference)], check=True)
            self.assertEqual(audio.read_bytes(), reference.read_bytes())
            self.assertGreater(len(audio.read_bytes()), 0)
            self.assertGreater(video.stat().st_size, 0)


if __name__ == "__main__":
    unittest.main()
