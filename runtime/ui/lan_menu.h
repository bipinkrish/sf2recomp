#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef union SDL_Event SDL_Event;
enum { LAN_MENU_UP=1, LAN_MENU_DOWN=2, LAN_MENU_CONFIRM=4, LAN_MENU_BACK=8 };
void lan_menu_publish(bool open); // guest thread: modal ownership, no managed pointers
bool lan_menu_capture(void);
unsigned lan_menu_requests(void);
bool lan_menu_handle_event(const SDL_Event *event); // SDL/window thread
void lan_menu_update_capture(void);
