#!/usr/bin/env python3
"""Compare actual recompiled game RNG functions and LAN RNG numerically; no UI.
Requires locally generated supported game code and conversion/build dependencies.
"""
from _sf2_tool_constants import TEST_LAN_RANDOMNESS as K
import argparse
import hashlib
import json
import os
import socket
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
FUNCTIONS = {K.N_342_0238: [K.S_7001177D20_138F, K.S_7001177D80_5FF9],
             K.N_335_7ACC: [K.S_70010F1550_1B5B, K.S_70010F1AB0_4CC1, K.S_70010F1B80_5AF7, K.S_70010F1F90_C686],
             K.N_450_8315: [K.S_70018D6FE0_D588, K.S_70018D7080_E66B, K.S_70018D70F0_74AB]}


def synchronized_pair(output):
    with socket.socket() as sock:
        sock.bind((K.S_127_0_0_1_8D35, 0))
        port = str(sock.getsockname()[1])
    common = {k: v for k, v in os.environ.items() if not k.startswith((K.S_SF2_LAN_17C4, K.S_SF2_REPLAY_DA3E))}
    jobs, logs = [], []
    try:
        for role in (K.S_HOST_E293, K.S_JOIN_0986):
            env = dict(common, SF2_LAN_PORT=port)
            env[K.S_SF2_LAN_HOST_86F3 if role == K.S_HOST_E293 else K.S_SF2_LAN_JOIN_30DC] = K.S_127_0_0_1_8D35
            if role == K.S_HOST_E293:
                env[K.S_SF2_LAN_SEED_2218] = K.S_12345_3B45
            log = (output / (role + K.S_SYNC_LOG_8010)).open(K.S_W_780F)
            logs.append(log)
            jobs.append(subprocess.Popen([str(output / K.S_SAMPLER_E316), K.S_PEER_7CD0], env=env, stdout=subprocess.PIPE, stderr=log, text=True))
        peers = []
        for job in jobs:
            text, _ = job.communicate(timeout=K.N_20_F5CA)
            if job.returncode:
                raise RuntimeError(K.S_HEADLESS_RNG_SYNCHRONIZATION_9585)
            peers.append(json.loads(text))
        assert peers[0][K.S_HASH_0E06] == peers[1][K.S_HASH_0E06] and all(p[K.S_STEPS_D30D] == K.N_1024_E39E for p in peers)
        return {K.S_PASSED_7BB2: True, K.S_PEERS_9C2A: peers}
    finally:
        for job in jobs:
            if job.poll() is None:
                job.kill()
                job.communicate()
        for log in logs:
            log.close()


def run(output, samples):
    output.mkdir(parents=True, exist_ok=False)
    blocks, manifest = [], []
    for chunk, names in FUNCTIONS.items():
        path = ROOT / f'game/gen/main/main_{chunk:03}.c'
        text = path.read_text()
        for name in names:
            start = text.index(f'void f_{name}(Cpu *c) {{')
            block = text[start:text.index(K.S_N_47EC, start) + K.N_2_D473]
            blocks.append(block)
            manifest.append({K.S_FUNCTION_91BD: name, K.S_SOURCE_5B71: str(path), K.S_SHA256_9465: hashlib.sha256(block.encode()).hexdigest()})
    sys.path.insert(0, str(ROOT / K.S_TOOLS_RECOMPR_6C0F))
    from nso import Nso
    nso = Nso(ROOT / K.S_GAME_EXTRACTED_UPDATE_EXEFS__0D9F)
    # These are the engine internalcalls actually registered for Unity Random.
    relocations = {r[0]: r[K.N_3_4E07] for r in nso.relocs}
    assert [relocations[p] for p in (K.N_60958512_F8B1, K.N_60958536_B46A, K.N_60958544_AAC8)] == [K.N_26046432_0DA1, K.N_26046592_B139, K.N_26046704_E680]
    declarations = K.S_N_AD65.join(f'void f_{name}(Cpu *c);' for names in FUNCTIONS.values() for name in names)
    (output / K.S_ORIGINAL_RNG_INC_B440).write_text(declarations + K.S_NVOID_F_7001B85980_CPU_C_N_38DB + K.S_N_AD65.join(blocks))
    sdl = Path(os.environ.get(K.S_SDL_DIR_2C71, ROOT / K.S_DEPS_3548))
    command = [K.S_CLANG_5CE7, K.S_O2_1875, K.S_FFUNCTION_SECTIONS_5B80, K.S_FDATA_SECTIONS_6341, K.S_I_C6C0, str(ROOT / K.S_RUNTIME_A233), K.S_I_C6C0, str(output), K.S_I_C6C0, str(sdl / K.S_INCLUDE_8C9D),
               f'-DORIGINAL_UNITY_FLOAT_SCALE={nso.u32(K.N_46810848_571D)}u', str(ROOT / K.S_TOOLS_LAN_RANDOMNESS_HARNESS_B9C3), str(ROOT / K.S_RUNTIME_LAN_C_2753),
               K.S_WL_GC_SECTIONS_F6CD, K.S_L_1E20, str(sdl / K.S_LIB_427F), K.S_WL_RPATH_6DD1 + str(sdl / K.S_LIB_427F), K.S_LSDL3_C720, K.S_LM_656A, K.S_PTHREAD_AF25, K.S_O_010C, str(output / K.S_SAMPLER_E316)]
    with (output / K.S_BUILD_LOG_AF0D).open(K.S_W_780F) as log:
        subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
    with (output / K.S_MEASUREMENTS_JSONL_0E5C).open(K.S_W_780F) as log:
        subprocess.run([str(output / K.S_SAMPLER_E316), str(samples)], stdout=log, check=True)
    rows = [json.loads(line) for line in (output / K.S_MEASUREMENTS_JSONL_0E5C).read_text().splitlines()]
    # Deliberately broken old setup formula must fail the joint-choice check.
    for row in rows:
        if row[K.S_KIND_395E] == K.S_JOINT_CHOICES_DF34:
            row[K.S_PASSES_B5F1] = row[K.S_MISSING_TRIPLES_648F] == 0 and row[K.S_CHI2_PER_DF_CA71] < K.N_2_D473 and abs(row[K.S_FIGHTER_CORRELATION_0532]) < K.N_0_015_C03E
        elif row[K.S_KIND_395E] == K.S_DISTRIBUTION_2D26:
            row[K.S_PASSES_B5F1] = row[K.S_OUT_OF_RANGE_D464] == 0 and row[K.S_MAX_BUCKET_Z_1F82] < K.N_6_E7F6 and abs(row[K.S_LAG1_CORRELATION_EC71]) < K.N_0_015_C03E
        elif row[K.S_KIND_395E] == K.S_EXACT_NATIVE_NEKKI_COMPARISO_B2DA:
            row[K.S_PASSES_B5F1] = row[K.S_MISMATCHES_7411] == 0
        else:
            row[K.S_PASSES_B5F1] = abs(row[K.S_LAN_ACTUAL_CALLSITE_CORRELAT_20EB]) < K.N_0_015_C03E
    sync = synchronized_pair(output)
    result = {K.S_HEADLESS_RNG_NETWORK_SYNC_5370: sync, K.S_SAMPLES_PER_SEED_1B7F: samples, K.S_SEEDS_BA1F: K.N_8_2C62, K.S_MAIN_BUILD_ID_0733: nso.build_id, K.S_ORIGINAL_FUNCTIONS_18AC: manifest,
              K.S_MEASUREMENTS_1B8D: rows,
              K.S_PASSED_7BB2: all(r[K.S_PASSES_B5F1] for r in rows if r.get(K.S_NAME_8423) != K.S_OLD_CORRELATED_MENU_62AC),
              K.S_NEGATIVE_CONTROL_DETECTED_5C9B: any(r.get(K.S_NAME_8423) == K.S_OLD_CORRELATED_MENU_62AC and not r[K.S_PASSES_B5F1] for r in rows),
              K.S_LIMITS_D10E: K.S_FINITE_DIAGNOSTIC_BATTERY_NO_D1F3}
    (output / K.S_RESULTS_JSON_179E).write_text(json.dumps(result, indent=K.N_2_D473) + K.S_N_AD65)
    for row in rows:
        print(json.dumps(row))
    print(K.S_PASSED_EE78, result[K.S_PASSED_7BB2], K.S_NEGATIVE_CONTROL_DETECTED_A4B5, result[K.S_NEGATIVE_CONTROL_DETECTED_5C9B])
    return result


if __name__ == K.S_MAIN_ACB5:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(K.S_OUTPUT_3033, type=Path, required=True)
    parser.add_argument(K.S_SAMPLES_4AFF, type=int, default=K.N_100000_3BB7)
    args = parser.parse_args()
    if args.samples < K.N_100000_3BB7:
        parser.error(K.S_SAMPLES_MUST_BE_AT_LEAST_100_51A0)
    result = run(args.output.resolve(), args.samples)
    sys.exit(0 if result[K.S_PASSED_7BB2] and result[K.S_NEGATIVE_CONTROL_DETECTED_5C9B] else 1)
