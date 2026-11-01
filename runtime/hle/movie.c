// nn::movie HLE implementation for video playback / cinematics
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include <SDL3/SDL.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#if !defined(_WIN32) && !defined(SF2_MOVIE_SDL_PROCESS)
#include <spawn.h>
#include <sys/wait.h>
#endif
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "../hle.h"
#include "../nvn/renderer.h"
#include "../rt.h"
#include "../settings.h"
#include "movie.h"

#if defined(_WIN32) || defined(SF2_MOVIE_SDL_PROCESS)
#define MOVIE_SDL_PROCESS 1
typedef SDL_Process *MovieChild;
typedef SDL_IOStream *MoviePipe;
#define MOVIE_PIPE_NONE NULL
#else
extern char **environ;
typedef pid_t MovieChild;
typedef int MoviePipe;
#define MOVIE_PIPE_NONE (-1)
#endif
extern void audio_movie_set_active(bool active);

#define MOVIE_FPS 25
#define MOVIE_AUDIO_RATE 48000
#define MOVIE_AUDIO_CHANNELS 2
#define MOVIE_AUDIO_BYTES_PER_SAMPLE 2
#define MOVIE_AUDIO_QUEUE_LIMIT (MOVIE_AUDIO_RATE * MOVIE_AUDIO_CHANNELS * MOVIE_AUDIO_BYTES_PER_SAMPLE / 4)
#define MOVIE_FRAME_BYTES ((size_t)RENDERER_MOVIE_WIDTH * RENDERER_MOVIE_HEIGHT * 3 / 2)
#define MOVIE_FRAME_COUNT 16
typedef struct {
    uint64_t index;
    uint8_t *pixels;
    bool valid;
} MovieFrame;

typedef struct {
    MovieChild child;
    MoviePipe video_fd, audio_fd;
    pthread_t video_thread, audio_thread, monitor_thread;
    bool video_started, audio_started, monitor_started;
    SDL_AudioStream *audio_stream;
    bool audio_subsystem;
    uint64_t start_ns;
    atomic_bool stop, source_ready, playback_started, first_frame_ready, first_frame_submitted;
    atomic_bool video_done, audio_done, audio_eof, audio_clock_active, monitor_done;
    atomic_uint_fast64_t audio_bytes_submitted;
} MoviePlayer;

static MoviePlayer g_movie = {.video_fd = MOVIE_PIPE_NONE, .audio_fd = MOVIE_PIPE_NONE};
static pthread_mutex_t g_movie_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_movie_frames_lock = PTHREAD_MUTEX_INITIALIZER;
static MovieFrame g_movie_frames[MOVIE_FRAME_COUNT];
static atomic_bool g_movie_visible;
static atomic_bool g_movie_skip_requested;
static atomic_bool g_intro_prestarted;
static atomic_bool g_movie_guest_ready;
static char g_intro_path[PATH_MAX];
static atomic_uint_fast64_t g_movie_displayed_frame = UINT64_MAX;
static uint64_t movie_now_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ull + t.tv_nsec;
}

static void movie_sleep_ns(uint64_t ns) {
    struct timespec t = {(time_t)(ns / 1000000000ull), (long)(ns % 1000000000ull)};
    while (nanosleep(&t, &t) && errno == EINTR) {}
}

static bool movie_path_safe(const char *path) {
    for (const char *p = path; *p;) {
        while (*p == '/') p++;
        const char *start = p;
        while (*p && *p != '/') p++;
        size_t n = (size_t)(p - start);
        if ((n == 1 && start[0] == '.') || (n == 2 && start[0] == '.' && start[1] == '.')) return false;
    }
    return true;
}

static bool movie_resolve_path(const char *guest_path, char *out, size_t out_size) {
    if (!guest_path || !*guest_path || !g_romfs_dir) return false;
    const char *p = guest_path;
    if (!strncmp(p, "rom:", 4)) p += 4;
    while (*p == '/') p++;
    const char *streaming = strstr(p, "Data/StreamingAssets/");
    if (streaming) p = streaming;
    char rel[PATH_MAX];
    if (!strncmp(p, "Data/StreamingAssets/", 21)) {
        if (snprintf(rel, sizeof rel, "%s", p) >= (int)sizeof rel) return false;
    } else if (!strncmp(p, "database/", 9)) {
        if (snprintf(rel, sizeof rel, "Data/StreamingAssets/%s", p) >= (int)sizeof rel) return false;
    } else {
        if (snprintf(rel, sizeof rel, "Data/StreamingAssets/database/%s", p) >= (int)sizeof rel) return false;
    }
    if (!movie_path_safe(rel)) return false;
    char candidate[PATH_MAX], root[PATH_MAX], resolved[PATH_MAX];
    if (snprintf(candidate, sizeof candidate, "%s/%s", g_romfs_dir, rel) >= (int)sizeof candidate) return false;
    if (!realpath(g_romfs_dir, root) || !realpath(candidate, resolved)) return false;
    size_t root_len = strlen(root);
#ifdef _WIN32
    if (_strnicmp(root, resolved, root_len) || resolved[root_len] != '/') return false;
#else
    if (strncmp(root, resolved, root_len) || resolved[root_len] != '/') return false;
#endif
    if (snprintf(out, out_size, "%s", resolved) >= (int)out_size) return false;
    return true;
}

#ifndef MOVIE_SDL_PROCESS
static int movie_move_fd(int fd) {
    int n;
#ifdef F_DUPFD_CLOEXEC
    n = fcntl(fd, F_DUPFD_CLOEXEC, 10);
#else
    n = fcntl(fd, F_DUPFD, 10);
    if (n >= 0 && fcntl(n, F_SETFD, FD_CLOEXEC) < 0) { close(n); n = -1; }
#endif
    if (n < 0) return -1;
    close(fd);
    return n;
}

static void movie_close_pair(int p[2]) {
    if (p[0] >= 0) close(p[0]);
    if (p[1] >= 0) close(p[1]);
    p[0] = p[1] = -1;
}

#endif

typedef struct {
    char executable[PATH_MAX];
    char library_dir[PATH_MAX];
} MovieDecoder;

static bool movie_select_decoder(MovieDecoder *decoder) {
    memset(decoder, 0, sizeof *decoder);
    const char *override = getenv("LARECOMP_FFMPEG");
    if (override && *override) {
        return snprintf(decoder->executable, sizeof decoder->executable, "%s", override) < (int)sizeof decoder->executable;
    }

    char executable[PATH_MAX];
    if (plat_exe_path(executable, sizeof executable)) {
        char *slash = strrchr(executable, '/');
        if (slash) {
            *slash = 0;
            if (snprintf(decoder->executable, sizeof decoder->executable,
                         "%s/lib/ffmpeg/bin/ffmpeg" PLAT_EXE_SUFFIX, executable) < (int)sizeof decoder->executable &&
                snprintf(decoder->library_dir, sizeof decoder->library_dir,
                         "%s/lib/ffmpeg/lib", executable) < (int)sizeof decoder->library_dir &&
                access(decoder->executable, X_OK) == 0)
                return true;
        }
    }

    // A raw build/smoke run can use the developer's PATH. Converted game folders always bundle FFmpeg here.
    strcpy(decoder->executable, "ffmpeg" PLAT_EXE_SUFFIX);
    LOG("[movie] bundled decoder not found beside the executable; trying PATH (developer fallback)\n");
    return true;
}

#ifndef MOVIE_SDL_PROCESS
static char **movie_spawn_environment(const char *library_dir, char *ld_library_path, size_t path_size,
                                      char ***owned_env) {
    *owned_env = NULL;
    if (!library_dir || !*library_dir) return environ;
#ifdef __APPLE__
    const char *variable = "DYLD_LIBRARY_PATH";
#else
    const char *variable = "LD_LIBRARY_PATH";
#endif
    const char *old_path = getenv(variable);
    int n = snprintf(ld_library_path, path_size, "%s=%s%s%s", variable, library_dir,
                     old_path && *old_path ? ":" : "", old_path && *old_path ? old_path : "");
    if (n < 0 || (size_t)n >= path_size) return NULL;
    size_t count = 0;
    while (environ[count]) count++;
    char **env = calloc(count + 2, sizeof *env);
    if (!env) return NULL;
    size_t used = 0;
    for (size_t i = 0; i < count; i++)
        if (strncmp(environ[i], variable, strlen(variable)) || environ[i][strlen(variable)] != '=')
            env[used++] = environ[i];
    env[used++] = ld_library_path;
    env[used] = NULL;
    *owned_env = env;
    return env;
}

static bool movie_spawn_decoder(const char *path, const MovieDecoder *decoder,
                                pid_t *pid, int *video_fd, int *audio_fd) {
    int video[2] = {-1, -1}, audio[2] = {-1, -1};
    if (pipe(video) || pipe(audio)) goto fail;
    for (int i = 0; i < 2; i++) {
        int n = movie_move_fd(video[i]);
        if (n < 0) goto fail;
        video[i] = n;
        n = movie_move_fd(audio[i]);
        if (n < 0) goto fail;
        audio[i] = n;
    }

    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions)) goto fail;
    int action_error = 0;
    action_error |= posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    action_error |= posix_spawn_file_actions_adddup2(&actions, video[1], STDOUT_FILENO);
    action_error |= posix_spawn_file_actions_adddup2(&actions, audio[1], 3);
    for (int i = 0; i < 2; i++) {
        action_error |= posix_spawn_file_actions_addclose(&actions, video[i]);
        action_error |= posix_spawn_file_actions_addclose(&actions, audio[i]);
    }
    if (action_error) {
        posix_spawn_file_actions_destroy(&actions);
        goto fail;
    }

    char scale_filter[128];
    snprintf(scale_filter, sizeof scale_filter, "scale=%d:%d:flags=fast_bilinear,fps=%d",
             RENDERER_MOVIE_WIDTH, RENDERER_MOVIE_HEIGHT, MOVIE_FPS);
    char *argv[] = {
        (char *)decoder->executable,
        "-nostdin", "-hide_banner", "-loglevel", "error", "-re", "-i", (char *)path,
        "-map", "0:v:0", "-vf", scale_filter, "-pix_fmt", "nv12", "-f", "rawvideo", "pipe:1",
        "-map", "0:a:0?", "-vn", "-c:a", "pcm_s16le", "-ar", "48000", "-ac", "2", "-f", "s16le", "pipe:3",
        NULL
    };
    pid_t child = 0;
    char child_ld_library_path[PATH_MAX * 2];
    char **owned_env = NULL;
    char **child_env = movie_spawn_environment(decoder->library_dir, child_ld_library_path,
                                                sizeof child_ld_library_path, &owned_env);
    if (!child_env) {
        posix_spawn_file_actions_destroy(&actions);
        LOG("[movie] cannot prepare the bundled decoder library path\n");
        goto fail;
    }
    int e = strchr(argv[0], '/') ? posix_spawn(&child, argv[0], &actions, NULL, argv, child_env)
                                : posix_spawnp(&child, argv[0], &actions, NULL, argv, child_env);
    free(owned_env);
    posix_spawn_file_actions_destroy(&actions);
    if (e) {
        LOG("[movie] cannot start %s: %s\n", argv[0], strerror(e));
        goto fail;
    }
    close(video[1]); video[1] = -1;
    close(audio[1]); audio[1] = -1;
    *pid = child;
    *video_fd = video[0]; video[0] = -1;
    *audio_fd = audio[0]; audio[0] = -1;
    return true;

fail:
    movie_close_pair(video);
    movie_close_pair(audio);
    return false;
}

#else
// Windows cannot inherit an arbitrary POSIX fd 3. Use its two standard output
// pipes instead: video on stdout, PCM on stderr. Quiet mode is essential: text
// diagnostics must never enter the PCM stream. Exit status is logged by the host.
// SF2_MOVIE_SDL_PROCESS exercises this same branch on Linux without a Windows SDK.
static bool movie_spawn_decoder(const char *path, const MovieDecoder *decoder,
                                MovieChild *child, MoviePipe *video_fd, MoviePipe *audio_fd) {
    char filter[128];
    snprintf(filter, sizeof filter, "scale=%d:%d:flags=fast_bilinear,fps=%d",
             RENDERER_MOVIE_WIDTH, RENDERER_MOVIE_HEIGHT, MOVIE_FPS);
    const char *argv[] = {
        decoder->executable, "-nostdin", "-hide_banner", "-loglevel", "quiet", "-re", "-i", path,
        "-map", "0:v:0", "-vf", filter, "-pix_fmt", "nv12", "-f", "rawvideo", "pipe:1",
        "-map", "0:a:0?", "-vn", "-c:a", "pcm_s16le", "-ar", "48000", "-ac", "2", "-f", "s16le", "pipe:2", NULL
    };
    SDL_PropertiesID props = SDL_CreateProperties();
    if (!props) return false;
    bool ok = SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, (void *)argv) &&
        SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL) &&
        SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP) &&
        SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_APP);
    SDL_Process *process = ok ? SDL_CreateProcessWithProperties(props) : NULL;
    SDL_DestroyProperties(props);
    if (!process) { LOG("[movie] cannot start decoder: %s\n", SDL_GetError()); return false; }
    SDL_PropertiesID output = SDL_GetProcessProperties(process);
    MoviePipe video = SDL_GetProcessOutput(process);
    MoviePipe audio = SDL_GetPointerProperty(output, SDL_PROP_PROCESS_STDERR_POINTER, NULL);
    if (!video || !audio) {
        SDL_KillProcess(process, true);
        SDL_WaitProcess(process, true, NULL);
        SDL_DestroyProcess(process);
        return false;
    }
    *child = process;
    *video_fd = video;
    *audio_fd = audio;
    return true;
}
#endif

static ssize_t movie_pipe_read(MoviePipe pipe, void *out, size_t size, MoviePlayer *player) {
#ifdef MOVIE_SDL_PROCESS
    while (!atomic_load(&player->stop)) {
        size_t n = SDL_ReadIO(pipe, out, size);
        if (n) return (ssize_t)n;
        SDL_IOStatus status = SDL_GetIOStatus(pipe);
        if (status == SDL_IO_STATUS_NOT_READY) { SDL_Delay(1); continue; }
        if (status == SDL_IO_STATUS_EOF) return 0;
        errno = EIO;
        return -1;
    }
    return 0;
#else
    (void)player;
    return read(pipe, out, size);
#endif
}

static void movie_pipe_close(MoviePipe pipe) {
#ifndef MOVIE_SDL_PROCESS
    if (pipe != MOVIE_PIPE_NONE) close(pipe);
#else
    (void)pipe; // SDL_DestroyProcess owns its I/O streams, after readers join.
#endif
}

static void movie_child_kill(MovieChild child) {
#ifdef MOVIE_SDL_PROCESS
    if (child) SDL_KillProcess(child, true);
#else
    if (child > 0) kill(child, SIGTERM);
#endif
}

static int movie_child_wait(MovieChild child) {
#ifdef MOVIE_SDL_PROCESS
    int status = 255;
    return SDL_WaitProcess(child, true, &status) ? status : 255;
#else
    int status = 0;
    pid_t waited;
    do { waited = waitpid(child, &status, 0); } while (waited < 0 && errno == EINTR);
    return waited < 0 ? 255 : WIFEXITED(status) ? WEXITSTATUS(status) : 255;
#endif
}

static void movie_child_destroy(MovieChild child) {
#ifdef MOVIE_SDL_PROCESS
    if (child) SDL_DestroyProcess(child);
#else
    (void)child;
#endif
}

static void movie_frames_clear(void) {
    pthread_mutex_lock(&g_movie_frames_lock);
    for (int i = 0; i < MOVIE_FRAME_COUNT; i++) {
        free(g_movie_frames[i].pixels);
        g_movie_frames[i] = (MovieFrame){0};
    }
    pthread_mutex_unlock(&g_movie_frames_lock);
}

static bool movie_frames_init(void) {
    pthread_mutex_lock(&g_movie_frames_lock);
    for (int i = 0; i < MOVIE_FRAME_COUNT; i++) {
        if (!g_movie_frames[i].pixels) g_movie_frames[i].pixels = malloc(MOVIE_FRAME_BYTES);
        if (!g_movie_frames[i].pixels) {
            for (int j = 0; j < MOVIE_FRAME_COUNT; j++) {
                free(g_movie_frames[j].pixels);
                g_movie_frames[j] = (MovieFrame){0};
            }
            pthread_mutex_unlock(&g_movie_frames_lock);
            return false;
        }
    }
    pthread_mutex_unlock(&g_movie_frames_lock);
    return true;
}

static size_t movie_read_frame(MoviePipe fd, void *buffer, size_t size, MoviePlayer *p) {
    size_t got = 0;
    while (got < size && !atomic_load(&p->stop)) {
        ssize_t n = movie_pipe_read(fd, (uint8_t *)buffer + got, size - got, p);
        if (n > 0) got += (size_t)n;
        else if (n == 0) break;
        else if (errno != EINTR) break;
    }
    return got;
}

static void *movie_video_reader(void *arg) {
    MoviePlayer *p = arg;
    uint8_t *scratch = malloc(MOVIE_FRAME_BYTES);
    if (scratch) {
        size_t n = movie_read_frame(p->video_fd, scratch, MOVIE_FRAME_BYTES, p);
        if (n == MOVIE_FRAME_BYTES) {
            pthread_mutex_lock(&g_movie_frames_lock);
            MovieFrame *frame = &g_movie_frames[0];
            memcpy(frame->pixels, scratch, MOVIE_FRAME_BYTES);
            frame->index = 0;
            frame->valid = true;
            pthread_mutex_unlock(&g_movie_frames_lock);
            atomic_store(&p->first_frame_ready, true);

            // Keep frame zero intact until the presenter has shown it and released playback.
            while (!atomic_load(&p->playback_started) && !atomic_load(&p->stop)) movie_sleep_ns(1000000ull);
        }
        for (uint64_t index = 1; n == MOVIE_FRAME_BYTES && !atomic_load(&p->stop);) {
            n = movie_read_frame(p->video_fd, scratch, MOVIE_FRAME_BYTES, p);
            if (n != MOVIE_FRAME_BYTES) break;
            // FFmpeg catches up to its input clock after a slow graphics boot.
            // Keep the early frames intact until the presenter consumes them,
            // rather than overwriting the queue and jumping ahead of the audio.
            while (!atomic_load(&p->stop)) {
                uint64_t displayed = atomic_load(&g_movie_displayed_frame);
                if (displayed == UINT64_MAX || index - displayed < MOVIE_FRAME_COUNT) break;
                movie_sleep_ns(1000000ull);
            }
            if (atomic_load(&p->stop)) break;
            pthread_mutex_lock(&g_movie_frames_lock);
            MovieFrame *frame = &g_movie_frames[index % MOVIE_FRAME_COUNT];
            memcpy(frame->pixels, scratch, MOVIE_FRAME_BYTES);
            frame->index = index++;
            frame->valid = true;
            pthread_mutex_unlock(&g_movie_frames_lock);
        }
        free(scratch);
    } else {
        LOG("[movie] cannot allocate video frame buffer\n");
        atomic_store(&p->stop, true);
        movie_child_kill(p->child);
    }
    movie_pipe_close(p->video_fd);
    p->video_fd = MOVIE_PIPE_NONE;
    atomic_store(&p->video_done, true);
    return NULL;
}

static void *movie_audio_reader(void *arg) {
    MoviePlayer *p = arg;
    uint8_t buffer[8192];
    // Drain FFmpeg immediately: it can fill the audio pipe before writing video frame zero.
    // Queue samples on the paused SDL stream, then resume only after that frame reaches the window.
    bool playback_started = false;
    for (;;) {
        if (atomic_load(&p->stop)) break;
        if (!playback_started && atomic_load(&p->playback_started)) {
            playback_started = true;
            if (p->audio_stream) SDL_ResumeAudioStreamDevice(p->audio_stream);
        }
        if (p->audio_stream && playback_started) {
            while (!atomic_load(&p->stop) && SDL_GetAudioStreamQueued(p->audio_stream) > MOVIE_AUDIO_QUEUE_LIMIT)
                movie_sleep_ns(5000000ull);
        }
        ssize_t n = movie_pipe_read(p->audio_fd, buffer, sizeof buffer, p);
        if (n > 0) {
            if (p->audio_stream) {
                if (SDL_PutAudioStreamData(p->audio_stream, buffer, (int)n)) {
                    atomic_fetch_add(&p->audio_bytes_submitted, (uint64_t)n);
                    atomic_store(&p->audio_clock_active, true);
                } else {
                    LOG("[movie] audio output failed: %s\n", SDL_GetError());
                }
            }
        } else if (n < 0 && errno == EINTR) continue;
        else break;
    }
    movie_pipe_close(p->audio_fd);
    p->audio_fd = MOVIE_PIPE_NONE;
    atomic_store(&p->audio_eof, true);
    // A short clip can reach audio EOF before frame zero is presented. Its queued
    // samples still need to start once the presenter releases playback.
    while (p->audio_stream && !atomic_load(&p->stop) &&
           !atomic_load(&p->playback_started) && !atomic_load(&p->video_done))
        movie_sleep_ns(1000000ull);
    if (!playback_started && atomic_load(&p->playback_started)) {
        playback_started = true;
        if (p->audio_stream) SDL_ResumeAudioStreamDevice(p->audio_stream);
    }
    if (p->audio_stream && playback_started && !atomic_load(&p->stop)) {
        SDL_FlushAudioStream(p->audio_stream);
        while (!atomic_load(&p->stop) && SDL_GetAudioStreamQueued(p->audio_stream) > 0)
            movie_sleep_ns(5000000ull);
    }
    atomic_store(&p->audio_done, true);
    return NULL;
}

static void *movie_monitor(void *arg) {
    MoviePlayer *p = arg;
    pthread_join(p->video_thread, NULL);
    pthread_join(p->audio_thread, NULL);
    int status = movie_child_wait(p->child);
    if (!atomic_load(&p->stop) && status != 0)
        LOG("[movie] decoder exited with status %#x\n", status);
    audio_movie_set_active(false);
    atomic_store(&g_movie_visible, false);
    LOG("[movie] host playback finished%s\n", atomic_load(&p->stop) ? " (stopped)" : "");
    atomic_store(&p->monitor_done, true);
    return NULL;
}

// Hide and silence immediately. The monitor joins/reaps the decoder in the background;
// the window thread must keep presenting while the guest loads the next scene.
static void movie_cancel_locked(void) {
    bool was_visible = atomic_exchange(&g_movie_visible, false);
    atomic_store(&g_movie.stop, true);
    atomic_store(&g_movie_skip_requested, false);
    // The monitor reaps a completed child while leaving its PID in the player. Only signal a live overlay, avoiding
    // the chance that a reused PID is signalled when a later movie/reset cleans up an already-finished clip.
    if (was_visible && g_movie.child) movie_child_kill(g_movie.child);
    if (g_movie.audio_stream) {
        SDL_SetAudioStreamGain(g_movie.audio_stream, 0.0f);
        SDL_PauseAudioStreamDevice(g_movie.audio_stream);
        SDL_ClearAudioStream(g_movie.audio_stream);
    }
    audio_movie_set_active(false);
    renderer_movie_clear();
}

static void movie_stop_locked(void) {
    movie_cancel_locked();
    if (g_movie.monitor_started) {
        pthread_join(g_movie.monitor_thread, NULL);
        g_movie.monitor_started = false;
    } else {
        if (g_movie.video_started) pthread_join(g_movie.video_thread, NULL);
        else movie_pipe_close(g_movie.video_fd);
        if (g_movie.audio_started) pthread_join(g_movie.audio_thread, NULL);
        else movie_pipe_close(g_movie.audio_fd);
        if (g_movie.child) movie_child_wait(g_movie.child);
    }
    movie_child_destroy(g_movie.child);
    if (g_movie.audio_stream) SDL_DestroyAudioStream(g_movie.audio_stream);
    if (g_movie.audio_subsystem) SDL_QuitSubSystem(SDL_INIT_AUDIO);
    audio_movie_set_active(false);
    g_movie.child = 0;
    g_movie.video_fd = g_movie.audio_fd = MOVIE_PIPE_NONE;
    g_movie.video_started = g_movie.audio_started = g_movie.monitor_started = false;
    g_movie.audio_stream = NULL;
    g_movie.audio_subsystem = false;
    g_movie.start_ns = 0;
    atomic_store(&g_movie.stop, false);
    atomic_store(&g_movie.source_ready, false);
    atomic_store(&g_movie.playback_started, false);
    atomic_store(&g_movie.first_frame_ready, false);
    atomic_store(&g_movie.first_frame_submitted, false);
    atomic_store(&g_movie.video_done, false);
    atomic_store(&g_movie.audio_done, false);
    atomic_store(&g_movie.audio_eof, false);
    atomic_store(&g_movie.audio_clock_active, false);
    atomic_store(&g_movie.monitor_done, false);
    atomic_store(&g_movie.audio_bytes_submitted, 0);
    atomic_store(&g_movie_displayed_frame, UINT64_MAX);
    movie_frames_clear();
    renderer_movie_clear();
}

static bool movie_start(const char *guest_path) {
#ifdef __ANDROID__
    // No external executable can be launched from app data on modern Android.
    // A native movie decoder is still required. Keep the guest extractor's
    // empty track list successful so it can finish the movie lifecycle.
    (void)guest_path;
    atomic_store(&g_movie.source_ready, true);
    atomic_store(&g_movie_guest_ready, true);
    LOG("[movie] playback skipped: Android decoder not implemented\n");
    return true;
#endif
    pthread_mutex_lock(&g_movie_lock);
    if (atomic_exchange(&g_intro_prestarted, false)) {
        char resolved[PATH_MAX];
        bool same = movie_resolve_path(guest_path, resolved, sizeof resolved) && !strcmp(resolved, g_intro_path);
        g_intro_path[0] = 0;
        if (same) {
            // Also acknowledge an intro already skipped during boot. Reopening it
            // here would replay the movie and lose the player's early skip.
            atomic_store(&g_movie.source_ready, true);
            atomic_store(&g_movie_guest_ready, true);
            LOG("[movie] guest adopted the intro prepared during boot%s\n", movie_overlay_active() ? "" : " (already finished/skipped)");
            pthread_mutex_unlock(&g_movie_lock);
            return true;
        }
    }
    movie_stop_locked();

    char path[PATH_MAX];
    MovieDecoder decoder;
    if (!movie_resolve_path(guest_path, path, sizeof path)) {
        LOG("[movie] cannot resolve game movie path: %s\n", guest_path ? guest_path : "(null)");
        pthread_mutex_unlock(&g_movie_lock);
        return false;
    }
    if (!movie_frames_init()) {
        LOG("[movie] cannot allocate frame queue\n");
        pthread_mutex_unlock(&g_movie_lock);
        return false;
    }
    if (!movie_select_decoder(&decoder) ||
        !movie_spawn_decoder(path, &decoder, &g_movie.child, &g_movie.video_fd, &g_movie.audio_fd)) {
        LOG("[movie] decoder startup failed for %s (rebuild the app package or set LARECOMP_FFMPEG)\n", path);
        movie_frames_clear();
        pthread_mutex_unlock(&g_movie_lock);
        return false;
    }

    if (!(getenv("LARECOMP_NO_AUDIO") && strcmp(getenv("LARECOMP_NO_AUDIO"), "0"))) {
        g_movie.audio_subsystem = SDL_InitSubSystem(SDL_INIT_AUDIO);
        if (g_movie.audio_subsystem) {
            SDL_AudioSpec spec = {SDL_AUDIO_S16LE, MOVIE_AUDIO_CHANNELS, MOVIE_AUDIO_RATE};
            g_movie.audio_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
            if (g_movie.audio_stream) {
                double gain = g_settings.volume;
                const char *env_gain = getenv("LARECOMP_AUDIO_GAIN");
                if (env_gain) gain *= atof(env_gain);
                SDL_SetAudioStreamGain(g_movie.audio_stream, (float)gain);
            } else {
                LOG("[movie] audio device unavailable: %s (video will use the system clock)\n", SDL_GetError());
            }
        } else {
            LOG("[movie] SDL audio init failed: %s (video will use the system clock)\n", SDL_GetError());
        }
    }

    atomic_store(&g_movie.stop, false);
    atomic_store(&g_movie.audio_eof, false);
    atomic_store(&g_movie.audio_clock_active, false);
    atomic_store(&g_movie.audio_bytes_submitted, 0);
    atomic_store(&g_movie.playback_started, false);
    atomic_store(&g_movie.first_frame_ready, false);
    atomic_store(&g_movie.first_frame_submitted, false);
    g_movie.start_ns = 0;
    audio_movie_set_active(g_movie.audio_stream != NULL);
    atomic_store(&g_movie_visible, true);
    int e = pthread_create(&g_movie.audio_thread, NULL, movie_audio_reader, &g_movie);
    if (e) {
        LOG("[movie] cannot start audio reader: %s\n", strerror(e));
        movie_stop_locked();
        pthread_mutex_unlock(&g_movie_lock);
        return false;
    }
    g_movie.audio_started = true;
    e = pthread_create(&g_movie.video_thread, NULL, movie_video_reader, &g_movie);
    if (e) {
        LOG("[movie] cannot start video reader: %s\n", strerror(e));
        movie_stop_locked();
        pthread_mutex_unlock(&g_movie_lock);
        return false;
    }
    g_movie.video_started = true;
    e = pthread_create(&g_movie.monitor_thread, NULL, movie_monitor, &g_movie);
    if (e) {
        LOG("[movie] cannot start decoder monitor: %s\n", strerror(e));
        movie_stop_locked();
        pthread_mutex_unlock(&g_movie_lock);
        return false;
    }
    g_movie.monitor_started = true;
    atomic_store(&g_movie.source_ready, true);
    atomic_store(&g_movie_guest_ready, true);
    LOG("[movie] ready %s (%dx%d at %d fps); audio waits for frame zero to reach the window\n", path,
        RENDERER_MOVIE_WIDTH, RENDERER_MOVIE_HEIGHT, MOVIE_FPS);
    pthread_mutex_unlock(&g_movie_lock);
    return true;
}

void movie_preload_intro(void) {
#ifdef __ANDROID__
    return;
#endif
    if (!g_settings.show_intro || atomic_load(&g_intro_prestarted)) return;
    char path[PATH_MAX];
    if (!movie_resolve_path("intro.mp4", path, sizeof path) || !movie_start("intro.mp4")) return;
    pthread_mutex_lock(&g_movie_lock);
    snprintf(g_intro_path, sizeof g_intro_path, "%s", path);
    atomic_store(&g_intro_prestarted, true);
    atomic_store(&g_movie_guest_ready, false);
    pthread_mutex_unlock(&g_movie_lock);
    LOG("[movie] intro decoding overlaps graphics and engine startup\n");
}

bool movie_guest_ready(void) {
    return atomic_load(&g_movie_guest_ready);
}

bool movie_overlay_active(void) {
    return atomic_load(&g_movie_visible);
}

void movie_request_skip(void) {
    if (movie_overlay_active()) atomic_store(&g_movie_skip_requested, true);
}

// UI thread: request cancellation without joining workers or waiting for startup's lock.
// Reclaim the player only after its monitor has completed all blocking shutdown work.
bool movie_poll(void) {
    if (!atomic_load(&g_movie_skip_requested) && !atomic_load(&g_movie.monitor_done)) return false;
    if (pthread_mutex_trylock(&g_movie_lock)) return false;
    bool dismissed = false;
    if (atomic_exchange(&g_movie_skip_requested, false) && movie_overlay_active()) {
        LOG("[movie] skipped by confirm/back or mouse input\n");
        movie_cancel_locked();
        dismissed = true;
    }
    if (g_movie.monitor_started && atomic_load(&g_movie.monitor_done)) movie_stop_locked();
    pthread_mutex_unlock(&g_movie_lock);
    return dismissed;
}

bool movie_render_tick(void) {
    if (!movie_overlay_active()) return false;
    MoviePlayer *p = &g_movie;
    if (!atomic_load(&p->first_frame_ready)) return false;
    uint64_t target_frame;
    pthread_mutex_lock(&g_movie_lock);
    SDL_AudioStream *stream = p->audio_stream;
    bool clock_active = atomic_load(&p->audio_clock_active);
    bool audio_eof = atomic_load(&p->audio_eof);
    uint64_t submitted = atomic_load(&p->audio_bytes_submitted);
    if (!atomic_load(&p->playback_started)) {
        // Do not advance the movie or release its audio until frame zero has reached the window.
        target_frame = 0;
    } else if (stream && clock_active) {
        int queued = SDL_GetAudioStreamQueued(stream);
        uint64_t queued_bytes = queued > 0 ? (uint64_t)queued : 0;
        uint64_t consumed = submitted > queued_bytes ? submitted - queued_bytes : 0;
        target_frame = consumed * MOVIE_FPS / (MOVIE_AUDIO_RATE * MOVIE_AUDIO_CHANNELS * MOVIE_AUDIO_BYTES_PER_SAMPLE);
    } else if (stream && !audio_eof) {
        target_frame = 0;
    } else {
        uint64_t elapsed = movie_now_ns() - p->start_ns;
        target_frame = elapsed * MOVIE_FPS / 1000000000ull;
    }
    pthread_mutex_unlock(&g_movie_lock);

    pthread_mutex_lock(&g_movie_frames_lock);
    MovieFrame *best = NULL;
    MovieFrame *first = NULL;
    for (int i = 0; i < MOVIE_FRAME_COUNT; i++) {
        MovieFrame *frame = &g_movie_frames[i];
        if (!frame->valid) continue;
        if (!first || frame->index < first->index) first = frame;
        if (frame->index <= target_frame && (!best || frame->index > best->index)) best = frame;
    }
    if (!best) best = first;
    if (best && atomic_load(&g_movie_displayed_frame) != best->index &&
        renderer_movie_submit_nv12(best->pixels, RENDERER_MOVIE_WIDTH, RENDERER_MOVIE_HEIGHT,
                                   RENDERER_MOVIE_WIDTH, RENDERER_MOVIE_WIDTH * RENDERER_MOVIE_HEIGHT)) {
        atomic_store(&g_movie_displayed_frame, best->index);
        if (best->index == 0) atomic_store(&p->first_frame_submitted, true);
    }
    pthread_mutex_unlock(&g_movie_frames_lock);
    return !atomic_load(&p->playback_started) && atomic_load(&p->first_frame_submitted);
}

// Called after the swapchain present that carried frame zero completed. Audio and video start from that moment.
void movie_presented(void) {
    MoviePlayer *p = &g_movie;
    pthread_mutex_lock(&g_movie_lock);
    if (movie_overlay_active() && !atomic_load(&p->playback_started) &&
        atomic_load(&p->first_frame_submitted)) {
        p->start_ns = movie_now_ns();
        atomic_store(&p->playback_started, true);
        LOG("[movie] frame zero presented; playback started\n");
    }
    pthread_mutex_unlock(&g_movie_lock);
}

static void movie_stop_from_guest(void) {
    MoviePlayer *p = &g_movie;
    if (!movie_overlay_active() ||
        (!atomic_load(&p->playback_started) && !atomic_load(&p->first_frame_submitted))) return;
    pthread_mutex_lock(&g_movie_lock);
    if (movie_overlay_active()) {
        LOG("[movie] guest stopped playback\n");
        movie_stop_locked();
    }
    pthread_mutex_unlock(&g_movie_lock);
}

RT_ON_RESET(movie_reset) {
    pthread_mutex_lock(&g_movie_lock);
    atomic_store(&g_intro_prestarted, false);
    atomic_store(&g_movie_guest_ready, false);
    g_intro_path[0] = 0;
    movie_stop_locked();
    pthread_mutex_unlock(&g_movie_lock);
}

static uint64_t g_extractor_vtable[16];
static uint64_t g_decoder_vtable[16];
static int g_movie_inited = 0;

static void movie_dtor(Cpu *c) { (void)c; }
static void movie_set_data_source(Cpu *c) {
    const char *path = (const char *)(uintptr_t)c->x[1];
    bool started = movie_start(path);
    LOG("[movie] SetDataSource: %s (%s; host decoder owns playback)\n",
        path ? path : "(null)", started ? "host playback started" : "host playback unavailable");
    c->x[0] = started ? 0 : 0x2;
}
static void movie_get_track_count(Cpu *c) {
    // The host owns decode/presentation; expose a successful empty guest track list so the SDK
    // does not create its own decoder or fail the game's managed movie lifecycle.
    if (c->x[1]) *(int32_t *)(uintptr_t)c->x[1] = 0;
    bool source_ready = atomic_load(&g_movie.source_ready);
    LOG("[movie] GetTrackCount -> %d (%s)\n", 0, source_ready ? "host-only playback" : "no active movie");
    c->x[0] = source_ready ? 0 : 0x2;
}
static void movie_get_track_config(Cpu *c) {
    c->x[0] = 0;
}

static void init_movie_vtables(void) {
    if (g_movie_inited) return;
    g_movie_inited = 1;
    uint64_t dtor = hle_register("<movie_dtor>", movie_dtor);
    uint64_t set_ds = hle_register("<movie_set_ds>", movie_set_data_source);
    uint64_t get_tc = hle_register("<movie_get_tc>", movie_get_track_count);
    uint64_t get_cfg = hle_register("<movie_get_cfg>", movie_get_track_config);

    g_extractor_vtable[0] = dtor;
    g_extractor_vtable[1] = dtor;
    g_extractor_vtable[2] = set_ds;
    g_extractor_vtable[3] = set_ds;
    g_extractor_vtable[4] = get_tc;
    g_extractor_vtable[5] = get_cfg;

    g_decoder_vtable[0] = dtor;
    g_decoder_vtable[1] = dtor;
}

HLE_FUNC("_ZN5movie12SetAllocatorEPFPvmmS0_EPFvS0_S0_EPFS0_S0_mS0_ES0_", movie_set_allocator) {
    init_movie_vtables();
    c->x[0] = 0;
}

HLE_FUNC("_ZN5movie11SetCoreMaskEm", movie_set_core_mask) {
    init_movie_vtables();
    c->x[0] = 0;
}

HLE_FUNC("_ZN5movie17GetThreadPriorityEv", movie_get_thread_prio) {
    c->x[0] = 44;
}

HLE_FUNC("_ZN5movie9ExtractorC1ENS_13ContainerTypeE", movie_extractor_c1) {
    init_movie_vtables();
    *(uint64_t *)(uintptr_t)c->x[0] = (uint64_t)(uintptr_t)g_extractor_vtable;
    c->x[0] = 0;
}

HLE_FUNC("_ZN5movie9ExtractorC2ENS_13ContainerTypeE", movie_extractor_c2) {
    init_movie_vtables();
    *(uint64_t *)(uintptr_t)c->x[0] = (uint64_t)(uintptr_t)g_extractor_vtable;
    c->x[0] = 0;
}

HLE_FUNC("_ZN5movie9ExtractorD1Ev", movie_extractor_d1) { (void)c; movie_stop_from_guest(); }
HLE_FUNC("_ZN5movie9ExtractorD2Ev", movie_extractor_d2) { (void)c; movie_stop_from_guest(); }

HLE_FUNC("_ZN5movie9Extractor13SetDataSourceEPKc", movie_extractor_set_ds) {
    movie_set_data_source(c);
}

HLE_FUNC("_ZN5movie9Extractor13GetTrackCountEPi", movie_extractor_get_tc) {
    movie_get_track_count(c);
}

HLE_FUNC("_ZN5movie9Extractor21GetTrackConfigurationEiPNS_9MediaDataE", movie_extractor_get_cfg) {
    movie_get_track_config(c);
}

HLE_FUNC("_ZN5movie7DecoderC1ENS_11DecoderTypeENS_19DecoderOutputFormatENS_11DecoderModeEi", movie_decoder_c1) {
    init_movie_vtables();
    *(uint64_t *)(uintptr_t)c->x[0] = (uint64_t)(uintptr_t)g_decoder_vtable;
    c->x[0] = 0;
}
HLE_FUNC("_ZN5movie7DecoderD1Ev", movie_decoder_d1) { (void)c; movie_stop_from_guest(); }
HLE_FUNC("_ZN5movie7DecoderD2Ev", movie_decoder_d2) { (void)c; movie_stop_from_guest(); }

HLE_FUNC("_ZN5movie9MediaDataC1Ev", movie_media_data_c1) { (void)c; }
HLE_FUNC("_ZN5movie9MediaDataD1Ev", movie_media_data_d1) { (void)c; }
HLE_FUNC("_ZN5movie9MediaData5ClearEv", movie_media_data_clear) { (void)c; }
HLE_FUNC("_ZNK5movie9MediaData9FindInt32EPKcPi", movie_media_data_find_i32) { c->x[0] = 0; }
HLE_FUNC("_ZNK5movie9MediaData9FindInt64EPKcPl", movie_media_data_find_i64) { c->x[0] = 0; }
HLE_FUNC("_ZNK5movie9MediaData10FindStringEPKcPS2_", movie_media_data_find_str) { c->x[0] = 0; }
HLE_FUNC("_ZNK5movie9MediaData8FindRectEPKcPiS3_S3_S3_", movie_media_data_find_rect) { c->x[0] = 0; }

HLE_FUNC("_ZN5movie6BufferC1Ev", movie_buffer_c1) { (void)c; }
HLE_FUNC("_ZN5movie6BufferD1Ev", movie_buffer_d1) { (void)c; }
HLE_FUNC("_ZNK5movie6Buffer4SizeEv", movie_buffer_size) { c->x[0] = 0; }
HLE_FUNC("_ZNK5movie6Buffer6OffsetEv", movie_buffer_offset) { c->x[0] = 0; }
HLE_FUNC("_ZNK5movie6Buffer4DataEv", movie_buffer_data) { c->x[0] = 0; }
HLE_FUNC("_ZN5movie6Buffer18SetDataAndCapacityEPvm", movie_buffer_set_cap) { (void)c; }
HLE_FUNC("_ZN5movie6Buffer8SetRangeEii", movie_buffer_set_range) { (void)c; }
