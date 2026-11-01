#!/usr/bin/env python3
"""Run two real game processes through LAN and compare their combat traces."""
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
    with p.open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()
def pair(output,save,host_flags,join_flags,ticks=600,scenario='exchange',live=False,timeout=200,join_menu_offset=0,no_trace=False,icd=None):
    output.mkdir(parents=True,exist_ok=False)
    tape=output/'inputs.txt';combat_replay.scenario(tape,ticks,scenario)
    with socket.socket() as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
    base=['--port',str(port),'--save',str(save),'--ticks',str(ticks),'--input-tape',str(tape),'--hidden']
    if icd is not None:base+=['--icd',str(icd)]
    specs=[['host','--bind','127.0.0.1','--seed','12345',*base,*host_flags],['join','127.0.0.1',*base,*join_flags]]
    processes=[];logs=[];record={'binary_sha256':digest(ROOT/'build/sf2'),'tape_sha256':digest(tape),'ticks':ticks,'profiles':specs,'cases':[]}
    start=time.monotonic()
    try:
        for role,spec in zip(['host','join'],specs):
            run=output/role;run.mkdir();args=lan_sparring.parser().parse_args(spec)
            args.binary=args.binary.resolve();args.game=args.game.resolve()
            if live:args.input_tape=None
            config=lan_sparring.configure(args,run)
            env=lan_sparring.environment(args,run,config);env.update(SDL_VIDEODRIVER='x11',SDL_JOYSTICK_LINUX_CLASSIC='1',LARECOMP_VK_DEVLOST_TRACE='1')
            if os.environ.get('SF2_LAN_TEST_VALIDATE'):env['LARECOMP_VK_VALIDATION_STOP']='1'
            if role=='join' and join_menu_offset:
                shifted=[]
                for command in env['LARECOMP_INPUT'].split(','):
                    frame,button,length=command.split(':');shifted.append(f'{int(frame)+round(join_menu_offset*args.fps/60)}:{button}:{length}')
                env['LARECOMP_INPUT']=','.join(shifted)
            if no_trace:env.pop('SF2_REPLAY_TRACE',None)
            if live:
                commands=[(2800,'RIGHT' if role=='host' else 'LEFT',70),(2900 if role=='host' else 2920,'X',8),(2940 if role=='host' else 2960,'Y',8)]
                env['LARECOMP_INPUT']+=','+','.join(f'{round(t*args.fps/60)}:{key}:{max(2,round(length*args.fps/60))}' for t,key,length in commands)
            (run/'environment.json').write_text(json.dumps({k:v for k,v in env.items() if k.startswith(('SF2_','LARECOMP_','VK_','SDL_'))},indent=2)+'\n')
            log=(run/'runtime.log').open('w');logs.append(log)
            processes.append(subprocess.Popen([str(args.binary),str(args.game)],cwd=run,env=env,stdout=log,stderr=subprocess.STDOUT))
        for role,process in zip(['host','join'],processes):
            try:code=process.wait(timeout=max(1,timeout-(time.monotonic()-start)))
            except subprocess.TimeoutExpired:process.kill();process.wait();code='timeout'
            record['cases'].append({'role':role,'exit_code':code})
    finally:
        for p in processes:
            if p.poll() is None:p.kill();p.wait()
        for log in logs:log.close()
    record['seconds']=time.monotonic()-start
    if all(c['exit_code']==0 for c in record['cases']):
        if not no_trace:
            try:record['comparison']=combat_replay.compare(output/'host/trace.jsonl',output/'join/trace.jsonl')
            except (OSError,ValueError,KeyError) as error:record['comparison']={'status':'invalid','identical':False,'error':str(error)}
        if live and not no_trace and record.get('comparison',{}).get('identical'):
            _,_,inputs,_=combat_replay.read_trace(output/'host/trace.jsonl',False)
            record['live_input_players']=sorted({e['player'] for e in inputs})
            if record['live_input_players']!=[1,2]:
                record['comparison'].update(identical=False,status='missing_live_input')
        if not no_trace and record.get('comparison',{}).get('identical'):
            checked=0
            with (output/'host/trace.jsonl').open() as stream:
                for line in stream:
                    row=json.loads(line)
                    if row['kind']!='state':continue
                    fighters=row['state']['models'][:2]
                    if len(fighters)!=2 or any(m['user']!=1 or m['ai']!=0 for m in fighters):
                        record['comparison'].update(identical=False,status='invalid_human_fighters',tick=row['tick']);break
                    checked+=1
            record['human_fighter_states_checked']=checked
    for c in record['cases']:
        run=output/c['role'];log=(run/'lasrecomp.log').read_text() if (run/'lasrecomp.log').exists() else (run/'runtime.log').read_text()
        c['lan_log']=[line for line in log.splitlines() if '[lan]' in line]
        c['gpu_timeout']='GPU did not finish' in log;c['startup_hang']='[hang] ===== main guest thread' in log
        if (run/'trace.jsonl').exists():c['trace_sha256']=digest(run/'trace.jsonl')
    if no_trace and all(c['exit_code']==0 for c in record['cases']):
        assert all(any(f'completed {ticks} synchronized combat steps' in line for line in c['lan_log']) for c in record['cases'])
        record['comparison']={'status':'runtime_hashes_matched','identical':True,'ticks':ticks,'trace_files':False}
    (output/'report.json').write_text(json.dumps(record,indent=2)+'\n')
    print(output.name,[(c['role'],c['exit_code']) for c in record['cases']],record.get('comparison',{}).get('status'),flush=True)
    return all(c['exit_code']==0 for c in record['cases']) and record.get('comparison',{}).get('identical',False)

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);p.add_argument('--save',type=Path,required=True);p.add_argument('--icd',type=Path);args=p.parse_args()
    output=args.output.resolve();save=args.save.resolve(strict=True)
    if output.exists():p.error('output must be a new directory')
    output.mkdir(parents=True)
    if not pair(output/'same-settings',save,['--fps','60','--aspect','original','--resolution','1x'],['--fps','60','--aspect','original','--resolution','1x'],icd=args.icd):return 1
    if not pair(output/'different-settings',save,['--fps','30','--aspect','original','--resolution','1x'],['--fps','120','--aspect','32:9','--resolution','0.50x','--upscaler','fsr1','--aa','smaa'],icd=args.icd):return 1
    return 0
if __name__=='__main__':raise SystemExit(main())
