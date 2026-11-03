#include "../constants/ui.h"
#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef union SDL_Event SDL_Event;
enum { LAN_MENU_UP=1, LAN_MENU_DOWN=SF2C_LAN_MENU_N_2_D473, LAN_MENU_CONFIRM=SF2C_LAN_MENU_N_4_4B22, LAN_MENU_BACK=SF2C_LAN_MENU_N_8_2C62 };
void lan_menu_publish(bool open); // guest thread: modal ownership, no managed pointers
bool lan_menu_capture(void);
unsigned lan_menu_requests(void);
bool campaign_back_requested(void);
bool lan_menu_handle_event(const SDL_Event *event); // SDL/window thread
void lan_menu_update_capture(void);
