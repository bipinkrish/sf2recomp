// In-process Android decoder. Each reader owns its extractor/codec; cancellation
// is atomic and all codec waits are bounded, so skipping never blocks the UI.
#ifdef __ANDROID__
#include "movie_android.h"
#include "../hle.h"
#include "../nvn/renderer.h"
#include <SDL3/SDL.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaExtractor.h>
#include <media/NdkImageReader.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

struct AndroidMovieTrack {
    struct AndroidMovie *owner;
    AMediaExtractor *extractor;
    AMediaCodec *codec;
    AImageReader *images;
    bool started, input_eos, output_eos, video, failed;
    uint8_t *frame;
    size_t pending, offset;
    unsigned inputs, outputs, samples, frames;
};
struct AndroidMovie {
    atomic_bool stop;
    AndroidMovieTrack video, audio;
};
enum { FRAME_BYTES = RENDERER_MOVIE_WIDTH * RENDERER_MOVIE_HEIGHT * 3 / 2 };

void android_movie_cancel(AndroidMovie *movie) {
    if (movie) atomic_store(&movie->stop, true);
}
bool android_movie_failed(AndroidMovie *movie) { return movie && (movie->video.failed || movie->audio.failed); }
static void close_track(AndroidMovieTrack *t) {
    if (t->codec) {
        if (t->started) AMediaCodec_stop(t->codec);
        AMediaCodec_delete(t->codec);
    }
    if (t->images) AImageReader_delete(t->images);
    if (t->extractor) AMediaExtractor_delete(t->extractor);
    free(t->frame);
}
void android_movie_close(AndroidMovie *movie) {
    if (!movie) return;
    close_track(&movie->video);
    close_track(&movie->audio);
    free(movie);
}
static bool open_track(AndroidMovieTrack *t, int fd, off_t length, bool video) {
    t->video = video;
    t->extractor = AMediaExtractor_new();
    if (!t->extractor || AMediaExtractor_setDataSourceFd(t->extractor, fd, 0, length)!=AMEDIA_OK) return false;
    size_t count = AMediaExtractor_getTrackCount(t->extractor);
    for (size_t i=0; i<count; ++i) {
        AMediaFormat *f = AMediaExtractor_getTrackFormat(t->extractor, i);
        const char *mime = NULL;
        if (!f) continue;
        bool match = AMediaFormat_getString(f, AMEDIAFORMAT_KEY_MIME, &mime) &&
                     !strncmp(mime, video ? "video/" : "audio/", 6);
        if (!match) { AMediaFormat_delete(f); continue; }
        // CPU-readable surface output is inconsistent in vendor hardware AVC
        // codecs. These small 720x400 clips use Android's platform software
        // decoder, whose YUV output works with AImageReader across GPUs.
        if (video && !strcmp(mime,"video/avc"))
            t->codec = AMediaCodec_createCodecByName("c2.android.avc.decoder");
        if (!t->codec) t->codec = AMediaCodec_createDecoderByType(mime);
        if (t->codec) {
            char *name=NULL;
            if (AMediaCodec_getName(t->codec,&name)==AMEDIA_OK) {
                LOG("[movie] Android %s codec: %s\n",video?"video":"audio",name);
                AMediaCodec_releaseName(t->codec,name);
            }
        }
        ANativeWindow *window = NULL;
        bool ok = t->codec && AMediaExtractor_selectTrack(t->extractor, i)==AMEDIA_OK;
        if (video) {
            // Our playback clock presents every decoded frame. ImageReader
            // surfaces otherwise permit MediaCodec to discard queued frames.
            AMediaFormat_setInt32(f,"allow-frame-drop",0);
            int32_t w=0,h=0;
            ok = ok && AMediaFormat_getInt32(f, AMEDIAFORMAT_KEY_WIDTH, &w) &&
                 AMediaFormat_getInt32(f, AMEDIAFORMAT_KEY_HEIGHT, &h) &&
                 w==RENDERER_MOVIE_WIDTH && h==RENDERER_MOVIE_HEIGHT;
            // AImage exposes actual plane/pixel strides, including vendor NV12
            // and planar buffers. Never guess the layout of codec byte buffers.
            ok = ok && AImageReader_new(w,h,AIMAGE_FORMAT_YUV_420_888,2,&t->images)==AMEDIA_OK &&
                 AImageReader_getWindow(t->images,&window)==AMEDIA_OK;
            t->frame = malloc(FRAME_BYTES);
            ok = ok && t->frame;
        } else {
            // All three supported game movies are 48 kHz stereo AAC. Request
            // signed PCM16 and validate the actual output format before use.
            AMediaFormat_setInt32(f,"pcm-encoding",2);
        }
        ok = ok && AMediaCodec_configure(t->codec,f,window,NULL,0)==AMEDIA_OK;
        AMediaFormat_delete(f);
        t->started = ok && AMediaCodec_start(t->codec)==AMEDIA_OK;
        return t->started;
    }
    // A silent movie is valid; its audio reader immediately reaches EOF.
    t->output_eos = !video;
    return !video;
}
AndroidMovie *android_movie_open(const char *path, AndroidMovieTrack **video, AndroidMovieTrack **audio) {
    int fd = open(path,O_RDONLY|O_CLOEXEC);
    struct stat st;
    if (fd<0) return NULL;
    // Independent extractors also need independent open-file descriptions.
    // A duplicated descriptor can share seek state between their reader threads.
    int audio_fd=open(path,O_RDONLY|O_CLOEXEC);
    AndroidMovie *m = calloc(1,sizeof *m);
    bool ok = m && audio_fd>=0 && !fstat(fd,&st) && st.st_size>0;
    if (m) { m->video.owner=m; m->audio.owner=m; }
    ok = ok && open_track(&m->video,fd,st.st_size,true) && open_track(&m->audio,audio_fd,st.st_size,false);
    close(fd);
    if(audio_fd>=0)close(audio_fd);
    if (!ok) { android_movie_close(m); LOG("[movie] Android codec setup failed\n"); return NULL; }
    *video=&m->video; *audio=&m->audio;
    LOG("[movie] Android MediaCodec decoder ready\n");
    return m;
}
static bool copy_image(AndroidMovieTrack *t, AImage *image) {
    AImageCropRect crop;
    int32_t format=0;
    if (AImage_getFormat(image,&format)!=AMEDIA_OK || format!=AIMAGE_FORMAT_YUV_420_888 ||
        AImage_getCropRect(image,&crop)!=AMEDIA_OK || crop.left<0 || crop.top<0 ||
        crop.right-crop.left!=RENDERER_MOVIE_WIDTH || crop.bottom-crop.top!=RENDERER_MOVIE_HEIGHT) return false;
    for (int plane=0; plane<3; ++plane) {
        uint8_t *src=NULL;
        int length=0, row=0, pixel=0;
        int shift=plane?1:0, w=RENDERER_MOVIE_WIDTH>>shift,h=RENDERER_MOVIE_HEIGHT>>shift;
        if (AImage_getPlaneData(image,plane,&src,&length)!=AMEDIA_OK ||
            AImage_getPlaneRowStride(image,plane,&row)!=AMEDIA_OK ||
            AImage_getPlanePixelStride(image,plane,&pixel)!=AMEDIA_OK || row<=0 || pixel<=0) return false;
        size_t start=(size_t)(crop.top>>shift)*row+(size_t)(crop.left>>shift)*pixel;
        size_t end=start+(size_t)(h-1)*row+(size_t)(w-1)*pixel;
        if (length<=0 || end>=(size_t)length) return false;
        uint8_t *dst=t->frame+(plane ? RENDERER_MOVIE_WIDTH*RENDERER_MOVIE_HEIGHT+plane-1 : 0);
        for (int y=0;y<h;++y)
            for (int x=0;x<w;++x)
                dst[(size_t)y*RENDERER_MOVIE_WIDTH+x*(plane?2:1)]=src[start+(size_t)y*row+(size_t)x*pixel];
    }
    return true;
}
static bool feed_input(AndroidMovieTrack *t) {
    if (t->input_eos) return true;
    ssize_t index=AMediaCodec_dequeueInputBuffer(t->codec,0);
    if (index==AMEDIACODEC_INFO_TRY_AGAIN_LATER) return true;
    if (index<0) return false;
    size_t capacity=0;
    uint8_t *data=AMediaCodec_getInputBuffer(t->codec,index,&capacity);
    ssize_t expected=AMediaExtractor_getSampleSize(t->extractor);
    if (!data || (expected>0 && (uint64_t)expected>capacity)) return false;
    ssize_t size=AMediaExtractor_readSampleData(t->extractor,data,capacity);
    t->input_eos=size<0;
    int64_t pts=t->input_eos?0:AMediaExtractor_getSampleTime(t->extractor);
    if (AMediaCodec_queueInputBuffer(t->codec,index,0,t->input_eos?0:size,pts,
        t->input_eos?AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM:0)!=AMEDIA_OK) return false;
    if (!t->input_eos) { ++t->samples;AMediaExtractor_advance(t->extractor); }
    ++t->inputs;
    return true;
}
ssize_t android_movie_read(AndroidMovieTrack *t, void *data, size_t size) {
    if (!t || !size) return 0;
    uint64_t deadline=SDL_GetTicksNS()+10000000000ull;
    while (!atomic_load(&t->owner->stop)) {
        if (t->pending) {
            size_t n=size<t->pending?size:t->pending;
            memcpy(data,t->frame+t->offset,n);t->offset+=n;t->pending-=n;
            return n;
        }
        if (t->output_eos || t->failed) return 0;
        if (SDL_GetTicksNS()>deadline || !feed_input(t)) goto fail;
        AMediaCodecBufferInfo info={0};
        ssize_t index=AMediaCodec_dequeueOutputBuffer(t->codec,&info,10000);
        if (index==AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
            AMediaFormat *format=AMediaCodec_getOutputFormat(t->codec);
            if (format) LOG("[movie] Android %s output: %s\n",t->video?"video":"audio",AMediaFormat_toString(format));
            int32_t rate=0,channels=0,encoding=2;
            bool valid=format!=NULL;
            if (valid && !t->video) {
                valid=AMediaFormat_getInt32(format,AMEDIAFORMAT_KEY_SAMPLE_RATE,&rate) && rate==48000 &&
                      AMediaFormat_getInt32(format,AMEDIAFORMAT_KEY_CHANNEL_COUNT,&channels) && channels==2;
                AMediaFormat_getInt32(format,"pcm-encoding",&encoding);
                valid=valid && encoding==2;
            }
            if (format) AMediaFormat_delete(format);
            if (!valid) goto fail;
            continue;
        }
        if (index==AMEDIACODEC_INFO_TRY_AGAIN_LATER || index==AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) continue;
        if (index<0) goto fail;
        ++t->outputs;
        t->output_eos=(info.flags&AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM)!=0;
        if(t->output_eos)LOG("[movie] Android %s EOF: %u samples, %u outputs, %u frames\n",t->video?"video":"audio",t->samples,t->outputs,t->frames);
        bool valid=true;
        if (t->video) {
            // Surface buffers can report size zero even when EOS is attached
            // to the final frame. The supported AVC clips have one frame per
            // sample; an empty terminal buffer has no outstanding sample.
            bool frame=info.size>0 || !t->output_eos || t->frames<t->samples;
            valid=AMediaCodec_releaseOutputBuffer(t->codec,index,frame)==AMEDIA_OK;
            if (valid && frame) {
                AImage *image=NULL;
                media_status_t status;
                uint64_t until=SDL_GetTicksNS()+2000000000ull;
                do {
                    status=AImageReader_acquireNextImage(t->images,&image);
                    if (status!=AMEDIA_IMGREADER_NO_BUFFER_AVAILABLE || atomic_load(&t->owner->stop)) break;
                    SDL_Delay(1);
                } while (SDL_GetTicksNS()<until);
                valid=status==AMEDIA_OK && image && copy_image(t,image);
                if (image) AImage_delete(image);
                if (valid) { ++t->frames;t->offset=0; t->pending=FRAME_BYTES; }
            }
        } else {
            size_t capacity=0;
            uint8_t *pcm=AMediaCodec_getOutputBuffer(t->codec,index,&capacity);
            valid=info.offset>=0 && info.size>=0 && (size_t)info.offset<=capacity &&
                  (size_t)info.size<=capacity-(size_t)info.offset && (!info.size || pcm);
            if (valid && info.size) {
                uint8_t *buffer=realloc(t->frame,info.size);
                valid=buffer!=NULL;
                if (valid) { t->frame=buffer;memcpy(buffer,pcm+info.offset,info.size);t->offset=0;t->pending=info.size; }
            }
            valid=AMediaCodec_releaseOutputBuffer(t->codec,index,false)==AMEDIA_OK && valid;
        }
        if (!valid) goto fail;
    }
    return 0;
fail:
    LOG("[movie] Android %s decode failed or timed out (%u input, %u output buffers)\n",t->video?"video":"audio",t->inputs,t->outputs);
    t->failed=true;
    // Stop both readers on errors so a failed video cannot leave muted audio
    // running indefinitely while the guest waits for playback to finish.
    android_movie_cancel(t->owner);
    return 0;
}
#endif
