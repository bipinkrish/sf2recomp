"""Relocate the explicitly bundled SDL/FFmpeg Mach-O files, then ad-hoc sign.

Vulkan, MoltenVK and shaderc stay system/SDK dependencies, as documented.
"""
from _sf2_tool_constants import MACOS_DYLIBS as K
import os
from pathlib import Path
import subprocess


def linked_libraries(path):
    output = subprocess.check_output([K.S_OTOOL_0A68, K.S_L_1E20, str(path)], text=True)
    return [line.strip().split(K.S_COMPATIBILITY_VERSION_8A13, 1)[0]
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
            replacement = K.S_LOADER_PATH_1531 + Path(relative).as_posix()
            if dependency != replacement:
                changes += [K.S_CHANGE_2CB2, dependency, replacement]
        if path.suffix == K.S_DYLIB_CB5B:
            changes += [K.S_ID_1E58, K.S_RPATH_6EEF + path.name]
        if changes:
            subprocess.run([K.S_INSTALL_NAME_TOOL_A6D1, *changes, str(path)], check=True)
    # Editing Mach-O load commands invalidates the compiler's existing signature.
    # Sign only after every edit; no identity/keychain or Developer ID is needed.
    for path in files:
        subprocess.run([K.S_CODESIGN_B8C9, K.S_FORCE_CA33, K.S_SIGN_5BDD, K.S_EMPTY_01F8, str(path)], check=True)
