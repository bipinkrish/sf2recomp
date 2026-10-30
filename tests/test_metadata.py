"""Official display metadata is read from the selected control NCA, never guessed."""
from pathlib import Path
import json
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import extract
import game_metadata


def control_nacp(title="Synthetic Game", version="9.8.7", language=0):
    data = bytearray(0x4000)
    offset = language * 0x300
    encoded = title.encode()
    data[offset:offset + len(encoded)] = encoded
    data[offset + 0x200:offset + 0x200 + len(b"Test Publisher")] = b"Test Publisher"
    encoded = version.encode()
    data[0x3060:0x3060 + len(encoded)] = encoded
    return data


class MetadataTests(unittest.TestCase):
    def test_official_display_version_and_language_fallback(self):
        info = game_metadata.parse_nacp(control_nacp())
        self.assertEqual(info["display_version"], "9.8.7")
        self.assertEqual(info["title"], "Synthetic Game")
        self.assertEqual(info["publisher"], "Test Publisher")
        self.assertEqual(info["application_id"], "org.sf2recomp.ShadowFight2")
        self.assertEqual(game_metadata.parse_nacp(control_nacp(language=2))["language"], "Japanese")
        with self.assertRaisesRegex(ValueError, "truncated"):
            game_metadata.parse_nacp(bytes(12))
        with self.assertRaisesRegex(ValueError, "official title or display version"):
            game_metadata.parse_nacp(control_nacp(version=""))

    def test_original_control_and_icon_pixels_are_preserved(self):
        from PIL import Image
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            control, output = root / "control", root / "updated game"
            control.mkdir()
            raw = control_nacp()
            (control / "control.nacp").write_bytes(raw)
            jpeg = control / "icon_AmericanEnglish.dat"
            Image.new("RGB", (16, 16), (120, 80, 40)).save(jpeg, format="JPEG")
            with Image.open(jpeg) as original:
                expected = original.convert("RGB").tobytes()
            info = game_metadata.write_game_metadata(control, output, "Patch")
            self.assertEqual((output / "control.nacp").read_bytes(), raw)
            with Image.open(output / "icon.png") as icon:
                self.assertEqual(icon.format, "PNG")
                self.assertEqual(icon.size, (16, 16))
                self.assertEqual(icon.convert("RGB").tobytes(), expected)
            self.assertEqual(info["control_source"], "Patch")
            self.assertEqual(json.loads((output / "game-info.json").read_text())["display_version"], "9.8.7")

    def test_update_control_preferred_and_missing_update_uses_base(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "metadata fixture"
            def select(update, missing_update=False):
                data, entries = b"", {}
                for kind, title in extract.TITLE_IDS.items():
                    identifier = ("c" if kind == "Application" else "d") * 32
                    content = f"<Content><Type>Control</Type><Id>{identifier}</Id></Content>"
                    if kind == "Patch" and not update:
                        content = ""
                    xml = f"<ContentMeta><Type>{kind}</Type><Id>0x{title:x}</Id>{content}</ContentMeta>".encode()
                    entries[kind + ".cnmt.xml"] = extract.Entry(len(data), len(xml))
                    data += xml
                    if content and not (kind == "Patch" and missing_update):
                        entries[identifier + ".nca"] = extract.Entry(0, 0)
                path.write_bytes(data)
                return extract.select_control(path, entries)
            self.assertEqual(select(True), ("Patch", "d" * 32 + ".nca"))
            self.assertEqual(select(False), ("Application", "c" * 32 + ".nca"))
            with self.assertRaisesRegex(ValueError, "control NCA is missing"):
                select(True, missing_update=True)


if __name__ == "__main__":
    unittest.main()
