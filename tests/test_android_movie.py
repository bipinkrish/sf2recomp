"""Cross-link the real MediaCodec backend; optionally run it on an ADB device."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_android import ROOT, SDK, TOOLCHAIN, SDL


@unittest.skipUnless(TOOLCHAIN.is_dir() and SDL.is_dir(), "prepared NDK and SDL required")
class AndroidMovieTests(unittest.TestCase):
    def test_native_decoder_links_and_decodes_on_selected_device(self):
        device = os.environ.get("ANDROID_MOVIE_DEVICE")
        adb = str(SDK / "platform-tools/adb")
        with tempfile.TemporaryDirectory(prefix="sf2-android-movie-") as temporary:
            root = Path(temporary)
            for abi, compiler in (("arm64-v8a", "aarch64-linux-android33-clang"),
                                  ("x86_64", "x86_64-linux-android33-clang")):
                command = [str(TOOLCHAIN / compiler), "-O2", "-Wall", "-Werror", "-Wno-format",
                           "-I" + str(ROOT / "runtime"), "-I" + str(SDL)]
                if abi == "x86_64": command.append("-mcx16")
                command += [str(ROOT / "tests/android_movie.c"), str(ROOT / "runtime/hle/movie_android.c"),
                            "-lmediandk", "-landroid", "-latomic", "-lm", "-Wl,-z,max-page-size=16384",
                            "-o", str(root / abi)]
                subprocess.run(command, check=True, capture_output=True, text=True)
            if not device:
                return  # Set ANDROID_MOVIE_DEVICE=emulator-5580 to execute the integration fixture.
            ffmpeg = shutil.which("ffmpeg")
            self.assertIsNotNone(ffmpeg, "FFmpeg required to make the synthetic test fixture")
            fixture = root / "fixture.mp4"
            subprocess.run([ffmpeg, "-v", "error", "-f", "lavfi", "-i", "color=c=red:s=720x400:r=25",
                            "-f", "lavfi", "-i", "sine=frequency=440:sample_rate=48000", "-t", "1",
                            "-c:v", "libx264", "-pix_fmt", "yuv420p", "-c:a", "aac", "-ac", "2",
                            str(fixture)], check=True)
            prefix = [adb, "-s", device]
            abi = subprocess.check_output([*prefix, "shell", "getprop", "ro.product.cpu.abi"], text=True).strip()
            self.assertIn(abi, ("arm64-v8a", "x86_64"))
            remote = "/data/local/tmp/sf2-movie-test"
            subprocess.run([*prefix, "push", str(root / abi), remote], check=True)
            subprocess.run([*prefix, "push", str(fixture), remote + ".mp4"], check=True)
            try:
                subprocess.run([*prefix, "shell", "chmod", "700", remote], check=True)
                subprocess.run([*prefix, "shell", remote, remote + ".mp4"], check=True, timeout=30)
            finally:
                subprocess.run([*prefix, "shell", "rm", "-f", remote, remote + ".mp4"], check=True)
