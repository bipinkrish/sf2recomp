#include "constants/core.h"
// Reliable ordered lockstep for two locally rendered SF2 Sparring instances.
#include "lan.h"
#include "settings.h"
#include "ui/menu.h"
#include "version.h"
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
#include <ifaddrs.h>
#include <net/if.h>
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

enum { MAGIC=SF2RECOMP_LAN_MAGIC, VERSION=SF2RECOMP_LAN_PROTOCOL,
       HELLO=LAN_PACKET_HELLO, CONFIG=LAN_PACKET_CONFIG, FRAME=LAN_PACKET_FRAME,
       END=LAN_PACKET_END, READY=LAN_PACKET_READY, LOADED=LAN_PACKET_LOADED,
       HEADER=LAN_PACKET_HEADER, MAX_PAYLOAD=LAN_MAX_EVENTS*SF2C_LAN_N_2_D473 };
static const uint8_t tool_greeting[]=SF2RECOMP_LAN_GREETING;
static _Noreturn void fail(const char *why);
static void check_tool_greeting(const uint8_t *payload,unsigned bytes) {
    if(bytes!=sizeof tool_greeting || memcmp(payload,tool_greeting,sizeof tool_greeting))
        fail(SF2C_LAN_S_DIFFERENT_SF2RECOMP_TOOL_VER_9665);
}
// Change on incompatible simulation/input/initialization/hash-policy changes.
// Roster indices depend on both INI switches. Reject unlike rosters before
// exchanging menu indices, including with older binaries lacking these cells.
#define SIMULATION_ID (SF2RECOMP_LAN_SIMULATION | \
    ((uint64_t)g_settings.enable_shadow_in_sparring << SF2RECOMP_LAN_ROSTER_SHADOW_BIT) | \
    ((uint64_t)g_settings.enable_titan_in_sparring << SF2RECOMP_LAN_ROSTER_TITAN_BIT))
static LanSocket peer=BAD_SOCKET, listener=BAD_SOCKET;
static bool enabled;
static unsigned player;
static LanConfig config;
unsigned lan_local_addresses(char out[SF2C_LAN_N_8_2C62][SF2C_LAN_N_16_B17E]) {
    unsigned count=0;
#ifdef _WIN32
    char hostname[SF2C_LAN_N_256_51E8];struct addrinfo hints={0},*list=NULL;
    hints.ai_family=AF_INET;
    if(gethostname(hostname,sizeof hostname) || getaddrinfo(hostname,NULL,&hints,&list))return 0;
    for(struct addrinfo *item=list;item && count<SF2C_LAN_N_8_2C62;item=item->ai_next) {
        struct sockaddr_in *address=(struct sockaddr_in *)item->ai_addr;
#else
    struct ifaddrs *list=NULL;
    if(getifaddrs(&list))return 0;
    for(struct ifaddrs *item=list;item && count<SF2C_LAN_N_8_2C62;item=item->ifa_next) {
        if(!item->ifa_addr || item->ifa_addr->sa_family!=AF_INET || !(item->ifa_flags&IFF_UP))continue;
        struct sockaddr_in *address=(struct sockaddr_in *)item->ifa_addr;
#endif
        uint32_t ip=ntohl(address->sin_addr.s_addr);
        if(!ip || (ip>>SF2C_LAN_N_24_C235)==SF2C_LAN_N_127_922C)continue;
        char text[SF2C_LAN_N_16_B17E];if(!inet_ntop(AF_INET,&address->sin_addr,text,sizeof text))continue;
        unsigned i=0;while(i<count && strcmp(out[i],text))++i;
        if(i==count)strcpy(out[count++],text);
    }
#ifdef _WIN32
    freeaddrinfo(list);
#else
    freeifaddrs(list);
#endif
    return count;
}
static uint32_t next_tick, remote_next_tick;
static uint64_t history_hash[LAN_MAX_DELAY+1];
static uint32_t history_tick[LAN_MAX_DELAY+1];
static uint64_t bytes_sent, bytes_received;
static struct { LanEvent events[SF2C_LAN_N_2_D473][LAN_MAX_EVENTS]; unsigned count[SF2C_LAN_N_2_D473]; } pipeline[LAN_MAX_DELAY+1];

static bool gui_mode, gui_guard, gui_combat;
static int gui_status;
static char gui_error[SF2C_LAN_N_160_A512];
static jmp_buf gui_escape;
static void gui_restore(void);
static bool gui_ready_input(void);
void lan_close(void) {
    if(peer!=BAD_SOCKET) { close_socket(peer);peer=BAD_SOCKET; }
    if(listener!=BAD_SOCKET) { close_socket(listener);listener=BAD_SOCKET; }
}
static _Noreturn void fail(const char *why) {
    fprintf(stderr,SF2C_LAN_S_LAN_S_N_D37B,why);
    if(gui_mode && gui_guard) {
        snprintf(gui_error,sizeof gui_error,SF2C_LAN_S_S_0030,why);gui_status=LAN_GUI_ERROR;
        lan_close();enabled=false;gui_restore();longjmp(gui_escape,1);
    }
    lan_close();fflush(stderr);_Exit(SF2C_LAN_N_3_4E07);
}
static uint32_t number(const char *value,uint32_t max) {
    char *end;errno=0;unsigned long n=strtoul(value,&end,SF2C_LAN_N_10_4A44);
    if(!*value || *value==SF2C_LAN_CH_EMPTY_01F8 || *value==SF2C_LAN_CH_EMPTY_3604 || *end || errno || n>max)fail(SF2C_LAN_S_INVALID_LAN_NUMERIC_OPTION_FF5D);
    return (uint32_t)n;
}
bool lan_enabled(void) { return enabled; }
unsigned lan_player(void) { return player; }
const LanConfig *lan_config(void) { return &config; }
static void nonblocking(LanSocket socket) {
#ifdef _WIN32
    u_long one=1;if(ioctlsocket(socket,FIONBIO,&one))fail(SF2C_LAN_S_CANNOT_MAKE_SOCKET_NONBLOCKI_BB3A);
#else
    int f=fcntl(socket,F_GETFL,0);if(f<0 || fcntl(socket,F_SETFL,f|O_NONBLOCK))fail(SF2C_LAN_S_CANNOT_MAKE_SOCKET_NONBLOCKI_BB3A);
#endif
}
static uint64_t deadline(unsigned seconds) { return SDL_GetTicksNS()+(uint64_t)seconds*SF2C_LAN_N_1000000000ULL_E860; }
static void wait_socket(LanSocket socket,bool write,uint64_t until) {
    for(;;) {
        if(menu_quit_requested())fail(SF2C_LAN_S_SESSION_CANCELLED_4346);
        uint64_t now=SDL_GetTicksNS();if(now>=until)fail(SF2C_LAN_S_PEER_TIMED_OUT_COMBAT_STOPPE_368C);
        uint64_t remaining=(until-now)/SF2C_LAN_N_1000_4051;if(remaining>SF2C_LAN_N_100000_3BB7)remaining=SF2C_LAN_N_100000_3BB7;
        struct timeval tv={(long)(remaining/SF2C_LAN_N_1000000_6CCE),(long)(remaining%SF2C_LAN_N_1000000_6CCE)};
        fd_set set;FD_ZERO(&set);FD_SET(socket,&set);
        int n=select((int)socket+1,write?NULL:&set,write?&set:NULL,NULL,&tv);
        if(n>0)return;
        if(n<0 && !interrupted())fail(SF2C_LAN_S_SOCKET_WAIT_FAILED_B780);
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
        else if(n==0)fail(SF2C_LAN_S_PEER_DISCONNECTED_COMBAT_STO_2C08);
        else if(!would_block() && !interrupted())fail(SF2C_LAN_S_CONNECTION_FAILED_COMBAT_STO_453E);
    }
}
static void put(uint8_t *p,uint32_t n) { n=htonl(n);memcpy(p,&n,SF2C_LAN_N_4_4B22); }
static uint32_t get(const uint8_t *p) { uint32_t n;memcpy(&n,p,SF2C_LAN_N_4_4B22);return ntohl(n); }
static void send_packet(unsigned type,uint32_t tick,uint64_t hash,const uint8_t *payload,unsigned bytes,uint64_t until) {
    if(bytes>MAX_PAYLOAD)fail(SF2C_LAN_S_TOO_MANY_INPUT_EVENTS_IN_ONE_EBE3);
    uint8_t packet[HEADER+MAX_PAYLOAD];
    put(packet,MAGIC);put(packet+SF2C_LAN_N_4_4B22,VERSION);put(packet+SF2C_LAN_N_8_2C62,type);put(packet+SF2C_LAN_N_12_6B51,tick);
    put(packet+SF2C_LAN_N_16_B17E,bytes);put(packet+SF2C_LAN_N_20_F5CA,(uint32_t)(hash>>SF2C_LAN_N_32_E29C));put(packet+SF2C_LAN_N_24_C235,(uint32_t)hash);put(packet+SF2C_LAN_N_28_59E1,player);
    if(bytes)memcpy(packet+HEADER,payload,bytes);
    transfer(packet,HEADER+bytes,true,until);
}
static unsigned receive_packet(unsigned type,uint32_t tick,uint64_t hash,uint8_t payload[MAX_PAYLOAD],uint64_t until) {
    uint8_t h[HEADER];transfer(h,sizeof h,false,until);
    if(get(h+SF2C_LAN_N_4_4B22)!=VERSION)fail(SF2C_LAN_S_DIFFERENT_SF2RECOMP_LAN_PROT_C428);
    if(get(h)!=MAGIC || get(h+SF2C_LAN_N_8_2C62)!=type || get(h+SF2C_LAN_N_12_6B51)!=tick || get(h+SF2C_LAN_N_28_59E1)!=1-player)
        fail(SF2C_LAN_S_INCOMPATIBLE_PEER_PACKET_TYP_67D2);
    unsigned bytes=get(h+SF2C_LAN_N_16_B17E);if(bytes>MAX_PAYLOAD)fail(SF2C_LAN_S_OVERSIZED_PEER_PACKET_86F1);
    uint64_t remote=(uint64_t)get(h+SF2C_LAN_N_20_F5CA)<<SF2C_LAN_N_32_E29C|get(h+SF2C_LAN_N_24_C235);
    if(remote!=hash) {
        fprintf(stderr,SF2C_LAN_S_LAN_DIVERGENCE_AT_STEP_U_LOC_2CD2 PRIx64 SF2C_LAN_S_REMOTE_016_C567 PRIx64 SF2C_LAN_S_N_D7A5,tick,hash,remote);
        fail(SF2C_LAN_S_COMBAT_STATE_SETUP_MISMATCH__A291);
    }
    if(bytes)transfer(payload,bytes,false,until);
    return bytes;
}
void lan_init(void) {
    const char *host=getenv(SF2C_LAN_S_SF2_LAN_HOST_36C9), *join=getenv(SF2C_LAN_S_SF2_LAN_JOIN_0B27);
    const char *port_option=getenv(SF2C_LAN_S_SF2_LAN_PORT_8363), *seed=getenv(SF2C_LAN_S_SF2_LAN_SEED_9C02);
    const char *match=getenv(SF2C_LAN_S_SF2_LAN_MATCH_23F8), *delay=getenv(SF2C_LAN_S_SF2_LAN_DELAY_71B5);
    if(!host && !join) {
        if(port_option || seed || match || delay)fail(SF2C_LAN_S_LAN_OPTIONS_REQUIRE_HOST_OR__504D);
        return;
    }
    if(enabled || (host && join))fail(SF2C_LAN_S_CHOOSE_ONE_LAN_ROLE_RESTART__5160);
    if(join && (seed || match || delay))fail(SF2C_LAN_S_THE_HOST_CHOOSES_THE_MATCH_S_3A57);
    enabled=true;player=join?1:0;
    uint32_t port=port_option?number(port_option,SF2C_LAN_N_65535_F2F8):SF2C_LAN_N_34250_67EB;if(!port)fail(SF2C_LAN_S_PORT_MUST_BE_POSITIVE_8198);
    config=(LanConfig){.seed=seed?number(seed,UINT32_MAX):(uint32_t)SDL_GetTicksNS(),.match={SF2C_LAN_N_17_4523,SF2C_LAN_N_18_4EC9,SF2C_LAN_N_13_3FDB},.delay=delay?number(delay,LAN_MAX_DELAY):SF2C_LAN_N_2_D473,.limit=getenv(SF2C_LAN_S_SF2_REPLAY_TICKS_1950)?number(getenv(SF2C_LAN_S_SF2_REPLAY_TICKS_1950),SF2C_LAN_N_1000000_6CCE):UINT32_MAX};
    if(!config.limit)fail(SF2C_LAN_S_STEP_LIMIT_MUST_BE_POSITIVE_CAD0);
    if(match) {
        char copy[SF2C_LAN_N_64_A68B];if(strlen(match)>=sizeof copy)fail(SF2C_LAN_S_INVALID_MATCH_SELECTION_013C);strcpy(copy,match);
        char *part=copy;
        for(unsigned i=0;i<SF2C_LAN_N_3_4E07;++i) { char *comma=strchr(part,SF2C_LAN_CH_EMPTY_EE1B);if((i<SF2C_LAN_N_2_D473)!=!!comma)fail(SF2C_LAN_S_MATCH_REQUIRES_FIGHTER_FIGHT_CFE1);if(comma)*comma=0;config.match[i]=number(part,SF2C_LAN_N_255_9556);part=comma?comma+1:part; }
    }
#ifdef _WIN32
    WSADATA wsa;if(WSAStartup(MAKEWORD(SF2C_LAN_N_2_D473,SF2C_LAN_N_2_D473),&wsa))fail(SF2C_LAN_S_CANNOT_INITIALIZE_WINDOWS_SO_842A);
#endif
    atexit(lan_close);
    struct sockaddr_in address={0};address.sin_family=AF_INET;address.sin_port=htons((uint16_t)port);
    if(inet_pton(AF_INET,join?join:host,&address.sin_addr)!=1)fail(SF2C_LAN_S_USE_A_NUMERIC_IPV4_ADDRESS_5296);
    if(host) {
        listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(listener==BAD_SOCKET)fail(SF2C_LAN_S_CANNOT_CREATE_LISTENER_EA33);
#ifndef _WIN32
        int one=1;setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,(const char *)&one,sizeof one);
#endif
        if(bind(listener,(struct sockaddr *)&address,sizeof address) || listen(listener,1))fail(SF2C_LAN_S_CANNOT_LISTEN_ON_REQUESTED_A_2E30);
        nonblocking(listener);fprintf(stderr,SF2C_LAN_S_LAN_HOSTING_ON_S_U_WAITING_F_79AA,host,port);
        uint64_t until=deadline(SF2C_LAN_N_120_2ABA);
        do { wait_socket(listener,false,until);peer=accept(listener,NULL,NULL); }while(peer==BAD_SOCKET && (would_block()||interrupted()));
        if(peer==BAD_SOCKET)fail(SF2C_LAN_S_CANNOT_ACCEPT_PEER_6A1A);close_socket(listener);listener=BAD_SOCKET;
    } else {
        fprintf(stderr,SF2C_LAN_S_LAN_CONNECTING_TO_S_U_AS_PLA_549C,join,port);
        uint64_t until=deadline(SF2C_LAN_N_120_2ABA);
        for(;;) {
            peer=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(peer==BAD_SOCKET)fail(SF2C_LAN_S_CANNOT_CREATE_CONNECTION_D2C2);nonblocking(peer);
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
            if(SDL_GetTicksNS()>=until)fail(SF2C_LAN_S_HOST_CONNECTION_TIMED_OUT_9468);SDL_Delay(SF2C_LAN_N_100_AD57);
        }
    }
    nonblocking(peer);int one=1;setsockopt(peer,IPPROTO_TCP,TCP_NODELAY,(const char *)&one,sizeof one);
#ifdef SO_NOSIGPIPE
    setsockopt(peer,SOL_SOCKET,SO_NOSIGPIPE,&one,sizeof one);
#endif
    uint8_t payload[MAX_PAYLOAD],chosen[SF2C_LAN_N_28_59E1];uint64_t until=deadline(SF2C_LAN_N_15_E629);
    send_packet(HELLO,0,SIMULATION_ID,tool_greeting,sizeof tool_greeting,until);
    check_tool_greeting(payload,receive_packet(HELLO,0,SIMULATION_ID,payload,until));
    if(!player) {
        put(chosen,config.seed);for(unsigned i=0;i<SF2C_LAN_N_3_4E07;++i)put(chosen+SF2C_LAN_N_4_4B22+i*SF2C_LAN_N_4_4B22,config.match[i]);put(chosen+SF2C_LAN_N_16_B17E,config.delay);put(chosen+SF2C_LAN_N_20_F5CA,SF2RECOMP_LAN_GAME_REVISION);put(chosen+SF2C_LAN_N_24_C235,config.limit);
        send_packet(CONFIG,0,SIMULATION_ID,chosen,sizeof chosen,until);
        if(receive_packet(CONFIG,0,SIMULATION_ID,payload,until)!=sizeof chosen || memcmp(payload,chosen,sizeof chosen))fail(SF2C_LAN_S_PEER_REJECTED_MATCH_CONFIGUR_86C0);
    } else {
        if(receive_packet(CONFIG,0,SIMULATION_ID,payload,until)!=sizeof chosen || get(payload+SF2C_LAN_N_20_F5CA)!=SF2RECOMP_LAN_GAME_REVISION)fail(SF2C_LAN_S_UNSUPPORTED_GAME_REVISION_CO_B6F3);
        config.seed=get(payload);for(unsigned i=0;i<SF2C_LAN_N_3_4E07;++i) { config.match[i]=get(payload+SF2C_LAN_N_4_4B22+i*SF2C_LAN_N_4_4B22);if(config.match[i]>SF2C_LAN_N_255_9556)fail(SF2C_LAN_S_INVALID_PEER_ROSTER_INDEX_D911); }
        config.delay=get(payload+SF2C_LAN_N_16_B17E);if(config.delay>LAN_MAX_DELAY)fail(SF2C_LAN_S_INVALID_PEER_INPUT_DELAY_FEEB);
        uint32_t remote_limit=get(payload+SF2C_LAN_N_24_C235);
        if(!remote_limit || (remote_limit>SF2C_LAN_N_1000000_6CCE && remote_limit!=UINT32_MAX))fail(SF2C_LAN_S_INVALID_PEER_STEP_LIMIT_35B5);
        if(getenv(SF2C_LAN_S_SF2_REPLAY_TICKS_1950) && config.limit!=remote_limit)fail(SF2C_LAN_S_HOST_JOIN_DIAGNOSTIC_STEP_LI_B204);
        config.limit=remote_limit;
        send_packet(CONFIG,0,SIMULATION_ID,payload,sizeof chosen,until);
    }
    g_settings.network_enabled=false;g_settings.ai_enabled=false;g_settings.show_intro=false;
    g_settings.player_setup=PLAYER_SETUP_SOLO;g_settings.unlock_all_sparring_characters=true;
    g_settings.developer_console=false;g_settings.developer_trainer=false;g_settings.developer_shop_cheats=false;
    fprintf(stderr,SF2C_LAN_S_LAN_CONNECTED_PLAYER_U_SEED__938E,player+1,config.seed,config.match[0],config.match[1],config.match[SF2C_LAN_N_2_D473],config.delay);
}
typedef struct { uint8_t outgoing[HEADER],incoming[HEADER];size_t sent,received;uint64_t until;bool complete; } LanBarrier;
static LanBarrier loading, readiness;
static bool barrier_poll(LanBarrier *barrier,unsigned type,uint64_t state) {
    if(gui_mode) { if(setjmp(gui_escape)) { gui_guard=false;return false; }gui_guard=true; }
    if(barrier->complete) { gui_guard=false;return true; }
    if(!barrier->until) {
        barrier->until=deadline(SF2C_LAN_N_120_2ABA);
        put(barrier->outgoing,MAGIC);put(barrier->outgoing+SF2C_LAN_N_4_4B22,VERSION);put(barrier->outgoing+SF2C_LAN_N_8_2C62,type);put(barrier->outgoing+SF2C_LAN_N_12_6B51,0);
        put(barrier->outgoing+SF2C_LAN_N_16_B17E,0);put(barrier->outgoing+SF2C_LAN_N_20_F5CA,(uint32_t)(state>>SF2C_LAN_N_32_E29C));put(barrier->outgoing+SF2C_LAN_N_24_C235,(uint32_t)state);put(barrier->outgoing+SF2C_LAN_N_28_59E1,player);
        fprintf(stderr,type==LOADED?SF2C_LAN_S_LAN_LOCAL_LOADING_COMPLETE_W_08AD:SF2C_LAN_S_LAN_COMBAT_PREPARED_WAITING__1D9D);
    }
    if(menu_quit_requested() || SDL_GetTicksNS()>=barrier->until)fail(SF2C_LAN_S_PEER_DID_NOT_BECOME_READY_4864);
    if(barrier->sent<HEADER) {
#ifdef MSG_NOSIGNAL
        int flags=MSG_NOSIGNAL;
#else
        int flags=0;
#endif
        int n=send(peer,(const char *)barrier->outgoing+barrier->sent,(int)(HEADER-barrier->sent),flags);
        if(n>0)barrier->sent+=(size_t)n;
        else if(n==0 || (!would_block() && !interrupted()))fail(SF2C_LAN_S_PEER_DISCONNECTED_DURING_REA_9B87);
    }
    if(gui_mode && !barrier->received && !gui_ready_input()) { gui_guard=false;return false; }
    if(barrier->received<HEADER) {
        int n=recv(peer,(char *)barrier->incoming+barrier->received,(int)(HEADER-barrier->received),0);
        if(n>0)barrier->received+=(size_t)n;
        else if(n==0 || (!would_block() && !interrupted()))fail(SF2C_LAN_S_PEER_DISCONNECTED_DURING_REA_9B87);
    }
    if(barrier->sent<HEADER || barrier->received<HEADER) { gui_guard=false;return false; }
    uint64_t remote=(uint64_t)get(barrier->incoming+SF2C_LAN_N_20_F5CA)<<SF2C_LAN_N_32_E29C|get(barrier->incoming+SF2C_LAN_N_24_C235);
    if(get(barrier->incoming)!=MAGIC || get(barrier->incoming+SF2C_LAN_N_4_4B22)!=VERSION || get(barrier->incoming+SF2C_LAN_N_8_2C62)!=type ||
       get(barrier->incoming+SF2C_LAN_N_12_6B51)!=0 || get(barrier->incoming+SF2C_LAN_N_16_B17E)!=0 || get(barrier->incoming+SF2C_LAN_N_28_59E1)!=1-player || remote!=state)
        fail(SF2C_LAN_S_INITIAL_COMBAT_STATE_SETUP_M_D8D5);
    bytes_sent+=HEADER;bytes_received+=HEADER;barrier->complete=true;
    fprintf(stderr,type==LOADED?SF2C_LAN_S_LAN_BOTH_PLAYERS_LOADED_STAR_8EB8:SF2C_LAN_S_LAN_BOTH_FIGHTERS_READY_STAR_5A12);
    gui_guard=false;return true;
}
bool lan_loaded_poll(void) { return barrier_poll(&loading,LOADED,0); }
bool lan_ready_poll(uint64_t state) { return barrier_poll(&readiness,READY,state); }
static void receive_frame(uint64_t until) {
    uint32_t expected=remote_next_tick;
    unsigned slot=expected%(LAN_MAX_DELAY+1),other=1-player;
    if(history_tick[slot]!=expected || expected>=next_tick+1)fail(SF2C_LAN_S_PEER_CHECKPOINT_OUTSIDE_THE__05DB);
    uint8_t payload[MAX_PAYLOAD];
    unsigned bytes=receive_packet(FRAME,expected,history_hash[slot],payload,until);
    if(bytes%SF2C_LAN_N_2_D473)fail(SF2C_LAN_S_MALFORMED_INPUT_BATCH_AB5F);
    pipeline[slot].count[other]=bytes/SF2C_LAN_N_2_D473;
    for(unsigned i=0;i<bytes/SF2C_LAN_N_2_D473;++i) {
        uint8_t code=payload[i*SF2C_LAN_N_2_D473],down=payload[i*SF2C_LAN_N_2_D473+1];
        if(code>SF2C_LAN_N_14_8527 || down>1)fail(SF2C_LAN_S_PEER_SENT_INVALID_FIGHTER_IN_E7D8);
        pipeline[slot].events[other][i]=(LanEvent){(uint8_t)other,code,down};
    }
    ++remote_next_tick;
}
unsigned lan_step(uint32_t tick,uint64_t state,const LanEvent *local,unsigned count,LanEvent out[LAN_MAX_EVENTS*SF2C_LAN_N_2_D473]) {
    while(!lan_ready_poll(state)) { if(gui_mode && !enabled)return 0;SDL_Delay(1); }
    if(gui_mode) { if(setjmp(gui_escape)) { gui_guard=false;return 0; }gui_guard=true; }
    if(!enabled || peer==BAD_SOCKET || tick!=next_tick || tick>=config.limit || count>LAN_MAX_EVENTS)fail(SF2C_LAN_S_INVALID_LOCAL_STEP_INPUT_BAT_ECB5);

    uint8_t sent[MAX_PAYLOAD];
    unsigned slot=tick%(LAN_MAX_DELAY+1);
    history_tick[slot]=tick;history_hash[slot]=state;pipeline[slot].count[player]=count;
    for(unsigned i=0;i<count;++i) {
        if(local[i].code>SF2C_LAN_N_14_8527 || local[i].down>1)fail(SF2C_LAN_S_INVALID_LOCAL_INPUT_5806);
        sent[i*SF2C_LAN_N_2_D473]=local[i].code;sent[i*SF2C_LAN_N_2_D473+1]=local[i].down;
        pipeline[slot].events[player][i]=local[i];pipeline[slot].events[player][i].player=(uint8_t)player;
    }
    uint64_t until=deadline(SF2C_LAN_N_15_E629);
    // Send inputs for tick+delay. Only wait for inputs needed NOW, captured
    // delay steps ago. Waiting for the future packet throttled both displays
    // to the slower peer's presentation rate despite a 60 Hz combat clock.
    send_packet(FRAME,tick,state,sent,count*SF2C_LAN_N_2_D473,until);
    unsigned total=0;
    if(tick>=config.delay) {
        uint32_t captured=tick-config.delay;
        while(remote_next_tick<=captured)receive_frame(until);
        unsigned ready_slot=captured%(LAN_MAX_DELAY+1);
        for(unsigned p=0;p<SF2C_LAN_N_2_D473;++p)for(unsigned i=0;i<pipeline[ready_slot].count[p];++i)
            out[total++]=pipeline[ready_slot].events[p][i];
    }
    ++next_tick;
    if(!tick || !(tick%SF2C_LAN_N_600_284B))fprintf(stderr,SF2C_LAN_S_LAN_SYNCHRONIZED_STEP_U_HASH_2788 PRIx64 SF2C_LAN_S_SENT_0A6E PRIu64 SF2C_LAN_S_RECEIVED_3B36 PRIu64 SF2C_LAN_S_BYTES_N_95EE,tick,state,bytes_sent,bytes_received);
    gui_guard=false;return total;
}
void lan_finish(uint32_t tick,uint64_t state) {
    if(gui_mode) { if(setjmp(gui_escape)) { gui_guard=false;return; }gui_guard=true; }
    uint8_t payload[MAX_PAYLOAD];uint64_t until=deadline(SF2C_LAN_N_15_E629);
    while(remote_next_tick<next_tick)receive_frame(until);
    send_packet(END,tick,state,NULL,0,until);
    if(receive_packet(END,tick,state,payload,until))fail(SF2C_LAN_S_INVALID_SESSION_END_C96E);
    fprintf(stderr,SF2C_LAN_S_LAN_COMPLETED_U_SYNCHRONIZED_6A2E PRIu64 SF2C_LAN_S_RECEIVED_3B36 PRIu64 SF2C_LAN_S_BYTES_N_95EE,tick,bytes_sent,bytes_received);
    if(gui_mode) { gui_combat=false;gui_guard=false; }else lan_close();
}


// The native browser shares this transport; no launcher/interpreter is involved.
enum { LOBBY=LAN_PACKET_LOBBY, MATCH=LAN_PACKET_MATCH, GUI_SIM_DELTA=SF2RECOMP_LAN_GUI_SIM_DELTA,
       DISCOVERY_PORT=SF2RECOMP_LAN_DISCOVERY_PORT, DISCOVERY_MAGIC=SF2RECOMP_LAN_DISCOVERY_MAGIC };
static bool gui_match;
static LanSocket discovery=BAD_SOCKET;
static uint64_t gui_until, query_at, gui_host_id;
static uint32_t menu_sent, menu_received, match_serial;
static LanHost discovered[SF2C_LAN_N_12_6B51];
static unsigned discovered_count;
static LanMenuEvent menu_queue[SF2C_LAN_N_128_2747];
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
    memset(&loading,0,sizeof loading);memset(&readiness,0,sizeof readiness);
    next_tick=remote_next_tick=0;memset(pipeline,0,sizeof pipeline);
    gui_restore();
}
static void gui_prepare(void) {
    lan_gui_leave();gui_mode=true;gui_error[0]=0;
#ifdef _WIN32
    static bool initialized;
    if(!initialized) { WSADATA wsa;if(WSAStartup(MAKEWORD(SF2C_LAN_N_2_D473,SF2C_LAN_N_2_D473),&wsa))fail(SF2C_LAN_S_CANNOT_INITIALIZE_LOCAL_NETW_46B2);initialized=true; }
#endif
    gui_previous_settings=g_settings;gui_settings_saved=true;
    g_settings.network_enabled=false;g_settings.ai_enabled=false;
    g_settings.player_setup=PLAYER_SETUP_SOLO;g_settings.unlock_all_sparring_characters=true;
    g_settings.developer_console=g_settings.developer_trainer=g_settings.developer_shop_cheats=false;
    config=(LanConfig){.seed=(uint32_t)SDL_GetTicksNS(),.match={SF2C_LAN_N_17_4523,SF2C_LAN_N_18_4EC9,SF2C_LAN_N_13_3FDB},.delay=SF2C_LAN_N_2_D473,.limit=UINT32_MAX};
    bytes_sent=bytes_received=0;
}
static LanSocket udp_socket(uint16_t port) {
    LanSocket s=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    if(s==BAD_SOCKET)fail(SF2C_LAN_S_CANNOT_OPEN_LAN_DISCOVERY_74B2);
    int one=1;
    setsockopt(s,SOL_SOCKET,SO_BROADCAST,(const char *)&one,sizeof one);
    struct sockaddr_in address={.sin_family=AF_INET,.sin_port=htons(port),.sin_addr={.s_addr=htonl(INADDR_ANY)}};
    if(bind(s,(struct sockaddr *)&address,sizeof address)) { close_socket(s);fail(SF2C_LAN_S_LAN_DISCOVERY_PORT_34251_IS__8676); }
    nonblocking(s);return s;
}
bool lan_gui_host(uint16_t port) {
    if(setjmp(gui_escape)) { gui_guard=false;return false; }gui_guard=true;
    gui_prepare();player=0;
    listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    if(listener==BAD_SOCKET)fail(SF2C_LAN_S_CANNOT_OPEN_A_LAN_HOST_044F);
#ifndef _WIN32
    int one=1;setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,(const char *)&one,sizeof one);
#endif
    struct sockaddr_in address={.sin_family=AF_INET,.sin_port=htons(port),.sin_addr={.s_addr=htonl(INADDR_ANY)}};
    if(bind(listener,(struct sockaddr *)&address,sizeof address) || listen(listener,1))fail(SF2C_LAN_S_CANNOT_HOST_TCP_PORT_34250_I_2FA2);
    nonblocking(listener);discovery=udp_socket(DISCOVERY_PORT);
    // Keep the advertised TCP port independently of the socket's local address.
    discovered[0].port=port;gui_host_id=SDL_GetTicksNS();
    gui_status=LAN_GUI_HOSTING;
    fprintf(stderr,SF2C_LAN_S_LAN_IN_GAME_HOST_READY_ON_TC_DDE8,port,DISCOVERY_PORT);
    gui_guard=false;return true;
}
bool lan_gui_search(void) {
    if(setjmp(gui_escape)) { gui_guard=false;return false; }gui_guard=true;
    gui_prepare();player=1;discovery=udp_socket(0);gui_status=LAN_GUI_SEARCHING;
    gui_guard=false;return true;
}
unsigned lan_gui_hosts(LanHost out[SF2C_LAN_N_12_6B51]) {
    unsigned count=0;uint64_t now=SDL_GetTicksNS();
    for(unsigned i=0;i<discovered_count;++i)if(now-discovered[i].seen<SF2C_LAN_N_5000000000ULL_3F32)out[count++]=discovered[i];
    return count;
}
bool lan_gui_join(const LanHost *host) {
    if(setjmp(gui_escape)) { gui_guard=false;return false; }gui_guard=true;
    peer=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(peer==BAD_SOCKET)fail(SF2C_LAN_S_CANNOT_OPEN_THE_HOST_CONNECT_75BF);
    nonblocking(peer);
    struct sockaddr_in address={.sin_family=AF_INET,.sin_port=htons(host->port)};
    if(inet_pton(AF_INET,host->address,&address.sin_addr)!=1)fail(SF2C_LAN_S_INVALID_DISCOVERED_HOST_ADDR_D31A);
    int rc=connect(peer,(struct sockaddr *)&address,sizeof address);
    if(rc && !would_block())fail(SF2C_LAN_S_THE_HOST_IS_NO_LONGER_AVAILA_3B98);
    gui_status=LAN_GUI_CONNECTING;gui_until=deadline(SF2C_LAN_N_10_4A44);
    gui_guard=false;return true;
}
static void gui_queue(LanMenuEvent event) {
    unsigned next=(menu_write+1)%SF2C_LAN_N_128_2747;
    if(next==menu_read)fail(SF2C_LAN_S_TOO_MANY_PENDING_SELECTION_I_1725);
    menu_queue[menu_write]=event;menu_write=next;
}
static bool gui_packet(uint8_t packet[HEADER+MAX_PAYLOAD]) {
    int n=recv(peer,(char *)packet,HEADER,MSG_PEEK);
    if(!n)fail(SF2C_LAN_S_THE_OTHER_PLAYER_LEFT_SPARRI_75E0);
    if(n<0) { if(would_block()||interrupted())return false;fail(SF2C_LAN_S_THE_LAN_CONNECTION_WAS_LOST_5E98); }
    if(n<HEADER)return false;
    if(get(packet)!=MAGIC || get(packet+SF2C_LAN_N_4_4B22)!=VERSION || get(packet+SF2C_LAN_N_28_59E1)!=1-player || get(packet+SF2C_LAN_N_16_B17E)>MAX_PAYLOAD)
        fail(SF2C_LAN_S_THE_OTHER_GAME_USES_AN_INCOM_F5EC);
    unsigned bytes=HEADER+get(packet+SF2C_LAN_N_16_B17E);
    // Never steal combat packets while the other machine is still loading.
    if(get(packet+SF2C_LAN_N_8_2C62)!=HELLO && get(packet+SF2C_LAN_N_8_2C62)!=CONFIG && get(packet+SF2C_LAN_N_8_2C62)!=LOBBY && get(packet+SF2C_LAN_N_8_2C62)!=MATCH)return false;
    n=recv(peer,(char *)packet,(int)bytes,MSG_PEEK);
    if(n<0) { if(would_block()||interrupted())return false;fail(SF2C_LAN_S_THE_LAN_CONNECTION_WAS_LOST_5E98); }
    if(n<(int)bytes)return false;
    transfer(packet,bytes,false,deadline(1));return true;
}
static void gui_discover(void) {
    if(discovery==BAD_SOCKET)return;
    uint8_t data[SF2C_LAN_N_96_7B1A];uint64_t now=SDL_GetTicksNS();
    if(gui_status==LAN_GUI_SEARCHING && now>=query_at) {
        memset(data,0,sizeof data);put(data,DISCOVERY_MAGIC);put(data+SF2C_LAN_N_4_4B22,VERSION);
        put(data+SF2C_LAN_N_8_2C62,(uint32_t)(SIMULATION_ID>>SF2C_LAN_N_32_E29C));put(data+SF2C_LAN_N_12_6B51,(uint32_t)SIMULATION_ID+GUI_SIM_DELTA);
        struct sockaddr_in address={.sin_family=AF_INET,.sin_port=htons(DISCOVERY_PORT),.sin_addr={.s_addr=htonl(INADDR_BROADCAST)}};
        sendto(discovery,(const char *)data,SF2C_LAN_N_16_B17E,0,(struct sockaddr *)&address,sizeof address);
        address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        sendto(discovery,(const char *)data,SF2C_LAN_N_16_B17E,0,(struct sockaddr *)&address,sizeof address);
        query_at=now+SF2C_LAN_N_1000000000ULL_E860;
    }
    for(unsigned attempt=0;attempt<SF2C_LAN_N_32_E29C;++attempt) {
        struct sockaddr_in sender;
#ifdef _WIN32
        int size=sizeof sender;
#else
        socklen_t size=sizeof sender;
#endif
        int n=recvfrom(discovery,(char *)data,sizeof data,0,(struct sockaddr *)&sender,&size);
        if(n<0) { if(would_block()||interrupted())break;fail(SF2C_LAN_S_LAN_DISCOVERY_FAILED_1A41); }
        if(n<SF2C_LAN_N_16_B17E || get(data)!=DISCOVERY_MAGIC || get(data+SF2C_LAN_N_4_4B22)!=VERSION ||
           get(data+SF2C_LAN_N_8_2C62)!=(uint32_t)(SIMULATION_ID>>SF2C_LAN_N_32_E29C) || get(data+SF2C_LAN_N_12_6B51)!=(uint32_t)SIMULATION_ID+GUI_SIM_DELTA)continue;
        if(gui_status==LAN_GUI_HOSTING && n==SF2C_LAN_N_16_B17E) {
            put(data+SF2C_LAN_N_16_B17E,discovered[0].port);memset(data+SF2C_LAN_N_20_F5CA,0,SF2C_LAN_N_64_A68B);
            if(gethostname((char *)data+SF2C_LAN_N_20_F5CA,SF2C_LAN_N_63_DA4E))strcpy((char *)data+SF2C_LAN_N_20_F5CA,SF2C_LAN_S_SHADOW_FIGHT_2_5A40);
            // Sanitize the advertised UI string; names never become markup.
            for(unsigned j=SF2C_LAN_N_20_F5CA;j<SF2C_LAN_N_83_BBB9 && data[j];++j)if(data[j]<SF2C_LAN_N_32_E29C || data[j]>SF2C_LAN_N_126_65A6 || data[j]==SF2C_LAN_CH_EMPTY_D13B || data[j]==SF2C_LAN_CH_EMPTY_4DE9)data[j]=SF2C_LAN_CH_EMPTY_BDC4;
            data[SF2C_LAN_N_83_BBB9]=0;put(data+SF2C_LAN_N_84_44C8,(uint32_t)(gui_host_id>>SF2C_LAN_N_32_E29C));put(data+SF2C_LAN_N_88_8B94,(uint32_t)gui_host_id);
            sendto(discovery,(const char *)data,SF2C_LAN_N_92_8241,0,(struct sockaddr *)&sender,size);
        } else if(gui_status==LAN_GUI_SEARCHING && n==SF2C_LAN_N_92_8241 && get(data+SF2C_LAN_N_16_B17E)>0 && get(data+SF2C_LAN_N_16_B17E)<=SF2C_LAN_N_65535_F2F8) {
            char ip[SF2C_LAN_N_16_B17E];if(!inet_ntop(AF_INET,&sender.sin_addr,ip,sizeof ip))continue;
            uint64_t id=(uint64_t)get(data+SF2C_LAN_N_84_44C8)<<SF2C_LAN_N_32_E29C|get(data+SF2C_LAN_N_88_8B94);
            if(!id)continue;
            unsigned i=0;
            for(;i<discovered_count;++i)if(discovered[i].id==id)break;
            if(i<discovered_count && strcmp(discovered[i].address,SF2C_LAN_S_127_0_0_1_8827) && !strcmp(ip,SF2C_LAN_S_127_0_0_1_8827)) { discovered[i].seen=now;continue; }
            if(i==SF2C_LAN_N_12_6B51)continue;
            if(i==discovered_count)++discovered_count;
            discovered[i]=(LanHost){.port=(uint16_t)get(data+SF2C_LAN_N_16_B17E),.id=id,.seen=now};
            strcpy(discovered[i].address,ip);
            for(unsigned j=0;j<SF2C_LAN_N_63_DA4E && data[SF2C_LAN_N_20_F5CA+j];++j) {
                unsigned char v=data[SF2C_LAN_N_20_F5CA+j];discovered[i].name[j]=(v>=SF2C_LAN_N_32_E29C && v<SF2C_LAN_N_127_922C && v!=SF2C_LAN_CH_EMPTY_D13B && v!=SF2C_LAN_CH_EMPTY_4DE9)?v:SF2C_LAN_CH_EMPTY_BDC4;
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
        if(peer==BAD_SOCKET) { if(!would_block() && !interrupted())fail(SF2C_LAN_S_CANNOT_ACCEPT_A_LAN_PLAYER_4682);gui_guard=false;return; }
        close_socket(listener);listener=BAD_SOCKET;gui_status=LAN_GUI_CONNECTING;gui_until=deadline(SF2C_LAN_N_10_4A44);
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
            if(getsockopt(peer,SOL_SOCKET,SO_ERROR,(char *)&error,&size) || error)fail(SF2C_LAN_S_THE_HOST_CONNECTION_WAS_REFU_D327);
            nonblocking(peer);int one=1;setsockopt(peer,IPPROTO_TCP,TCP_NODELAY,(const char *)&one,sizeof one);
#ifdef SO_NOSIGPIPE
            setsockopt(peer,SOL_SOCKET,SO_NOSIGPIPE,&one,sizeof one);
#endif
            send_packet(HELLO,0,SIMULATION_ID+GUI_SIM_DELTA,tool_greeting,sizeof tool_greeting,deadline(1));
            gui_status=LAN_GUI_HELLO;gui_until=deadline(SF2C_LAN_N_10_4A44);
        }
    }
    if(gui_status>=LAN_GUI_CONNECTING && gui_status<=LAN_GUI_CONFIG && SDL_GetTicksNS()>=gui_until)fail(SF2C_LAN_S_CONNECTION_TIMED_OUT_TRY_JOI_82AA);
    if(peer!=BAD_SOCKET && !gui_combat && gui_status>=LAN_GUI_HELLO) {
        uint8_t packet[HEADER+MAX_PAYLOAD];
        for(unsigned tries=0;tries<SF2C_LAN_N_64_A68B && gui_packet(packet);++tries) {
            unsigned type=get(packet+SF2C_LAN_N_8_2C62),bytes=get(packet+SF2C_LAN_N_16_B17E);
            uint64_t hash=(uint64_t)get(packet+SF2C_LAN_N_20_F5CA)<<SF2C_LAN_N_32_E29C|get(packet+SF2C_LAN_N_24_C235);
            uint8_t *p=packet+HEADER;
            if(gui_status==LAN_GUI_HELLO) {
                if(type!=HELLO || get(packet+SF2C_LAN_N_12_6B51) || hash!=SIMULATION_ID+GUI_SIM_DELTA)fail(SF2C_LAN_S_DIFFERENT_GAME_LAN_VERSIONS__2164);
                check_tool_greeting(p,bytes);
                if(!player) {
                    uint8_t cfg[SF2C_LAN_N_28_59E1];put(cfg,config.seed);
                    for(unsigned i=0;i<SF2C_LAN_N_3_4E07;++i)put(cfg+SF2C_LAN_N_4_4B22+i*SF2C_LAN_N_4_4B22,config.match[i]);
                    put(cfg+SF2C_LAN_N_16_B17E,config.delay);put(cfg+SF2C_LAN_N_20_F5CA,SF2RECOMP_LAN_GAME_REVISION);put(cfg+SF2C_LAN_N_24_C235,config.limit);
                    send_packet(CONFIG,0,SIMULATION_ID+GUI_SIM_DELTA,cfg,sizeof cfg,deadline(1));
                }
                gui_status=LAN_GUI_CONFIG;
            } else if(gui_status==LAN_GUI_CONFIG) {
                if(type!=CONFIG || bytes!=SF2C_LAN_N_28_59E1 || get(packet+SF2C_LAN_N_12_6B51) || hash!=SIMULATION_ID+GUI_SIM_DELTA ||
                   get(p+SF2C_LAN_N_20_F5CA)!=SF2RECOMP_LAN_GAME_REVISION || get(p+SF2C_LAN_N_16_B17E)>LAN_MAX_DELAY || get(p+SF2C_LAN_N_24_C235)!=UINT32_MAX)fail(SF2C_LAN_S_INCOMPATIBLE_MATCH_CONFIGURA_F2E2);
                if(player) { config.seed=get(p);for(unsigned i=0;i<SF2C_LAN_N_3_4E07;++i) { config.match[i]=get(p+SF2C_LAN_N_4_4B22+i*SF2C_LAN_N_4_4B22);if(config.match[i]>SF2C_LAN_N_255_9556)fail(SF2C_LAN_S_INVALID_ROSTER_INDEX_72CE); }config.delay=get(p+SF2C_LAN_N_16_B17E);send_packet(CONFIG,0,hash,p,bytes,deadline(1)); }
                else if(get(p)!=config.seed || get(p+SF2C_LAN_N_16_B17E)!=config.delay)fail(SF2C_LAN_S_THE_GUEST_REJECTED_THE_MATCH_8A8E);
                gui_status=LAN_GUI_CONNECTED;enabled=true;
                if(discovery!=BAD_SOCKET) { close_socket(discovery);discovery=BAD_SOCKET; }
                fprintf(stderr,SF2C_LAN_S_LAN_IN_GAME_CONNECTION_ESTAB_BCE9,player+1);
            } else if(type==LOBBY && bytes==SF2C_LAN_N_4_4B22 && get(packet+SF2C_LAN_N_12_6B51)==menu_received++ && hash==0 &&
                      p[0]<=SF2C_LAN_N_2_D473 && p[1]<=1 && ((p[SF2C_LAN_N_2_D473]<=SF2C_LAN_N_24_C235 && p[SF2C_LAN_N_3_4E07]<=SF2C_LAN_N_2_D473) || (p[0]<SF2C_LAN_N_2_D473 && p[1]==1 && p[SF2C_LAN_N_3_4E07]==SF2C_LAN_N_3_4E07))) {
                gui_queue((LanMenuEvent){1-player,p[0],p[1],p[SF2C_LAN_N_2_D473],p[SF2C_LAN_N_3_4E07]});
            } else if(type==MATCH && player && bytes==SF2C_LAN_N_16_B17E && get(packet+SF2C_LAN_N_12_6B51)==match_serial++ && hash==0) {
                config.seed=get(p);for(unsigned i=0;i<SF2C_LAN_N_3_4E07;++i) { config.match[i]=get(p+SF2C_LAN_N_4_4B22+i*SF2C_LAN_N_4_4B22);if(config.match[i]>SF2C_LAN_N_255_9556)fail(SF2C_LAN_S_INVALID_SELECTED_FIGHTER_OR__9257); }
                gui_match=true;break;
            } else fail(SF2C_LAN_S_INVALID_LAN_LOBBY_PACKET_EF22);
        }
    }
    gui_guard=false;
}
bool lan_gui_send_menu(unsigned module,unsigned down,unsigned event,unsigned input_type) {
    if(!enabled || gui_combat || gui_status!=LAN_GUI_CONNECTED)return false;
    if(setjmp(gui_escape)) { gui_guard=false;return false; }gui_guard=true;
    if(module>SF2C_LAN_N_2_D473 || down>1 || !((event<=SF2C_LAN_N_24_C235 && input_type<=SF2C_LAN_N_2_D473) || (module<SF2C_LAN_N_2_D473 && down==1 && event<=SF2C_LAN_N_255_9556 && input_type==SF2C_LAN_N_3_4E07)))fail(SF2C_LAN_S_INVALID_LOCAL_MENU_INPUT_4D85);
    uint8_t payload[SF2C_LAN_N_4_4B22]={module,down,event,input_type};
    send_packet(LOBBY,menu_sent++,0,payload,sizeof payload,deadline(1));
    gui_queue((LanMenuEvent){player,module,down,event,input_type});
    gui_guard=false;return true;
}
bool lan_gui_pop_menu(LanMenuEvent *event,unsigned module) {
    while(menu_read!=menu_write && (menu_queue[menu_read].module<module || (menu_queue[menu_read].module==SF2C_LAN_N_2_D473 && module!=SF2C_LAN_N_2_D473)))menu_read=(menu_read+1)%SF2C_LAN_N_128_2747;
    if(menu_read==menu_write || menu_queue[menu_read].module!=module)return false;
    *event=menu_queue[menu_read];menu_read=(menu_read+1)%SF2C_LAN_N_128_2747;return true;
}
bool lan_gui_send_match(const uint32_t match[SF2C_LAN_N_3_4E07]) {
    if(!enabled || player || gui_combat)return false;
    if(setjmp(gui_escape)) { gui_guard=false;return false; }gui_guard=true;
    uint8_t payload[SF2C_LAN_N_16_B17E];config.seed+=SF2RECOMP_LAN_REMATCH_SEED_STEP;put(payload,config.seed);
    for(unsigned i=0;i<SF2C_LAN_N_3_4E07;++i) { if(match[i]>SF2C_LAN_N_255_9556)fail(SF2C_LAN_S_UNSUPPORTED_SELECTED_ROSTER__55D6);config.match[i]=match[i];put(payload+SF2C_LAN_N_4_4B22+i*SF2C_LAN_N_4_4B22,match[i]); }
    send_packet(MATCH,match_serial++,0,payload,sizeof payload,deadline(1));
    gui_match=true;gui_guard=false;return true;
}
bool lan_gui_take_match(void) { bool value=gui_match;gui_match=false;return value; }
void lan_gui_begin_match(void) {
    gui_combat=true;next_tick=remote_next_tick=0;
    memset(&loading,0,sizeof loading);memset(&readiness,0,sizeof readiness);
    memset(pipeline,0,sizeof pipeline);menu_read=menu_write=0;
}

static bool gui_ready_input(void) {
    uint8_t packet[HEADER+MAX_PAYLOAD];
    for(unsigned i=0;i<SF2C_LAN_N_64_A68B;++i) {
        int n=recv(peer,(char *)packet,HEADER,MSG_PEEK);
        if(!n)fail(SF2C_LAN_S_THE_OTHER_PLAYER_LEFT_WHILE__CC0E);
        if(n<0) { if(would_block()||interrupted())return false;fail(SF2C_LAN_S_CONNECTION_LOST_WHILE_LOADIN_36BE); }
        if(n<HEADER)return false;
        if(get(packet+SF2C_LAN_N_8_2C62)!=LOBBY)return true;
        if(get(packet)!=MAGIC || get(packet+SF2C_LAN_N_4_4B22)!=VERSION || get(packet+SF2C_LAN_N_28_59E1)!=1-player ||
           get(packet+SF2C_LAN_N_12_6B51)!=menu_received || get(packet+SF2C_LAN_N_16_B17E)!=SF2C_LAN_N_4_4B22 || get(packet+SF2C_LAN_N_20_F5CA) || get(packet+SF2C_LAN_N_24_C235))
            fail(SF2C_LAN_S_INVALID_SELECTION_PACKET_AT__EB53);
        n=recv(peer,(char *)packet,HEADER+SF2C_LAN_N_4_4B22,MSG_PEEK);
        if(n<HEADER+SF2C_LAN_N_4_4B22)return false;
        transfer(packet,HEADER+SF2C_LAN_N_4_4B22,false,deadline(1));++menu_received;
    }
    return false;
}
