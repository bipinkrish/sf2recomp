#!/usr/bin/env python3
"""Compare actual recompiled game RNG functions and LAN RNG numerically; no UI.
Requires locally generated supported game code and conversion/build dependencies.
"""
import argparse
import hashlib
import json
import os
import socket
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
FUNCTIONS = {342: ['7001177d20', '7001177d80'],
             335: ['70010f1550', '70010f1ab0', '70010f1b80', '70010f1f90'],
             450: ['70018d6fe0', '70018d7080', '70018d70f0']}


def synchronized_pair(output):
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        port = str(sock.getsockname()[1])
    common = {k: v for k, v in os.environ.items() if not k.startswith(('SF2_LAN_', 'SF2_REPLAY_'))}
    jobs, logs = [], []
    try:
        for role in ('host', 'join'):
            env = dict(common, SF2_LAN_PORT=port)
            env['SF2_LAN_HOST' if role == 'host' else 'SF2_LAN_JOIN'] = '127.0.0.1'
            if role == 'host':
                env['SF2_LAN_SEED'] = '12345'
            log = (output / (role + '-sync.log')).open('w')
            logs.append(log)
            jobs.append(subprocess.Popen([str(output / 'sampler'), '--peer'], env=env, stdout=subprocess.PIPE, stderr=log, text=True))
        peers = []
        for job in jobs:
            text, _ = job.communicate(timeout=20)
            if job.returncode:
                raise RuntimeError('headless RNG synchronization peer failed; inspect *-sync.log')
            peers.append(json.loads(text))
        assert peers[0]['hash'] == peers[1]['hash'] and all(p['steps'] == 1024 for p in peers)
        return {'passed': True, 'peers': peers}
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
            block = text[start:text.index('\n}', start) + 2]
            blocks.append(block)
            manifest.append({'function': name, 'source': str(path), 'sha256': hashlib.sha256(block.encode()).hexdigest()})
    sys.path.insert(0, str(ROOT / 'tools/recompr'))
    from nso import Nso
    nso = Nso(ROOT / 'game/extracted/update/exefs/main')
    # These are the engine internalcalls actually registered for Unity Random.
    relocations = {r[0]: r[3] for r in nso.relocs}
    assert [relocations[p] for p in (0x3a22730, 0x3a22748, 0x3a22750)] == [0x18d6fe0, 0x18d7080, 0x18d70f0]
    declarations = '\n'.join(f'void f_{name}(Cpu *c);' for names in FUNCTIONS.values() for name in names)
    (output / 'original_rng.inc').write_text(declarations + '\nvoid f_7001b85980(Cpu *c);\n' + '\n'.join(blocks))
    sdl = Path(os.environ.get('SDL_DIR', ROOT / 'deps'))
    command = ['clang', '-O2', '-ffunction-sections', '-fdata-sections', '-I', str(ROOT / 'runtime'), '-I', str(output), '-I', str(sdl / 'include'),
               f'-DORIGINAL_UNITY_FLOAT_SCALE={nso.u32(0x2ca46e0)}u', str(ROOT / 'tools/lan_randomness_harness.c'), str(ROOT / 'runtime/lan.c'),
               '-Wl,--gc-sections', '-L', str(sdl / 'lib'), '-Wl,-rpath,' + str(sdl / 'lib'), '-lSDL3', '-lm', '-pthread', '-o', str(output / 'sampler')]
    with (output / 'build.log').open('w') as log:
        subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
    with (output / 'measurements.jsonl').open('w') as log:
        subprocess.run([str(output / 'sampler'), str(samples)], stdout=log, check=True)
    rows = [json.loads(line) for line in (output / 'measurements.jsonl').read_text().splitlines()]
    # Deliberately broken old setup formula must fail the joint-choice check.
    for row in rows:
        if row['kind'] == 'joint_choices':
            row['passes'] = row['missing_triples'] == 0 and row['chi2_per_df'] < 2 and abs(row['fighter_correlation']) < .015
        elif row['kind'] == 'distribution':
            row['passes'] = row['out_of_range'] == 0 and row['max_bucket_z'] < 6 and abs(row['lag1_correlation']) < .015
        elif row['kind'] == 'exact_native_nekki_comparison':
            row['passes'] = row['mismatches'] == 0
        else:
            row['passes'] = abs(row['lan_actual_callsite_correlation']) < .015
    sync = synchronized_pair(output)
    result = {'headless_rng_network_sync': sync, 'samples_per_seed': samples, 'seeds': 8, 'main_build_id': nso.build_id, 'original_functions': manifest,
              'measurements': rows,
              'passed': all(r['passes'] for r in rows if r.get('name') != 'old_correlated_menu'),
              'negative_control_detected': any(r.get('name') == 'old_correlated_menu' and not r['passes'] for r in rows),
              'limits': 'Finite diagnostic battery, not a proof of independence or a full RNG certification. Original RNG algorithms execute unchanged; their state-storage provider is stubbed. Exact Nekki comparison aligns internal stream states, not original match outcomes.'}
    (output / 'results.json').write_text(json.dumps(result, indent=2) + '\n')
    for row in rows:
        print(json.dumps(row))
    print('passed:', result['passed'], 'negative control detected:', result['negative_control_detected'])
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--samples', type=int, default=100000)
    args = parser.parse_args()
    if args.samples < 100000:
        parser.error('--samples must be at least 100000 for the reported diagnostic thresholds')
    result = run(args.output.resolve(), args.samples)
    sys.exit(0 if result['passed'] and result['negative_control_detected'] else 1)
