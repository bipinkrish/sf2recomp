"""Install the optional Android toolchain inside SF2Recomp, never globally."""
from _sf2_tool_constants import SETUP_ANDROID as K
import argparse
import json
import os
from pathlib import Path
import platform
import re
import subprocess
import sys
import urllib.parse
import urllib.request
from downloads import download, unpack, open_url

ROOT = Path(__file__).resolve().parents[1]
TOOLCHAIN = dict(line.split(K.S_EMPTY_C874, 1) for line in (ROOT / K.S_ANDROID_TOOLCHAIN_PROPERTIES_C485).read_text().splitlines()
                 if line.strip() and not line.lstrip().startswith(K.S_EMPTY_4734))
NDK = TOOLCHAIN[K.S_NDK_41BB]
PACKAGES = (K.S_PLATFORM_TOOLS_CDB4, K.S_PLATFORMS_ANDROID_4347 + TOOLCHAIN[K.S_COMPILE_SDK_D63B],
            K.S_BUILD_TOOLS_2479 + TOOLCHAIN[K.S_BUILD_TOOLS_8451], f"ndk;{NDK}", K.S_CMAKE_C8B0 + TOOLCHAIN[K.S_CMAKE_38DF])
# Google's published SHA-256 checksums, https://developer.android.com/studio
COMMAND_TOOLS = {
    K.S_LINUX_09D1: (K.S_LINUX_09D1, K.S_4E4C464F145A7512B57D088AC6C2_6390),
    K.S_WINDOWS_F2ED: (K.S_WIN_7C20, K.S_90AE805D20434428BFFCB699C290_910E),
    K.S_MACOS_X86_64_B004: (K.S_MAC_X86_64_5E04, K.S_C5A6378AB5CF7E0D570192140511_BD6E),
    K.S_MACOS_ARM64_0404: (K.S_MAC_ARM64_F933, K.S_835B62A26162B229B441D1F6D468_8EA6),
}
JDK_VERSION = TOOLCHAIN[K.S_JDK_BE7D]


def host():
    return K.S_WINDOWS_F2ED if os.name == K.S_NT_88F0 else K.S_MACOS_91E2 if sys.platform == K.S_DARWIN_5439 else K.S_LINUX_09D1


def environment(directory=None):
    directory = Path(directory or ROOT / K.S_EXTENSIONS_ANDROID_A2DA).resolve()
    sdk, jdk = directory / K.S_SDK_0065, directory / K.S_JDK_BE7D / JDK_VERSION
    java = jdk / K.S_CONTENTS_HOME_930F if host() == K.S_MACOS_91E2 else jdk
    env = os.environ.copy()
    env.update(ANDROID_HOME=str(sdk), ANDROID_SDK_ROOT=str(sdk), JAVA_HOME=str(java),
               GRADLE_USER_HOME=str(directory / K.S_GRADLE_0D08), ANDROID_USER_HOME=str(directory / K.S_USER_55D4),
               ANDROID_PREFS_ROOT=str(directory / K.S_USER_55D4))
    env[K.S_PATH_B629] = str(java / K.S_BIN_0955) + os.pathsep + str(sdk / K.S_PLATFORM_TOOLS_CDB4) + os.pathsep + env.get(K.S_PATH_B629, K.S_EMPTY_6F49)
    return env


def components(env):
    sdk = Path(env[K.S_ANDROID_HOME_00C5])
    return (sdk / (K.S_PLATFORMS_ANDROID_AA33 + TOOLCHAIN[K.S_COMPILE_SDK_D63B] + K.S_ANDROID_JAR_7F93), sdk / f"ndk/{NDK}/build/cmake/android.toolchain.cmake",
                sdk / (K.S_CMAKE_493A + TOOLCHAIN[K.S_CMAKE_38DF] + K.S_BIN_57F6) / (K.S_CMAKE_EXE_FD68 if os.name == K.S_NT_88F0 else K.S_CMAKE_38DF),
                sdk / (K.S_BUILD_TOOLS_0393 + TOOLCHAIN[K.S_BUILD_TOOLS_8451]) / (K.S_AAPT2_EXE_2974 if os.name == K.S_NT_88F0 else K.S_AAPT2_5E66),
                Path(env[K.S_JAVA_HOME_10D6]) / K.S_BIN_0955 / (K.S_JAVA_EXE_7EFD if os.name == K.S_NT_88F0 else K.S_JAVA_E680))


def ready(directory=None):
    directory = Path(directory or ROOT / K.S_EXTENSIONS_ANDROID_A2DA).resolve()
    stamp = directory / K.S_READY_JSON_1117
    try:
        record = json.loads(stamp.read_text())
        return (record.get(K.S_PACKAGES_27EC) == list(PACKAGES) and record.get(K.S_JDK_BE7D) == JDK_VERSION
                and all(path.is_file() for path in components(environment(directory))))
    except (OSError, ValueError):
        return False


def ensure(*, accept_licenses=False, directory=None):
    directory = Path(directory or ROOT / K.S_EXTENSIONS_ANDROID_A2DA).resolve()
    sdk = directory / K.S_SDK_0065
    env = environment(directory)
    if ready(directory):
        return env
    if not accept_licenses:
        raise ValueError(K.S_ANDROID_SETUP_REQUIRES_ACCEP_702E)
    directory.mkdir(parents=True, exist_ok=True)
    cache = directory / K.S_DOWNLOADS_A2FB
    system = host()
    arch = K.S_ARM64_6AF1 if platform.machine().lower() in (K.S_ARM64_6AF1, K.S_AARCH64_E802) else K.S_X86_64_CB4A
    key = f"macos-{arch}" if system == K.S_MACOS_91E2 else system
    name, checksum = COMMAND_TOOLS[key]
    filename = f"commandlinetools-{name}-15859902_latest.zip"
    archive = download(f"https://dl.google.com/android/repository/{filename}", cache / filename, checksum)
    unpack(archive, sdk / K.S_CMDLINE_TOOLS_LATEST_10A4, root=K.S_CMDLINE_TOOLS_B093)
    if not (Path(env[K.S_JAVA_HOME_10D6]) / K.S_BIN_0955 / (K.S_JAVA_EXE_7EFD if os.name == K.S_NT_88F0 else K.S_JAVA_E680)).is_file():
        api_os = {K.S_MACOS_91E2: K.S_MAC_5E63, K.S_WINDOWS_F2ED: K.S_WINDOWS_F2ED, K.S_LINUX_09D1: K.S_LINUX_09D1}[system]
        api_arch = K.S_AARCH64_E802 if arch == K.S_ARM64_6AF1 else K.S_X64_2974
        version, build = JDK_VERSION.split(K.S_EMPTY_3604)
        extension = K.S_ZIP_7EE9 if system == K.S_WINDOWS_F2ED else K.S_TAR_GZ_FCC5
        major = version.split(K.S_EMPTY_277A)[0]
        filename = f"OpenJDK{major}U-jdk_{api_arch}_{api_os}_hotspot_{version}_{build}.{extension}"
        tag = urllib.parse.quote(K.S_JDK_A3CD + JDK_VERSION, safe=K.S_EMPTY_6F49)
        url = f"https://github.com/adoptium/temurin{major}-binaries/releases/download/{tag}/{filename}"
        with open_url(url + K.S_SHA256_TXT_E6EA) as response:
            checksum = response.read().decode(K.S_ASCII_0C02).split()[0]
        if not re.fullmatch(K.S_0_9A_F_64_64DC, checksum):
            raise ValueError(K.S_INVALID_PUBLISHED_JAVA_CHECK_43E5)
        archive = download(url, cache / filename, checksum)
        unpack(archive, directory / K.S_JDK_BE7D / JDK_VERSION, root=K.S_EMPTY_15A7)
    # Invoke sdkmanager's Java main class directly, avoiding .bat/shell scripts.
    manager = [str(Path(env[K.S_JAVA_HOME_10D6]) / K.S_BIN_0955 / (K.S_JAVA_EXE_7EFD if os.name == K.S_NT_88F0 else K.S_JAVA_E680)),
               K.S_DCOM_ANDROID_SDKLIB_TOOLSDIR_A8A5 + str(sdk / K.S_CMDLINE_TOOLS_LATEST_10A4),
               K.S_CP_7E0F, str(sdk / K.S_CMDLINE_TOOLS_LATEST_LIB_3306),
               K.S_COM_ANDROID_SDKLIB_TOOL_SDKM_27E4, K.S_SDK_ROOT_B5D4 + str(sdk)]
    subprocess.run([*manager, K.S_LICENSES_5ADF], input=K.S_Y_N_68B4 * K.N_100_AD57, text=True, check=True, env=env)
    subprocess.run([*manager, *PACKAGES], check=True, env=env)
    if not all(path.is_file() for path in components(env)):
        raise ValueError(K.S_ANDROID_TOOLCHAIN_INSTALLATI_699C)
    stamp = directory / K.S_READY_JSON_1117
    pending = stamp.with_suffix(K.S_TMP_31A6)
    pending.write_text(json.dumps({K.S_PACKAGES_27EC: PACKAGES, K.S_JDK_BE7D: JDK_VERSION}), encoding=K.S_UTF_8_5383)
    pending.replace(stamp)
    return env


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(K.S_ACCEPT_ANDROID_LICENSES_236F, action=K.S_STORE_TRUE_2C29)
    args = parser.parse_args()
    ensure(accept_licenses=args.accept_android_licenses)
    print(K.S_ANDROID_EXTENSION_READY_IN_077B, ROOT / K.S_EXTENSIONS_ANDROID_A2DA)


if __name__ == K.S_MAIN_ACB5:
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        sys.exit(str(error))
