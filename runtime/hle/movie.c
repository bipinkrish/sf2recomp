// nn::movie HLE implementation for video playback / cinematics
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../hle.h"
#include "../rt.h"

static uint64_t g_extractor_vtable[16];
static uint64_t g_decoder_vtable[16];
static int g_movie_inited = 0;

static void movie_dtor(Cpu *c) { (void)c; }
static void movie_set_data_source(Cpu *c) {
    const char *path = (const char *)(uintptr_t)c->x[1];
    LOG("[movie] SetDataSource: %s (returning error to skip)\n", path ? path : "(null)");
    c->x[0] = 0x2;
}
static void movie_get_track_count(Cpu *c) {
    LOG("[movie] GetTrackCount -> error\n");
    if (c->x[1]) *(int32_t *)(uintptr_t)c->x[1] = 0;
    c->x[0] = 0x2;
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

HLE_FUNC("_ZN5movie9ExtractorD1Ev", movie_extractor_d1) { (void)c; }
HLE_FUNC("_ZN5movie9ExtractorD2Ev", movie_extractor_d2) { (void)c; }

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
HLE_FUNC("_ZN5movie7DecoderD1Ev", movie_decoder_d1) { (void)c; }

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
