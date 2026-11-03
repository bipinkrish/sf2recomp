"""Native input-tool paths and subprocess helpers shared by all supported hosts."""
from _sf2_tool_constants import HOST_TOOLS as K
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def host_target():
    return K.S_WINDOWS_F2ED if os.name == K.S_NT_88F0 else K.S_MACOS_91E2 if sys.platform == K.S_DARWIN_5439 else K.S_LINUX_09D1


def require_host(target):
    if target not in {K.S_LINUX_09D1, K.S_WINDOWS_F2ED, K.S_MACOS_91E2} or target != host_target():
        raise ValueError(f"Run {target} input-tool builds on a native {target} host.")
    from release_support import manifest
    if target == K.S_WINDOWS_F2ED and os.environ.get(K.S_MSYSTEM_844D) != K.S_UCRT64_E4F4 and not manifest():
        raise ValueError(K.S_USE_NATIVE_WINDOWS_PYTHON_FR_5ABC)


def build_bash():
    """Select MSYS2 explicitly: Windows searches System32 before PATH."""
    if host_target() != K.S_WINDOWS_F2ED:
        return K.BASH_COMMAND
    require_host(K.S_WINDOWS_F2ED)
    cygpath = shutil.which(K.MSYS_PATH_CONVERTER)
    if not cygpath:
        raise ValueError(K.MSYS_PATH_CONVERTER_MISSING)
    shell = Path(subprocess.check_output(
        [cygpath, K.MSYS_NATIVE_PATH_OPTION, K.MSYS_BASH_PATH], text=True).strip())
    if not shell.is_file():
        raise ValueError(K.MSYS_BASH_MISSING)
    return str(shell)


def tool_directory(target=None):
    target = target or host_target()
    return ROOT / K.S_TOOLS_BIN_16A3 / target if target != K.S_LINUX_09D1 else ROOT / K.S_TOOLS_BIN_16A3


def tool_path(name, target=None):
    target = target or host_target()
    return tool_directory(target) / (name + (K.S_EXE_A67B if target == K.S_WINDOWS_F2ED else K.S_EMPTY_6F49))


def native_executable(path, target=None):
    try:
        with Path(path).open(K.S_RB_81FE) as stream:
            magic = stream.read(K.N_4_4B22)
            target = target or host_target()
            if target == K.S_MACOS_91E2:
                # 64-bit Mach-O (either byte order), fat and fat64 containers.
                return magic in {K.B_B_XCF_XFA_XED_XFE_9D60, K.B_B_XFE_XED_XFA_XCF_BA42,
                                 K.B_B_XCA_XFE_XBA_XBE_BF83, K.B_B_XBE_XBA_XFE_XCA_7DC0,
                                 K.B_B_XCA_XFE_XBA_XBF_CC4E, K.B_B_XBF_XBA_XFE_XCA_1125}
            return magic.startswith(K.B_B_MZ_F763 if target == K.S_WINDOWS_F2ED else K.B_B_X7FELF_C9CC)
    except OSError:
        return False


def run(*args, **kwargs):
    return subprocess.run([a.as_posix() if isinstance(a, Path) else str(a) for a in args], check=True, **kwargs)


def digest(path):
    with Path(path).open(K.S_RB_81FE) as stream:
        return hashlib.file_digest(stream, K.S_SHA256_9465).hexdigest()


def dotnet_environment():
    env = os.environ.copy()
    env.setdefault(K.S_DOTNET_ROLL_FORWARD_7F3A, K.S_MAJOR_1D5C)
    return env


def dotnet_sdk():
    command = os.environ.get(K.S_DOTNET_8CD7, K.S_DOTNET_B78C)
    executable = shutil.which(command)
    if not executable:
        raise ValueError(K.S_INSTALL_THE_NET_8_SDK_PUT_DO_5DE4)
    result = run(executable, K.S_LIST_SDKS_BB04, capture_output=True, text=True)
    if not any(int(v) >= K.N_8_2C62 for v in re.findall(K.S_D_2E81, result.stdout, re.MULTILINE)):
        raise ValueError(K.S_THE_NET_8_SDK_IS_REQUIRED_A__3461)
    return executable
