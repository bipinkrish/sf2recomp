#!/usr/bin/env python3
"""Start locally rendered LAN Sparring; each machine uses its usual P1 controls."""
import argparse
import configparser
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
MENU=((600,'DOWN'),(690,'DOWN'),(780,'A'),(1000,'A'),(1200,'RIGHT'),(1400,'A'),(1600,'RIGHT'),(1800,'A'),(2000,'A'),(2200,'A'))

def configure(args,output):
    """Private configuration/cache/save directories for this process only."""
    cfg=configparser.ConfigParser(interpolation=None)
    if args.config.exists():cfg.read(args.config)
    source=args.save
    if source is None:
        source=Path(cfg.get('storage','save_directory',fallback='save'))
        if not source.is_absolute():source=args.binary.parent/source
    if not source.is_dir():
        raise ValueError('No existing save found: start the game offline once, or supply --save /path/to/save')
    values={'network':{'enabled':'false'},'gameplay':{'unlock_all_sparring_characters':'true','show_intro':'false','ai_enabled':'false'},
            'input':{'player_setup':'solo'},'video':{'target_fps':str(args.fps),'fullscreen':'false'},'storage':{'save_directory':'save'}}
    for key,value in [('aspect_ratio',args.aspect),('internal_resolution',args.resolution),('upscaler',args.upscaler),('antialiasing',args.aa)]:
        if value is not None:values['video'][key]=value
    if args.hidden:values['audio']={'volume':'0'}
    for section,options in values.items():
        if not cfg.has_section(section):cfg.add_section(section)
        for key,value in options.items():cfg.set(section,key,value)
    shutil.copytree(source,output/'save')
    config=output/'sf2recomp.ini'
    with config.open('w') as f:cfg.write(f)
    return config

def environment(args,output,config):
    env={k:v for k,v in os.environ.items() if not k.startswith(('SF2_LAN_','SF2_REPLAY_','LARECOMP_'))}
    env.update(SF2_CONFIG=str(config),LARECOMP_DATA_ROOT=str(output),LARECOMP_DATA_HOME=str(output),
               SF2_LAN_PORT=str(args.port),LARECOMP_RENDER_THREAD='1',
               LARECOMP_INPUT=','.join(f'{round(t*args.fps/60)}:{key}:{max(2,round(8*args.fps/60))}' for t,key in MENU))
    if args.role=='host':
        env.update(SF2_LAN_HOST=args.bind,SF2_LAN_MATCH=args.match,SF2_LAN_DELAY=str(args.delay))
        if args.seed is not None:env['SF2_LAN_SEED']=str(args.seed)
    else:env['SF2_LAN_JOIN']=args.address
    if args.ticks is not None:
        env.update(SF2_REPLAY_TICKS=str(args.ticks),SF2_REPLAY_TRACE=str(output/'trace.jsonl'))
    if args.input_tape is not None:env['SF2_REPLAY_INPUT']=str(args.input_tape.resolve(strict=True))
    if args.hidden:env.update(LARECOMP_HIDDEN='1',LARECOMP_NO_USER_INPUT='1',LARECOMP_WINDOW='960x540')
    if args.icd:
        env['VK_DRIVER_FILES']=str(args.icd.resolve(strict=True))
        if 'nvidia' in env['VK_DRIVER_FILES']:env['__NV_PRIME_RENDER_OFFLOAD']='1'
    return env

def parser():
    p=argparse.ArgumentParser(description=__doc__)
    roles=p.add_subparsers(dest='role',required=True)
    host=roles.add_parser('host',help='host as player 1')
    host.add_argument('--bind',default='0.0.0.0',help='local IPv4 interface to listen on')
    host.add_argument('--match',default='17,18,13',help='native P1,P2,arena roster indices (default Wraith,Smother,Statue)')
    host.add_argument('--seed',type=int)
    host.add_argument('--delay',type=int,choices=range(7),default=2,help='shared input delay in combat steps')
    join=roles.add_parser('join',help='join the host as player 2');join.add_argument('address',help="host's numeric IPv4 address")
    for command in (host,join):
        command.add_argument('--binary',type=Path,default=ROOT/'build/sf2')
        command.add_argument('--game',type=Path,default=ROOT/'game/extracted/update')
        command.add_argument('--config',type=Path,default=ROOT/'sf2recomp.ini')
        command.add_argument('--save',type=Path,help='copy this existing save into the private session')
        command.add_argument('--output',type=Path,help='new session directory; default build/lan/<unique role>')
        command.add_argument('--port',type=int,default=34250)
        command.add_argument('--fps',type=int,choices=(30,60,120,144),default=60)
        command.add_argument('--aspect',choices=('original','16:10','21:9','32:9'))
        command.add_argument('--resolution',choices=('1x','0.50x','0.75x','0.25x','match'))
        command.add_argument('--upscaler',choices=('bilinear','fsr1'))
        command.add_argument('--aa',choices=('off','smaa'))
        command.add_argument('--icd',type=Path,help=argparse.SUPPRESS)
        command.add_argument('--ticks',type=int,help='diagnostic: end after this many combat steps and write a trace')
        command.add_argument('--input-tape',type=Path,help=argparse.SUPPRESS)
        command.add_argument('--hidden',action='store_true',help=argparse.SUPPRESS)
    return p

def main(argv=None):
    p=parser();args=p.parse_args(argv)
    if not 1<=args.port<=65535:p.error('port must be 1..65535')
    if args.ticks is not None and not 1<=args.ticks<=1000000:p.error('ticks must be 1..1000000')
    if args.input_tape and args.ticks is None:p.error('input tape requires a diagnostic tick limit')
    args.binary=args.binary.resolve(strict=True);args.game=args.game.resolve(strict=True);args.config=args.config.resolve()
    if args.save is not None:args.save=args.save.resolve(strict=True)
    if args.output:
        output=args.output.resolve();output.mkdir(parents=True,exist_ok=False)
    else:
        parent=ROOT/'build/lan';parent.mkdir(parents=True,exist_ok=True)
        output=Path(tempfile.mkdtemp(prefix=args.role+'-',dir=parent))
    try:config=configure(args,output)
    except ValueError as error:p.error(str(error))
    env=environment(args,output,config)
    (output/'environment.json').write_text(json.dumps({k:v for k,v in env.items() if k.startswith(('SF2_','LARECOMP_','VK_'))},indent=2)+'\n')
    print(f'LAN {args.role}: private session {output}',flush=True)
    print('Use your normal player-1 keyboard/controller; the joined machine controls fighter 2.',flush=True)
    try:return subprocess.call([str(args.binary),str(args.game)],cwd=output,env=env)
    except KeyboardInterrupt:return 130

if __name__=='__main__':raise SystemExit(main())
