"""Real SDL contacts -> stable Unity touch frames, including independent releases."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_settings import compile_harness


class TouchControlsTests(unittest.TestCase):
    def test_multitouch_edges_viewport_and_cancellation(self):
        source = r'''
#include <assert.h>
#include <math.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "settings.h"
#include "ui/touch.h"
static SDL_Window *window;
static MenuTouch touches[MENU_TOUCH_MAX];
static void finger(unsigned type, SDL_FingerID id, float x, float y) {
    SDL_Event e = {0}; e.type = type; e.tfinger.windowID = SDL_GetWindowID(window);
    e.tfinger.touchID = 12; e.tfinger.fingerID = id;
    e.tfinger.x = x; e.tfinger.y = y; e.tfinger.pressure = .75f;
    menu_touch_event(&e);
}
static unsigned frame(bool captured) { return menu_touch_frame(1920, 1080, captured, touches); }
int main(void) {
    assert(SDL_Init(SDL_INIT_VIDEO));
    window = SDL_CreateWindow("touch", 1000, 1000, 0); assert(window);
    g_settings.aspect = ASPECT_ORIGINAL;
    // SDL IDs may exceed 32 bits. Touch IDs presented to Unity are stable and nonnegative.
    finger(SDL_EVENT_FINGER_DOWN, 0x100000001ll, .25f, .375f);
    finger(SDL_EVENT_FINGER_DOWN, 0x200000001ll, .75f, .625f);
    assert(frame(false) == 2);
    assert(touches[0].finger == 0 && touches[1].finger == 1);
    assert(touches[0].phase == TOUCH_BEGAN && touches[1].phase == TOUCH_BEGAN);
    assert(fabsf(touches[0].x - 480) < .01 && fabsf(touches[0].y - 780) < .01);
    assert(touches[0].dx == 0 && touches[0].pressure == .75f);
    finger(SDL_EVENT_FINGER_MOTION, 0x100000001ll, .3f, .375f);
    finger(SDL_EVENT_FINGER_UP, 0x200000001ll, .75f, .625f);
    assert(frame(false) == 2 && touches[0].phase == TOUCH_MOVED && touches[1].phase == TOUCH_ENDED);
    assert(fabsf(touches[0].dx - 96) < .01);
    assert(frame(false) == 1 && touches[0].phase == TOUCH_STATIONARY && touches[0].dx == 0);
    // Releasing an attack never releases the held movement finger.
    finger(SDL_EVENT_FINGER_DOWN, 9, .8f, .7f);
    finger(SDL_EVENT_FINGER_UP, 9, .8f, .7f);
    assert(frame(false) == 2 && touches[1].phase == TOUCH_BEGAN);
    assert(frame(false) == 2 && touches[1].phase == TOUCH_ENDED);
    assert(frame(false) == 1 && touches[0].finger == 0);
    assert(frame(true) == 1 && touches[0].phase == TOUCH_CANCELED);
    assert(frame(false) == 0);
    // Began uses the initial position even when SDL batches a whole drag/tap.
    finger(SDL_EVENT_FINGER_DOWN, 10, .25f, .375f);
    finger(SDL_EVENT_FINGER_MOTION, 10, .5f, 0);
    finger(SDL_EVENT_FINGER_UP, 10, .5f, 0);
    assert(frame(false) == 1 && touches[0].phase == TOUCH_BEGAN && fabsf(touches[0].x - 480) < .01f);
    assert(frame(false) == 1 && touches[0].phase == TOUCH_ENDED && touches[0].y > 1080);
    assert(frame(false) == 0); // black bars remain outside the Unity viewport
    // Android may reuse its finger ID for another tap before Unity has consumed
    // the first release. Preserve both taps with distinct in-flight Unity IDs.
    for (int i = 0; i < 2; ++i) {
        finger(SDL_EVENT_FINGER_DOWN, 10, .3f, .3f);
        finger(SDL_EVENT_FINGER_UP, 10, .3f, .3f);
    }
    assert(frame(false) == 2 && touches[0].phase == TOUCH_BEGAN && touches[1].phase == TOUCH_BEGAN);
    assert(touches[0].finger != touches[1].finger);
    assert(frame(false) == 2 && touches[0].phase == TOUCH_ENDED && touches[1].phase == TOUCH_ENDED);
    assert(frame(false) == 0);
    finger(SDL_EVENT_FINGER_DOWN, 11, .3f, .3f); assert(frame(false) == 1);
    SDL_Event lost = {0}; lost.type = SDL_EVENT_WINDOW_FOCUS_LOST; menu_touch_event(&lost);
    assert(frame(false) == 1 && touches[0].phase == TOUCH_CANCELED && frame(false) == 0);
    // Focus/capture before the first guest frame must not leak a fresh press.
    finger(SDL_EVENT_FINGER_DOWN, 12, .3f, .3f); assert(frame(true) == 0);
    // Ignore nonfinite input, unknown releases and excess contacts safely.
    finger(SDL_EVENT_FINGER_DOWN, 13, NAN, .3f); assert(frame(false) == 0);
    finger(SDL_EVENT_FINGER_UP, 99, .3f, .3f); assert(frame(false) == 0);
    for (int i = 0; i < MENU_TOUCH_MAX + 2; ++i) finger(SDL_EVENT_FINGER_DOWN, 100 + i, .1f, .2f);
    assert(frame(false) == MENU_TOUCH_MAX);
    finger(SDL_EVENT_FINGER_CANCELED, 100, .1f, .2f);
    assert(frame(false) == MENU_TOUCH_MAX && touches[0].phase == TOUCH_CANCELED);
    assert(frame(false) == MENU_TOUCH_MAX - 1);
    menu_touch_cancel(); assert(frame(false) == MENU_TOUCH_MAX - 1 && frame(false) == 0);
    // A movie's skip finger stays captured after the overlay disappears.
    SDL_Event tap={0};tap.type=SDL_EVENT_FINGER_DOWN;tap.tfinger.touchID=12;tap.tfinger.fingerID=777;
    assert(menu_touch_movie_event(&tap,true));
    tap.type=SDL_EVENT_FINGER_MOTION;assert(menu_touch_movie_event(&tap,false));
    tap.type=SDL_EVENT_FINGER_UP;assert(menu_touch_movie_event(&tap,false));
    tap.type=SDL_EVENT_FINGER_DOWN;assert(!menu_touch_movie_event(&tap,false));
    assert(menu_touch_movie_event(&tap,true));
    assert(!menu_touch_movie_event(&lost,false));
    tap.type=SDL_EVENT_FINGER_UP;assert(!menu_touch_movie_event(&tap,false));
    SDL_DestroyWindow(window); SDL_Quit();
}
'''
        with tempfile.TemporaryDirectory(prefix="sf2-touch-") as tmp:
            root = Path(tmp)
            (root / "touch.c").write_text(source)
            compile_harness(root / "touch.c", root / "touch")
            subprocess.run([str(root / "touch")], check=True,
                           env={**os.environ, "SDL_VIDEODRIVER": "dummy"})
