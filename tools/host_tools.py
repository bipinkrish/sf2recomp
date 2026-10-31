"""Native input-tool paths and subprocess helpers shared by all supported hosts."""
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def host_target():
    return "windows" if os.name == "nt" else "macos" if sys.platform == "darwin" else "linux"


def require_host(target):
    if target not in {"linux", "windows", "macos"} or target != host_target():
        raise ValueError(f"Run {target} input-tool builds on a native {target} host.")
    if target == "windows" and os.environ.get("MSYSTEM") != "UCRT64":
        raise ValueError("Use native Windows Python from an MSYS2 UCRT64 shell; see docs/windows.md.")


def tool_directory(target=None):
    target = target or host_target()
    return ROOT / "tools/bin" / target if target != "linux" else ROOT / "tools/bin"


def tool_path(name, target=None):
    target = target or host_target()
    return tool_directory(target) / (name + (".exe" if target == "windows" else ""))


def native_executable(path, target=None):
    try:
        with Path(path).open("rb") as stream:
            magic = stream.read(4)
            target = target or host_target()
            if target == "macos":
                # 64-bit Mach-O (either byte order), fat and fat64 containers.
                return magic in {b"\xcf\xfa\xed\xfe", b"\xfe\xed\xfa\xcf",
                                 b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca",
                                 b"\xca\xfe\xba\xbf", b"\xbf\xba\xfe\xca"}
            return magic.startswith(b"MZ" if target == "windows" else b"\x7fELF")
    except OSError:
        return False


def run(*args, **kwargs):
    return subprocess.run([a.as_posix() if isinstance(a, Path) else str(a) for a in args], check=True, **kwargs)


def digest(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def dotnet_environment():
    env = os.environ.copy()
    env.setdefault("DOTNET_ROLL_FORWARD", "Major")
    return env


def dotnet_sdk():
    command = os.environ.get("DOTNET", "dotnet")
    executable = shutil.which(command)
    if not executable:
        raise ValueError("Install the .NET 8+ SDK; put dotnet on PATH or set DOTNET to its executable path.")
    result = run(executable, "--list-sdks", capture_output=True, text=True)
    if not any(int(v) >= 8 for v in re.findall(r"^(\d+)\.", result.stdout, re.MULTILINE)):
        raise ValueError("The .NET 8+ SDK is required; a runtime-only install cannot build the input tools.")
    return executable
