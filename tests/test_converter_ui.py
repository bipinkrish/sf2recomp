"""The converter window must run the CLI and preserve useful failure logs."""
import io
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from types import SimpleNamespace
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import conversion_log
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

    def test_progress_tracks_phases_and_each_compiled_file(self):
        log = self.root / "conversion.log"
        lines, updates = [], []
        script = """print('SF2_PROGRESS [1, 2, "Compile game"]')
for done in range(1, 26):
    print(f"Building game: {done}/25 files ready")
print('SF2_PROGRESS [2, 2, "Package game"]')
"""
        self.assertEqual(ui.stream_conversion([sys.executable, "-u", "-c", script], log, lines.append,
                                              on_progress=updates.append), 0)
        self.assertEqual(len(lines), 25)
        self.assertEqual("".join(lines), log.read_text())
        self.assertEqual(updates[0][0], 0)
        self.assertEqual(updates[1][0], 2)
        self.assertEqual(updates[-1], (50, "Step 2/2 · Package game"))

    def test_cancel_stops_child_processes_and_keeps_the_log(self):
        log = self.root / "conversion.log"
        ready = threading.Event()
        result = []
        process = ui.ConversionProcess()
        script = """
import subprocess
import sys
subprocess.run([sys.executable, '-u', '-c', "import time; print('child started'); time.sleep(30)"])
"""
        def work():
            result.append(ui.stream_conversion([sys.executable, "-u", "-c", script], log,
                                               lambda line: ready.set(), process))

        worker = threading.Thread(target=work, daemon=True)
        worker.start()
        try:
            self.assertTrue(ready.wait(10))
        finally:
            process.cancel()
            worker.join(5)
        self.assertFalse(worker.is_alive())
        self.assertTrue(process.cancelled)
        self.assertNotEqual(result, [0])
        self.assertEqual(len(result), 1)
        self.assertIn("child started", log.read_text())

    def test_cli_log_preserves_process_output_and_exit_code(self):
        entry = self.root / "conversion script.py"
        entry.write_text("import sys; print('stdout'); print('stderr', file=sys.stderr); sys.exit(7)")
        stdout, stderr = io.BytesIO(), io.BytesIO()
        with patch.object(conversion_log, "ROOT", self.root), \
                patch.object(sys, "stdout", SimpleNamespace(buffer=stdout)), \
                patch.object(sys, "stderr", io.TextIOWrapper(stderr, encoding="utf-8", write_through=True)) as errors:
            self.assertEqual(conversion_log.run_logged_cli(entry, []), 7)
            errors.detach()
        self.assertEqual(stdout.getvalue().strip(), b"stdout")
        logs = list((self.root / "logs").glob("sf2recomp-conversion-*.log"))
        self.assertEqual(len(logs), 1)
        self.assertIn(b"stderr", stderr.getvalue())
        self.assertIn(str(logs[0]).encode(), stderr.getvalue())
        self.assertEqual(set(logs[0].read_text().splitlines()), {"stdout", "stderr"})

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
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        logs = patch.object(conversion_log, "ROOT", Path(directory.name))
        logs.start()
        self.addCleanup(logs.stop)

    def test_window_runs_worker_and_restores_controls_on_success_and_failure(self):
        root = tk.Tk()
        self.addCleanup(root.destroy)
        window = ui.ConverterWindow(root)
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            for exit_code in (0, 3):
                command = [sys.executable, "-u", "-c", f"import sys; print('first log line'); print('middle\\n' * 1200); print('Building game: 1/1 files ready'); sys.exit({exit_code})"]
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
                self.assertIn("first log line", window.log.get("1.0", "end"))
                self.assertIn("Building game: 1/1", window.log.get("1.0", "end"))
                self.assertNotIn("Building game: 1/1", window.status.get())
                expected = "normal" if exit_code == 0 else "disabled"
                self.assertEqual(str(window.open_button.cget("state")), expected)
                self.assertIn("complete" if exit_code == 0 else "failed", window.status.get())


    def test_cancel_button_stops_work_with_measured_progress(self):
        root = tk.Tk()
        self.addCleanup(root.destroy)
        window = ui.ConverterWindow(root)
        script = """import time
print('SF2_PROGRESS [1, 2, "Compile game"]')
print('Building game: 1/2 files ready')
time.sleep(30)
"""
        command = [sys.executable, "-u", "-c", script]
        with patch.object(ui, "conversion_command", return_value=(command, ROOT)):
            window.start()
        self.assertEqual(str(window.cancel_button.cget("state")), "normal")
        deadline = time.monotonic() + 5
        while float(window.progress.cget("value")) == 0 and time.monotonic() < deadline:
            root.update()
            time.sleep(0.01)
        window.cancel_button.invoke()
        self.assertEqual(float(window.progress.cget("value")), 25)
        deadline = time.monotonic() + 5
        while window.running and time.monotonic() < deadline:
            root.update()
            time.sleep(0.01)
        self.assertFalse(window.running)
        self.assertEqual(window.status.get(), "Conversion cancelled.")
        self.assertEqual(str(window.cancel_button.cget("state")), "disabled")
        self.assertIn("Building game: 1/2", window.log.get("1.0", "end"))


if __name__ == "__main__":
    unittest.main()
