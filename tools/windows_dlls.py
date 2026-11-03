"""Resolve native PE DLL dependencies before writing a Windows game package."""
from _sf2_tool_constants import WINDOWS_DLLS as K
import os
from pathlib import Path
import re
import shutil
import subprocess

# The Vulkan loader is supplied by the graphics driver. Never package a driver
# or an MSYS/Cygwin runtime beside the native game executable.
SYSTEM_DLLS = set(K.S_NADVAPI32_BCRYPT_CFGMGR32_CO_3AD2.split())


def imports(binary, objdump=None):
    tool = objdump or shutil.which(K.S_OBJDUMP_4C71) or shutil.which(K.S_LLVM_OBJDUMP_CF1B)
    if not tool:
        raise ValueError(K.S_DLL_PACKAGING_REQUIRES_OBJDU_9C33)
    result = subprocess.run([str(tool), K.S_P_C8C0, str(binary)], check=True, capture_output=True, text=True)
    return re.findall(K.S_DLL_NAME_S_S_402D, result.stdout, re.IGNORECASE)


def dependency_closure(binaries, search_dirs, objdump=None):
    """Return {lowercase DLL name: source path}; missing imports fail closed."""
    folders = list(dict.fromkeys(Path(p).resolve() for p in search_dirs))
    indexes = {p: {f.name.lower(): f for f in p.iterdir() if f.is_file()}
               for p in folders if p.is_dir()}
    system = Path(os.environ.get(K.S_SYSTEMROOT_4D37, K.S_C_WINDOWS_3A00)) / K.S_SYSTEM32_24D6
    pending = [Path(p) for p in binaries]
    visited, found = set(), {}
    while pending:
        binary = pending.pop()
        key = str(binary.resolve()).lower()
        if key in visited:
            continue
        visited.add(key)
        for name in imports(binary, objdump):
            lower = name.lower()
            if not re.fullmatch(K.S_A_Z0_9_DLL_0DE8, lower):
                raise ValueError(f"Invalid DLL import in {binary.name}: {name}")
            if lower in {K.S_MSYS_2_0_DLL_2950, K.S_CYGWIN1_DLL_2414}:
                raise ValueError(f"{binary.name} depends on {name}; use native UCRT64 libraries.")
            if (lower[:-K.N_4_4B22] in SYSTEM_DLLS or lower.startswith((K.S_API_MS_WIN_1210, K.S_EXT_MS_WIN_E963))
                    or (system / name).is_file()):
                continue
            candidate = next((indexes[p][lower] for p in folders if lower in indexes.get(p, {})), None)
            if candidate is None:
                raise ValueError(f"Missing Windows DLL {name}, required by {binary.name}.")
            if lower not in found:
                found[lower] = candidate
                pending.append(candidate)
    return found
