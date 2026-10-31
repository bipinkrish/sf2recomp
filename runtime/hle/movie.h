#pragma once

#include <stdbool.h>

// The window loop can present the movie while the guest loads assets; normal game presents use the same path.
bool movie_overlay_active(void);
bool movie_render_tick(void);  // true while frame zero must be shown before audio can start
void movie_presented(void);

void movie_request_skip(void);  // thread-safe; actual teardown runs on the UI thread
void movie_poll(void);          // UI thread
