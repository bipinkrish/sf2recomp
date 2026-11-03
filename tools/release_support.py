"""The installed release contract, shared by conversion and input preparation.

Source checkouts build their dependencies. Releases consume a sealed set of
prebuilt tools and compile only the user's generated C, without shell tooling.
"""
from _sf2_tool_constants import RELEASE_SUPPORT as K
import concurrent.futures
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / K.S_RELEASE_JSON_5FBE
SLOW = {f"main_{n:03}" for n in (K.N_47_3148, K.N_183_B8AE, K.N_662_81F2, K.N_704_E4E5, K.N_735_1A42, K.N_746_5DAD, K.N_759_B967, K.N_48_9801, K.N_188_D606, K.N_526_F7C2, K.N_673_F446, K.N_715_35C7, K.N_770_CA0C)} | {K.S_SDK_090_1F9D}


def manifest():
    if not MANIFEST.is_file():
        return None
    data = json.loads(MANIFEST.read_text(encoding=K.S_UTF_8_5383))
    if data.get(K.S_SCHEMA_9B05) != 1:
        raise ValueError(K.S_UNSUPPORTED_SF2RECOMP_RELEAS_555D)
    return data


def expand(parts):
    return [str(p).replace(K.S_ROOT_51FF, str(ROOT)).replace(K.S_PYTHON_44E8, sys.executable) for p in parts]


def helper(name):
    data = manifest()
    return expand(data[K.S_HELPERS_7DB9][name]) if data else None


def environment():
    env = os.environ.copy()
    env.pop(K.S_PYTHONHOME_E7F0, None)
    env.pop(K.S_PYTHONPATH_D57E, None)
    env[K.S_PYTHONNOUSERSITE_A07F] = K.S_1_9A76
    # Keep all compiler caches in the writable tool directory.
    env[K.S_ZIG_GLOBAL_CACHE_DIR_2F3E] = str(ROOT / K.S_CACHE_ZIG_GLOBAL_73DB)
    env[K.S_ZIG_LOCAL_CACHE_DIR_EA24] = str(ROOT / K.S_CACHE_ZIG_LOCAL_13D6)
    env[K.S_DOTNET_CLI_HOME_FC3A] = str(ROOT / K.S_CACHE_DOTNET_3785)
    env[K.S_DOTNET_MULTILEVEL_LOOKUP_C70E] = K.S_0_59A9
    env[K.S_DOTNET_SYSTEM_GLOBALIZATION__E5F8] = K.S_1_9A76
    env[K.S_PYTHONDONTWRITEBYTECODE_2982] = K.S_1_9A76
    env[K.S_PATH_B629] = str(ROOT / K.S_NATIVE_LIB_601C) + os.pathsep + env.get(K.S_PATH_B629, K.S_EMPTY_6F49)
    if sys.platform.startswith(K.S_LINUX_09D1):
        env[K.S_LD_LIBRARY_PATH_A0EE] = str(ROOT / K.S_NATIVE_LIB_601C)
    if sys.platform == K.S_DARWIN_5439:
        env[K.S_VK_DRIVER_FILES_B081] = str(ROOT / K.S_NATIVE_MOLTENVK_ICD_JSON_4F2D)
    return env


def compile_sources(sources, output, jobs=1, *, runtime=True):
    """Compile translated modules and link against the release's own runtime."""
    data = manifest()
    if not data:
        raise ValueError(K.S_THIS_COMMAND_REQUIRES_AN_ASS_09C1)
    compiler = expand(data[K.S_COMPILER_01AF])
    common = expand(data[K.S_CFLAGS_4DFE]) + [K.S_FVISIBILITY_HIDDEN_226F, K.S_FNO_STRICT_ALIASING_E1F5, K.S_WNO_RETURN_TYPE_2049,
        K.S_WNO_UNUSED_LABEL_4FFF, K.S_I_C6C0 + str(ROOT / K.S_RUNTIME_A233)]
    cache = ROOT / K.S_BUILD_RELEASE_OBJECTS_054C
    cache.mkdir(parents=True, exist_ok=True)
    env = environment()

    def compile_one(source):
        # main/sdk have separate names, including their table translation units.
        obj = cache / (source.stem + K.S_O_8C6F)
        stamp = obj.with_suffix(K.S_JSON_D021)
        flags = [K.S_O0_9C08 if source.stem in SLOW else K.S_O1_639F, *common]
        identity = {K.S_SOURCE_5B71: str(source.resolve()), K.S_MTIME_542F: source.stat().st_mtime_ns,
                    K.S_FLAGS_13A4: flags, K.S_RELEASE_3AB3: data[K.S_VERSION_25EB], K.S_BUILD_ID_34C8: data[K.S_BUILD_ID_34C8]}
        try:
            cached = json.loads(stamp.read_text())
        except (OSError, ValueError):
            cached = None
        if not (obj.is_file() and cached == identity):
            pending = obj.with_suffix(K.S_PENDING_O_2199)
            subprocess.run([*compiler, *flags, K.S_C_3F7E, str(source), K.S_O_010C, str(pending)], check=True, env=env)
            pending.replace(obj)
            pending_stamp = stamp.with_suffix(K.S_TMP_31A6)
            pending_stamp.write_text(json.dumps(identity), encoding=K.S_UTF_8_5383)
            pending_stamp.replace(stamp)
        return obj

    print(f"Building game: {len(sources)} files, {jobs} workers...", flush=True)
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        pending_jobs = [pool.submit(compile_one, source) for source in sorted(sources)]
        objects = []
        try:
            for completed, future in enumerate(concurrent.futures.as_completed(pending_jobs), 1):
                objects.append(future.result())
                if completed == len(sources) or completed % K.N_25_B7A5 == 0:
                    print(f"Building game: {completed}/{len(sources)} files ready", flush=True)
        except BaseException:
            for future in pending_jobs:
                future.cancel()
            raise
    objects.sort()
    print(K.S_LINKING_GAME_3235, flush=True)
    output.parent.mkdir(parents=True, exist_ok=True)
    pending = output.with_name(output.stem + K.S_PENDING_C62D + output.suffix)
    flags = expand(data[K.S_LDFLAGS_861D] if runtime else data[K.S_PROBE_LDFLAGS_BA61])
    # Response files avoid Windows command-line limits and preserve spaces.
    response = cache / K.S_LINK_RSP_F1EE
    arguments = [*map(str, objects), *flags, K.S_O_010C, str(pending)]
    response.write_text(K.S_N_AD65.join(K.S_EMPTY_3251 + p.replace(K.S_EMPTY_6672, K.S_EMPTY_0825).replace(K.S_EMPTY_3251, K.S_EMPTY_08A0) + K.S_EMPTY_3251 for p in arguments), encoding=K.S_UTF_8_5383)
    subprocess.run([*compiler, K.S_EMPTY_DDB8 + str(response)], check=True, env=env)
    pending.replace(output)
    return output
