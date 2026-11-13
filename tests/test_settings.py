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
                    str(ROOT / "runtime/replay.c"), str(ROOT / "runtime/lan.c"),
                    str(ROOT / "runtime/hle/bindings.c"),
                    str(ROOT / "runtime/ui/developer.c"),
                    str(ROOT / "runtime/ui/mouse.c"), str(ROOT / "runtime/ui/touch.c"),
                    str(ROOT / "runtime/ui/lan_menu.c"), str(ROOT / "runtime/ui/settings_input.c"),
                    str(source), "-Wl,--gc-sections", *libs,
                    "-lm", "-ldl", "-pthread", "-o", str(output)], check=True)


class SettingsTests(unittest.TestCase):
    def test_extensible_config_and_environment_precedence(self):
        harness = r'''
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "settings.h"
int main(int argc, char **argv) {
    assert(argc == 3);
    unsetenv("SF2RECOMP_LANG"); unsetenv("SF2RECOMP_VOLUME"); unsetenv("SF2RECOMP_REVERB");
    assert(settings_load_file(argv[1]));
    assert(!strcmp(settings_language(), "de"));
    assert(g_settings.volume == 0.25f && !g_settings.reverb && g_settings.fullscreen);
    assert(settings_load_file(argv[2]));
    assert(!strcmp(settings_language(), "de"));
    assert(g_settings.volume == 0.25f && !g_settings.reverb && g_settings.fullscreen);
    setenv("SF2_CONFIG", argv[1], 1);
    setenv("SF2RECOMP_LANG", "en_GB.UTF-8", 1);
    setenv("SF2RECOMP_VOLUME", "0.75", 1);
    settings_load();
    assert(!strcmp(settings_language(), "en-GB"));
    assert(g_settings.volume == 0.75f && !g_settings.reverb && g_settings.fullscreen);
    setenv("SF2RECOMP_REVERB", "true", 1); settings_init();
    assert(g_settings.reverb);
    setenv("SF2RECOMP_REVERB", "false", 1); settings_init();
    assert(!g_settings.reverb);
    setenv("SF2RECOMP_REVERB", "only", 1);
    const char *invalid_volumes[] = {"nan", "inf", "-0.5", "2", "garbage"};
    for (int i = 0; i < 5; ++i) {
        setenv("SF2RECOMP_VOLUME", invalid_volumes[i], 1); settings_init();
        assert(g_settings.volume == 0.75f && !g_settings.reverb);
    }
    setenv("SF2RECOMP_LANG", "AuTo", 1); setenv("LC_ALL", "fr_FR.UTF-8", 1);
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

    def test_menu_draft_round_trips_into_active_ini(self):
        harness = r'''
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "settings_config.h"
#include <SDL3/SDL.h>

static unsigned field(SettingsGroup group, const char *name) {
    for (unsigned i = 0; i < settings_config_count(); ++i)
        if (settings_config_group(i) == group && !strcmp(settings_config_label(i), name))
            return i;
    abort();
}

int main(int argc, char **argv) {
    assert(argc == 4);
    setenv("SF2_CONFIG", argv[1], 1);
    setenv("SF2RECOMP_VOLUME", "0.8", 1);
    settings_load();
    settings_resolve_defaults(false);
    SettingsConfig draft;
    SettingsSaveResult result;
    settings_config_begin(&draft);
    unsigned volume = field(SETTINGS_GROUP_AUDIO, "Master volume");
    unsigned reverb = field(SETTINGS_GROUP_AUDIO, "Reverb");
    unsigned resolution = field(SETTINGS_GROUP_VIDEO, "Internal resolution");
    unsigned upscaler = field(SETTINGS_GROUP_VIDEO, "Upscaler");
    unsigned antialias = field(SETTINGS_GROUP_VIDEO, "Anti-aliasing");
    unsigned fullscreen = field(SETTINGS_GROUP_VIDEO, "Fullscreen");
    unsigned fps = field(SETTINGS_GROUP_VIDEO, "Target FPS");
    unsigned save = field(SETTINGS_GROUP_STORAGE, "Save directory");
    unsigned key = field(SETTINGS_GROUP_KEYBOARD, "A");
    unsigned pad = field(SETTINGS_GROUP_GAMEPAD, "B");
    assert(draft.settings.volume == 0.25f && g_settings.volume == 0.8f);
    assert(!strcmp(settings_config_override(volume), "SF2RECOMP_VOLUME"));
    assert(!settings_config_requires_restart(volume) && !settings_config_requires_restart(fullscreen));
    assert(settings_config_requires_restart(fps) && settings_config_requires_restart(key));
    assert(!settings_config_set(&draft, volume, "nan"));
    assert(!settings_config_set(&draft, fps, "361"));
    assert(!settings_config_set(&draft, key, "unknown key"));
    assert(settings_config_set(&draft, volume, "0.35"));
    assert(settings_config_set(&draft, resolution, "0.5x"));
    assert(settings_config_set(&draft, upscaler, "bilinear"));
    assert(settings_config_set(&draft, antialias, "smaa"));
    assert(settings_upscaler() == UPSCALE_FSR1 && settings_antialias() == AA_OFF);
    assert(settings_config_set(&draft, fps, "144"));
    assert(settings_config_set(&draft, save, "my saves/it's #1; good"));
    assert(settings_config_set(&draft, key, ";"));
    assert(settings_config_set(&draft, pad, "rtrigger"));
    settings_config_step(&draft, fullscreen, 1);
    settings_config_step(&draft, reverb, 1);
    assert(settings_reverb() && settings_resolution() == RES_MATCH);
    assert(settings_config_save(&draft, &result));
    assert(settings_volume() == 0.8f && !settings_reverb() && settings_resolution() == RES_Q50);
    assert(settings_upscaler() == UPSCALE_BILINEAR && settings_antialias() == AA_SMAA);
    assert(result.applied == 4 && result.overridden == 1 && result.restart_pending == 4);
    assert(result.request_fullscreen && !g_settings.fullscreen);
    assert(g_settings.target_fps == 60 && g_bind_key[BIND_A] == SDL_SCANCODE_K);
    g_settings.fullscreen = true; // the window thread's successful result
    settings_config_begin(&draft);
    assert(draft.settings.volume == 0.35f && draft.settings.res_mode == RES_Q50 && !draft.settings.res_auto);
    assert(settings_config_set(&draft, fps, "120"));
    assert(settings_config_set(&draft, upscaler, "auto"));
    assert(settings_config_save(&draft, &result));
    assert(settings_upscaler() == UPSCALE_FSR1 && result.applied == 1);
    assert(settings_config_save(&draft, &result));
    assert(result.applied == 0 && result.overridden == 0 && result.restart_pending == 4);
    assert(!result.request_fullscreen);
    unsetenv("SF2RECOMP_VOLUME");
    assert(settings_load_file(argv[1]));
    assert(g_settings.volume == 0.35f && g_settings.fullscreen && g_settings.target_fps == 120);
    assert(!strcmp(g_settings.save_directory, "my saves/it's #1; good"));
    assert(g_bind_key[BIND_A] == SDL_SCANCODE_SEMICOLON && g_bind_pad[BIND_B] == PADB_RTRIGGER);

    setenv("SF2_CONFIG", argv[2], 1);
    settings_load();
    settings_config_begin(&draft);
    assert(settings_config_set(&draft, fps, "75"));
    assert(settings_config_set(&draft, volume, "0.4"));
    assert(!settings_config_save(&draft, &result)); // parent directory is missing
    assert(settings_volume() == 0.35f && result.applied == 0);
    SettingsConfig current;
    settings_config_begin(&current);
    assert(current.settings.target_fps == 120);
    assert(mkdir(argv[3], 0700) == 0);
    assert(settings_config_save(&draft, &result));
    assert(settings_volume() == 0.4f && g_settings.target_fps == 120);
    assert(result.applied == 1 && result.restart_pending == 1);
    assert(settings_load_file(argv[2]) && g_settings.target_fps == 75);
}
'''
        with tempfile.TemporaryDirectory(prefix="sf2-config-save-") as directory:
            path = pathlib.Path(directory)
            config = path / "custom settings.ini"
            config.write_bytes(
                b"# Keep this comment\r\n[Audio]\r\nvolume  = 0.5  # old value\r\n"
                b"[audio]\r\nvolume = 0.25 ; effective value\r\nreverb = yes\r\n"
                b"[video]\r\ninternal_resolution=auto\r\n"
                b"[controls.keyboard]\r\na = K\r\nb = J\r\n"
                b"[future]\r\nfullscreen = quoted; unchanged"
            )
            (path / "test.c").write_text(harness)
            compile_harness(path / "test.c", path / "test")
            new_directory = path / "new"
            new_config = new_directory / "new.ini"
            subprocess.run([str(path / "test"), str(config), str(new_config), str(new_directory)], check=True)
            self.assertEqual(new_config.read_text().strip(), "[audio]\nvolume = 0.4\n\n[video]\ntarget_fps = 75")
            saved = config.read_bytes()
            self.assertIn(b"# Keep this comment\r\n[Audio]\r\nvolume  = 0.35  # old value\r\n", saved)
            self.assertIn(b"volume = 0.35 ; effective value\r\nreverb = false\r\n", saved)
            self.assertIn(b'internal_resolution=0.50x\r\n', saved)
            self.assertIn(b'a = ";"\r\nb = J\r\n', saved)
            self.assertIn(b"[future]\r\nfullscreen = quoted; unchanged\n", saved)
            self.assertEqual(saved.count(b"target_fps = 120"), 1)
            self.assertIn(b"b = rtrigger", saved)
            self.assertEqual(list(path.glob("*.tmp.*")), [])

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
    unsetenv("SF2RECOMP_LANG"); unsetenv("SF2RECOMP_VOLUME"); unsetenv("SF2RECOMP_REVERB");
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
    unsetenv("SF2RECOMP_SAVE_DIR");
    assert(!strcmp(g_settings.save_directory, "save"));
    assert(settings_load_file(argv[1]));
    assert(!strcmp(g_settings.save_directory, "my saves/slot #one; ok"));
    assert(settings_load_file(argv[2]));
    assert(!strcmp(g_settings.save_directory, "my saves/slot #one; ok"));
    assert(settings_load_file(argv[3]));
    assert(!strcmp(g_settings.save_directory, "/tmp/absolute saves"));
    setenv("SF2RECOMP_SAVE_DIR", "override saves", 1); settings_init();
    assert(!strcmp(g_settings.save_directory, "override saves"));
    setenv("SF2RECOMP_SAVE_DIR", "", 1); settings_init();
    assert(!strcmp(g_settings.save_directory, "override saves"));
    char huge[SETTINGS_SAVE_DIRECTORY_CAP + 1];
    memset(huge, 'x', sizeof huge - 1); huge[sizeof huge - 1] = 0;
    setenv("SF2RECOMP_SAVE_DIR", huge, 1); settings_init();
    assert(!strcmp(g_settings.save_directory, "override saves"));
    setenv("SF2RECOMP_SAVE_DIR", "bad\tpath", 1); settings_init();
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
            self.assertIn("invalid SF2RECOMP_SAVE_DIR", result.stderr)

    def test_locale_selection(self):
        harness = r'''
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "settings.h"
int main(void) {
    unsetenv("SF2RECOMP_LANG"); unsetenv("LC_ALL"); unsetenv("LC_MESSAGES");
    setenv("LANG", "en_US.UTF-8", 1);
    assert(!strcmp(settings_language(), "en-US"));
    assert(!strcmp(settings_language_name(), "English (US)"));
    setenv("LC_MESSAGES", "de_DE.UTF-8", 1);
    assert(!strcmp(settings_language(), "de"));
    setenv("LC_ALL", "fr_FR.UTF-8", 1);
    assert(!strcmp(settings_language(), "fr"));
    setenv("SF2RECOMP_LANG", "ja", 1); settings_init();
    assert(!strcmp(settings_language(), "ja"));
    setenv("SF2RECOMP_LANG", "ko-Hangul", 1); settings_init();
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
