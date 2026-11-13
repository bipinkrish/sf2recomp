"""Recorded commands retain textures across finalization and asynchronous submission."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class TextureLifetimeTests(unittest.TestCase):
    def test_record_finalize_submit_and_retirement(self):
        with tempfile.TemporaryDirectory(prefix="sf2-texture-lifetime-") as folder:
            binary = Path(folder) / "test"
            subprocess.run([
                "clang", "-O1", "-g", "-Wno-format", "-fsanitize=address,undefined",
                "-fno-omit-frame-pointer", "-ffunction-sections", "-fdata-sections",
                "-I" + str(ROOT / "runtime"), str(ROOT / "tests/nvn_texture_lifetime.c"),
                "-Wl,--gc-sections", "-pthread", "-o", str(binary),
            ], check=True)
            for mode in ("0", "1"):
                with self.subTest(render_thread=mode):
                    env = dict(os.environ, SF2RECOMP_RENDER_THREAD=mode,
                               ASAN_OPTIONS=os.environ.get("ASAN_OPTIONS", "detect_leaks=0"))
                    subprocess.run([str(binary)], env=env, check=True, timeout=20)


if __name__ == "__main__":
    unittest.main()
