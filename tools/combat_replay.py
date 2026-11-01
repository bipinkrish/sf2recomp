#!/usr/bin/env python3
"""Create logical combat tapes, compare traces, and run isolated graphics matrices."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
MENU = [(600, 'STICK_DOWN'), (690, 'STICK_DOWN'), (780, 'A'), (1000, 'A'),
        (1200, 'STICK_RIGHT'), (1400, 'A'), (1600, 'STICK_RIGHT'),
        (1800, 'A'), (2000, 'A'), (2200, 'A')]
CASES = [('baseline-a', 60, 'original', '1x', 'bilinear', 'off'),
         ('baseline-b', 60, 'original', '1x', 'bilinear', 'off'),
         ('aspect-16-10', 60, '16:10', '1x', 'bilinear', 'off'),
         ('wide-21-9', 60, '21:9', '1x', 'bilinear', 'off'),
         ('wide-32-9', 60, '32:9', '1x', 'bilinear', 'off'),
         ('fps-30', 30, 'original', '1x', 'bilinear', 'off'),
         ('fps-120', 120, 'original', '1x', 'bilinear', 'off'),
         ('fps-144', 144, 'original', '1x', 'bilinear', 'off'),
         ('scaled-bilinear', 60, 'original', '0.50x', 'bilinear', 'off'),
         ('scaled-fsr-smaa', 60, 'original', '0.50x', 'fsr1', 'smaa')]
ANIMATION_FIELDS = ['sign', 'current_node_id', 'first_frame', 'finish_frame',
                    'transition_first_frame', 'animation_frame', 'loop_frame', 'interframe',
                    'index', 'old_frame', 'where_position_bits', 'start_position_bits',
                    'model_start_position_bits', 'model_frame', 'wall_shift_bits',
                    'slow_factor_bits', 'frame_align', 'left_wall_margin', 'right_wall_margin',
                    'wall_left_bits', 'wall_right_bits', 'buffer_index', 'slow_buffer']
MODEL_FIELDS = ['index', 'current_sign', 'combo_time', 'round_stage', 'fight_frame_index',
                'strike_count', 'combo_type', 'combo_counter', 'control_before_fight',
                'event_type', 'animation_type', 'current_state', 'shock_count', 'ai_delay',
                'falling_time', 'fixed_sign', 'pain_bits', 'magic_count']


def scenario(path, ticks, kind='exchange'):
    """Both fighters move toward one another and use alternating attacks."""
    events = []
    def hold(tick, player, code, duration):
        if tick + duration < ticks:
            events.extend([(tick, player, code, 1), (tick + duration, player, code, 0)])
    if kind == 'edges':
        if ticks < 1000:
            raise ValueError('edge scenario requires at least 1000 ticks')
        hold(0, 1, 7, 800)
        hold(0, 2, 3, 800)
        for tick in range(810, ticks - 15, 25):
            hold(tick, 1, 9 if tick % 2 else 10, 4)
            hold(tick + 8, 2, 10 if tick % 2 else 9, 4)
    elif kind == 'walls':
        if ticks < 2000:
            raise ValueError('wall scenario requires at least 2000 ticks')
        for player in (1, 2):
            hold(0, player, 7, 500)       # pursue to the left boundary
            hold(700, player, 3, 1000)    # then traverse to the right boundary
        for start, end in ((510, 685), (1710, ticks - 15)):
            for tick in range(start, end, 25):
                hold(tick, 1, 9 if tick % 2 else 10, 4)
                hold(tick + 8, 2, 10 if tick % 2 else 9, 4)
    elif kind == 'exchange':
        hold(0, 1, 3, 75)
        hold(0, 2, 7, 75)
        for tick in range(85, ticks - 15, 35):
            hold(tick, 1, 9 if tick % 2 else 10, 4)
            hold(tick + 10, 2, 10 if tick % 2 else 9, 4)
    else:
        raise ValueError('unknown replay scenario')
    with path.open('x') as stream:
        stream.write('SF2_REPLAY 1\n')
        stream.write('# tick player(1/2) quadrant(0..14) down(0/1); stable order within each tick\n')
        for event in sorted(events, key=lambda e: e[0]):
            stream.write(' '.join(map(str, event)) + '\n')


def read_trace(path, retain_states=True):
    """Validate incrementally; full node traces can be hundreds of megabytes."""
    header, setup, complete = None, None, None
    inputs, states, next_tick = [], [], 0
    last_state = None
    with Path(path).open() as stream:
        for line in stream:
            if not line.strip():
                continue
            row = json.loads(line)
            if not isinstance(row, dict) or complete is not None:
                raise ValueError(f'{path}: invalid record or data after completion')
            kind = row.get('kind')
            if header is None:
                if kind != 'header' or row.get('schema') != 1 or not isinstance(row.get('limit'), int) or row['limit'] < 1:
                    raise ValueError(f'{path}: missing/unsupported trace header')
                header = row
            elif kind == 'setup' and setup is None:
                if not row.get('location') or len(row.get('players', [])) != 2 or not all(row['players']):
                    raise ValueError(f'{path}: invalid match setup')
                setup = row
            elif kind == 'state':
                if row.get('tick') != next_tick or not isinstance(row.get('state'), dict) or any(
                        not isinstance(row.get('diagnostic', {}).get(k), int)
                        for k in ('present', 'elapsed_ns', 'fight_frame')):
                    raise ValueError(f'{path}: missing/duplicate/out-of-order combat state or diagnostics')
                if header.get('coverage') == 'observed-combat-fields-v3':
                    framing = row['state'].get('arena_framing')
                    if (not isinstance(framing, list) or len(framing) != 6 or
                            any(type(v) is not int or not 0 <= v <= 0xffffffff for v in framing)):
                        raise ValueError(f'{path}: missing/invalid combat framing fields for v3 coverage')
                last_state = row['state']
                next_tick += 1
                if retain_states:
                    states.append(row)
            elif kind == 'input':
                event = {k: row[k] for k in ('tick', 'player', 'code', 'down')}
                if (not 0 <= event['tick'] < header['limit'] or event['player'] not in (1, 2) or
                        not 0 <= event['code'] <= 14 or event['down'] not in (0, 1) or
                        (inputs and event['tick'] < inputs[-1]['tick'])):
                    raise ValueError(f'{path}: invalid/out-of-order logical input')
                inputs.append(event)
            elif kind == 'complete':
                complete = row
            else:
                raise ValueError(f'{path}: duplicate markers or unknown trace records')
    if not header or complete is None:
        raise ValueError(f'{path}: incomplete trace; no successful completion marker')
    if header.get('end_on_match') is True and complete.get('reason') == 'match_finished':
        actual = complete.get('ticks')
        flags = (last_state or {}).get('flags', [])
        if (type(actual) is not int or not 0 < actual <= header['limit'] or
                not isinstance(flags, list) or len(flags) < 2 or flags[1] != 1):
            raise ValueError(f'{path}: early LAN completion requires native game-over state')
        header = {**header, 'planned_limit': header['limit'], 'limit': actual}
    if not setup or next_tick != header['limit'] + 1 or complete.get('ticks') != header['limit']:
        raise ValueError(f'{path}: missing setup, states or invalid completion count')
    if complete.get('events_applied') != len(inputs):
        raise ValueError(f'{path}: applied input count does not match trace')
    return header, setup, inputs, states


def state_records(path):
    # Called only after read_trace has validated the entire file. Keep just the
    # current pair of large node snapshots resident during exact comparison.
    with Path(path).open() as stream:
        for line in stream:
            if line.strip():
                row = json.loads(line)
                if row.get('kind') == 'state':
                    yield row


def differences(a, b, path='state', cap=12):
    result = []
    def visit(x, y, key):
        if len(result) >= cap:
            return
        if type(x) is not type(y):
            result.append({'field': key, 'reference': x, 'candidate': y})
        elif isinstance(x, dict):
            for k in sorted(x.keys() | y.keys()):
                if k not in x or k not in y:
                    result.append({'field': f'{key}.{k}', 'reference': x.get(k), 'candidate': y.get(k)})
                else:
                    visit(x[k], y[k], f'{key}.{k}')
                if len(result) >= cap:
                    return
        elif isinstance(x, list):
            if len(x) != len(y):
                result.append({'field': f'{key}.length', 'reference': len(x), 'candidate': len(y)})
            for i, (left, right) in enumerate(zip(x, y)):
                visit(left, right, f'{key}[{i}]')
        elif x != y:
            result.append({'field': key, 'reference': x, 'candidate': y})
    visit(a, b, path)
    for difference in result:
        raw = difference['field']
        field = raw
        for group, names in [('animation', ANIMATION_FIELDS), ('model', MODEL_FIELDS),
                             ('round', ['time', 'time_total', 'number', 'total', 'processing']),
                             ('controller', ['time', 'combo_interval']),
                             ('arena_framing', ['width_bits', 'location_scale_bits', 'scale_bits', 'min_scale_bits', 'center_bits', 'x_bits'])]:
            field = re.sub(r'\.' + group + r'\[(\d+)\]',
                           lambda m: '.' + group + '.' + names[int(m[1])] if int(m[1]) < len(names) else m[0], field)
        field = re.sub(r'\.(current|previous|shift|velocity)\[([012])\]',
                       lambda m: '.' + m[1] + '.' + 'xyz'[int(m[2])] + '_bits', field)
        if field != raw:
            difference.update(field=field, raw_field=raw)
    return result[:cap]


def compare(reference, candidate):
    a, b = read_trace(reference, False), read_trace(candidate, False)
    report = {'reference': str(reference), 'candidate': str(candidate), 'identical': False,
              'coverage': a[0]['coverage'], 'rng_policy': a[0]['rng_policy']}
    for key in ('game', 'limit', 'coverage', 'rng_policy', 'seed', 'initialization', 'combat_geometry', 'ai_enabled'):
        if a[0].get(key) != b[0].get(key):
            report.update(status='incompatible', differences=differences(a[0].get(key), b[0].get(key), f'header.{key}'))
            return report
    if a[1] != b[1] or a[2] != b[2]:
        report.update(status='incompatible', differences=differences(a[1], b[1], 'setup') or differences(a[2], b[2], 'inputs'))
        return report
    mismatch_ticks = 0
    timing = {}
    for left, right in zip(state_records(reference), state_records(candidate)):
        for key, row in (('reference', left), ('candidate', right)):
            if key not in timing:
                timing[key] = [row['diagnostic'], row['diagnostic']]
            timing[key][1] = row['diagnostic']
        # This logical game counter can affect later simulation. It is kept
        # beside presentation diagnostics in the trace but MUST be compared.
        left_state = {**left['state'], 'fight_frame': left['diagnostic']['fight_frame']}
        right_state = {**right['state'], 'fight_frame': right['diagnostic']['fight_frame']}
        if left_state != right_state:
            mismatch_ticks += 1
            if 'first_tick' not in report:
                report.update(first_tick=left['tick'], differences=differences(left_state, right_state))
    report.update(status='diverged' if mismatch_ticks else 'matched', identical=not mismatch_ticks,
                  mismatched_states=mismatch_ticks, compared_states=a[0]['limit'] + 1)
    for key, trace in [('reference', a), ('candidate', b)]:
        first, last = timing[key]
        duration = (last['elapsed_ns'] - first['elapsed_ns']) / 1e9
        presents = last['present'] - first['present']
        report[f'{key}_observed_fps'] = presents / duration if duration else None
        report[f'{key}_combat_steps_per_second'] = trace[0]['limit'] / duration if duration else None
    return report


def matrix_conclusion(cases):
    controls = {c['name']: c for c in cases}
    if any(c.get('status') in ('invalid', 'reference_unavailable') for c in cases):
        return 'incomplete_evidence'
    if 'baseline-b' not in controls:
        return 'repeat_control_not_run'
    if controls['baseline-b'].get('status') == 'incompatible':
        return 'different_match_setup_or_inputs'
    if not controls['baseline-b'].get('identical'):
        return 'not_repeatable_at_fixed_settings; cross_setting_cause_not_established'
    if any(c.get('status') == 'incompatible' for c in cases):
        return 'different_match_setup_or_inputs'
    if not all(c.get('identical') for c in cases):
        return 'repeat_control_matched_but_cross_setting_state_diverged'
    return 'all_observed_fields_matched; not_a_complete_state_determinism_proof'


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def matrix(args):
    binary, game, output = args.binary.resolve(strict=True), args.game.resolve(strict=True), args.output.resolve()
    if output.exists():
        raise ValueError('matrix output directory must be new; previous evidence is never overwritten')
    if not 120 <= args.ticks <= 1000000:
        raise ValueError('matrix requires at least 120 combat ticks')
    if args.scenario == 'walls' and args.ticks < 2000:
        raise ValueError('wall scenario requires at least 2000 ticks')
    if args.scenario == 'edges' and args.ticks < 1000:
        raise ValueError('edge scenario requires at least 1000 ticks')
    selected = args.cases.split(',') if args.cases else [c[0] for c in CASES]
    cases = [c for c in CASES if c[0] in selected]
    if len(cases) != len(selected) or 'baseline-a' not in selected:
        raise ValueError('select known cases and include baseline-a')
    output.mkdir(parents=True)
    tape = output / 'inputs.txt'
    scenario(tape, args.ticks, args.scenario)
    manifest = {'binary': str(binary), 'binary_sha256': digest(binary), 'game': str(game),
                'input_sha256': digest(tape), 'ticks': args.ticks, 'cases': [],
                'seed': args.seed, 'scenario': args.scenario, 'render_thread': args.render_thread, 'note': 'Controlled initialization/RNG when seed supplied. Observed fields only; no rollback or LAN.'}
    if args.save_fixture:
        fixture = args.save_fixture.resolve(strict=True)
        manifest['save_fixture'] = {str(p.relative_to(fixture)): digest(p) for p in sorted(fixture.rglob('*')) if p.is_file()}
    for name, fps, aspect, resolution, upscaler, aa in cases:
        run = output / name
        run.mkdir()
        if args.save_fixture:
            shutil.copytree(fixture, run / 'save')
        config = run / 'sf2recomp.ini'
        config.write_text(f'''[general]
language = en-US
[network]
enabled = false
[gameplay]
unlock_all_sparring_characters = true
show_intro = false
ai_enabled = false
[video]
fullscreen = false
aspect_ratio = {aspect}
target_fps = {fps}
internal_resolution = {resolution}
upscaler = {upscaler}
antialiasing = {aa}
[input]
player_setup = solo
[audio]
volume = 0
[storage]
save_directory = save
''')
        env = {k: v for k, v in os.environ.items() if not k.startswith(('LARECOMP_', 'SF2_REPLAY_', 'SF2_LAN_'))}
        env.update(SF2_CONFIG=str(config), LARECOMP_DATA_ROOT=str(run), LARECOMP_DATA_HOME=str(run),
                   LARECOMP_HIDDEN='1', LARECOMP_NO_USER_INPUT='1', LARECOMP_HOST_INPUT='',
                   LARECOMP_RENDER_THREAD=str(args.render_thread), LARECOMP_WINDOW='960x540', SF2_REPLAY_TRACE=str(run / 'trace.jsonl'),
                   SF2_REPLAY_INPUT=str(tape), SF2_REPLAY_TICKS=str(args.ticks), SF2_REPLAY_MATCH='17,18,13',
                   LARECOMP_INPUT=','.join(f'{round(t * fps / 60)}:{key}:{max(2, round(8 * fps / 60))}' for t, key in MENU))
        if args.seed is not None:
            env['SF2_REPLAY_SEED'] = str(args.seed)
        if args.icd:
            env['VK_DRIVER_FILES'] = str(args.icd.resolve(strict=True))
        if 'nvidia' in env.get('VK_DRIVER_FILES', ''):
            env['__NV_PRIME_RENDER_OFFLOAD'] = '1'
        (run / 'environment.json').write_text(json.dumps({k: v for k, v in env.items()
            if k.startswith(('SF2_', 'LARECOMP_', 'VK_', 'SDL_'))}, indent=2) + '\n')
        print(f'Running {name}: {fps} FPS, {aspect}, {resolution}, {upscaler}, {aa}', flush=True)
        started = time.monotonic()
        with (run / 'runtime.log').open('w') as log:
            try:
                result = subprocess.run([str(binary), str(game)], cwd=run, env=env, stdout=log,
                                        stderr=subprocess.STDOUT, timeout=args.timeout)
                entry = {'name': name, 'exit_code': result.returncode}
            except subprocess.TimeoutExpired:
                entry = {'name': name, 'status': 'timeout'}
        entry['seconds'] = round(time.monotonic() - started, 2)
        try:
            if entry.get('exit_code') != 0:
                raise ValueError(f'process did not finish successfully: {entry}')
            read_trace(run / 'trace.jsonl', False)
            entry['trace_complete'] = True
            try:
                read_trace(output / 'baseline-a/trace.jsonl', False)
            except (OSError, ValueError, KeyError) as error:
                entry.update(status='reference_unavailable', error=str(error))
            else:
                entry.update(compare(output / 'baseline-a/trace.jsonl', run / 'trace.jsonl'))
        except (OSError, ValueError, KeyError) as error:
            entry.update(status='invalid', error=str(error))
        manifest['cases'].append(entry)
        manifest['conclusion'] = matrix_conclusion(manifest['cases'])
        (output / 'report.json').write_text(json.dumps(manifest, indent=2) + '\n')
        print(f'  {entry["status"]}' + (f' at tick {entry["first_tick"]}' if 'first_tick' in entry else ''), flush=True)
    # A matrix is successful only if evidence exists AND all observations match.
    return 0 if 'baseline-b' in selected and all(c.get('identical') for c in manifest['cases']) else 1


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    create = sub.add_parser('scenario')
    create.add_argument('output', type=Path)
    create.add_argument('--ticks', type=int, default=600)
    create.add_argument('--scenario', choices=('exchange', 'walls', 'edges'), default='exchange')
    check = sub.add_parser('compare')
    check.add_argument('reference', type=Path)
    check.add_argument('candidate', type=Path)
    run = sub.add_parser('matrix')
    run.add_argument('--binary', type=Path, default=ROOT / 'build/sf2')
    run.add_argument('--game', type=Path, default=ROOT / 'game/extracted/update')
    run.add_argument('--output', type=Path, required=True)
    run.add_argument('--ticks', type=int, default=600)
    run.add_argument('--timeout', type=int, default=180)
    run.add_argument('--save-fixture', type=Path)
    run.add_argument('--icd', type=Path)
    run.add_argument('--cases')
    run.add_argument('--scenario', choices=('exchange', 'walls', 'edges'), default='exchange')
    run.add_argument('--render-thread', type=int, choices=(0, 1), default=1, help='existing renderer execution mode; recorded per batch')
    run.add_argument('--seed', type=int, help='enable controlled initialization and scoped RNG with this uint32 seed')
    args = parser.parse_args(argv)
    try:
        if args.command == 'matrix' and args.seed is not None and not 0 <= args.seed <= 0xffffffff:
            raise ValueError('seed must be an unsigned 32-bit integer')
        if args.command == 'scenario':
            if not 120 <= args.ticks <= 1000000:
                raise ValueError('scenario requires 120..1000000 ticks')
            scenario(args.output, args.ticks, args.scenario)
            return 0
        if args.command == 'compare':
            report = compare(args.reference, args.candidate)
            print(json.dumps(report, indent=2))
            return 0 if report['identical'] else 1
        return matrix(args)
    except (OSError, ValueError, KeyError) as error:
        print(f'Combat replay failed: {error}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(main())
