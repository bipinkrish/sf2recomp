"""Relocate the explicitly bundled SDL/FFmpeg Mach-O files, then ad-hoc sign.

Vulkan, MoltenVK and shaderc stay system/SDK dependencies, as documented.
"""
import os
from pathlib import Path
import subprocess


def linked_libraries(path):
    output = subprocess.check_output(["otool", "-L", str(path)], text=True)
    return [line.strip().split(" (compatibility version", 1)[0]
            for line in output.splitlines()[1:] if line.strip()]


def relocate(executables, libraries):
    # Include symlink names when matching load commands, but edit/sign each
    # underlying binary only once. The output's aliases are all relative.
    by_name = {}
    for path in libraries:
        path = Path(path)
        actual = path.resolve(strict=True)
        if path.name in by_name and by_name[path.name] != actual:
            raise ValueError(f"Conflicting bundled dylib name: {path.name}")
        by_name[path.name] = actual
    files = list(dict.fromkeys([*(Path(p).resolve(strict=True) for p in executables), *by_name.values()]))
    for path in files:
        changes = []
        for dependency in linked_libraries(path):
            destination = by_name.get(Path(dependency).name)
            if destination is None or destination == path:
                continue
            relative = os.path.relpath(destination, path.parent)
            replacement = "@loader_path/" + Path(relative).as_posix()
            if dependency != replacement:
                changes += ["-change", dependency, replacement]
        if path.suffix == ".dylib":
            changes += ["-id", "@rpath/" + path.name]
        if changes:
            subprocess.run(["install_name_tool", *changes, str(path)], check=True)
    # Editing Mach-O load commands invalidates the compiler's existing signature.
    # Sign only after every edit; no identity/keychain or Developer ID is needed.
    for path in files:
        subprocess.run(["codesign", "--force", "--sign", "-", str(path)], check=True)
