"""Sparring unlock override is configurable and touches only the runtime roster."""
import pathlib
import subprocess
import tempfile
import unittest

from test_settings import compile_harness


class SparringUnlockTests(unittest.TestCase):
    def test_default_unlocks_roster_and_false_preserves_original_checks(self):
        source = r'''
#include <assert.h>
#include <string.h>
#include "hle/hooks.c"

int main(int argc, char **argv) {
    assert(argc == 4 && g_settings.unlock_all_sparring_characters);
    unsigned char settings[0x20] = {0}, roster[0x20 + 3 * 8] = {0};
    unsigned char first[0x60], last[0x60], expected[0x60];
    memset(first, 0x5a, sizeof first);
    memset(last, 0x5a, sizeof last);
    memset(expected, 0x5a, sizeof expected);
    expected[0x58] = 1;
    ST64((uintptr_t)settings + 0x18, (uintptr_t)roster);
    ST64((uintptr_t)roster + 0x18, 3);
    ST64((uintptr_t)roster + 0x20, (uintptr_t)first);
    ST64((uintptr_t)roster + 0x30, (uintptr_t)last); // middle entry intentionally null
    Cpu cpu = {0};
    cpu.x[0] = (uintptr_t)settings;
    cpu.x[19] = 0x12345678;
    cpu.sp = 0xabcdef;
    Cpu before = cpu;
    unsigned char saved_settings[sizeof settings], saved_roster[sizeof roster];
    memcpy(saved_settings, settings, sizeof settings);
    memcpy(saved_roster, roster, sizeof roster);

    // The built-in default works without any profile or saved unlock list.
    assert(unlock_sparring_characters(&cpu) == 1);
    assert(!memcmp(first, expected, sizeof first));
    assert(!memcmp(last, expected, sizeof last));
    assert(!memcmp(&cpu, &before, sizeof cpu));
    assert(!memcmp(settings, saved_settings, sizeof settings));
    assert(!memcmp(roster, saved_roster, sizeof roster));

    assert(settings_load_file(argv[1]) && !g_settings.unlock_all_sparring_characters);
    first[0x58] = 0;
    assert(unlock_sparring_characters(&cpu) == 0); // run original guest instructions
    assert(first[0x58] == 0 && last[0x58] == 1);
    assert(!memcmp(&cpu, &before, sizeof cpu));
    assert(settings_load_file(argv[2]) && !g_settings.unlock_all_sparring_characters);
    assert(settings_load_file(argv[3]) && g_settings.unlock_all_sparring_characters);
    assert(unlock_sparring_characters(&cpu) == 1 && first[0x58] == 1);

    ST64((uintptr_t)roster + 0x18, 0);
    assert(unlock_sparring_characters(&cpu) == 1); // empty array
    ST64((uintptr_t)settings + 0x18, 0);
    assert(unlock_sparring_characters(&cpu) == 0); // defer invalid objects to original
    cpu.x[0] = 0;
    assert(unlock_sparring_characters(&cpu) == 0);
}
'''
        with tempfile.TemporaryDirectory(prefix="sf2-sparring-config-") as directory:
            root = pathlib.Path(directory)
            (root / "test.c").write_text(source)
            configs = []
            for name, value in (("off", "false"), ("invalid", "maybe"), ("on", "true")):
                path = root / f"{name}.ini"
                path.write_text(f"[gameplay]\nunlock_all_sparring_characters = {value}\n")
                configs.append(str(path))
            compile_harness(root / "test.c", root / "test")
            subprocess.run([str(root / "test"), *configs], check=True)
