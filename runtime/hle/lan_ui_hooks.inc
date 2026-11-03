// Native Unity dialogs and Sparring lifecycle. All managed calls stay on the
// guest update thread; the shipping executable needs no Python or external UI.
#include "../lan.h"
#include "../lan_random.h"
#include "../ui/lan_menu.h"
#include <inttypes.h>

enum { LAN_UI_NONE, LAN_UI_MODE, LAN_UI_ROLE, LAN_UI_BROWSER, LAN_UI_WAIT, LAN_UI_ERROR };
enum { LAN_CHOICE_NORMAL=1, LAN_CHOICE_LAN, LAN_CHOICE_HOST, LAN_CHOICE_JOIN,
       LAN_CHOICE_REFRESH, LAN_CHOICE_LEAVE, LAN_CHOICE_BACK, LAN_CHOICE_HOST_BASE=100 };
static uint64_t lan_dialog,lan_menu_scene,lan_pvp_scene,lan_active_module,lan_end_screen;
static unsigned lan_phase,lan_module_kind,lan_action;
static bool lan_allow_entry,lan_native_call,lan_dialog_closing,lan_ui_pumping;
static bool lan_match_starting,lan_entered;
static LanHost lan_visible_hosts[12];
static unsigned lan_visible_count,lan_choice_count,lan_choice,lan_commands[14];
static uint64_t lan_back_owner,lan_selection_state;
static char lan_back_prompt[96];
static uint64_t lan_menu_prototype,lan_menu_template,lan_menu_buttons[14];
static uint64_t lan_menu_root,lan_main_root;
static uint64_t sparring_module;
static unsigned sparring_kind;
static int sparring_pressed=-1;


static void reset_lan_ui(void) {
    if(lan_gui_active()) { replay_gui_stop();lan_gui_leave(); }
    lan_menu_publish(false);lan_selection_state=0;lan_menu_prototype=lan_menu_template=sparring_module=0;
    memset(lan_menu_buttons,0,sizeof lan_menu_buttons);sparring_pressed=-1;
    lan_menu_root=lan_main_root=0;
    input_menu_navigation(true);
    lan_dialog=lan_menu_scene=lan_pvp_scene=lan_active_module=lan_end_screen=0;
    lan_phase=lan_module_kind=lan_action=lan_visible_count=0;
    lan_allow_entry=lan_native_call=lan_dialog_closing=lan_ui_pumping=lan_match_starting=lan_entered=false;
}
static void lan_update_back_prompt(Cpu *c) {
    if(!lan_dialog)return;
    uint64_t back=LD64(lan_dialog+0x50);if(!back)return;
    char key[64],text[96];
    if(input_any_gamepad_connected())
        snprintf(key,sizeof key,"%s",bind_pad_label(bind_prompt_host(BIND_B),bind_label_family()));
    else {
        const char *name=bind_key_label(g_bind_key[BIND_B]);
        snprintf(key,sizeof key,"%s",name && *name && strcmp(name,"(none)") ? name : "Unbound");
    }
    snprintf(text,sizeof text,"BACK [%s]",key);
    if(back==lan_back_owner && !strcmp(text,lan_back_prompt))return;
    lan_back_owner=back;snprintf(lan_back_prompt,sizeof lan_back_prompt,"%s",text);
    uint64_t value=guest_string(c,text);
    ST64(lan_dialog+0x88,value);ST8(lan_dialog+0x90,0);
    guest_invoke2(c,0x70011cbb80ull,back,value);
}
static uint64_t lan_type(Cpu *c,const char *name) {
    return guest_invoke2(c,0x70008ea5c0ull,0,guest_string(c,name));
}
static uint64_t lan_new(Cpu *c,const char *name) {
    uint64_t type=lan_type(c,name);
    return type?guest_invoke2(c,0x700039cb10ull,0,type):0;
}
// Clone the real main-menu item, including its kunai selection artwork and
// authored typography. Pointer callbacks are intercepted only for these clones,
// so none of the original main-menu listeners can run inside a LAN dialog.
static void lan_capture_menu_item(Cpu *c,uint64_t transform) {
    (void)c;lan_menu_prototype=transform;
}
static void lan_keep_menu_template(Cpu *c) {
    if(!lan_menu_prototype)return;
    if(!lan_menu_template) {
        uint64_t type=lan_type(c,"Nekki.SF2.GUI.GeneralMenu.MenuItemButton, Assembly-CSharp");
        uint64_t source=type?guest_invoke2(c,0x700121e790ull,lan_menu_prototype,type):0;
        if(!source)return;
        // Capture while the main menu still owns a live source. A disconnect
        // can be the first dialog, after Unity has destroyed that scene.
        lan_menu_template=guest_invoke4(c,0x70012333a0ull,0,source,0,0);
        if(!lan_menu_template)return;
        uint64_t object=guest_invoke2(c,0x700121e720ull,lan_menu_template,0);
        guest_invoke2(c,0x7001226480ull,object,0);
        guest_invoke2(c,0x70012339d0ull,0,object);
    }
}
static void lan_menu_row(Cpu *c,unsigned index,uint64_t row,const char *text,int size) {
    if(!lan_menu_template || !row)return;
    uint64_t prototype=lan_menu_template;
    uint64_t parent=guest_invoke2(c,0x700121e6b0ull,row,0);
    uint64_t item=guest_invoke4(c,0x70012333a0ull,0,prototype,parent,0);
    if(!item)return;
    lan_menu_buttons[index]=item;
    uint64_t empty=lan_new(c,"UnityEngine.Events.UnityEvent, UnityEngine");
    if(empty)for(unsigned offset=0x48;offset<=0x68;offset+=8)ST64(item+offset,empty);
    uint64_t rect=guest_invoke2(c,0x700121e6b0ull,item,0);
    guest_set_vec2(c,0x700123a050ull,rect,0,0);
    guest_set_vec2(c,0x700123a250ull,rect,1,1);
    guest_set_vec2(c,0x700123a550ull,rect,0,0);
    guest_set_vec2(c,0x700123a7d0ull,rect,0,0);
    uint64_t label=LD64(item+0x28),old_label=LD64(row+0x18),button=LD64(row+0x20),line=LD64(row+0x28);
    guest_invoke2(c,0x7000e73b90ull,label,1);
    guest_invoke2(c,0x7000e72180ull,label,size);
    guest_invoke2(c,0x7000e71230ull,label,guest_string(c,text));
    guest_invoke2(c,0x7000e70080ull,item,0);
    guest_invoke2(c,0x7000e6c080ull,item,0);
    if(old_label)guest_invoke2(c,GUEST_BEHAVIOUR_SET_ENABLED,old_label,0);
    if(button)guest_invoke2(c,GUEST_BEHAVIOUR_SET_ENABLED,button,0);
    if(line)guest_invoke2(c,GUEST_BEHAVIOUR_SET_ENABLED,line,0);
    uint64_t object=guest_invoke2(c,0x700121e720ull,item,0);
    guest_invoke2(c,0x7001226480ull,object,1);
}
static uint64_t lan_line(Cpu *c,const char *label,unsigned command) {
    uint64_t line=lan_new(c,"Nekki.SF2.Core.Utils.StoryDialogContent, Assembly-CSharp");
    if(!line)return 0;
    uint64_t empty=guest_string(c,"");
    ST64(line+0x10,guest_string(c,label));
    ST64(line+0x18,command?guest_string(c,label):empty);
    char url[48];snprintf(url,sizeof url,"SF2LAN/%u",command);
    ST64(line+0x20,guest_string(c,url));
    ST32(line+0x30,0x43160000); // 150 authored UI units
    ST32(line+0x34,80);ST32(line+0x48,UINT32_MAX);
    ST32(line+0x78,command?2:0);
    for(unsigned i=0;i<3;++i)ST32(line+0x7c+i*4,0x3e4ccccd);
    ST32(line+0x88,0x3f800000);
    return line;
}
static void lan_close_dialog(Cpu *c) {
    uint64_t dialog=lan_dialog;lan_dialog=0;lan_menu_publish(false);
    if(lan_menu_root) {
        uint64_t root=lan_menu_root;lan_menu_root=0;
        guest_invoke2(c,0x7001226480ull,root,0);
        guest_invoke2(c,0x7001233760ull,0,root); // Object.Destroy
        if(lan_main_root)guest_invoke2(c,0x7001226480ull,lan_main_root,1);
        lan_main_root=0;
    }
    memset(lan_menu_buttons,0,sizeof lan_menu_buttons);
    if(dialog) {
        lan_dialog_closing=true;guest_invoke2(c,0x7000cee250ull,dialog,0);lan_dialog_closing=false;
    }
}
static void lan_focus_choice(Cpu *c);
static bool lan_show(Cpu *c,unsigned phase,const char *title,const char *const labels[],const unsigned commands[],unsigned count) {
    lan_close_dialog(c);
    uint64_t info=lan_new(c,"Nekki.SF2.GUI.Dialogs.MultiLineScrollDialogInfo, Assembly-CSharp");
    uint64_t list=lan_new(c,"System.Collections.Generic.List`1[[Nekki.SF2.Core.Utils.StoryDialogContent, Assembly-CSharp]], mscorlib");
    uint64_t element=lan_type(c,"Nekki.SF2.Core.Utils.StoryDialogContent, Assembly-CSharp");
    uint64_t array=element?guest_invoke4(c,0x70006e6740ull,0,element,count,0):0;
    if(!info || !list || !array) { LOG("[lan-ui] cannot allocate native dialog\n");return false; }
    for(unsigned i=0;i<count;++i) {
        uint64_t line=lan_line(c,labels[i],commands[i]);
        if(!line)return false;
        ST64(array+0x20+i*8,line);
    }
    ST64(list+0x10,array);ST32(list+0x18,count);ST32(list+0x1c,1);
    ST64(info+0x10,list);ST64(info+0x18,guest_string(c,title));
    ST32(info+0x24,2); // native Cancel/back footer, independent of row actions
    ST64(info+0x28,guest_string(c,"OK"));ST64(info+0x38,guest_string(c,"BACK"));
    lan_dialog=guest_invoke4(c,0x7000cf1360ull,0,15,info,0);
    lan_phase=phase;
    lan_choice_count=count;memcpy(lan_commands,commands,count*sizeof commands[0]);lan_choice=0;
    while(lan_choice<count && !lan_commands[lan_choice])++lan_choice;
    lan_menu_publish(lan_dialog!=0);
    if(lan_dialog) {
        uint64_t scroll=LD64(lan_dialog+0xb8),slider=LD64(lan_dialog+0xd0);
        bool browser=phase==LAN_UI_BROWSER && count>4;
        // Reuse the native rows as fixed choices. Only a host list can scroll.
        if(scroll)guest_invoke2(c,0x70005d3190ull,scroll,browser);
        if(slider) {
            uint64_t object=guest_invoke2(c,0x700121e720ull,slider,0);
            if(object)guest_invoke2(c,0x7001226480ull,object,browser);
        }
        uint64_t back=LD64(lan_dialog+0x50);
        if(back) {
            guest_invoke2(c,0x70011cb2f0ull,back,1); // native B / remapped keyboard Back
            guest_invoke2(c,0x70011cba70ull,back,0);
            uint64_t hint=LD64(back+0x140);
            if(hint) {
                guest_invoke2(c,0x7000cd7120ull,hint,1); // HintID.B
                uint64_t object=guest_invoke2(c,0x700121e720ull,hint,0);
                if(object)guest_invoke2(c,0x7001226480ull,object,1);
                guest_invoke2(c,GUEST_BEHAVIOUR_SET_ENABLED,hint,1);
            }
        }
    }
    if(lan_dialog) {
        uint64_t rows=LD64(lan_dialog+0xe8),items=rows?LD64(rows+0x10):0;
        unsigned n=rows?LD32(rows+0x18):0;
        for(unsigned i=0;items && i<n && i<count;++i) {
            uint64_t row=LD64(items+0x20+i*8),label=row?LD64(row+0x18):0;
            if(commands[i] && lan_menu_template) {
                lan_menu_row(c,i,row,labels[i],commands[i]>=LAN_CHOICE_HOST_BASE?48:80);
            } else if(label) {
                // ScrollDialog rows normally treat Text as a localization key.
                // These dynamic host names and choices are literal UI strings.
                guest_invoke2(c,0x7000e73b90ull,label,1);
                guest_invoke2(c,0x7000e72180ull,label,commands[i]>=LAN_CHOICE_HOST_BASE?48:commands[i]?80:55);
            }
        }
    }
    lan_update_back_prompt(c);lan_focus_choice(c);
    if(lan_dialog) {
        uint64_t rows=LD64(lan_dialog+0xe8);
        LOG("[lan-ui] content prefab %" PRIx64 " rows %u, supplied %u\n",LD64(lan_dialog+0xe0),rows?LD32(rows+0x18):0,count);
    }
    LOG("[lan-ui] %s (%u choices), dialog %" PRIx64 "\n",title,count,lan_dialog);
    return lan_dialog!=0;
}
// Reuse the actual main-menu section, including its layout, typeface and
// kunai artwork. Only its choices change; the scene and background stay put.
static bool lan_submenu(Cpu *c,unsigned phase,const char *first,const char *second,
                        unsigned first_command,unsigned second_command) {
    lan_close_dialog(c);
    uint64_t lists=lan_menu_scene?LD64(lan_menu_scene+0x88):0;
    uint64_t array=lists?LD64(lists+0x10):0;
    uint64_t main=array && LD32(lists+0x18)?LD64(array+0x20):0;
    if(!main)return false;
    uint64_t rect=guest_invoke2(c,0x700121e6b0ull,main,0);
    uint64_t parent=rect?guest_invoke2(c,0x70015d8be0ull,rect,0):0;
    uint64_t clone=parent?guest_invoke4(c,0x70012333a0ull,0,main,parent,0):0;
    if(!clone)return false;
    lan_menu_root=guest_invoke2(c,0x700121e720ull,clone,0);
    uint64_t buttons=LD64(clone+0x18),items=buttons?LD64(buttons+0x10):0;
    unsigned count=buttons?LD32(buttons+0x18):0;
    if(!lan_menu_root || !items || count<3) { lan_close_dialog(c);return false; }
    const char *labels[]={first,second,"BACK"};
    unsigned commands[]={first_command,second_command,LAN_CHOICE_BACK};
    for(unsigned i=0;i<count;++i) {
        uint64_t button=LD64(items+0x20+i*8);
        uint64_t object=guest_invoke2(c,0x700121e720ull,button,0);
        guest_invoke2(c,0x7001226480ull,object,i<3);
        if(i>=3)continue;
        lan_menu_buttons[i]=button;lan_commands[i]=commands[i];
        uint64_t label=LD64(button+0x28);
        guest_invoke2(c,0x7000e73b90ull,label,1);
        guest_invoke2(c,0x7000e71230ull,label,guest_string(c,labels[i]));
    }
    lan_main_root=guest_invoke2(c,0x700121e720ull,main,0);
    guest_invoke2(c,0x7001226480ull,lan_main_root,0);
    guest_invoke2(c,0x7001226480ull,lan_menu_root,1);
    lan_phase=phase;lan_choice_count=3;lan_choice=0;
    lan_menu_publish(true);lan_focus_choice(c);
    LOG("[lan-ui] main-menu submenu %u\n",phase);
    return true;
}
static void lan_mode_dialog(Cpu *c) {
    lan_keep_menu_template(c);
    if(lan_submenu(c,LAN_UI_MODE,"NORMAL","LAN MULTIPLAYER",LAN_CHOICE_NORMAL,LAN_CHOICE_LAN))return;
    const char *labels[]={"NORMAL","LAN MULTIPLAYER"};unsigned cmds[]={LAN_CHOICE_NORMAL,LAN_CHOICE_LAN};
    lan_show(c,LAN_UI_MODE,"SPARRING",labels,cmds,2);
}
static void lan_role_dialog(Cpu *c) {
    if(lan_submenu(c,LAN_UI_ROLE,"HOST","JOIN",LAN_CHOICE_HOST,LAN_CHOICE_JOIN))return;
    const char *labels[]={"HOST","JOIN"};unsigned cmds[]={LAN_CHOICE_HOST,LAN_CHOICE_JOIN};
    lan_show(c,LAN_UI_ROLE,"LAN MULTIPLAYER",labels,cmds,2);
}
static void lan_browser(Cpu *c) {
    const char *labels[14];unsigned cmds[14],count=0;char names[12][128];
    lan_visible_count=lan_gui_hosts(lan_visible_hosts);
    if(!lan_visible_count) { labels[count]="Searching for hosts on your local network...";cmds[count++]=0; }
    for(unsigned i=0;i<lan_visible_count;++i) {
        snprintf(names[i],sizeof names[i],"%s - %s",lan_visible_hosts[i].name,lan_visible_hosts[i].address);
        labels[count]=names[i];cmds[count++]=LAN_CHOICE_HOST_BASE+i;
    }
    labels[count]="REFRESH";cmds[count++]=LAN_CHOICE_REFRESH;
    lan_show(c,LAN_UI_BROWSER,"JOIN LAN GAME",labels,cmds,count);
}
static void lan_wait_dialog(Cpu *c,const char *message) {
    const char *labels[]={message};unsigned cmds[]={0};
    lan_show(c,LAN_UI_WAIT,"LAN MULTIPLAYER",labels,cmds,1);
}
static void lan_host_wait_dialog(Cpu *c) {
    char addresses[8][16],message[320];unsigned count=lan_local_addresses(addresses);
    size_t used=snprintf(message,sizeof message,"Waiting for player 2...\nHost IP%s: ",count>1?"s":"");
    if(!count)snprintf(message+used,sizeof message-used,"unavailable. Check Wi-Fi.");
    for(unsigned i=0;i<count;++i)
        used+=snprintf(message+used,sizeof message-used,"%s%s",i?", ":"",addresses[i]);
    lan_wait_dialog(c,message);
}
static void lan_error_dialog(Cpu *c) {
    const char *labels[]={lan_gui_error(),"RETURN TO MENU"};unsigned cmds[]={0,LAN_CHOICE_LEAVE};
    lan_show(c,LAN_UI_ERROR,"LAN CONNECTION",labels,cmds,2);
}
static void lan_go(Cpu *c,unsigned screen) {
    uint64_t module=guest_invoke2(c,0x7000f48cf0ull,0,0);
    Cpu saved=*c;c->x[0]=module;c->x[1]=screen;c->x[2]=c->x[3]=c->x[5]=0;c->x[4]=screen!=7;
    guest_call(c,0x7000f48ef0ull);*c=saved;
}
static void lan_enter_pvp(Cpu *c) {
    lan_close_dialog(c);lan_phase=LAN_UI_NONE;lan_allow_entry=true;lan_entered=true;
    guest_invoke2(c,0x7000e6ed90ull,lan_menu_scene,0);lan_allow_entry=false;
}
static void lan_leave_ui(Cpu *c,bool go_main) {
    lan_close_dialog(c);replay_gui_stop();lan_gui_leave();
    rt_hook_features&=~RT_HOOK_REPLAY;
    lan_pvp_scene=lan_active_module=lan_end_screen=0;lan_entered=false;lan_phase=LAN_UI_NONE;
    LOG("[lan-ui] left Sparring; connection closed\n");
    if(go_main)lan_go(c,7);
}
static bool lan_random_name(uint64_t name) {
    static const uint16_t random_name[]={'r','a','n','d','o','m'};
    return name && LD32(name+0x10)==6 && !memcmp((void *)(uintptr_t)(name+0x14),random_name,sizeof random_name);
}
static uint32_t lan_concrete_index(uint64_t array,uint64_t *random_state) {
    unsigned n=array?LD64(array+0x18):0,count=0;uint32_t candidates[256];
    if(n>256)return UINT32_MAX;
    for(unsigned j=0;j<n;++j) {
        uint64_t item=LD64(array+0x20+j*8),name=item?LD64(item+0x18):0;
        if(name && !lan_random_name(name))candidates[count++]=j;
    }
    return count?candidates[lan_selection_bounded(random_state,count)]:UINT32_MAX;
}
static bool lan_host_match(Cpu *c) {
    uint64_t scene=lan_pvp_scene;
    if(!scene)return false;
    uint64_t list=guest_invoke2(c,0x7000cf9830ull,0,0),settings=list?LD64(list+0x18):0;
    uint32_t match[3];
    for(unsigned i=0;i<3;++i) {
        uint64_t array=settings?LD64(settings+(i==2?0x20:0x18)):0;
        uint64_t selected=LD64(scene+0xa0+i*8);unsigned n=array?LD64(array+0x18):0;
        match[i]=UINT32_MAX;
        for(unsigned j=0;j<n && j<256;++j) {
            uint64_t item=LD64(array+0x20+j*8),name=item?LD64(item+0x18):0;
            if(name && selected && LD32(name+0x10)==LD32(selected+0x10) &&
               !memcmp((void *)(uintptr_t)(name+0x14),(void *)(uintptr_t)(selected+0x14),LD32(name+0x10)*2)) { match[i]=j;break; }
        }
        if(match[i]==UINT32_MAX)return false;
        if(lan_random_name(LD64(scene+0xa0+i*8))) {
            // Roster order is not fixed: exclude every placeholder by identity.
            match[i]=lan_concrete_index(array,&lan_selection_state);
            if(match[i]==UINT32_MAX)return false;
        }
    }
    LOG("[lan-ui] host chose fighters %u/%u and arena %u\n",match[0],match[1],match[2]);
    return lan_gui_send_match(match);
}
static void lan_begin_native_match(Cpu *c) {
    if(!lan_pvp_scene)return;
    lan_close_dialog(c);lan_phase=LAN_UI_NONE;
    lan_gui_begin_match();replay_gui_start();rt_hook_features|=RT_HOOK_REPLAY;
    lan_match_starting=lan_native_call=true;
    guest_invoke2(c,0x7000d961f0ull,lan_pvp_scene,0);
    lan_native_call=false;lan_match_starting=false;lan_active_module=0;
}
static void lan_focus_choice(Cpu *c) {
    if(!lan_dialog && !lan_menu_root)return;
    for(unsigned i=0;i<lan_choice_count;++i)if(lan_menu_buttons[i])
        guest_invoke2(c,0x7000e6c080ull,lan_menu_buttons[i],i==lan_choice);
    if(lan_choice<lan_choice_count && lan_menu_buttons[lan_choice])return;
    uint64_t button=0;
    if(lan_choice==lan_choice_count)button=LD64(lan_dialog+0x50);
    else {
        uint64_t rows=LD64(lan_dialog+0xe8),items=rows?LD64(rows+0x10):0;
        if(items && lan_choice<LD32(rows+0x18)) {
            uint64_t row=LD64(items+0x20+lan_choice*8);button=row?LD64(row+0x20):0;
        }
    }
    if(button)guest_invoke2(c,0x70005d9e60ull,button,0);
}
static void lan_dialog_input(Cpu *c) {
    unsigned requests=lan_menu_requests();if((!lan_dialog && !lan_menu_root) || !requests)return;
    if(requests&LAN_MENU_BACK) { lan_action=LAN_CHOICE_BACK;return; }
    if(requests&(LAN_MENU_UP|LAN_MENU_DOWN)) {
        int direction=(requests&LAN_MENU_UP)?-1:1;
        unsigned slots=lan_choice_count+(lan_menu_root?0:1);
        do { lan_choice=(lan_choice+slots+direction)%slots; }
        while(lan_choice<lan_choice_count && !lan_commands[lan_choice]);
        lan_focus_choice(c);
    }
    if(requests&LAN_MENU_CONFIRM)lan_action=lan_choice<lan_choice_count?lan_commands[lan_choice]:LAN_CHOICE_BACK;
}
// Native table geometry (not hard-coded screen coordinates) maps a click to
// the same selection/confirmation methods used by the controller. LAN queues
// the absolute cell index; each peer applies it for the owning player.
static bool sparring_select_cell(Cpu *c,uint64_t module,unsigned kind,unsigned player,unsigned index) {
    uint64_t table=LD64(module+0x20),array=LD64(module+(kind?0x90:0x98));
    unsigned columns=LD32(module+0x5c),count=array?LD64(array+0x18):0;
    if(!table || !columns || index>=count || player>1)return false;
    if(LD8(module+0x58+player))return false; // already Ready
    uint64_t selector=LD64(table+0x18+player*8);if(!selector)return false;
    int dx=(int)(index%columns)-(int)LD32(selector+0x38);
    int dy=(int)(index/columns)-(int)LD32(selector+0x3c);
    // With footer legends hidden, tapping/clicking the selected cell again is
    // the same confirmation as A. A zero-distance MoveSelection would reset
    // the native equipment/confirmation phase instead of advancing it.
    if(dx || dy) {
        Cpu saved=*c;c->x[0]=module;c->x[1]=player;c->x[2]=(uint32_t)dx;c->x[3]=(uint32_t)dy;c->x[4]=1;c->x[5]=0;
        guest_call(c,kind?0x7000e46120ull:0x7000e43da0ull);*c=saved;
    }
    uint64_t input=guest_invoke2(c,player?0x7000d94e40ull:0x7000d94dd0ull,0,0);
    if(!input)return false;
    bool previous=lan_native_call;lan_native_call=true;
    guest_invoke4(c,kind?0x7000d98de0ull:0x7000e44df0ull,module,0,LD32(input+0x18),0);
    lan_native_call=previous;
    LOG("[mouse] Sparring %s player %u selected cell %u\n",kind?"arena":"character",player+1,index);
    return true;
}
static bool sparring_contains(Cpu *c,uint64_t rect,uint64_t camera,const MenuMouse *mouse) {
    Cpu saved=*c;c->x[0]=0;c->x[1]=rect;c->x[2]=camera;c->x[3]=0;
    memcpy(&c->v[0],&mouse->x,4);memcpy(&c->v[1],&mouse->y,4);
    guest_call(c,0x7000af9300ull);bool inside=c->x[0]!=0;*c=saved;return inside;
}
static void sparring_mouse_frame(Cpu *c,const MenuMouse *mouse) {
    if(!sparring_module || lan_dialog || developer_console_open()) { sparring_pressed=-1;return; }
    if(mouse->x<0 || mouse->y<0) { sparring_pressed=-1;return; }
    // Swipes need held/motion frames; wheel events have no button edges.
    if(!(mouse->held&1) && !(mouse->pressed&1) && !(mouse->released&1) && !mouse->scroll_y)return;
    uint64_t manager=guest_invoke2(c,0x7000cede90ull,0,0);
    if(manager && LD64(manager+0x40)) { sparring_pressed=-1;return; }
    uint64_t module=sparring_module,table=LD64(module+0x20),container=table?LD64(table+0x28):0;
    uint64_t array=LD64(module+(sparring_kind?0x90:0x98));unsigned count=array?LD64(array+0x18):0;
    if(!container || count>256)return;
    uint64_t type=lan_type(c,"UnityEngine.Canvas, UnityEngine");
    uint64_t canvas=type?guest_invoke2(c,0x700121ead0ull,container,type):0;
    uint64_t camera=canvas?guest_invoke2(c,0x7000af7250ull,canvas,0):0;
    if(!sparring_kind && table==sparring_view.table && sparring_roster_scroll_input(c,table,mouse,
            sparring_contains(c,LD64(table+0x30),camera,mouse))) {
        sparring_pressed=-1;return;
    }
    unsigned children=guest_invoke2(c,0x70015d9e00ull,container,0);
    int hit=-1;
    for(unsigned i=0;i<count && i<children;++i) {
        if(!sparring_kind && !sparring_roster_cell_visible(table,i))continue;
        uint64_t cell=guest_invoke2(c,0x70015da1f0ull,container,i);
        if(cell && sparring_contains(c,cell,camera,mouse)) { hit=i;break; }
    }
    if(mouse->pressed&1)sparring_pressed=hit;
    if(mouse->released&1) {
        int pressed=sparring_pressed;sparring_pressed=-1;
        if(hit<0 || pressed!=hit)return;
        if(lan_gui_active()) {
            if(lan_gui_status()==LAN_GUI_CONNECTED)lan_gui_send_menu(sparring_kind,1,hit,3);
        } else {
            unsigned player=sparring_kind?0:LD32(module+0xa0);
            if(guest_invoke2(c,0x7000d982c0ull,module,0))player=LD8(module+0x58)?1:0;
            sparring_select_cell(c,module,sparring_kind,player,hit);
        }
    }
}
static void lan_ui_pump(Cpu *c) {
    if(lan_ui_pumping)return;
    lan_ui_pumping=true;
    if(campaign_back_requested()) {
        uint64_t dialogs=guest_invoke2(c,0x7000cede90ull,0,0);
        uint64_t back=guest_invoke2(c,0x7000f0cce0ull,0,0);
        uint64_t controllers=back?LD64(back+0x18):0;
        if((dialogs && LD64(dialogs+0x40)) || (controllers && LD32(controllers+0x18)))
            guest_invoke2(c,0x7000f0cf40ull,back,0);
        else lan_go(c,7);
    }
    lan_update_back_prompt(c);
    lan_dialog_input(c);
    unsigned action=lan_action;lan_action=0;
    if(action) {
        if(action==LAN_CHOICE_NORMAL) { LOG("[lan-ui] original local Sparring selected\n");lan_enter_pvp(c); }
        else if(action==LAN_CHOICE_LAN)lan_role_dialog(c);
        else if(action==LAN_CHOICE_HOST) {
            if(lan_gui_host(34250)) {
                // One advancing selection stream per hosted session. Only the
                // host draws; MATCH sends the concrete choices to both peers.
                lan_selection_state=(uint64_t)lan_config()->seed^UINT64_C(0x53463253454c4543);
                lan_enter_pvp(c);
            } else lan_error_dialog(c);
        } else if(action==LAN_CHOICE_JOIN || action==LAN_CHOICE_REFRESH) {
            if(lan_gui_search())lan_browser(c);else lan_error_dialog(c);
        } else if(action>=LAN_CHOICE_HOST_BASE && action<LAN_CHOICE_HOST_BASE+lan_visible_count) {
            LanHost host=lan_visible_hosts[action-LAN_CHOICE_HOST_BASE];
            if(lan_gui_join(&host))lan_wait_dialog(c,"Connecting to host...");else lan_error_dialog(c);
        } else if(action==LAN_CHOICE_LEAVE)lan_leave_ui(c,lan_entered);
        else if(action==LAN_CHOICE_BACK) {
            unsigned phase=lan_phase;lan_close_dialog(c);
            if(phase==LAN_UI_MODE)lan_phase=LAN_UI_NONE;
            else if(phase==LAN_UI_ROLE)lan_mode_dialog(c);
            else if(phase==LAN_UI_BROWSER) { lan_gui_leave();lan_role_dialog(c); }
            else lan_leave_ui(c,lan_entered);
        }
    }
    if(lan_gui_active()) {
        int before=lan_gui_status();lan_gui_poll();int status=lan_gui_status();
        if(status==LAN_GUI_ERROR && lan_phase!=LAN_UI_ERROR) {
            replay_gui_stop();lan_error_dialog(c);
        } else if(status==LAN_GUI_CONNECTED && before!=status) {
            if(lan_player())lan_enter_pvp(c);else { lan_close_dialog(c);lan_phase=LAN_UI_NONE; }
        }
        if(status==LAN_GUI_SEARCHING && lan_phase==LAN_UI_BROWSER) {
            LanHost hosts[12];unsigned n=lan_gui_hosts(hosts);bool changed=n!=lan_visible_count;
            for(unsigned i=0;!changed && i<n;++i)changed=strcmp(hosts[i].address,lan_visible_hosts[i].address) || strcmp(hosts[i].name,lan_visible_hosts[i].name) || hosts[i].port!=lan_visible_hosts[i].port;
            if(changed)lan_browser(c);
        }
        if(status==LAN_GUI_CONNECTED) {
            LanMenuEvent event;
            unsigned kind=lan_end_screen?2:lan_module_kind;
            uint64_t module=lan_active_module;
            if((module || lan_end_screen) && !lan_match_starting)while(lan_gui_pop_menu(&event,kind)) {
                if(kind==2) { lan_end_screen=0;lan_active_module=0;lan_go(c,8);break; }
                if(event.input_type==3) {
                    sparring_select_cell(c,module,kind,event.player,event.event);
                    if(lan_active_module!=module || lan_module_kind!=kind)break;
                    continue;
                }
                uint64_t input=guest_invoke2(c,event.player?0x7000d94e40ull:0x7000d94dd0ull,0,0);
                uint32_t id=input?LD32(input+0x18):event.player;
                uint64_t method=kind? (event.down?0x7000d98c30ull:0x7000d98de0ull) :
                                                     (event.down?0x7000e44aa0ull:0x7000e44df0ull);
                if(ENV("LARECOMP_LAN_UI_TRACE"))LOG("[lan-ui] apply menu player %u module %u event %u down %u id %u\n",event.player+1,kind,event.event,event.down,id);
                lan_native_call=true;
                guest_invoke4(c,method,lan_active_module,event.input_type,id,event.event);
                lan_native_call=false;
                if(lan_active_module!=module || lan_module_kind!=kind)break;
            }
            if(lan_pvp_scene && lan_gui_take_match())lan_begin_native_match(c);
        }
    }
    lan_ui_pumping=false;
}
static int lan_ui_hook(Cpu *c,uint64_t site) {
    if(site==0x7000f48ef0ull) {
        sparring_module=0;sparring_pressed=-1;
        unsigned screen=c->x[1];
        input_menu_navigation(screen==5?INPUT_NAV_COMBAT:screen==8?INPUT_NAV_SPARRING:
            (screen==2 || screen==3 || screen==4 || screen==6)?INPUT_NAV_CAMPAIGN:INPUT_NAV_MENU);
    }
    if(site==0x7000e5b660ull || site==0x7000e5b9d0ull) { input_menu_navigation(true);return -1; }
    if(site==0x7000e5b8f0ull) { input_menu_navigation(false);return -1; }
    if(site==0x7000e43000ull || site==0x7000e45ea0ull) {
        sparring_module=c->x[0];sparring_kind=site==0x7000e45ea0ull;sparring_pressed=-1;
        input_menu_navigation(INPUT_NAV_SPARRING);
    }
    if(site==0x7000d985a0ull || site==0x7000d985b0ull) {
        if(c->x[0]==sparring_module)sparring_module=0;
        return -1;
    }
    if((lan_dialog || lan_menu_root) && (site==0x7000e6fca0ull || (site>=0x7000e6fd00ull && site<=0x7000e6fd30ull))) {
        for(unsigned i=0;i<lan_choice_count;++i)if(lan_menu_buttons[i]==c->x[0]) {
            if(site==0x7000e6fd20ull) { lan_choice=i;lan_focus_choice(c); }
            else if(site==0x7000e6fd30ull)guest_invoke2(c,0x7000e6c080ull,c->x[0],0);
            else if(site==0x7000e6fca0ull && c->x[1] && LD32(c->x[1]+0x12c)==0)lan_action=lan_commands[i];
            return 1;
        }
    }
    if(site==0x7000e6ed90ull) {
        if(!g_settings.allow_sparring_lan_multiplayer || lan_allow_entry || (replay_enabled() && !lan_gui_active()))return -1;
        lan_menu_scene=c->x[0];lan_mode_dialog(c);return 1;
    }
    if(site==0x7000dc7550ull) {
        uint64_t url=LD64(c->x[0]+0x30);char text[64];
        if(!dev_read_string(url,text,sizeof text) || strncmp(text,"SF2LAN/",7))return -1;
        char *end;unsigned long choice=strtoul(text+7,&end,10);
        if(*end || choice>UINT32_MAX)return 1;
        lan_action=(unsigned)choice;return 1;
    }
    if(site==0x7000cee2d0ull && c->x[0]==lan_dialog && !lan_dialog_closing) {
        lan_dialog=0;lan_menu_publish(false);lan_action=LAN_CHOICE_BACK;return -1;
    }
    if(!lan_gui_active())return -1;
    // ConfirmItem inlines ControlCount instead of using HaveSecondPlayer.
    // A LAN session has two players even with one local input device.
    if(site==0x7000d98aa8ull || site==0x7000d98b48ull) { c->x[0]=2;return 0; }
    if(site==0x7000cf1360ull && (c->x[1]==0 || c->x[1]==13) && c->x[2]) {
        char alias[64];uint64_t info=c->x[2];
        if(dev_read_string(LD64(info+0x20),alias,sizeof alias) && !strcmp(alias,"PVPTutorial2")) {
            ST64(info+0x10,guest_string(c,"LAN SPARRING"));ST8(info+0x18,0);
            ST64(info+0x20,guest_string(c,"Each player uses their normal player-1 controls on their own computer."));
            ST8(info+0x28,0);
        }
    }
    if(site==0x7000f48ef0ull && c->x[1]==8 && lan_entered && replay_enabled()) {
        replay_gui_stop();rt_hook_features&=~RT_HOOK_REPLAY;
        lan_pvp_scene=lan_active_module=lan_end_screen=0;
    }
    if(site==0x7000f48ef0ull && c->x[1]==5)lan_pvp_scene=lan_active_module=0;
    if(site==0x7000f48ef0ull && c->x[1]!=5 && c->x[1]!=8 && lan_entered) {
        lan_leave_ui(c,false);return -1;
    }
    if(site==0x7000d95000ull && !lan_native_call) {
        lan_pvp_scene=c->x[0];lan_native_call=true;
        guest_invoke2(c,site,lan_pvp_scene,c->x[1]);lan_native_call=false;
        if(lan_gui_status()!=LAN_GUI_CONNECTED)lan_host_wait_dialog(c);
        return 1;
    }
    if(site==0x7000d961f0ull && !lan_match_starting) {
        if(!lan_player() && lan_gui_status()==LAN_GUI_CONNECTED && !lan_host_match(c))
            lan_wait_dialog(c,"Choose concrete fighters and an arena to begin.");
        return 1;
    }
    if(site==0x7000d94fd0ull || site==0x7000d982c0ull) { c->x[0]=1;return 1; }
    if(site==0x7000e44380ull || site==0x7000e46200ull || site==0x7000d98fa0ull)c->x[1]=1;
    if(site==0x7000e43000ull || site==0x7000e45ea0ull) {
        lan_active_module=c->x[0];lan_module_kind=site==0x7000e45ea0ull;lan_end_screen=0;
    }
    if(site==0x7000e51220ull) { lan_end_screen=c->x[0];lan_active_module=0;LOG("[lan-ui] native match result; connection retained\n"); }
    if(site==0x7000e51a20ull && !lan_native_call) { lan_gui_send_menu(2,1,0,1);return 1; }
    if(site==0x7000e51760ull && !lan_native_call) { lan_action=LAN_CHOICE_LEAVE;return 1; }
    if(!lan_native_call && (site==0x7000e44aa0ull || site==0x7000e44df0ull ||
                           site==0x7000d98c30ull || site==0x7000d98de0ull)) {
        uint64_t input=guest_invoke2(c,0x7000d94dd0ull,0,0);
        if(input && (uint32_t)c->x[2]==LD32(input+0x18))
            lan_gui_send_menu(site==0x7000d98c30ull || site==0x7000d98de0ull,
                              site==0x7000e44aa0ull || site==0x7000d98c30ull,c->x[3],c->x[1]);
        return 1;
    }
    return -1;
}

RT_ON_RESET(lan_ui_guest_reset) { reset_lan_ui(); }
