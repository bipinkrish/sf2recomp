#!/usr/bin/env python3
"""Create logical combat tapes, compare traces, and run isolated graphics matrices."""
from _sf2_tool_constants import COMBAT_REPLAY as K
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
MENU = [(K.N_600_284B, K.S_STICK_DOWN_4A8C), (K.N_690_F610, K.S_STICK_DOWN_4A8C), (K.N_780_0E78, K.S_A_341F), (K.N_1000_4051, K.S_A_341F),
        (K.N_1200_1519, K.S_STICK_RIGHT_FC4E), (K.N_1400_55FD, K.S_A_341F), (K.N_1600_B458, K.S_STICK_RIGHT_FC4E),
        (K.N_1800_E49E, K.S_A_341F), (K.N_2000_81A8, K.S_A_341F), (K.N_2200_2F83, K.S_A_341F)]
CASES = [(K.S_BASELINE_A_F41A, K.N_60_39FA, K.S_ORIGINAL_DE33, K.S_1X_9CA2, K.S_BILINEAR_9362, K.S_OFF_7E45),
         (K.S_BASELINE_B_70B0, K.N_60_39FA, K.S_ORIGINAL_DE33, K.S_1X_9CA2, K.S_BILINEAR_9362, K.S_OFF_7E45),
         (K.S_ASPECT_16_10_A2EC, K.N_60_39FA, K.S_16_10_8E03, K.S_1X_9CA2, K.S_BILINEAR_9362, K.S_OFF_7E45),
         (K.S_WIDE_21_9_460B, K.N_60_39FA, K.S_21_9_A3B0, K.S_1X_9CA2, K.S_BILINEAR_9362, K.S_OFF_7E45),
         (K.S_WIDE_32_9_23CA, K.N_60_39FA, K.S_32_9_CD32, K.S_1X_9CA2, K.S_BILINEAR_9362, K.S_OFF_7E45),
         (K.S_FPS_30_BA67, K.N_30_624B, K.S_ORIGINAL_DE33, K.S_1X_9CA2, K.S_BILINEAR_9362, K.S_OFF_7E45),
         (K.S_FPS_120_7EDC, K.N_120_2ABA, K.S_ORIGINAL_DE33, K.S_1X_9CA2, K.S_BILINEAR_9362, K.S_OFF_7E45),
         (K.S_FPS_144_3835, K.N_144_5EC1, K.S_ORIGINAL_DE33, K.S_1X_9CA2, K.S_BILINEAR_9362, K.S_OFF_7E45),
         (K.S_SCALED_BILINEAR_266F, K.N_60_39FA, K.S_ORIGINAL_DE33, K.S_0_50X_E651, K.S_BILINEAR_9362, K.S_OFF_7E45),
         (K.S_SCALED_FSR_SMAA_CB3C, K.N_60_39FA, K.S_ORIGINAL_DE33, K.S_0_50X_E651, K.S_FSR1_66C6, K.S_SMAA_424E)]
ANIMATION_FIELDS = [K.S_SIGN_1C35, K.S_CURRENT_NODE_ID_8066, K.S_FIRST_FRAME_FB1A, K.S_FINISH_FRAME_BDA6,
                    K.S_TRANSITION_FIRST_FRAME_2A9C, K.S_ANIMATION_FRAME_5B4D, K.S_LOOP_FRAME_9866, K.S_INTERFRAME_0130,
                    K.S_INDEX_2314, K.S_OLD_FRAME_6D74, K.S_WHERE_POSITION_BITS_9D8E, K.S_START_POSITION_BITS_A31B,
                    K.S_MODEL_START_POSITION_BITS_8EC8, K.S_MODEL_FRAME_7D60, K.S_WALL_SHIFT_BITS_FAF0,
                    K.S_SLOW_FACTOR_BITS_0F6F, K.S_FRAME_ALIGN_C3E9, K.S_LEFT_WALL_MARGIN_72F8, K.S_RIGHT_WALL_MARGIN_DEED,
                    K.S_WALL_LEFT_BITS_2B63, K.S_WALL_RIGHT_BITS_E220, K.S_BUFFER_INDEX_02C1, K.S_SLOW_BUFFER_7215]
MODEL_FIELDS = [K.S_INDEX_2314, K.S_CURRENT_SIGN_3D82, K.S_COMBO_TIME_9FF6, K.S_ROUND_STAGE_9547, K.S_FIGHT_FRAME_INDEX_8D94,
                K.S_STRIKE_COUNT_2470, K.S_COMBO_TYPE_D916, K.S_COMBO_COUNTER_0D0F, K.S_CONTROL_BEFORE_FIGHT_AA3A,
                K.S_EVENT_TYPE_3A6B, K.S_ANIMATION_TYPE_1EA4, K.S_CURRENT_STATE_2D55, K.S_SHOCK_COUNT_4A6C, K.S_AI_DELAY_E8BA,
                K.S_FALLING_TIME_707E, K.S_FIXED_SIGN_0F8D, K.S_PAIN_BITS_16D6, K.S_MAGIC_COUNT_D777]


def scenario(path, ticks, kind=K.S_EXCHANGE_2655):
    """Both fighters move toward one another and use alternating attacks."""
    events = []
    def hold(tick, player, code, duration):
        if tick + duration < ticks:
            events.extend([(tick, player, code, 1), (tick + duration, player, code, 0)])
    if kind == K.S_EDGES_28F8:
        if ticks < K.N_1000_4051:
            raise ValueError(K.S_EDGE_SCENARIO_REQUIRES_AT_LE_F49F)
        hold(0, 1, K.N_7_7902, K.N_800_1A1C)
        hold(0, K.N_2_D473, K.N_3_4E07, K.N_800_1A1C)
        for tick in range(K.N_810_5D85, ticks - K.N_15_E629, K.N_25_B7A5):
            hold(tick, 1, K.N_9_1958 if tick % K.N_2_D473 else K.N_10_4A44, K.N_4_4B22)
            hold(tick + K.N_8_2C62, K.N_2_D473, K.N_10_4A44 if tick % K.N_2_D473 else K.N_9_1958, K.N_4_4B22)
    elif kind == K.S_WALLS_FC31:
        if ticks < K.N_2000_81A8:
            raise ValueError(K.S_WALL_SCENARIO_REQUIRES_AT_LE_C40D)
        for player in (1, K.N_2_D473):
            hold(0, player, K.N_7_7902, K.N_500_0604)       # pursue to the left boundary
            hold(K.N_700_99EE, player, K.N_3_4E07, K.N_1000_4051)    # then traverse to the right boundary
        for start, end in ((K.N_510_5E5C, K.N_685_BB9B), (K.N_1710_1AA3, ticks - K.N_15_E629)):
            for tick in range(start, end, K.N_25_B7A5):
                hold(tick, 1, K.N_9_1958 if tick % K.N_2_D473 else K.N_10_4A44, K.N_4_4B22)
                hold(tick + K.N_8_2C62, K.N_2_D473, K.N_10_4A44 if tick % K.N_2_D473 else K.N_9_1958, K.N_4_4B22)
    elif kind == K.S_EXCHANGE_2655:
        hold(0, 1, K.N_3_4E07, K.N_75_F369)
        hold(0, K.N_2_D473, K.N_7_7902, K.N_75_F369)
        for tick in range(K.N_85_B494, ticks - K.N_15_E629, K.N_35_9F14):
            hold(tick, 1, K.N_9_1958 if tick % K.N_2_D473 else K.N_10_4A44, K.N_4_4B22)
            hold(tick + K.N_10_4A44, K.N_2_D473, K.N_10_4A44 if tick % K.N_2_D473 else K.N_9_1958, K.N_4_4B22)
    else:
        raise ValueError(K.S_UNKNOWN_REPLAY_SCENARIO_599F)
    with path.open(K.S_X_4755) as stream:
        stream.write(K.S_SF2_REPLAY_1_N_95B1)
        stream.write(K.S_TICK_PLAYER_1_2_QUADRANT_0_1_1CA7)
        for event in sorted(events, key=lambda e: e[0]):
            stream.write(K.S_EMPTY_730D.join(map(str, event)) + K.S_N_AD65)


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
            kind = row.get(K.S_KIND_395E)
            if header is None:
                if kind != K.S_HEADER_BCBD or row.get(K.S_SCHEMA_9B05) != 1 or not isinstance(row.get(K.S_LIMIT_2198), int) or row[K.S_LIMIT_2198] < 1:
                    raise ValueError(f'{path}: missing/unsupported trace header')
                header = row
            elif kind == K.S_SETUP_5D11 and setup is None:
                if not row.get(K.S_LOCATION_FEBF) or len(row.get(K.S_PLAYERS_A8BB, [])) != K.N_2_D473 or not all(row[K.S_PLAYERS_A8BB]):
                    raise ValueError(f'{path}: invalid match setup')
                setup = row
            elif kind == K.S_STATE_27BB:
                if row.get(K.S_TICK_1C33) != next_tick or not isinstance(row.get(K.S_STATE_27BB), dict) or any(
                        not isinstance(row.get(K.S_DIAGNOSTIC_31C3, {}).get(k), int)
                        for k in (K.S_PRESENT_3B12, K.S_ELAPSED_NS_C548, K.S_FIGHT_FRAME_1C8F)):
                    raise ValueError(f'{path}: missing/duplicate/out-of-order combat state or diagnostics')
                if header.get(K.S_COVERAGE_69B2) == K.S_OBSERVED_COMBAT_FIELDS_V3_27A3:
                    framing = row[K.S_STATE_27BB].get(K.S_ARENA_FRAMING_C9B6)
                    if (not isinstance(framing, list) or len(framing) != K.N_6_E7F6 or
                            any(type(v) is not int or not 0 <= v <= K.N_4294967295_F807 for v in framing)):
                        raise ValueError(f'{path}: missing/invalid combat framing fields for v3 coverage')
                last_state = row[K.S_STATE_27BB]
                next_tick += 1
                if retain_states:
                    states.append(row)
            elif kind == K.S_INPUT_AFBC:
                event = {k: row[k] for k in (K.S_TICK_1C33, K.S_PLAYER_BB19, K.S_CODE_2188, K.S_DOWN_48CE)}
                if (not 0 <= event[K.S_TICK_1C33] < header[K.S_LIMIT_2198] or event[K.S_PLAYER_BB19] not in (1, K.N_2_D473) or
                        not 0 <= event[K.S_CODE_2188] <= K.N_14_8527 or event[K.S_DOWN_48CE] not in (0, 1) or
                        (inputs and event[K.S_TICK_1C33] < inputs[-1][K.S_TICK_1C33])):
                    raise ValueError(f'{path}: invalid/out-of-order logical input')
                inputs.append(event)
            elif kind == K.S_COMPLETE_CB34:
                complete = row
            else:
                raise ValueError(f'{path}: duplicate markers or unknown trace records')
    if not header or complete is None:
        raise ValueError(f'{path}: incomplete trace; no successful completion marker')
    if header.get(K.S_END_ON_MATCH_7D0F) is True and complete.get(K.S_REASON_FFA7) == K.S_MATCH_FINISHED_9EC8:
        actual = complete.get(K.S_TICKS_7CBF)
        flags = (last_state or {}).get(K.S_FLAGS_13A4, [])
        if (type(actual) is not int or not 0 < actual <= header[K.S_LIMIT_2198] or
                not isinstance(flags, list) or len(flags) < K.N_2_D473 or flags[1] != 1):
            raise ValueError(f'{path}: early LAN completion requires native game-over state')
        header = {**header, K.S_PLANNED_LIMIT_43F3: header[K.S_LIMIT_2198], K.S_LIMIT_2198: actual}
    if not setup or next_tick != header[K.S_LIMIT_2198] + 1 or complete.get(K.S_TICKS_7CBF) != header[K.S_LIMIT_2198]:
        raise ValueError(f'{path}: missing setup, states or invalid completion count')
    if complete.get(K.S_EVENTS_APPLIED_7C03) != len(inputs):
        raise ValueError(f'{path}: applied input count does not match trace')
    return header, setup, inputs, states


def state_records(path):
    # Called only after read_trace has validated the entire file. Keep just the
    # current pair of large node snapshots resident during exact comparison.
    with Path(path).open() as stream:
        for line in stream:
            if line.strip():
                row = json.loads(line)
                if row.get(K.S_KIND_395E) == K.S_STATE_27BB:
                    yield row


def differences(a, b, path=K.S_STATE_27BB, cap=K.N_12_6B51):
    result = []
    def visit(x, y, key):
        if len(result) >= cap:
            return
        if type(x) is not type(y):
            result.append({K.S_FIELD_830B: key, K.S_REFERENCE_C560: x, K.S_CANDIDATE_CB3D: y})
        elif isinstance(x, dict):
            for k in sorted(x.keys() | y.keys()):
                if k not in x or k not in y:
                    result.append({K.S_FIELD_830B: f'{key}.{k}', K.S_REFERENCE_C560: x.get(k), K.S_CANDIDATE_CB3D: y.get(k)})
                else:
                    visit(x[k], y[k], f'{key}.{k}')
                if len(result) >= cap:
                    return
        elif isinstance(x, list):
            if len(x) != len(y):
                result.append({K.S_FIELD_830B: f'{key}.length', K.S_REFERENCE_C560: len(x), K.S_CANDIDATE_CB3D: len(y)})
            for i, (left, right) in enumerate(zip(x, y)):
                visit(left, right, f'{key}[{i}]')
        elif x != y:
            result.append({K.S_FIELD_830B: key, K.S_REFERENCE_C560: x, K.S_CANDIDATE_CB3D: y})
    visit(a, b, path)
    for difference in result:
        raw = difference[K.S_FIELD_830B]
        field = raw
        for group, names in [(K.S_ANIMATION_2B37, ANIMATION_FIELDS), (K.S_MODEL_8BD2, MODEL_FIELDS),
                             (K.S_ROUND_BD7D, [K.S_TIME_2BD0, K.S_TIME_TOTAL_B879, K.S_NUMBER_BC01, K.S_TOTAL_C68E, K.S_PROCESSING_9861]),
                             (K.S_CONTROLLER_63B0, [K.S_TIME_2BD0, K.S_COMBO_INTERVAL_4D42]),
                             (K.S_ARENA_FRAMING_C9B6, [K.S_WIDTH_BITS_AE27, K.S_LOCATION_SCALE_BITS_8E07, K.S_SCALE_BITS_58AD, K.S_MIN_SCALE_BITS_6C91, K.S_CENTER_BITS_A3C8, K.S_X_BITS_E225])]:
            field = re.sub(K.S_EMPTY_0A7D + group + K.S_D_760E,
                           lambda m: K.S_EMPTY_277A + group + K.S_EMPTY_277A + names[int(m[1])] if int(m[1]) < len(names) else m[0], field)
        field = re.sub(K.S_CURRENT_PREVIOUS_SHIFT_VELOC_CF47,
                       lambda m: K.S_EMPTY_277A + m[1] + K.S_EMPTY_277A + K.S_XYZ_775C[int(m[K.N_2_D473])] + K.S_BITS_423E, field)
        if field != raw:
            difference.update(field=field, raw_field=raw)
    return result[:cap]


def compare(reference, candidate):
    a, b = read_trace(reference, False), read_trace(candidate, False)
    report = {K.S_REFERENCE_C560: str(reference), K.S_CANDIDATE_CB3D: str(candidate), K.S_IDENTICAL_4197: False,
              K.S_COVERAGE_69B2: a[0][K.S_COVERAGE_69B2], K.S_RNG_POLICY_0C29: a[0][K.S_RNG_POLICY_0C29]}
    for key in (K.S_GAME_CF8E, K.S_LIMIT_2198, K.S_COVERAGE_69B2, K.S_RNG_POLICY_0C29, K.S_SEED_795A, K.S_INITIALIZATION_EC90, K.S_COMBAT_GEOMETRY_A4FB, K.S_AI_ENABLED_6F2A):
        if a[0].get(key) != b[0].get(key):
            report.update(status=K.S_INCOMPATIBLE_9977, differences=differences(a[0].get(key), b[0].get(key), f'header.{key}'))
            return report
    if a[1] != b[1] or a[K.N_2_D473] != b[K.N_2_D473]:
        report.update(status=K.S_INCOMPATIBLE_9977, differences=differences(a[1], b[1], K.S_SETUP_5D11) or differences(a[K.N_2_D473], b[K.N_2_D473], K.S_INPUTS_112B))
        return report
    mismatch_ticks = 0
    timing = {}
    for left, right in zip(state_records(reference), state_records(candidate)):
        for key, row in ((K.S_REFERENCE_C560, left), (K.S_CANDIDATE_CB3D, right)):
            if key not in timing:
                timing[key] = [row[K.S_DIAGNOSTIC_31C3], row[K.S_DIAGNOSTIC_31C3]]
            timing[key][1] = row[K.S_DIAGNOSTIC_31C3]
        # This logical game counter can affect later simulation. It is kept
        # beside presentation diagnostics in the trace but MUST be compared.
        left_state = {**left[K.S_STATE_27BB], K.S_FIGHT_FRAME_1C8F: left[K.S_DIAGNOSTIC_31C3][K.S_FIGHT_FRAME_1C8F]}
        right_state = {**right[K.S_STATE_27BB], K.S_FIGHT_FRAME_1C8F: right[K.S_DIAGNOSTIC_31C3][K.S_FIGHT_FRAME_1C8F]}
        if left_state != right_state:
            mismatch_ticks += 1
            if K.S_FIRST_TICK_B874 not in report:
                report.update(first_tick=left[K.S_TICK_1C33], differences=differences(left_state, right_state))
    report.update(status=K.S_DIVERGED_F6BE if mismatch_ticks else K.S_MATCHED_A40A, identical=not mismatch_ticks,
                  mismatched_states=mismatch_ticks, compared_states=a[0][K.S_LIMIT_2198] + 1)
    for key, trace in [(K.S_REFERENCE_C560, a), (K.S_CANDIDATE_CB3D, b)]:
        first, last = timing[key]
        duration = (last[K.S_ELAPSED_NS_C548] - first[K.S_ELAPSED_NS_C548]) / K.N_1000000000_0_E3E9
        presents = last[K.S_PRESENT_3B12] - first[K.S_PRESENT_3B12]
        report[f'{key}_observed_fps'] = presents / duration if duration else None
        report[f'{key}_combat_steps_per_second'] = trace[0][K.S_LIMIT_2198] / duration if duration else None
    return report


def matrix_conclusion(cases):
    controls = {c[K.S_NAME_8423]: c for c in cases}
    if any(c.get(K.S_STATUS_DA66) in (K.S_INVALID_4F52, K.S_REFERENCE_UNAVAILABLE_AB5E) for c in cases):
        return K.S_INCOMPLETE_EVIDENCE_2685
    if K.S_BASELINE_B_70B0 not in controls:
        return K.S_REPEAT_CONTROL_NOT_RUN_DDE8
    if controls[K.S_BASELINE_B_70B0].get(K.S_STATUS_DA66) == K.S_INCOMPATIBLE_9977:
        return K.S_DIFFERENT_MATCH_SETUP_OR_INP_7531
    if not controls[K.S_BASELINE_B_70B0].get(K.S_IDENTICAL_4197):
        return K.S_NOT_REPEATABLE_AT_FIXED_SETT_BF92
    if any(c.get(K.S_STATUS_DA66) == K.S_INCOMPATIBLE_9977 for c in cases):
        return K.S_DIFFERENT_MATCH_SETUP_OR_INP_7531
    if not all(c.get(K.S_IDENTICAL_4197) for c in cases):
        return K.S_REPEAT_CONTROL_MATCHED_BUT_C_377C
    return K.S_ALL_OBSERVED_FIELDS_MATCHED__66A7


def digest(path):
    with path.open(K.S_RB_81FE) as stream:
        return hashlib.file_digest(stream, K.S_SHA256_9465).hexdigest()


def matrix(args):
    binary, game, output = args.binary.resolve(strict=True), args.game.resolve(strict=True), args.output.resolve()
    if output.exists():
        raise ValueError(K.S_MATRIX_OUTPUT_DIRECTORY_MUST_8451)
    if not K.N_120_2ABA <= args.ticks <= K.N_1000000_6CCE:
        raise ValueError(K.S_MATRIX_REQUIRES_AT_LEAST_120_369A)
    if args.scenario == K.S_WALLS_FC31 and args.ticks < K.N_2000_81A8:
        raise ValueError(K.S_WALL_SCENARIO_REQUIRES_AT_LE_C40D)
    if args.scenario == K.S_EDGES_28F8 and args.ticks < K.N_1000_4051:
        raise ValueError(K.S_EDGE_SCENARIO_REQUIRES_AT_LE_F49F)
    selected = args.cases.split(K.S_EMPTY_EE1B) if args.cases else [c[0] for c in CASES]
    cases = [c for c in CASES if c[0] in selected]
    if len(cases) != len(selected) or K.S_BASELINE_A_F41A not in selected:
        raise ValueError(K.S_SELECT_KNOWN_CASES_AND_INCLU_FF56)
    output.mkdir(parents=True)
    tape = output / K.S_INPUTS_TXT_B73E
    scenario(tape, args.ticks, args.scenario)
    manifest = {K.S_BINARY_1967: str(binary), K.S_BINARY_SHA256_2D5F: digest(binary), K.S_GAME_CF8E: str(game),
                K.S_INPUT_SHA256_65C2: digest(tape), K.S_TICKS_7CBF: args.ticks, K.S_CASES_4BA2: [],
                K.S_SEED_795A: args.seed, K.S_SCENARIO_036D: args.scenario, K.S_RENDER_THREAD_4097: args.render_thread, K.S_NOTE_E956: K.S_CONTROLLED_INITIALIZATION_RN_439D}
    if args.save_fixture:
        fixture = args.save_fixture.resolve(strict=True)
        manifest[K.S_SAVE_FIXTURE_0130] = {str(p.relative_to(fixture)): digest(p) for p in sorted(fixture.rglob(K.S_EMPTY_15A7)) if p.is_file()}
    for name, fps, aspect, resolution, upscaler, aa in cases:
        run = output / name
        run.mkdir()
        if args.save_fixture:
            shutil.copytree(fixture, run / K.S_SAVE_FBDD)
        config = run / K.S_SF2RECOMP_INI_8714
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
        env = {k: v for k, v in os.environ.items() if not k.startswith((K.S_LARECOMP_B959, K.S_SF2_REPLAY_DA3E, K.S_SF2_LAN_17C4))}
        env.update(SF2_CONFIG=str(config), LARECOMP_DATA_ROOT=str(run), LARECOMP_DATA_HOME=str(run),
                   LARECOMP_HIDDEN=K.S_1_9A76, LARECOMP_NO_USER_INPUT=K.S_1_9A76, LARECOMP_HOST_INPUT=K.S_EMPTY_6F49,
                   LARECOMP_RENDER_THREAD=str(args.render_thread), LARECOMP_WINDOW=K.S_960X540_9D0F, SF2_REPLAY_TRACE=str(run / K.S_TRACE_JSONL_FB9A),
                   SF2_REPLAY_INPUT=str(tape), SF2_REPLAY_TICKS=str(args.ticks), SF2_REPLAY_MATCH=K.S_17_18_13_2495,
                   LARECOMP_INPUT=K.S_EMPTY_EE1B.join(f'{round(t * fps / K.N_60_39FA)}:{key}:{max(K.N_2_D473, round(K.N_8_2C62 * fps / K.N_60_39FA))}' for t, key in MENU))
        if args.seed is not None:
            env[K.S_SF2_REPLAY_SEED_6AB9] = str(args.seed)
        if args.icd:
            env[K.S_VK_DRIVER_FILES_B081] = str(args.icd.resolve(strict=True))
        if K.S_NVIDIA_7F25 in env.get(K.S_VK_DRIVER_FILES_B081, K.S_EMPTY_6F49):
            env[K.S_NV_PRIME_RENDER_OFFLOAD_E67F] = K.S_1_9A76
        (run / K.S_ENVIRONMENT_JSON_64EE).write_text(json.dumps({k: v for k, v in env.items()
            if k.startswith((K.S_SF2_8749, K.S_LARECOMP_B959, K.S_VK_6B79, K.S_SDL_8FFE))}, indent=K.N_2_D473) + K.S_N_AD65)
        print(f'Running {name}: {fps} FPS, {aspect}, {resolution}, {upscaler}, {aa}', flush=True)
        started = time.monotonic()
        with (run / K.S_RUNTIME_LOG_DDBF).open(K.S_W_780F) as log:
            try:
                result = subprocess.run([str(binary), str(game)], cwd=run, env=env, stdout=log,
                                        stderr=subprocess.STDOUT, timeout=args.timeout)
                entry = {K.S_NAME_8423: name, K.S_EXIT_CODE_8421: result.returncode}
            except subprocess.TimeoutExpired:
                entry = {K.S_NAME_8423: name, K.S_STATUS_DA66: K.S_TIMEOUT_B6D3}
        entry[K.S_SECONDS_C79C] = round(time.monotonic() - started, K.N_2_D473)
        try:
            if entry.get(K.S_EXIT_CODE_8421) != 0:
                raise ValueError(f'process did not finish successfully: {entry}')
            read_trace(run / K.S_TRACE_JSONL_FB9A, False)
            entry[K.S_TRACE_COMPLETE_044F] = True
            try:
                read_trace(output / K.S_BASELINE_A_TRACE_JSONL_5AC7, False)
            except (OSError, ValueError, KeyError) as error:
                entry.update(status=K.S_REFERENCE_UNAVAILABLE_AB5E, error=str(error))
            else:
                entry.update(compare(output / K.S_BASELINE_A_TRACE_JSONL_5AC7, run / K.S_TRACE_JSONL_FB9A))
        except (OSError, ValueError, KeyError) as error:
            entry.update(status=K.S_INVALID_4F52, error=str(error))
        manifest[K.S_CASES_4BA2].append(entry)
        manifest[K.S_CONCLUSION_3479] = matrix_conclusion(manifest[K.S_CASES_4BA2])
        (output / K.S_REPORT_JSON_2AE0).write_text(json.dumps(manifest, indent=K.N_2_D473) + K.S_N_AD65)
        print(f'  {entry[K.S_STATUS_DA66]}' + (f' at tick {entry[K.S_FIRST_TICK_B874]}' if K.S_FIRST_TICK_B874 in entry else K.S_EMPTY_6F49), flush=True)
    # A matrix is successful only if evidence exists AND all observations match.
    return 0 if K.S_BASELINE_B_70B0 in selected and all(c.get(K.S_IDENTICAL_4197) for c in manifest[K.S_CASES_4BA2]) else 1


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest=K.S_COMMAND_CFC6, required=True)
    create = sub.add_parser(K.S_SCENARIO_036D)
    create.add_argument(K.S_OUTPUT_5648, type=Path)
    create.add_argument(K.S_TICKS_0A0C, type=int, default=K.N_600_284B)
    create.add_argument(K.S_SCENARIO_1852, choices=(K.S_EXCHANGE_2655, K.S_WALLS_FC31, K.S_EDGES_28F8), default=K.S_EXCHANGE_2655)
    check = sub.add_parser(K.S_COMPARE_C4A5)
    check.add_argument(K.S_REFERENCE_C560, type=Path)
    check.add_argument(K.S_CANDIDATE_CB3D, type=Path)
    run = sub.add_parser(K.S_MATRIX_F80D)
    run.add_argument(K.S_BINARY_1375, type=Path, default=ROOT / K.S_BUILD_SF2_ECEA)
    run.add_argument(K.S_GAME_1AB6, type=Path, default=ROOT / K.S_GAME_EXTRACTED_UPDATE_E969)
    run.add_argument(K.S_OUTPUT_3033, type=Path, required=True)
    run.add_argument(K.S_TICKS_0A0C, type=int, default=K.N_600_284B)
    run.add_argument(K.S_TIMEOUT_66B3, type=int, default=K.N_180_7B69)
    run.add_argument(K.S_SAVE_FIXTURE_3072, type=Path)
    run.add_argument(K.S_ICD_3183, type=Path)
    run.add_argument(K.S_CASES_3BFB)
    run.add_argument(K.S_SCENARIO_1852, choices=(K.S_EXCHANGE_2655, K.S_WALLS_FC31, K.S_EDGES_28F8), default=K.S_EXCHANGE_2655)
    run.add_argument(K.S_RENDER_THREAD_F8EC, type=int, choices=(0, 1), default=1, help=K.S_EXISTING_RENDERER_EXECUTION__4D88)
    run.add_argument(K.S_SEED_B0D2, type=int, help=K.S_ENABLE_CONTROLLED_INITIALIZA_B88C)
    args = parser.parse_args(argv)
    try:
        if args.command == K.S_MATRIX_F80D and args.seed is not None and not 0 <= args.seed <= K.N_4294967295_F807:
            raise ValueError(K.S_SEED_MUST_BE_AN_UNSIGNED_32__3EEC)
        if args.command == K.S_SCENARIO_036D:
            if not K.N_120_2ABA <= args.ticks <= K.N_1000000_6CCE:
                raise ValueError(K.S_SCENARIO_REQUIRES_120_100000_5EED)
            scenario(args.output, args.ticks, args.scenario)
            return 0
        if args.command == K.S_COMPARE_C4A5:
            report = compare(args.reference, args.candidate)
            print(json.dumps(report, indent=K.N_2_D473))
            return 0 if report[K.S_IDENTICAL_4197] else 1
        return matrix(args)
    except (OSError, ValueError, KeyError) as error:
        print(f'Combat replay failed: {error}', file=sys.stderr)
        return K.N_2_D473


if __name__ == K.S_MAIN_ACB5:
    sys.exit(main())
