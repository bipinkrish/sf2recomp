"""Portable output must read its own INI without legacy migration side effects."""
import os
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class PortablePathsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.workspace = tempfile.TemporaryDirectory(prefix="sf2-path-harness-")
        cls.binary = pathlib.Path(cls.workspace.name) / "test"
        source = cls.binary.with_suffix(".c")
        source.write_text(r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "paths.h"
bool plat_exe_path(char *out, size_t size) {
    return snprintf(out, size, "%s", getenv("SF2_TEST_EXE")) < (int)size;
}
int main(void) {
    char early[4096], cwd[4096], content[256] = {0};
    if (!rt_data_root_early(early, sizeof early)) return 2;
    rt_paths_init();
    if (!getcwd(cwd, sizeof cwd)) return 3;
    FILE *ini = fopen(getenv("SF2_CONFIG"), "r");
    if (!ini) return 4;
    if (!fgets(content, sizeof content, ini)) return 5;
    fclose(ini);
    printf("early=%s\nroot=%s\ncwd=%s\nconfig=%s\ncontent=%s", early,
           rt_data_root(), cwd, getenv("SF2_CONFIG"), content);
}
''')
        subprocess.run(["clang", "-O2", "-I", str(ROOT / "runtime"),
                        str(ROOT / "runtime/paths.c"), str(source), "-o", str(cls.binary)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.workspace.cleanup()

    def run_paths(self, converted, launch, **overrides):
        env = os.environ.copy()
        for key in ("SF2_CONFIG", "SF2RECOMP_DATA_ROOT", "SF2RECOMP_DATA_HOME"):
            env.pop(key, None)
        env.update(SF2_TEST_EXE=str(converted / "sf2"), **overrides)
        result = subprocess.run([str(self.binary)], cwd=launch, env=env,
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return dict(line.split("=", 1) for line in result.stdout.splitlines())

    def make_folders(self, workspace):
        converted, launch = workspace / "converted game", workspace / "launch directory"
        converted.mkdir()
        launch.mkdir()
        (converted / "sf2recomp.ini").write_text("converted configuration\n")
        (launch / "sf2recomp.ini").write_text("wrong working-directory configuration\n")
        return converted, launch

    def test_ini_is_beside_executable_from_another_directory(self):
        with tempfile.TemporaryDirectory(prefix="sf2-portable-") as folder:
            converted, launch = self.make_folders(pathlib.Path(folder))
            got = self.run_paths(converted, launch)
            self.assertEqual(got["early"], str(converted))
            self.assertEqual(got["root"], str(converted))
            self.assertEqual(got["cwd"], str(converted))
            self.assertEqual(got["config"], str(converted / "sf2recomp.ini"))
            self.assertEqual(got["content"], "converted configuration")

    def test_relative_config_resolves_before_data_directory_change(self):
        with tempfile.TemporaryDirectory(prefix="sf2-relative-config-") as folder:
            workspace = pathlib.Path(folder)
            converted, launch = self.make_folders(workspace)
            custom = launch / "my config.ini"
            custom.write_text("explicit custom configuration\n")
            got = self.run_paths(converted, launch, SF2_CONFIG="my config.ini",
                                 SF2RECOMP_DATA_ROOT="state/session")
            self.assertEqual(got["config"], str(custom))
            self.assertEqual(got["root"], str(launch / "state/session"))
            self.assertEqual(got["cwd"], got["root"])
            self.assertEqual(got["content"], "explicit custom configuration")

    def test_data_override_keeps_default_ini_beside_executable(self):
        with tempfile.TemporaryDirectory(prefix="sf2-data-override-") as folder:
            workspace = pathlib.Path(folder)
            converted, launch = self.make_folders(workspace)
            for override in ("SF2RECOMP_DATA_ROOT", "SF2RECOMP_DATA_HOME"):
                with self.subTest(override=override):
                    state = workspace / override / "save state"
                    got = self.run_paths(converted, launch, **{override: str(state)})
                    self.assertEqual(got["early"], str(state))
                    self.assertEqual(got["root"], str(state))
                    self.assertEqual(got["config"], str(converted / "sf2recomp.ini"))
                    self.assertEqual(got["content"], "converted configuration")

    def test_legacy_migration_files_cannot_redirect_or_delete_data(self):
        with tempfile.TemporaryDirectory(prefix="sf2-no-migration-") as folder:
            workspace = pathlib.Path(folder)
            converted, launch = self.make_folders(workspace)
            old = workspace / "old data"
            old.mkdir()
            save = old / "keep-my-save"
            save.write_text("irreplaceable user save\n")
            markers = ["data_root.txt", ".lasrecomp_move_to", ".lasrecomp_move_from", ".lasrecomp_incomplete"]
            for marker in markers:
                (converted / marker).write_text(str(old) + "\n")
            got = self.run_paths(converted, launch)
            self.assertEqual(got["root"], str(converted))
            self.assertEqual(save.read_text(), "irreplaceable user save\n")
            for marker in markers:
                self.assertEqual((converted / marker).read_text(), str(old) + "\n")


if __name__ == "__main__":
    unittest.main()
