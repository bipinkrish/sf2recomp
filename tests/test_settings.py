"""Runtime INI settings, control bindings and canonical language identifiers."""
import os
import pathlib
import shlex
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


def sdl_flags():
    sdl_dir = pathlib.Path(os.environ.get("SDL_DIR", ROOT / "deps")).resolve()
    lib_dir = sdl_dir / "lib"
    if (sdl_dir / "include/SDL3/SDL.h").is_file() and any(lib_dir.glob("libSDL3.*")):
        return ["-I", str(sdl_dir / "include")], [
            "-L", str(lib_dir), "-lSDL3", "-Xlinker", "-rpath", "-Xlinker", str(lib_dir)]
    try:
        subprocess.run(["pkg-config", "--exists", "sdl3 >= 3.4.0"], check=True)
        cflags = shlex.split(subprocess.check_output(["pkg-config", "--cflags", "sdl3"], text=True))
        libs = shlex.split(subprocess.check_output(["pkg-config", "--libs", "sdl3"], text=True))
        lib_dir = subprocess.check_output(["pkg-config", "--variable=libdir", "sdl3"], text=True).strip()
    except (FileNotFoundError, subprocess.CalledProcessError) as error:
        raise RuntimeError("Tests require SDL3 >= 3.4.0: install its development package or set SDL_DIR to a complete prefix") from error
    if lib_dir:
        libs += ["-Xlinker", "-rpath", "-Xlinker", lib_dir]
    return cflags, libs


def compile_harness(source, output):
    # Exercise the real binding parser and SDL's scancode names, not a second
    # implementation of the configuration format. Unused UI helpers are stripped.
    cflags, libs = sdl_flags()
    subprocess.run(["clang", "-O2", "-Wno-format", "-ffunction-sections", "-fdata-sections",
                    "-I", str(ROOT / "runtime"), *cflags,
                    str(ROOT / "runtime/settings.c"), str(ROOT / "runtime/network.c"),
                    str(ROOT / "runtime/hle/bindings.c"),
                    str(ROOT / "runtime/ui/developer.c"),
                    str(source), "-Wl,--gc-sections", *libs,
                    "-lm", "-ldl", "-pthread", "-o", str(output)], check=True)


class SettingsTests(unittest.TestCase):
    def test_experimental_keyboard_prompts_require_opt_in(self):
        harness = r'''
#include <assert.h>
#include "settings.h"
int main(int argc, char **argv) {
    assert(argc == 4 && !g_settings.experimental_keyboard_prompts);
    assert(settings_load_file(argv[1]) && g_settings.experimental_keyboard_prompts);
    assert(settings_load_file(argv[2]) && !g_settings.experimental_keyboard_prompts);
    assert(settings_load_file(argv[3]) && !g_settings.experimental_keyboard_prompts);
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix="sf2-experimental-config-") as directory:
            root = pathlib.Path(directory)
            (root / "test.c").write_text(harness)
            files = []
            for name, value in (("on", "true"), ("off", "false"), ("invalid", "maybe")):
                path = root / f"{name}.ini"
                path.write_text(f"[experimental]\nkeyboard_prompts = {value}\n")
                files.append(str(path))
            compile_harness(root / "test.c", root / "test")
            subprocess.run([str(root / "test"), *files], check=True)

    def test_extensible_config_and_environment_precedence(self):
        harness = r'''
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "settings.h"
int main(int argc, char **argv) {
    assert(argc == 3);
    unsetenv("LARECOMP_LANG"); unsetenv("LARECOMP_VOLUME"); unsetenv("LARECOMP_REVERB");
    assert(settings_load_file(argv[1]));
    assert(!strcmp(settings_language(), "de"));
    assert(g_settings.volume == 0.25f && !g_settings.reverb && g_settings.fullscreen);
    assert(settings_load_file(argv[2]));
    assert(!strcmp(settings_language(), "de"));
    assert(g_settings.volume == 0.25f && !g_settings.reverb && g_settings.fullscreen);
    setenv("SF2_CONFIG", argv[1], 1);
    setenv("LARECOMP_LANG", "en_GB.UTF-8", 1);
    setenv("LARECOMP_VOLUME", "0.75", 1);
    settings_load();
    assert(!strcmp(settings_language(), "en-GB"));
    assert(g_settings.volume == 0.75f && !g_settings.reverb && g_settings.fullscreen);
    setenv("LARECOMP_REVERB", "true", 1); settings_init();
    assert(g_settings.reverb);
    setenv("LARECOMP_REVERB", "false", 1); settings_init();
    assert(!g_settings.reverb);
    setenv("LARECOMP_REVERB", "only", 1);
    const char *invalid_volumes[] = {"nan", "inf", "-0.5", "2", "garbage"};
    for (int i = 0; i < 5; ++i) {
        setenv("LARECOMP_VOLUME", invalid_volumes[i], 1); settings_init();
        assert(g_settings.volume == 0.75f && !g_settings.reverb);
    }
    setenv("LARECOMP_LANG", "AuTo", 1); setenv("LC_ALL", "fr_FR.UTF-8", 1);
    settings_init(); assert(!strcmp(settings_language(), "fr"));
    assert(!settings_load_file("/does/not/exist/sf2.ini"));
}
'''
        with tempfile.TemporaryDirectory(prefix="sf2-config-") as directory:
            path = pathlib.Path(directory)
            valid = path / "custom settings.ini"
            valid.write_bytes(b'''# comments and whitespace\r\n[general]\r\nlanguage = "de_DE.UTF-8" ; note\r\n[audio]\r\nvolume = 0.25\r\nreverb = off\r\n[video]\r\nfullscreen = YES\r\n[future]\r\nlanguage = ja\r\npath = "some#path;name"\r\n''')
            invalid = path / "invalid.ini"
            invalid.write_text("[general]\nlanguage=nonsense\n[audio]\nvolume=nan\nreverb=unknown\n[video]\nfullscreen=maybe\n[audio\nvolume=0.9\n")
            (path / "test.c").write_text(harness)
            compile_harness(path / "test.c", path / "test")
            subprocess.run([str(path / "test"), str(valid), str(invalid)], check=True)

    def test_runtime_control_remapping(self):
        harness = r'''
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "settings.h"
#include "input_bind.h"
// Exercise the actual event -> HID state consumer with the configuration parser.
#include "hle/input.c"
bool menu_input_captured(void) { return false; }
static bool movie_visible, skip_requested;
void movie_request_skip(void) { skip_requested = true; }
bool movie_overlay_active(void) { return movie_visible; }
uint64_t renderer_present_count(void) { return 1; }
volatile int g_inject_plus;
int main(int argc, char **argv) {
    assert(argc == 3);
    unsetenv("LARECOMP_LANG"); unsetenv("LARECOMP_VOLUME"); unsetenv("LARECOMP_REVERB");
    // Runtime INIs are user-editable; check built-in defaults independently,
    // then reset the loaded map so custom local bindings cannot affect this test.
    assert(bind_is_default(BIND_DEV_KEY) && bind_is_default(BIND_DEV_PAD));
    assert(settings_load_file(argv[1]));
    bind_reset(BIND_DEV_KEY); bind_reset(BIND_DEV_PAD);
    assert(bind_is_default(BIND_DEV_KEY) && bind_is_default(BIND_DEV_PAD));
    assert(settings_load_file(argv[2]));
    assert(g_bind_key[BIND_A] == SDL_SCANCODE_SPACE);
    assert(g_bind_key[BIND_B] == SDL_SCANCODE_UNKNOWN);
    assert(g_bind_key[BIND_LS_UP] == SDL_SCANCODE_UP);
    assert(g_bind_pad[BIND_A] == SDL_GAMEPAD_BUTTON_NORTH);
    assert(g_bind_pad[BIND_B] == -1);
    assert(g_bind_pad[BIND_ZL] == PADB_RTRIGGER);
    assert(g_bind_pad[BIND_X] == SDL_GAMEPAD_BUTTON_NORTH); // intentional shared mapping
    assert(bind_config_set(BIND_DEV_KEY, "a", "not a real key") == BIND_CONFIG_INVALID);
    assert(bind_config_set(BIND_DEV_PAD, "a", "not a button") == BIND_CONFIG_INVALID);
    assert(bind_config_set(BIND_DEV_KEY, "future_action", "Space") == BIND_CONFIG_UNKNOWN);
    assert(g_bind_key[BIND_A] == SDL_SCANCODE_SPACE && g_bind_pad[BIND_A] == SDL_GAMEPAD_BUTTON_NORTH);

    SDL_Event event = {0};
    event.type = SDL_EVENT_KEY_DOWN; event.key.scancode = SDL_SCANCODE_K;
    input_handle_event(&event);
    assert(!(input_buttons_now() & (1ull << BIND_A))); // old key no longer confirms
    event.key.scancode = SDL_SCANCODE_SPACE;
    input_handle_event(&event);
    assert(input_buttons_now() & (1ull << BIND_A));
    event.type = SDL_EVENT_KEY_UP;
    input_handle_event(&event);
    assert(!(input_buttons_now() & (1ull << BIND_A)));
    event.type = SDL_EVENT_KEY_DOWN; event.key.scancode = SDL_SCANCODE_UP;
    input_handle_event(&event);
    assert(key_ly == 32767);
    event.key.scancode = SDL_SCANCODE_J;
    input_handle_event(&event);
    assert(!(input_buttons_now() & (1ull << BIND_B))); // explicitly unbound

    host_test = true;
    test_pad = 1u << SDL_GAMEPAD_BUTTON_EAST;
    recompute_pad(0);
    assert(!(pads[0].buttons & (1ull << BIND_A)));
    test_pad = (1u << SDL_GAMEPAD_BUTTON_NORTH) | (1u << PADB_RTRIGGER);
    recompute_pad(0);
    assert(pads[0].buttons & (1ull << BIND_A));
    assert(pads[0].buttons & (1ull << BIND_X));
    assert(pads[0].buttons & (1ull << BIND_ZL));
    assert(!(pads[0].buttons & (1ull << BIND_B)));

    uint64_t state[5] = {0};
    uint32_t player = 0;
    Cpu cpu = {0};
    cpu.x[0] = (uintptr_t)state; cpu.x[1] = (uintptr_t)&player;
    movie_visible = true;
    fill_state(&cpu);
    assert(state[1] == 0 && state[2] == 0 && state[3] == 0);
    assert(((uint32_t *)state)[8] == 1);
    note_press(0, 1ull << BIND_A, 0);
    assert(skip_requested);
    movie_visible = false;
    fill_state(&cpu);
    assert(!(state[1] & (1ull << BIND_A))); // skip press cannot select Story
    note_press(0, 0, 1ull << BIND_A);
    fill_state(&cpu);
    assert(state[1] & (1ull << BIND_A));

    FILE *saved = tmpfile(); assert(saved);
    bind_config_save(saved);
    rewind(saved);
    char data[2048] = {0}; assert(fread(data, 1, sizeof data - 1, saved));
    assert(strstr(data, "[controls.gamepad]"));
    assert(strstr(data, "b = none"));
    assert(strstr(data, "a = \"Space\""));
    fclose(saved);
}
'''
        with tempfile.TemporaryDirectory(prefix="sf2-bindings-") as directory:
            path = pathlib.Path(directory)
            config = path / "controls.ini"
            config.write_text("""[CONTROLS.KEYBOARD]
A = sPaCe
b = NoNe
LS_UP = Up
future_action = anything
[controls.gamepad]
a = NORTH
b = NONE
zl = rtrigger
[future.controls]
a = K
[controls.keyboard]
a = not a valid SDL key
[controls.gamepad]
a = not_a_button
""")
            (path / "test.c").write_text(harness)
            compile_harness(path / "test.c", path / "test")
            result = subprocess.run([str(path / "test"), str(ROOT / "sf2recomp.ini"), str(config)],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("keeping previous binding", result.stderr)
            self.assertNotIn("future_action", result.stderr)

    def test_ini_changes_apply_without_recompiling(self):
        harness = r'''
#include <assert.h>
#include <SDL3/SDL.h>
#include "settings.h"
#include "input_bind.h"
int main(int argc, char **argv) {
    assert(argc == 3);
    assert(settings_load_file(argv[1]));
    assert(g_bind_key[BIND_A] == SDL_GetScancodeFromName(argv[2]));
}
'''
        with tempfile.TemporaryDirectory(prefix="sf2-binding-restart-") as directory:
            path = pathlib.Path(directory)
            config = path / "sf2recomp.ini"
            (path / "test.c").write_text(harness)
            compile_harness(path / "test.c", path / "test")
            for key in ("Space", "Return"):
                config.write_text("[controls.keyboard]\na = " + key + "\n")
                subprocess.run([str(path / "test"), str(config), key], check=True)

    def test_save_directory_runtime_setting_and_environment(self):
        harness = r'''
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "settings.h"
int main(int argc, char **argv) {
    assert(argc == 4);
    unsetenv("LARECOMP_SAVE_DIR");
    assert(!strcmp(g_settings.save_directory, "save"));
    assert(settings_load_file(argv[1]));
    assert(!strcmp(g_settings.save_directory, "my saves/slot #one; ok"));
    assert(settings_load_file(argv[2]));
    assert(!strcmp(g_settings.save_directory, "my saves/slot #one; ok"));
    assert(settings_load_file(argv[3]));
    assert(!strcmp(g_settings.save_directory, "/tmp/absolute saves"));
    setenv("LARECOMP_SAVE_DIR", "override saves", 1); settings_init();
    assert(!strcmp(g_settings.save_directory, "override saves"));
    setenv("LARECOMP_SAVE_DIR", "", 1); settings_init();
    assert(!strcmp(g_settings.save_directory, "override saves"));
    char huge[SETTINGS_SAVE_DIRECTORY_CAP + 1];
    memset(huge, 'x', sizeof huge - 1); huge[sizeof huge - 1] = 0;
    setenv("LARECOMP_SAVE_DIR", huge, 1); settings_init();
    assert(!strcmp(g_settings.save_directory, "override saves"));
    setenv("LARECOMP_SAVE_DIR", "bad\tpath", 1); settings_init();
    assert(!strcmp(g_settings.save_directory, "override saves"));
}
'''
        with tempfile.TemporaryDirectory(prefix="sf2-storage-config-") as directory:
            path = pathlib.Path(directory)
            valid = path / "valid.ini"
            valid.write_text('[storage]\nsave_directory = "my saves/slot #one; ok"\n')
            invalid = path / "invalid.ini"
            invalid.write_text('[storage]\nsave_directory = ""\nsave_directory = "   "\n'
                               'save_directory = "bad\tpath"\nsave_directory = ' + 'x' * 768 + '\n'
                               '[future.storage]\nsave_directory = ignored\n')
            absolute = path / "absolute.ini"
            absolute.write_text('[STORAGE]\nSAVE_DIRECTORY = "/tmp/absolute saves"\n')
            (path / "test.c").write_text(harness)
            compile_harness(path / "test.c", path / "test")
            result = subprocess.run([str(path / "test"), str(valid), str(invalid), str(absolute)],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("invalid [storage] save_directory", result.stderr)
            self.assertIn("invalid LARECOMP_SAVE_DIR", result.stderr)

    def test_locale_selection(self):
        harness = r'''
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "settings.h"
int main(void) {
    unsetenv("LARECOMP_LANG"); unsetenv("LC_ALL"); unsetenv("LC_MESSAGES");
    setenv("LANG", "en_US.UTF-8", 1);
    assert(!strcmp(settings_language(), "en-US"));
    assert(!strcmp(settings_language_name(), "English (US)"));
    setenv("LC_MESSAGES", "de_DE.UTF-8", 1);
    assert(!strcmp(settings_language(), "de"));
    setenv("LC_ALL", "fr_FR.UTF-8", 1);
    assert(!strcmp(settings_language(), "fr"));
    setenv("LARECOMP_LANG", "ja", 1); settings_init();
    assert(!strcmp(settings_language(), "ja"));
    setenv("LARECOMP_LANG", "ko-Hangul", 1); settings_init();
    assert(!strcmp(settings_language(), "ko"));
    assert(!strcmp(settings_language_name(), "Korean"));
    assert(settings_language_index("en_GB.UTF-8") == settings_language_index("en-GB"));
    g_settings.language = LANG_AUTO;
    unsetenv("LC_ALL"); unsetenv("LC_MESSAGES"); setenv("LANG", "C.UTF-8", 1);
    assert(!strcmp(settings_language(), "en-US"));
    for (int i = 0; i < N_LANGUAGES; ++i) assert(strlen(g_language_codes[i]) < 8);
}
'''
        with tempfile.TemporaryDirectory(prefix="sf2-language-") as directory:
            path = pathlib.Path(directory)
            (path / "test.c").write_text(harness)
            compile_harness(path / "test.c", path / "test")
            subprocess.run([str(path / "test")], check=True)


if __name__ == "__main__":
    unittest.main()
