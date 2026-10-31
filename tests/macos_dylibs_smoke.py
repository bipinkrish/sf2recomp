"""Run on a Mac: move real Mach-O binaries away from their original prefix."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from macos_dylibs import relocate


def main():
    with tempfile.TemporaryDirectory(prefix="sf2 native dylibs ") as folder:
        root = Path(folder)
        source, output = root / "original", root / "output"
        source.mkdir()
        (output / "lib").mkdir(parents=True)
        (source / "message.c").write_text("int message(void) { return 23; }\n")
        (source / "main.c").write_text("extern int message(void); int main(void) { return message(); }\n")
        library = source / "libmessage.1.dylib"
        subprocess.run(["clang", "-dynamiclib", str(source / "message.c"), "-o", str(library),
                        "-Wl,-headerpad_max_install_names", "-install_name", str(library)], check=True)
        subprocess.run(["clang", str(source / "main.c"), str(library), "-o", str(source / "app"),
                        "-Wl,-headerpad_max_install_names"], check=True)
        shutil.copy2(source / "app", output / "app")
        shutil.copy2(library, output / "lib" / library.name)
        relocate([output / "app"], [output / "lib" / library.name])
        shutil.rmtree(source)
        moved = root / "moved Unicode-é"
        output.rename(moved)
        result = subprocess.run([str(moved / "app")], cwd="/", check=False)
        assert result.returncode == 23, result
        print("Relocated and signed Mach-O binaries run after the original prefix is removed.")


if __name__ == "__main__":
    main()
