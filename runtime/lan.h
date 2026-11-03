// Opt-in host transport; original game networking remains disabled.
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum { LAN_MAX_EVENTS = 64, LAN_MAX_DELAY = 6 };
typedef struct { uint32_t seed, match[3], delay, limit; } LanConfig;
typedef struct { uint8_t player, code, down; } LanEvent;
bool lan_enabled(void);
unsigned lan_player(void); // host 0, join 1; both use their local P1 controls
void lan_init(void);       // before guest threads; negotiate the host's config
const LanConfig *lan_config(void);
bool lan_loaded_poll(void); // both native loaders finished; release the fight intro
bool lan_ready_poll(uint64_t state); // nonblocking: keep rendering while the peer loads
unsigned lan_step(uint32_t tick, uint64_t state, const LanEvent *local, unsigned count, LanEvent out[LAN_MAX_EVENTS * 2]);
void lan_finish(uint32_t tick, uint64_t state);
void lan_close(void);

// In-game LAN browser/session API. Called only on the guest update thread.
enum { LAN_GUI_OFF, LAN_GUI_HOSTING, LAN_GUI_SEARCHING, LAN_GUI_CONNECTING,
       LAN_GUI_HELLO, LAN_GUI_CONFIG, LAN_GUI_CONNECTED, LAN_GUI_ERROR };
typedef struct { char address[16], name[64]; uint16_t port; uint64_t id, seen; } LanHost;
typedef struct { uint8_t player, module, down, event, input_type; } LanMenuEvent;
bool lan_gui_active(void);
int lan_gui_status(void);
const char *lan_gui_error(void);
bool lan_gui_host(uint16_t port);
unsigned lan_local_addresses(char out[8][16]);
bool lan_gui_search(void);
bool lan_gui_join(const LanHost *host);
void lan_gui_poll(void);
unsigned lan_gui_hosts(LanHost out[12]);
bool lan_gui_send_menu(unsigned module, unsigned down, unsigned event, unsigned input_type);
bool lan_gui_pop_menu(LanMenuEvent *event, unsigned module);
bool lan_gui_send_match(const uint32_t match[3]);
bool lan_gui_take_match(void);
void lan_gui_begin_match(void);
void lan_gui_leave(void);
