#include "../constants/services.h"
// audren:u (audio renderer) and audout:u.
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdatomic.h>
#include <time.h>

#include <SDL3/SDL.h>

#include "../hle.h"
#include "../settings.h"
#include "../ipc.h"

// ------------------------------------------------------------------ audren: a CPU mixer modelled on
// Ryujinx's AudioRenderer (VoiceState / DataSourceHelper / StateUpdater; MIT, Ryujinx Team and Contributors,
// licenses/Ryujinx.txt).
// Voices decode PCM16 / ADPCM wave buffers, resample to 48 kHz, and mix into their destination mix's
// buffers; sub mixes route into their destination; the final mix feeds the device sink, which we
// downmix to stereo and push to an SDL audio stream. Effects and splitters are not implemented.
#define WB_COUNT SF2C_AUDIO_WB_COUNT
#define MIXBUF_MAX SF2C_AUDIO_MIXBUF_MAX
#define HIST SF2C_AUDIO_HIST
static atomic_bool movie_audio_active;

// Keep menu/game streams from competing with the decoded cutscene soundtrack. The movie's own SDL stream is separate.
void audio_movie_set_active(bool active) { atomic_store(&movie_audio_active, active); }

typedef struct {
    uint64_t addr, size, ctx;
    uint32_t start, end, loop_start, loop_end;
    int32_t loop_count;
    uint8_t loop, eos, sent;  // sent = IsSendToAudioProcessor
} WaveBuf;

typedef struct {  // per channel resource, "VoiceUpdateState"
    uint8_t valid[WB_COUNT];
    uint32_t wb_index, consumed;
    uint64_t played;
    int32_t offset, loop_count;
    float fraction;
    int16_t hist[HIST];
    int16_t pred_scale, h0, h1;
    float bq[SF2C_AUDIO_N_2_D473][SF2C_AUDIO_N_2_D473];  // biquad filter state per voice filter
} ChanState;

typedef struct {
    float mix[MIXBUF_MAX], prev[MIXBUF_MAX];
    uint8_t used;
} ChanRes;

typedef struct {
    uint8_t in_use, is_new, play_state;  // play_state: 0 started, 1 stopped, 2 stopping, 3 paused
    uint8_t fmt, chans, drop;
    uint32_t rate, wb_count, flush;
    float pitch, volume, prev_volume;
    int32_t mix_id;
    uint32_t splitter_id;
    uint32_t chan_ids[SF2C_AUDIO_N_6_E7F6];
    uint64_t ds_addr;
    uint16_t behaviour;
    uint8_t bq_on[SF2C_AUDIO_N_2_D473], bq_reset[SF2C_AUDIO_N_2_D473];
    float bq[SF2C_AUDIO_N_2_D473][SF2C_AUDIO_N_5_EF2D];  // a0 a1 a2 b1 b2
    WaveBuf wb[WB_COUNT];
} Voice;

typedef struct {
    uint8_t used;
    float volume;
    uint32_t bufs;
    int32_t dest;
    uint32_t dest_splitter;
    float vol[MIXBUF_MAX * MIXBUF_MAX];
    float *buf;  // bufs * sample_count
} Mix;

typedef struct Effect Effect;

typedef struct {
    uint32_t sample_rate, sample_count, mix_buffers, submixes, voices, sinks, effects, perf_frames;
    uint32_t splitters, revision;
    uint64_t elapsed_frames;
    uint32_t event_r, event_w;
    KEvent *event;
    uint32_t state;  // 0 started, 1 stopped
    pthread_mutex_t lock;
    Voice *voice;
    ChanRes *chres;
    ChanState *chst;
    Mix *mix;
    uint32_t nmix;
    uint8_t sink_used, sink_inputs, sink_input[SF2C_AUDIO_N_6_E7F6];
    Effect *fx;
    SDL_AudioStream *stream;
    FILE *wav;
    uint32_t wav_bytes;
    pthread_t thread;
    volatile bool quit;  // reboot (audio_reset)
} Renderer;

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

// One sample of DSP ADPCM at absolute sample index s (frames of 8 bytes: header + 14 nibbles).
static int16_t adpcm_next(const uint8_t *data, uint64_t size, uint32_t s, const int16_t *coefs, ChanState *cs) {
    uint32_t frame = s / SF2C_AUDIO_N_14_8527, n = s % SF2C_AUDIO_N_14_8527;
    uint64_t hdr = (uint64_t)frame * SF2C_AUDIO_N_8_2C62;
    if (hdr + 1 + n / SF2C_AUDIO_N_2_D473 >= size) return 0;
    uint8_t ps = data[hdr];
    int scale = ps & SF2C_AUDIO_N_15_E629, ci = (ps >> SF2C_AUDIO_N_4_4B22) & SF2C_AUDIO_N_7_7902;
    uint8_t b = data[hdr + 1 + n / SF2C_AUDIO_N_2_D473];
    int nib = (n & 1) ? (int8_t)(b << SF2C_AUDIO_N_4_4B22) >> SF2C_AUDIO_N_4_4B22 : (int8_t)b >> SF2C_AUDIO_N_4_4B22;
    int pred = coefs[ci * SF2C_AUDIO_N_2_D473] * cs->h0 + coefs[ci * SF2C_AUDIO_N_2_D473 + 1] * cs->h1;
    int v = ((nib * (SF2C_AUDIO_N_2048_BFA0 << scale)) + pred + SF2C_AUDIO_N_1024_E39E) >> SF2C_AUDIO_N_11_4FC8;
    if (v > SF2C_AUDIO_N_32767_D844) v = SF2C_AUDIO_N_32767_D844;
    if (v < -SF2C_AUDIO_N_32768_D196) v = -SF2C_AUDIO_N_32768_D196;
    cs->h1 = cs->h0;
    cs->h0 = (int16_t)v;
    return (int16_t)v;
}

static void mark_end(ChanState *cs, WaveBuf *wb, uint32_t *idx) {
    cs->valid[*idx] = 0;
    *idx = (*idx + 1) % WB_COUNT;
    cs->loop_count = 0;
    cs->consumed++;
    if (wb->eos) cs->played = 0;
}

// Decode up to `need` samples of channel `ch` into out; returns how many were produced.
static int decode_voice(Voice *v, int ch, ChanState *cs, int16_t *out, int need) {
    int y = 0;
    uint32_t idx = cs->wb_index;
    const int16_t *coefs = (const int16_t *)(uintptr_t)v->ds_addr;
    while (y < need) {
        if (!cs->valid[idx]) break;  // starving
        WaveBuf *wb = &v->wb[idx];
        if (cs->offset == 0 && v->fmt == SF2C_AUDIO_N_6_E7F6 && wb->ctx) {
            const int16_t *c = (const int16_t *)(uintptr_t)wb->ctx;
            cs->pred_scale = c[0]; cs->h0 = c[1]; cs->h1 = c[SF2C_AUDIO_N_2_D473];
        }
        uint32_t start = wb->start, end = wb->end;
        if (cs->loop_count > 0 && wb->loop_start && wb->loop_end && wb->loop_start <= wb->loop_end) {
            start = wb->loop_start;
            end = wb->loop_end;
        }
        int total = (int)end - (int)start, n = 0;
        int avail = total - cs->offset;
        if (avail < 0) avail = 0;
        int want = need - y < avail ? need - y : avail;
        const uint8_t *data = (const uint8_t *)(uintptr_t)wb->addr;
        if (data && wb->size) {
            if (v->fmt == SF2C_AUDIO_N_2_D473) {  // PCM16 interleaved
                const int16_t *pcm = (const int16_t *)data;
                uint64_t nsamp = wb->size / SF2C_AUDIO_N_2_D473 / v->chans;
                for (; n < want; n++) {
                    uint64_t s = (uint64_t)start + cs->offset + n;
                    if (s >= nsamp) break;
                    out[y + n] = pcm[s * v->chans + ch];
                }
            } else if (v->fmt == SF2C_AUDIO_N_6_E7F6 && coefs) {
                for (; n < want; n++) out[y + n] = adpcm_next(data, wb->size, start + cs->offset + n, coefs, cs);
            } else if (v->fmt == SF2C_AUDIO_N_3_4E07) {  // PCM float
                const float *pcm = (const float *)data;
                uint64_t nsamp = wb->size / SF2C_AUDIO_N_4_4B22 / v->chans;
                for (; n < want; n++) {
                    uint64_t s = (uint64_t)start + cs->offset + n;
                    if (s >= nsamp) break;
                    out[y + n] = (int16_t)clampf(pcm[s * v->chans + ch] * SF2C_AUDIO_N_32767_F_099A, -SF2C_AUDIO_N_32768_F_7EA1, SF2C_AUDIO_N_32767_F_099A);
                }
            }
        }
        y += n;
        cs->offset += n;
        cs->played += n;
        if (cs->offset >= total || n == 0) {
            cs->offset = 0;
            if (wb->loop) {
                cs->loop_count++;
                if (wb->loop_count >= 0 && (n == 0 || cs->loop_count > wb->loop_count)) mark_end(cs, wb, &idx);
                if (n == 0) break;
                if (v->behaviour & 1) cs->played = 0;  // PlayedSampleCountResetWhenLooping
            } else {
                mark_end(cs, wb, &idx);
            }
        }
    }
    cs->wb_index = idx;
    return y;
}

// UpdateParametersForCommandGeneration: returns whether the voice should be played this frame.
static int voice_prepare(Renderer *r, Voice *v) {
    int nch = v->chans > SF2C_AUDIO_N_6_E7F6 ? SF2C_AUDIO_N_6_E7F6 : v->chans;
    if (v->is_new) {
        for (int c = 0; c < nch; c++) memset(&r->chst[v->chan_ids[c] % r->voices], 0, sizeof(ChanState));
        v->prev_volume = v->volume;
        v->is_new = 0;
    }
    if (v->flush) {
        uint32_t wi = 0;
        for (uint32_t i = 0; i < v->flush; i++) {
            for (int c = 0; c < nch; c++) {
                ChanState *cs = &r->chst[v->chan_ids[c] % r->voices];
                if (i == 0) wi = cs->wb_index;
                cs->valid[(wi + i) % WB_COUNT] = 0;
                cs->wb_index = (cs->wb_index + 1) % WB_COUNT;
                cs->consumed++;
            }
            v->wb[(wi + i) % WB_COUNT].sent = 1;
        }
        v->flush = 0;
    }
    if (v->play_state == 0) {
        for (int i = 0; i < WB_COUNT; i++) {
            if (v->wb[i].sent) continue;
            for (int c = 0; c < nch; c++) r->chst[v->chan_ids[c] % r->voices].valid[i] = 1;
            v->wb[i].sent = 1;
        }
        ChanState *p = &r->chst[v->chan_ids[0] % r->voices];
        for (int i = 0; i < WB_COUNT; i++) if (p->valid[i]) return 1;
        return 0;
    }
    if (v->play_state == SF2C_AUDIO_N_2_D473) {  // stopping: drop everything queued
        for (int i = 0; i < WB_COUNT; i++) {
            v->wb[i].sent = 1;
            for (int c = 0; c < nch; c++) {
                ChanState *cs = &r->chst[v->chan_ids[c] % r->voices];
                if (cs->valid[i]) { cs->wb_index = (cs->wb_index + 1) % WB_COUNT; cs->consumed++; }
                cs->valid[i] = 0;
            }
        }
        for (int c = 0; c < nch; c++) {
            ChanState *cs = &r->chst[v->chan_ids[c] % r->voices];
            cs->offset = 0; cs->played = 0; cs->fraction = 0; cs->loop_count = 0;
            memset(cs->hist, 0, sizeof cs->hist);
            cs->pred_scale = cs->h0 = cs->h1 = 0;
        }
        v->play_state = 1;
    }
    return 0;
}

static Mix *find_mix(Renderer *r, int32_t id) { return id >= 0 && (uint32_t)id < r->nmix && r->mix[id].used ? &r->mix[id] : NULL; }

// ------------------------------------------------------------------ reverb (port of Ryujinx ReverbState/Command)
typedef struct { float *buf; uint32_t sr, cur, last, count, max; } DelayLine;

static void dl_set(DelayLine *d, float ms) {
    uint32_t n = (uint32_t)roundf(d->sr * ms);
    d->count = n < d->max ? n : d->max;
    d->cur = 0;
    d->last = d->count ? d->count - 1 : 0;
}
static void dl_init(DelayLine *d, uint32_t sr, float ms_max) {
    d->sr = sr;
    d->max = (uint32_t)roundf(sr * ms_max);
    free(d->buf);
    d->buf = calloc(d->max + 1, sizeof(float));
    dl_set(d, ms_max);
}
static float dl_read(DelayLine *d) { return d->buf[d->cur]; }
static float dl_update(DelayLine *d, float v) {
    float o = d->buf[d->cur];
    d->buf[d->cur++] = v;
    if (d->cur >= d->last) d->cur = 0;
    return o;
}
static float dl_tap_unsafe(DelayLine *d, uint32_t idx, int off) {
    int t = (int)d->cur - ((int)idx + off);
    if (t < 0) t += (int)d->count;
    if (t < 0 || (uint32_t)t > d->max) return 0;
    return d->buf[t];
}
static float dl_tap(DelayLine *d, uint32_t idx) {
    if (idx >= d->count) idx = d->count ? d->count - 1 : 0;
    return dl_tap_unsafe(d, idx, -1);
}

typedef struct { DelayLine dl; float rate; } DecayDelay;
static float dd_update(DecayDelay *d, float v) {
    float p = v - d->rate * dl_read(&d->dl);
    return dl_update(&d->dl, p) + p * d->rate;
}

typedef struct {
    uint8_t input[SF2C_AUDIO_N_6_E7F6], output[SF2C_AUDIO_N_6_E7F6];
    uint16_t ch_max, ch;
    int32_t sample_rate, early_mode, early_gain, pre_delay, late_mode, late_gain, decay_time, hf_decay_ratio,
        coloration, reverb_gain, out_gain, dry_gain;
} ReverbParam;

typedef struct {
    int ready;
    uint8_t raw[SF2C_AUDIO_N_0X44_E723];  // last parameter bytes seen
    ReverbParam p;
    DelayLine fdn[SF2C_AUDIO_N_4_4B22], pre, fc;
    DecayDelay decay[SF2C_AUDIO_N_4_4B22];
    uint32_t early_time[SF2C_AUDIO_N_10_4A44], pre_time;
    float early_gain[SF2C_AUDIO_N_10_4A44], hf_direct[SF2C_AUDIO_N_4_4B22], hf_prev[SF2C_AUDIO_N_4_4B22], prev_fb[SF2C_AUDIO_N_4_4B22];
} Reverb;

static const float rv_fdn_times[SF2C_AUDIO_N_20_F5CA] = {SF2C_AUDIO_N_53_953247F_F0AA, SF2C_AUDIO_N_79_192566F_5EAB, SF2C_AUDIO_N_116_238770F_06A6, SF2C_AUDIO_N_130_615295F_BC48, SF2C_AUDIO_N_53_953247F_F0AA, SF2C_AUDIO_N_79_192566F_5EAB,
    SF2C_AUDIO_N_116_238770F_06A6, SF2C_AUDIO_N_170_615295F_5A57, SF2C_AUDIO_N_5_EF2D, SF2C_AUDIO_N_10_4A44, SF2C_AUDIO_N_5_EF2D, SF2C_AUDIO_N_10_4A44, SF2C_AUDIO_N_47_03F_BBF2, SF2C_AUDIO_N_71_7F22, SF2C_AUDIO_N_103_454F, SF2C_AUDIO_N_170_734D, SF2C_AUDIO_N_53_953247F_F0AA, SF2C_AUDIO_N_79_192566F_5EAB, SF2C_AUDIO_N_116_238770F_06A6, SF2C_AUDIO_N_170_615295F_5A57};
static const float rv_decay_times[SF2C_AUDIO_N_20_F5CA] = {SF2C_AUDIO_N_7_7902, SF2C_AUDIO_N_9_1958, SF2C_AUDIO_N_13_3FDB, SF2C_AUDIO_N_17_4523, SF2C_AUDIO_N_7_7902, SF2C_AUDIO_N_9_1958, SF2C_AUDIO_N_13_3FDB, SF2C_AUDIO_N_17_4523, 1, 1, 1, 1, SF2C_AUDIO_N_7_7902, SF2C_AUDIO_N_7_7902, SF2C_AUDIO_N_13_3FDB, SF2C_AUDIO_N_9_1958, SF2C_AUDIO_N_7_7902, SF2C_AUDIO_N_9_1958, SF2C_AUDIO_N_13_3FDB, SF2C_AUDIO_N_17_4523};
static const float rv_early_times[SF2C_AUDIO_N_50_1A65] = {SF2C_AUDIO_N_0_0F_6895, SF2C_AUDIO_N_3_5F_8924, SF2C_AUDIO_N_2_8F_E8D4, SF2C_AUDIO_N_3_9F_48FA, SF2C_AUDIO_N_2_7F_B3C8, SF2C_AUDIO_N_13_4F_BBBE, SF2C_AUDIO_N_7_9F_3640, SF2C_AUDIO_N_8_4F_8B36, SF2C_AUDIO_N_9_9F_C2F3, SF2C_AUDIO_N_12_0F_4418,
    SF2C_AUDIO_N_0_0F_6895, SF2C_AUDIO_N_11_8F_B092, SF2C_AUDIO_N_5_5F_84EC, SF2C_AUDIO_N_11_2F_A81E, SF2C_AUDIO_N_10_4F_EC75, SF2C_AUDIO_N_38_1F_AB68, SF2C_AUDIO_N_22_2F_37BE, SF2C_AUDIO_N_29_6F_5F8D, SF2C_AUDIO_N_21_2F_6343, SF2C_AUDIO_N_24_8F_4279,
    SF2C_AUDIO_N_0_0F_6895, SF2C_AUDIO_N_41_5F_CF04, SF2C_AUDIO_N_20_5F_6968, SF2C_AUDIO_N_41_3F_6BA8, SF2C_AUDIO_N_0_0F_6895, SF2C_AUDIO_N_29_5F_3E9C, SF2C_AUDIO_N_33_8F_1E3B, SF2C_AUDIO_N_45_2F_4AC8, SF2C_AUDIO_N_46_8F_A562, SF2C_AUDIO_N_0_0F_6895,
    SF2C_AUDIO_N_33_1F_FD7F, SF2C_AUDIO_N_43_3F_906F, SF2C_AUDIO_N_22_8F_2BB7, SF2C_AUDIO_N_37_9F_1A68, SF2C_AUDIO_N_14_9F_E8A9, SF2C_AUDIO_N_35_3F_5686, SF2C_AUDIO_N_17_9F_7802, SF2C_AUDIO_N_34_2F_34DE, SF2C_AUDIO_N_0_0F_6895, SF2C_AUDIO_N_43_3F_906F, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static const float rv_early_gain[SF2C_AUDIO_N_50_1A65] = {SF2C_AUDIO_N_0_70F_658D, SF2C_AUDIO_N_0_68F_DDB5, SF2C_AUDIO_N_0_70F_658D, SF2C_AUDIO_N_0_68F_DDB5, SF2C_AUDIO_N_0_70F_658D, SF2C_AUDIO_N_0_68F_DDB5, SF2C_AUDIO_N_0_70F_658D, SF2C_AUDIO_N_0_68F_DDB5, SF2C_AUDIO_N_0_68F_DDB5, SF2C_AUDIO_N_0_68F_DDB5,
    SF2C_AUDIO_N_0_70F_658D, SF2C_AUDIO_N_0_68F_DDB5, SF2C_AUDIO_N_0_70F_658D, SF2C_AUDIO_N_0_68F_DDB5, SF2C_AUDIO_N_0_70F_658D, SF2C_AUDIO_N_0_68F_DDB5, SF2C_AUDIO_N_0_68F_DDB5, SF2C_AUDIO_N_0_68F_DDB5, SF2C_AUDIO_N_0_68F_DDB5, SF2C_AUDIO_N_0_68F_DDB5,
    SF2C_AUDIO_N_0_50F_08F1, SF2C_AUDIO_N_0_70F_658D, SF2C_AUDIO_N_0_70F_658D, SF2C_AUDIO_N_0_68F_DDB5, SF2C_AUDIO_N_0_50F_08F1, SF2C_AUDIO_N_0_68F_DDB5, SF2C_AUDIO_N_0_68F_DDB5, SF2C_AUDIO_N_0_70F_658D, SF2C_AUDIO_N_0_68F_DDB5, SF2C_AUDIO_N_0_00F_A65B,
    SF2C_AUDIO_N_0_93F_2B83, SF2C_AUDIO_N_0_92F_C109, SF2C_AUDIO_N_0_87F_FEAA, SF2C_AUDIO_N_0_86F_9ECE, SF2C_AUDIO_N_0_94F_72F4, SF2C_AUDIO_N_0_81F_C8B3, SF2C_AUDIO_N_0_80F_2FA7, SF2C_AUDIO_N_0_77F_90D4, SF2C_AUDIO_N_0_76F_7D5F, SF2C_AUDIO_N_0_65F_E225, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static const float rv_pre_times[SF2C_AUDIO_N_5_EF2D] = {SF2C_AUDIO_N_12_5F_9D47, SF2C_AUDIO_N_40_0F_BA4D, SF2C_AUDIO_N_50_0F_36FB, SF2C_AUDIO_N_50_0F_36FB, SF2C_AUDIO_N_0_0F_6895};

static float q14(int32_t v) { return v / SF2C_AUDIO_N_16384_0F_EB94; }
static uint32_t rv_rate(const ReverbParam *p) {
    uint32_t sr = (uint32_t)q14(p->sample_rate);  // samples per millisecond
    return sr && sr <= SF2C_AUDIO_N_192_EB3B ? sr : SF2C_AUDIO_N_48_9801;
}

static void reverb_update(Reverb *rv) {
    ReverbParam *p = &rv->p;
    uint32_t sr = rv_rate(p);
    float pre_ms = q14(p->pre_delay), early_gain = q14(p->early_gain), coloration = q14(p->coloration);
    float decay_time = q14(p->decay_time);
    int em = p->early_mode >= 0 && p->early_mode < SF2C_AUDIO_N_5_EF2D ? p->early_mode : SF2C_AUDIO_N_4_4B22;
    int lm = p->late_mode >= 0 && p->late_mode < SF2C_AUDIO_N_5_EF2D ? p->late_mode : 0;
    for (int i = 0; i < SF2C_AUDIO_N_10_4A44; i++) {
        uint32_t t = (uint32_t)roundf(sr * (rv_early_times[i] + pre_ms));
        rv->early_time[i] = (t < rv->pre.max ? t : rv->pre.max) + 1;
        rv->early_gain[i] = rv_early_gain[i] * early_gain;
    }
    if (p->ch == SF2C_AUDIO_N_2_D473) { rv->early_gain[SF2C_AUDIO_N_4_4B22] *= SF2C_AUDIO_N_0_5F_2DB0; rv->early_gain[SF2C_AUDIO_N_5_EF2D] *= SF2C_AUDIO_N_0_5F_2DB0; }
    uint32_t pt = (uint32_t)roundf(sr * (rv_pre_times[em] + pre_ms));
    rv->pre_time = pt < rv->pre.max ? pt : rv->pre.max;
    float hf_ratio = q14(p->hf_decay_ratio), hf_cos = cosf(SF2C_AUDIO_N_1280_0F_7ADA / sr);
    for (int i = 0; i < SF2C_AUDIO_N_4_4B22; i++) {
        dl_set(&rv->fdn[i], rv_fdn_times[lm * SF2C_AUDIO_N_4_4B22 + i]);
        dl_set(&rv->decay[i].dl, rv_decay_times[lm * SF2C_AUDIO_N_4_4B22 + i]);
        float a = -SF2C_AUDIO_N_3_0F_70A4 * (rv->decay[i].dl.count + rv->fdn[i].count);
        float b = decay_time > 0 ? a / (decay_time * sr) : -SF2C_AUDIO_N_1000_0F_2FBA;
        float c, d;
        if (hf_ratio < SF2C_AUDIO_N_0_995F_9B8C && hf_ratio > 0) {
            float e = powf(SF2C_AUDIO_N_10_0F_BE4E, (((SF2C_AUDIO_N_1_0F_CA71 / hf_ratio) - SF2C_AUDIO_N_1_0F_CA71) * SF2C_AUDIO_N_2_D473) / SF2C_AUDIO_N_100_AD57 * (b / SF2C_AUDIO_N_10_4A44));
            float f = SF2C_AUDIO_N_1_0F_CA71 - e, g = SF2C_AUDIO_N_2_0F_2E19 - (e * SF2C_AUDIO_N_2_D473 * hf_cos);
            float h = sqrtf(fmaxf(0, g * g - f * f * SF2C_AUDIO_N_4_4B22));
            c = f != 0 ? (g - h) / (f * SF2C_AUDIO_N_2_D473) : 0;
            d = SF2C_AUDIO_N_1_0F_CA71 - c;
        } else {
            c = 0;
            d = 1;
        }
        rv->hf_direct[i] = powf(SF2C_AUDIO_N_10_0F_BE4E, b / SF2C_AUDIO_N_1000_4051) * d * SF2C_AUDIO_N_0_7071F_4960;
        rv->hf_prev[i] = c;
        rv->prev_fb[i] = 0;
        rv->decay[i].rate = SF2C_AUDIO_N_0_6F_6EB9 * (SF2C_AUDIO_N_1_0F_CA71 - coloration);
    }
}

static void reverb_init(Reverb *rv) {
    uint32_t sr = rv_rate(&rv->p);
    for (int i = 0; i < SF2C_AUDIO_N_4_4B22; i++) {
        dl_init(&rv->fdn[i], sr, rv_fdn_times[SF2C_AUDIO_N_16_B17E + i]);
        dl_init(&rv->decay[i].dl, sr, rv_decay_times[SF2C_AUDIO_N_16_B17E + i]);
    }
    dl_init(&rv->pre, sr, SF2C_AUDIO_N_150_0F_7DA6);  // long pre-delay needs REV3+... 350 ms
    dl_init(&rv->fc, sr, SF2C_AUDIO_N_5_0F_6796);
    reverb_update(rv);
    rv->ready = 1;
}

static void reverb_process(Reverb *rv, float *const *in, float *const *out, uint32_t n) {
    static const int8_t oe1[SF2C_AUDIO_N_10_4A44] = {0}, te[SF2C_AUDIO_N_10_4A44] = {0, 1, SF2C_AUDIO_N_2_D473, SF2C_AUDIO_N_3_4E07, SF2C_AUDIO_N_4_4B22, SF2C_AUDIO_N_5_EF2D, SF2C_AUDIO_N_6_E7F6, SF2C_AUDIO_N_7_7902, SF2C_AUDIO_N_8_2C62, SF2C_AUDIO_N_9_1958};
    static const int8_t o1[SF2C_AUDIO_N_4_4B22] = {0}, tf1[SF2C_AUDIO_N_4_4B22] = {0, 1, SF2C_AUDIO_N_2_D473, SF2C_AUDIO_N_3_4E07};
    static const int8_t oe2[SF2C_AUDIO_N_10_4A44] = {0, 0, 1, 1, 0, 1, 0, 0, 1, 1}, o2[SF2C_AUDIO_N_4_4B22] = {0, 0, 1, 1}, tf2[SF2C_AUDIO_N_4_4B22] = {SF2C_AUDIO_N_2_D473, 0, SF2C_AUDIO_N_3_4E07, 1};
    static const int8_t oe4[SF2C_AUDIO_N_10_4A44] = {0, 0, 1, 1, 0, 1, SF2C_AUDIO_N_2_D473, SF2C_AUDIO_N_2_D473, SF2C_AUDIO_N_3_4E07, SF2C_AUDIO_N_3_4E07}, o4[SF2C_AUDIO_N_4_4B22] = {0, 1, SF2C_AUDIO_N_2_D473, SF2C_AUDIO_N_3_4E07}, tf4[SF2C_AUDIO_N_4_4B22] = {0, 1, SF2C_AUDIO_N_2_D473, SF2C_AUDIO_N_3_4E07};
    static const int8_t oe6[SF2C_AUDIO_N_20_F5CA] = {0, SF2C_AUDIO_N_5_EF2D, 0, SF2C_AUDIO_N_5_EF2D, 1, SF2C_AUDIO_N_5_EF2D, 1, SF2C_AUDIO_N_5_EF2D, SF2C_AUDIO_N_4_4B22, SF2C_AUDIO_N_5_EF2D, SF2C_AUDIO_N_4_4B22, SF2C_AUDIO_N_5_EF2D, SF2C_AUDIO_N_2_D473, SF2C_AUDIO_N_5_EF2D, SF2C_AUDIO_N_2_D473, SF2C_AUDIO_N_5_EF2D, SF2C_AUDIO_N_3_4E07, SF2C_AUDIO_N_5_EF2D, SF2C_AUDIO_N_3_4E07, SF2C_AUDIO_N_5_EF2D};
    static const int8_t te6[SF2C_AUDIO_N_20_F5CA] = {0, 0, 1, 1, SF2C_AUDIO_N_2_D473, SF2C_AUDIO_N_2_D473, SF2C_AUDIO_N_3_4E07, SF2C_AUDIO_N_3_4E07, SF2C_AUDIO_N_4_4B22, SF2C_AUDIO_N_4_4B22, SF2C_AUDIO_N_5_EF2D, SF2C_AUDIO_N_5_EF2D, SF2C_AUDIO_N_6_E7F6, SF2C_AUDIO_N_6_E7F6, SF2C_AUDIO_N_7_7902, SF2C_AUDIO_N_7_7902, SF2C_AUDIO_N_8_2C62, SF2C_AUDIO_N_8_2C62, SF2C_AUDIO_N_9_1958, SF2C_AUDIO_N_9_1958};
    static const int8_t o6[SF2C_AUDIO_N_6_E7F6] = {0, 1, SF2C_AUDIO_N_2_D473, SF2C_AUDIO_N_3_4E07, SF2C_AUDIO_N_4_4B22, SF2C_AUDIO_N_5_EF2D}, tf6[SF2C_AUDIO_N_6_E7F6] = {0, 1, SF2C_AUDIO_N_2_D473, SF2C_AUDIO_N_3_4E07, -1, SF2C_AUDIO_N_3_4E07};
    int ch = rv->p.ch;
    const int8_t *oe, *tel, *o, *tf;
    int ne, nf;
    switch (ch) {
    case 1: oe = oe1; tel = te; o = o1; tf = tf1; ne = SF2C_AUDIO_N_10_4A44; nf = SF2C_AUDIO_N_4_4B22; break;
    case SF2C_AUDIO_N_2_D473: oe = oe2; tel = te; o = o2; tf = tf2; ne = SF2C_AUDIO_N_10_4A44; nf = SF2C_AUDIO_N_4_4B22; break;
    case SF2C_AUDIO_N_4_4B22: oe = oe4; tel = te; o = o4; tf = tf4; ne = SF2C_AUDIO_N_10_4A44; nf = SF2C_AUDIO_N_4_4B22; break;
    case SF2C_AUDIO_N_6_E7F6: oe = oe6; tel = te6; o = o6; tf = tf6; ne = SF2C_AUDIO_N_20_F5CA; nf = SF2C_AUDIO_N_6_E7F6; break;
    default: return;
    }
    float reverb_gain = q14(rv->p.reverb_gain), late_gain = q14(rv->p.late_gain);
    float out_gain = q14(rv->p.out_gain), dry_gain = q14(rv->p.dry_gain);
    for (uint32_t s = 0; s < n; s++) {
        float ov[SF2C_AUDIO_N_6_E7F6] = {0}, fb[SF2C_AUDIO_N_4_4B22], fo[SF2C_AUDIO_N_4_4B22], cin[SF2C_AUDIO_N_6_E7F6];
        for (int i = 0; i < ne; i++) ov[oe[i]] += dl_tap_unsafe(&rv->pre, rv->early_time[tel[i]], 0) * rv->early_gain[tel[i]];
        if (ch == SF2C_AUDIO_N_6_E7F6) ov[SF2C_AUDIO_N_5_EF2D] *= SF2C_AUDIO_N_0_2F_ED8E;
        float pre = 0;
        for (int c = 0; c < ch; c++) { cin[c] = in[c][s] * SF2C_AUDIO_N_64_A68B; pre += cin[c] * reverb_gain; }
        dl_update(&rv->pre, pre);
        float late = dl_tap(&rv->pre, rv->pre_time) * late_gain;
        for (int i = 0; i < SF2C_AUDIO_N_4_4B22; i++) {
            fo[i] = dl_read(&rv->fdn[i]) * rv->hf_direct[i] + rv->prev_fb[i] * rv->hf_prev[i];
            rv->prev_fb[i] = fo[i];
        }
        fb[0] = fo[SF2C_AUDIO_N_2_D473] + fo[1];
        fb[1] = -fo[0] - fo[SF2C_AUDIO_N_3_4E07];
        fb[SF2C_AUDIO_N_2_D473] = fo[0] - fo[SF2C_AUDIO_N_3_4E07];
        fb[SF2C_AUDIO_N_3_4E07] = fo[1] - fo[SF2C_AUDIO_N_2_D473];
        for (int i = 0; i < SF2C_AUDIO_N_4_4B22; i++) {
            fo[i] = dd_update(&rv->decay[i], fb[i] + late);
            dl_update(&rv->fdn[i], fo[i]);
        }
        for (int i = 0; i < nf; i++) if (tf[i] >= 0) ov[o[i]] += fo[tf[i]];
        if (ch == SF2C_AUDIO_N_6_E7F6) ov[SF2C_AUDIO_N_4_4B22] += dl_update(&rv->fc, (fo[SF2C_AUDIO_N_2_D473] - fo[SF2C_AUDIO_N_3_4E07]) * SF2C_AUDIO_N_0_5F_2DB0);
        for (int c = 0; c < ch; c++) out[c][s] = (ov[c] * out_gain + cin[c] * dry_gain) / SF2C_AUDIO_N_64_A68B;
    }
}

struct Effect {
    uint8_t type, enabled;
    int32_t mix_id;
    uint32_t order;
    Reverb rv;
};

// Effect slot update from EffectInParameterVersion1 (0xC0 bytes: type, is_new, enabled, mix, buffers, order, data).
static void effect_update(Effect *fx, const uint8_t *e) {
    if (fx->type != e[0]) memset(&fx->rv.p, 0, sizeof fx->rv.p), fx->rv.ready = 0;
    fx->type = e[0];
    fx->enabled = e[SF2C_AUDIO_N_2_D473];
    memcpy(&fx->mix_id, e + SF2C_AUDIO_N_4_4B22, SF2C_AUDIO_N_4_4B22);
    memcpy(&fx->order, e + SF2C_AUDIO_N_0X18_D47C, SF2C_AUDIO_N_4_4B22);
    if (fx->type != SF2C_AUDIO_N_4_4B22) return;
    const uint8_t *d = e + SF2C_AUDIO_N_0X20_A766;
    uint16_t ch_max, ch;
    memcpy(&ch_max, d + SF2C_AUDIO_N_12_6B51, SF2C_AUDIO_N_2_D473);
    memcpy(&ch, d + SF2C_AUDIO_N_14_8527, SF2C_AUDIO_N_2_D473);
    if (ch_max != 1 && ch_max != SF2C_AUDIO_N_2_D473 && ch_max != SF2C_AUDIO_N_4_4B22 && ch_max != SF2C_AUDIO_N_6_E7F6) return;
    if (ch != 1 && ch != SF2C_AUDIO_N_2_D473 && ch != SF2C_AUDIO_N_4_4B22 && ch != SF2C_AUDIO_N_6_E7F6) return;
    if (e[1] || !fx->rv.ready) {
        memcpy(fx->rv.raw, d, sizeof fx->rv.raw);
        memcpy(&fx->rv.p, d, sizeof fx->rv.p);
        if (getenv(SF2C_AUDIO_S_LARECOMP_AUDIO_LOG_866C))
            LOG(SF2C_AUDIO_S_AUDREN_REVERB_INIT_CH_U_RATE_307A,
                ch, fx->rv.p.sample_rate, fx->rv.p.early_mode, fx->rv.p.early_gain, fx->rv.p.pre_delay,
                fx->rv.p.late_mode, fx->rv.p.late_gain, fx->rv.p.decay_time, fx->rv.p.hf_decay_ratio,
                fx->rv.p.coloration, fx->rv.p.reverb_gain, fx->rv.p.out_gain, fx->rv.p.dry_gain);
        reverb_init(&fx->rv);
    } else if (memcmp(fx->rv.raw, d, SF2C_AUDIO_N_0X40_1AF3)) {  // parameters changed (status byte at 0x40 ignored)
        memcpy(fx->rv.raw, d, sizeof fx->rv.raw);
        memcpy(&fx->rv.p, d, sizeof fx->rv.p);
        reverb_update(&fx->rv);
    }
}

static void apply_effects(Renderer *r, int32_t mix_id, Mix *m) {
    uint32_t N = r->sample_count;
    for (uint32_t order = 0; order < r->effects; order++) {
        for (uint32_t i = 0; i < r->effects; i++) {
            Effect *fx = &r->fx[i];
            if (fx->type != SF2C_AUDIO_N_4_4B22 || fx->mix_id != mix_id || fx->order != order || !fx->rv.ready) continue;
            int ch = fx->rv.p.ch;
            float *in[SF2C_AUDIO_N_6_E7F6], *out[SF2C_AUDIO_N_6_E7F6];
            int ok = 1;
            for (int c = 0; c < ch; c++) {
                if (fx->rv.p.input[c] >= m->bufs || fx->rv.p.output[c] >= m->bufs) ok = 0;
                in[c] = m->buf + (fx->rv.p.input[c] % MIXBUF_MAX) * N;
                out[c] = m->buf + (fx->rv.p.output[c] % MIXBUF_MAX) * N;
            }
            if (!ok) continue;
            static int no_reverb = -1;
            if (no_reverb < 0) no_reverb = getenv(SF2C_AUDIO_S_LARECOMP_NO_REVERB_0417) ? 1 : 0;
            static int toggle = -1;
            if (toggle < 0) toggle = getenv(SF2C_AUDIO_S_LARECOMP_REVERB_TOGGLE_C8DB) ? atoi(getenv(SF2C_AUDIO_S_LARECOMP_REVERB_TOGGLE_C8DB)) : 0;
            int off = no_reverb || !g_settings.reverb || (toggle && (r->elapsed_frames / toggle) % SF2C_AUDIO_N_2_D473);
            if (fx->enabled && !off) reverb_process(&fx->rv, in, out, N);
            else
                for (int c = 0; c < ch; c++) if (in[c] != out[c]) memcpy(out[c], in[c], N * sizeof(float));
        }
    }
}

static int hf_log = -1;
static double hf[SF2C_AUDIO_N_1024_E39E];

static void render_frame(Renderer *r, float *stereo) {
    if (hf_log < 0) hf_log = getenv(SF2C_AUDIO_S_LARECOMP_AUDIO_HF_BDB2) ? 1 : 0;
    uint32_t N = r->sample_count;
    for (uint32_t m = 0; m < r->nmix; m++)
        if (r->mix[m].buf) memset(r->mix[m].buf, 0, sizeof(float) * MIXBUF_MAX * N);
    int16_t tmp[HIST + SF2C_AUDIO_N_4096_8B92];
    float smp[SF2C_AUDIO_N_4096_8B92];
    for (uint32_t vi = 0; vi < r->voices; vi++) {
        Voice *v = &r->voice[vi];
        if (!v->in_use) continue;
        int play = voice_prepare(r, v);
        if (!play || v->wb_count == 0 || v->drop) continue;
        Mix *dst = find_mix(r, v->mix_id);
        float ratio = (float)v->rate / r->sample_rate * v->pitch;
        if (!(ratio > 0) || ratio > SF2C_AUDIO_N_8_2C62) ratio = 1;
        int nch = v->chans > SF2C_AUDIO_N_6_E7F6 ? SF2C_AUDIO_N_6_E7F6 : v->chans;
        for (int c = 0; c < nch; c++) {
            uint32_t rid = v->chan_ids[c] % r->voices;
            ChanState *cs = &r->chst[rid];
            ChanRes *cr = &r->chres[rid];
            int need = (int)(cs->fraction + ratio * N);
            if (need > SF2C_AUDIO_N_4096_8B92) need = SF2C_AUDIO_N_4096_8B92;
            memcpy(tmp, cs->hist, sizeof cs->hist);
            int got = decode_voice(v, c, cs, tmp + HIST, need);
            if (got < need) memset(tmp + HIST + got, 0, (need - got) * SF2C_AUDIO_N_2_D473);
            float pos = cs->fraction;
            for (uint32_t i = 0; i < N; i++, pos += ratio) {
                int k = (int)pos;
                float f = pos - k;
                if (k + SF2C_AUDIO_N_3_4E07 > need + HIST - 1) k = need + HIST - SF2C_AUDIO_N_4_4B22;
                float y0 = tmp[k], y1 = tmp[k + 1], y2 = tmp[k + SF2C_AUDIO_N_2_D473], y3 = tmp[k + SF2C_AUDIO_N_3_4E07];
                if (ratio == SF2C_AUDIO_N_1_0F_CA71 && f == 0) { smp[i] = y1; continue; }
                // 4-point Hermite between y1 and y2
                float c1 = SF2C_AUDIO_N_0_5F_2DB0 * (y2 - y0), c2 = y0 - SF2C_AUDIO_N_2_5F_240A * y1 + SF2C_AUDIO_N_2_D473 * y2 - SF2C_AUDIO_N_0_5F_2DB0 * y3;
                float c3 = SF2C_AUDIO_N_0_5F_2DB0 * (y3 - y0) + SF2C_AUDIO_N_1_5F_DF85 * (y1 - y2);
                smp[i] = ((c3 * f + c2) * f + c1) * f + y1;
            }
            for (int fi = 0; fi < SF2C_AUDIO_N_2_D473; fi++) {
                if (!v->bq_on[fi]) continue;
                float *st = cs->bq[fi], *k = v->bq[fi];
                if (v->bq_reset[fi]) st[0] = st[1] = 0;
                for (uint32_t i = 0; i < N; i++) {
                    float in = smp[i], o = in * k[0] + st[0];
                    st[0] = in * k[1] + o * k[SF2C_AUDIO_N_3_4E07] + st[1];
                    st[1] = in * k[SF2C_AUDIO_N_2_D473] + o * k[SF2C_AUDIO_N_4_4B22];
                    smp[i] = o;
                }
            }
            cs->fraction = cs->fraction + ratio * N - need;
            if (cs->fraction < 0 || cs->fraction >= 1) cs->fraction = 0;
            memcpy(cs->hist, tmp + need, sizeof cs->hist);
            if (hf_log) {
                // Goertzel power around 19.6 kHz (scaled by volume)
                double w = SF2C_AUDIO_N_2_D473 * M_PI * SF2C_AUDIO_N_19582_0_2495 / r->sample_rate, cw = SF2C_AUDIO_N_2_D473 * cos(w), q1 = 0, q2 = 0;
                for (uint32_t i = 0; i < N; i++) { double q0 = cw * q1 - q2 + smp[i]; q2 = q1; q1 = q0; }
                double e = (q1 * q1 + q2 * q2 - q1 * q2 * cw) * v->volume * v->volume / N;
                hf[vi] += e;
            }
            if (!dst) continue;
            static int solo = -SF2C_AUDIO_N_2_D473;
            if (solo == -SF2C_AUDIO_N_2_D473) solo = getenv(SF2C_AUDIO_S_LARECOMP_AUDIO_SOLO_74D4) ? atoi(getenv(SF2C_AUDIO_S_LARECOMP_AUDIO_SOLO_74D4)) : -1;
            if (solo >= 0 && (int)vi != solo) continue;
            for (uint32_t b = 0; b < dst->bufs && b < MIXBUF_MAX; b++) {
                float g0 = v->prev_volume * cr->prev[b], g1 = v->volume * cr->mix[b];
                if (g0 == 0 && g1 == 0) continue;
                float *o = dst->buf + b * N, dg = (g1 - g0) / N;
                for (uint32_t i = 0; i < N; i++) o[i] += smp[i] * (g0 + dg * i);
            }
        }
        for (int c = 0; c < nch; c++) {
            ChanRes *cr = &r->chres[v->chan_ids[c] % r->voices];
            memcpy(cr->prev, cr->mix, sizeof cr->prev);
        }
        v->bq_reset[0] = v->bq_reset[1] = 0;
        v->prev_volume = v->volume;
    }
    if (hf_log && r->elapsed_frames % SF2C_AUDIO_N_200_27BA == 0) {
        LOG(SF2C_AUDIO_S_AUDREN_HF_F_LLU_0A0C, (unsigned long long)r->elapsed_frames);
        for (uint32_t k = 0; k < r->voices; k++)
            if (hf[k] > SF2C_AUDIO_N_1E6_8C96)
                LOG(SF2C_AUDIO_S_V_U_0F_LLX_BQ_D_D_3F_3F_FMT__B4C3, k, hf[k] / SF2C_AUDIO_N_1E6_8C96,
                    (unsigned long long)r->voice[k].wb[0].addr, r->voice[k].bq_on[0], r->voice[k].bq_on[1],
                    r->voice[k].bq[1][0], r->voice[k].bq[1][SF2C_AUDIO_N_3_4E07], r->voice[k].fmt, r->voice[k].rate, r->voice[k].pitch);
        LOG(SF2C_AUDIO_S_N_D7A5);
        memset(hf, 0, sizeof hf);
    }
    // sub mixes, deepest first
    int depth[SF2C_AUDIO_N_64_A68B] = {0};
    int maxd = 0;
    for (uint32_t m = 1; m < r->nmix && m < SF2C_AUDIO_N_64_A68B; m++) {
        if (!r->mix[m].used) continue;
        int d = 0;
        for (Mix *x = &r->mix[m]; x && d < SF2C_AUDIO_N_64_A68B; x = find_mix(r, x->dest)) { if (x == &r->mix[0]) break; d++; }
        depth[m] = d;
        if (d > maxd) maxd = d;
    }
    for (int d = maxd; d >= 1; d--) {
        for (uint32_t m = 1; m < r->nmix && m < SF2C_AUDIO_N_64_A68B; m++) {
            Mix *s = &r->mix[m];
            if (!s->used || depth[m] != d) continue;
            apply_effects(r, (int32_t)m, s);
            Mix *t = find_mix(r, s->dest);
            if (!t || t == s) continue;
            for (uint32_t i = 0; i < s->bufs; i++)
                for (uint32_t j = 0; j < t->bufs; j++) {
                    float g = s->volume * s->vol[i * MIXBUF_MAX + j];
                    if (g == 0) continue;
                    float *src = s->buf + i * N, *o = t->buf + j * N;
                    for (uint32_t k = 0; k < N; k++) o[k] += src[k] * g;
                }
        }
    }
    // final mix -> device sink (FL FR FC LFE BL BR) -> stereo
    Mix *f = find_mix(r, 0);
    if (f) apply_effects(r, 0, f);
    for (uint32_t i = 0; i < N; i++) {
        float ch[SF2C_AUDIO_N_6_E7F6] = {0};
        if (f && r->sink_used)
            for (int c = 0; c < r->sink_inputs && c < SF2C_AUDIO_N_6_E7F6; c++)
                if (r->sink_input[c] < f->bufs) ch[c] = f->buf[r->sink_input[c] * N + i] * f->volume;
        float L = ch[0], R = ch[1];
        if (r->sink_inputs >= SF2C_AUDIO_N_6_E7F6) {
            L += SF2C_AUDIO_N_0_707F_D12F * ch[SF2C_AUDIO_N_2_D473] + SF2C_AUDIO_N_0_251F_84A9 * ch[SF2C_AUDIO_N_3_4E07] + SF2C_AUDIO_N_0_707F_D12F * ch[SF2C_AUDIO_N_4_4B22];
            R += SF2C_AUDIO_N_0_707F_D12F * ch[SF2C_AUDIO_N_2_D473] + SF2C_AUDIO_N_0_251F_84A9 * ch[SF2C_AUDIO_N_3_4E07] + SF2C_AUDIO_N_0_707F_D12F * ch[SF2C_AUDIO_N_5_EF2D];
        }
        stereo[i * SF2C_AUDIO_N_2_D473] = clampf(L / SF2C_AUDIO_N_32768_F_7EA1, -1, 1);
        stereo[i * SF2C_AUDIO_N_2_D473 + 1] = clampf(R / SF2C_AUDIO_N_32768_F_7EA1, -1, 1);
    }
}

// Output gain: menu volume times LARECOMP_AUDIO_GAIN (applied live).
static void apply_gain(SDL_AudioStream *st) {
    static double env = -1;
    if (env < 0) env = getenv(SF2C_AUDIO_S_LARECOMP_AUDIO_GAIN_1F0B) ? atof(getenv(SF2C_AUDIO_S_LARECOMP_AUDIO_GAIN_1F0B)) : 1.0;
    float g = (float)(env * g_settings.volume);
    if (atomic_load(&movie_audio_active)) g = SF2C_AUDIO_N_0_0F_6895;
    if (st && SDL_GetAudioStreamGain(st) != g) SDL_SetAudioStreamGain(st, g);
}

static void wav_header(FILE *f, uint32_t bytes) {
    uint32_t h[SF2C_AUDIO_N_11_4FC8] = {SF2C_AUDIO_N_0X46464952_D2A4, SF2C_AUDIO_N_36_76A5 + bytes, SF2C_AUDIO_N_0X45564157_9E42, SF2C_AUDIO_N_0X20746D66_0B3F, SF2C_AUDIO_N_16_B17E, SF2C_AUDIO_N_0X00020003_637B, SF2C_AUDIO_N_48000_5D1D, SF2C_AUDIO_N_48000_5D1D * SF2C_AUDIO_N_8_2C62,
                      SF2C_AUDIO_N_0X00200008_3B4F, SF2C_AUDIO_N_0X61746164_097C, bytes};
    fseek(f, 0, SEEK_SET);
    fwrite(h, SF2C_AUDIO_N_4_4B22, SF2C_AUDIO_N_11_4FC8, f);
    fseek(f, 0, SEEK_END);
}

static void *renderer_tick(void *arg) {
    Renderer *r = arg;
    float out[SF2C_AUDIO_N_2_D473 * SF2C_AUDIO_N_4096_8B92];
    uint32_t N = r->sample_count > SF2C_AUDIO_N_4096_8B92 ? SF2C_AUDIO_N_4096_8B92 : r->sample_count;
    int frame_bytes = (int)(N * SF2C_AUDIO_N_2_D473 * sizeof(float));
    uint64_t next = 0;
    struct timespec now;
    int target = SF2C_AUDIO_N_4_4B22 * frame_bytes;  // queued audio to keep ahead of the device: >= 2 device periods
    if (r->stream) {
        SDL_AudioSpec dev;
        int dev_frames = 0;
        if (SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(r->stream), &dev, &dev_frames) && dev_frames > 0) {
            int t = (SF2C_AUDIO_N_2_D473 * dev_frames * (int)r->sample_rate / (dev.freq ? dev.freq : SF2C_AUDIO_N_48000_5D1D) + (int)N) * SF2C_AUDIO_N_2_D473 * (int)sizeof(float);
            if (t > target) target = t;
            LOG(SF2C_AUDIO_S_AUDREN_DEVICE_D_HZ_D_FRAMES__C330, dev.freq, dev_frames,
                target / (SF2C_AUDIO_N_2_D473 * (int)sizeof(float)));
        }
    }
    while (!r->quit) {
        if (r->stream) {
            while (SDL_GetAudioStreamQueued(r->stream) >= target && !r->quit) {
                struct timespec ts = {0, SF2C_AUDIO_N_1000000_6CCE};
                nanosleep(&ts, NULL);
            }
        } else {
            clock_gettime(CLOCK_MONOTONIC, &now);
            uint64_t t = now.tv_sec * SF2C_AUDIO_N_1000000000ULL_E860 + now.tv_nsec;
            if (!next) next = t;
            if (next > t) { struct timespec ts = {0, (long)(next - t)}; nanosleep(&ts, NULL); }
            next += (uint64_t)N * SF2C_AUDIO_N_1000000000ULL_E860 / r->sample_rate;
        }
        pthread_mutex_lock(&r->lock);
        if (r->state == 0) {
            render_frame(r, out);
            r->elapsed_frames++;
        } else {
            memset(out, 0, frame_bytes);
        }
        pthread_mutex_unlock(&r->lock);
        if (r->stream) { apply_gain(r->stream); SDL_PutAudioStreamData(r->stream, out, frame_bytes); }
        if (r->wav && r->state == 0) {
            fwrite(out, 1, frame_bytes, r->wav);
            r->wav_bytes += frame_bytes;
            if ((r->elapsed_frames & SF2C_AUDIO_N_255_9556) == 0) { wav_header(r->wav, r->wav_bytes); fflush(r->wav); }
        }
        pthread_mutex_lock(&g_klock);
        kernel_signal(r->event);
        pthread_mutex_unlock(&g_klock);
    }
    return NULL;
}

static void update_voice(Renderer *r, Voice *v, const uint8_t *p, uint8_t *out) {
    uint8_t is_new = p[SF2C_AUDIO_N_8_2C62];
    if (is_new) {
        memset(v, 0, sizeof *v);
        v->play_state = 1;
        v->mix_id = -1;
        v->splitter_id = SF2C_AUDIO_N_0XFFFFFFFF_BEFA;
        for (int i = 0; i < WB_COUNT; i++) v->wb[i].sent = 1;
        v->is_new = 1;
    }
    v->in_use = 1;
    uint8_t ps = p[SF2C_AUDIO_N_10_4A44];
    if (ps == 0) v->play_state = 0;
    else if (ps == 1) { if (v->play_state != 1) v->play_state = SF2C_AUDIO_N_2_D473; }
    else if (ps == SF2C_AUDIO_N_2_D473) v->play_state = SF2C_AUDIO_N_3_4E07;
    v->fmt = p[SF2C_AUDIO_N_11_4FC8];
    memcpy(&v->rate, p + SF2C_AUDIO_N_12_6B51, SF2C_AUDIO_N_4_4B22);
    uint32_t ch;
    memcpy(&ch, p + SF2C_AUDIO_N_24_C235, SF2C_AUDIO_N_4_4B22);
    v->chans = ch > SF2C_AUDIO_N_6_E7F6 ? SF2C_AUDIO_N_6_E7F6 : (uint8_t)ch;
    memcpy(&v->pitch, p + SF2C_AUDIO_N_28_59E1, SF2C_AUDIO_N_4_4B22);
    memcpy(&v->volume, p + SF2C_AUDIO_N_32_E29C, SF2C_AUDIO_N_4_4B22);
    memcpy(&v->wb_count, p + SF2C_AUDIO_N_60_39FA, SF2C_AUDIO_N_4_4B22);
    memcpy(&v->ds_addr, p + SF2C_AUDIO_N_72_8722, SF2C_AUDIO_N_8_2C62);
    memcpy(&v->mix_id, p + SF2C_AUDIO_N_88_8B94, SF2C_AUDIO_N_4_4B22);
    memcpy(&v->splitter_id, p + SF2C_AUDIO_N_92_8241, SF2C_AUDIO_N_4_4B22);
    memcpy(v->chan_ids, p + SF2C_AUDIO_N_320_8882, SF2C_AUDIO_N_24_C235);
    v->flush += p[SF2C_AUDIO_N_345_DA70];
    memcpy(&v->behaviour, p + SF2C_AUDIO_N_348_06B2, SF2C_AUDIO_N_2_D473);
    if (p[SF2C_AUDIO_N_344_02E6]) v->drop = 0;
    for (int f = 0; f < SF2C_AUDIO_N_2_D473; f++) {  // BiquadFilterParameter: enable, reserved, s16 numerator[3], denominator[2] (Q14)
        const uint8_t *b = p + SF2C_AUDIO_N_36_76A5 + f * SF2C_AUDIO_N_12_6B51;
        if (b[0] && !v->bq_on[f]) {
            v->bq_reset[f] = 1;
            if (getenv(SF2C_AUDIO_S_LARECOMP_AUDIO_LOG_866C)) {
                int16_t k[SF2C_AUDIO_N_5_EF2D];
                memcpy(k, b + SF2C_AUDIO_N_2_D473, SF2C_AUDIO_N_10_4A44);
                LOG(SF2C_AUDIO_S_AUDREN_V_LD_BIQUAD_D_ON_D_D__1CFA, (long)(v - r->voice), f, k[0], k[1], k[SF2C_AUDIO_N_2_D473], k[SF2C_AUDIO_N_3_4E07], k[SF2C_AUDIO_N_4_4B22]);
            }
        }
        v->bq_on[f] = b[0];
        int16_t c[SF2C_AUDIO_N_5_EF2D];
        memcpy(c, b + SF2C_AUDIO_N_2_D473, SF2C_AUDIO_N_10_4A44);
        for (int k = 0; k < SF2C_AUDIO_N_5_EF2D; k++) v->bq[f][k] = c[k] / SF2C_AUDIO_N_16384_0F_EB94;
    }
    if (v->splitter_id != SF2C_AUDIO_N_0XFFFFFFFF_BEFA && v->mix_id < 0) {
        static int warned;
        if (!warned++) LOG(SF2C_AUDIO_S_AUDREN_SPLITTER_DESTINATIONS_9676);
    }
    ChanState *cs0 = &r->chst[v->chan_ids[0] % r->voices];
    if (is_new)
        for (int c = 0; c < v->chans; c++) memset(r->chst[v->chan_ids[c] % r->voices].valid, 0, WB_COUNT);
    for (int i = 0; i < WB_COUNT; i++) {
        const uint8_t *w = p + SF2C_AUDIO_N_96_7B1A + i * SF2C_AUDIO_N_0X38_AE4F;
        WaveBuf *wb = &v->wb[i];
        if (!cs0->valid[i] && wb->sent) wb->addr = 0;
        if (!w[SF2C_AUDIO_N_26_5F9C]) {  // !SentToServer: a new buffer for the DSP
            memcpy(&wb->addr, w, SF2C_AUDIO_N_8_2C62);
            memcpy(&wb->size, w + SF2C_AUDIO_N_8_2C62, SF2C_AUDIO_N_8_2C62);
            memcpy(&wb->start, w + SF2C_AUDIO_N_16_B17E, SF2C_AUDIO_N_4_4B22);
            memcpy(&wb->end, w + SF2C_AUDIO_N_20_F5CA, SF2C_AUDIO_N_4_4B22);
            wb->loop = w[SF2C_AUDIO_N_24_C235];
            wb->eos = w[SF2C_AUDIO_N_25_B7A5];
            memcpy(&wb->loop_count, w + SF2C_AUDIO_N_28_59E1, SF2C_AUDIO_N_4_4B22);
            memcpy(&wb->ctx, w + SF2C_AUDIO_N_32_E29C, SF2C_AUDIO_N_8_2C62);
            memcpy(&wb->loop_start, w + SF2C_AUDIO_N_48_9801, SF2C_AUDIO_N_4_4B22);
            memcpy(&wb->loop_end, w + SF2C_AUDIO_N_52_41CF, SF2C_AUDIO_N_4_4B22);
            if ((r->revision >> SF2C_AUDIO_N_24_C235) - SF2C_AUDIO_CH_0_59A9 < SF2C_AUDIO_N_8_2C62) {  // WaveBuffer version 1: loop forever over [start, end)
                wb->loop_count = -1;
                wb->loop_start = wb->loop_end = 0;
            }
            wb->sent = 0;
            static int dumped;
            if (getenv(SF2C_AUDIO_S_LARECOMP_AUDIO_DUMPWB_5E0C) && !dumped && wb->size > SF2C_AUDIO_N_0X100000_0A2A && v->fmt == SF2C_AUDIO_N_6_E7F6 && v->ds_addr) {
                dumped = 1;
                FILE *f = fopen(SF2C_AUDIO_S_BUILD_WB_DUMP_BIN_BE1E, SF2C_AUDIO_S_WB_3D6A);
                if (f) {
                    uint32_t hdr[SF2C_AUDIO_N_4_4B22] = {(uint32_t)wb->size, wb->start, wb->end, (uint32_t)(wb->ctx != 0)};
                    fwrite(hdr, SF2C_AUDIO_N_4_4B22, SF2C_AUDIO_N_4_4B22, f);
                    fwrite((const void *)(uintptr_t)v->ds_addr, 1, SF2C_AUDIO_N_32_E29C, f);
                    int16_t ctx[SF2C_AUDIO_N_3_4E07] = {0};
                    if (wb->ctx) memcpy(ctx, (const void *)(uintptr_t)wb->ctx, SF2C_AUDIO_N_6_E7F6);
                    fwrite(ctx, SF2C_AUDIO_N_2_D473, SF2C_AUDIO_N_3_4E07, f);
                    fwrite((const void *)(uintptr_t)wb->addr, 1, wb->size, f);
                    fclose(f);
                    LOG(SF2C_AUDIO_S_AUDREN_DUMPED_WAVE_BUFFER_LL_EA75, (unsigned long long)wb->addr,
                        (unsigned long long)wb->size, (unsigned long long)v->ds_addr, ctx[0], ctx[1], ctx[SF2C_AUDIO_N_2_D473]);
                }
            }
            static int vlog = -1;
            if (vlog < 0) vlog = getenv(SF2C_AUDIO_S_LARECOMP_AUDIO_LOG_866C) ? 1 : 0;
            if (vlog)
                LOG(SF2C_AUDIO_S_AUDREN_F_LLU_V_LD_WB_D_FMT_U_9994
                    SF2C_AUDIO_S_U_U_LOOP_U_D_EOS_U_CTX_LLX_D_06B8, (unsigned long long)r->elapsed_frames,
                    (long)(v - r->voice), i, v->fmt, v->rate, v->chans, v->pitch, v->volume, v->mix_id, v->chan_ids[0],
                    (unsigned long long)wb->addr, (unsigned long long)wb->size, wb->start, wb->end, wb->loop,
                    wb->loop_count, wb->eos, (unsigned long long)wb->ctx, (unsigned long long)v->ds_addr);
            if (vlog && v->ds_addr) {
                const int16_t *k = (const int16_t *)(uintptr_t)v->ds_addr;
                LOG(SF2C_AUDIO_S_AUDREN_COEFS_V_LD_4306, (long)(v - r->voice));
                for (int j = 0; j < SF2C_AUDIO_N_19_9400; j++) LOG(SF2C_AUDIO_S_D_7E40, k[j]);
                LOG(SF2C_AUDIO_S_N_D7A5);
            }
        }
    }
    // VoiceOutStatus
    if (is_new || v->is_new) {
        memset(out, 0, SF2C_AUDIO_N_0X10_BF9C);
    } else {
        memcpy(out, &cs0->played, SF2C_AUDIO_N_8_2C62);
        memcpy(out + SF2C_AUDIO_N_8_2C62, &cs0->consumed, SF2C_AUDIO_N_4_4B22);
        out[SF2C_AUDIO_N_12_6B51] = v->drop;
    }
}

static void update_mix(Renderer *r, const uint8_t *p, int id) {
    if (id < 0 || (uint32_t)id >= r->nmix) return;
    Mix *m = &r->mix[id];
    memcpy(&m->volume, p, SF2C_AUDIO_N_4_4B22);
    memcpy(&m->bufs, p + SF2C_AUDIO_N_8_2C62, SF2C_AUDIO_N_4_4B22);
    if (m->bufs > MIXBUF_MAX) m->bufs = MIXBUF_MAX;
    m->used = p[SF2C_AUDIO_N_12_6B51];
    memcpy(&m->dest, p + SF2C_AUDIO_N_0X924_5BD2, SF2C_AUDIO_N_4_4B22);
    memcpy(&m->dest_splitter, p + SF2C_AUDIO_N_0X928_3A34, SF2C_AUDIO_N_4_4B22);
    memcpy(m->vol, p + SF2C_AUDIO_N_0X24_EB21, sizeof m->vol);
    if (!m->buf) m->buf = calloc(MIXBUF_MAX * r->sample_count, sizeof(float));
    LOG(SF2C_AUDIO_S_AUDREN_MIX_D_USED_U_VOL_3F_B_F1E1, id, m->used, m->volume, m->bufs, m->dest);
    for (int i = 0; i < MIXBUF_MAX * MIXBUF_MAX; i++)
        if (m->vol[i] != 0) LOG(SF2C_AUDIO_S_D_D_3F_C146, i / MIXBUF_MAX, i % MIXBUF_MAX, m->vol[i]);
    LOG(SF2C_AUDIO_S_N_D7A5);
}

// RequestUpdate: input is an UpdateDataHeader + sections; write a matching output.
static void request_update(Renderer *r, IpcReq *req, IpcResp *resp) {
    IpcBuf *in = in_buf(req, 0), *out = out_buf(req, 0);
    if (!in || !out || !in->size || !out->size) return;
    const uint8_t *ip = (const uint8_t *)(uintptr_t)in->addr;
    uint8_t *op = (uint8_t *)(uintptr_t)out->addr;
    const uint32_t *h = (const uint32_t *)ip;
    uint32_t behavior_sz = h[1], pools_sz = h[SF2C_AUDIO_N_2_D473], voices_sz = h[SF2C_AUDIO_N_3_4E07], chans_sz = h[SF2C_AUDIO_N_4_4B22], effects_sz = h[SF2C_AUDIO_N_5_EF2D];
    uint32_t mixes_sz = h[SF2C_AUDIO_N_6_E7F6], sinks_sz = h[SF2C_AUDIO_N_7_7902], perf_sz = h[SF2C_AUDIO_N_8_2C62];
    {
        static int dump_n = -1;
        static long count;
        if (dump_n < 0) dump_n = getenv(SF2C_AUDIO_S_LARECOMP_AUDIO_DUMP_E9E4) ? 1 : 0;
        count++;
        if (dump_n && (count == SF2C_AUDIO_N_300_983B || count == SF2C_AUDIO_N_2000_81A8 || count == SF2C_AUDIO_N_6000_8D28 || count == SF2C_AUDIO_N_12000_C5D1)) {
            char path[SF2C_AUDIO_N_64_A68B];
            snprintf(path, sizeof path, SF2C_AUDIO_S_BUILD_AUDREN_IN_LD_BIN_9D39, count);
            FILE *f = fopen(path, SF2C_AUDIO_S_WB_3D6A);
            if (f) { fwrite(ip, 1, in->size, f); fclose(f); }
            LOG(SF2C_AUDIO_S_AUDREN_DUMPED_S_LLU_BYTES_RE_9138, path, (unsigned long long)in->size, r->revision);
        }
    }
    uint32_t npools = pools_sz / SF2C_AUDIO_N_0X20_A766, nvoices = voices_sz / SF2C_AUDIO_N_0X170_9B54, neffects = effects_sz / SF2C_AUDIO_N_0XC0_EB51;
    uint32_t nchans = chans_sz / SF2C_AUDIO_N_0X70_B0F6, nsinks = sinks_sz / SF2C_AUDIO_N_0X140_A49E;
    if (nvoices > r->voices) nvoices = r->voices;
    if (nchans > r->voices) nchans = r->voices;
    memset(op, 0, out->size);
    uint32_t *oh = (uint32_t *)op;
    oh[0] = h[0];
    uint32_t o = SF2C_AUDIO_N_0X40_1AF3;
    pthread_mutex_lock(&r->lock);
    // memory pools: acknowledge attach/detach requests
    const uint8_t *pin = ip + SF2C_AUDIO_N_0X40_1AF3 + behavior_sz;
    oh[SF2C_AUDIO_N_2_D473] = npools * SF2C_AUDIO_N_0X10_BF9C;
    for (uint32_t i = 0; i < npools && o + SF2C_AUDIO_N_0X10_BF9C <= out->size; i++, o += SF2C_AUDIO_N_0X10_BF9C) {
        uint32_t st = *(const uint32_t *)(pin + i * SF2C_AUDIO_N_0X20_A766 + SF2C_AUDIO_N_0X10_BF9C);
        *(uint32_t *)(op + o) = st == SF2C_AUDIO_N_4_4B22 ? SF2C_AUDIO_N_5_EF2D : st == SF2C_AUDIO_N_2_D473 ? SF2C_AUDIO_N_3_4E07 : st;
    }
    // voice channel resources
    const uint8_t *cin = pin + pools_sz;
    for (uint32_t i = 0; i < nchans; i++) {
        const uint8_t *c = cin + i * SF2C_AUDIO_N_0X70_B0F6;
        uint32_t id;
        memcpy(&id, c, SF2C_AUDIO_N_4_4B22);
        if (id >= r->voices) continue;
        r->chres[id].used = c[SF2C_AUDIO_N_0X64_3246];
        if (c[SF2C_AUDIO_N_0X64_3246]) memcpy(r->chres[id].mix, c + SF2C_AUDIO_N_4_4B22, sizeof r->chres[id].mix);
    }
    // voices
    const uint8_t *vin = cin + chans_sz;
    oh[SF2C_AUDIO_N_3_4E07] = r->voices * SF2C_AUDIO_N_0X10_BF9C;
    for (uint32_t i = 0; i < r->voices; i++) r->voice[i].in_use = 0;
    for (uint32_t i = 0; i < nvoices && o + SF2C_AUDIO_N_0X10_BF9C <= out->size; i++, o += SF2C_AUDIO_N_0X10_BF9C) {
        const uint8_t *v = vin + i * SF2C_AUDIO_N_0X170_9B54;
        if (v[SF2C_AUDIO_N_9_1958]) update_voice(r, &r->voice[i], v, op + o);
    }
    // effects (not processed): report them as enabled/disabled per request
    const uint8_t *ein = vin + voices_sz;
    oh[SF2C_AUDIO_N_5_EF2D] = neffects * SF2C_AUDIO_N_0X10_BF9C;
    for (uint32_t i = 0; i < neffects && o + SF2C_AUDIO_N_0X10_BF9C <= out->size; i++, o += SF2C_AUDIO_N_0X10_BF9C) {
        const uint8_t *e = ein + i * SF2C_AUDIO_N_0XC0_EB51;
        op[o] = e[SF2C_AUDIO_N_2_D473] ? SF2C_AUDIO_N_3_4E07 : SF2C_AUDIO_N_4_4B22;  // EffectState Enabled / Disabled
        if (i < r->effects) effect_update(&r->fx[i], e);
        static uint32_t seen_types;
        if (e[0] && e[0] < SF2C_AUDIO_N_32_E29C && !(seen_types & (SF2C_AUDIO_N_1U_B8E6 << e[0]))) {
            seen_types |= SF2C_AUDIO_N_1U_B8E6 << e[0];
            LOG(SF2C_AUDIO_S_AUDREN_EFFECT_TYPE_U_USED_S__CAEF, e[0], e[0] == SF2C_AUDIO_N_4_4B22 ? SF2C_AUDIO_S_EMPTY_12AE : SF2C_AUDIO_S_NOT_IMPLEMENTED_6670, i,
                *(const int32_t *)(e + SF2C_AUDIO_N_4_4B22), e[SF2C_AUDIO_N_2_D473]);
        }
        if (e[0] && getenv(SF2C_AUDIO_S_LARECOMP_AUDIO_LOG_866C)) {
            static uint8_t last[SF2C_AUDIO_N_16_B17E][SF2C_AUDIO_N_0XC0_EB51];
            if (i < SF2C_AUDIO_N_16_B17E && memcmp(last[i], e, SF2C_AUDIO_N_0XC0_EB51)) {
                memcpy(last[i], e, SF2C_AUDIO_N_0XC0_EB51);
                LOG(SF2C_AUDIO_S_AUDREN_F_LLU_EFFECT_U_EB57, (unsigned long long)r->elapsed_frames, i);
                for (int k = 0; k < SF2C_AUDIO_N_0X70_B0F6; k += SF2C_AUDIO_N_4_4B22) LOG(SF2C_AUDIO_S_08X_981A, *(const uint32_t *)(e + k));
                LOG(SF2C_AUDIO_S_N_D7A5);
            }
        }
    }
    const uint8_t *min = ein + effects_sz;
    if (r->splitters && *(const uint32_t *)min == SF2C_AUDIO_N_0X48444E53_8845) {
        static int warned;
        if (!warned++) LOG(SF2C_AUDIO_S_AUDREN_SPLITTER_SECTION_PRES_FFBF);
    }
    // mixes: the section ends right before the sinks
    const uint8_t *sin = ip + h[SF2C_AUDIO_N_15_E629] - perf_sz - sinks_sz;
    min = sin - mixes_sz;
    if ((r->revision >> SF2C_AUDIO_N_24_C235) - SF2C_AUDIO_CH_0_59A9 >= SF2C_AUDIO_N_7_7902 && *(const uint32_t *)min == SF2C_AUDIO_N_0XCAFEBABE_3A89) {
        uint32_t n = *(const uint32_t *)(min + SF2C_AUDIO_N_4_4B22);
        for (uint32_t i = 0; i < n; i++) {
            const uint8_t *m = min + SF2C_AUDIO_N_0X20_A766 + i * SF2C_AUDIO_N_0X930_7948;
            int32_t id;
            memcpy(&id, m + SF2C_AUDIO_N_16_B17E, SF2C_AUDIO_N_4_4B22);
            update_mix(r, m, id);
        }
    } else {
        for (uint32_t i = 0; i < mixes_sz / SF2C_AUDIO_N_0X930_7948; i++) update_mix(r, min + i * SF2C_AUDIO_N_0X930_7948, i);
    }
    // sinks
    oh[SF2C_AUDIO_N_7_7902] = nsinks * SF2C_AUDIO_N_0X20_A766;
    for (uint32_t i = 0; i < nsinks; i++, o += SF2C_AUDIO_N_0X20_A766) {
        const uint8_t *s = sin + i * SF2C_AUDIO_N_0X140_A49E;
        if (s[0] == 1) {  // device sink
            r->sink_used = s[1];
            uint32_t n;
            memcpy(&n, s + SF2C_AUDIO_N_0X20_A766 + SF2C_AUDIO_N_0X100_BE6E, SF2C_AUDIO_N_4_4B22);
            r->sink_inputs = n > SF2C_AUDIO_N_6_E7F6 ? SF2C_AUDIO_N_6_E7F6 : n;
            memcpy(r->sink_input, s + SF2C_AUDIO_N_0X124_5B0A, SF2C_AUDIO_N_6_E7F6);
        }
    }
    uint64_t elapsed = r->elapsed_frames;
    pthread_mutex_unlock(&r->lock);
    oh[SF2C_AUDIO_N_8_2C62] = perf_sz ? SF2C_AUDIO_N_0X10_BF9C : 0;
    o += oh[SF2C_AUDIO_N_8_2C62];
    oh[1] = SF2C_AUDIO_N_0XB0_497E;  // behavior / error info
    o += SF2C_AUDIO_N_0XB0_497E;
    oh[SF2C_AUDIO_N_10_4A44] = SF2C_AUDIO_N_0X10_BF9C;  // render info: elapsed frame count
    if (o + SF2C_AUDIO_N_0X10_BF9C <= out->size) *(uint64_t *)(op + o) = elapsed;
    o += SF2C_AUDIO_N_0X10_BF9C;
    oh[SF2C_AUDIO_N_15_E629] = o;
    (void)resp;
}

static void renderer_open_output(Renderer *r) {
    if (getenv(SF2C_AUDIO_S_LARECOMP_AUDIO_WAV_F745)) {
        r->wav = fopen(getenv(SF2C_AUDIO_S_LARECOMP_AUDIO_WAV_F745), SF2C_AUDIO_S_WB_3D6A);
        if (r->wav) wav_header(r->wav, 0);
    }
    if (getenv(SF2C_AUDIO_S_LARECOMP_NO_AUDIO_8230) && strcmp(getenv(SF2C_AUDIO_S_LARECOMP_NO_AUDIO_8230), SF2C_AUDIO_S_0_9808)) return;
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) { LOG(SF2C_AUDIO_S_AUDREN_SDL_AUDIO_INIT_FAILED_FA38, SDL_GetError()); return; }
    SDL_AudioSpec spec = {SDL_AUDIO_F32, SF2C_AUDIO_N_2_D473, (int)r->sample_rate};
    r->stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (!r->stream) { LOG(SF2C_AUDIO_S_AUDREN_SDL_AUDIO_OPEN_FAILED_9841, SDL_GetError()); return; }
    if (getenv(SF2C_AUDIO_S_LARECOMP_AUDIO_GAIN_1F0B)) SDL_SetAudioStreamGain(r->stream, (float)atof(getenv(SF2C_AUDIO_S_LARECOMP_AUDIO_GAIN_1F0B)));
    SDL_ResumeAudioStreamDevice(r->stream);
}

static void audio_renderer(Service *s, IpcReq *req, IpcResp *resp) {
    Renderer *r = s->state;
    switch (req->cmd) {
    case 0: OUT(uint32_t, 0) = r->sample_rate; resp_size(resp, SF2C_AUDIO_N_4_4B22); break;
    case 1: OUT(uint32_t, 0) = r->sample_count; resp_size(resp, SF2C_AUDIO_N_4_4B22); break;
    case SF2C_AUDIO_N_2_D473: OUT(uint32_t, 0) = r->mix_buffers; resp_size(resp, SF2C_AUDIO_N_4_4B22); break;
    case SF2C_AUDIO_N_3_4E07: OUT(uint32_t, 0) = r->state; resp_size(resp, SF2C_AUDIO_N_4_4B22); break;
    case SF2C_AUDIO_N_4_4B22: case SF2C_AUDIO_N_10_4A44: request_update(r, req, resp); break;
    case SF2C_AUDIO_N_5_EF2D: r->state = 0; break;
    case SF2C_AUDIO_N_6_E7F6: r->state = 1; break;
    case SF2C_AUDIO_N_7_7902: resp->copy_handles[resp->ncopy++] = r->event_r; break;
    case SF2C_AUDIO_N_8_2C62: break;
    case SF2C_AUDIO_N_9_1958: OUT(uint32_t, 0) = SF2C_AUDIO_N_100_AD57; resp_size(resp, SF2C_AUDIO_N_4_4B22); break;
    default: LOG(SF2C_AUDIO_S_AUDREN_IAUDIORENDERER_CMD_U__A4C9, req->cmd);
    }
}

static uint32_t new_event_handle(void) {
    uint32_t w, r;
    kernel_create_event_pair(&w, &r);
    return r;
}

static void audio_device(Service *s, IpcReq *req, IpcResp *resp) {
    (void)s;
    switch (req->cmd) {
    case 0: case SF2C_AUDIO_N_6_E7F6: {  // ListAudioDeviceName(Auto)
        IpcBuf *b = out_buf(req, 0);
        if (b && b->size >= SF2C_AUDIO_N_0X100_BE6E) strcpy((char *)(uintptr_t)b->addr, SF2C_AUDIO_S_AUDIOTVOUTPUT_B0FF);
        OUT(uint32_t, 0) = 1;
        resp_size(resp, SF2C_AUDIO_N_4_4B22);
        break;
    }
    case SF2C_AUDIO_N_3_4E07: case SF2C_AUDIO_N_10_4A44: {  // GetActiveAudioDeviceName(Auto)
        IpcBuf *b = out_buf(req, 0);
        if (b && b->size) strncpy((char *)(uintptr_t)b->addr, SF2C_AUDIO_S_AUDIOTVOUTPUT_B0FF, b->size);
        break;
    }
    case SF2C_AUDIO_N_4_4B22: case SF2C_AUDIO_N_11_4FC8: case SF2C_AUDIO_N_12_6B51: resp->copy_handles[resp->ncopy++] = new_event_handle(); break;
    case SF2C_AUDIO_N_5_EF2D: OUT(uint32_t, 0) = SF2C_AUDIO_N_2_D473; resp_size(resp, SF2C_AUDIO_N_4_4B22); break;  // GetActiveChannelCount
    case SF2C_AUDIO_N_8_2C62: OUT(float, 0) = SF2C_AUDIO_N_1_0F_CA71; resp_size(resp, SF2C_AUDIO_N_4_4B22); break;
    default: break;
    }
}

typedef struct AudioOut AudioOut;
static void audio_track(Renderer *r, AudioOut *a);

SERVICE(SF2C_AUDIO_S_AUDREN_U_C818, audren_u) {
    (void)s;
    switch (req->cmd) {
    case 0: {  // OpenAudioRenderer(params, work size, aruid, pid, transfer mem, process)
        Renderer *r = calloc(1, sizeof *r);
        const uint32_t *p = (const uint32_t *)req->data;
        r->sample_rate = p[0]; r->sample_count = p[1]; r->mix_buffers = p[SF2C_AUDIO_N_2_D473]; r->submixes = p[SF2C_AUDIO_N_3_4E07];
        r->voices = p[SF2C_AUDIO_N_4_4B22]; r->sinks = p[SF2C_AUDIO_N_5_EF2D]; r->effects = p[SF2C_AUDIO_N_6_E7F6]; r->perf_frames = p[SF2C_AUDIO_N_7_7902];
        r->splitters = p[SF2C_AUDIO_N_9_1958];
        r->revision = p[SF2C_AUDIO_N_12_6B51];
        r->state = 1;
        if (r->voices > SF2C_AUDIO_N_1024_E39E) r->voices = SF2C_AUDIO_N_1024_E39E;
        r->nmix = r->submixes + 1;
        pthread_mutex_init(&r->lock, NULL);
        r->voice = calloc(r->voices, sizeof(Voice));
        r->chres = calloc(r->voices, sizeof(ChanRes));
        r->chst = calloc(r->voices, sizeof(ChanState));
        r->mix = calloc(r->nmix, sizeof(Mix));
        r->fx = calloc(r->effects ? r->effects : 1, sizeof(Effect));
        renderer_open_output(r);
        r->event = kernel_create_event_pair(&r->event_w, &r->event_r);
        pthread_create(&r->thread, NULL, renderer_tick, r);
        audio_track(r, NULL);
        LOG(SF2C_AUDIO_S_AUDREN_RENDERER_U_HZ_U_SAMPL_9245,
            r->sample_rate, r->sample_count, r->voices, r->nmix, r->mix_buffers, r->splitters, r->effects);
        ipc_return_object(resp, service_new(SF2C_AUDIO_S_IAUDIORENDERER_6CEF, audio_renderer, r));
        break;
    }
    case 1: OUT(uint64_t, 0) = SF2C_AUDIO_N_0X1000000_771C; resp_size(resp, SF2C_AUDIO_N_8_2C62); break;  // GetWorkBufferSize: 16 MB
    case SF2C_AUDIO_N_2_D473: case SF2C_AUDIO_N_4_4B22: ipc_return_object(resp, service_new(SF2C_AUDIO_S_IAUDIODEVICE_66FD, audio_device, NULL)); break;
    default: LOG(SF2C_AUDIO_S_AUDREN_CMD_U_OK_N_C46D, req->cmd);
    }
}

// ------------------------------------------------------------------ audout (buffers released in real time)
typedef struct AudioOut {
    uint32_t rate, channels, state;
    uint64_t tags[SF2C_AUDIO_N_64_A68B], sizes[SF2C_AUDIO_N_64_A68B], datas[SF2C_AUDIO_N_64_A68B];
    SDL_AudioStream *stream;
    FILE *wav;
    uint32_t wav_bytes;
    int queued, released_n;
    uint64_t released[SF2C_AUDIO_N_64_A68B];
    uint32_t ev_r, ev_w;
    KEvent *event;
    uint64_t played;
    uint64_t idle_tick_ns;  // idle release-event cadence (real audout ticks the driver at ~2-5 ms)
    pthread_mutex_t lock;
    pthread_t thread;
    volatile bool quit;  // reboot (audio_reset)
} AudioOut;

// Every renderer / audio out of this boot: a reboot stops their threads (they read guest memory and signal this
// boot's kernel events) and closes their output streams.
static Renderer *g_rens[SF2C_AUDIO_N_8_2C62];
static AudioOut *g_outs[SF2C_AUDIO_N_8_2C62];
static pthread_mutex_t g_track_lock = PTHREAD_MUTEX_INITIALIZER;
static void audio_track(Renderer *r, AudioOut *a) {
    pthread_mutex_lock(&g_track_lock);
    for (int i = 0; i < SF2C_AUDIO_N_8_2C62; i++) {
        if (r && !g_rens[i]) { g_rens[i] = r; r = NULL; }
        if (a && !g_outs[i]) { g_outs[i] = a; a = NULL; }
    }
    pthread_mutex_unlock(&g_track_lock);
}

static void audout_free(AudioOut *a) {
    a->quit = true;
    pthread_join(a->thread, NULL);
    if (a->stream) SDL_DestroyAudioStream(a->stream);
    if (a->wav) fclose(a->wav);
    free(a);
}

// The game closes its IAudioOut after every movie (the title's attract loop opens one per showing): stop the
// thread and the output stream then, or each showing leaves both running and fills g_outs.
static void audout_close(Service *s) {
    AudioOut *a = s->state;
    bool mine = false;
    pthread_mutex_lock(&g_track_lock);
    for (int i = 0; i < SF2C_AUDIO_N_8_2C62; i++) if (g_outs[i] == a) { g_outs[i] = NULL; mine = true; }
    pthread_mutex_unlock(&g_track_lock);
    if (!mine) return;  // (a reboot already closed it)
    s->state = NULL;
    audout_free(a);
    LOG(SF2C_AUDIO_S_AUDOUT_CLOSED_N_951E);
}

RT_ON_RESET(audio_reset) {
    atomic_store(&movie_audio_active, false);
    int n = 0;
    for (int i = 0; i < SF2C_AUDIO_N_8_2C62; i++) {
        Renderer *r = g_rens[i];
        if (r) {
            r->quit = true;
            pthread_join(r->thread, NULL);
            if (r->stream) SDL_DestroyAudioStream(r->stream);
            if (r->wav) { wav_header(r->wav, r->wav_bytes); fclose(r->wav); }
            for (uint32_t m = 0; m < r->nmix; m++) free(r->mix[m].buf);
            free(r->voice), free(r->chres), free(r->chst), free(r->mix), free(r->fx);  // (reverb delay lines leak)
            free(r);
            g_rens[i] = NULL;
            n++;
        }
        if (g_outs[i]) { audout_free(g_outs[i]); g_outs[i] = NULL; n++; }
    }
    LOG(SF2C_AUDIO_S_AUDIO_REBOOT_D_OUTPUTS_CLOSE_C58A, n);
}

// Buffers are copied into an SDL stream and released once the stream has drained below its target, so the
// stream plays the role of the hardware queue. Without a device, buffers are released in real time.
static void *audout_thread(void *arg) {
    AudioOut *a = arg;
    uint32_t ch = a->channels ? a->channels : SF2C_AUDIO_N_2_D473;
    int target = 0;
    if (a->stream) {
        SDL_AudioSpec dev;
        int dev_frames = 0;
        if (!SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(a->stream), &dev, &dev_frames) || dev_frames <= 0)
            dev_frames = SF2C_AUDIO_N_1024_E39E;
        target = SF2C_AUDIO_N_2_D473 * dev_frames * SF2C_AUDIO_N_2_D473 * (int)ch;  // bytes of s16 at the source rate (close enough at 48 kHz)
    }
    while (!a->quit) {
        pthread_mutex_lock(&a->lock);
        uint64_t size = 0, tag = 0, data = 0;
        if (a->state == 0 && a->queued) { size = a->sizes[0]; tag = a->tags[0]; data = a->datas[0]; }
        pthread_mutex_unlock(&a->lock);
        if (!size) {
            // Idle tick: real audout is a driver state-machine that keeps the release event firing
            // (~2-5 ms cadence) even with an empty queue. Our guest audio thread registered this
            // pair right after OpenAudioOut and parks on it forever otherwise — boots hang here.
            struct timespec cts;
            clock_gettime(CLOCK_MONOTONIC, &cts);
            uint64_t tns = (uint64_t)cts.tv_sec * SF2C_AUDIO_N_1000000000ULL_E860 + (uint64_t)cts.tv_nsec;
            bool fire = tns - a->idle_tick_ns >= SF2C_AUDIO_N_8000000ULL_CF6C;
            if (fire) a->idle_tick_ns = tns;
            struct timespec ts = {0, SF2C_AUDIO_N_1000000_6CCE};
            nanosleep(&ts, NULL);
            if (fire) {
                pthread_mutex_lock(&g_klock);
                kernel_signal(a->event);
                pthread_mutex_unlock(&g_klock);
            }
            continue;
        }
        uint64_t frames = size / (SF2C_AUDIO_N_2_D473 * ch);
        if (a->stream) {
            apply_gain(a->stream);
            if (data) SDL_PutAudioStreamData(a->stream, (const void *)(uintptr_t)data, (int)size);
            while (SDL_GetAudioStreamQueued(a->stream) > target && !a->quit) { struct timespec ts = {0, SF2C_AUDIO_N_1000000_6CCE}; nanosleep(&ts, NULL); }
        } else {
            uint64_t ns = frames * SF2C_AUDIO_N_1000000000ULL_E860 / (a->rate ? a->rate : SF2C_AUDIO_N_48000_5D1D);
            struct timespec ts = {(time_t)(ns / SF2C_AUDIO_N_1000000000ULL_E860), (long)(ns % SF2C_AUDIO_N_1000000000ULL_E860)};
            nanosleep(&ts, NULL);
        }
        if (a->wav && data) { fwrite((const void *)(uintptr_t)data, 1, size, a->wav); a->wav_bytes += (uint32_t)size; }
        pthread_mutex_lock(&a->lock);
        if (a->queued && a->tags[0] == tag) {
            if (a->released_n < SF2C_AUDIO_N_64_A68B) a->released[a->released_n++] = a->tags[0];
            memmove(a->tags, a->tags + 1, (a->queued - 1) * SF2C_AUDIO_N_8_2C62);
            memmove(a->sizes, a->sizes + 1, (a->queued - 1) * SF2C_AUDIO_N_8_2C62);
            memmove(a->datas, a->datas + 1, (a->queued - 1) * SF2C_AUDIO_N_8_2C62);
            a->queued--;
            a->played += frames;
        }
        pthread_mutex_unlock(&a->lock);
        pthread_mutex_lock(&g_klock);
        kernel_signal(a->event);
        pthread_mutex_unlock(&g_klock);
    }
    return NULL;
}

static void audio_out(Service *s, IpcReq *req, IpcResp *resp) {
    AudioOut *a = s->state;
    if (getenv(SF2C_AUDIO_S_LARECOMP_AUDIO_LOG_866C) && req->cmd != SF2C_AUDIO_N_3_4E07 && req->cmd != SF2C_AUDIO_N_7_7902) {
        IpcBuf *ob = out_buf(req, 0);
        LOG(SF2C_AUDIO_S_AUDOUT_CMD_U_OUT_LLX_LLX_N_3FBD, req->cmd, ob ? (unsigned long long)ob->addr : SF2C_AUDIO_N_0ULL_DF1B,
            ob ? (unsigned long long)ob->size : SF2C_AUDIO_N_0ULL_DF1B);
    }
    switch (req->cmd) {
    case 0: OUT(uint32_t, 0) = a->state; resp_size(resp, SF2C_AUDIO_N_4_4B22); break;   // GetAudioOutState
    case 1: a->state = 0; break;                                      // Start
    case SF2C_AUDIO_N_2_D473: a->state = 1; break;                                      // Stop
    case SF2C_AUDIO_N_3_4E07: case SF2C_AUDIO_N_7_7902: {  // AppendAudioOutBuffer(tag) <- AudioOutBuffer {next, samples, capacity, size, offset}
        IpcBuf *b = in_buf(req, 0);
        uint64_t tag = IN(uint64_t, 0);
        uint64_t size = b && b->size >= SF2C_AUDIO_N_0X28_EAB9 ? ((uint64_t *)(uintptr_t)b->addr)[SF2C_AUDIO_N_3_4E07] : 0;
        if (getenv(SF2C_AUDIO_S_LARECOMP_AUDIO_LOG_866C) && b)
            LOG(SF2C_AUDIO_S_AUDOUT_APPEND_TAG_LLX_BUF_LL_85F2, (unsigned long long)tag,
                (unsigned long long)b->addr, (unsigned long long)b->size, ((unsigned long long *)(uintptr_t)b->addr)[0],
                ((unsigned long long *)(uintptr_t)b->addr)[1], ((unsigned long long *)(uintptr_t)b->addr)[SF2C_AUDIO_N_2_D473],
                ((unsigned long long *)(uintptr_t)b->addr)[SF2C_AUDIO_N_3_4E07], ((unsigned long long *)(uintptr_t)b->addr)[SF2C_AUDIO_N_4_4B22]);
        pthread_mutex_lock(&a->lock);
        // AudioOutBuffer {next, samples, capacity, size, offset}
        uint64_t data = b && b->size >= SF2C_AUDIO_N_0X28_EAB9 ? ((uint64_t *)(uintptr_t)b->addr)[1] + ((uint64_t *)(uintptr_t)b->addr)[SF2C_AUDIO_N_4_4B22] : 0;
        if (a->queued < SF2C_AUDIO_N_64_A68B) {
            a->tags[a->queued] = tag;
            a->sizes[a->queued] = size ? size : SF2C_AUDIO_N_4096_8B92;
            a->datas[a->queued] = size ? data : 0;
            a->queued++;
        }
        pthread_mutex_unlock(&a->lock);
        break;
    }
    case SF2C_AUDIO_N_4_4B22: resp->copy_handles[resp->ncopy++] = a->ev_r; break;  // RegisterBufferEvent
    case SF2C_AUDIO_N_5_EF2D: case SF2C_AUDIO_N_8_2C62: {  // GetReleasedAudioOutBuffers -> tags
        IpcBuf *b = out_buf(req, 0);
        pthread_mutex_lock(&a->lock);
        int n = a->released_n;
        int max = b ? (int)(b->size / SF2C_AUDIO_N_8_2C62) : 0;
        if (n > max) n = max;
        if (b && b->size) memset((void *)(uintptr_t)b->addr, 0, b->size);  // SDK reads entry 0 even when n == 0
        for (int i = 0; i < n; i++) ((uint64_t *)(uintptr_t)b->addr)[i] = a->released[i];
        if (getenv(SF2C_AUDIO_S_LARECOMP_AUDIO_LOG_866C) && n) LOG(SF2C_AUDIO_S_AUDOUT_RELEASED_D_FIRST_LLX__C47F, n, (unsigned long long)a->released[0]);
        memmove(a->released, a->released + n, (a->released_n - n) * SF2C_AUDIO_N_8_2C62);
        a->released_n -= n;
        pthread_mutex_unlock(&a->lock);
        OUT(uint32_t, 0) = (uint32_t)n;
        resp_size(resp, SF2C_AUDIO_N_4_4B22);
        break;
    }
    case SF2C_AUDIO_N_6_E7F6: OUT(uint8_t, 0) = 0; resp_size(resp, 1); break;
    case SF2C_AUDIO_N_9_1958: OUT(uint32_t, 0) = (uint32_t)a->queued; resp_size(resp, SF2C_AUDIO_N_4_4B22); break;
    case SF2C_AUDIO_N_10_4A44: OUT(uint64_t, 0) = a->played; resp_size(resp, SF2C_AUDIO_N_8_2C62); break;
    case SF2C_AUDIO_N_11_4FC8: OUT(uint8_t, 0) = 1; resp_size(resp, 1); break;
    case SF2C_AUDIO_N_12_6B51: break;
    case SF2C_AUDIO_N_13_3FDB: OUT(float, 0) = SF2C_AUDIO_N_1_0F_CA71; resp_size(resp, SF2C_AUDIO_N_4_4B22); break;
    default: LOG(SF2C_AUDIO_S_AUDOUT_IAUDIOOUT_CMD_U_OK_N_BEA4, req->cmd);
    }
}

SERVICE(SF2C_AUDIO_S_AUDOUT_U_ED5F, audout_u) {
    (void)s;
    switch (req->cmd) {
    case 0: case SF2C_AUDIO_N_2_D473: {  // ListAudioOuts
        IpcBuf *b = out_buf(req, 0);
        if (b && b->size >= SF2C_AUDIO_N_0X100_BE6E) strcpy((char *)(uintptr_t)b->addr, SF2C_AUDIO_S_DEVICEOUT_BC5C);
        OUT(uint32_t, 0) = 1;
        resp_size(resp, SF2C_AUDIO_N_4_4B22);
        break;
    }
    case 1: case SF2C_AUDIO_N_3_4E07: {  // OpenAudioOut(rate, channels, aruid, name) -> rate, channels, format, state + IAudioOut
        AudioOut *a = calloc(1, sizeof *a);
        a->rate = IN(uint32_t, 0) ? IN(uint32_t, 0) : SF2C_AUDIO_N_48000_5D1D;
        a->channels = IN(uint16_t, SF2C_AUDIO_N_4_4B22) ? IN(uint16_t, SF2C_AUDIO_N_4_4B22) : SF2C_AUDIO_N_2_D473;
        if (a->channels != 1 && a->channels != SF2C_AUDIO_N_2_D473 && a->channels != SF2C_AUDIO_N_6_E7F6) a->channels = SF2C_AUDIO_N_2_D473;
        a->state = 1;
        pthread_mutex_init(&a->lock, NULL);
        a->event = kernel_create_event_pair(&a->ev_w, &a->ev_r);
        if (getenv(SF2C_AUDIO_S_LARECOMP_AUDOUT_WAV_7222)) a->wav = fopen(getenv(SF2C_AUDIO_S_LARECOMP_AUDOUT_WAV_7222), SF2C_AUDIO_S_WB_3D6A);
        if (!(getenv(SF2C_AUDIO_S_LARECOMP_NO_AUDIO_8230) && strcmp(getenv(SF2C_AUDIO_S_LARECOMP_NO_AUDIO_8230), SF2C_AUDIO_S_0_9808)) && SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            SDL_AudioSpec spec = {SDL_AUDIO_S16, (int)a->channels, (int)a->rate};
            a->stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
            if (a->stream) {
                if (getenv(SF2C_AUDIO_S_LARECOMP_AUDIO_GAIN_1F0B)) SDL_SetAudioStreamGain(a->stream, (float)atof(getenv(SF2C_AUDIO_S_LARECOMP_AUDIO_GAIN_1F0B)));
                SDL_ResumeAudioStreamDevice(a->stream);
            }
        }
        pthread_create(&a->thread, NULL, audout_thread, a);
        audio_track(NULL, a);
        IpcBuf *b = out_buf(req, 0);
        if (b && b->size) strncpy((char *)(uintptr_t)b->addr, SF2C_AUDIO_S_DEVICEOUT_BC5C, b->size);
        OUT(uint32_t, 0) = a->rate;
        OUT(uint32_t, SF2C_AUDIO_N_4_4B22) = a->channels;
        OUT(uint32_t, SF2C_AUDIO_N_8_2C62) = SF2C_AUDIO_N_2_D473;  // PCM16
        OUT(uint32_t, SF2C_AUDIO_N_12_6B51) = 1; // stopped
        resp_size(resp, SF2C_AUDIO_N_16_B17E);
        Service *svc = service_new(SF2C_AUDIO_S_IAUDIOOUT_AA39, audio_out, a);
        svc->on_close = audout_close;
        ipc_return_object(resp, svc);
        LOG(SF2C_AUDIO_S_AUDOUT_OPENED_U_HZ_U_CH_N_5A59, a->rate, a->channels);
        break;
    }
    default: LOG(SF2C_AUDIO_S_AUDOUT_CMD_U_OK_N_DDE1, req->cmd);
    }
}
