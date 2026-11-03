"""Combat tape injection, register preservation, and fail-closed comparisons."""
import copy
import importlib.util
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import unittest
from test_settings import sdl_flags

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('combat_replay', ROOT / 'tools/combat_replay.py')
replay = importlib.util.module_from_spec(spec)
spec.loader.exec_module(replay)

HARNESS = r'''
#include <assert.h>
#include <SDL3/SDL_timer.h>
#include "replay.c"
Settings g_settings = {.target_fps=60};
static int presses, releases, quits, rounds, stances, plays;
static uint64_t fake_instance;
uint64_t renderer_present_count(void) { return 100; }
uint64_t plat_now_ns(void) { return 1000000; }
void menu_request_quit(void) { ++quits; }
bool menu_quit_requested(void) { return false; }
void guest_call(Cpu *c, uint64_t address) {
    if(address==0x7000cf9830ull)c->x[0]=fake_instance;
    else if(address==0x7000ca7100ull)++rounds;
    else if(address==0x7000ca6df0ull) { ++stances;ST32(c->x[0]+0x198,1); }
    else if(address==0x7000ca7c00ull) { ++plays;ST32(c->x[0]+0x198,2); }
    else if(address==0x7000c9dd50ull) {
        uint64_t object=c->x[0];
        assert(!replay_hook(c,address)); // recursion guard invokes original body
        c->x[19]=object;
        assert(!replay_hook(c,0x7000c9de18ull));
        assert(!replay_hook(c,0x7000c9de40ull));
        ST32(object+0x24,LD32(object+0x24)+1);
    }
    else if (address==0x7000ffbf90ull) { float health=100; memcpy(&c->v[0],&health,4); }
    else {
        assert(injecting);
        assert(!replay_hook(c,address));
        if(address==0x7000ff7af0ull)++presses;
        else { assert(address==0x7000ff7c40ull);++releases; }
    }
    c->x[30]=0xBAD; c->nzcv=0xBAD; // all guest invocation state must be restored
}
int main(int argc,char **argv) {
    replay_init();
    if(argc>1 && !strcmp(argv[1],"init"))return 0;
    if(argc>1 && !strcmp(argv[1],"loading")) {
        assert(lan_enabled());selected=true;
        Cpu c={0};c.x[19]=0x123456;c.x[8]=1;c.nzcv=0x5678;
        Cpu saved=c;
        // The native loader calls this only after its own async work finishes.
        // Hold either peer before that boundary while the other keeps updating.
        if(lan_player()==(unsigned)atoi(argv[3])) {
            FILE *marker;
            while(!(marker=fopen(argv[2],"r")))SDL_Delay(1);
            fclose(marker);
        } else {
            for(unsigned i=0;i<100;++i) {
                assert(replay_hook(&c,0x7000da027cull)==2);
                assert(!memcmp(&saved,&c,sizeof c));
                assert(!preparing && !warmup && !fight && !tick);
                SDL_Delay(1);
            }
            FILE *marker=fopen(argv[2],"w");assert(marker);fclose(marker);
        }
        while(replay_hook(&c,0x7000da027cull)==2)SDL_Delay(1);
        assert(!memcmp(&saved,&c,sizeof c));
        assert(!replay_hook(&c,0x7000da027cull)); // repeated polls stay complete
        LanEvent out[LAN_MAX_EVENTS*2];
        for(unsigned i=0;i<128;++i)assert(!lan_step(i,1000+i,NULL,0,out));
        lan_finish(128,9999);
        return 0;
    }
    unsigned char f[0x200]={0}, list[0x28]={0}, array[0x30]={0}, fl[0x20]={0};
    unsigned char p1[0x300]={0}, p2[0x300]={0};
    unsigned char location[0x80]={0}, name[0x30]={0};
    ST32((uintptr_t)name+0x10,4); const uint16_t text[]={ 't','e','s','t' };
    memcpy(name+0x14,text,sizeof text);
    ST64((uintptr_t)location+0x18,(uintptr_t)name);
    ST64((uintptr_t)f+0x70,(uintptr_t)location);
    ST32((uintptr_t)fl+0x18,18);
    ST64((uintptr_t)f+0x30,(uintptr_t)fl);
    ST32((uintptr_t)f+0x198,2);
    ST64((uintptr_t)f+0xb8,(uintptr_t)p1); ST64((uintptr_t)f+0xc0,(uintptr_t)p2);
    ST64((uintptr_t)f+0x40,(uintptr_t)list);
    ST64((uintptr_t)list+0x10,(uintptr_t)array); ST32((uintptr_t)list+0x18,2);
    ST64((uintptr_t)array+0x20,(uintptr_t)p1);ST64((uintptr_t)array+0x28,(uintptr_t)p2);
    ST64((uintptr_t)p1+0x1b8,(uintptr_t)name);ST64((uintptr_t)p2+0x1b8,(uintptr_t)name);
    unsigned char scene[0xc0]={0};
    for(int i=0;i<3;++i) ST64((uintptr_t)scene+0xa0+i*8,(uintptr_t)name);
    unsigned char instance[0x20]={0}, settings[0x30]={0}, catalog[0x30]={0}, entry[0x20]={0};
    fake_instance=(uintptr_t)instance;
    ST64((uintptr_t)instance+0x18,(uintptr_t)settings);
    ST64((uintptr_t)settings+0x18,(uintptr_t)catalog);ST64((uintptr_t)settings+0x20,(uintptr_t)catalog);
    ST64((uintptr_t)catalog+0x18,2);ST64((uintptr_t)catalog+0x20,(uintptr_t)entry);ST64((uintptr_t)catalog+0x28,(uintptr_t)entry);
    ST64((uintptr_t)entry+0x18,(uintptr_t)name);
    if(getenv("SF2_REPLAY_MATCH"))for(int i=0;i<3;++i)ST64((uintptr_t)scene+0xa0+i*8,0);
    Cpu c={0};c.x[0]=(uintptr_t)scene;c.x[19]=(uintptr_t)f;c.sp=0x1234;c.nzcv=0x12345678;
    assert(!replay_hook(&c,0x7000d961f0ull));
    for(int i=0;i<3;++i)assert(LD64((uintptr_t)scene+0xa0+i*8)==(uintptr_t)name);
    if(argc>1 && !strcmp(argv[1],"human")) {
        unsigned char pa[0x70],pb[0x70];memset(pa,0x7a,sizeof pa);memset(pb,0x7a,sizeof pb);
        ST64((uintptr_t)p1+0x140,(uintptr_t)pa);ST64((uintptr_t)p2+0x140,(uintptr_t)pb);
        Cpu before=c;lan_human_fighters((uintptr_t)f);
        assert(!memcmp(&before,&c,sizeof c));
        for(unsigned i=0;i<sizeof pa;++i) {
            unsigned expected=i==0x62?0:i==0x63?1:0x7a;
            assert(pa[i]==expected && pb[i]==expected);
        }
        return 0;
    }
    if(argc>1 && !strcmp(argv[1],"seedquality")) {
        simulation_depth=1;controlled=selected=true;
        double sx=0,sy=0,sxx=0,syy=0,sxy=0;unsigned n=200000;
        for(unsigned i=0;i<n;++i) {
            seed=i*0x9e3779b9u;stream_count=0;
            c.x[30]=0x700102c338ull;c.x[1]=0;c.x[2]=1000000;
            assert(controlled_hook(&c,0x7001237bc0ull)==1);double x=(uint32_t)c.x[0]/1000000.;
            c.x[30]=0x700102c394ull;c.x[1]=0;c.x[2]=1000000;
            assert(controlled_hook(&c,0x7001237bc0ull)==1);double y=(uint32_t)c.x[0]/1000000.;
            sx+=x;sy+=y;sxx+=x*x;syy+=y*y;sxy+=x*y;
        }
        double correlation=(n*sxy-sx*sy)/sqrt((n*sxx-sx*sx)*(n*syy-sy*sy));
        assert(fabs(correlation)<.015);return 0;
    }
    if(argc>1 && !strcmp(argv[1],"control")) {
        unsigned char generator[0x20]={0};
        // Unscoped calls stay original even with seeded replay enabled.
        c.x[0]=(uintptr_t)generator;c.x[1]=99;
        assert(!replay_hook(&c,0x7001177d10ull));
        assert(!stream_count);
        simulation_depth=1;
        assert(replay_hook(&c,0x7001177d10ull)==1);
        assert(LD32((uintptr_t)generator+0x14)==seed);
        c.x[30]=0x7000ffc234ull;
        assert(replay_hook(&c,0x7001177d20ull)==1);
        uint32_t first=c.x[0];
        // A cosmetic call site cannot consume the combat site's stream.
        c.x[0]=(uintptr_t)generator;c.x[30]=0x7000ea8818ull;
        assert(replay_hook(&c,0x7001177d20ull)==1);
        c.x[0]=(uintptr_t)generator;c.x[30]=0x7000ffc234ull;
        assert(replay_hook(&c,0x7001177d20ull)==1);
        assert(c.x[0]==((first*0x41c64e6du+0x3039u)&0x7fffffffu));
        c.x[1]=(uint32_t)-20;c.x[2]=10;c.x[30]=0x700102c338ull;
        for(int i=0;i<100;++i) {
            assert(replay_hook(&c,0x7001237bc0ull)==1);
            assert((int32_t)c.x[0]>=-20 && (int32_t)c.x[0]<10);
        }
        float lo=-2,hi=4;
        memcpy(&c.v[0],&lo,4);memcpy(&c.v[1],&hi,4);
        assert(replay_hook(&c,0x7001237b50ull)==1);
        float result;memcpy(&result,&c.v[0],4);assert(result>=lo && result<=hi);
        simulation_depth=0;
        ST32((uintptr_t)f+0x198,0);
        c.x[0]=(uintptr_t)f;Cpu held=c;
        assert(replay_hook(&c,0x7000c9dd50ull)==1);
        assert(!memcmp(&c,&held,sizeof c) && !rounds && !preparing);
        unsigned char pre[0x90]={0}, viewer[0x80]={0}, round_data[0x30]={0};
        ST64((uintptr_t)f+0x1a0,(uintptr_t)pre);
        ST64((uintptr_t)pre+0x80,(uintptr_t)viewer);
        ST64((uintptr_t)viewer+0x70,(uintptr_t)round_data);
        for(int i=0;i<240;++i) {
            c.x[0]=(uintptr_t)f;
            assert(replay_hook(&c,0x7000ca7e80ull)==1); // ignore wall-clock intro callbacks
            assert(replay_hook(&c,0x7000ca7c00ull)==1);
            assert(replay_hook(&c,0x7000c9dd50ull)==1);
        }
        assert(rounds==1 && stances==1 && plays==0 && !fight && warmup==240);
        c.x[0]=(uintptr_t)f;
        assert(replay_hook(&c,0x7000c9dd50ull)==1);
        assert(plays==1 && fight==(uintptr_t)f && tick==1 && frame_origin==240);
        printf("%u\n",first);
        return 0;
    }
    if(argc>1 && !strcmp(argv[1],"record")) {
        assert(!replay_hook(&c,0x7000c9de18ull));
        assert(!replay_hook(&c,0x7000c9de40ull));
        c.x[0]=(uintptr_t)p2;c.x[1]=10;
        assert(!replay_hook(&c,0x7000ff7af0ull));
        assert(!replay_hook(&c,0x7000c9de18ull));
        assert(!replay_hook(&c,0x7000c9de40ull));
        assert(quits==1);
        return 0;
    }
    Cpu saved=c;
    assert(!replay_hook(&c,0x7000c9de18ull));
    assert(!memcmp(&c,&saved,sizeof c));assert(presses==2 && releases==0);
    c.x[0]=(uintptr_t)p1;c.x[1]=10;saved=c;
    assert(replay_hook(&c,0x7000ff7af0ull)==1);
    assert(!memcmp(&c,&saved,sizeof c) && presses==2);
    assert(!replay_hook(&c,0x7000c9de40ull));assert(tick==1 && !quits);
    assert(!replay_hook(&c,0x7000c9de18ull));
    assert(presses==2 && releases==2);assert(!memcmp(&c,&saved,sizeof c));
    assert(!replay_hook(&c,0x7000c9de40ull));assert(tick==2 && quits==1);
    assert(!replay_hook(&c,0x7000c9de18ull));assert(tick==2 && quits==1);
}
'''


class RuntimeReplayTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='sf2-replay-test-')
        cls.root = Path(cls.temp.name)
        source = cls.root / 'harness.c'
        source.write_text(HARNESS)
        cls.binary = cls.root / 'harness'
        cflags,libs=sdl_flags()
        subprocess.run(['clang', '-O2', '-I', str(ROOT / 'runtime'), *cflags, str(source), str(ROOT/'runtime/lan.c'), *libs, '-lm', '-o', str(cls.binary)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def env(self, root):
        env = {k: v for k, v in os.environ.items() if not k.startswith('SF2_REPLAY_')}
        env.update(SF2_REPLAY_TRACE=str(root / 'trace.jsonl'), SF2_REPLAY_INPUT=str(root / 'tape.txt'), SF2_REPLAY_TICKS='2')
        return env

    def test_tick_routing_registers_live_suppression_and_complete_trace(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / 'tape.txt').write_text('SF2_REPLAY 1\n0 1 9 1\n0 2 10 1\n1 1 9 0\n1 2 10 0\n')
            subprocess.run([str(self.binary)], env=self.env(root), check=True)
            header, setup, inputs, states = replay.read_trace(root / 'trace.jsonl')
            self.assertEqual([s['tick'] for s in states], [0, 1, 2])
            self.assertEqual([e['player'] for e in inputs], [1, 2, 1, 2])
            self.assertEqual(setup['players'], ['test', 'test'])
            self.assertEqual(states[0]['state']['models'][0]['health_bits'], 0x42c80000)
            self.assertEqual(header['rng_policy'], 'original-unmodified')

    def test_lan_marks_both_native_fighters_as_human_without_changing_other_parameters(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);(root/'tape.txt').write_text('SF2_REPLAY 1\n0 1 9 1\n')
            subprocess.run([str(self.binary),'human'],env=self.env(root),check=True)

    def test_loading_waits_for_either_slow_peer_before_native_intro(self):
        for slow_player in (0,1):
            with self.subTest(slow_player=slow_player), tempfile.TemporaryDirectory() as folder:
                root=Path(folder)
                with socket.socket() as s:
                    s.bind(('127.0.0.1',0));port=str(s.getsockname()[1])
                processes=[]
                try:
                    for player in (0,1):
                        env={k:v for k,v in os.environ.items() if not k.startswith(('SF2_LAN_','SF2_REPLAY_'))}
                        env.update(SF2_LAN_PORT=port,SF2_REPLAY_TICKS='128',SF2_REPLAY_TRACE=str(root/f'{player}.jsonl'))
                        env['SF2_LAN_JOIN' if player else 'SF2_LAN_HOST']='127.0.0.1'
                        if not player:env['SF2_LAN_SEED']='12345'
                        processes.append(subprocess.Popen([str(self.binary),'loading',str(root/'loaded'),str(slow_player)],env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE))
                    for process in processes:
                        _,log=process.communicate(timeout=8)
                        self.assertEqual(process.returncode,0,log.decode())
                        self.assertIn(b'both players loaded',log)
                        self.assertIn(b'completed 128 synchronized',log)
                finally:
                    for process in processes:
                        if process.poll() is None:process.kill()
                        process.communicate()

    def test_fixed_match_uses_original_roster_objects_and_preserves_registers(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / 'tape.txt').write_text('SF2_REPLAY 1\n0 1 9 1\n0 2 10 1\n1 1 9 0\n1 2 10 0\n')
            env = self.env(root)
            env['SF2_REPLAY_MATCH'] = '0,1,0'
            subprocess.run([str(self.binary)], env=env, check=True)
            self.assertTrue(replay.read_trace(root / 'trace.jsonl')[0]['fixed_match'])

    def test_bad_tapes_and_existing_output_are_rejected(self):
        for tape in ['wrong\n', 'SF2_REPLAY 1\n0 3 9 1\n', 'SF2_REPLAY 1\n2 1 9 1\n',
                     'SF2_REPLAY 1\n1 1 9 1\n0 1 9 0\n', 'SF2_REPLAY 1\n0 1 9 1 garbage\n']:
            with self.subTest(tape=tape), tempfile.TemporaryDirectory() as folder:
                root = Path(folder)
                (root / 'tape.txt').write_text(tape)
                result = subprocess.run([str(self.binary), 'init'], env=self.env(root), capture_output=True)
                self.assertEqual(result.returncode, 2)
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / 'tape.txt').write_text('SF2_REPLAY 1\n')
            (root / 'trace.jsonl').write_text('keep me')
            result = subprocess.run([str(self.binary), 'init'], env=self.env(root), capture_output=True)
            self.assertEqual(result.returncode, 2)
            self.assertEqual((root / 'trace.jsonl').read_text(), 'keep me')

    def test_live_recording_preserves_input_and_tick_identity(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            env = self.env(root)
            del env['SF2_REPLAY_INPUT']
            env['SF2_REPLAY_RECORD'] = str(root / 'record.txt')
            subprocess.run([str(self.binary), 'record'], env=env, check=True)
            self.assertEqual((root / 'record.txt').read_text(), 'SF2_REPLAY 1\n1 2 10 1\n')
            self.assertEqual(replay.read_trace(root / 'trace.jsonl')[2],
                             [{'tick': 1, 'player': 2, 'code': 10, 'down': 1}])

    def test_combat_stream_first_draws_are_not_correlated_across_shared_seeds(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / 'tape.txt').write_text('SF2_REPLAY 1\n')
            subprocess.run([str(self.binary), 'seedquality'], env=self.env(root), check=True)

    def test_controlled_rng_is_scoped_seeded_and_pre_fight_uses_fixed_steps(self):
        outputs = []
        for seed in (12345, 12345, 54321):
            with tempfile.TemporaryDirectory() as folder:
                root = Path(folder)
                (root / 'tape.txt').write_text('SF2_REPLAY 1\n')
                env = self.env(root)
                env['SF2_REPLAY_SEED'] = str(seed)
                result = subprocess.run([str(self.binary), 'control'], env=env, check=True, capture_output=True)
                outputs.append(result.stdout)
                header = json.loads((root / 'trace.jsonl').read_text().splitlines()[0])
                self.assertEqual(header['seed'], seed)
                self.assertEqual(header['rng_policy'], 'scoped-callsite-lcg-v2')
        self.assertEqual(outputs[0], outputs[1])
        self.assertNotEqual(outputs[0], outputs[2])


class ComparisonTests(unittest.TestCase):
    def test_replay_hook_instructions_match_supported_game(self):
        game = ROOT / 'game/extracted/update/exefs/main'
        if not game.is_file():
            self.skipTest('local original game unavailable')
        from test_hook_groups import patches
        from nso import Nso
        module = Nso(game)
        for site, hook in patches.HOOKS.items():
            if len(hook) == 4 and hook[3] == '2u':
                self.assertEqual(module.u32(site - 0x7000000000), hook[0], hex(site))

    def rows(self):
        return [{'kind': 'header', 'schema': 1, 'limit': 1, 'game': 'update_v131072',
                 'coverage': 'observed-combat-fields-v1', 'rng_policy': 'original-unmodified', 'ai_enabled': False},
                {'kind': 'setup', 'location': 'dojo', 'players': ['a', 'b']},
                {'kind': 'state', 'tick': 0, 'state': {'models': [{'health_bits': 1}]},
                 'diagnostic': {'present': 1, 'elapsed_ns': 0, 'fight_frame': 400}},
                {'kind': 'state', 'tick': 1, 'state': {'models': [{'health_bits': 1}]},
                 'diagnostic': {'present': 2, 'elapsed_ns': 10000000, 'fight_frame': 400}},
                {'kind': 'complete', 'ticks': 1, 'events_applied': 0}]

    def compare_rows(self, left, right):
        with tempfile.TemporaryDirectory() as folder:
            a, b = [Path(folder) / n for n in ('a.jsonl', 'b.jsonl')]
            for path, rows in [(a, left), (b, right)]:
                path.write_text(''.join(json.dumps(r) + '\n' for r in rows))
            return replay.compare(a, b)

    def test_diagnostics_do_not_mask_or_create_gameplay_differences(self):
        left = self.rows()
        right = copy.deepcopy(left)
        right[0]['fps'] = 144
        right[3]['diagnostic']['present'] = 100
        self.assertTrue(self.compare_rows(left, right)['identical'])
        right[3]['state']['models'][0]['health_bits'] = 2
        report = self.compare_rows(left, right)
        self.assertFalse(report['identical'])
        self.assertEqual(report['first_tick'], 1)
        self.assertEqual(report['differences'][0]['field'], 'state.models[0].health_bits')

    def test_lan_early_completion_requires_native_game_over_and_full_state_prefix(self):
        rows=self.rows();rows[0].update(limit=600,end_on_match=True)
        rows[3]['state']['flags']=[0,1,0,0,0]
        rows[-1]['reason']='match_finished'
        report=self.compare_rows(rows,rows)
        self.assertTrue(report['identical']);self.assertEqual(report['compared_states'],2)
        for field,value in [('end_on_match',False)]:
            invalid=copy.deepcopy(rows);invalid[0][field]=value
            with self.assertRaises(ValueError):self.compare_rows(rows,invalid)
        invalid=copy.deepcopy(rows);invalid[3]['state']['flags'][1]=0
        with self.assertRaises(ValueError):self.compare_rows(rows,invalid)
        invalid=copy.deepcopy(rows);invalid.pop(3)
        with self.assertRaises(ValueError):self.compare_rows(rows,invalid)

    def test_incomplete_empty_duplicate_and_different_setup_cannot_pass(self):
        left = self.rows()
        for rows in ([], left[:-1], left[:3] + [left[2]] + left[3:]):
            with self.subTest(rows=rows), self.assertRaises(ValueError):
                self.compare_rows(left, rows)
        right = copy.deepcopy(left)
        right[1]['location'] = 'different'
        self.assertEqual(self.compare_rows(left, right)['status'], 'incompatible')

    def test_v3_cannot_claim_matching_framing_when_fields_are_missing(self):
        rows = self.rows()
        rows[0]['coverage'] = 'observed-combat-fields-v3'
        for invalid in (None, [], [0] * 5, [0] * 7, [0] * 5 + [False]):
            with self.subTest(invalid=invalid), self.assertRaises(ValueError):
                candidate = copy.deepcopy(rows)
                for row in candidate:
                    if row['kind'] == 'state':
                        row['state']['arena_framing'] = invalid
                self.compare_rows(candidate, candidate)
        for row in rows:
            if row['kind'] == 'state':
                row['state']['arena_framing'] = [0] * 6
        self.assertTrue(self.compare_rows(rows, rows)['identical'])

    def test_scenario_has_actions_for_both_fighters_and_stays_in_bounds(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'tape.txt'
            replay.scenario(path, 180)
            events = [tuple(map(int, line.split())) for line in path.read_text().splitlines()
                      if line and line[0].isdigit()]
            self.assertEqual({e[1] for e in events}, {1, 2})
            self.assertTrue(all(e[0] < 180 for e in events))
            self.assertEqual([e[0] for e in events], sorted(e[0] for e in events))

    def test_wall_scenario_releases_controls_and_attacks_at_both_ends(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'walls.txt'
            with self.assertRaises(ValueError):
                replay.scenario(path, 600, 'walls')
            replay.scenario(path, 2000, 'walls')
            held = set()
            attacks = {1: set(), 2: set()}
            directions = {1: set(), 2: set()}
            for line in path.read_text().splitlines():
                if not line or not line[0].isdigit():
                    continue
                tick, player, code, down = map(int, line.split())
                self.assertLess(tick, 2000)
                key = player, code
                if down:
                    self.assertNotIn(key, held)
                    held.add(key)
                    if code in (3, 7):
                        directions[player].add(code)
                    elif code in (9, 10):
                        attacks[player].add('left' if tick < 700 else 'right')
                else:
                    self.assertIn(key, held)
                    held.remove(key)
            self.assertFalse(held)
            for player in (1, 2):
                self.assertEqual(directions[player], {3, 7})
                self.assertEqual(attacks[player], {'left', 'right'})

    def test_edge_scenario_separates_fighters_before_attacking(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'edges.txt'
            replay.scenario(path, 1000, 'edges')
            events = [tuple(map(int, line.split())) for line in path.read_text().splitlines()
                      if line and line[0].isdigit()]
            self.assertEqual([e for e in events if e[0] == 0], [(0, 1, 7, 1), (0, 2, 3, 1)])
            self.assertEqual([e for e in events if e[0] == 800], [(800, 1, 7, 0), (800, 2, 3, 0)])
            self.assertTrue(all(810 <= e[0] < 1000 for e in events if e[2] in (9, 10)))

    def test_unstable_repeat_control_prevents_fps_causal_claim(self):
        cases = [{'name': 'baseline-a', 'status': 'matched', 'identical': True},
                 {'name': 'baseline-b', 'status': 'diverged', 'identical': False},
                 {'name': 'fps-120', 'status': 'diverged', 'identical': False}]
        self.assertIn('cross_setting_cause_not_established', replay.matrix_conclusion(cases))
        self.assertEqual(replay.matrix_conclusion(cases[:1]), 'repeat_control_not_run')
        self.assertEqual(replay.matrix_conclusion([{'name': 'baseline-a', 'status': 'reference_unavailable'}]),
                         'incomplete_evidence')

    def test_animation_differences_have_field_names(self):
        left = {'animation': [0] * len(replay.ANIMATION_FIELDS)}
        right = copy.deepcopy(left)
        right['animation'][7] = 1
        difference = replay.differences(left, right)[0]
        self.assertEqual(difference['field'], 'state.animation.interframe')
        self.assertEqual(difference['raw_field'], 'state.animation[7]')

    def test_logical_fight_frame_cannot_be_hidden_as_presentation_metadata(self):
        left = self.rows()
        right = copy.deepcopy(left)
        right[2]['diagnostic']['fight_frame'] += 1
        report = self.compare_rows(left, right)
        self.assertFalse(report['identical'])
        self.assertEqual(report['first_tick'], 0)
        self.assertEqual(report['differences'][0]['field'], 'state.fight_frame')

    def test_seed_initialization_and_rng_state_are_part_of_the_contract(self):
        left = self.rows()
        left[0].update(seed=12345, initialization='native-round-stance-240-steps-v1')
        right = copy.deepcopy(left)
        right[0]['seed'] = 12346
        self.assertEqual(self.compare_rows(left, right)['status'], 'incompatible')
        right = copy.deepcopy(left)
        right[0]['combat_geometry'] = 'pvp-authored-16:9-v1'
        self.assertEqual(self.compare_rows(left, right)['status'], 'incompatible')
        right = copy.deepcopy(left)
        right[0]['initialization'] = 'original'
        self.assertEqual(self.compare_rows(left, right)['status'], 'incompatible')
        right = copy.deepcopy(left)
        left[3]['state']['random_streams'] = [['020000000102c338', 1, 2]]
        right[3]['state']['random_streams'] = [['020000000102c338', 2, 3]]
        report = self.compare_rows(left, right)
        self.assertEqual(report['status'], 'diverged')
        self.assertEqual(report['first_tick'], 1)


if __name__ == '__main__':
    unittest.main()
