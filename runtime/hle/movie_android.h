#pragma once
#ifdef __ANDROID__
#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>
typedef struct AndroidMovie AndroidMovie;
typedef struct AndroidMovieTrack AndroidMovieTrack;
AndroidMovie *android_movie_open(const char *path, AndroidMovieTrack **video, AndroidMovieTrack **audio);
ssize_t android_movie_read(AndroidMovieTrack *track, void *data, size_t size);
void android_movie_cancel(AndroidMovie *movie);
bool android_movie_failed(AndroidMovie *movie); // after both readers have joined
void android_movie_close(AndroidMovie *movie); // after both readers have joined
#endif
