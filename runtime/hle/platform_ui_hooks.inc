#include "../constants/hle.h"
// Hide only the authored menu footer panels. Dialog HintUI instances
// and FightLeft/FightRightHintList retain their original rendering and behavior.
// A CanvasGroup leaves module initialization, input subscriptions and native
// selection state intact while hiding the whole footer (including its BG).
static uint64_t footer_canvas_group_type;
#ifdef __ANDROID__
static MenuTouch android_touches[MENU_TOUCH_MAX];
static unsigned android_touch_count;
static MenuMouse android_primary_touch;
static uint64_t mobile_boss_owner,mobile_boss_control,mobile_boss_native,mobile_boss_object;
static uint64_t mobile_boss_icon,mobile_boss_background,mobile_boss_ready_sprite;
static uint64_t mobile_boss_material,mobile_boss_plain_material;
static float mobile_boss_radius,mobile_boss_icon_radius;
static float mobile_boss_last_progress=-1;
static int mobile_boss_last_ready=-1;
static void mobile_vec2(Cpu *c,uint64_t method,uint64_t object,float value[SF2C_PLATFORM_UI_HOOKS_N_2_D473]) {
    Cpu saved=*c;c->x[0]=object;c->x[1]=0;guest_call(c,method);
    memcpy(value,&c->v[0],SF2C_PLATFORM_UI_HOOKS_N_4_4B22);memcpy(value+1,&c->v[1],SF2C_PLATFORM_UI_HOOKS_N_4_4B22);*c=saved;
}
static void mobile_color(Cpu *c,uint64_t image,float r,float g,float b,float a) {
    Cpu saved=*c;float rgba[]={r,g,b,a};c->x[0]=image;c->x[1]=0;
    for(unsigned i=0;i<SF2C_PLATFORM_UI_HOOKS_N_4_4B22;++i)memcpy(&c->v[i],rgba+i,SF2C_PLATFORM_UI_HOOKS_N_4_4B22);
    guest_call(c,SF2C_PLATFORM_UI_HOOKS_N_0X7000114F90ULL_9628);*c=saved;
}
static void mobile_boss_clear(Cpu *c) {
    if(mobile_boss_object) {
        guest_invoke2(c,SF2C_PLATFORM_UI_HOOKS_N_0X7001226480ULL_8D5A,mobile_boss_object,0);
        guest_invoke2(c,SF2C_PLATFORM_UI_HOOKS_N_0X7001233760ULL_8B27,0,mobile_boss_object);
    }
    if(mobile_boss_plain_material)guest_invoke2(c,SF2C_PLATFORM_UI_HOOKS_N_0X7001233760ULL_8B27,0,mobile_boss_plain_material);
    mobile_boss_owner=mobile_boss_control=mobile_boss_native=mobile_boss_object=0;
    mobile_boss_icon=mobile_boss_background=mobile_boss_ready_sprite=0;
    mobile_boss_material=mobile_boss_plain_material=0;
}
static bool mobile_boss_button(Cpu *c,uint64_t controller) {
    uint64_t mobile=guest_invoke2(c,SF2C_PLATFORM_UI_HOOKS_N_0X7000C7E300ULL_DBE1,0,0);
    uint64_t actions=mobile?LD64(mobile+SF2C_PLATFORM_UI_HOOKS_N_0X30_8BAD):0;
    if(!actions)return false;
    unsigned player=lan_gui_active()?lan_player():0;
    uint64_t side=LD64(controller+SF2C_PLATFORM_UI_HOOKS_N_0X20_A766+player*SF2C_PLATFORM_UI_HOOKS_N_8_2C62),boss=side?LD64(side+SF2C_PLATFORM_UI_HOOKS_N_0X28_EAB9):0;
    uint64_t fight=guest_invoke2(c,SF2C_PLATFORM_UI_HOOKS_N_0X7000C91C70ULL_A0A1,0,0);
    uint64_t parameters=fight?LD64(fight+SF2C_PLATFORM_UI_HOOKS_N_0XA0_3862+player*SF2C_PLATFORM_UI_HOOKS_N_8_2C62):0;
    uint64_t alias=parameters?LD64(parameters+SF2C_PLATFORM_UI_HOOKS_N_0X1E8_E538):0;
    if(!boss || !alias || !LD32(alias+SF2C_PLATFORM_UI_HOOKS_N_0X10_BF9C))return false;
    uint64_t magic=LD64(actions+SF2C_PLATFORM_UI_HOOKS_N_0X50_4171);
    uint64_t missile=LD64(actions+SF2C_PLATFORM_UI_HOOKS_N_0X48_0EF0),punch=LD64(actions+SF2C_PLATFORM_UI_HOOKS_N_0X40_1AF3);
    if(!magic || !missile || !punch || !LD64(magic+SF2C_PLATFORM_UI_HOOKS_N_0XE0_F5C1))return false;
    uint64_t magic_rect=guest_invoke2(c,SF2C_PLATFORM_UI_HOOKS_N_0X700121E6B0ULL_59E6,magic,0);
    uint64_t parent=guest_invoke2(c,SF2C_PLATFORM_UI_HOOKS_N_0X70015D8BE0ULL_6A41,magic_rect,0);
    uint64_t button=guest_invoke4(c,SF2C_PLATFORM_UI_HOOKS_N_0X70012333A0ULL_826D,0,magic,parent,0);
    if(!button)return false;
    uint64_t object=guest_invoke2(c,SF2C_PLATFORM_UI_HOOKS_N_0X700121E720ULL_0F0E,button,0);
    guest_invoke2(c,SF2C_PLATFORM_UI_HOOKS_N_0X7001226480ULL_8D5A,object,1); // run Awake before installing the dispatcher
    // Share the native ActionButtons callbacks. SFButton passes its ButtonId
    // as the event argument, so this invokes mobile Super rather than Magic.
    ST64(button+SF2C_PLATFORM_UI_HOOKS_N_0XE0_F5C1,LD64(magic+SF2C_PLATFORM_UI_HOOKS_N_0XE0_F5C1));ST32(button+SF2C_PLATFORM_UI_HOOKS_N_0X114_9904,SF2C_PLATFORM_UI_HOOKS_N_14_8527);
    // Keep Magic's ring and geometry, but draw its plain circular background
    // separately: the raid sprite bakes an oversized spiral into the background.
    mobile_boss_background=LD64(button+SF2C_PLATFORM_UI_HOOKS_N_0XB8_8F73);mobile_boss_icon=LD64(button+SF2C_PLATFORM_UI_HOOKS_N_0X128_15E4);
    guest_invoke2(c,SF2C_PLATFORM_UI_HOOKS_N_0X700010D790ULL_F093,mobile_boss_background,0);
    guest_invoke2(c,SF2C_PLATFORM_UI_HOOKS_N_0X700011D5E0ULL_3CD1,mobile_boss_background,0);
    uint64_t ring=LD64(button+SF2C_PLATFORM_UI_HOOKS_N_0X120_50E1);
    guest_invoke2(c,SF2C_PLATFORM_UI_HOOKS_N_0X700010D820ULL_86D7,ring,SF2C_PLATFORM_UI_HOOKS_N_3_4E07); // Image.Type.Filled
    guest_invoke2(c,SF2C_PLATFORM_UI_HOOKS_N_0X700011D7D0ULL_F887,ring,SF2C_PLATFORM_UI_HOOKS_N_4_4B22); // FillMethod.Radial360
    memset((void *)(uintptr_t)(button+SF2C_PLATFORM_UI_HOOKS_N_0X90_4255),0,SF2C_PLATFORM_UI_HOOKS_N_24_C235);
    float dimensions[SF2C_PLATFORM_UI_HOOKS_N_2_D473];mobile_vec2(c,SF2C_PLATFORM_UI_HOOKS_N_0X700123A6D0ULL_19AD,magic_rect,dimensions);
    mobile_boss_radius=fminf(dimensions[0],dimensions[1])*SF2C_PLATFORM_UI_HOOKS_N_5F_33EA;
    mobile_boss_ready_sprite=guest_invoke2(c,SF2C_PLATFORM_UI_HOOKS_N_0X7000111A30ULL_F6E5,LD64(boss+SF2C_PLATFORM_UI_HOOKS_N_0X38_AE4F),0);
    uint64_t icon_rect=guest_invoke2(c,SF2C_PLATFORM_UI_HOOKS_N_0X700121E6B0ULL_59E6,mobile_boss_icon,0);
    guest_set_vec2(c,SF2C_PLATFORM_UI_HOOKS_N_0X700123A550ULL_8625,icon_rect,0,0);
    // This child starts inactive. Its first Awake resolves the serialized
    // magic sprite, so run it before replacing its artwork/material.
    guest_invoke2(c,SF2C_PLATFORM_UI_HOOKS_N_0X7001226480ULL_8D5A,guest_invoke2(c,SF2C_PLATFORM_UI_HOOKS_N_0X700121E720ULL_0F0E,mobile_boss_icon,0),1);
    mobile_boss_material=guest_invoke2(c,SF2C_PLATFORM_UI_HOOKS_N_0X700011E210ULL_871C,mobile_boss_icon,0);
    uint64_t plain=guest_invoke2(c,SF2C_PLATFORM_UI_HOOKS_N_0X700011E210ULL_871C,mobile_boss_background,0);
    mobile_boss_plain_material=guest_invoke2(c,SF2C_PLATFORM_UI_HOOKS_N_0X70012331F0ULL_1E3D,0,plain);
    guest_invoke2(c,SF2C_PLATFORM_UI_HOOKS_N_0X700122A5F0ULL_7DCC,mobile_boss_plain_material,
        guest_invoke2(c,SF2C_PLATFORM_UI_HOOKS_N_0X70015D5A50ULL_F39E,0,0));
    mobile_vec2(c,SF2C_PLATFORM_UI_HOOKS_N_0X700123A6D0ULL_19AD,icon_rect,dimensions);
    mobile_boss_icon_radius=fminf(dimensions[0],dimensions[1])*SF2C_PLATFORM_UI_HOOKS_N_5F_33EA;
    if(mobile_boss_icon_radius<=0)mobile_boss_icon_radius=mobile_boss_radius*SF2C_PLATFORM_UI_HOOKS_N_65F_60EC;
    // One centered icon owns both states. Its unavailable silhouette uses crisp
    // canvas geometry; readiness restores the game's gold artwork.
    ST64(button+SF2C_PLATFORM_UI_HOOKS_N_0X128_15E4,0);
    uint64_t rect=guest_invoke2(c,SF2C_PLATFORM_UI_HOOKS_N_0X700121E6B0ULL_59E6,button,0);
    uint64_t missile_rect=guest_invoke2(c,SF2C_PLATFORM_UI_HOOKS_N_0X700121E6B0ULL_59E6,missile,0);
    uint64_t punch_rect=guest_invoke2(c,SF2C_PLATFORM_UI_HOOKS_N_0X700121E6B0ULL_59E6,punch,0);
    float m[SF2C_PLATFORM_UI_HOOKS_N_2_D473],r[SF2C_PLATFORM_UI_HOOKS_N_2_D473],p[SF2C_PLATFORM_UI_HOOKS_N_2_D473];
    mobile_vec2(c,SF2C_PLATFORM_UI_HOOKS_N_0X700123A410ULL_52BF,magic_rect,m);
    mobile_vec2(c,SF2C_PLATFORM_UI_HOOKS_N_0X700123A410ULL_52BF,missile_rect,r);
    mobile_vec2(c,SF2C_PLATFORM_UI_HOOKS_N_0X700123A410ULL_52BF,punch_rect,p);
    // Continue the two interleaved diagonals in the same parent coordinates.
    guest_set_vec2(c,SF2C_PLATFORM_UI_HOOKS_N_0X700123A550ULL_8625,rect,m[0]+r[0]-p[0],m[1]+r[1]-p[1]);
    mobile_boss_owner=mobile;mobile_boss_control=button;mobile_boss_native=boss;
    mobile_boss_object=guest_invoke2(c,SF2C_PLATFORM_UI_HOOKS_N_0X700121E720ULL_0F0E,button,0);
    mobile_boss_last_progress=-1;mobile_boss_last_ready=-1;
    LOG(SF2C_PLATFORM_UI_HOOKS_S_TOUCH_BOSS_POWER_USES_MOBILE_C673);
    return true;
}
static void mobile_boss_update(Cpu *c) {
    float progress;uint32_t bits=LD32(mobile_boss_native+SF2C_PLATFORM_UI_HOOKS_N_0X60_3D6E);memcpy(&progress,&bits,SF2C_PLATFORM_UI_HOOKS_N_4_4B22);
    int enabled=LD8(mobile_boss_native+SF2C_PLATFORM_UI_HOOKS_N_0X64_3246)!=0;
    int ready=enabled && LD8(mobile_boss_native+SF2C_PLATFORM_UI_HOOKS_N_0X80_D41C);
    if(progress!=mobile_boss_last_progress) {
        // The native Switch controller stores percentage points (0..100).
        // Unity Image.fillAmount takes a fraction (0..1).
        guest_set_vec2(c,SF2C_PLATFORM_UI_HOOKS_N_0X700011D870ULL_B36F,LD64(mobile_boss_control+SF2C_PLATFORM_UI_HOOKS_N_0X120_50E1),progress/SF2C_PLATFORM_UI_HOOKS_N_100_F_089E,0);
        guest_invoke2(c,SF2C_PLATFORM_UI_HOOKS_N_0X70001151D0ULL_0E62,LD64(mobile_boss_control+SF2C_PLATFORM_UI_HOOKS_N_0X120_50E1),0);
        guest_invoke2(c,SF2C_PLATFORM_UI_HOOKS_N_0X7001226480ULL_8D5A,guest_invoke2(c,SF2C_PLATFORM_UI_HOOKS_N_0X700121E720ULL_0F0E,
            LD64(mobile_boss_control+SF2C_PLATFORM_UI_HOOKS_N_0X120_50E1),0),progress>0);
        mobile_boss_last_progress=progress;
    }
    if(ready!=mobile_boss_last_ready) {
        guest_invoke2(c,SF2C_PLATFORM_UI_HOOKS_N_0X70005D8310ULL_7E78,mobile_boss_control,ready);
        LOG(SF2C_PLATFORM_UI_HOOKS_S_TOUCH_BOSS_POWER_READY_D_ENA_6EA1,ready,enabled,progress);
        guest_invoke2(c,SF2C_PLATFORM_UI_HOOKS_N_0X700010D790ULL_F093,mobile_boss_icon,ready?mobile_boss_ready_sprite:0);
        guest_invoke2(c,SF2C_PLATFORM_UI_HOOKS_N_0X700011D5E0ULL_3CD1,mobile_boss_icon,0);
        guest_invoke2(c,SF2C_PLATFORM_UI_HOOKS_N_0X700011E430ULL_83EA,mobile_boss_icon,ready?mobile_boss_material:mobile_boss_plain_material);
        mobile_color(c,mobile_boss_icon,1,1,1,1); // geometry already carries the unavailable tint
        guest_invoke2(c,SF2C_PLATFORM_UI_HOOKS_N_0X7001226480ULL_8D5A,guest_invoke2(c,SF2C_PLATFORM_UI_HOOKS_N_0X700121E720ULL_0F0E,mobile_boss_icon,0),1);
        mobile_boss_last_ready=ready;
    }
    guest_invoke2(c,SF2C_PLATFORM_UI_HOOKS_N_0X7001226480ULL_8D5A,mobile_boss_object,1);
}

// A plain circle with Magic's authored fill/border colors. Unity supplies its
// white UI texture because this Image has no sprite; clipping/fades stay native.
static int mobile_boss_mesh(Cpu *c) {
    bool icon=c->x[0] && c->x[0]==mobile_boss_icon && mobile_boss_last_ready==0;
    if(!icon && (!mobile_boss_background || c->x[0]!=mobile_boss_background))return -1;
    Cpu saved=*c;uint64_t mesh=c->x[1];
    guest_invoke2(c,SF2C_PLATFORM_UI_HOOKS_N_0X7000AED470ULL_D40F,mesh,0);
    if(icon) {
        // Three tapered spiral arms, with a one-unit feather on each edge.
        // Unlike a thresholded glow texture this stays sharp at any UI scale.
        for(unsigned arm=0;arm<SF2C_PLATFORM_UI_HOOKS_N_3_4E07;++arm) {
            for(unsigned i=0;i<=SF2C_PLATFORM_UI_HOOKS_N_64_A68B;++i) {
                float t=i/SF2C_PLATFORM_UI_HOOKS_N_64_F_1607,angle=arm*SF2C_PLATFORM_UI_HOOKS_N_2_09439510239F_0698+t*SF2C_PLATFORM_UI_HOOKS_N_3_8F_7121,r=SF2C_PLATFORM_UI_HOOKS_N_12F_4C3E+SF2C_PLATFORM_UI_HOOKS_N_78F_390E*t;
                float co=cosf(angle),si=sinf(angle),dx=SF2C_PLATFORM_UI_HOOKS_N_78F_390E*co-r*SF2C_PLATFORM_UI_HOOKS_N_3_8F_7121*si,dy=SF2C_PLATFORM_UI_HOOKS_N_78F_390E*si+r*SF2C_PLATFORM_UI_HOOKS_N_3_8F_7121*co;
                float length=hypotf(dx,dy),width=SF2C_PLATFORM_UI_HOOKS_N_135F_4407*powf(fmaxf(0,sinf(t*SF2C_PLATFORM_UI_HOOKS_N_3_14159265359F_E18C)),SF2C_PLATFORM_UI_HOOKS_N_7F_7EF6);
                for(unsigned edge=0;edge<SF2C_PLATFORM_UI_HOOKS_N_4_4B22;++edge) {
                    float sign=edge<SF2C_PLATFORM_UI_HOOKS_N_2_D473?-1:1,feather=(edge==0 || edge==SF2C_PLATFORM_UI_HOOKS_N_3_4E07)?SF2C_PLATFORM_UI_HOOKS_N_1_F_6B16/mobile_boss_icon_radius:0;
                    float offset=sign*(width+feather);
                    float v[]={(r*co-dy/length*offset)*mobile_boss_icon_radius,
                               (r*si+dx/length*offset)*mobile_boss_icon_radius,0,SF2C_PLATFORM_UI_HOOKS_N_5F_33EA,SF2C_PLATFORM_UI_HOOKS_N_5F_33EA};
                    *c=saved;c->x[0]=mesh;c->x[1]=edge==0 || edge==SF2C_PLATFORM_UI_HOOKS_N_3_4E07?SF2C_PLATFORM_UI_HOOKS_N_0X002E2E21U_27EF:SF2C_PLATFORM_UI_HOOKS_N_0XEB2E2E21U_2C73;c->x[SF2C_PLATFORM_UI_HOOKS_N_2_D473]=0;
                    for(unsigned k=0;k<SF2C_PLATFORM_UI_HOOKS_N_5_EF2D;++k)memcpy(&c->v[k],v+k,SF2C_PLATFORM_UI_HOOKS_N_4_4B22);
                    guest_call(c,SF2C_PLATFORM_UI_HOOKS_N_0X7000AEDD60ULL_D862);
                }
            }
            for(unsigned i=0;i<SF2C_PLATFORM_UI_HOOKS_N_64_A68B;++i)for(unsigned edge=0;edge<SF2C_PLATFORM_UI_HOOKS_N_3_4E07;++edge) {
                unsigned a=arm*SF2C_PLATFORM_UI_HOOKS_N_260_39BB+i*SF2C_PLATFORM_UI_HOOKS_N_4_4B22+edge,b=a+SF2C_PLATFORM_UI_HOOKS_N_4_4B22;
                guest_invoke4(c,SF2C_PLATFORM_UI_HOOKS_N_0X7000AEDF00ULL_4722,mesh,a,b,a+1);
                guest_invoke4(c,SF2C_PLATFORM_UI_HOOKS_N_0X7000AEDF00ULL_4722,mesh,a+1,b,b+1);
            }
        }
        *c=saved;return 1;
    }
    const unsigned count=SF2C_PLATFORM_UI_HOOKS_N_64_A68B;
    float stroke=mobile_boss_radius*SF2C_PLATFORM_UI_HOOKS_N_10F_93F8; // Magic's authored border is 10/102 of its radius
    for(unsigned i=0;i<count;++i) {
        float angle=i*(SF2C_PLATFORM_UI_HOOKS_N_6_28318530718F_A61A/count);
        for(unsigned ring=0;ring<SF2C_PLATFORM_UI_HOOKS_N_5_EF2D;++ring) {
            float radius=ring==0?0:mobile_boss_radius-(ring==1?stroke+1:ring==SF2C_PLATFORM_UI_HOOKS_N_2_D473?stroke:ring==SF2C_PLATFORM_UI_HOOKS_N_3_4E07?1:0);
            float vertex[]={cosf(angle)*radius,sinf(angle)*radius,0,SF2C_PLATFORM_UI_HOOKS_N_5F_33EA,SF2C_PLATFORM_UI_HOOKS_N_5F_33EA};
            *c=saved;c->x[0]=mesh;
            c->x[1]=ring<SF2C_PLATFORM_UI_HOOKS_N_2_D473?SF2C_PLATFORM_UI_HOOKS_N_0X995A5739U_7044:ring==SF2C_PLATFORM_UI_HOOKS_N_4_4B22?SF2C_PLATFORM_UI_HOOKS_N_0X00312D1BU_DBAE:SF2C_PLATFORM_UI_HOOKS_N_0XFF312D1BU_E7E4;
            c->x[SF2C_PLATFORM_UI_HOOKS_N_2_D473]=0;
            for(unsigned k=0;k<SF2C_PLATFORM_UI_HOOKS_N_5_EF2D;++k)memcpy(&c->v[k],vertex+k,SF2C_PLATFORM_UI_HOOKS_N_4_4B22);
            guest_call(c,SF2C_PLATFORM_UI_HOOKS_N_0X7000AEDD60ULL_D862);
        }
    }
    for(unsigned i=0;i<count;++i) {
        unsigned a=i*SF2C_PLATFORM_UI_HOOKS_N_5_EF2D,b=((i+1)%count)*SF2C_PLATFORM_UI_HOOKS_N_5_EF2D;
        guest_invoke4(c,SF2C_PLATFORM_UI_HOOKS_N_0X7000AEDF00ULL_4722,mesh,a,b+1,a+1);
        for(unsigned ring=1;ring<SF2C_PLATFORM_UI_HOOKS_N_4_4B22;++ring) {
            guest_invoke4(c,SF2C_PLATFORM_UI_HOOKS_N_0X7000AEDF00ULL_4722,mesh,a+ring,b+ring,a+ring+1);
            guest_invoke4(c,SF2C_PLATFORM_UI_HOOKS_N_0X7000AEDF00ULL_4722,mesh,a+ring+1,b+ring,b+ring+1);
        }
    }
    *c=saved;return 1;
}

#endif
RT_ON_RESET(reset_platform_ui) {
    footer_canvas_group_type = 0;
#ifdef __ANDROID__
    memset(android_touches, 0, sizeof android_touches);
    android_touch_count = 0;
    android_primary_touch=(MenuMouse){.x=-SF2C_PLATFORM_UI_HOOKS_N_10000_39E5,.y=-SF2C_PLATFORM_UI_HOOKS_N_10000_39E5};
    mobile_boss_owner=mobile_boss_control=mobile_boss_native=mobile_boss_object=0;
    mobile_boss_icon=mobile_boss_background=mobile_boss_ready_sprite=0;
    mobile_boss_material=mobile_boss_plain_material=0;
#endif
}

static void hide_ui_module(Cpu *c, uint64_t list) {
    if (!footer_canvas_group_type)
        footer_canvas_group_type = guest_invoke2(c, SF2C_PLATFORM_UI_HOOKS_N_0X70008EA5C0ULL_2C81, 0,
            guest_string(c, SF2C_PLATFORM_UI_HOOKS_S_UNITYENGINE_CANVASGROUP_UNIT_96C3));
    if (!footer_canvas_group_type) return;
    uint64_t object = guest_invoke2(c, SF2C_PLATFORM_UI_HOOKS_N_0X700121E720ULL_0F0E, list, 0);
    uint64_t group = guest_invoke2(c, SF2C_PLATFORM_UI_HOOKS_N_0X700121E790ULL_8667, list, footer_canvas_group_type);
    if (!group) group = guest_invoke2(c, SF2C_PLATFORM_UI_HOOKS_N_0X70012260E0ULL_58B9, object, footer_canvas_group_type);
    if (!group) return;
    guest_set_vec2(c, SF2C_PLATFORM_UI_HOOKS_N_0X7000AF7FB0ULL_525E, group, 0, 0); // CanvasGroup.alpha
    guest_invoke2(c, SF2C_PLATFORM_UI_HOOKS_N_0X7000AF8110ULL_6583, group, 0);     // no invisible pointer targets
}

static void hide_menu_footer(Cpu *c, uint64_t list) {
    static const char *const names[] = {
        SF2C_PLATFORM_UI_HOOKS_S_GENERALMENUHINTLIST_C62B, SF2C_PLATFORM_UI_HOOKS_S_MAPHINTLIST_DF34, SF2C_PLATFORM_UI_HOOKS_S_PROFILEHINTLIST_7EE8, SF2C_PLATFORM_UI_HOOKS_S_SHOPHINTLIST_7DCB,
        SF2C_PLATFORM_UI_HOOKS_S_DOJOHINTLIST_58ED, SF2C_PLATFORM_UI_HOOKS_S_PVPLEFTHINTLIST_5E56, SF2C_PLATFORM_UI_HOOKS_S_PVPRIGHTHINTLIST_E7F5
    };
    for (unsigned i = 0; i < sizeof names / sizeof names[0]; ++i) {
        if (!guest_object_named(c, list, names[i])) continue;
        hide_ui_module(c, list);
        LOG(SF2C_PLATFORM_UI_HOOKS_S_UI_HIDDEN_MENU_CONTROL_FOOTE_5D24);
        break;
    }
}

#ifdef __ANDROID__
static void mobile_tutorial_menu_target(Cpu *c,uint64_t object) {
    // The tutorial still owns this button's native click listener.
    // Keep its hit area on a visible navigation control after hiding
    // the footer, so the tutorial blocker cannot strand the player.
    uint64_t main=guest_invoke2(c,SF2C_PLATFORM_UI_HOOKS_N_0X7000E27660ULL_8F47,0,0);
    uint64_t navigation=main?LD64(main+SF2C_PLATFORM_UI_HOOKS_N_0X108_650A):0;
    uint64_t header=navigation?LD64(navigation+SF2C_PLATFORM_UI_HOOKS_N_0X48_0EF0):0;
    if(header) {
        uint64_t rect=guest_invoke2(c,SF2C_PLATFORM_UI_HOOKS_N_0X7001226320ULL_D386,object,0);
        uint64_t parent=guest_invoke2(c,SF2C_PLATFORM_UI_HOOKS_N_0X700121E6B0ULL_59E6,header,0);
        hide_ui_module(c,rect);
        guest_invoke4(c,SF2C_PLATFORM_UI_HOOKS_N_0X70015D8EC0ULL_472E,rect,parent,0,0);
        guest_set_vec2(c,SF2C_PLATFORM_UI_HOOKS_N_0X700123A050ULL_D8FB,rect,0,0);
        guest_set_vec2(c,SF2C_PLATFORM_UI_HOOKS_N_0X700123A250ULL_F630,rect,1,1);
        guest_set_vec2(c,SF2C_PLATFORM_UI_HOOKS_N_0X700123A9D0ULL_564C,rect,SF2C_PLATFORM_UI_HOOKS_N_5F_33EA,SF2C_PLATFORM_UI_HOOKS_N_5F_33EA);
        guest_set_vec2(c,SF2C_PLATFORM_UI_HOOKS_N_0X700123A550ULL_8625,rect,0,0);
        guest_set_vec2(c,SF2C_PLATFORM_UI_HOOKS_N_0X700123A7D0ULL_D64C,rect,0,0);
        LOG(SF2C_PLATFORM_UI_HOOKS_S_TOUCH_TUTORIAL_MENU_TARGET_F_8894);
    }
}
#endif

static int platform_ui_hook(Cpu *c, uint64_t site) {
    if (site == SF2C_PLATFORM_UI_HOOKS_N_0X7000CDC110ULL_EA44) { // HintList.Init
        hide_menu_footer(c, c->x[0]);
        return 0;
    }
#ifdef __ANDROID__
    switch (site) {
    case SF2C_PLATFORM_UI_HOOKS_N_0X7000F7CDC0ULL_3C85: { // TutorialExtension.ActivateTutorialControl
        if(guest_object_named(c,c->x[1],SF2C_PLATFORM_UI_HOOKS_S_HINTGROUP_COMMON_MENU_39F0))
            mobile_tutorial_menu_target(c,c->x[1]);
        return 0;
    }
    case SF2C_PLATFORM_UI_HOOKS_N_0X700011EFB0ULL_31CC:return mobile_boss_mesh(c);
    case SF2C_PLATFORM_UI_HOOKS_N_0X7000C90A10ULL_0305: { // GameController.Update (Refresh is inlined by IL2CPP)
        if(mobile_boss_owner!=c->x[0]) {
            uint64_t controller=guest_invoke2(c,SF2C_PLATFORM_UI_HOOKS_N_0X7000C95B90ULL_5710,0,0);
            if(controller)mobile_boss_button(c,controller);
        }
        if(mobile_boss_owner==c->x[0] && mobile_boss_control)mobile_boss_update(c);
        return 0;
    }
    case SF2C_PLATFORM_UI_HOOKS_N_0X7000C8F630ULL_7160: // GameController.Init
    case SF2C_PLATFORM_UI_HOOKS_N_0X7000C90720ULL_83AB: // GameController.Free
        mobile_boss_clear(c);return 0;
    case SF2C_PLATFORM_UI_HOOKS_N_0X70015BC860ULL_3FE5: // SwitchVideoPlayerController.Update
        // The host owns playback. Complete the original managed lifecycle as
        // soon as the overlay ends, instead of waiting for empty SDK tracks.
        if (c->x[0] && LD8(c->x[0]+SF2C_PLATFORM_UI_HOOKS_N_0X28_EAB9) && movie_playback_completed()) {
            guest_invoke2(c,SF2C_PLATFORM_UI_HOOKS_N_0X70015BCA30ULL_200B,c->x[0],0);
            LOG(SF2C_PLATFORM_UI_HOOKS_S_MOVIE_MANAGED_PLAYBACK_COMPL_F203);
            return 1;
        }
        return 0;
    case SF2C_PLATFORM_UI_HOOKS_N_0X70015BBF50ULL_3AD6:
        c->x[0]=movie_watched_to_end();return 1;
    case SF2C_PLATFORM_UI_HOOKS_N_0X7001227DD0ULL_2FAF: case SF2C_PLATFORM_UI_HOOKS_N_0X7001227E40ULL_43F9: case SF2C_PLATFORM_UI_HOOKS_N_0X7001227EB0ULL_35C4: {
        unsigned bits=site==SF2C_PLATFORM_UI_HOOKS_N_0X7001227DD0ULL_2FAF?android_primary_touch.held:
                      site==SF2C_PLATFORM_UI_HOOKS_N_0X7001227E40ULL_43F9?android_primary_touch.pressed:android_primary_touch.released;
        c->x[0]=c->x[1]==0 && bits!=0;return 1;
    }
    case SF2C_PLATFORM_UI_HOOKS_N_0X7001227F20ULL_4B16:
        c->v[0]=c->v[1]=c->v[SF2C_PLATFORM_UI_HOOKS_N_2_D473]=0;
        memcpy(&c->v[0],&android_primary_touch.x,SF2C_PLATFORM_UI_HOOKS_N_4_4B22);
        memcpy(&c->v[1],&android_primary_touch.y,SF2C_PLATFORM_UI_HOOKS_N_4_4B22);
        return 1;
    case SF2C_PLATFORM_UI_HOOKS_N_0X7000C95BF0ULL_C5AE: // GameControllerSwitch.Init
        // The mobile ActionButtons already display these cooldowns on the
        // right. Keep the Switch module's state/callbacks, hide its duplicate HUD.
        hide_ui_module(c, c->x[0]);
        return 0;
    case SF2C_PLATFORM_UI_HOOKS_N_0X7000E1A610ULL_F214: // AssemblyController.ShowController
        c->x[0] = 1; return 1;
    case SF2C_PLATFORM_UI_HOOKS_N_0X7000C8F80CULL_6EF1: // InitTouchControl: LeftContainer.SetActive
    case SF2C_PLATFORM_UI_HOOKS_N_0X7000C8F830ULL_E935: // InitTouchControl: ActionButtons.SetVisible
        c->x[1] = 1; return 0;
    case SF2C_PLATFORM_UI_HOOKS_N_0X7000C970B0ULL_F378: // TouchControlData.InputType
        // Sparring assigns a gamepad; Dojo can use the default touch team. Follow the
        // current player's native assignment, including its control index.
        {
            uint64_t data=guest_invoke2(c,SF2C_PLATFORM_UI_HOOKS_N_0X7000C92820ULL_7F4A,0,0);
            if(data) {
                ST32(c->x[0]+SF2C_PLATFORM_UI_HOOKS_N_0X14_F942,LD32(data+SF2C_PLATFORM_UI_HOOKS_N_0X18_D47C));
                c->x[0]=LD32(data+SF2C_PLATFORM_UI_HOOKS_N_0X14_F942);
            } else c->x[0]=0;
            return 1;
        }
    case SF2C_PLATFORM_UI_HOOKS_N_0X70012283E0ULL_9411: c->x[0] = android_touch_count; return 1;
    case SF2C_PLATFORM_UI_HOOKS_N_0X70012285C0ULL_7733: c->x[0] = 1; return 1;
    case SF2C_PLATFORM_UI_HOOKS_N_0X7001228450ULL_32EB: { // Input.GetTouch returns a struct through x8
        unsigned index = (uint32_t)c->x[1];
        MenuTouch value = {.finger = -1, .phase = TOUCH_CANCELED};
        if (index < android_touch_count) value = android_touches[index];
        memcpy((void *)(uintptr_t)c->x[SF2C_PLATFORM_UI_HOOKS_N_8_2C62], &value, sizeof value);
        return 1;
    }
    default: break;
    }
#endif
    return -1;
}
