#!/usr/bin/env python3
"""Opt-in hardware test of the runtime's actual driver-owned heap allocator.

Requires Vulkan development files, Clang and glslc. Select a driver using
VK_DRIVER_FILES (e.g. /usr/share/vulkan/icd.d/radeon_icd.json). Allocates the
full guest heap, runs a compute read/modify/write at its beginning, middle and
end, checks host visibility after a fence, then repeats after replacing the heap.
No game files or saves are used. Does not enable external-memory host imports.
"""
from _sf2_tool_constants import TEST_GPU_HEAP as K
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]

PRELUDE = K.PRELUDE

MAIN = K.MAIN


def main():
    from centralize_literals import expanded
    source = expanded(ROOT / K.S_RUNTIME_NVN_VULKAN_EXEC_C_AEA5)
    start = source.index(K.S_STATIC_BOOL_GMEM_DMABUF_6389)
    end = source.index(K.S_N_ENDIF_N_N_THE_GUEST_HEAP_S_5C4B, start)
    constants = K.S_N_AD65.join(line for line in expanded(ROOT / K.S_RUNTIME_RT_H_B604).splitlines()
                          if line.startswith((K.S_DEFINE_HEAP_BASE_7C89, K.S_DEFINE_HEAP_MAX_0D14)))
    with tempfile.TemporaryDirectory(prefix=K.S_SF2_GPU_HEAP_84FD) as directory:
        work = pathlib.Path(directory)
        (work / K.S_TEST_C_07A8).write_text(PRELUDE + constants + K.S_N_AD65 + source[start:end] + MAIN)
        (work / K.S_TEST_COMP_2B42).write_text(K.S_VERSION_450_NLAYOUT_LOCAL_SI_D979)
        subprocess.run([K.S_GLSLC_6670, str(work / K.S_TEST_COMP_2B42), K.S_O_010C, str(work / K.S_TEST_SPV_80BB)], check=True)
        subprocess.run([K.S_CLANG_5CE7, K.S_O2_1875, K.S_WALL_DED7, K.S_WEXTRA_814A, str(work / K.S_TEST_C_07A8),
                        K.S_LVULKAN_E7F5, K.S_O_010C, str(work / K.S_TEST_6E94)], check=True)
        subprocess.run([str(work / K.S_TEST_6E94), str(work / K.S_TEST_SPV_80BB)], check=True, timeout=K.N_45_8117)


if __name__ == K.S_MAIN_ACB5:
    try:
        main()
    except (subprocess.CalledProcessError, subprocess.TimeoutExpired, FileNotFoundError) as error:
        print(f"GPU heap test failed: {error}", file=sys.stderr)
        sys.exit(1)
