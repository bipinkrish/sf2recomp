"""The converter window must run the CLI and preserve useful failure logs."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import converter_ui as ui
import release


class ConversionCommandTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="sf2 picker ")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.nsp = self.root / "game with spaces.nsp"
        self.keys = self.root / "prod.keys"
        self.nsp.touch()
        self.keys.touch()
        self.output = self.root / "native game"

    def test_release_command_preserves_paths_and_does_not_reopen_the_ui(self):
        with patch.object(ui, "manifest", return_value={"schema": 1}):
            command, output = ui.conversion_command("NSP file", str(self.nsp), str(self.keys),
                                                     str(self.output), ui.TARGETS[0])
        self.assertEqual(output, self.output)
        self.assertIn("-I", command)
        self.assertIn(str(ROOT / "tools/release_entry.py"), command)
        self.assertEqual(command[command.index("--nsp") + 1], str(self.nsp))
        self.assertEqual(command[command.index("--keys") + 1], str(self.keys))
        self.assertNotIn("--gui", command)

    def test_android_requires_explicit_license_acceptance_on_first_use(self):
        with patch.object(ui, "manifest", return_value={"schema": 1}), patch("setup_android.ready", return_value=False):
            with self.assertRaisesRegex(ValueError, "Read and accept"):
                ui.conversion_command("NSP file", str(self.nsp), str(self.keys), str(self.output), ui.TARGETS[1])
            command, _ = ui.conversion_command("NSP file", str(self.nsp), str(self.keys), str(self.output), ui.TARGETS[1], True)
        self.assertIn("--accept-android-licenses", command)
        self.assertEqual(command[command.index("--abi") + 1], "arm64-v8a")

    def test_extracted_folder_needs_no_keys_and_uses_source_converter(self):
        game = self.root / "extracted"
        game.mkdir()
        with patch.object(ui, "manifest", return_value=None):
            command, _ = ui.conversion_command("Extracted game folder", str(game), "", str(self.output), ui.TARGETS[0])
        self.assertIn(str(ROOT / "tools/convert.py"), command)
        self.assertIn("--game", command)
        self.assertNotIn("--keys", command)
        with self.assertRaisesRegex(ValueError, "separate"):
            ui.conversion_command("Extracted game folder", str(game), "", str(game / "nested"), ui.TARGETS[0])

    def test_real_subprocess_failure_is_streamed_and_saved(self):
        lines = []
        log = self.root / "conversion.log"
        code = ui.stream_conversion([sys.executable, "-u", "-c",
                                     "import sys; print('progress'); print('failure', file=sys.stderr); sys.exit(7)"],
                                    log, lines.append)
        self.assertEqual(code, 7)
        self.assertEqual("".join(lines), log.read_text())
        self.assertIn("failure", log.read_text())

    def test_no_arguments_open_window_but_explicit_input_keeps_cli(self):
        with patch.object(release, "manifest", return_value={"target": ui.host_target()}), \
                patch.object(ui, "main", return_value=0) as open_ui:
            self.assertEqual(release.main([]), 0)
            open_ui.assert_called_once()
            with self.assertRaises(SystemExit):
                release.main(["--nsp", str(self.nsp)])  # Missing keys remains a CLI error.
            open_ui.assert_called_once()


try:
    import tkinter as tk
except ImportError:
    tk = None


@unittest.skipUnless(tk and os.environ.get("DISPLAY"), "Requires Tk and a desktop display")
class WindowTests(unittest.TestCase):
    def test_window_runs_worker_and_restores_controls_on_success_and_failure(self):
        root = tk.Tk()
        self.addCleanup(root.destroy)
        window = ui.ConverterWindow(root)
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            for exit_code in (0, 3):
                command = [sys.executable, "-u", "-c", f"import sys; print('Building game: 1/1 files ready'); sys.exit({exit_code})"]
                with patch.object(ui, "conversion_command", return_value=(command, output)):
                    window.start()
                self.assertTrue(window.running)
                self.assertEqual(str(window.start_button.cget("state")), "disabled")
                deadline = time.monotonic() + 10
                while window.running and time.monotonic() < deadline:
                    root.update()
                    time.sleep(0.01)
                self.assertFalse(window.running)
                self.assertEqual(str(window.start_button.cget("state")), "normal")
                self.assertIn("Building game: 1/1", window.log.get("1.0", "end"))
                expected = "normal" if exit_code == 0 else "disabled"
                self.assertEqual(str(window.open_button.cget("state")), expected)
                self.assertIn("complete" if exit_code == 0 else "failed", window.status.get())


if __name__ == "__main__":
    unittest.main()
