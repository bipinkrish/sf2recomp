"""Native discovery, reusable sessions and recoverable disconnects."""
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import unittest
from test_settings import sdl_flags
ROOT=Path(__file__).resolve().parents[1]
HARNESS=r"""
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <SDL3/SDL_timer.h>
#include "settings.h"
#include "lan.h"
#include "lan_random.h"
Settings g_settings={.network_enabled=true,.ai_enabled=true,.player_setup=PLAYER_SETUP_TWO_CONTROLLERS};
bool menu_quit_requested(void) { return false; }
static void pump_until_connected(bool host) {
    uint64_t until=SDL_GetTicksNS()+10000000000ull;
    while(lan_gui_status()!=LAN_GUI_CONNECTED) {
        assert(SDL_GetTicksNS()<until);
        lan_gui_poll();assert(lan_gui_status()!=LAN_GUI_ERROR);
        if(!host && lan_gui_status()==LAN_GUI_SEARCHING) {
            LanHost list[12];unsigned count=lan_gui_hosts(list);
            if(count) { assert(list[0].port);assert(lan_gui_join(&list[0])); }
        }
        SDL_Delay(1);
    }
}
int main(int argc,char **argv) {
    bool host=!strcmp(argv[1],"host");
    assert(host?lan_gui_host(atoi(argv[2])):lan_gui_search());
    pump_until_connected(host);
    assert(lan_enabled() && lan_gui_active() && !g_settings.network_enabled && !g_settings.ai_enabled);
    uint64_t host_choices=12345;
    for(unsigned game=0;game<2;++game) {
        assert(lan_gui_send_menu(0,1,4+lan_player(),1));
        unsigned mask=0;LanMenuEvent e;
        uint64_t until=SDL_GetTicksNS()+5000000000ull;
        while(mask!=3) {
            assert(SDL_GetTicksNS()<until);lan_gui_poll();
            while(lan_gui_pop_menu(&e,0)) {
                assert(e.module==0 && e.down==1 && e.event==4+e.player);
                mask|=1u<<e.player;
            }
            SDL_Delay(1);
        }
        uint32_t expected[3];
        for(unsigned i=0;i<3;++i)expected[i]=lan_selection_bounded(&host_choices,i==2?14:60);
        if(host)assert(lan_gui_send_match(expected));
        until=SDL_GetTicksNS()+5000000000ull;
        while(!lan_gui_take_match()) { assert(SDL_GetTicksNS()<until);lan_gui_poll();SDL_Delay(1); }
        assert(!memcmp(lan_config()->match,expected,sizeof expected));
        lan_gui_begin_match();
        for(unsigned tick=0;tick<128;++tick) {
            LanEvent local={99,(tick+lan_player())%15,tick%2},out[LAN_MAX_EVENTS*2];
            unsigned n=lan_step(tick,UINT64_C(1000)+tick+((uint64_t)lan_config()->seed<<24)+((uint64_t)expected[0]<<16)+((uint64_t)expected[1]<<8)+expected[2],&local,1,out);
            if(tick<2)assert(!n);
            else assert(n==2 && out[0].player==0 && out[1].player==1 &&
                        out[0].code==(tick-2)%15 && out[1].code==(tick-1)%15);
        }
        lan_finish(128,8888);
        assert(lan_enabled() && lan_gui_status()==LAN_GUI_CONNECTED);
        fprintf(stderr,"native match %u complete; connection retained\n",game);
    }
    if(host) {
        lan_gui_leave();assert(!lan_enabled() && !lan_gui_active());
        assert(g_settings.network_enabled && g_settings.ai_enabled && g_settings.player_setup==PLAYER_SETUP_TWO_CONTROLLERS);
    } else {
        uint64_t until=SDL_GetTicksNS()+3000000000ull;
        while(lan_gui_status()!=LAN_GUI_ERROR) { assert(SDL_GetTicksNS()<until);lan_gui_poll();SDL_Delay(1); }
        assert(!lan_enabled());assert(strstr(lan_gui_error(),"left Sparring"));
        assert(g_settings.network_enabled && g_settings.ai_enabled);
        lan_gui_leave();assert(lan_gui_search());lan_gui_leave();
    }
}
"""
class NativeLanTests(unittest.TestCase):
    def test_discovery_two_matches_disconnect_and_reentry(self):
        with tempfile.TemporaryDirectory(prefix='sf2-native-lan-') as directory:
            work=Path(directory);source=work/'test.c';source.write_text('#include <string.h>\n'+HARNESS)
            cflags,libs=sdl_flags()
            subprocess.run(['clang','-O2','-I'+str(ROOT/'runtime'),*cflags,str(source),str(ROOT/'runtime/lan.c'),*libs,'-o',str(work/'test')],check=True)
            with socket.socket() as s:s.bind(('127.0.0.1',0));port=str(s.getsockname()[1])
            env={k:v for k,v in os.environ.items() if not k.startswith(('SF2_LAN_','SF2_REPLAY_'))}
            host=subprocess.Popen([str(work/'test'),'host',port],env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
            join=subprocess.Popen([str(work/'test'),'join',port],env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
            try:
                _,jl=join.communicate(timeout=15);_,hl=host.communicate(timeout=15)
                self.assertEqual((host.returncode,join.returncode),(0,0),(hl+jl).decode())
                self.assertEqual((hl+jl).count(b'connection retained'),4)
            finally:
                for p in (host,join):
                    if p.poll() is None:p.kill();p.communicate()
