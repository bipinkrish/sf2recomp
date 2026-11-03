#!/usr/bin/env python3
"""Start locally rendered LAN Sparring; each machine uses its usual P1 controls."""
from _sf2_tool_constants import LAN_SPARRING as K
import argparse
import configparser
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
MENU=((K.N_600_284B,K.S_DOWN_645C),(K.N_690_F610,K.S_DOWN_645C),(K.N_780_0E78,K.S_A_341F),(K.N_1000_4051,K.S_A_341F),(K.N_1200_1519,K.S_RIGHT_BF07),(K.N_1400_55FD,K.S_A_341F),(K.N_1600_B458,K.S_RIGHT_BF07),(K.N_1800_E49E,K.S_A_341F),(K.N_2000_81A8,K.S_A_341F),(K.N_2200_2F83,K.S_A_341F))

def configure(args,output):
    """Private configuration/cache/save directories for this process only."""
    cfg=configparser.ConfigParser(interpolation=None)
    if args.config.exists():cfg.read(args.config)
    source=args.save
    if source is None:
        source=Path(cfg.get(K.S_STORAGE_53C2,K.S_SAVE_DIRECTORY_3C87,fallback=K.S_SAVE_FBDD))
        if not source.is_absolute():source=args.binary.parent/source
    if not source.is_dir():
        raise ValueError(K.S_NO_EXISTING_SAVE_FOUND_START_0EBB)
    values={K.S_NETWORK_E684:{K.S_ENABLED_E36B:K.S_FALSE_D719},K.S_GAMEPLAY_F8D3:{K.S_UNLOCK_ALL_SPARRING_CHARACTE_117F:K.S_TRUE_DFF8,K.S_SHOW_INTRO_7A11:K.S_FALSE_D719,K.S_AI_ENABLED_6F2A:K.S_FALSE_D719},
            K.S_INPUT_AFBC:{K.S_PLAYER_SETUP_CEC8:K.S_SOLO_8B3B},K.S_VIDEO_513E:{K.S_TARGET_FPS_FE29:str(args.fps),K.S_FULLSCREEN_116C:K.S_FALSE_D719},K.S_STORAGE_53C2:{K.S_SAVE_DIRECTORY_3C87:K.S_SAVE_FBDD}}
    for key,value in [(K.S_ASPECT_RATIO_A74D,args.aspect),(K.S_INTERNAL_RESOLUTION_E538,args.resolution),(K.S_UPSCALER_BB7E,args.upscaler),(K.S_ANTIALIASING_1CD7,args.aa)]:
        if value is not None:values[K.S_VIDEO_513E][key]=value
    if args.hidden:values[K.S_AUDIO_45F1]={K.S_VOLUME_D262:K.S_0_59A9}
    for section,options in values.items():
        if not cfg.has_section(section):cfg.add_section(section)
        for key,value in options.items():cfg.set(section,key,value)
    shutil.copytree(source,output/K.S_SAVE_FBDD)
    config=output/K.S_SF2RECOMP_INI_8714
    with config.open(K.S_W_780F) as f:cfg.write(f)
    return config

def environment(args,output,config):
    env={k:v for k,v in os.environ.items() if not k.startswith((K.S_SF2_LAN_17C4,K.S_SF2_REPLAY_DA3E,K.S_LARECOMP_B959))}
    env.update(SF2_CONFIG=str(config),LARECOMP_DATA_ROOT=str(output),LARECOMP_DATA_HOME=str(output),
               SF2_LAN_PORT=str(args.port),LARECOMP_RENDER_THREAD=K.S_1_9A76,
               LARECOMP_INPUT=K.S_EMPTY_EE1B.join(f'{round(t*args.fps/K.N_60_39FA)}:{key}:{max(K.N_2_D473,round(K.N_8_2C62*args.fps/K.N_60_39FA))}' for t,key in MENU))
    if args.role==K.S_HOST_E293:
        env.update(SF2_LAN_HOST=args.bind,SF2_LAN_MATCH=args.match,SF2_LAN_DELAY=str(args.delay))
        if args.seed is not None:env[K.S_SF2_LAN_SEED_2218]=str(args.seed)
    else:env[K.S_SF2_LAN_JOIN_30DC]=args.address
    if args.ticks is not None:
        env.update(SF2_REPLAY_TICKS=str(args.ticks),SF2_REPLAY_TRACE=str(output/K.S_TRACE_JSONL_FB9A))
    if args.input_tape is not None:env[K.S_SF2_REPLAY_INPUT_2230]=str(args.input_tape.resolve(strict=True))
    if args.hidden:env.update(LARECOMP_HIDDEN=K.S_1_9A76,LARECOMP_NO_USER_INPUT=K.S_1_9A76,LARECOMP_WINDOW=K.S_960X540_9D0F)
    if args.icd:
        env[K.S_VK_DRIVER_FILES_B081]=str(args.icd.resolve(strict=True))
        if K.S_NVIDIA_7F25 in env[K.S_VK_DRIVER_FILES_B081]:env[K.S_NV_PRIME_RENDER_OFFLOAD_E67F]=K.S_1_9A76
    return env

def parser():
    p=argparse.ArgumentParser(description=__doc__)
    roles=p.add_subparsers(dest=K.S_ROLE_20CA,required=True)
    host=roles.add_parser(K.S_HOST_E293,help=K.S_HOST_AS_PLAYER_1_EC94)
    host.add_argument(K.S_BIND_482E,default=K.S_0_0_0_0_843A,help=K.S_LOCAL_IPV4_INTERFACE_TO_LIST_D274)
    host.add_argument(K.S_MATCH_B059,default=K.S_17_18_13_2495,help=K.S_NATIVE_P1_P2_ARENA_ROSTER_IN_37D3)
    host.add_argument(K.S_SEED_B0D2,type=int)
    host.add_argument(K.S_DELAY_C55E,type=int,choices=range(K.N_7_7902),default=K.N_2_D473,help=K.S_SHARED_INPUT_DELAY_IN_COMBAT_801D)
    join=roles.add_parser(K.S_JOIN_0986,help=K.S_JOIN_THE_HOST_AS_PLAYER_2_AD60);join.add_argument(K.S_ADDRESS_4FFB,help=K.S_HOST_S_NUMERIC_IPV4_ADDRESS_E8B8)
    for command in (host,join):
        command.add_argument(K.S_BINARY_1375,type=Path,default=ROOT/K.S_BUILD_SF2_ECEA)
        command.add_argument(K.S_GAME_1AB6,type=Path,default=ROOT/K.S_GAME_EXTRACTED_UPDATE_E969)
        command.add_argument(K.S_CONFIG_A0C0,type=Path,default=ROOT/K.S_SF2RECOMP_INI_8714)
        command.add_argument(K.S_SAVE_9BC9,type=Path,help=K.S_COPY_THIS_EXISTING_SAVE_INTO_1092)
        command.add_argument(K.S_OUTPUT_3033,type=Path,help=K.S_NEW_SESSION_DIRECTORY_DEFAUL_72F1)
        command.add_argument(K.S_PORT_2E58,type=int,default=K.N_34250_67EB)
        command.add_argument(K.S_FPS_054E,type=int,choices=(K.N_30_624B,K.N_60_39FA,K.N_120_2ABA,K.N_144_5EC1),default=K.N_60_39FA)
        command.add_argument(K.S_ASPECT_A664,choices=(K.S_ORIGINAL_DE33,K.S_16_10_8E03,K.S_21_9_A3B0,K.S_32_9_CD32))
        command.add_argument(K.S_RESOLUTION_0AEB,choices=(K.S_1X_9CA2,K.S_0_50X_E651,K.S_0_75X_7623,K.S_0_25X_3D81,K.S_MATCH_3189))
        command.add_argument(K.S_UPSCALER_FA9D,choices=(K.S_BILINEAR_9362,K.S_FSR1_66C6))
        command.add_argument(K.S_AA_872D,choices=(K.S_OFF_7E45,K.S_SMAA_424E))
        command.add_argument(K.S_ICD_3183,type=Path,help=argparse.SUPPRESS)
        command.add_argument(K.S_TICKS_0A0C,type=int,help=K.S_DIAGNOSTIC_END_AFTER_THIS_MA_93CB)
        command.add_argument(K.S_INPUT_TAPE_80B5,type=Path,help=argparse.SUPPRESS)
        command.add_argument(K.S_HIDDEN_84FD,action=K.S_STORE_TRUE_2C29,help=argparse.SUPPRESS)
    return p

def main(argv=None):
    p=parser();args=p.parse_args(argv)
    if not 1<=args.port<=K.N_65535_F2F8:p.error(K.S_PORT_MUST_BE_1_65535_FDBB)
    if args.ticks is not None and not 1<=args.ticks<=K.N_1000000_6CCE:p.error(K.S_TICKS_MUST_BE_1_1000000_6943)
    if args.input_tape and args.ticks is None:p.error(K.S_INPUT_TAPE_REQUIRES_A_DIAGNO_7AA1)
    args.binary=args.binary.resolve(strict=True);args.game=args.game.resolve(strict=True);args.config=args.config.resolve()
    if args.save is not None:args.save=args.save.resolve(strict=True)
    if args.output:
        output=args.output.resolve();output.mkdir(parents=True,exist_ok=False)
    else:
        parent=ROOT/K.S_BUILD_LAN_1A9A;parent.mkdir(parents=True,exist_ok=True)
        output=Path(tempfile.mkdtemp(prefix=args.role+K.S_EMPTY_01F8,dir=parent))
    try:config=configure(args,output)
    except ValueError as error:p.error(str(error))
    env=environment(args,output,config)
    (output/K.S_ENVIRONMENT_JSON_64EE).write_text(json.dumps({k:v for k,v in env.items() if k.startswith((K.S_SF2_8749,K.S_LARECOMP_B959,K.S_VK_6B79))},indent=K.N_2_D473)+K.S_N_AD65)
    print(f'LAN {args.role}: private session {output}',flush=True)
    print(K.S_USE_YOUR_NORMAL_PLAYER_1_KEY_3C3B,flush=True)
    try:return subprocess.call([str(args.binary),str(args.game)],cwd=output,env=env)
    except KeyboardInterrupt:return K.N_130_38D6

if __name__==K.S_MAIN_ACB5:raise SystemExit(main())
