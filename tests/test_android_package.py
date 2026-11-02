"""Build-time game packaging is cached and tied to the translated modules."""
from pathlib import Path
import json
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import build_android


class AndroidPackageTests(unittest.TestCase):
    def test_rebuild_preserves_an_apk_being_installed(self):
        with tempfile.TemporaryDirectory(prefix="sf2-apk-publish-") as temporary:
            root=Path(temporary)
            source, output=root/"new.apk", root/"output/game.apk"
            source.write_bytes(b"old package")
            build_android.publish_apk(source,output)
            with output.open("rb") as installer:
                self.assertEqual(installer.read(4),b"old ")
                source.write_bytes(b"new and larger package")
                build_android.publish_apk(source,output)
                self.assertEqual(installer.read(),b"package")
            self.assertEqual(output.read_bytes(),b"new and larger package")
            self.assertFalse(output.with_suffix(".apk.tmp").exists())

    def test_local_game_package_cache_and_build_identity(self):
        with tempfile.TemporaryDirectory(prefix="sf2-android-package-") as temporary:
            root = Path(temporary)
            assets = root / "build/android-assets"
            assets.mkdir(parents=True)
            game = root / "extracted"
            (game / "exefs").mkdir(parents=True)
            (game / "romfs/Data").mkdir(parents=True)
            content = game / "romfs/Data/globalgamemanagers"
            content.write_bytes(b"local game data")
            identities = {"main": "ab" * 32, "sdk": "cd" * 32}
            for name, identity in identities.items():
                header = bytearray(0x100)
                header[:4] = b"NSO0"
                header[0x40:0x60] = bytes.fromhex(identity)
                (game / "exefs" / name).write_bytes(header)
            (game / "exefs/main.uncompressed").write_bytes(b"not needed")
            (game / "icon.png").write_bytes(b"original game icon")
            (game / "game-info.json").write_text(json.dumps({"title": "Shadow Fight 2", "publisher": "Nekki Limited", "display_version": "1.1.0"}))
            with patch.object(build_android, "ROOT", root):
                build_android.package_game(game, identities)
                archive = assets / "game.zip"
                with zipfile.ZipFile(archive) as zip:
                    self.assertEqual(set(zip.namelist()), {"exefs/main", "exefs/sdk", "romfs/Data/globalgamemanagers", "icon.png", "game-info.json"})
                    self.assertEqual(zip.read("romfs/Data/globalgamemanagers"), content.read_bytes())
                self.assertEqual((root / "build/android-res/drawable-nodpi/game_icon.png").read_bytes(), (game / "icon.png").read_bytes())
                metadata = json.loads((root / "build/android/game-metadata.json").read_text())
                self.assertEqual(metadata["title"], "Shadow Fight 2")
                self.assertEqual(metadata["display_version"], "1.1.0")
                self.assertEqual(metadata["source_build_id"], identities["main"])
                timestamp = archive.stat().st_mtime_ns
                identity = (assets / "game-package-id.txt").read_text()
                build_android.package_game(game, identities)
                self.assertEqual(archive.stat().st_mtime_ns, timestamp)
                content.write_bytes(b"updated local game data")
                build_android.package_game(game, identities)
                self.assertNotEqual((assets / "game-package-id.txt").read_text(), identity)
                original = archive.read_bytes()
                (game / "exefs/main").write_bytes(b"truncated")
                with self.assertRaises(ValueError):
                    build_android.package_game(game, identities)
                self.assertEqual(archive.read_bytes(), original)
