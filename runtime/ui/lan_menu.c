// Native desktop input for the game's LAN dialogs. Managed navigation and
// callbacks are applied on the guest thread, never from SDL's window thread.
#include "lan_menu.h"
#include "mouse.h"
#include "../input_bind.h"
#include <SDL3/SDL.h>
#include <stdatomic.h>
static atomic_bool modal;
static atomic_int desktop_menu_navigation=INPUT_NAV_MENU;
static atomic_bool campaign_back;
void input_menu_navigation(InputNavigation context) {
    atomic_store(&desktop_menu_navigation,context);atomic_store(&campaign_back,false);
}
bool campaign_back_requested(void) { return atomic_exchange(&campaign_back,false); }
bool input_menu_navigation_active(void) { return atomic_load(&desktop_menu_navigation)!=INPUT_NAV_COMBAT; }
int input_menu_back_binding(void) {
    // Sparring uses B to undo equipment/Ready and Start to leave the scene.
    return atomic_load(&desktop_menu_navigation)==INPUT_NAV_SPARRING ? BIND_PLUS : BIND_B;
}
static atomic_uint requests;
static int axis_direction;
static bool trigger_held[2];
void lan_menu_publish(bool open) {
    if(atomic_exchange(&modal,open)!=open)atomic_store(&requests,0);
}
bool lan_menu_capture(void) { return atomic_load(&modal); }
unsigned lan_menu_requests(void) { return atomic_exchange(&requests,0); }
static void request(unsigned action) { atomic_fetch_or(&requests,action); }
bool lan_menu_handle_event(const SDL_Event *e) {
    if(!lan_menu_capture() && atomic_load(&desktop_menu_navigation)==INPUT_NAV_CAMPAIGN &&
       (e->type==SDL_EVENT_KEY_DOWN || e->type==SDL_EVENT_KEY_UP) &&
       e->key.scancode==SDL_SCANCODE_ESCAPE && bind_key_unassigned(SDL_SCANCODE_ESCAPE)) {
        if(e->type==SDL_EVENT_KEY_DOWN && !e->key.repeat)atomic_store(&campaign_back,true);
        return true;
    }
    if(!lan_menu_capture())return false;
    if(e->type==SDL_EVENT_KEY_DOWN || e->type==SDL_EVENT_GAMEPAD_BUTTON_DOWN || e->type==SDL_EVENT_GAMEPAD_AXIS_MOTION)
        menu_mouse_event(e); // keyboard/pad focus clears pointer hover like other menus
    if(e->type==SDL_EVENT_KEY_DOWN || e->type==SDL_EVENT_KEY_UP) {
        if(e->type==SDL_EVENT_KEY_UP)return true;
        SDL_Scancode key=e->key.scancode;
        if(bind_menu_key(key,BIND_B)) { if(!e->key.repeat)request(LAN_MENU_BACK); }
        else if(bind_menu_key(key,BIND_A)) { if(!e->key.repeat)request(LAN_MENU_CONFIRM); }
        else if(key==g_bind_key[BIND_UP] || key==g_bind_key[BIND_LS_UP] || key==SDL_SCANCODE_UP)request(LAN_MENU_UP);
        else if(key==g_bind_key[BIND_DOWN] || key==g_bind_key[BIND_LS_DOWN] || key==SDL_SCANCODE_DOWN)request(LAN_MENU_DOWN);
        return true;
    }
    if(e->type==SDL_EVENT_GAMEPAD_BUTTON_DOWN || e->type==SDL_EVENT_GAMEPAD_BUTTON_UP) {
        if(e->type==SDL_EVENT_GAMEPAD_BUTTON_UP)return true;
        int b=e->gbutton.button;
        if(b==g_bind_pad[BIND_B])request(LAN_MENU_BACK);
        else if(b==g_bind_pad[BIND_A])request(LAN_MENU_CONFIRM);
        else if(b==g_bind_pad[BIND_UP])request(LAN_MENU_UP);
        else if(b==g_bind_pad[BIND_DOWN])request(LAN_MENU_DOWN);
        return true;
    }
    if(e->type==SDL_EVENT_GAMEPAD_AXIS_MOTION) {
        if(e->gaxis.axis==SDL_GAMEPAD_AXIS_LEFTY) {
            int direction=e->gaxis.value < -16000 ? -1:e->gaxis.value >16000 ?1:0;
            if(direction && direction!=axis_direction)request(direction<0?LAN_MENU_UP:LAN_MENU_DOWN);
            axis_direction=direction;
        } else if(e->gaxis.axis==SDL_GAMEPAD_AXIS_LEFT_TRIGGER || e->gaxis.axis==SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) {
            unsigned which=e->gaxis.axis==SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
            bool down=e->gaxis.value>16000;int button=which?PADB_RTRIGGER:PADB_LTRIGGER;
            if(down && !trigger_held[which]) {
                if(g_bind_pad[BIND_A]==button)request(LAN_MENU_CONFIRM);
                else if(g_bind_pad[BIND_B]==button)request(LAN_MENU_BACK);
            }
            trigger_held[which]=down;
        }
        return true;
    }
    return false; // original Unity mouse raycasters continue handling clicks
}
void lan_menu_update_capture(void) {
    static bool previous;
    bool open=lan_menu_capture();
    if(open!=previous) {
        extern void input_reset(void);
        input_reset();previous=open;axis_direction=0;
        trigger_held[0]=trigger_held[1]=false;
    }
}
