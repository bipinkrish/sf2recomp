#include "../constants/hle.h"
// Native Unity dialogs and Sparring lifecycle. All managed calls stay on the
// guest update thread; the shipping executable needs no Python or external UI.
#include "../lan.h"
#include "../lan_random.h"
#include "../ui/lan_menu.h"
#include <inttypes.h>

enum { LAN_UI_NONE, LAN_UI_MODE, LAN_UI_ROLE, LAN_UI_BROWSER, LAN_UI_WAIT, LAN_UI_ERROR, LAN_UI_SETTINGS };
enum { LAN_CHOICE_NORMAL=1, LAN_CHOICE_LAN, LAN_CHOICE_HOST, LAN_CHOICE_JOIN,
       LAN_CHOICE_REFRESH, LAN_CHOICE_LEAVE, LAN_CHOICE_BACK, LAN_CHOICE_PRIMARY, LAN_CHOICE_HOST_BASE=SF2C_LAN_UI_HOOKS_N_100_AD57 };
static uint64_t lan_dialog,lan_primary_button,lan_menu_scene,lan_pvp_scene,lan_active_module,lan_end_screen;
static unsigned lan_phase,lan_module_kind,lan_action;
static bool lan_allow_entry,lan_native_call,lan_dialog_closing,lan_ui_pumping;
static bool lan_match_starting,lan_entered;
static LanHost lan_visible_hosts[SF2C_LAN_UI_HOOKS_N_12_6B51];
static unsigned lan_visible_count,lan_choice_count,lan_choice,lan_commands[SF2C_LAN_UI_HOOKS_N_14_8527];
static uint64_t lan_back_owner,lan_selection_state;
static char lan_back_prompt[SF2C_LAN_UI_HOOKS_N_96_7B1A];
static uint64_t lan_menu_prototype,lan_menu_template,lan_menu_buttons[SF2C_LAN_UI_HOOKS_N_14_8527];
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
    lan_dialog=lan_primary_button=lan_menu_scene=lan_pvp_scene=lan_active_module=lan_end_screen=0;
    lan_phase=lan_module_kind=lan_action=lan_visible_count=0;
    lan_allow_entry=lan_native_call=lan_dialog_closing=lan_ui_pumping=lan_match_starting=lan_entered=false;
}
static void lan_update_back_prompt(Cpu *c) {
    if(!lan_dialog)return;
    uint64_t back=LD64(lan_dialog+SF2C_LAN_UI_HOOKS_N_0X50_4171);if(!back)return;
    char key[SF2C_LAN_UI_HOOKS_N_64_A68B],text[SF2C_LAN_UI_HOOKS_N_96_7B1A];
    if(input_any_gamepad_connected())
        snprintf(key,sizeof key,SF2C_LAN_UI_HOOKS_S_S_0030,bind_pad_label(bind_prompt_host(BIND_B),bind_label_family()));
    else {
        const char *name=bind_key_label(g_bind_key[BIND_B]);
        snprintf(key,sizeof key,SF2C_LAN_UI_HOOKS_S_S_0030,name && *name && strcmp(name,SF2C_LAN_UI_HOOKS_S_NONE_A64B) ? name : SF2C_LAN_UI_HOOKS_S_UNBOUND_58E3);
    }
    snprintf(text,sizeof text,SF2C_LAN_UI_HOOKS_S_BACK_S_501C,key);
    if(back==lan_back_owner && !strcmp(text,lan_back_prompt))return;
    lan_back_owner=back;snprintf(lan_back_prompt,sizeof lan_back_prompt,SF2C_LAN_UI_HOOKS_S_S_0030,text);
    uint64_t value=guest_string(c,text);
    ST64(lan_dialog+SF2C_LAN_UI_HOOKS_N_0X88_AC70,value);ST8(lan_dialog+SF2C_LAN_UI_HOOKS_N_0X90_4255,0);
    guest_invoke2(c,SF2C_LAN_UI_HOOKS_N_0X70011CBB80ULL_C4C0,back,value);
}
static uint64_t lan_type(Cpu *c,const char *name) {
    return guest_invoke2(c,SF2C_LAN_UI_HOOKS_N_0X70008EA5C0ULL_2C81,0,guest_string(c,name));
}
static uint64_t lan_new(Cpu *c,const char *name) {
    uint64_t type=lan_type(c,name);
    return type?guest_invoke2(c,SF2C_LAN_UI_HOOKS_N_0X700039CB10ULL_D3DA,0,type):0;
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
        uint64_t type=lan_type(c,SF2C_LAN_UI_HOOKS_S_NEKKI_SF2_GUI_GENERALMENU_ME_6EB3);
        uint64_t source=type?guest_invoke2(c,SF2C_LAN_UI_HOOKS_N_0X700121E790ULL_8667,lan_menu_prototype,type):0;
        if(!source)return;
        // Capture while the main menu still owns a live source. A disconnect
        // can be the first dialog, after Unity has destroyed that scene.
        lan_menu_template=guest_invoke4(c,SF2C_LAN_UI_HOOKS_N_0X70012333A0ULL_826D,0,source,0,0);
        if(!lan_menu_template)return;
        uint64_t object=guest_invoke2(c,SF2C_LAN_UI_HOOKS_N_0X700121E720ULL_0F0E,lan_menu_template,0);
        guest_invoke2(c,SF2C_LAN_UI_HOOKS_N_0X7001226480ULL_8D5A,object,0);
        guest_invoke2(c,SF2C_LAN_UI_HOOKS_N_0X70012339D0ULL_280C,0,object);
    }
}
static void lan_menu_row(Cpu *c,unsigned index,uint64_t row,const char *text,int size) {
    if(!lan_menu_template || !row)return;
    uint64_t prototype=lan_menu_template;
    uint64_t parent=guest_invoke2(c,SF2C_LAN_UI_HOOKS_N_0X700121E6B0ULL_59E6,row,0);
    uint64_t item=guest_invoke4(c,SF2C_LAN_UI_HOOKS_N_0X70012333A0ULL_826D,0,prototype,parent,0);
    if(!item)return;
    lan_menu_buttons[index]=item;
    uint64_t empty=lan_new(c,SF2C_LAN_UI_HOOKS_S_UNITYENGINE_EVENTS_UNITYEVEN_6BA0);
    if(empty)for(unsigned offset=SF2C_LAN_UI_HOOKS_N_0X48_0EF0;offset<=SF2C_LAN_UI_HOOKS_N_0X68_40D3;offset+=SF2C_LAN_UI_HOOKS_N_8_2C62)ST64(item+offset,empty);
    uint64_t rect=guest_invoke2(c,SF2C_LAN_UI_HOOKS_N_0X700121E6B0ULL_59E6,item,0);
    guest_set_vec2(c,SF2C_LAN_UI_HOOKS_N_0X700123A050ULL_D8FB,rect,0,0);
    guest_set_vec2(c,SF2C_LAN_UI_HOOKS_N_0X700123A250ULL_F630,rect,1,1);
    guest_set_vec2(c,SF2C_LAN_UI_HOOKS_N_0X700123A550ULL_8625,rect,0,0);
    guest_set_vec2(c,SF2C_LAN_UI_HOOKS_N_0X700123A7D0ULL_D64C,rect,0,0);
    uint64_t label=LD64(item+SF2C_LAN_UI_HOOKS_N_0X28_EAB9),old_label=LD64(row+SF2C_LAN_UI_HOOKS_N_0X18_D47C),button=LD64(row+SF2C_LAN_UI_HOOKS_N_0X20_A766),line=LD64(row+SF2C_LAN_UI_HOOKS_N_0X28_EAB9);
    guest_invoke2(c,SF2C_LAN_UI_HOOKS_N_0X7000E73B90ULL_3B3B,label,1);
    guest_invoke2(c,SF2C_LAN_UI_HOOKS_N_0X7000E72180ULL_A6E0,label,size);
    guest_invoke2(c,SF2C_LAN_UI_HOOKS_N_0X7000E71230ULL_67AB,label,guest_string(c,text));
    guest_invoke2(c,SF2C_LAN_UI_HOOKS_N_0X7000E70080ULL_2A74,item,0);
    guest_invoke2(c,SF2C_LAN_UI_HOOKS_N_0X7000E6C080ULL_BAEA,item,0);
    if(old_label)guest_invoke2(c,GUEST_BEHAVIOUR_SET_ENABLED,old_label,0);
    if(button)guest_invoke2(c,GUEST_BEHAVIOUR_SET_ENABLED,button,0);
    if(line)guest_invoke2(c,GUEST_BEHAVIOUR_SET_ENABLED,line,0);
    uint64_t object=guest_invoke2(c,SF2C_LAN_UI_HOOKS_N_0X700121E720ULL_0F0E,item,0);
    guest_invoke2(c,SF2C_LAN_UI_HOOKS_N_0X7001226480ULL_8D5A,object,1);
}
static uint64_t lan_line(Cpu *c,const char *label,unsigned command,bool compact) {
    uint64_t line=lan_new(c,SF2C_LAN_UI_HOOKS_S_NEKKI_SF2_CORE_UTILS_STORYDI_FA81);
    if(!line)return 0;
    uint64_t empty=guest_string(c,SF2C_LAN_UI_HOOKS_S_EMPTY_12AE);
    ST64(line+SF2C_LAN_UI_HOOKS_N_0X10_BF9C,guest_string(c,label));
    ST64(line+SF2C_LAN_UI_HOOKS_N_0X18_D47C,command?guest_string(c,label):empty);
    char url[SF2C_LAN_UI_HOOKS_N_48_9801];snprintf(url,sizeof url,SF2C_LAN_UI_HOOKS_S_SF2LAN_U_99D7,command);
    ST64(line+SF2C_LAN_UI_HOOKS_N_0X20_A766,guest_string(c,url));
    ST32(line+SF2C_LAN_UI_HOOKS_N_0X30_8BAD,compact?SETTINGS_DIALOG_ROW_HEIGHT_BITS:SF2C_LAN_UI_HOOKS_N_0X43160000_BAF0); // 72 or 150 UI units
    ST32(line+SF2C_LAN_UI_HOOKS_N_0X34_806E,compact?SETTINGS_DIALOG_FONT_SIZE:SF2C_LAN_UI_HOOKS_N_80_4844);
    ST32(line+SF2C_LAN_UI_HOOKS_N_0X48_0EF0,UINT32_MAX);
    ST32(line+SF2C_LAN_UI_HOOKS_N_0X78_756D,command?SF2C_LAN_UI_HOOKS_N_2_D473:0);
    for(unsigned i=0;i<SF2C_LAN_UI_HOOKS_N_3_4E07;++i)ST32(line+SF2C_LAN_UI_HOOKS_N_0X7C_3CC4+i*SF2C_LAN_UI_HOOKS_N_4_4B22,SF2C_LAN_UI_HOOKS_N_0X3E4CCCCD_89DE);
    ST32(line+SF2C_LAN_UI_HOOKS_N_0X88_AC70,SF2C_LAN_UI_HOOKS_N_0X3F800000_67F1);
    return line;
}
static void lan_close_dialog(Cpu *c, bool keep_capture) {
    uint64_t dialog=lan_dialog;lan_dialog=lan_primary_button=0;
    if(!keep_capture)lan_menu_publish(false);
    if(lan_menu_root) {
        uint64_t root=lan_menu_root;lan_menu_root=0;
        guest_invoke2(c,SF2C_LAN_UI_HOOKS_N_0X7001226480ULL_8D5A,root,0);
        guest_invoke2(c,SF2C_LAN_UI_HOOKS_N_0X7001233760ULL_8B27,0,root); // Object.Destroy
        if(lan_main_root)guest_invoke2(c,SF2C_LAN_UI_HOOKS_N_0X7001226480ULL_8D5A,lan_main_root,1);
        lan_main_root=0;
    }
    memset(lan_menu_buttons,0,sizeof lan_menu_buttons);
    if(dialog) {
        lan_dialog_closing=true;guest_invoke2(c,SF2C_LAN_UI_HOOKS_N_0X7000CEE250ULL_90D4,dialog,0);lan_dialog_closing=false;
    }
}
static void lan_focus_choice(Cpu *c);
static bool lan_show(Cpu *c,unsigned phase,const char *title,const char *const labels[],const unsigned commands[],unsigned count,const char *primary) {
    lan_close_dialog(c,phase==LAN_UI_SETTINGS && lan_phase==LAN_UI_SETTINGS);
    uint64_t info=lan_new(c,SF2C_LAN_UI_HOOKS_S_NEKKI_SF2_GUI_DIALOGS_MULTIL_D71F);
    uint64_t list=lan_new(c,SF2C_LAN_UI_HOOKS_S_SYSTEM_COLLECTIONS_GENERIC_L_D29A);
    uint64_t element=lan_type(c,SF2C_LAN_UI_HOOKS_S_NEKKI_SF2_CORE_UTILS_STORYDI_FA81);
    uint64_t array=element?guest_invoke4(c,SF2C_LAN_UI_HOOKS_N_0X70006E6740ULL_63EE,0,element,count,0):0;
    if(!info || !list || !array) { LOG(SF2C_LAN_UI_HOOKS_S_LAN_UI_CANNOT_ALLOCATE_NATIV_D5C0);return false; }
    for(unsigned i=0;i<count;++i) {
        uint64_t line=lan_line(c,labels[i],commands[i],phase==LAN_UI_SETTINGS);
        if(!line)return false;
        ST64(array+SF2C_LAN_UI_HOOKS_N_0X20_A766+i*SF2C_LAN_UI_HOOKS_N_8_2C62,line);
    }
    ST64(list+SF2C_LAN_UI_HOOKS_N_0X10_BF9C,array);ST32(list+SF2C_LAN_UI_HOOKS_N_0X18_D47C,count);ST32(list+SF2C_LAN_UI_HOOKS_N_0X1C_7BDE,1);
    ST64(info+SF2C_LAN_UI_HOOKS_N_0X10_BF9C,list);ST64(info+SF2C_LAN_UI_HOOKS_N_0X18_D47C,guest_string(c,title));
    // BaseDialog.FooterButtons: Cancel alone, or both OK and Cancel.
    ST32(info+SF2C_LAN_UI_HOOKS_N_0X24_EB21,primary?SF2C_LAN_UI_HOOKS_N_3_4E07:SF2C_LAN_UI_HOOKS_N_2_D473);
    ST64(info+SF2C_LAN_UI_HOOKS_N_0X28_EAB9,guest_string(c,primary?primary:SF2C_LAN_UI_HOOKS_S_OK_98C4));
    ST64(info+SF2C_LAN_UI_HOOKS_N_0X38_AE4F,guest_string(c,SF2C_LAN_UI_HOOKS_S_BACK_E053));
    lan_dialog=guest_invoke4(c,SF2C_LAN_UI_HOOKS_N_0X7000CF1360ULL_B396,0,SF2C_LAN_UI_HOOKS_N_15_E629,info,0);
    lan_primary_button=lan_dialog && primary?LD64(lan_dialog+SF2C_LAN_UI_HOOKS_N_0X58_08FB):0;
    if(lan_primary_button)guest_invoke2(c,SF2C_LAN_UI_HOOKS_N_0X70005D8310ULL_7E78,lan_primary_button,1); // Selectable.interactable
    lan_phase=phase;
    lan_choice_count=count;memcpy(lan_commands,commands,count*sizeof commands[0]);lan_choice=0;
    while(lan_choice<count && !lan_commands[lan_choice])++lan_choice;
    lan_menu_publish(lan_dialog!=0);
    if(lan_dialog) {
        uint64_t scroll=LD64(lan_dialog+SF2C_LAN_UI_HOOKS_N_0XB8_8F73),slider=LD64(lan_dialog+SF2C_LAN_UI_HOOKS_N_0XD0_42B7);
        bool browser=phase==LAN_UI_BROWSER && count>SF2C_LAN_UI_HOOKS_N_4_4B22;
        // Reuse the native rows as fixed choices. Only a host list can scroll.
        if(scroll)guest_invoke2(c,SF2C_LAN_UI_HOOKS_N_0X70005D3190ULL_44B7,scroll,browser);
        if(slider) {
            uint64_t object=guest_invoke2(c,SF2C_LAN_UI_HOOKS_N_0X700121E720ULL_0F0E,slider,0);
            if(object)guest_invoke2(c,SF2C_LAN_UI_HOOKS_N_0X7001226480ULL_8D5A,object,browser);
        }
        uint64_t back=LD64(lan_dialog+SF2C_LAN_UI_HOOKS_N_0X50_4171);
        if(back) {
            guest_invoke2(c,SF2C_LAN_UI_HOOKS_N_0X70011CB2F0ULL_466B,back,1); // native B / remapped keyboard Back
            guest_invoke2(c,SF2C_LAN_UI_HOOKS_N_0X70011CBA70ULL_109B,back,0);
            uint64_t hint=LD64(back+SF2C_LAN_UI_HOOKS_N_0X140_A49E);
            if(hint) {
                guest_invoke2(c,SF2C_LAN_UI_HOOKS_N_0X7000CD7120ULL_BD22,hint,1); // HintID.B
                uint64_t object=guest_invoke2(c,SF2C_LAN_UI_HOOKS_N_0X700121E720ULL_0F0E,hint,0);
                if(object)guest_invoke2(c,SF2C_LAN_UI_HOOKS_N_0X7001226480ULL_8D5A,object,1);
                guest_invoke2(c,GUEST_BEHAVIOUR_SET_ENABLED,hint,1);
            }
        }
    }
    if(lan_dialog) {
        uint64_t rows=LD64(lan_dialog+SF2C_LAN_UI_HOOKS_N_0XE8_7494),items=rows?LD64(rows+SF2C_LAN_UI_HOOKS_N_0X10_BF9C):0;
        unsigned n=rows?LD32(rows+SF2C_LAN_UI_HOOKS_N_0X18_D47C):0;
        for(unsigned i=0;items && i<n && i<count;++i) {
            uint64_t row=LD64(items+SF2C_LAN_UI_HOOKS_N_0X20_A766+i*SF2C_LAN_UI_HOOKS_N_8_2C62),label=row?LD64(row+SF2C_LAN_UI_HOOKS_N_0X18_D47C):0;
            int size=phase==LAN_UI_SETTINGS?SETTINGS_DIALOG_FONT_SIZE:
                commands[i]>=LAN_CHOICE_HOST_BASE?SF2C_LAN_UI_HOOKS_N_48_9801:
                commands[i]?SF2C_LAN_UI_HOOKS_N_80_4844:SF2C_LAN_UI_HOOKS_N_55_02D2;
            if(commands[i] && lan_menu_template) {
                lan_menu_row(c,i,row,labels[i],size);
            } else if(label) {
                // ScrollDialog rows normally treat Text as a localization key.
                // These dynamic host names and choices are literal UI strings.
                guest_invoke2(c,SF2C_LAN_UI_HOOKS_N_0X7000E73B90ULL_3B3B,label,1);
                guest_invoke2(c,SF2C_LAN_UI_HOOKS_N_0X7000E72180ULL_A6E0,label,size);
            }
        }
    }
    lan_update_back_prompt(c);lan_focus_choice(c);
    if(lan_dialog) {
        uint64_t rows=LD64(lan_dialog+SF2C_LAN_UI_HOOKS_N_0XE8_7494);
        LOG(SF2C_LAN_UI_HOOKS_S_LAN_UI_CONTENT_PREFAB_7922 PRIx64 SF2C_LAN_UI_HOOKS_S_ROWS_U_SUPPLIED_U_N_D073,LD64(lan_dialog+SF2C_LAN_UI_HOOKS_N_0XE0_F5C1),rows?LD32(rows+SF2C_LAN_UI_HOOKS_N_0X18_D47C):0,count);
    }
    LOG(SF2C_LAN_UI_HOOKS_S_LAN_UI_S_U_CHOICES_DIALOG_4309 PRIx64 SF2C_LAN_UI_HOOKS_S_N_D7A5,title,count,lan_dialog);
    return lan_dialog!=0;
}
// Reuse the actual main-menu section, including its layout, typeface and
// kunai artwork. Only its choices change; the scene and background stay put.
static bool lan_submenu(Cpu *c,unsigned phase,const char *first,const char *second,
                        unsigned first_command,unsigned second_command) {
    lan_close_dialog(c,false);
    uint64_t lists=lan_menu_scene?LD64(lan_menu_scene+SF2C_LAN_UI_HOOKS_N_0X88_AC70):0;
    uint64_t array=lists?LD64(lists+SF2C_LAN_UI_HOOKS_N_0X10_BF9C):0;
    uint64_t main=array && LD32(lists+SF2C_LAN_UI_HOOKS_N_0X18_D47C)?LD64(array+SF2C_LAN_UI_HOOKS_N_0X20_A766):0;
    if(!main)return false;
    uint64_t rect=guest_invoke2(c,SF2C_LAN_UI_HOOKS_N_0X700121E6B0ULL_59E6,main,0);
    uint64_t parent=rect?guest_invoke2(c,SF2C_LAN_UI_HOOKS_N_0X70015D8BE0ULL_6A41,rect,0):0;
    uint64_t clone=parent?guest_invoke4(c,SF2C_LAN_UI_HOOKS_N_0X70012333A0ULL_826D,0,main,parent,0):0;
    if(!clone)return false;
    lan_menu_root=guest_invoke2(c,SF2C_LAN_UI_HOOKS_N_0X700121E720ULL_0F0E,clone,0);
    uint64_t buttons=LD64(clone+SF2C_LAN_UI_HOOKS_N_0X18_D47C),items=buttons?LD64(buttons+SF2C_LAN_UI_HOOKS_N_0X10_BF9C):0;
    unsigned count=buttons?LD32(buttons+SF2C_LAN_UI_HOOKS_N_0X18_D47C):0;
    if(!lan_menu_root || !items || count<SF2C_LAN_UI_HOOKS_N_3_4E07) { lan_close_dialog(c,false);return false; }
    const char *labels[]={first,second,SF2C_LAN_UI_HOOKS_S_BACK_E053};
    unsigned commands[]={first_command,second_command,LAN_CHOICE_BACK};
    for(unsigned i=0;i<count;++i) {
        uint64_t button=LD64(items+SF2C_LAN_UI_HOOKS_N_0X20_A766+i*SF2C_LAN_UI_HOOKS_N_8_2C62);
        uint64_t object=guest_invoke2(c,SF2C_LAN_UI_HOOKS_N_0X700121E720ULL_0F0E,button,0);
        guest_invoke2(c,SF2C_LAN_UI_HOOKS_N_0X7001226480ULL_8D5A,object,i<SF2C_LAN_UI_HOOKS_N_3_4E07);
        if(i>=SF2C_LAN_UI_HOOKS_N_3_4E07)continue;
        lan_menu_buttons[i]=button;lan_commands[i]=commands[i];
        uint64_t label=LD64(button+SF2C_LAN_UI_HOOKS_N_0X28_EAB9);
        guest_invoke2(c,SF2C_LAN_UI_HOOKS_N_0X7000E73B90ULL_3B3B,label,1);
        guest_invoke2(c,SF2C_LAN_UI_HOOKS_N_0X7000E71230ULL_67AB,label,guest_string(c,labels[i]));
    }
    lan_main_root=guest_invoke2(c,SF2C_LAN_UI_HOOKS_N_0X700121E720ULL_0F0E,main,0);
    guest_invoke2(c,SF2C_LAN_UI_HOOKS_N_0X7001226480ULL_8D5A,lan_main_root,0);
    guest_invoke2(c,SF2C_LAN_UI_HOOKS_N_0X7001226480ULL_8D5A,lan_menu_root,1);
    lan_phase=phase;lan_choice_count=SF2C_LAN_UI_HOOKS_N_3_4E07;lan_choice=0;
    lan_menu_publish(true);lan_focus_choice(c);
    LOG(SF2C_LAN_UI_HOOKS_S_LAN_UI_MAIN_MENU_SUBMENU_U_N_DB8A,phase);
    return true;
}
static void lan_mode_dialog(Cpu *c) {
    lan_keep_menu_template(c);
    if(lan_submenu(c,LAN_UI_MODE,SF2C_LAN_UI_HOOKS_S_NORMAL_5E7A,SF2C_LAN_UI_HOOKS_S_LAN_MULTIPLAYER_1520,LAN_CHOICE_NORMAL,LAN_CHOICE_LAN))return;
    const char *labels[]={SF2C_LAN_UI_HOOKS_S_NORMAL_5E7A,SF2C_LAN_UI_HOOKS_S_LAN_MULTIPLAYER_1520};unsigned cmds[]={LAN_CHOICE_NORMAL,LAN_CHOICE_LAN};
    lan_show(c,LAN_UI_MODE,SF2C_LAN_UI_HOOKS_S_SPARRING_357C,labels,cmds,SF2C_LAN_UI_HOOKS_N_2_D473,NULL);
}
static void lan_role_dialog(Cpu *c) {
    if(lan_submenu(c,LAN_UI_ROLE,SF2C_LAN_UI_HOOKS_S_HOST_046A,SF2C_LAN_UI_HOOKS_S_JOIN_3D8A,LAN_CHOICE_HOST,LAN_CHOICE_JOIN))return;
    const char *labels[]={SF2C_LAN_UI_HOOKS_S_HOST_046A,SF2C_LAN_UI_HOOKS_S_JOIN_3D8A};unsigned cmds[]={LAN_CHOICE_HOST,LAN_CHOICE_JOIN};
    lan_show(c,LAN_UI_ROLE,SF2C_LAN_UI_HOOKS_S_LAN_MULTIPLAYER_1520,labels,cmds,SF2C_LAN_UI_HOOKS_N_2_D473,NULL);
}
static void lan_browser(Cpu *c) {
    const char *labels[SF2C_LAN_UI_HOOKS_N_14_8527];unsigned cmds[SF2C_LAN_UI_HOOKS_N_14_8527],count=0;char names[SF2C_LAN_UI_HOOKS_N_12_6B51][SF2C_LAN_UI_HOOKS_N_128_2747];
    lan_visible_count=lan_gui_hosts(lan_visible_hosts);
    if(!lan_visible_count) { labels[count]=SF2C_LAN_UI_HOOKS_S_SEARCHING_FOR_HOSTS_ON_YOUR__AF88;cmds[count++]=0; }
    for(unsigned i=0;i<lan_visible_count;++i) {
        snprintf(names[i],sizeof names[i],SF2C_LAN_UI_HOOKS_S_S_S_33DC,lan_visible_hosts[i].name,lan_visible_hosts[i].address);
        labels[count]=names[i];cmds[count++]=LAN_CHOICE_HOST_BASE+i;
    }
    labels[count]=SF2C_LAN_UI_HOOKS_S_REFRESH_E71E;cmds[count++]=LAN_CHOICE_REFRESH;
    lan_show(c,LAN_UI_BROWSER,SF2C_LAN_UI_HOOKS_S_JOIN_LAN_GAME_5569,labels,cmds,count,NULL);
}
static void lan_wait_dialog(Cpu *c,const char *message) {
    const char *labels[]={message};unsigned cmds[]={0};
    lan_show(c,LAN_UI_WAIT,SF2C_LAN_UI_HOOKS_S_LAN_MULTIPLAYER_1520,labels,cmds,1,NULL);
}
static void lan_host_wait_dialog(Cpu *c) {
    char addresses[SF2C_LAN_UI_HOOKS_N_8_2C62][SF2C_LAN_UI_HOOKS_N_16_B17E],message[SF2C_LAN_UI_HOOKS_N_320_8882];unsigned count=lan_local_addresses(addresses);
    size_t used=snprintf(message,sizeof message,SF2C_LAN_UI_HOOKS_S_WAITING_FOR_PLAYER_2_NHOST_I_DBC2,count>1?SF2C_LAN_UI_HOOKS_S_S_0F46:SF2C_LAN_UI_HOOKS_S_EMPTY_12AE);
    if(!count)snprintf(message+used,sizeof message-used,SF2C_LAN_UI_HOOKS_S_UNAVAILABLE_CHECK_WI_FI_E36B);
    for(unsigned i=0;i<count;++i)
        used+=snprintf(message+used,sizeof message-used,SF2C_LAN_UI_HOOKS_S_S_S_72BD,i?SF2C_LAN_UI_HOOKS_S_EMPTY_F240:SF2C_LAN_UI_HOOKS_S_EMPTY_12AE,addresses[i]);
    lan_wait_dialog(c,message);
}
static void lan_error_dialog(Cpu *c) {
    const char *labels[]={lan_gui_error(),SF2C_LAN_UI_HOOKS_S_RETURN_TO_MENU_BB57};unsigned cmds[]={0,LAN_CHOICE_LEAVE};
    lan_show(c,LAN_UI_ERROR,SF2C_LAN_UI_HOOKS_S_LAN_CONNECTION_ABEC,labels,cmds,SF2C_LAN_UI_HOOKS_N_2_D473,NULL);
}
static void lan_go(Cpu *c,unsigned screen) {
    uint64_t module=guest_invoke2(c,SF2C_LAN_UI_HOOKS_N_0X7000F48CF0ULL_AC69,0,0);
    Cpu saved=*c;c->x[0]=module;c->x[1]=screen;c->x[SF2C_LAN_UI_HOOKS_N_2_D473]=c->x[SF2C_LAN_UI_HOOKS_N_3_4E07]=c->x[SF2C_LAN_UI_HOOKS_N_5_EF2D]=0;c->x[SF2C_LAN_UI_HOOKS_N_4_4B22]=screen!=SF2C_LAN_UI_HOOKS_N_7_7902;
    guest_call(c,SF2C_LAN_UI_HOOKS_N_0X7000F48EF0ULL_6134);*c=saved;
}
static void lan_enter_pvp(Cpu *c) {
    lan_close_dialog(c,false);lan_phase=LAN_UI_NONE;lan_allow_entry=true;lan_entered=true;
    guest_invoke2(c,SF2C_LAN_UI_HOOKS_N_0X7000E6ED90ULL_74FD,lan_menu_scene,0);lan_allow_entry=false;
}
static void lan_leave_ui(Cpu *c,bool go_main) {
    lan_close_dialog(c,false);replay_gui_stop();lan_gui_leave();
    rt_hook_features&=~RT_HOOK_REPLAY;
    lan_pvp_scene=lan_active_module=lan_end_screen=0;lan_entered=false;lan_phase=LAN_UI_NONE;
    LOG(SF2C_LAN_UI_HOOKS_S_LAN_UI_LEFT_SPARRING_CONNECT_34B3);
    if(go_main)lan_go(c,SF2C_LAN_UI_HOOKS_N_7_7902);
}
static bool lan_random_name(uint64_t name) {
    static const uint16_t random_name[]={SF2C_LAN_UI_HOOKS_CH_R_5BDE,SF2C_LAN_UI_HOOKS_CH_A_D749,SF2C_LAN_UI_HOOKS_CH_N_76E4,SF2C_LAN_UI_HOOKS_CH_D_8F5F,SF2C_LAN_UI_HOOKS_CH_O_5820,SF2C_LAN_UI_HOOKS_CH_M_4109};
    return name && LD32(name+SF2C_LAN_UI_HOOKS_N_0X10_BF9C)==SF2C_LAN_UI_HOOKS_N_6_E7F6 && !memcmp((void *)(uintptr_t)(name+SF2C_LAN_UI_HOOKS_N_0X14_F942),random_name,sizeof random_name);
}
static uint32_t lan_concrete_index(uint64_t array,uint64_t *random_state) {
    unsigned n=array?LD64(array+SF2C_LAN_UI_HOOKS_N_0X18_D47C):0,count=0;uint32_t candidates[SF2C_LAN_UI_HOOKS_N_256_51E8];
    if(n>SF2C_LAN_UI_HOOKS_N_256_51E8)return UINT32_MAX;
    for(unsigned j=0;j<n;++j) {
        uint64_t item=LD64(array+SF2C_LAN_UI_HOOKS_N_0X20_A766+j*SF2C_LAN_UI_HOOKS_N_8_2C62),name=item?LD64(item+SF2C_LAN_UI_HOOKS_N_0X18_D47C):0;
        if(name && !lan_random_name(name))candidates[count++]=j;
    }
    return count?candidates[lan_selection_bounded(random_state,count)]:UINT32_MAX;
}
static bool lan_host_match(Cpu *c) {
    uint64_t scene=lan_pvp_scene;
    if(!scene)return false;
    uint64_t list=guest_invoke2(c,SF2C_LAN_UI_HOOKS_N_0X7000CF9830ULL_7401,0,0),settings=list?LD64(list+SF2C_LAN_UI_HOOKS_N_0X18_D47C):0;
    uint32_t match[SF2C_LAN_UI_HOOKS_N_3_4E07];
    for(unsigned i=0;i<SF2C_LAN_UI_HOOKS_N_3_4E07;++i) {
        uint64_t array=settings?LD64(settings+(i==SF2C_LAN_UI_HOOKS_N_2_D473?SF2C_LAN_UI_HOOKS_N_0X20_A766:SF2C_LAN_UI_HOOKS_N_0X18_D47C)):0;
        uint64_t selected=LD64(scene+SF2C_LAN_UI_HOOKS_N_0XA0_3862+i*SF2C_LAN_UI_HOOKS_N_8_2C62);unsigned n=array?LD64(array+SF2C_LAN_UI_HOOKS_N_0X18_D47C):0;
        match[i]=UINT32_MAX;
        for(unsigned j=0;j<n && j<SF2C_LAN_UI_HOOKS_N_256_51E8;++j) {
            uint64_t item=LD64(array+SF2C_LAN_UI_HOOKS_N_0X20_A766+j*SF2C_LAN_UI_HOOKS_N_8_2C62),name=item?LD64(item+SF2C_LAN_UI_HOOKS_N_0X18_D47C):0;
            if(name && selected && LD32(name+SF2C_LAN_UI_HOOKS_N_0X10_BF9C)==LD32(selected+SF2C_LAN_UI_HOOKS_N_0X10_BF9C) &&
               !memcmp((void *)(uintptr_t)(name+SF2C_LAN_UI_HOOKS_N_0X14_F942),(void *)(uintptr_t)(selected+SF2C_LAN_UI_HOOKS_N_0X14_F942),LD32(name+SF2C_LAN_UI_HOOKS_N_0X10_BF9C)*SF2C_LAN_UI_HOOKS_N_2_D473)) { match[i]=j;break; }
        }
        if(match[i]==UINT32_MAX)return false;
        if(lan_random_name(LD64(scene+SF2C_LAN_UI_HOOKS_N_0XA0_3862+i*SF2C_LAN_UI_HOOKS_N_8_2C62))) {
            // Roster order is not fixed: exclude every placeholder by identity.
            match[i]=lan_concrete_index(array,&lan_selection_state);
            if(match[i]==UINT32_MAX)return false;
        }
    }
    LOG(SF2C_LAN_UI_HOOKS_S_LAN_UI_HOST_CHOSE_FIGHTERS_U_F911,match[0],match[1],match[SF2C_LAN_UI_HOOKS_N_2_D473]);
    return lan_gui_send_match(match);
}
static void lan_begin_native_match(Cpu *c) {
    if(!lan_pvp_scene)return;
    lan_close_dialog(c,false);lan_phase=LAN_UI_NONE;
    lan_gui_begin_match();replay_gui_start();rt_hook_features|=RT_HOOK_REPLAY;
    lan_match_starting=lan_native_call=true;
    guest_invoke2(c,SF2C_LAN_UI_HOOKS_N_0X7000D961F0ULL_34E0,lan_pvp_scene,0);
    lan_native_call=false;lan_match_starting=false;lan_active_module=0;
}
static void lan_focus_choice(Cpu *c) {
    if(!lan_dialog && !lan_menu_root)return;
    for(unsigned i=0;i<lan_choice_count;++i)if(lan_menu_buttons[i])
        guest_invoke2(c,SF2C_LAN_UI_HOOKS_N_0X7000E6C080ULL_BAEA,lan_menu_buttons[i],i==lan_choice);
    if(lan_choice<lan_choice_count && lan_menu_buttons[lan_choice])return;
    uint64_t button=0;
    if(lan_choice>=lan_choice_count) {
        button=lan_choice==lan_choice_count && lan_primary_button?
            lan_primary_button:LD64(lan_dialog+SF2C_LAN_UI_HOOKS_N_0X50_4171);
    }
    else {
        uint64_t rows=LD64(lan_dialog+SF2C_LAN_UI_HOOKS_N_0XE8_7494),items=rows?LD64(rows+SF2C_LAN_UI_HOOKS_N_0X10_BF9C):0;
        if(items && lan_choice<LD32(rows+SF2C_LAN_UI_HOOKS_N_0X18_D47C)) {
            uint64_t row=LD64(items+SF2C_LAN_UI_HOOKS_N_0X20_A766+lan_choice*SF2C_LAN_UI_HOOKS_N_8_2C62);button=row?LD64(row+SF2C_LAN_UI_HOOKS_N_0X20_A766):0;
        }
    }
    if(button)guest_invoke2(c,SF2C_LAN_UI_HOOKS_N_0X70005D9E60ULL_220F,button,0);
}
static void lan_dialog_input(Cpu *c) {
    unsigned requests=lan_menu_requests();if((!lan_dialog && !lan_menu_root) || !requests)return;
    if(requests&LAN_MENU_BACK) { lan_action=LAN_CHOICE_BACK;return; }
    if(requests&(LAN_MENU_UP|LAN_MENU_DOWN)) {
        int direction=(requests&LAN_MENU_UP)?-1:1;
        unsigned slots=lan_choice_count+(lan_menu_root?0:1)+(lan_primary_button!=0);
        do { lan_choice=(lan_choice+slots+direction)%slots; }
        while(lan_choice<lan_choice_count && !lan_commands[lan_choice]);
        lan_focus_choice(c);
    }
    if(requests&LAN_MENU_CONFIRM) {
        if(lan_choice<lan_choice_count)lan_action=lan_commands[lan_choice];
        else lan_action=lan_choice==lan_choice_count && lan_primary_button?LAN_CHOICE_PRIMARY:LAN_CHOICE_BACK;
    }
}
// Native table geometry (not hard-coded screen coordinates) maps a click to
// the same selection/confirmation methods used by the controller. LAN queues
// the absolute cell index; each peer applies it for the owning player.
static bool sparring_select_cell(Cpu *c,uint64_t module,unsigned kind,unsigned player,unsigned index) {
    uint64_t table=LD64(module+SF2C_LAN_UI_HOOKS_N_0X20_A766),array=LD64(module+(kind?SF2C_LAN_UI_HOOKS_N_0X90_4255:SF2C_LAN_UI_HOOKS_N_0X98_14A1));
    unsigned columns=LD32(module+SF2C_LAN_UI_HOOKS_N_0X5C_F9CC),count=array?LD64(array+SF2C_LAN_UI_HOOKS_N_0X18_D47C):0;
    if(!table || !columns || index>=count || player>1)return false;
    if(LD8(module+SF2C_LAN_UI_HOOKS_N_0X58_08FB+player))return false; // already Ready
    uint64_t selector=LD64(table+SF2C_LAN_UI_HOOKS_N_0X18_D47C+player*SF2C_LAN_UI_HOOKS_N_8_2C62);if(!selector)return false;
    int dx=(int)(index%columns)-(int)LD32(selector+SF2C_LAN_UI_HOOKS_N_0X38_AE4F);
    int dy=(int)(index/columns)-(int)LD32(selector+SF2C_LAN_UI_HOOKS_N_0X3C_4150);
    // With footer legends hidden, tapping/clicking the selected cell again is
    // the same confirmation as A. A zero-distance MoveSelection would reset
    // the native equipment/confirmation phase instead of advancing it.
    if(dx || dy) {
        Cpu saved=*c;c->x[0]=module;c->x[1]=player;c->x[SF2C_LAN_UI_HOOKS_N_2_D473]=(uint32_t)dx;c->x[SF2C_LAN_UI_HOOKS_N_3_4E07]=(uint32_t)dy;c->x[SF2C_LAN_UI_HOOKS_N_4_4B22]=1;c->x[SF2C_LAN_UI_HOOKS_N_5_EF2D]=0;
        guest_call(c,kind?SF2C_LAN_UI_HOOKS_N_0X7000E46120ULL_8BA7:SF2C_LAN_UI_HOOKS_N_0X7000E43DA0ULL_93F4);*c=saved;
    }
    uint64_t input=guest_invoke2(c,player?SF2C_LAN_UI_HOOKS_N_0X7000D94E40ULL_4D84:SF2C_LAN_UI_HOOKS_N_0X7000D94DD0ULL_16C0,0,0);
    if(!input)return false;
    bool previous=lan_native_call;lan_native_call=true;
    guest_invoke4(c,kind?SF2C_LAN_UI_HOOKS_N_0X7000D98DE0ULL_F125:SF2C_LAN_UI_HOOKS_N_0X7000E44DF0ULL_11B3,module,0,LD32(input+SF2C_LAN_UI_HOOKS_N_0X18_D47C),0);
    lan_native_call=previous;
    LOG(SF2C_LAN_UI_HOOKS_S_MOUSE_SPARRING_S_PLAYER_U_SE_7810,kind?SF2C_LAN_UI_HOOKS_S_ARENA_18CE:SF2C_LAN_UI_HOOKS_S_CHARACTER_FFE5,player+1,index);
    return true;
}
static bool sparring_contains(Cpu *c,uint64_t rect,uint64_t camera,const MenuMouse *mouse) {
    Cpu saved=*c;c->x[0]=0;c->x[1]=rect;c->x[SF2C_LAN_UI_HOOKS_N_2_D473]=camera;c->x[SF2C_LAN_UI_HOOKS_N_3_4E07]=0;
    memcpy(&c->v[0],&mouse->x,SF2C_LAN_UI_HOOKS_N_4_4B22);memcpy(&c->v[1],&mouse->y,SF2C_LAN_UI_HOOKS_N_4_4B22);
    guest_call(c,SF2C_LAN_UI_HOOKS_N_0X7000AF9300ULL_B5C6);bool inside=c->x[0]!=0;*c=saved;return inside;
}
static void sparring_mouse_frame(Cpu *c,const MenuMouse *mouse) {
    if(!sparring_module || lan_dialog || developer_console_open()) { sparring_pressed=-1;return; }
    if(mouse->x<0 || mouse->y<0) { sparring_pressed=-1;return; }
    // Swipes need held/motion frames; wheel events have no button edges.
    if(!(mouse->held&1) && !(mouse->pressed&1) && !(mouse->released&1) && !mouse->scroll_y)return;
    uint64_t manager=guest_invoke2(c,SF2C_LAN_UI_HOOKS_N_0X7000CEDE90ULL_12E4,0,0);
    if(manager && LD64(manager+SF2C_LAN_UI_HOOKS_N_0X40_1AF3)) { sparring_pressed=-1;return; }
    uint64_t module=sparring_module,table=LD64(module+SF2C_LAN_UI_HOOKS_N_0X20_A766),container=table?LD64(table+SF2C_LAN_UI_HOOKS_N_0X28_EAB9):0;
    uint64_t array=LD64(module+(sparring_kind?SF2C_LAN_UI_HOOKS_N_0X90_4255:SF2C_LAN_UI_HOOKS_N_0X98_14A1));unsigned count=array?LD64(array+SF2C_LAN_UI_HOOKS_N_0X18_D47C):0;
    if(!container || count>SF2C_LAN_UI_HOOKS_N_256_51E8)return;
    uint64_t type=lan_type(c,SF2C_LAN_UI_HOOKS_S_UNITYENGINE_CANVAS_UNITYENGI_6B6E);
    uint64_t canvas=type?guest_invoke2(c,SF2C_LAN_UI_HOOKS_N_0X700121EAD0ULL_885E,container,type):0;
    uint64_t camera=canvas?guest_invoke2(c,SF2C_LAN_UI_HOOKS_N_0X7000AF7250ULL_7D4E,canvas,0):0;
    if(!sparring_kind && table==sparring_view.table && sparring_roster_scroll_input(c,table,mouse,
            sparring_contains(c,LD64(table+SF2C_LAN_UI_HOOKS_N_0X30_8BAD),camera,mouse))) {
        sparring_pressed=-1;return;
    }
    unsigned children=guest_invoke2(c,SF2C_LAN_UI_HOOKS_N_0X70015D9E00ULL_574A,container,0);
    int hit=-1;
    for(unsigned i=0;i<count && i<children;++i) {
        if(!sparring_kind && !sparring_roster_cell_visible(table,i))continue;
        uint64_t cell=guest_invoke2(c,SF2C_LAN_UI_HOOKS_N_0X70015DA1F0ULL_3A2D,container,i);
        if(cell && sparring_contains(c,cell,camera,mouse)) { hit=i;break; }
    }
    if(mouse->pressed&1)sparring_pressed=hit;
    if(mouse->released&1) {
        int pressed=sparring_pressed;sparring_pressed=-1;
        if(hit<0 || pressed!=hit)return;
        if(lan_gui_active()) {
            if(lan_gui_status()==LAN_GUI_CONNECTED)lan_gui_send_menu(sparring_kind,1,hit,SF2C_LAN_UI_HOOKS_N_3_4E07);
        } else {
            unsigned player=sparring_kind?0:LD32(module+SF2C_LAN_UI_HOOKS_N_0XA0_3862);
            if(guest_invoke2(c,SF2C_LAN_UI_HOOKS_N_0X7000D982C0ULL_AB10,module,0))player=LD8(module+SF2C_LAN_UI_HOOKS_N_0X58_08FB)?1:0;
            sparring_select_cell(c,module,sparring_kind,player,hit);
        }
    }
}
static void settings_menu_action(Cpu *c, unsigned action);

static void lan_ui_pump(Cpu *c) {
    if(lan_ui_pumping)return;
    lan_ui_pumping=true;
    if(campaign_back_requested()) {
        uint64_t dialogs=guest_invoke2(c,SF2C_LAN_UI_HOOKS_N_0X7000CEDE90ULL_12E4,0,0);
        uint64_t back=guest_invoke2(c,SF2C_LAN_UI_HOOKS_N_0X7000F0CCE0ULL_B1C8,0,0);
        uint64_t controllers=back?LD64(back+SF2C_LAN_UI_HOOKS_N_0X18_D47C):0;
        if((dialogs && LD64(dialogs+SF2C_LAN_UI_HOOKS_N_0X40_1AF3)) || (controllers && LD32(controllers+SF2C_LAN_UI_HOOKS_N_0X18_D47C)))
            guest_invoke2(c,SF2C_LAN_UI_HOOKS_N_0X7000F0CF40ULL_3E96,back,0);
        else lan_go(c,SF2C_LAN_UI_HOOKS_N_7_7902);
    }
    lan_update_back_prompt(c);
    lan_dialog_input(c);
    unsigned action=lan_action;lan_action=0;
    if(action) {
        if(lan_phase==LAN_UI_SETTINGS)settings_menu_action(c,action);
        else if(action==LAN_CHOICE_NORMAL) { LOG(SF2C_LAN_UI_HOOKS_S_LAN_UI_ORIGINAL_LOCAL_SPARRI_AA3A);lan_enter_pvp(c); }
        else if(action==LAN_CHOICE_LAN)lan_role_dialog(c);
        else if(action==LAN_CHOICE_HOST) {
            if(lan_gui_host(SF2C_LAN_UI_HOOKS_N_34250_67EB)) {
                // One advancing selection stream per hosted session. Only the
                // host draws; MATCH sends the concrete choices to both peers.
                lan_selection_state=(uint64_t)lan_config()->seed^SF2RECOMP_LAN_SELECTION_SALT;
                lan_enter_pvp(c);
            } else lan_error_dialog(c);
        } else if(action==LAN_CHOICE_JOIN || action==LAN_CHOICE_REFRESH) {
            if(lan_gui_search())lan_browser(c);else lan_error_dialog(c);
        } else if(action>=LAN_CHOICE_HOST_BASE && action<LAN_CHOICE_HOST_BASE+lan_visible_count) {
            LanHost host=lan_visible_hosts[action-LAN_CHOICE_HOST_BASE];
            if(lan_gui_join(&host))lan_wait_dialog(c,SF2C_LAN_UI_HOOKS_S_CONNECTING_TO_HOST_029C);else lan_error_dialog(c);
        } else if(action==LAN_CHOICE_LEAVE)lan_leave_ui(c,lan_entered);
        else if(action==LAN_CHOICE_BACK) {
            unsigned phase=lan_phase;lan_close_dialog(c,false);
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
            if(lan_player())lan_enter_pvp(c);else { lan_close_dialog(c,false);lan_phase=LAN_UI_NONE; }
        }
        if(status==LAN_GUI_SEARCHING && lan_phase==LAN_UI_BROWSER) {
            LanHost hosts[SF2C_LAN_UI_HOOKS_N_12_6B51];unsigned n=lan_gui_hosts(hosts);bool changed=n!=lan_visible_count;
            for(unsigned i=0;!changed && i<n;++i)changed=strcmp(hosts[i].address,lan_visible_hosts[i].address) || strcmp(hosts[i].name,lan_visible_hosts[i].name) || hosts[i].port!=lan_visible_hosts[i].port;
            if(changed)lan_browser(c);
        }
        if(status==LAN_GUI_CONNECTED) {
            LanMenuEvent event;
            unsigned kind=lan_end_screen?SF2C_LAN_UI_HOOKS_N_2_D473:lan_module_kind;
            uint64_t module=lan_active_module;
            if((module || lan_end_screen) && !lan_match_starting)while(lan_gui_pop_menu(&event,kind)) {
                if(kind==SF2C_LAN_UI_HOOKS_N_2_D473) { lan_end_screen=0;lan_active_module=0;lan_go(c,SF2C_LAN_UI_HOOKS_N_8_2C62);break; }
                if(event.input_type==SF2C_LAN_UI_HOOKS_N_3_4E07) {
                    sparring_select_cell(c,module,kind,event.player,event.event);
                    if(lan_active_module!=module || lan_module_kind!=kind)break;
                    continue;
                }
                uint64_t input=guest_invoke2(c,event.player?SF2C_LAN_UI_HOOKS_N_0X7000D94E40ULL_4D84:SF2C_LAN_UI_HOOKS_N_0X7000D94DD0ULL_16C0,0,0);
                uint32_t id=input?LD32(input+SF2C_LAN_UI_HOOKS_N_0X18_D47C):event.player;
                uint64_t method=kind? (event.down?SF2C_LAN_UI_HOOKS_N_0X7000D98C30ULL_0A5A:SF2C_LAN_UI_HOOKS_N_0X7000D98DE0ULL_F125) :
                                                     (event.down?SF2C_LAN_UI_HOOKS_N_0X7000E44AA0ULL_2848:SF2C_LAN_UI_HOOKS_N_0X7000E44DF0ULL_11B3);
                if(ENV(SF2C_LAN_UI_HOOKS_S_SF2RECOMP_LAN_UI_TRACE_DC31))LOG(SF2C_LAN_UI_HOOKS_S_LAN_UI_APPLY_MENU_PLAYER_U_M_E9B9,event.player+1,kind,event.event,event.down,id);
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
    if(site==SF2C_LAN_UI_HOOKS_N_0X7000F48EF0ULL_6134) {
        sparring_module=0;sparring_pressed=-1;
        unsigned screen=c->x[1];
        input_menu_navigation(screen==SF2C_LAN_UI_HOOKS_N_5_EF2D?INPUT_NAV_COMBAT:screen==SF2C_LAN_UI_HOOKS_N_8_2C62?INPUT_NAV_SPARRING:
            (screen==SF2C_LAN_UI_HOOKS_N_2_D473 || screen==SF2C_LAN_UI_HOOKS_N_3_4E07 || screen==SF2C_LAN_UI_HOOKS_N_4_4B22 || screen==SF2C_LAN_UI_HOOKS_N_6_E7F6)?INPUT_NAV_CAMPAIGN:INPUT_NAV_MENU);
    }
    if(site==SF2C_LAN_UI_HOOKS_N_0X7000E5B660ULL_32B5 || site==SF2C_LAN_UI_HOOKS_N_0X7000E5B9D0ULL_0CF4) { input_menu_navigation(true);return -1; }
    if(site==SF2C_LAN_UI_HOOKS_N_0X7000E5B8F0ULL_60FC) { input_menu_navigation(false);return -1; }
    if(site==SF2C_LAN_UI_HOOKS_N_0X7000E43000ULL_EAF7 || site==SF2C_LAN_UI_HOOKS_N_0X7000E45EA0ULL_BD18) {
        sparring_module=c->x[0];sparring_kind=site==SF2C_LAN_UI_HOOKS_N_0X7000E45EA0ULL_BD18;sparring_pressed=-1;
        input_menu_navigation(INPUT_NAV_SPARRING);
    }
    if(site==SF2C_LAN_UI_HOOKS_N_0X7000D985A0ULL_CC7B || site==SF2C_LAN_UI_HOOKS_N_0X7000D985B0ULL_BCB0) {
        if(c->x[0]==sparring_module)sparring_module=0;
        return -1;
    }
    if((lan_dialog || lan_menu_root) && (site==SF2C_LAN_UI_HOOKS_N_0X7000E6FCA0ULL_29CC || (site>=SF2C_LAN_UI_HOOKS_N_0X7000E6FD00ULL_E737 && site<=SF2C_LAN_UI_HOOKS_N_0X7000E6FD30ULL_7E8A))) {
        for(unsigned i=0;i<lan_choice_count;++i)if(lan_menu_buttons[i]==c->x[0]) {
            if(site==SF2C_LAN_UI_HOOKS_N_0X7000E6FD20ULL_4C5E) { lan_choice=i;lan_focus_choice(c); }
            else if(site==SF2C_LAN_UI_HOOKS_N_0X7000E6FD30ULL_7E8A)guest_invoke2(c,SF2C_LAN_UI_HOOKS_N_0X7000E6C080ULL_BAEA,c->x[0],0);
            else if(site==SF2C_LAN_UI_HOOKS_N_0X7000E6FCA0ULL_29CC && c->x[1] && LD32(c->x[1]+SF2C_LAN_UI_HOOKS_N_0X12C_7DB2)==0)lan_action=lan_commands[i];
            return 1;
        }
    }
    if(site==SF2C_LAN_UI_HOOKS_N_0X7000E6ED90ULL_74FD) {
        if(!g_settings.allow_sparring_lan_multiplayer || lan_allow_entry || (replay_enabled() && !lan_gui_active()))return -1;
        lan_menu_scene=c->x[0];lan_mode_dialog(c);return 1;
    }
    if(site==SF2C_LAN_UI_HOOKS_N_0X7000DC7550ULL_FD24 && lan_dialog) {
        uint64_t url=LD64(c->x[0]+SF2C_LAN_UI_HOOKS_N_0X30_8BAD);char text[SF2C_LAN_UI_HOOKS_N_64_A68B];
        if(!dev_read_string(url,text,sizeof text) || strncmp(text,SF2C_LAN_UI_HOOKS_S_SF2LAN_63CA,SF2C_LAN_UI_HOOKS_N_7_7902))return -1;
        char *end;unsigned long choice=strtoul(text+SF2C_LAN_UI_HOOKS_N_7_7902,&end,SF2C_LAN_UI_HOOKS_N_10_4A44);
        if(*end || choice>UINT32_MAX)return 1;
        lan_action=(unsigned)choice;return 1;
    }
    if(site==SF2C_LAN_UI_HOOKS_N_0X7000CEE2D0ULL_BE68 && lan_dialog && c->x[0]==lan_dialog && !lan_dialog_closing) {
        lan_dialog=lan_primary_button=0;
        if(lan_phase!=LAN_UI_SETTINGS)lan_menu_publish(false);
        lan_action=LAN_CHOICE_BACK;return -1;
    }
    if(!lan_gui_active())return -1;
    // ConfirmItem inlines ControlCount instead of using HaveSecondPlayer.
    // A LAN session has two players even with one local input device.
    if(site==SF2C_LAN_UI_HOOKS_N_0X7000D98AA8ULL_62B0 || site==SF2C_LAN_UI_HOOKS_N_0X7000D98B48ULL_96AE) { c->x[0]=SF2C_LAN_UI_HOOKS_N_2_D473;return 0; }
    if(site==SF2C_LAN_UI_HOOKS_N_0X7000CF1360ULL_B396 && (c->x[1]==0 || c->x[1]==SF2C_LAN_UI_HOOKS_N_13_3FDB) && c->x[SF2C_LAN_UI_HOOKS_N_2_D473]) {
        char alias[SF2C_LAN_UI_HOOKS_N_64_A68B];uint64_t info=c->x[SF2C_LAN_UI_HOOKS_N_2_D473];
        if(dev_read_string(LD64(info+SF2C_LAN_UI_HOOKS_N_0X20_A766),alias,sizeof alias) && !strcmp(alias,SF2C_LAN_UI_HOOKS_S_PVPTUTORIAL2_F8E7)) {
            ST64(info+SF2C_LAN_UI_HOOKS_N_0X10_BF9C,guest_string(c,SF2C_LAN_UI_HOOKS_S_LAN_SPARRING_21F0));ST8(info+SF2C_LAN_UI_HOOKS_N_0X18_D47C,0);
            ST64(info+SF2C_LAN_UI_HOOKS_N_0X20_A766,guest_string(c,SF2C_LAN_UI_HOOKS_S_EACH_PLAYER_USES_THEIR_NORMA_7C73));
            ST8(info+SF2C_LAN_UI_HOOKS_N_0X28_EAB9,0);
        }
    }
    if(site==SF2C_LAN_UI_HOOKS_N_0X7000F48EF0ULL_6134 && c->x[1]==SF2C_LAN_UI_HOOKS_N_8_2C62 && lan_entered && replay_enabled()) {
        replay_gui_stop();rt_hook_features&=~RT_HOOK_REPLAY;
        lan_pvp_scene=lan_active_module=lan_end_screen=0;
    }
    if(site==SF2C_LAN_UI_HOOKS_N_0X7000F48EF0ULL_6134 && c->x[1]==SF2C_LAN_UI_HOOKS_N_5_EF2D)lan_pvp_scene=lan_active_module=0;
    if(site==SF2C_LAN_UI_HOOKS_N_0X7000F48EF0ULL_6134 && c->x[1]!=SF2C_LAN_UI_HOOKS_N_5_EF2D && c->x[1]!=SF2C_LAN_UI_HOOKS_N_8_2C62 && lan_entered) {
        lan_leave_ui(c,false);return -1;
    }
    if(site==SF2C_LAN_UI_HOOKS_N_0X7000D95000ULL_BAB9 && !lan_native_call) {
        lan_pvp_scene=c->x[0];lan_native_call=true;
        guest_invoke2(c,site,lan_pvp_scene,c->x[1]);lan_native_call=false;
        if(lan_gui_status()!=LAN_GUI_CONNECTED)lan_host_wait_dialog(c);
        return 1;
    }
    if(site==SF2C_LAN_UI_HOOKS_N_0X7000D961F0ULL_34E0 && !lan_match_starting) {
        if(!lan_player() && lan_gui_status()==LAN_GUI_CONNECTED && !lan_host_match(c))
            lan_wait_dialog(c,SF2C_LAN_UI_HOOKS_S_CHOOSE_CONCRETE_FIGHTERS_AND_5E55);
        return 1;
    }
    if(site==SF2C_LAN_UI_HOOKS_N_0X7000D94FD0ULL_7973 || site==SF2C_LAN_UI_HOOKS_N_0X7000D982C0ULL_AB10) { c->x[0]=1;return 1; }
    if(site==SF2C_LAN_UI_HOOKS_N_0X7000E44380ULL_AD08 || site==SF2C_LAN_UI_HOOKS_N_0X7000E46200ULL_AA2E || site==SF2C_LAN_UI_HOOKS_N_0X7000D98FA0ULL_3C61)c->x[1]=1;
    if(site==SF2C_LAN_UI_HOOKS_N_0X7000E43000ULL_EAF7 || site==SF2C_LAN_UI_HOOKS_N_0X7000E45EA0ULL_BD18) {
        lan_active_module=c->x[0];lan_module_kind=site==SF2C_LAN_UI_HOOKS_N_0X7000E45EA0ULL_BD18;lan_end_screen=0;
    }
    if(site==SF2C_LAN_UI_HOOKS_N_0X7000E51220ULL_E3C2) { lan_end_screen=c->x[0];lan_active_module=0;LOG(SF2C_LAN_UI_HOOKS_S_LAN_UI_NATIVE_MATCH_RESULT_C_8394); }
    if(site==SF2C_LAN_UI_HOOKS_N_0X7000E51A20ULL_C671 && !lan_native_call) { lan_gui_send_menu(SF2C_LAN_UI_HOOKS_N_2_D473,1,0,1);return 1; }
    if(site==SF2C_LAN_UI_HOOKS_N_0X7000E51760ULL_1AB2 && !lan_native_call) { lan_action=LAN_CHOICE_LEAVE;return 1; }
    if(!lan_native_call && (site==SF2C_LAN_UI_HOOKS_N_0X7000E44AA0ULL_2848 || site==SF2C_LAN_UI_HOOKS_N_0X7000E44DF0ULL_11B3 ||
                           site==SF2C_LAN_UI_HOOKS_N_0X7000D98C30ULL_0A5A || site==SF2C_LAN_UI_HOOKS_N_0X7000D98DE0ULL_F125)) {
        uint64_t input=guest_invoke2(c,SF2C_LAN_UI_HOOKS_N_0X7000D94DD0ULL_16C0,0,0);
        if(input && (uint32_t)c->x[SF2C_LAN_UI_HOOKS_N_2_D473]==LD32(input+SF2C_LAN_UI_HOOKS_N_0X18_D47C))
            lan_gui_send_menu(site==SF2C_LAN_UI_HOOKS_N_0X7000D98C30ULL_0A5A || site==SF2C_LAN_UI_HOOKS_N_0X7000D98DE0ULL_F125,
                              site==SF2C_LAN_UI_HOOKS_N_0X7000E44AA0ULL_2848 || site==SF2C_LAN_UI_HOOKS_N_0X7000D98C30ULL_0A5A,c->x[SF2C_LAN_UI_HOOKS_N_3_4E07],c->x[1]);
        return 1;
    }
    return -1;
}

RT_ON_RESET(lan_ui_guest_reset) { reset_lan_ui(); }
