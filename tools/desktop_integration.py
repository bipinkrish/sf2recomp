"""Write native desktop app metadata and launcher resources."""
from _sf2_tool_constants import DESKTOP_INTEGRATION as K
import os
from pathlib import Path
import plistlib
import re
import shutil

CONVERTER_ID = K.S_ORG_SF2RECOMP_CONVERTER_1D81
LINUX_CONVERTER_ICON = K.S_ASSETS_SF2RECOMP_README_PNG_6BA4


def write_macos_app(folder, executable, icon, *, title, identifier, version, hardlink=False):
    from PIL import Image

    contents = folder / f"{title}.app" / K.S_CONTENTS_9785
    binary = contents / K.S_MACOS_61C2 / executable.name
    binary.parent.mkdir(parents=True, exist_ok=True)
    resources = contents / K.S_RESOURCES_9DEA
    resources.mkdir(exist_ok=True)
    with Image.open(icon) as image:
        image.convert(K.S_RGBA_6607).save(resources / K.S_APPICON_ICNS_44CB)
    numeric = re.match(K.S_R_D_D_D_38BC, version)[0]
    (contents / K.S_INFO_PLIST_23A1).write_bytes(plistlib.dumps({
        K.S_CFBUNDLEIDENTIFIER_8DA2: identifier, K.S_CFBUNDLENAME_8066: title, K.S_CFBUNDLEDISPLAYNAME_05D2: title,
        K.S_CFBUNDLEEXECUTABLE_29D7: executable.name, K.S_CFBUNDLEICONFILE_E7FF: K.S_APPICON_ICNS_44CB,
        K.S_CFBUNDLEPACKAGETYPE_461C: K.S_APPL_716E, K.S_CFBUNDLESHORTVERSIONSTRING_3461: numeric,
        K.S_CFBUNDLEVERSION_9A09: numeric, K.S_NSHIGHRESOLUTIONCAPABLE_3F5A: True,
    }))
    pending = binary.with_suffix(K.S_PENDING_276C)
    pending.unlink(missing_ok=True)
    if hardlink:
        try:
            os.link(executable, pending)
        except OSError:
            shutil.copy2(executable, pending)
        # The Mach-O uses @executable_path/lib from either entry point.
        lib = binary.parent / K.S_LIB_5A09
        if not lib.is_symlink():
            lib.symlink_to(K.S_LIB_5EFF, target_is_directory=True)
    else:
        shutil.copy2(executable, pending)
    pending.replace(binary)
    return contents.parent


def desktop_text(text):
    """Escape the desktop-entry string format independently of command quoting."""
    return str(text).replace(K.S_EMPTY_687A, K.S_EMPTY_11C3).replace(K.S_N_D7A5, K.S_N_6309).replace(K.S_R_8EB8, K.S_R_1074).replace(K.S_T_729E, K.S_T_DB1A)


def desktop_exec(path):
    # Exec has a second quoting layer; percent signs are field-code escapes.
    if K.S_EMPTY_90FB in str(path):
        raise ValueError(K.S_DESKTOP_EXECUTABLE_PATHS_CAN_93FB)
    quoted = K.S_EMPTY_12AE.join(K.S_EMPTY_687A + c if c in K.S_EMPTY_D3F3 else c for c in str(path))
    return desktop_text(K.S_EMPTY_3251 + quoted.replace(K.S_EMPTY_AAE8, K.S_EMPTY_A21C) + K.S_EMPTY_3251)


def linux_desktop(executable, icon, *, title, identifier, version, category, window_class=None):
    return ("[Desktop Entry]\nVersion=1.0\nType=Application\n"
            f"Name={desktop_text(title)}\nExec={desktop_exec(executable)}\n"
            f"Path={desktop_text(executable.parent)}\nIcon={desktop_text(icon)}\n"
            f"Terminal=false\nCategories={category};\nStartupWMClass={window_class or identifier}\n"
            f"X-AppVersion={desktop_text(version)}\n")


def register_linux_desktop(identifier, entry):
    data = Path(os.environ.get(K.S_XDG_DATA_HOME_76F9) or Path.home() / K.S_LOCAL_SHARE_5321)
    if not data.is_absolute():
        raise ValueError(K.S_XDG_DATA_HOME_MUST_BE_AN_ABS_FF80)
    applications = data / K.S_APPLICATIONS_2206
    applications.mkdir(parents=True, exist_ok=True)
    target = applications / f"{identifier}.desktop"
    target.write_text(entry, encoding=K.S_UTF_8_D983)
    print(f"Registered user application: {target}")
    return target
