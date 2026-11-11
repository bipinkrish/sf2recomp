#include "../constants/nvn.h"
// Output resolution, shared by the Metal and Vulkan backends (each calls output_scale() once per presented frame).
// Screen targets (selected native aspect at 1080 lines) are allocated at (sx, sy) times their size; the
// image is then drawn into (vx, vy, vw, vh) of the window. Video tab "Internal resolution" (settings_resolution()):
//   Match display  render at the window's own pixel size (default)
//   1x .. 4x       N times the Switch's docked image: 1080 lines x N (wide windows keep their shape: 32:9 at 2x =
//                  7680 x 2160); capped to the GPU's 16384-pixel texture limit. 1x on an integrated GPU: 720 lines,
//                  a lower base resolution suited to integrated GPUs
//   .75x / .50x / .25x  of Match display (scaled up to the window - with MetalFX on macOS when chosen)
//   Dynamic        Match display, lowered in steps while the GPU can't hold the configured target FPS and raised again.
//                  The render targets stay allocated at the Match display size (ax, ay) and the frame is drawn into
//                  their top-left part at (sx, sy): no reallocation at a step, so no hitch (each step reallocated
//                  every screen-sized texture before: ~325 ms on an M3 Max, seconds at 4K on Vulkan, issue #100).
// Test overrides: LARECOMP_SCALE=<f> (render height = 1080 x f), LARECOMP_WIDE_TEST=<f> (forced shape),
// LARECOMP_DYN_TEST=<f> (Dynamic pinned at factor f of the allocation: shots at a fixed part), LARECOMP_DYN_START=<f>
// (Dynamic starts at factor f).
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "../hle.h"
#include "../settings.h"
#include "output_scale.h"

#define MAX_TEX SF2C_OUTPUT_SCALE_MAX_TEX
// Dynamic resolution: a factor on the Match display scale, from the floor to 1 in coarse 0.1 steps. The floor: 1/3 of
// the Match display, but never under 540 lines (half the Switch's 1080): a 1080p window 0.50 (960x540), 1440 lines
// 0.375, 4K and up 1/3 (1280x720 at 4K, still 1440 lines at 8K); 540 lines or less: no scaling. It was 1/3 everywhere:
// 640x360 of a 1080p window, blurry on integrated GPUs (issue #205).
// It holds a resolution and changes only when it must: down when 3 of the last 4 half seconds were over budget or
// they averaged over 95% of it (1 s in a row when frames miss the target outright), straight to where the last second's frame is predicted to fit with room; up
// one step after 5 s in a row (no load spikes in it) with that step predicted to fit with room, at least 5 s after the
// last change, and the wait before a raise doubles each time a raise is undone within 30 s (up to a minute; reset
// after a calm minute). Before: down after one half second over, up after 3 s in 0.05 steps - it stepped all the time
// (45 changes in 6 min at 5120x1440 before the GPU time fix #197; 25 in 5 min at 7680x4320 on an M3 Max after it).
// It reacts to the GPU only: a lower resolution doesn't help a frame the CPU / the game made slow (a level streaming
// in, a shader compiling: 100+ ms frames with the GPU nearly idle, which used to step it down on a fast GPU and, with
// the slow raises, keep it low for a minute). With GPU times (Metal's command buffers, Vulkan's timestamps) it goes by
// those alone. Without, by the frame times with the spikes left out: the mean of the half second's frames up to 3x their
// median (a frame missing one vsync still counts). Either way a spike
// neither steps it down nor, so, doubles the raise wait. LARECOMP_DYN_NO_GPU=1: ignore the GPU times (tests the
// frame-time path); LARECOMP_SPIKE_MS (pace.c): CPU-side spikes.
//
// The GPU time has two parts: the game's (G, scales with the factor: about its area, factor^2) and the window step's
// (W: the image drawn into the window - upscale, SMAA - and the menu: depends on the window's size only). The budget
// is for G + W (87% of the configured target frame interval); a raise must be predicted to fit with room to spare (G x (new / old)^2 with 25%
// on top, + W: the room is for G, which moves with the scene - W is the window's and steady, and large at 4K / 8K
// with an upscaler: 7-9 ms at 7680x4320 with MetalFX on an M3 Max; no fixed raise threshold, so it settles one step
// under where it would go over instead of hunting), and a step down goes straight to where G + W is predicted to fit
// with that same room (at most 0.3 at once), so the next raise isn't predicted to fit right away.
// Held: when even the floor misses the configured target and frames wait for the display, going lower gains nothing,
// so the budget becomes 87% of the held frame interval (whole virtual refreshes) and the factor goes back up as far as
// it fits there (a large W - 4K SMAA / FSR 1 on an integrated
// GPU - or a slow GPU at a big window). A hold keeps measuring: G + W over the hold's budget for a second lowers the
// factor; at the floor for 1.5 s, the hold adds one refresh to each frame (at the default 60 Hz, 30 to 20 fps; G at
// the floor can grow after the hold began: UHD 770 4K 19.4 -> 27.9 ms, frames every 49-65 ms while it said 30). It
// steps back up only when the floor is predicted to fit the faster rate with 20% to spare for 2 s (after 10 s, doubling each
// time that fails or is undone within 30 s), and starts over when W changes (another upscaler / anti-aliasing / window
// size). Frames that don't wait (mailbox: a frame every G + W) keep going down to the floor: there every step down is
// a faster frame. tools/test_dynres.py: the controller against a simulated GPU (the UHD 770 cases).
#define DYN_MAX_SAMPLES SF2C_OUTPUT_SCALE_N_180_7B69 // up to half a second at the maximum configured 360 fps
#define DYN_FLOOR_LINES SF2C_OUTPUT_SCALE_N_540_0_EC0E     // the floor: at least this many render lines...
#define DYN_FLOOR_SHARE (1.0 / SF2C_OUTPUT_SCALE_N_3_4E07)  // ... and at least this share of the Match display's
#define DYN_STEP SF2C_OUTPUT_SCALE_N_0_1_14BE        // the factor's grid (the floor below its first step)
#define DYN_RAISE_FIT SF2C_OUTPUT_SCALE_N_0_9_8139   // a hold's raise: predicted G + W under this share of the budget
#define DYN_UP_FIT SF2C_OUTPUT_SCALE_N_0_8_1E9D      // a raise / a step down's target (not held): predicted G / this + W within the budget
#define DYN_DOWN_VOTES SF2C_OUTPUT_SCALE_N_3_4E07    // decisions over budget out of the last 4 (2 s) before a step down
#define DYN_DOWN_FAST SF2C_OUTPUT_SCALE_N_2_D473     // ... in a row when the frames miss the target outright (1 s)
#define DYN_UP_VOTES SF2C_OUTPUT_SCALE_N_10_4A44     // decisions in a row with the next step predicted to fit before a raise (5 s)
#define DYN_RAISE_WAIT SF2C_OUTPUT_SCALE_N_5_0_A19A  // s from the last change to a raise (doubles while raises are undone, up to 60)
#define DYN_MAX_DOWN SF2C_OUTPUT_SCALE_N_0_3_2217    // the most a step down lowers the factor at once
#define DYN_HOLD_UP_FIT SF2C_OUTPUT_SCALE_N_0_8_1E9D // a hold steps up: the floor's predicted G + W under this share of the faster rate's budget
#define DYN_HOLD_MAX SF2C_OUTPUT_SCALE_N_3_4E07      // virtual refreshes a hold waits at most
static double g_dyn = 1.0;
static double g_dyn_floor = DYN_FLOOR_SHARE;  // (set from the window: dyn_set_floor)
static unsigned g_down_hist;  // the last decisions over budget (bit 0: the latest)
static double g_down_gw[SF2C_OUTPUT_SCALE_N_4_4B22];   // and their G + W
static int g_down_fast_votes;  // decisions in a row missing the configured target outright
static double g_prev_G = -1, g_prev_W;  // the decision before's G and W (a step down's target: the two averaged)
static double g_dyn_gpu_sum, g_dyn_win_sum, g_last_t, g_dyn_dt[DYN_MAX_SAMPLES];
static int g_dyn_n, g_dyn_gpu_n, g_dyn_good;
static double g_dyn_cool_until, g_dyn_raise_wait = DYN_RAISE_WAIT, g_dyn_raise_at;
static double g_dyn_last_raise = -SF2C_OUTPUT_SCALE_N_100_AD57, g_dyn_last_lower = -SF2C_OUTPUT_SCALE_N_100_AD57;
static double g_hold_dt, g_hold_w;               // held: the frame interval at the floor (> 0) and W then
static double g_probe_at, g_probe_wait = SF2C_OUTPUT_SCALE_N_10_0_F1E4;  // looking for the configured target again (from a hold)
static bool g_probing;
static double g_probe_start = -SF2C_OUTPUT_SCALE_N_100_AD57, g_probe_ok_at = -SF2C_OUTPUT_SCALE_N_100_AD57;  // when the last target-rate probe began / was last met
static int g_probe_good;                                     // probing: decisions in a row that met the target
static double g_probe_from;                                  // the factor the hold had when the probe began
static double g_probe_fail_gw, g_probe_fail_at = -SF2C_OUTPUT_SCALE_N_1000_4051;      // the last failed probe: G + W at the floor then, when
#define DYN_PROBE_MAX SF2C_OUTPUT_SCALE_N_60_0_DB58  // s: the longest wait between target-rate probes from a hold
// held: G at the floor followed through the scene's load - measured there as the hold began, then scaled by how G
// changed at each factor since (G right after a step is the reference at the new factor): the part of G that
// doesn't scale with the resolution makes area estimates optimistic (UHD 770 1080p: ~6.5 of 9.1 ms at 0.50)
static double g_floor_g, g_ref_g;
static bool g_ref_pending;
static int g_hold_miss, g_hold_up_votes;  // held: decisions in a row over the hold's budget / with room for the next rate
static int g_hold_met_votes;              // held at three refreshes: frames met the next faster two-refresh rate
static double g_hold_up_at = -SF2C_OUTPUT_SCALE_N_100_AD57;        // when the hold last stepped up

static int target_fps(void) {
    return g_settings.target_fps >= SF2C_OUTPUT_SCALE_N_30_624B && g_settings.target_fps <= SF2C_OUTPUT_SCALE_N_360_838F ? g_settings.target_fps : SF2C_OUTPUT_SCALE_N_60_39FA;
}
static double target_frame_ms(void) { return SF2C_OUTPUT_SCALE_N_1000_0_4250 / target_fps(); }
static int dyn_sample_count(void) { return target_fps() / SF2C_OUTPUT_SCALE_N_2_D473; }
static int hold_vsyncs(void) { return (int)lround(g_hold_dt / target_frame_ms()); }
static bool at_floor(void) { return g_dyn <= g_dyn_floor + SF2C_OUTPUT_SCALE_N_1E_6_F465; }
// the grid, the floor below its first step (and no step closer than half a step above the floor)
static double dyn_grid(double f) {
    if (f <= g_dyn_floor + DYN_STEP / SF2C_OUTPUT_SCALE_N_2_D473 - SF2C_OUTPUT_SCALE_N_1E_6_F465) return g_dyn_floor;
    return fmin(1.0, fmax(g_dyn_floor, round(f / DYN_STEP) * DYN_STEP));
}
// the factor where G + W is predicted to fit `budget` (G scales with the area), on the grid
static double dyn_fit(double G, double W, double budget) {
    double fit = g_dyn * sqrt(fmax(0, budget - W) / G);
    double f = floor(fit / DYN_STEP + SF2C_OUTPUT_SCALE_N_1E_6_F465) * DYN_STEP;
    return f < g_dyn_floor + DYN_STEP / SF2C_OUTPUT_SCALE_N_2_D473 - SF2C_OUTPUT_SCALE_N_1E_6_F465 ? g_dyn_floor : f;
}
// the floor for a Match display of `lines` render lines (1 when that is 540 or less)
static void dyn_set_floor(double lines) {
    double f = fmin(1.0, fmax(DYN_FLOOR_SHARE, DYN_FLOOR_LINES / fmax(lines, 1)));
    if (fabs(f - g_dyn_floor) < SF2C_OUTPUT_SCALE_N_1E_9_102F) return;
    g_dyn_floor = f;
    if (g_dyn < f) g_dyn = f;
    else g_dyn = dyn_grid(g_dyn);
}

// The window's size changed (fullscreen <-> windowed, a resize): every scalable target is reallocated and what was
// measured belongs to the old size. The factor keeps the render height (to the nearest step), so the game's part of
// the GPU time stays about the same. Before, the factor stayed as it was: a window made larger missed the target until
// slow frames stepped it down, one made smaller sat blurry until a raise, and a hold on an integrated GPU
// ended because the window step's time changed, so the factor went down to the floor and climbed back over the next
// seconds - the image changed sharpness for a while after every switch (issue #205). A hold now keeps its frame rate
// (its floor's G scaled to the new floor, the window step measured again, a probe for the target a probe wait away); the
// votes start over, the raise wait is back at its first 5 s, and the next decision comes 1 s after the change, from
// frames at the new size only (a window dragged to size: none until it stops).
static void dyn_window_changed(double now, double from, double to) {
    double prev = g_dyn, prev_floor_lines = g_dyn_floor * from;
    dyn_set_floor(to);
    g_dyn = dyn_grid(prev * from / to);
    if (g_hold_dt > 0) {
        double r = g_dyn_floor * to / prev_floor_lines;
        g_floor_g *= r * r;
        g_ref_pending = true;  // (G's reference at the new size: the first decision there)
        g_hold_w = -1;         // (the window step's time at the new size: the first decision there)
        g_hold_miss = g_hold_up_votes = g_hold_met_votes = 0;
        g_probe_at = fmax(g_probe_at, now + g_probe_wait);  // (a target-rate probe: once the hold has measured the new size)
    }
    g_down_hist = 0, g_down_fast_votes = 0, g_dyn_good = 0, g_prev_G = -1;
    memset(g_down_gw, 0, sizeof g_down_gw);
    g_dyn_raise_wait = DYN_RAISE_WAIT, g_dyn_raise_at = now + DYN_RAISE_WAIT, g_dyn_last_raise = g_dyn_last_lower = -SF2C_OUTPUT_SCALE_N_100_AD57;
    g_dyn_cool_until = now + 1.0;
    g_dyn_n = g_dyn_gpu_n = 0, g_dyn_gpu_sum = g_dyn_win_sum = 0;
    LOG(SF2C_OUTPUT_SCALE_S_DYNRES_WINDOW_0F_0F_LINES_2F_9E23, from, to, prev, g_dyn, prev * from,
        g_dyn * to, g_hold_dt > 0 ? SF2C_OUTPUT_SCALE_S_THE_HOLD_GOES_ON_E5EE : SF2C_OUTPUT_SCALE_S_EMPTY_12AE);
}

static DynSplit g_split;  // sums since the last decision
static int g_split_n;
void output_scale_note_split(const DynSplit *d) {
    if (g_split_n >= SF2C_OUTPUT_SCALE_N_600_284B) { memset(&g_split, 0, sizeof g_split); g_split_n = 0; }  // (not deciding: Dynamic is off)
    g_split.game += d->game; g_split.win += d->win; g_split.span += d->span; g_split.frames += d->frames;
    g_split.w_slot += d->w_slot; g_split.w_acquire += d->w_acquire; g_split.w_present += d->w_present; g_split.w_other += d->w_other;
    g_split.win_counted = d->win_counted;
    g_split_n++;
}

static double g_sum_ms;  // (Metal) spans summed since the last decision
static int g_sum_n;
void output_scale_note_sum(double ms) {
    if (g_sum_n >= SF2C_OUTPUT_SCALE_N_600_284B) g_sum_ms = 0, g_sum_n = 0;
    g_sum_ms += ms, g_sum_n++;
}

static int cmp_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

// game_ms: the game's GPU time of the last frame (< 0: unknown); win_ms: the window step's (< 0: unknown / not counted)
static void dyn_update(double now, double game_ms, double win_ms, double lines) {
    static int no_gpu = -1;
    if (no_gpu < 0) no_gpu = getenv(SF2C_OUTPUT_SCALE_S_LARECOMP_DYN_NO_GPU_D34F) && atoi(getenv(SF2C_OUTPUT_SCALE_S_LARECOMP_DYN_NO_GPU_D34F));
    if (no_gpu) game_ms = -1;
    static double prev_lines;
    if (prev_lines > 0 && fabs(lines - prev_lines) > SF2C_OUTPUT_SCALE_N_0_5_D2CB) dyn_window_changed(now, prev_lines, lines);
    prev_lines = lines;
    dyn_set_floor(lines);
    static int start = -1;  // LARECOMP_DYN_START=<f>: Dynamic starts at factor f (tests: climbing back from the floor)
    if (start < 0) {
        start = 0;
        if (getenv(SF2C_OUTPUT_SCALE_S_LARECOMP_DYN_START_0C8E)) g_dyn = dyn_grid(fmin(1.0, atof(getenv(SF2C_OUTPUT_SCALE_S_LARECOMP_DYN_START_0C8E))));
    }
    double frame_ms = target_frame_ms();
    int samples = dyn_sample_count();
    double dt = g_last_t > 0 ? (now - g_last_t) * SF2C_OUTPUT_SCALE_N_1000_0_4250 : frame_ms;
    g_last_t = now;
    if (dt > SF2C_OUTPUT_SCALE_N_250_1E47) return;  // (loading, menus, a stall: not the GPU's steady state)
    g_dyn_dt[g_dyn_n++] = dt;
    if (game_ms >= 0) { g_dyn_gpu_sum += game_ms; g_dyn_win_sum += fmax(0, win_ms); g_dyn_gpu_n++; }
    if (g_dyn_n < samples) return;  // decide every ~half second at the configured target
    // frame times: median, and the mean without the outliers (single CPU-side spikes)
    double sorted[DYN_MAX_SAMPLES], kept = 0;
    memcpy(sorted, g_dyn_dt, (size_t)samples * sizeof *sorted);
    qsort(sorted, samples, sizeof *sorted, cmp_double);
    double med = samples & 1 ? sorted[samples / SF2C_OUTPUT_SCALE_N_2_D473] : (sorted[samples / SF2C_OUTPUT_SCALE_N_2_D473 - 1] + sorted[samples / SF2C_OUTPUT_SCALE_N_2_D473]) / SF2C_OUTPUT_SCALE_N_2_D473;
    int nkept = 0;
    for (int i = 0; i < samples; i++) if (sorted[i] <= SF2C_OUTPUT_SCALE_N_3_4E07 * med) { kept += sorted[i]; nkept++; }
    bool gpu = g_dyn_gpu_n >= (samples + 1) / SF2C_OUTPUT_SCALE_N_2_D473;
    double avg_dt = kept / nkept, G = gpu ? g_dyn_gpu_sum / g_dyn_gpu_n : -1, W = gpu ? g_dyn_win_sum / g_dyn_gpu_n : 0;
    double avg_gpu = gpu ? G + W : -1;
    int spikes = samples - nkept;
    g_dyn_gpu_sum = g_dyn_win_sum = 0;
    g_dyn_n = g_dyn_gpu_n = 0;
    double sum_ms = g_sum_n ? g_sum_ms / g_sum_n : -1;
    g_sum_ms = 0, g_sum_n = 0;
    if (now < g_dyn_cool_until) return;
    double prev = g_dyn;
    static int log = -1;  // LARECOMP_LOG_DYNRES=1: every decision, not only the steps
    if (log < 0) log = getenv(SF2C_OUTPUT_SCALE_S_LARECOMP_LOG_DYNRES_73A7) != NULL;
    double budget = frame_ms * SF2C_OUTPUT_SCALE_N_0_87_36C2;
    bool hold_up = false;  // (a held rate stepped up: the factor was set to fit it already)
    if (gpu) {
        if (g_hold_dt > 0 && g_hold_w < 0) g_hold_w = W;  // (the window's size changed: its step's time there)
        else if (g_hold_dt > 0 && fabs(W - g_hold_w) > fmax(1.0, SF2C_OUTPUT_SCALE_N_0_25_A30A * g_hold_w)) {  // another upscaler / AA
            LOG(SF2C_OUTPUT_SCALE_S_DYNRES_WINDOW_STEP_1F_1F_MS__CE3D, g_hold_w, W, SF2C_OUTPUT_SCALE_N_1000_4051 / g_hold_dt);
            g_hold_dt = 0, g_probing = false, g_probe_wait = SF2C_OUTPUT_SCALE_N_10_0_F1E4, g_hold_miss = g_hold_up_votes = g_hold_met_votes = 0;
        }
        // held at three refreshes while frames already come at two, with G + W inside that hold's budget (10% to spare)
        // for 1 s: resume two-refresh frames right away (no probe wait - unless the last step up was undone)
        int hvs = g_hold_dt > 0 ? hold_vsyncs() : 0;
        double faster = (hvs - 1) * frame_ms;
        bool met = hvs > SF2C_OUTPUT_SCALE_N_2_D473 && avg_dt <= SF2C_OUTPUT_SCALE_N_1_05_00FE * faster && avg_gpu <= faster * SF2C_OUTPUT_SCALE_N_0_87_36C2 * DYN_RAISE_FIT;
        g_hold_met_votes = met ? g_hold_met_votes + 1 : 0;
        if (g_hold_dt > 0 && g_ref_pending) g_ref_g = G, g_ref_pending = false;  // (the first measure at a new factor)
        if (g_hold_dt > 0 && g_ref_g > 0) {
            // the next faster rate (one fewer refresh per frame); the floor must fit it with room for 2 s in a row
            double g_floor = g_floor_g * G / g_ref_g;
            int vs = hvs;
            double up_budget = vs > SF2C_OUTPUT_SCALE_N_2_D473 ? (vs - 1) * frame_ms * SF2C_OUTPUT_SCALE_N_0_87_36C2 : budget;
            bool room = g_floor + W <= up_budget * DYN_HOLD_UP_FIT;
            g_hold_up_votes = room ? g_hold_up_votes + 1 : 0;
            bool met_now = met && g_hold_met_votes >= SF2C_OUTPUT_SCALE_N_2_D473 && (g_probe_wait <= SF2C_OUTPUT_SCALE_N_10_0_F1E4 || now >= g_probe_at);
            if ((room && g_hold_up_votes >= SF2C_OUTPUT_SCALE_N_4_4B22 && now >= g_probe_at) || met_now) {
                g_hold_up_votes = g_hold_miss = g_hold_met_votes = 0;
                if (vs > SF2C_OUTPUT_SCALE_N_2_D473) {
                    g_hold_dt = (vs - 1) * frame_ms;
                    g_hold_up_at = now, g_probe_at = now + g_probe_wait;
                    double to = fmin(g_dyn, dyn_fit(G, W, g_hold_dt * SF2C_OUTPUT_SCALE_N_0_87_36C2 * DYN_RAISE_FIT));
                    if (met_now)
                        LOG(SF2C_OUTPUT_SCALE_S_DYNRES_FRAMES_ALREADY_MEET_0_7271
                            SF2C_OUTPUT_SCALE_S_0F_FPS_N_2901, SF2C_OUTPUT_SCALE_N_1000_4051 / g_hold_dt, avg_dt, G, W, g_dyn, SF2C_OUTPUT_SCALE_N_1000_4051 / g_hold_dt);
                    else
                        LOG(SF2C_OUTPUT_SCALE_S_DYNRES_THE_FLOOR_WOULD_FIT_0_4569
                            SF2C_OUTPUT_SCALE_S_0F_FPS_2F_2F_N_1DF8, SF2C_OUTPUT_SCALE_N_1000_4051 / g_hold_dt, G, g_dyn, g_floor, g_dyn_floor, W, SF2C_OUTPUT_SCALE_N_1000_4051 / g_hold_dt, g_dyn, to);
                    if (to != g_dyn) g_floor_g *= G / g_ref_g, g_ref_pending = true, g_dyn = to;
                    hold_up = true;
                } else {
                    LOG(SF2C_OUTPUT_SCALE_S_DYNRES_THE_FLOOR_WOULD_FIT_D_67AC,
                        target_fps(), G, g_dyn, g_floor, g_dyn_floor, W);
                    g_hold_dt = 0, g_probing = true, g_probe_start = now, g_probe_good = 0;
                }
            }
        }
        // Held at two refreshes with the GPU 30% idle or more: G may be inflated by a lowered clock (an integrated GPU
        // clocks down while frames wait for the display), so the hold's own measure may not show the target fitting
        // again. Every probe wait (10 s, doubling each time a probe fails, at most a minute) it tries the target at the
        // floor; a probe that fails is back in the hold within ~1.5 s at the factor it had. After a failed probe
        // the next one waits until the floor's estimate is 10% under what that probe measured there (the scene got
        // lighter) or 2 minutes have passed: a hold that is real (SMAA + FSR 1 at 1080p on the UHD 770) probes rarely.
        double floor_gw = g_ref_g > 0 ? g_floor_g * G / g_ref_g + W : -1;
        bool probe_ok = now - g_probe_fail_at > SF2C_OUTPUT_SCALE_N_120_0_1849 || (floor_gw > 0 && floor_gw < SF2C_OUTPUT_SCALE_N_0_9_8139 * g_probe_fail_gw);
        if (g_hold_dt > 0 && !hold_up && hold_vsyncs() == SF2C_OUTPUT_SCALE_N_2_D473 && avg_gpu < SF2C_OUTPUT_SCALE_N_0_7_973B * g_hold_dt && now >= g_probe_at && probe_ok) {
            LOG(SF2C_OUTPUT_SCALE_S_DYNRES_HELD_AT_0F_FPS_WITH_T_9D95
                SF2C_OUTPUT_SCALE_S_TRYING_D_AT_2F_N_3C28, SF2C_OUTPUT_SCALE_N_1000_4051 / g_hold_dt, SF2C_OUTPUT_SCALE_N_100_AD57 * avg_gpu / g_hold_dt, G, W, target_fps(), g_dyn, g_dyn_floor);
            g_hold_dt = 0, g_probing = true, g_probe_start = now, g_probe_good = 0;
            g_hold_up_votes = g_hold_miss = g_hold_met_votes = 0;
            g_probe_from = g_dyn, g_dyn = g_dyn_floor;
        }
        if (g_hold_dt > 0) budget = g_hold_dt * SF2C_OUTPUT_SCALE_N_0_87_36C2;
        // The target is out of reach at the floor and frames wait for the display: lower gains nothing, so hold a whole
        // number of virtual refreshes (at most 3) to avoid spending GPU time on frames the display cannot show.
        double vsyncs = fmin(floor(avg_dt / frame_ms + SF2C_OUTPUT_SCALE_N_0_1_14BE), DYN_HOLD_MAX);
        static int votes;  // (two half seconds in a row)
        bool held_back = g_hold_dt == 0 && at_floor() && avg_gpu > budget && vsyncs >= SF2C_OUTPUT_SCALE_N_2_D473 && avg_dt >= SF2C_OUTPUT_SCALE_N_1_25_004A * avg_gpu &&
                         now - g_probe_start >= SF2C_OUTPUT_SCALE_N_1_5_9F29;  // (a probe: the clock comes back up first)
        votes = held_back ? votes + 1 : 0;
        if (held_back && votes >= SF2C_OUTPUT_SCALE_N_2_D473) {
            g_hold_dt = vsyncs * frame_ms, g_hold_w = W;
            g_floor_g = g_ref_g = G, g_ref_pending = false;
            g_hold_miss = g_hold_up_votes = g_hold_met_votes = 0;
            // a probe that failed, or one met a moment ago that didn't last: wait longer for the next
            if (g_probing || now - g_probe_ok_at < SF2C_OUTPUT_SCALE_N_30_0_26C9) g_probe_wait = fmin(g_probe_wait * SF2C_OUTPUT_SCALE_N_2_D473, DYN_PROBE_MAX);
            if (g_probing && now - g_probe_start < SF2C_OUTPUT_SCALE_N_5_0_A19A) g_probe_fail_gw = G + W, g_probe_fail_at = now;
            if (g_probing && now - g_probe_start < SF2C_OUTPUT_SCALE_N_5_0_A19A && g_probe_from > g_dyn) {  // (a probe failed: the hold's factor again)
                LOG(SF2C_OUTPUT_SCALE_S_DYNRES_TARGET_D_FPS_DIDN_T_F_B85E, target_fps(), g_probe_from);
                g_ref_pending = true;  // (the floor's G as just measured; G's reference at that factor next)
                g_dyn = g_probe_from;
            }
            g_probing = false;
            g_probe_at = now + g_probe_wait;
            budget = g_hold_dt * SF2C_OUTPUT_SCALE_N_0_87_36C2;
            if (W >= SF2C_OUTPUT_SCALE_N_0_4_F3E3 * frame_ms || W >= G)
                LOG(SF2C_OUTPUT_SCALE_S_DYNRES_WINDOW_STEP_1F_MS_IS__606C
                    SF2C_OUTPUT_SCALE_S_REACH_HOLDING_0F_FPS_THE_FAC_FD61, W, G, g_dyn, avg_dt,
                    target_fps(), SF2C_OUTPUT_SCALE_N_1000_4051 / g_hold_dt, budget);
            else
                LOG(SF2C_OUTPUT_SCALE_S_DYNRES_GAME_1F_WINDOW_1F_MS__C652
                    SF2C_OUTPUT_SCALE_S_FPS_THE_FACTOR_GOES_UP_WHILE_B2E1, G, W, g_dyn, avg_dt, target_fps(), SF2C_OUTPUT_SCALE_N_1000_4051 / g_hold_dt, budget);
        }
        // a probe met the target: two decisions in a row (the game's own frames may still be coming from a lower-rate mode)
        g_probe_good = g_probing && g_hold_dt == 0 && avg_gpu <= budget && avg_dt < SF2C_OUTPUT_SCALE_N_1_2_77AC * frame_ms ? g_probe_good + 1 : 0;
        if (g_probe_good >= SF2C_OUTPUT_SCALE_N_2_D473) g_probing = false, g_probe_good = 0, g_probe_ok_at = now, g_probe_wait = SF2C_OUTPUT_SCALE_N_10_0_F1E4;
    }
    bool slow = gpu ? avg_gpu > budget : avg_dt > SF2C_OUTPUT_SCALE_N_1_044_3676 * frame_ms;
    // held, but the frames came slower than the hold's (the display waits one more refresh): over budget too
    if (gpu && g_hold_dt > 0 && avg_dt > SF2C_OUTPUT_SCALE_N_1_2_77AC * g_hold_dt && avg_gpu > SF2C_OUTPUT_SCALE_N_0_8_1E9D * budget) slow = true;
    bool hold_wait = hold_up;  // held and over its budget, not yet long enough to act on it (or the hold just stepped up)
    if (gpu && g_hold_dt > 0) {
        g_hold_miss = slow && !hold_up ? g_hold_miss + 1 : 0;
        if (slow && at_floor() && g_hold_miss >= SF2C_OUTPUT_SCALE_N_3_4E07) {  // nothing lower: add another virtual refresh to the held interval
            int vs = hold_vsyncs();
            if (vs < DYN_HOLD_MAX) {
                LOG(SF2C_OUTPUT_SCALE_S_DYNRES_GAME_1F_WINDOW_1F_MS__6B80
                    SF2C_OUTPUT_SCALE_S_HOLDING_0F_FPS_N_CA31, G, W, g_dyn, SF2C_OUTPUT_SCALE_N_1000_4051 / g_hold_dt, budget, avg_dt, target_fps() / (double)(vs + 1));
                if (now - g_hold_up_at < SF2C_OUTPUT_SCALE_N_30_0_26C9) g_probe_wait = fmin(g_probe_wait * SF2C_OUTPUT_SCALE_N_2_D473, DYN_PROBE_MAX);  // (the step up didn't hold)
                g_hold_dt = (vs + 1) * frame_ms;
                budget = g_hold_dt * SF2C_OUTPUT_SCALE_N_0_87_36C2;
                g_floor_g = g_ref_g = G, g_ref_pending = false;
                g_probe_at = now + g_probe_wait;
                g_hold_up_votes = 0;
            }
            g_hold_miss = 0;
            hold_wait = true;
        } else if (slow && (at_floor() || g_hold_miss < SF2C_OUTPUT_SCALE_N_2_D473)) hold_wait = true;
        if (slow && !hold_wait) g_hold_miss = 0;
    }
    // over budget for a while (not held: a hold acts on its own misses above): 3 of the last 4 decisions, or their G + W
    // averaging over 95% of the budget (a load near the budget alternates over / under from one half second to the
    // next while frames already miss: M3 Max at 7680x4320, G + W 12 / 15.5 / 12.4 / 15.9 ms, frames every 17-19 ms).
    // Frames missing vsync often with the GPU near the budget count as over too. The
    // frames missing the target outright (G + W over a whole refresh) act sooner.
    bool must = false;
    if (g_hold_dt == 0 && !hold_wait) {
        bool over = slow || (gpu && avg_dt > SF2C_OUTPUT_SCALE_N_1_2_77AC * frame_ms && avg_gpu > SF2C_OUTPUT_SCALE_N_0_8_1E9D * budget);
        if (over && !slow) slow = true;  // (the steps below go by `slow`)
        g_down_hist = (g_down_hist << 1 | over) & SF2C_OUTPUT_SCALE_N_15_E629;
        memmove(g_down_gw + 1, g_down_gw, SF2C_OUTPUT_SCALE_N_3_4E07 * sizeof *g_down_gw);
        g_down_gw[0] = gpu ? avg_gpu : 0;
        double mean4 = (g_down_gw[0] + g_down_gw[1] + g_down_gw[SF2C_OUTPUT_SCALE_N_2_D473] + g_down_gw[SF2C_OUTPUT_SCALE_N_3_4E07]) / SF2C_OUTPUT_SCALE_N_4_4B22;
        bool missing = gpu ? avg_gpu > budget / SF2C_OUTPUT_SCALE_N_0_87_36C2 : avg_dt > SF2C_OUTPUT_SCALE_N_1_2_77AC * frame_ms;
        g_down_fast_votes = missing ? g_down_fast_votes + 1 : 0;
        must = __builtin_popcount(g_down_hist) >= DYN_DOWN_VOTES || g_down_fast_votes >= DYN_DOWN_FAST ||
               (slow && g_down_gw[SF2C_OUTPUT_SCALE_N_3_4E07] > 0 && mean4 > SF2C_OUTPUT_SCALE_N_0_95_22FA * budget);
    }
    if (slow && !at_floor() && g_hold_dt == 0 && !hold_wait && !must) g_dyn_good = 0;  // (over: waiting to be sure)
    else if (hold_wait || (slow && at_floor())) g_dyn_good = 0;  // (at the floor: nothing lower)
    else if (slow) {
        double down = g_dyn - DYN_STEP;
        if (gpu && G > 0) {  // straight to where G + W is predicted to fit, with the room a raise would need (G and W of
                             // the last second: one half second's peak would overshoot it, and a raise would follow)
            double g2 = g_prev_G > 0 ? (G + g_prev_G) / SF2C_OUTPUT_SCALE_N_2_D473 : G, w2 = g_prev_G > 0 ? (W + g_prev_W) / SF2C_OUTPUT_SCALE_N_2_D473 : W;
            down = fmin(down, g_hold_dt > 0 ? dyn_fit(g2, w2, budget * DYN_RAISE_FIT) : dyn_fit(g2 / DYN_UP_FIT, w2, budget));
            down = fmax(down, g_dyn - DYN_MAX_DOWN);
        }
        g_dyn = fmax(g_dyn_floor, down);
        g_dyn_good = 0;
        g_down_hist = 0, g_down_fast_votes = 0;
        memset(g_down_gw, 0, sizeof g_down_gw);
        // the raise didn't hold (spikes can't undo one any more: they're not in the measure)
        if (now - g_dyn_last_raise < SF2C_OUTPUT_SCALE_N_30_0_26C9) g_dyn_raise_wait = fmin(g_dyn_raise_wait * SF2C_OUTPUT_SCALE_N_2_D473, SF2C_OUTPUT_SCALE_N_60_0_DB58);
        g_dyn_last_lower = now;
        g_dyn_raise_at = now + g_dyn_raise_wait;
    } else if (g_dyn < 1.0) {
        // headroom for a while (every decision of it), and not too soon after the last change. With GPU timing: the
        // next step is predicted to fit with room (G scales with the area); without: frames at the target rate
        double up = dyn_grid(g_dyn + DYN_STEP);
        double g_up = G * (up / g_dyn) * (up / g_dyn);
        bool fits = !gpu ? avg_dt < SF2C_OUTPUT_SCALE_N_1_014_62E4 * frame_ms : g_hold_dt > 0 ? g_up + W <= budget * DYN_RAISE_FIT : g_up / DYN_UP_FIT + W <= budget;
        // (a decision with spikes - a level loading, a fade, a cutscene starting - says little about the scene ahead:
        // it starts the count over, so a raise doesn't come from a light loading screen)
        g_dyn_good = fits && !spikes ? g_dyn_good + 1 : 0;
        bool room = fits && g_dyn_good >= DYN_UP_VOTES && now >= g_dyn_raise_at;
        if (room) {
            g_dyn = up, g_dyn_good = 0, g_down_hist = 0, g_dyn_last_raise = now, g_dyn_raise_at = now + g_dyn_raise_wait;
            memset(g_down_gw, 0, sizeof g_down_gw);
        }
        else if (now - g_dyn_last_lower > SF2C_OUTPUT_SCALE_N_60_0_DB58 && g_dyn_raise_wait > DYN_RAISE_WAIT) g_dyn_raise_wait = DYN_RAISE_WAIT;  // (a minute of calm: forget the hunting)
    }
    g_dyn = dyn_grid(g_dyn);
    g_prev_G = gpu && g_dyn == prev ? G : -1, g_prev_W = W;  // (a change: the next decision measures the new factor)
    if (gpu && g_hold_dt > 0 && g_dyn != prev && !g_ref_pending && g_ref_g > 0)  // the floor's G as of now; G's new reference next time
        g_floor_g *= G / g_ref_g, g_ref_pending = true;
    if (g_dyn != prev || log) {
        if (g_dyn != prev) g_dyn_cool_until = now + 1.0;
        char split[SF2C_OUTPUT_SCALE_N_320_8882] = SF2C_OUTPUT_SCALE_S_EMPTY_12AE;
        if (g_split_n) {  // (Vulkan) per frame: GPU by part, and where the CPU waited
            double n = g_split_n, nf = g_split.frames > 0 ? g_split.frames : n;
            snprintf(split, sizeof split, SF2C_OUTPUT_SCALE_S_PER_FRAME_GPU_GAME_1F_WINDOW_00FD
                     SF2C_OUTPUT_SCALE_S_PRESENT_1F_OTHER_1F_MS_821F, g_split.game / n, g_split.win / n, g_split.win_counted ? SF2C_OUTPUT_SCALE_S_EMPTY_12AE : SF2C_OUTPUT_SCALE_S_NOT_COUNTED_D9D8,
                     g_split.span / nf, g_split.span / nf - (g_split.game + g_split.win) / n, g_split.w_slot / n, g_split.w_acquire / n,
                     g_split.w_present / n, g_split.w_other / n);
        }
        if (sum_ms >= 0) snprintf(split, sizeof split, SF2C_OUTPUT_SCALE_S_SPANS_SUMMED_1F_MS_308F, sum_ms);
        char hold[SF2C_OUTPUT_SCALE_N_48_9801] = SF2C_OUTPUT_SCALE_S_EMPTY_12AE;
        if (g_hold_dt > 0) snprintf(hold, sizeof hold, SF2C_OUTPUT_SCALE_S_HOLDING_0F_FPS_DF6A, SF2C_OUTPUT_SCALE_N_1000_4051 / g_hold_dt);
        LOG(SF2C_OUTPUT_SCALE_S_DYNRES_2F_2F_FRAME_1F_MS_MED_FA0C
            SF2C_OUTPUT_SCALE_S_BUDGET_1F_S_NEXT_RAISE_IN_0F_63B4, prev, g_dyn, avg_dt, med, spikes, spikes == 1 ? SF2C_OUTPUT_SCALE_S_EMPTY_12AE : SF2C_OUTPUT_SCALE_S_S_0F46, avg_gpu, G, W,
            budget, hold, fmax(0, g_dyn_raise_at - now), split);
    }
    memset(&g_split, 0, sizeof g_split);
    g_split_n = 0;
}

double output_scale_dynamic_factor(void) { return g_dyn; }
double output_scale_dynamic_hold_fps(void) { return g_hold_dt > 0 ? SF2C_OUTPUT_SCALE_N_1000_4051 / g_hold_dt : 0; }

void output_scale_log(unsigned long long frame, double dw, double dh, const OutScale *o) {
    static double psx, psy, pdw, pdh;
    static int nlog;
    if ((o->sx != psx || o->sy != psy || dw != pdw || dh != pdh) && nlog++ < SF2C_OUTPUT_SCALE_N_200_27BA)
        LOG(SF2C_OUTPUT_SCALE_S_SCALE_FRAME_LLU_DRAWABLE_0FX_9B84, frame, dw, dh, o->sx, o->sy,
            o->shape != 1.0);
    psx = o->sx, psy = o->sy, pdw = dw, pdh = dh;
}

void output_fit(double dw, double dh, double sw, double sh, OutputRect *rect) {
    double fit = fmin(dw / sw, dh / sh);
    rect->w = sw * fit; rect->h = sh * fit;
    rect->x = (dw - rect->w) / SF2C_OUTPUT_SCALE_N_2_D473; rect->y = (dh - rect->h) / SF2C_OUTPUT_SCALE_N_2_D473;
}

void output_scale(double dw, double dh, bool reshape_allowed, double now, double game_ms, double win_ms, OutScale *o) {
    double base_width = settings_screen_width(SF2C_OUTPUT_SCALE_N_1080_32EB);
    double fit = fmin(dw / base_width, dh / SF2C_OUTPUT_SCALE_N_1080_0_AF38);
    static double forced = -1, wide_test = -1;
    if (forced < 0) forced = getenv(SF2C_OUTPUT_SCALE_S_LARECOMP_SCALE_2675) ? atof(getenv(SF2C_OUTPUT_SCALE_S_LARECOMP_SCALE_2675)) : 0;
    if (wide_test < 0) wide_test = getenv(SF2C_OUTPUT_SCALE_S_LARECOMP_WIDE_TEST_A818) ? atof(getenv(SF2C_OUTPUT_SCALE_S_LARECOMP_WIDE_TEST_A818)) : 0;
    double shape = wide_test > 0 ? wide_test : (dw / dh) / (base_width / SF2C_OUTPUT_SCALE_N_1080_0_AF38);
    bool reshape = reshape_allowed && fabs(shape - 1.0) > SF2C_OUTPUT_SCALE_N_0_005_07E1;
    if (!reshape) shape = 1.0;
    // render height in units of the Switch's 1080 lines
    double h, dyn = 1.0;
    int mode = settings_resolution();
    static bool was_dynamic;
    if (mode == RES_DYNAMIC && !was_dynamic) {
        // Measurements collected at a fixed resolution cannot guide Dynamic.
        g_last_t = g_dyn_gpu_sum = g_dyn_win_sum = g_sum_ms = 0;
        g_dyn_n = g_dyn_gpu_n = g_sum_n = g_split_n = 0;
        g_split = (DynSplit){0};
    }
    was_dynamic = mode == RES_DYNAMIC;
    if (forced > 0) h = forced;
    else if (mode >= 1 && mode <= SF2C_OUTPUT_SCALE_N_8_2C62) h = mode == 1 && g_gpu_integrated ? SF2C_OUTPUT_SCALE_N_720_D829 / SF2C_OUTPUT_SCALE_N_1080_0_AF38 : mode;
    else {
        h = reshape ? dh / SF2C_OUTPUT_SCALE_N_1080_0_AF38 : fit;  // Match display
        if (mode == RES_DYNAMIC) {
            static double pinned = -1;
            if (pinned < 0) pinned = getenv(SF2C_OUTPUT_SCALE_S_LARECOMP_DYN_TEST_AACD) ? atof(getenv(SF2C_OUTPUT_SCALE_S_LARECOMP_DYN_TEST_AACD)) : 0;
            if (pinned > 0) dyn = pinned;
            else { dyn_update(now, game_ms, win_ms, h * SF2C_OUTPUT_SCALE_N_1080_32EB); dyn = g_dyn; }
        }
        else if (mode == RES_Q75) h *= SF2C_OUTPUT_SCALE_N_0_75_CE5D;
        else if (mode == RES_Q25) h *= SF2C_OUTPUT_SCALE_N_0_25_A30A;
        else if (mode == RES_Q50) h *= SF2C_OUTPUT_SCALE_N_0_5_D2CB;
    }
    h = fmax(h, SF2C_OUTPUT_SCALE_N_0_1_14BE);  // (.25x of a small window)
    double ax = h * shape, ay = h;
    double cap = fmin(MAX_TEX / (base_width * ax), MAX_TEX / (SF2C_OUTPUT_SCALE_N_1080_0_AF38 * ay));
    if (cap < 1.0) { ax *= cap; ay *= cap; }
    o->ax = ax; o->ay = ay;
    o->sx = ax * dyn; o->sy = ay * dyn; o->shape = shape;
    // Native SF2 wide mode uses uniform scaling; fit its chosen aspect to the window.
    if (reshape) { o->vw = dw; o->vh = dh; }
    else { o->vw = base_width * fit; o->vh = SF2C_OUTPUT_SCALE_N_1080_0_AF38 * fit; }
    if (wide_test > 0) {
        double f2 = fmin(dw / (base_width * wide_test), dh / SF2C_OUTPUT_SCALE_N_1080_0_AF38);
        o->vw = base_width * wide_test * f2; o->vh = SF2C_OUTPUT_SCALE_N_1080_0_AF38 * f2;
    }
    o->vx = (dw - o->vw) / SF2C_OUTPUT_SCALE_N_2_D473; o->vy = (dh - o->vh) / SF2C_OUTPUT_SCALE_N_2_D473;
}
