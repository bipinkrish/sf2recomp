// Run with a one-second, 720x400, 25 fps red H.264 / stereo 48 kHz AAC fixture.
#include "hle/movie_android.h"
#include <SDL3/SDL.h>
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
// Only timing is needed from SDL by this decoder-only integration check.
Uint64 SDL_GetTicksNS(void) {
    struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);
    return (uint64_t)t.tv_sec*1000000000ull+t.tv_nsec;
}
void SDL_Delay(Uint32 ms) {
    struct timespec t={ms/1000,(ms%1000)*1000000};nanosleep(&t,NULL);
}
typedef struct { AndroidMovieTrack *track; size_t bytes; bool video; } Reader;
static void *drain(void *argument) {
    Reader *r=argument;
    unsigned char data[720*400*3/2];
    ssize_t n;
    while ((n=android_movie_read(r->track,data,sizeof data))>0) {
        if (r->video) {
            assert(n==sizeof data);
            // Verify Y/U/V ordering, including vendor interleaved chroma.
            assert(data[0]>=78 && data[0]<=84);
            assert(data[720*400]>=86 && data[720*400]<=94);
            assert(data[720*400+1]>=236 && data[720*400+1]<=244);
        }
        r->bytes+=n;
    }
    assert(n==0);
    return NULL;
}
int main(int argc,char **argv) {
    assert(argc==2);
    AndroidMovieTrack *video=NULL,*audio=NULL;
    assert(!android_movie_open("/does/not/exist",&video,&audio));
    AndroidMovie *movie=android_movie_open(argv[1],&video,&audio);assert(movie);
    Reader readers[]={{video,0,true},{audio,0,false}};
    pthread_t threads[2];
    for (int i=0;i<2;++i)assert(!pthread_create(&threads[i],NULL,drain,&readers[i]));
    for (int i=0;i<2;++i)pthread_join(threads[i],NULL);
    fprintf(stderr,"Decoded %zu video frames and %zu PCM bytes\n",readers[0].bytes/(720*400*3/2),readers[1].bytes);
    assert(readers[0].bytes==720*400*3/2*25);
    assert(readers[1].bytes>=48000*4*.9 && readers[1].bytes<=48000*4*1.2);
    android_movie_close(movie);
    movie=android_movie_open(argv[1],&video,&audio);assert(movie);
    unsigned char frame[720*400*3/2];
    assert(android_movie_read(video,frame,sizeof frame)==sizeof frame);
    uint64_t before=SDL_GetTicksNS();
    android_movie_cancel(movie);
    assert(!android_movie_read(video,frame,sizeof frame));
    assert(!android_movie_read(audio,frame,sizeof frame));
    android_movie_close(movie);
    assert(SDL_GetTicksNS()-before<2000000000ull);
    puts("Android video frames, NV12 planes, AAC PCM, EOF and cancellation PASS");
}
