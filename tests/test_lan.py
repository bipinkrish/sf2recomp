"""Real localhost sockets: ownership, framing, delay, divergence and disconnects."""
import os
from pathlib import Path
import socket
import struct
import subprocess
import tempfile
import time
import unittest
from test_settings import sdl_flags

ROOT=Path(__file__).resolve().parents[1]
HARNESS=r'''
#include <assert.h>
#include <SDL3/SDL_timer.h>
#include "lan.h"
#include "settings.h"
#include <stdlib.h>
#include <string.h>
Settings g_settings={.network_enabled=true,.ai_enabled=true};
bool menu_quit_requested(void) { return false; }
int main(int argc,char **argv) {
    lan_init();
    if(!lan_enabled())return 0;
    assert(!g_settings.network_enabled && !g_settings.ai_enabled && g_settings.player_setup==PLAYER_SETUP_SOLO);
    const LanConfig *cfg=lan_config();
    assert(cfg->seed==12345 && cfg->match[0]==17 && cfg->match[1]==18 && cfg->match[2]==13);
    unsigned me=lan_player();
    for(unsigned tick=0;tick<128;++tick) {
        if(argc>1 && !strcmp(argv[1],"close") && me==1 && tick==10) { lan_close();return 0; }
        if(me && !(tick%9))SDL_Delay(2);
        uint64_t state=UINT64_C(0x100000000)+tick;
        if(argc>1 && !strcmp(argv[1],"diverge") && me==1 && tick==10)++state;
        // Caller-provided ownership is deliberately wrong: role, rather than
        // an untrusted event field, must determine which fighter is controlled.
        LanEvent local={99,(tick+me)%15,tick%2},out[LAN_MAX_EVENTS*2];
        unsigned n=lan_step(tick,state,&local,1,out);
        if(tick<cfg->delay)assert(!n);
        else {
            unsigned captured=tick-cfg->delay;
            assert(n==2 && out[0].player==0 && out[1].player==1);
            assert(out[0].code==captured%15 && out[1].code==(captured+1)%15);
            assert(out[0].down==captured%2 && out[1].down==captured%2);
        }
    }
    lan_finish(128,999);
}
'''

class LanTransportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp=tempfile.TemporaryDirectory(prefix='sf2-lan-test-');cls.root=Path(cls.tmp.name)
        source=cls.root/'test.c';source.write_text(HARNESS);cls.binary=cls.root/'test'
        cflags,libs=sdl_flags()
        subprocess.run(['clang','-O2','-Wall','-I'+str(ROOT/'runtime'),*cflags,str(source),str(ROOT/'runtime/lan.c'),*libs,'-o',str(cls.binary)],check=True)
    @classmethod
    def tearDownClass(cls):cls.tmp.cleanup()
    def base_env(self):return {k:v for k,v in os.environ.items() if not k.startswith('SF2_LAN_')}
    def port(self):
        with socket.socket() as s:s.bind(('127.0.0.1',0));return str(s.getsockname()[1])
    def pair(self,mode='',delay=2,host_limit=None,join_limit=None):
        port=self.port();host=self.base_env();join=dict(host)
        host.update(SF2_LAN_HOST='127.0.0.1',SF2_LAN_PORT=port,SF2_LAN_SEED='12345',SF2_LAN_DELAY=str(delay))
        join.update(SF2_LAN_JOIN='127.0.0.1',SF2_LAN_PORT=port)
        if host_limit is not None:host['SF2_REPLAY_TICKS']=str(host_limit)
        if join_limit is not None:join['SF2_REPLAY_TICKS']=str(join_limit)
        a=subprocess.Popen([str(self.binary),mode],env=host,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        b=subprocess.Popen([str(self.binary),mode],env=join,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        try:
            _,bl=b.communicate(timeout=12);_,al=a.communicate(timeout=12)
            return a.returncode,b.returncode,al,bl
        finally:
            for p in (a,b):
                if p.poll() is None:p.kill();p.communicate()
    def test_shared_setup_owned_input_order_and_zero_default_max_delay(self):
        for delay in (0,2,6):
            with self.subTest(delay=delay):
                a,b,al,bl=self.pair(delay=delay)
                self.assertEqual((a,b),(0,0),(al+bl).decode());self.assertIn(b'completed 128 synchronized',al)
    def test_host_step_limit_is_negotiated_and_conflicting_override_is_rejected(self):
        a,b,al,bl=self.pair(host_limit=128)
        self.assertEqual((a,b),(0,0),(al+bl).decode())
        a,b,al,bl=self.pair(host_limit=128,join_limit=256)
        self.assertEqual((a,b),(3,3));self.assertIn(b'step limits differ',al+bl)

    def test_divergence_stops_both_peers(self):
        a,b,al,bl=self.pair('diverge');self.assertEqual((a,b),(3,3));self.assertIn(b'divergence at step 10',al+bl)
    def test_closed_peer_stops_without_hanging(self):
        a,b,al,bl=self.pair('close');self.assertEqual((a,b),(3,0));self.assertIn(b'combat stopped',al)
    def test_oversized_packet_and_old_rng_policy_are_rejected_before_payload(self):
        for size,policy,message in [(0xffffffff,0x00030004,b'oversized peer packet'),
                                    (0,0x00030003,b'state/setup mismatch')]:
            with self.subTest(policy=policy,size=size):
                port=self.port();env=self.base_env();env.update(SF2_LAN_HOST='127.0.0.1',SF2_LAN_PORT=port,SF2_LAN_SEED='12345')
                p=subprocess.Popen([str(self.binary)],env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
                try:
                    end=time.monotonic()+3
                    while True:
                        try:s=socket.create_connection(('127.0.0.1',int(port)),timeout=.2);break
                        except ConnectionRefusedError:
                            if time.monotonic()>end:raise
                            time.sleep(.01)
                    with s:s.sendall(struct.pack('!8I',0x5346324c,1,1,0,size,0x53463201,policy,1))
                    _,log=p.communicate(timeout=3);self.assertEqual(p.returncode,3);self.assertIn(message,log)
                finally:
                    if p.poll() is None:p.kill();p.communicate()
    def test_offline_is_socket_free_and_invalid_roles_fail_before_connecting(self):
        cases=[({},0),({'SF2_LAN_PORT':'34250'},3),({'SF2_LAN_HOST':'127.0.0.1','SF2_LAN_JOIN':'127.0.0.1'},3),
               ({'SF2_LAN_JOIN':'127.0.0.1','SF2_LAN_SEED':'9'},3),({'SF2_LAN_HOST':'invalid'},3),({'SF2_LAN_HOST':'127.0.0.1','SF2_LAN_PORT':'0'},3)]
        for flags,expected in cases:
            with self.subTest(flags=flags):
                env=self.base_env();env.update(flags)
                r=subprocess.run([str(self.binary)],env=env,capture_output=True,timeout=2)
                self.assertEqual(r.returncode,expected,r.stderr.decode())
