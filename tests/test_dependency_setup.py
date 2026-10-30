"""Pinned source setup must reject bad inputs without damaging installed tools."""
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import tarfile
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("setup_extraction", ROOT / "tools/setup_extraction.py")
setup = importlib.util.module_from_spec(spec)
spec.loader.exec_module(setup)


class DependencySetupTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="sf2-source-test-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.archives = self.root / "archives"
        self.archives.mkdir()
        self.output = self.root / "third_party"
        self.pins = []
        for name in ("hactool", "nstool", "dependency"):
            archive = self.archives / f"{name}-revision.tar.gz"
            with tarfile.open(archive, "w:gz") as tar:
                for file, content in (("makefile", b"source makefile"), ("LICENSE", b"original notice")):
                    member = tarfile.TarInfo(f"upstream/{file}")
                    member.size = len(content)
                    tar.addfile(member, io.BytesIO(content))
            self.pins.append({
                "name": name, "repository": "unused/offline", "revision": "revision",
                "sha256": hashlib.sha256(archive.read_bytes()).hexdigest(),
                "destination": name if name != "dependency" else "nstool/deps/dependency",
            })
        self.lock = self.root / "lock.json"
        self.lock.write_text(json.dumps(self.pins))
        self.patcher = patch.object(setup, "LOCK", self.lock)
        self.patcher.start()
        self.addCleanup(self.patcher.stop)

    def test_offline_install_preserves_notices_and_reuses_built_tools_without_archives(self):
        setup.prepare(self.output, self.archives, offline=True)
        self.assertEqual((self.output / "nstool/deps/dependency/LICENSE").read_bytes(), b"original notice")
        built = self.output / "nstool/bin/nstool"
        built.parent.mkdir()
        built.write_bytes(b"built tool")
        for archive in self.archives.iterdir():
            archive.unlink()
        setup.prepare(self.output, self.archives, offline=True)
        self.assertEqual(built.read_bytes(), b"built tool")
        setup.prepare(self.output, self.archives, check=True)
        (self.output / "hactool/makefile").write_text("modified")
        with self.assertRaisesRegex(ValueError, "missing or changed"):
            setup.prepare(self.output, self.archives, check=True)

    def test_checksum_rejection_preserves_existing_sources(self):
        self.output.mkdir()
        sentinel = self.output / "keep.txt"
        sentinel.write_text("existing source")
        (self.archives / "nstool-revision.tar.gz").write_bytes(b"corrupt archive")
        with self.assertRaisesRegex(ValueError, "checksum mismatch"):
            setup.prepare(self.output, self.archives, offline=True)
        self.assertEqual(list(self.output.iterdir()), [sentinel])
        self.assertEqual(sentinel.read_text(), "existing source")

    def test_archive_cannot_escape_staging_directory(self):
        archive = self.archives / "hactool-revision.tar.gz"
        with tarfile.open(archive, "w:gz") as tar:
            member = tarfile.TarInfo("../../escaped")
            member.size = 3
            tar.addfile(member, io.BytesIO(b"bad"))
        self.pins[0]["sha256"] = hashlib.sha256(archive.read_bytes()).hexdigest()
        self.lock.write_text(json.dumps(self.pins))
        with self.assertRaises(tarfile.FilterError):
            setup.prepare(self.output, self.archives, offline=True)
        self.assertFalse((self.root / "escaped").exists())
        self.assertFalse(self.output.exists())


if __name__ == "__main__":
    unittest.main()
