#!/usr/bin/env python3
"""Run two real game processes through LAN and compare their combat traces."""
from _sf2_tool_constants import TEST_LAN_SPARRING as K
import argparse
import hashlib
import json
import os
from pathlib import Path
import socket
import subprocess
import time
import lan_sparring
import combat_replay

ROOT=Path(__file__).resolve().parents[1]
def digest(p):
    with p.open(K.S_RB_81FE) as stream:return hashlib.file_digest(stream,K.S_SHA256_9465).hexdigest()
def pair(output,save,host_flags,join_flags,ticks=K.N_600_284B,scenario=K.S_EXCHANGE_2655,live=False,timeout=K.N_200_27BA,join_menu_offset=0,no_trace=False,icd=None):
    output.mkdir(parents=True,exist_ok=False)
    tape=output/K.S_INPUTS_TXT_B73E;combat_replay.scenario(tape,ticks,scenario)
    with socket.socket() as s:s.bind((K.S_127_0_0_1_8D35,0));port=s.getsockname()[1]
    base=[K.S_PORT_2E58,str(port),K.S_SAVE_9BC9,str(save),K.S_TICKS_0A0C,str(ticks),K.S_INPUT_TAPE_80B5,str(tape),K.S_HIDDEN_84FD]
    if icd is not None:base+=[K.S_ICD_3183,str(icd)]
    specs=[[K.S_HOST_E293,K.S_BIND_482E,K.S_127_0_0_1_8D35,K.S_SEED_B0D2,K.S_12345_3B45,*base,*host_flags],[K.S_JOIN_0986,K.S_127_0_0_1_8D35,*base,*join_flags]]
    processes=[];logs=[];record={K.S_BINARY_SHA256_2D5F:digest(ROOT/K.S_BUILD_SF2_ECEA),K.S_TAPE_SHA256_2A90:digest(tape),K.S_TICKS_7CBF:ticks,K.S_PROFILES_2ED2:specs,K.S_CASES_4BA2:[]}
    start=time.monotonic()
    try:
        for role,spec in zip([K.S_HOST_E293,K.S_JOIN_0986],specs):
            run=output/role;run.mkdir();args=lan_sparring.parser().parse_args(spec)
            args.binary=args.binary.resolve();args.game=args.game.resolve()
            if live:args.input_tape=None
            config=lan_sparring.configure(args,run)
            env=lan_sparring.environment(args,run,config);env.update(SDL_VIDEODRIVER=K.S_X11_7FD2,SDL_JOYSTICK_LINUX_CLASSIC=K.S_1_9A76,SF2RECOMP_VK_DEVLOST_TRACE=K.S_1_9A76)
            if os.environ.get(K.S_SF2_LAN_TEST_VALIDATE_B138):env[K.S_SF2RECOMP_VK_VALIDATION_STOP_3771]=K.S_1_9A76
            if role==K.S_JOIN_0986 and join_menu_offset:
                shifted=[]
                for command in env[K.S_SF2RECOMP_INPUT_07C4].split(K.S_EMPTY_EE1B):
                    frame,button,length=command.split(K.S_EMPTY_4266);shifted.append(f'{int(frame)+round(join_menu_offset*args.fps/K.N_60_39FA)}:{button}:{length}')
                env[K.S_SF2RECOMP_INPUT_07C4]=K.S_EMPTY_EE1B.join(shifted)
            if no_trace:env.pop(K.S_SF2_REPLAY_TRACE_6C6C,None)
            if live:
                commands=[(K.N_2800_19D6,K.S_RIGHT_BF07 if role==K.S_HOST_E293 else K.S_LEFT_56A0,K.N_70_FF5A),(K.N_2900_AB4E if role==K.S_HOST_E293 else K.N_2920_B72B,K.S_X_1040,K.N_8_2C62),(K.N_2940_9992 if role==K.S_HOST_E293 else K.N_2960_B519,K.S_Y_8CC0,K.N_8_2C62)]
                env[K.S_SF2RECOMP_INPUT_07C4]+=K.S_EMPTY_EE1B+K.S_EMPTY_EE1B.join(f'{round(t*args.fps/K.N_60_39FA)}:{key}:{max(K.N_2_D473,round(length*args.fps/K.N_60_39FA))}' for t,key,length in commands)
            (run/K.S_ENVIRONMENT_JSON_64EE).write_text(json.dumps({k:v for k,v in env.items() if k.startswith((K.S_SF2_8749,K.S_SF2RECOMP_B959,K.S_VK_6B79,K.S_SDL_8FFE))},indent=K.N_2_D473)+K.S_N_AD65)
            log=(run/K.S_RUNTIME_LOG_DDBF).open(K.S_W_780F);logs.append(log)
            processes.append(subprocess.Popen([str(args.binary),str(args.game)],cwd=run,env=env,stdout=log,stderr=subprocess.STDOUT))
        for role,process in zip([K.S_HOST_E293,K.S_JOIN_0986],processes):
            try:code=process.wait(timeout=max(1,timeout-(time.monotonic()-start)))
            except subprocess.TimeoutExpired:process.kill();process.wait();code=K.S_TIMEOUT_B6D3
            record[K.S_CASES_4BA2].append({K.S_ROLE_20CA:role,K.S_EXIT_CODE_8421:code})
    finally:
        for p in processes:
            if p.poll() is None:p.kill();p.wait()
        for log in logs:log.close()
    record[K.S_SECONDS_C79C]=time.monotonic()-start
    if all(c[K.S_EXIT_CODE_8421]==0 for c in record[K.S_CASES_4BA2]):
        if not no_trace:
            try:record[K.S_COMPARISON_436F]=combat_replay.compare(output/K.S_HOST_TRACE_JSONL_9748,output/K.S_JOIN_TRACE_JSONL_A3C4)
            except (OSError,ValueError,KeyError) as error:record[K.S_COMPARISON_436F]={K.S_STATUS_DA66:K.S_INVALID_4F52,K.S_IDENTICAL_4197:False,K.S_ERROR_6401:str(error)}
        if live and not no_trace and record.get(K.S_COMPARISON_436F,{}).get(K.S_IDENTICAL_4197):
            _,_,inputs,_=combat_replay.read_trace(output/K.S_HOST_TRACE_JSONL_9748,False)
            record[K.S_LIVE_INPUT_PLAYERS_E53F]=sorted({e[K.S_PLAYER_BB19] for e in inputs})
            if record[K.S_LIVE_INPUT_PLAYERS_E53F]!=[1,K.N_2_D473]:
                record[K.S_COMPARISON_436F].update(identical=False,status=K.S_MISSING_LIVE_INPUT_8848)
        if not no_trace and record.get(K.S_COMPARISON_436F,{}).get(K.S_IDENTICAL_4197):
            checked=0
            with (output/K.S_HOST_TRACE_JSONL_9748).open() as stream:
                for line in stream:
                    row=json.loads(line)
                    if row[K.S_KIND_395E]!=K.S_STATE_27BB:continue
                    fighters=row[K.S_STATE_27BB][K.S_MODELS_66F7][:K.N_2_D473]
                    if len(fighters)!=K.N_2_D473 or any(m[K.S_USER_55D4]!=1 or m[K.S_AI_5BC8]!=0 for m in fighters):
                        record[K.S_COMPARISON_436F].update(identical=False,status=K.S_INVALID_HUMAN_FIGHTERS_1387,tick=row[K.S_TICK_1C33]);break
                    checked+=1
            record[K.S_HUMAN_FIGHTER_STATES_CHECKED_E144]=checked
    for c in record[K.S_CASES_4BA2]:
        run=output/c[K.S_ROLE_20CA];log=(run/K.S_LASRECOMP_LOG_5283).read_text() if (run/K.S_LASRECOMP_LOG_5283).exists() else (run/K.S_RUNTIME_LOG_DDBF).read_text()
        c[K.S_LAN_LOG_9F35]=[line for line in log.splitlines() if K.S_LAN_41A2 in line]
        c[K.S_GPU_TIMEOUT_59E0]=K.S_GPU_DID_NOT_FINISH_7651 in log;c[K.S_STARTUP_HANG_1484]=K.S_HANG_MAIN_GUEST_THREAD_5B28 in log
        if (run/K.S_TRACE_JSONL_FB9A).exists():c[K.S_TRACE_SHA256_D561]=digest(run/K.S_TRACE_JSONL_FB9A)
    if no_trace and all(c[K.S_EXIT_CODE_8421]==0 for c in record[K.S_CASES_4BA2]):
        assert all(any(f'completed {ticks} synchronized combat steps' in line for line in c[K.S_LAN_LOG_9F35]) for c in record[K.S_CASES_4BA2])
        record[K.S_COMPARISON_436F]={K.S_STATUS_DA66:K.S_RUNTIME_HASHES_MATCHED_C8DC,K.S_IDENTICAL_4197:True,K.S_TICKS_7CBF:ticks,K.S_TRACE_FILES_6064:False}
    (output/K.S_REPORT_JSON_2AE0).write_text(json.dumps(record,indent=K.N_2_D473)+K.S_N_AD65)
    print(output.name,[(c[K.S_ROLE_20CA],c[K.S_EXIT_CODE_8421]) for c in record[K.S_CASES_4BA2]],record.get(K.S_COMPARISON_436F,{}).get(K.S_STATUS_DA66),flush=True)
    return all(c[K.S_EXIT_CODE_8421]==0 for c in record[K.S_CASES_4BA2]) and record.get(K.S_COMPARISON_436F,{}).get(K.S_IDENTICAL_4197,False)

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument(K.S_OUTPUT_3033,type=Path,required=True);p.add_argument(K.S_SAVE_9BC9,type=Path,required=True);p.add_argument(K.S_ICD_3183,type=Path);args=p.parse_args()
    output=args.output.resolve();save=args.save.resolve(strict=True)
    if output.exists():p.error(K.S_OUTPUT_MUST_BE_A_NEW_DIRECTO_A0FF)
    output.mkdir(parents=True)
    if not pair(output/K.S_SAME_SETTINGS_7C55,save,[K.S_FPS_054E,K.S_60_1642,K.S_ASPECT_A664,K.S_ORIGINAL_DE33,K.S_RESOLUTION_0AEB,K.S_1X_9CA2],[K.S_FPS_054E,K.S_60_1642,K.S_ASPECT_A664,K.S_ORIGINAL_DE33,K.S_RESOLUTION_0AEB,K.S_1X_9CA2],icd=args.icd):return 1
    if not pair(output/K.S_DIFFERENT_SETTINGS_770D,save,[K.S_FPS_054E,K.S_30_C110,K.S_ASPECT_A664,K.S_ORIGINAL_DE33,K.S_RESOLUTION_0AEB,K.S_1X_9CA2],[K.S_FPS_054E,K.S_120_0FE0,K.S_ASPECT_A664,K.S_32_9_CD32,K.S_RESOLUTION_0AEB,K.S_0_50X_E651,K.S_UPSCALER_FA9D,K.S_FSR1_66C6,K.S_AA_872D,K.S_SMAA_424E],icd=args.icd):return 1
    return 0
if __name__==K.S_MAIN_ACB5:raise SystemExit(main())
