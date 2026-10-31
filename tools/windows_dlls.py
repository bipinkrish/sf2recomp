"""Resolve native PE DLL dependencies before writing a Windows game package."""
import os
from pathlib import Path
import re
import shutil
import subprocess

# The Vulkan loader is supplied by the graphics driver. Never package a driver
# or an MSYS/Cygwin runtime beside the native game executable.
SYSTEM_DLLS = set("""
advapi32 bcrypt cfgmgr32 comctl32 comdlg32 crypt32 d3d12 dbghelp dhcpcsvc dnsapi
dwmapi dxgi gdi32 hid imm32 iphlpapi kernel32 msvcrt ntdll ole32 oleaut32 powrprof
propsys psapi rpcrt4 secur32 setupapi shell32 shlwapi ucrtbase user32 userenv
usp10 uxtheme version vulkan-1 winhttp wininet winmm winspool wintrust ws2_32
wtsapi32 normaliz avrt shcore dxcore
""".split())


def imports(binary, objdump=None):
    tool = objdump or shutil.which("objdump") or shutil.which("llvm-objdump")
    if not tool:
        raise ValueError("DLL packaging requires objdump or llvm-objdump on PATH.")
    result = subprocess.run([str(tool), "-p", str(binary)], check=True, capture_output=True, text=True)
    return re.findall(r"DLL Name:\s*([^\s]+)", result.stdout, re.IGNORECASE)


def dependency_closure(binaries, search_dirs, objdump=None):
    """Return {lowercase DLL name: source path}; missing imports fail closed."""
    folders = list(dict.fromkeys(Path(p).resolve() for p in search_dirs))
    indexes = {p: {f.name.lower(): f for f in p.iterdir() if f.is_file()}
               for p in folders if p.is_dir()}
    system = Path(os.environ.get("SystemRoot", "C:/Windows")) / "System32"
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
            if not re.fullmatch(r"[a-z0-9_+.\-]+\.dll", lower):
                raise ValueError(f"Invalid DLL import in {binary.name}: {name}")
            if lower in {"msys-2.0.dll", "cygwin1.dll"}:
                raise ValueError(f"{binary.name} depends on {name}; use native UCRT64 libraries.")
            if (lower[:-4] in SYSTEM_DLLS or lower.startswith(("api-ms-win-", "ext-ms-win-"))
                    or (system / name).is_file()):
                continue
            candidate = next((indexes[p][lower] for p in folders if lower in indexes.get(p, {})), None)
            if candidate is None:
                raise ValueError(f"Missing Windows DLL {name}, required by {binary.name}.")
            if lower not in found:
                found[lower] = candidate
                pending.append(candidate)
    return found
