// Reliable ordered lockstep for two locally rendered SF2 Sparring instances.
#include "lan.h"
#include "settings.h"
#include "ui/menu.h"
#include <SDL3/SDL_timer.h>
#include <errno.h>
#include <setjmp.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET LanSocket;
#define BAD_SOCKET INVALID_SOCKET
#define close_socket closesocket
static bool would_block(void) { int e=WSAGetLastError(); return e==WSAEWOULDBLOCK || e==WSAEINPROGRESS || e==WSAEALREADY; }
static bool interrupted(void) { return WSAGetLastError()==WSAEINTR; }
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int LanSocket;
#define BAD_SOCKET (-1)
#define close_socket close
static bool would_block(void) { return errno==EAGAIN || errno==EWOULDBLOCK || errno==EINPROGRESS || errno==EALREADY; }
static bool interrupted(void) { return errno==EINTR; }
#endif

enum { MAGIC=0x5346324c, VERSION=1, HELLO=1, CONFIG=2, FRAME=3, END=4, READY=5, HEADER=32, MAX_PAYLOAD=LAN_MAX_EVENTS*2 };
// Change on incompatible simulation/input/initialization/hash-policy changes.
#define SIMULATION_ID UINT64_C(0x5346320100030004)
static LanSocket peer=BAD_SOCKET, listener=BAD_SOCKET;
static bool enabled;
static unsigned player;
static LanConfig config;
static uint32_t next_tick, remote_next_tick;
static uint64_t history_hash[LAN_MAX_DELAY+1];
static uint32_t history_tick[LAN_MAX_DELAY+1];
static uint64_t bytes_sent, bytes_received;
static struct { LanEvent events[2][LAN_MAX_EVENTS]; unsigned count[2]; } pipeline[LAN_MAX_DELAY+1];

static bool gui_mode, gui_guard, gui_combat;
static int gui_status;
static char gui_error[160];
static jmp_buf gui_escape;
static void gui_restore(void);
static bool gui_ready_input(void);
void lan_close(void) {
    if(peer!=BAD_SOCKET) { close_socket(peer);peer=BAD_SOCKET; }
    if(listener!=BAD_SOCKET) { close_socket(listener);listener=BAD_SOCKET; }
}
static _Noreturn void fail(const char *why) {
    fprintf(stderr,"[lan] %s\n",why);
    if(gui_mode && gui_guard) {
        snprintf(gui_error,sizeof gui_error,"%s",why);gui_status=LAN_GUI_ERROR;
        lan_close();enabled=false;gui_restore();longjmp(gui_escape,1);
    }
    lan_close();fflush(stderr);_Exit(3);
}
static uint32_t number(const char *value,uint32_t max) {
    char *end;errno=0;unsigned long n=strtoul(value,&end,10);
    if(!*value || *value=='-' || *value=='+' || *end || errno || n>max)fail("invalid LAN numeric option");
    return (uint32_t)n;
}
bool lan_enabled(void) { return enabled; }
unsigned lan_player(void) { return player; }
const LanConfig *lan_config(void) { return &config; }
static void nonblocking(LanSocket socket) {
#ifdef _WIN32
    u_long one=1;if(ioctlsocket(socket,FIONBIO,&one))fail("cannot make socket nonblocking");
#else
    int f=fcntl(socket,F_GETFL,0);if(f<0 || fcntl(socket,F_SETFL,f|O_NONBLOCK))fail("cannot make socket nonblocking");
#endif
}
static uint64_t deadline(unsigned seconds) { return SDL_GetTicksNS()+(uint64_t)seconds*1000000000ull; }
static void wait_socket(LanSocket socket,bool write,uint64_t until) {
    for(;;) {
        if(menu_quit_requested())fail("session cancelled");
        uint64_t now=SDL_GetTicksNS();if(now>=until)fail("peer timed out; combat stopped");
        uint64_t remaining=(until-now)/1000;if(remaining>100000)remaining=100000;
        struct timeval tv={(long)(remaining/1000000),(long)(remaining%1000000)};
        fd_set set;FD_ZERO(&set);FD_SET(socket,&set);
        int n=select((int)socket+1,write?NULL:&set,write?&set:NULL,NULL,&tv);
        if(n>0)return;
        if(n<0 && !interrupted())fail("socket wait failed");
    }
}
static void transfer(void *data,size_t bytes,bool sending,uint64_t until) {
    uint8_t *p=data;
    while(bytes) {
        wait_socket(peer,sending,until);
#ifdef MSG_NOSIGNAL
        int flags=sending?MSG_NOSIGNAL:0;
#else
        int flags=0;
#endif
        int n=sending?send(peer,(const char *)p,(int)bytes,flags):recv(peer,(char *)p,(int)bytes,0);
        if(n>0) { p+=n;bytes-=(size_t)n;if(sending)bytes_sent+=n;else bytes_received+=n; }
        else if(n==0)fail("peer disconnected; combat stopped");
        else if(!would_block() && !interrupted())fail("connection failed; combat stopped");
    }
}
static void put(uint8_t *p,uint32_t n) { n=htonl(n);memcpy(p,&n,4); }
static uint32_t get(const uint8_t *p) { uint32_t n;memcpy(&n,p,4);return ntohl(n); }
static void send_packet(unsigned type,uint32_t tick,uint64_t hash,const uint8_t *payload,unsigned bytes,uint64_t until) {
    if(bytes>MAX_PAYLOAD)fail("too many input events in one step");
    uint8_t packet[HEADER+MAX_PAYLOAD];
    put(packet,MAGIC);put(packet+4,VERSION);put(packet+8,type);put(packet+12,tick);
    put(packet+16,bytes);put(packet+20,(uint32_t)(hash>>32));put(packet+24,(uint32_t)hash);put(packet+28,player);
    if(bytes)memcpy(packet+HEADER,payload,bytes);
    transfer(packet,HEADER+bytes,true,until);
}
static unsigned receive_packet(unsigned type,uint32_t tick,uint64_t hash,uint8_t payload[MAX_PAYLOAD],uint64_t until) {
    uint8_t h[HEADER];transfer(h,sizeof h,false,until);
    if(get(h)!=MAGIC || get(h+4)!=VERSION || get(h+8)!=type || get(h+12)!=tick || get(h+28)!=1-player)
        fail("incompatible peer, packet type, role or step sequence");
    unsigned bytes=get(h+16);if(bytes>MAX_PAYLOAD)fail("oversized peer packet");
    uint64_t remote=(uint64_t)get(h+20)<<32|get(h+24);
    if(remote!=hash) {
        fprintf(stderr,"[lan] divergence at step %u: local %016" PRIx64 " remote %016" PRIx64 "\n",tick,hash,remote);
        fail("combat state/setup mismatch; refusing to continue");
    }
    if(bytes)transfer(payload,bytes,false,until);
    return bytes;
}
void lan_init(void) {
    const char *host=getenv("SF2_LAN_HOST"), *join=getenv("SF2_LAN_JOIN");
    const char *port_option=getenv("SF2_LAN_PORT"), *seed=getenv("SF2_LAN_SEED");
    const char *match=getenv("SF2_LAN_MATCH"), *delay=getenv("SF2_LAN_DELAY");
    if(!host && !join) {
        if(port_option || seed || match || delay)fail("LAN options require HOST or JOIN");
        return;
    }
    if(enabled || (host && join))fail("choose one LAN role; restart to reconnect");
    if(join && (seed || match || delay))fail("the host chooses the match, seed and input delay");
    enabled=true;player=join?1:0;
    uint32_t port=port_option?number(port_option,65535):34250;if(!port)fail("port must be positive");
    config=(LanConfig){.seed=seed?number(seed,UINT32_MAX):(uint32_t)SDL_GetTicksNS(),.match={17,18,13},.delay=delay?number(delay,LAN_MAX_DELAY):2,.limit=getenv("SF2_REPLAY_TICKS")?number(getenv("SF2_REPLAY_TICKS"),1000000):UINT32_MAX};
    if(!config.limit)fail("step limit must be positive");
    if(match) {
        char copy[64];if(strlen(match)>=sizeof copy)fail("invalid match selection");strcpy(copy,match);
        char *part=copy;
        for(unsigned i=0;i<3;++i) { char *comma=strchr(part,',');if((i<2)!=!!comma)fail("MATCH requires fighter,fighter,arena indices");if(comma)*comma=0;config.match[i]=number(part,255);part=comma?comma+1:part; }
    }
#ifdef _WIN32
    WSADATA wsa;if(WSAStartup(MAKEWORD(2,2),&wsa))fail("cannot initialize Windows sockets");
#endif
    atexit(lan_close);
    struct sockaddr_in address={0};address.sin_family=AF_INET;address.sin_port=htons((uint16_t)port);
    if(inet_pton(AF_INET,join?join:host,&address.sin_addr)!=1)fail("use a numeric IPv4 address");
    if(host) {
        listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(listener==BAD_SOCKET)fail("cannot create listener");
#ifndef _WIN32
        int one=1;setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,(const char *)&one,sizeof one);
#endif
        if(bind(listener,(struct sockaddr *)&address,sizeof address) || listen(listener,1))fail("cannot listen on requested address/port");
        nonblocking(listener);fprintf(stderr,"[lan] hosting on %s:%u; waiting for player 2\n",host,port);
        uint64_t until=deadline(120);
        do { wait_socket(listener,false,until);peer=accept(listener,NULL,NULL); }while(peer==BAD_SOCKET && (would_block()||interrupted()));
        if(peer==BAD_SOCKET)fail("cannot accept peer");close_socket(listener);listener=BAD_SOCKET;
    } else {
        fprintf(stderr,"[lan] connecting to %s:%u as player 2\n",join,port);
        uint64_t until=deadline(120);
        for(;;) {
            peer=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(peer==BAD_SOCKET)fail("cannot create connection");nonblocking(peer);
            if(!connect(peer,(struct sockaddr *)&address,sizeof address))break;
            if(would_block()) {
                wait_socket(peer,true,until);int error=0;
#ifdef _WIN32
                int size=sizeof error;
#else
                socklen_t size=sizeof error;
#endif
                if(!getsockopt(peer,SOL_SOCKET,SO_ERROR,(char *)&error,&size) && !error)break;
            }
            close_socket(peer);peer=BAD_SOCKET;
            if(SDL_GetTicksNS()>=until)fail("host connection timed out");SDL_Delay(100);
        }
    }
    nonblocking(peer);int one=1;setsockopt(peer,IPPROTO_TCP,TCP_NODELAY,(const char *)&one,sizeof one);
#ifdef SO_NOSIGPIPE
    setsockopt(peer,SOL_SOCKET,SO_NOSIGPIPE,&one,sizeof one);
#endif
    uint8_t payload[MAX_PAYLOAD],chosen[28];uint64_t until=deadline(15);
    send_packet(HELLO,0,SIMULATION_ID,NULL,0,until);
    if(receive_packet(HELLO,0,SIMULATION_ID,payload,until))fail("invalid greeting");
    if(!player) {
        put(chosen,config.seed);for(unsigned i=0;i<3;++i)put(chosen+4+i*4,config.match[i]);put(chosen+16,config.delay);put(chosen+20,0x00020000);put(chosen+24,config.limit);
        send_packet(CONFIG,0,SIMULATION_ID,chosen,sizeof chosen,until);
        if(receive_packet(CONFIG,0,SIMULATION_ID,payload,until)!=sizeof chosen || memcmp(payload,chosen,sizeof chosen))fail("peer rejected match configuration");
    } else {
        if(receive_packet(CONFIG,0,SIMULATION_ID,payload,until)!=sizeof chosen || get(payload+20)!=0x00020000)fail("unsupported game revision/configuration");
        config.seed=get(payload);for(unsigned i=0;i<3;++i) { config.match[i]=get(payload+4+i*4);if(config.match[i]>255)fail("invalid peer roster index"); }
        config.delay=get(payload+16);if(config.delay>LAN_MAX_DELAY)fail("invalid peer input delay");
        uint32_t remote_limit=get(payload+24);
        if(!remote_limit || (remote_limit>1000000 && remote_limit!=UINT32_MAX))fail("invalid peer step limit");
        if(getenv("SF2_REPLAY_TICKS") && config.limit!=remote_limit)fail("host/join diagnostic step limits differ");
        config.limit=remote_limit;
        send_packet(CONFIG,0,SIMULATION_ID,payload,sizeof chosen,until);
    }
    g_settings.network_enabled=false;g_settings.ai_enabled=false;g_settings.show_intro=false;
    g_settings.player_setup=PLAYER_SETUP_SOLO;g_settings.unlock_all_sparring_characters=true;
    g_settings.developer_console=false;g_settings.developer_trainer=false;g_settings.developer_shop_cheats=false;
    fprintf(stderr,"[lan] connected: player %u, seed %u, match %u,%u,%u, delay %u steps\n",player+1,config.seed,config.match[0],config.match[1],config.match[2],config.delay);
}
static bool ready;
static struct { uint8_t outgoing[HEADER],incoming[HEADER];size_t sent,received;uint64_t until; } readiness;
bool lan_ready_poll(uint64_t state) {
    if(gui_mode) { if(setjmp(gui_escape)) { gui_guard=false;return false; }gui_guard=true; }
    if(ready) { gui_guard=false;return true; }
    if(!readiness.until) {
        readiness.until=deadline(120);
        put(readiness.outgoing,MAGIC);put(readiness.outgoing+4,VERSION);put(readiness.outgoing+8,READY);put(readiness.outgoing+12,0);
        put(readiness.outgoing+16,0);put(readiness.outgoing+20,(uint32_t)(state>>32));put(readiness.outgoing+24,(uint32_t)state);put(readiness.outgoing+28,player);
        fprintf(stderr,"[lan] combat prepared; waiting for peer readiness\n");
    }
    if(menu_quit_requested() || SDL_GetTicksNS()>=readiness.until)fail("peer did not become ready");
    if(readiness.sent<HEADER) {
#ifdef MSG_NOSIGNAL
        int flags=MSG_NOSIGNAL;
#else
        int flags=0;
#endif
        int n=send(peer,(const char *)readiness.outgoing+readiness.sent,(int)(HEADER-readiness.sent),flags);
        if(n>0)readiness.sent+=(size_t)n;
        else if(n==0 || (!would_block() && !interrupted()))fail("peer disconnected during readiness");
    }
    if(gui_mode && !readiness.received && !gui_ready_input()) { gui_guard=false;return false; }
    if(readiness.received<HEADER) {
        int n=recv(peer,(char *)readiness.incoming+readiness.received,(int)(HEADER-readiness.received),0);
        if(n>0)readiness.received+=(size_t)n;
        else if(n==0 || (!would_block() && !interrupted()))fail("peer disconnected during readiness");
    }
    if(readiness.sent<HEADER || readiness.received<HEADER) { gui_guard=false;return false; }
    uint64_t remote=(uint64_t)get(readiness.incoming+20)<<32|get(readiness.incoming+24);
    if(get(readiness.incoming)!=MAGIC || get(readiness.incoming+4)!=VERSION || get(readiness.incoming+8)!=READY ||
       get(readiness.incoming+12)!=0 || get(readiness.incoming+16)!=0 || get(readiness.incoming+28)!=1-player || remote!=state)
        fail("initial combat state/setup mismatch");
    bytes_sent+=HEADER;bytes_received+=HEADER;ready=true;
    fprintf(stderr,"[lan] both fighters ready; starting synchronized combat\n");
    gui_guard=false;return true;
}
static void receive_frame(uint64_t until) {
    uint32_t expected=remote_next_tick;
    unsigned slot=expected%(LAN_MAX_DELAY+1),other=1-player;
    if(history_tick[slot]!=expected || expected>=next_tick+1)fail("peer checkpoint outside the input window");
    uint8_t payload[MAX_PAYLOAD];
    unsigned bytes=receive_packet(FRAME,expected,history_hash[slot],payload,until);
    if(bytes%2)fail("malformed input batch");
    pipeline[slot].count[other]=bytes/2;
    for(unsigned i=0;i<bytes/2;++i) {
        uint8_t code=payload[i*2],down=payload[i*2+1];
        if(code>14 || down>1)fail("peer sent invalid fighter input");
        pipeline[slot].events[other][i]=(LanEvent){(uint8_t)other,code,down};
    }
    ++remote_next_tick;
}
unsigned lan_step(uint32_t tick,uint64_t state,const LanEvent *local,unsigned count,LanEvent out[LAN_MAX_EVENTS*2]) {
    while(!lan_ready_poll(state)) { if(gui_mode && !enabled)return 0;SDL_Delay(1); }
    if(gui_mode) { if(setjmp(gui_escape)) { gui_guard=false;return 0; }gui_guard=true; }
    if(!enabled || peer==BAD_SOCKET || tick!=next_tick || tick>=config.limit || count>LAN_MAX_EVENTS)fail("invalid local step/input batch");

    uint8_t sent[MAX_PAYLOAD];
    unsigned slot=tick%(LAN_MAX_DELAY+1);
    history_tick[slot]=tick;history_hash[slot]=state;pipeline[slot].count[player]=count;
    for(unsigned i=0;i<count;++i) {
        if(local[i].code>14 || local[i].down>1)fail("invalid local input");
        sent[i*2]=local[i].code;sent[i*2+1]=local[i].down;
        pipeline[slot].events[player][i]=local[i];pipeline[slot].events[player][i].player=(uint8_t)player;
    }
    uint64_t until=deadline(15);
    // Send inputs for tick+delay. Only wait for inputs needed NOW, captured
    // delay steps ago. Waiting for the future packet throttled both displays
    // to the slower peer's presentation rate despite a 60 Hz combat clock.
    send_packet(FRAME,tick,state,sent,count*2,until);
    unsigned total=0;
    if(tick>=config.delay) {
        uint32_t captured=tick-config.delay;
        while(remote_next_tick<=captured)receive_frame(until);
        unsigned ready_slot=captured%(LAN_MAX_DELAY+1);
        for(unsigned p=0;p<2;++p)for(unsigned i=0;i<pipeline[ready_slot].count[p];++i)
            out[total++]=pipeline[ready_slot].events[p][i];
    }
    ++next_tick;
    if(!tick || !(tick%600))fprintf(stderr,"[lan] synchronized step %u, hash %016" PRIx64 ", sent %" PRIu64 " received %" PRIu64 " bytes\n",tick,state,bytes_sent,bytes_received);
    gui_guard=false;return total;
}
void lan_finish(uint32_t tick,uint64_t state) {
    if(gui_mode) { if(setjmp(gui_escape)) { gui_guard=false;return; }gui_guard=true; }
    uint8_t payload[MAX_PAYLOAD];uint64_t until=deadline(15);
    while(remote_next_tick<next_tick)receive_frame(until);
    send_packet(END,tick,state,NULL,0,until);
    if(receive_packet(END,tick,state,payload,until))fail("invalid session end");
    fprintf(stderr,"[lan] completed %u synchronized combat steps; sent %" PRIu64 " received %" PRIu64 " bytes\n",tick,bytes_sent,bytes_received);
    if(gui_mode) { gui_combat=false;gui_guard=false; }else lan_close();
}


// The native browser shares this transport; no launcher/interpreter is involved.
enum { LOBBY=6, MATCH=7, GUI_SIM_DELTA=0x1000, DISCOVERY_PORT=34251, DISCOVERY_MAGIC=0x53463244 };
static bool gui_match;
static LanSocket discovery=BAD_SOCKET;
static uint64_t gui_until, query_at, gui_host_id;
static uint32_t menu_sent, menu_received, match_serial;
static LanHost discovered[12];
static unsigned discovered_count;
static LanMenuEvent menu_queue[128];
static unsigned menu_read,menu_write;
static Settings gui_previous_settings;
static bool gui_settings_saved;

bool lan_gui_active(void) { return gui_mode; }
int lan_gui_status(void) { return gui_status; }
const char *lan_gui_error(void) { return gui_error; }
static void gui_restore(void) {
    if(!gui_settings_saved)return;
    g_settings.network_enabled=gui_previous_settings.network_enabled;
    g_settings.ai_enabled=gui_previous_settings.ai_enabled;
    g_settings.show_intro=gui_previous_settings.show_intro;
    g_settings.player_setup=gui_previous_settings.player_setup;
    g_settings.unlock_all_sparring_characters=gui_previous_settings.unlock_all_sparring_characters;
    g_settings.developer_console=gui_previous_settings.developer_console;
    g_settings.developer_trainer=gui_previous_settings.developer_trainer;
    g_settings.developer_shop_cheats=gui_previous_settings.developer_shop_cheats;
    gui_settings_saved=false;
}
void lan_gui_leave(void) {
    lan_close();
    if(discovery!=BAD_SOCKET) { close_socket(discovery);discovery=BAD_SOCKET; }
    enabled=gui_mode=gui_combat=gui_match=false;gui_status=LAN_GUI_OFF;
    menu_read=menu_write=menu_sent=menu_received=0;
    discovered_count=0;query_at=0;match_serial=0;
    memset(&readiness,0,sizeof readiness);ready=false;
    next_tick=remote_next_tick=0;memset(pipeline,0,sizeof pipeline);
    gui_restore();
}
static void gui_prepare(void) {
    lan_gui_leave();gui_mode=true;gui_error[0]=0;
#ifdef _WIN32
    static bool initialized;
    if(!initialized) { WSADATA wsa;if(WSAStartup(MAKEWORD(2,2),&wsa))fail("Cannot initialize local networking.");initialized=true; }
#endif
    gui_previous_settings=g_settings;gui_settings_saved=true;
    g_settings.network_enabled=false;g_settings.ai_enabled=false;
    g_settings.player_setup=PLAYER_SETUP_SOLO;g_settings.unlock_all_sparring_characters=true;
    g_settings.developer_console=g_settings.developer_trainer=g_settings.developer_shop_cheats=false;
    config=(LanConfig){.seed=(uint32_t)SDL_GetTicksNS(),.match={17,18,13},.delay=2,.limit=UINT32_MAX};
    bytes_sent=bytes_received=0;
}
static LanSocket udp_socket(uint16_t port) {
    LanSocket s=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    if(s==BAD_SOCKET)fail("Cannot open LAN discovery.");
    int one=1;
    setsockopt(s,SOL_SOCKET,SO_BROADCAST,(const char *)&one,sizeof one);
    struct sockaddr_in address={.sin_family=AF_INET,.sin_port=htons(port),.sin_addr={.s_addr=htonl(INADDR_ANY)}};
    if(bind(s,(struct sockaddr *)&address,sizeof address)) { close_socket(s);fail("LAN discovery port 34251 is already in use."); }
    nonblocking(s);return s;
}
bool lan_gui_host(uint16_t port) {
    if(setjmp(gui_escape)) { gui_guard=false;return false; }gui_guard=true;
    gui_prepare();player=0;
    listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    if(listener==BAD_SOCKET)fail("Cannot open a LAN host.");
#ifndef _WIN32
    int one=1;setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,(const char *)&one,sizeof one);
#endif
    struct sockaddr_in address={.sin_family=AF_INET,.sin_port=htons(port),.sin_addr={.s_addr=htonl(INADDR_ANY)}};
    if(bind(listener,(struct sockaddr *)&address,sizeof address) || listen(listener,1))fail("Cannot host: TCP port 34250 is already in use.");
    nonblocking(listener);discovery=udp_socket(DISCOVERY_PORT);
    // Keep the advertised TCP port independently of the socket's local address.
    discovered[0].port=port;gui_host_id=SDL_GetTicksNS();
    gui_status=LAN_GUI_HOSTING;
    fprintf(stderr,"[lan] in-game host ready on TCP %u; discovery UDP %u\n",port,DISCOVERY_PORT);
    gui_guard=false;return true;
}
bool lan_gui_search(void) {
    if(setjmp(gui_escape)) { gui_guard=false;return false; }gui_guard=true;
    gui_prepare();player=1;discovery=udp_socket(0);gui_status=LAN_GUI_SEARCHING;
    gui_guard=false;return true;
}
unsigned lan_gui_hosts(LanHost out[12]) {
    unsigned count=0;uint64_t now=SDL_GetTicksNS();
    for(unsigned i=0;i<discovered_count;++i)if(now-discovered[i].seen<5000000000ull)out[count++]=discovered[i];
    return count;
}
bool lan_gui_join(const LanHost *host) {
    if(setjmp(gui_escape)) { gui_guard=false;return false; }gui_guard=true;
    peer=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(peer==BAD_SOCKET)fail("Cannot open the host connection.");
    nonblocking(peer);
    struct sockaddr_in address={.sin_family=AF_INET,.sin_port=htons(host->port)};
    if(inet_pton(AF_INET,host->address,&address.sin_addr)!=1)fail("Invalid discovered host address.");
    int rc=connect(peer,(struct sockaddr *)&address,sizeof address);
    if(rc && !would_block())fail("The host is no longer available. Refresh the host list.");
    gui_status=LAN_GUI_CONNECTING;gui_until=deadline(10);
    gui_guard=false;return true;
}
static void gui_queue(LanMenuEvent event) {
    unsigned next=(menu_write+1)%128;
    if(next==menu_read)fail("Too many pending selection inputs.");
    menu_queue[menu_write]=event;menu_write=next;
}
static bool gui_packet(uint8_t packet[HEADER+MAX_PAYLOAD]) {
    int n=recv(peer,(char *)packet,HEADER,MSG_PEEK);
    if(!n)fail("The other player left Sparring.");
    if(n<0) { if(would_block()||interrupted())return false;fail("The LAN connection was lost."); }
    if(n<HEADER)return false;
    if(get(packet)!=MAGIC || get(packet+4)!=VERSION || get(packet+28)!=1-player || get(packet+16)>MAX_PAYLOAD)
        fail("The other game uses an incompatible LAN protocol.");
    unsigned bytes=HEADER+get(packet+16);
    // Never steal combat packets while the other machine is still loading.
    if(get(packet+8)!=HELLO && get(packet+8)!=CONFIG && get(packet+8)!=LOBBY && get(packet+8)!=MATCH)return false;
    n=recv(peer,(char *)packet,(int)bytes,MSG_PEEK);
    if(n<0) { if(would_block()||interrupted())return false;fail("The LAN connection was lost."); }
    if(n<(int)bytes)return false;
    transfer(packet,bytes,false,deadline(1));return true;
}
static void gui_discover(void) {
    if(discovery==BAD_SOCKET)return;
    uint8_t data[96];uint64_t now=SDL_GetTicksNS();
    if(gui_status==LAN_GUI_SEARCHING && now>=query_at) {
        memset(data,0,sizeof data);put(data,DISCOVERY_MAGIC);put(data+4,VERSION);
        put(data+8,(uint32_t)(SIMULATION_ID>>32));put(data+12,(uint32_t)SIMULATION_ID+GUI_SIM_DELTA);
        struct sockaddr_in address={.sin_family=AF_INET,.sin_port=htons(DISCOVERY_PORT),.sin_addr={.s_addr=htonl(INADDR_BROADCAST)}};
        sendto(discovery,(const char *)data,16,0,(struct sockaddr *)&address,sizeof address);
        address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        sendto(discovery,(const char *)data,16,0,(struct sockaddr *)&address,sizeof address);
        query_at=now+1000000000ull;
    }
    for(unsigned attempt=0;attempt<32;++attempt) {
        struct sockaddr_in sender;
#ifdef _WIN32
        int size=sizeof sender;
#else
        socklen_t size=sizeof sender;
#endif
        int n=recvfrom(discovery,(char *)data,sizeof data,0,(struct sockaddr *)&sender,&size);
        if(n<0) { if(would_block()||interrupted())break;fail("LAN discovery failed."); }
        if(n<16 || get(data)!=DISCOVERY_MAGIC || get(data+4)!=VERSION ||
           get(data+8)!=(uint32_t)(SIMULATION_ID>>32) || get(data+12)!=(uint32_t)SIMULATION_ID+GUI_SIM_DELTA)continue;
        if(gui_status==LAN_GUI_HOSTING && n==16) {
            put(data+16,discovered[0].port);memset(data+20,0,64);
            if(gethostname((char *)data+20,63))strcpy((char *)data+20,"Shadow Fight 2");
            // Sanitize the advertised UI string; names never become markup.
            for(unsigned j=20;j<83 && data[j];++j)if(data[j]<32 || data[j]>126 || data[j]=='<' || data[j]=='>')data[j]='_';
            data[83]=0;put(data+84,(uint32_t)(gui_host_id>>32));put(data+88,(uint32_t)gui_host_id);
            sendto(discovery,(const char *)data,92,0,(struct sockaddr *)&sender,size);
        } else if(gui_status==LAN_GUI_SEARCHING && n==92 && get(data+16)>0 && get(data+16)<=65535) {
            char ip[16];if(!inet_ntop(AF_INET,&sender.sin_addr,ip,sizeof ip))continue;
            uint64_t id=(uint64_t)get(data+84)<<32|get(data+88);
            if(!id)continue;
            unsigned i=0;
            for(;i<discovered_count;++i)if(discovered[i].id==id)break;
            if(i<discovered_count && strcmp(discovered[i].address,"127.0.0.1") && !strcmp(ip,"127.0.0.1")) { discovered[i].seen=now;continue; }
            if(i==12)continue;
            if(i==discovered_count)++discovered_count;
            discovered[i]=(LanHost){.port=(uint16_t)get(data+16),.id=id,.seen=now};
            strcpy(discovered[i].address,ip);
            for(unsigned j=0;j<63 && data[20+j];++j) {
                unsigned char v=data[20+j];discovered[i].name[j]=(v>=32 && v<127 && v!='<' && v!='>')?v:'_';
            }
        }
    }
}
void lan_gui_poll(void) {
    if(!gui_mode || gui_status==LAN_GUI_ERROR)return;
    if(setjmp(gui_escape)) { gui_guard=false;return; }gui_guard=true;
    gui_discover();
    if(gui_status==LAN_GUI_HOSTING) {
        peer=accept(listener,NULL,NULL);
        if(peer==BAD_SOCKET) { if(!would_block() && !interrupted())fail("Cannot accept a LAN player.");gui_guard=false;return; }
        close_socket(listener);listener=BAD_SOCKET;gui_status=LAN_GUI_CONNECTING;gui_until=deadline(10);
    }
    if(gui_status==LAN_GUI_CONNECTING) {
        fd_set set;FD_ZERO(&set);FD_SET(peer,&set);struct timeval zero={0};
        if(select((int)peer+1,NULL,&set,NULL,&zero)>0) {
            int error=0;
#ifdef _WIN32
            int size=sizeof error;
#else
            socklen_t size=sizeof error;
#endif
            if(getsockopt(peer,SOL_SOCKET,SO_ERROR,(char *)&error,&size) || error)fail("The host connection was refused.");
            nonblocking(peer);int one=1;setsockopt(peer,IPPROTO_TCP,TCP_NODELAY,(const char *)&one,sizeof one);
#ifdef SO_NOSIGPIPE
            setsockopt(peer,SOL_SOCKET,SO_NOSIGPIPE,&one,sizeof one);
#endif
            send_packet(HELLO,0,SIMULATION_ID+GUI_SIM_DELTA,NULL,0,deadline(1));
            gui_status=LAN_GUI_HELLO;gui_until=deadline(10);
        }
    }
    if(gui_status>=LAN_GUI_CONNECTING && gui_status<=LAN_GUI_CONFIG && SDL_GetTicksNS()>=gui_until)fail("Connection timed out. Try joining again.");
    if(peer!=BAD_SOCKET && !gui_combat && gui_status>=LAN_GUI_HELLO) {
        uint8_t packet[HEADER+MAX_PAYLOAD];
        for(unsigned tries=0;tries<64 && gui_packet(packet);++tries) {
            unsigned type=get(packet+8),bytes=get(packet+16);
            uint64_t hash=(uint64_t)get(packet+20)<<32|get(packet+24);
            uint8_t *p=packet+HEADER;
            if(gui_status==LAN_GUI_HELLO) {
                if(type!=HELLO || bytes || get(packet+12) || hash!=SIMULATION_ID+GUI_SIM_DELTA)fail("Different game/LAN versions. Update both games.");
                if(!player) {
                    uint8_t cfg[28];put(cfg,config.seed);
                    for(unsigned i=0;i<3;++i)put(cfg+4+i*4,config.match[i]);
                    put(cfg+16,config.delay);put(cfg+20,0x20000);put(cfg+24,config.limit);
                    send_packet(CONFIG,0,SIMULATION_ID+GUI_SIM_DELTA,cfg,sizeof cfg,deadline(1));
                }
                gui_status=LAN_GUI_CONFIG;
            } else if(gui_status==LAN_GUI_CONFIG) {
                if(type!=CONFIG || bytes!=28 || get(packet+12) || hash!=SIMULATION_ID+GUI_SIM_DELTA ||
                   get(p+20)!=0x20000 || get(p+16)>LAN_MAX_DELAY || get(p+24)!=UINT32_MAX)fail("Incompatible match configuration.");
                if(player) { config.seed=get(p);for(unsigned i=0;i<3;++i) { config.match[i]=get(p+4+i*4);if(config.match[i]>255)fail("Invalid roster index."); }config.delay=get(p+16);send_packet(CONFIG,0,hash,p,bytes,deadline(1)); }
                else if(get(p)!=config.seed || get(p+16)!=config.delay)fail("The guest rejected the match settings.");
                gui_status=LAN_GUI_CONNECTED;enabled=true;
                if(discovery!=BAD_SOCKET) { close_socket(discovery);discovery=BAD_SOCKET; }
                fprintf(stderr,"[lan] in-game connection established as player %u\n",player+1);
            } else if(type==LOBBY && bytes==4 && get(packet+12)==menu_received++ && hash==0 &&
                      p[0]<=2 && p[1]<=1 && p[2]<=24 && p[3]<=2) {
                gui_queue((LanMenuEvent){1-player,p[0],p[1],p[2],p[3]});
            } else if(type==MATCH && player && bytes==16 && get(packet+12)==match_serial++ && hash==0) {
                config.seed=get(p);for(unsigned i=0;i<3;++i) { config.match[i]=get(p+4+i*4);if(config.match[i]>255)fail("Invalid selected fighter or arena."); }
                gui_match=true;break;
            } else fail("Invalid LAN lobby packet.");
        }
    }
    gui_guard=false;
}
bool lan_gui_send_menu(unsigned module,unsigned down,unsigned event,unsigned input_type) {
    if(!enabled || gui_combat || gui_status!=LAN_GUI_CONNECTED)return false;
    if(setjmp(gui_escape)) { gui_guard=false;return false; }gui_guard=true;
    if(module>2 || down>1 || event>24 || input_type>2)fail("Invalid local menu input.");
    uint8_t payload[4]={module,down,event,input_type};
    send_packet(LOBBY,menu_sent++,0,payload,sizeof payload,deadline(1));
    gui_queue((LanMenuEvent){player,module,down,event,input_type});
    gui_guard=false;return true;
}
bool lan_gui_pop_menu(LanMenuEvent *event,unsigned module) {
    while(menu_read!=menu_write && (menu_queue[menu_read].module<module || (menu_queue[menu_read].module==2 && module!=2)))menu_read=(menu_read+1)%128;
    if(menu_read==menu_write || menu_queue[menu_read].module!=module)return false;
    *event=menu_queue[menu_read];menu_read=(menu_read+1)%128;return true;
}
bool lan_gui_send_match(const uint32_t match[3]) {
    if(!enabled || player || gui_combat)return false;
    if(setjmp(gui_escape)) { gui_guard=false;return false; }gui_guard=true;
    uint8_t payload[16];config.seed+=0x9e3779b9u;put(payload,config.seed);
    for(unsigned i=0;i<3;++i) { if(match[i]>255)fail("Unsupported selected roster index.");config.match[i]=match[i];put(payload+4+i*4,match[i]); }
    send_packet(MATCH,match_serial++,0,payload,sizeof payload,deadline(1));
    gui_match=true;gui_guard=false;return true;
}
bool lan_gui_take_match(void) { bool value=gui_match;gui_match=false;return value; }
void lan_gui_begin_match(void) {
    gui_combat=true;next_tick=remote_next_tick=0;
    memset(&readiness,0,sizeof readiness);ready=false;
    memset(pipeline,0,sizeof pipeline);menu_read=menu_write=0;
}

static bool gui_ready_input(void) {
    uint8_t packet[HEADER+MAX_PAYLOAD];
    for(unsigned i=0;i<64;++i) {
        int n=recv(peer,(char *)packet,HEADER,MSG_PEEK);
        if(!n)fail("The other player left while loading combat.");
        if(n<0) { if(would_block()||interrupted())return false;fail("Connection lost while loading combat."); }
        if(n<HEADER)return false;
        if(get(packet+8)!=LOBBY)return true;
        if(get(packet)!=MAGIC || get(packet+4)!=VERSION || get(packet+28)!=1-player ||
           get(packet+12)!=menu_received || get(packet+16)!=4 || get(packet+20) || get(packet+24))
            fail("Invalid selection packet at the combat boundary.");
        n=recv(peer,(char *)packet,HEADER+4,MSG_PEEK);
        if(n<HEADER+4)return false;
        transfer(packet,HEADER+4,false,deadline(1));++menu_received;
    }
    return false;
}
