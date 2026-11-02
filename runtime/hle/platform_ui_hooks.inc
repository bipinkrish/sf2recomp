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
static void mobile_vec2(Cpu *c,uint64_t method,uint64_t object,float value[2]) {
    Cpu saved=*c;c->x[0]=object;c->x[1]=0;guest_call(c,method);
    memcpy(value,&c->v[0],4);memcpy(value+1,&c->v[1],4);*c=saved;
}
static void mobile_color(Cpu *c,uint64_t image,float r,float g,float b,float a) {
    Cpu saved=*c;float rgba[]={r,g,b,a};c->x[0]=image;c->x[1]=0;
    for(unsigned i=0;i<4;++i)memcpy(&c->v[i],rgba+i,4);
    guest_call(c,0x7000114f90ull);*c=saved;
}
static void mobile_boss_clear(Cpu *c) {
    if(mobile_boss_object) {
        guest_invoke2(c,0x7001226480ull,mobile_boss_object,0);
        guest_invoke2(c,0x7001233760ull,0,mobile_boss_object);
    }
    if(mobile_boss_plain_material)guest_invoke2(c,0x7001233760ull,0,mobile_boss_plain_material);
    mobile_boss_owner=mobile_boss_control=mobile_boss_native=mobile_boss_object=0;
    mobile_boss_icon=mobile_boss_background=mobile_boss_ready_sprite=0;
    mobile_boss_material=mobile_boss_plain_material=0;
}
static bool mobile_boss_button(Cpu *c,uint64_t controller) {
    uint64_t mobile=guest_invoke2(c,0x7000c7e300ull,0,0);
    uint64_t actions=mobile?LD64(mobile+0x30):0;
    if(!actions)return false;
    unsigned player=lan_gui_active()?lan_player():0;
    uint64_t side=LD64(controller+0x20+player*8),boss=side?LD64(side+0x28):0;
    uint64_t fight=guest_invoke2(c,0x7000c91c70ull,0,0);
    uint64_t parameters=fight?LD64(fight+0xa0+player*8):0;
    uint64_t alias=parameters?LD64(parameters+0x1e8):0;
    if(!boss || !alias || !LD32(alias+0x10))return false;
    uint64_t magic=LD64(actions+0x50);
    uint64_t missile=LD64(actions+0x48),punch=LD64(actions+0x40);
    if(!magic || !missile || !punch || !LD64(magic+0xe0))return false;
    uint64_t magic_rect=guest_invoke2(c,0x700121e6b0ull,magic,0);
    uint64_t parent=guest_invoke2(c,0x70015d8be0ull,magic_rect,0);
    uint64_t button=guest_invoke4(c,0x70012333a0ull,0,magic,parent,0);
    if(!button)return false;
    uint64_t object=guest_invoke2(c,0x700121e720ull,button,0);
    guest_invoke2(c,0x7001226480ull,object,1); // run Awake before installing the dispatcher
    // Share the native ActionButtons callbacks. SFButton passes its ButtonId
    // as the event argument, so this invokes mobile Super rather than Magic.
    ST64(button+0xe0,LD64(magic+0xe0));ST32(button+0x114,14);
    // Keep Magic's ring and geometry, but draw its plain circular background
    // separately: the raid sprite bakes an oversized spiral into the background.
    mobile_boss_background=LD64(button+0xb8);mobile_boss_icon=LD64(button+0x128);
    guest_invoke2(c,0x700010d790ull,mobile_boss_background,0);
    guest_invoke2(c,0x700011d5e0ull,mobile_boss_background,0);
    uint64_t ring=LD64(button+0x120);
    guest_invoke2(c,0x700010d820ull,ring,3); // Image.Type.Filled
    guest_invoke2(c,0x700011d7d0ull,ring,4); // FillMethod.Radial360
    memset((void *)(uintptr_t)(button+0x90),0,24);
    float dimensions[2];mobile_vec2(c,0x700123a6d0ull,magic_rect,dimensions);
    mobile_boss_radius=fminf(dimensions[0],dimensions[1])*.5f;
    mobile_boss_ready_sprite=guest_invoke2(c,0x7000111a30ull,LD64(boss+0x38),0);
    uint64_t icon_rect=guest_invoke2(c,0x700121e6b0ull,mobile_boss_icon,0);
    guest_set_vec2(c,0x700123a550ull,icon_rect,0,0);
    // This child starts inactive. Its first Awake resolves the serialized
    // magic sprite, so run it before replacing its artwork/material.
    guest_invoke2(c,0x7001226480ull,guest_invoke2(c,0x700121e720ull,mobile_boss_icon,0),1);
    mobile_boss_material=guest_invoke2(c,0x700011e210ull,mobile_boss_icon,0);
    uint64_t plain=guest_invoke2(c,0x700011e210ull,mobile_boss_background,0);
    mobile_boss_plain_material=guest_invoke2(c,0x70012331f0ull,0,plain);
    guest_invoke2(c,0x700122a5f0ull,mobile_boss_plain_material,
        guest_invoke2(c,0x70015d5a50ull,0,0));
    mobile_vec2(c,0x700123a6d0ull,icon_rect,dimensions);
    mobile_boss_icon_radius=fminf(dimensions[0],dimensions[1])*.5f;
    if(mobile_boss_icon_radius<=0)mobile_boss_icon_radius=mobile_boss_radius*.65f;
    // One centered icon owns both states. Its unavailable silhouette uses crisp
    // canvas geometry; readiness restores the game's gold artwork.
    ST64(button+0x128,0);
    uint64_t rect=guest_invoke2(c,0x700121e6b0ull,button,0);
    uint64_t missile_rect=guest_invoke2(c,0x700121e6b0ull,missile,0);
    uint64_t punch_rect=guest_invoke2(c,0x700121e6b0ull,punch,0);
    float m[2],r[2],p[2];
    mobile_vec2(c,0x700123a410ull,magic_rect,m);
    mobile_vec2(c,0x700123a410ull,missile_rect,r);
    mobile_vec2(c,0x700123a410ull,punch_rect,p);
    // Continue the two interleaved diagonals in the same parent coordinates.
    guest_set_vec2(c,0x700123a550ull,rect,m[0]+r[0]-p[0],m[1]+r[1]-p[1]);
    mobile_boss_owner=mobile;mobile_boss_control=button;mobile_boss_native=boss;
    mobile_boss_object=guest_invoke2(c,0x700121e720ull,button,0);
    mobile_boss_last_progress=-1;mobile_boss_last_ready=-1;
    LOG("[touch] boss power uses mobile magic styling and zigzag layout\n");
    return true;
}
static void mobile_boss_update(Cpu *c) {
    float progress;uint32_t bits=LD32(mobile_boss_native+0x60);memcpy(&progress,&bits,4);
    int enabled=LD8(mobile_boss_native+0x64)!=0;
    int ready=enabled && LD8(mobile_boss_native+0x80);
    if(progress!=mobile_boss_last_progress) {
        // The native Switch controller stores percentage points (0..100).
        // Unity Image.fillAmount takes a fraction (0..1).
        guest_set_vec2(c,0x700011d870ull,LD64(mobile_boss_control+0x120),progress/100.f,0);
        guest_invoke2(c,0x70001151d0ull,LD64(mobile_boss_control+0x120),0);
        guest_invoke2(c,0x7001226480ull,guest_invoke2(c,0x700121e720ull,
            LD64(mobile_boss_control+0x120),0),progress>0);
        mobile_boss_last_progress=progress;
    }
    if(ready!=mobile_boss_last_ready) {
        guest_invoke2(c,0x70005d8310ull,mobile_boss_control,ready);
        LOG("[touch] boss power ready=%d enabled=%d progress=%.3f\n",ready,enabled,progress);
        guest_invoke2(c,0x700010d790ull,mobile_boss_icon,ready?mobile_boss_ready_sprite:0);
        guest_invoke2(c,0x700011d5e0ull,mobile_boss_icon,0);
        guest_invoke2(c,0x700011e430ull,mobile_boss_icon,ready?mobile_boss_material:mobile_boss_plain_material);
        mobile_color(c,mobile_boss_icon,1,1,1,1); // geometry already carries the unavailable tint
        guest_invoke2(c,0x7001226480ull,guest_invoke2(c,0x700121e720ull,mobile_boss_icon,0),1);
        mobile_boss_last_ready=ready;
    }
    guest_invoke2(c,0x7001226480ull,mobile_boss_object,1);
}

// A plain circle with Magic's authored fill/border colors. Unity supplies its
// white UI texture because this Image has no sprite; clipping/fades stay native.
static int mobile_boss_mesh(Cpu *c) {
    bool icon=c->x[0] && c->x[0]==mobile_boss_icon && mobile_boss_last_ready==0;
    if(!icon && (!mobile_boss_background || c->x[0]!=mobile_boss_background))return -1;
    Cpu saved=*c;uint64_t mesh=c->x[1];
    guest_invoke2(c,0x7000aed470ull,mesh,0);
    if(icon) {
        // Three tapered spiral arms, with a one-unit feather on each edge.
        // Unlike a thresholded glow texture this stays sharp at any UI scale.
        for(unsigned arm=0;arm<3;++arm) {
            for(unsigned i=0;i<=64;++i) {
                float t=i/64.f,angle=arm*2.09439510239f+t*3.8f,r=.12f+.78f*t;
                float co=cosf(angle),si=sinf(angle),dx=.78f*co-r*3.8f*si,dy=.78f*si+r*3.8f*co;
                float length=hypotf(dx,dy),width=.135f*powf(fmaxf(0,sinf(t*3.14159265359f)),.7f);
                for(unsigned edge=0;edge<4;++edge) {
                    float sign=edge<2?-1:1,feather=(edge==0 || edge==3)?1.f/mobile_boss_icon_radius:0;
                    float offset=sign*(width+feather);
                    float v[]={(r*co-dy/length*offset)*mobile_boss_icon_radius,
                               (r*si+dx/length*offset)*mobile_boss_icon_radius,0,.5f,.5f};
                    *c=saved;c->x[0]=mesh;c->x[1]=edge==0 || edge==3?0x002e2e21u:0xeb2e2e21u;c->x[2]=0;
                    for(unsigned k=0;k<5;++k)memcpy(&c->v[k],v+k,4);
                    guest_call(c,0x7000aedd60ull);
                }
            }
            for(unsigned i=0;i<64;++i)for(unsigned edge=0;edge<3;++edge) {
                unsigned a=arm*260+i*4+edge,b=a+4;
                guest_invoke4(c,0x7000aedf00ull,mesh,a,b,a+1);
                guest_invoke4(c,0x7000aedf00ull,mesh,a+1,b,b+1);
            }
        }
        *c=saved;return 1;
    }
    const unsigned count=64;
    float stroke=mobile_boss_radius*.10f; // Magic's authored border is 10/102 of its radius
    for(unsigned i=0;i<count;++i) {
        float angle=i*(6.28318530718f/count);
        for(unsigned ring=0;ring<5;++ring) {
            float radius=ring==0?0:mobile_boss_radius-(ring==1?stroke+1:ring==2?stroke:ring==3?1:0);
            float vertex[]={cosf(angle)*radius,sinf(angle)*radius,0,.5f,.5f};
            *c=saved;c->x[0]=mesh;
            c->x[1]=ring<2?0x995a5739u:ring==4?0x00312d1bu:0xff312d1bu;
            c->x[2]=0;
            for(unsigned k=0;k<5;++k)memcpy(&c->v[k],vertex+k,4);
            guest_call(c,0x7000aedd60ull);
        }
    }
    for(unsigned i=0;i<count;++i) {
        unsigned a=i*5,b=((i+1)%count)*5;
        guest_invoke4(c,0x7000aedf00ull,mesh,a,b+1,a+1);
        for(unsigned ring=1;ring<4;++ring) {
            guest_invoke4(c,0x7000aedf00ull,mesh,a+ring,b+ring,a+ring+1);
            guest_invoke4(c,0x7000aedf00ull,mesh,a+ring+1,b+ring,b+ring+1);
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
    android_primary_touch=(MenuMouse){.x=-10000,.y=-10000};
    mobile_boss_owner=mobile_boss_control=mobile_boss_native=mobile_boss_object=0;
    mobile_boss_icon=mobile_boss_background=mobile_boss_ready_sprite=0;
    mobile_boss_material=mobile_boss_plain_material=0;
#endif
}

static void hide_ui_module(Cpu *c, uint64_t list) {
    if (!footer_canvas_group_type)
        footer_canvas_group_type = guest_invoke2(c, 0x70008ea5c0ull, 0,
            guest_string(c, "UnityEngine.CanvasGroup, UnityEngine"));
    if (!footer_canvas_group_type) return;
    uint64_t object = guest_invoke2(c, 0x700121e720ull, list, 0);
    uint64_t group = guest_invoke2(c, 0x700121e790ull, list, footer_canvas_group_type);
    if (!group) group = guest_invoke2(c, 0x70012260e0ull, object, footer_canvas_group_type);
    if (!group) return;
    guest_set_vec2(c, 0x7000af7fb0ull, group, 0, 0); // CanvasGroup.alpha
    guest_invoke2(c, 0x7000af8110ull, group, 0);     // no invisible pointer targets
}

static void hide_menu_footer(Cpu *c, uint64_t list) {
    static const char *const names[] = {
        "GeneralMenuHintList", "MapHintList", "ProfileHintList", "ShopHintList",
        "DojoHintList", "PvPLeftHintList", "PvPRightHintList"
    };
    for (unsigned i = 0; i < sizeof names / sizeof names[0]; ++i) {
        if (!guest_object_named(c, list, names[i])) continue;
        hide_ui_module(c, list);
        LOG("[ui] hidden menu control footer\n");
        break;
    }
}

#ifdef __ANDROID__
static void mobile_tutorial_menu_target(Cpu *c,uint64_t object) {
    // The tutorial still owns this button's native click listener.
    // Keep its hit area on a visible navigation control after hiding
    // the footer, so the tutorial blocker cannot strand the player.
    uint64_t main=guest_invoke2(c,0x7000e27660ull,0,0);
    uint64_t navigation=main?LD64(main+0x108):0;
    uint64_t header=navigation?LD64(navigation+0x48):0;
    if(header) {
        uint64_t rect=guest_invoke2(c,0x7001226320ull,object,0);
        uint64_t parent=guest_invoke2(c,0x700121e6b0ull,header,0);
        hide_ui_module(c,rect);
        guest_invoke4(c,0x70015d8ec0ull,rect,parent,0,0);
        guest_set_vec2(c,0x700123a050ull,rect,0,0);
        guest_set_vec2(c,0x700123a250ull,rect,1,1);
        guest_set_vec2(c,0x700123a9d0ull,rect,.5f,.5f);
        guest_set_vec2(c,0x700123a550ull,rect,0,0);
        guest_set_vec2(c,0x700123a7d0ull,rect,0,0);
        LOG("[touch] tutorial menu target follows scene header\n");
    }
}
#endif

static int platform_ui_hook(Cpu *c, uint64_t site) {
    if (site == 0x7000cdc110ull) { // HintList.Init
        hide_menu_footer(c, c->x[0]);
        return 0;
    }
#ifdef __ANDROID__
    switch (site) {
    case 0x7000f7cdc0ull: { // TutorialExtension.ActivateTutorialControl
        if(guest_object_named(c,c->x[1],"HintGroup_Common_Menu"))
            mobile_tutorial_menu_target(c,c->x[1]);
        return 0;
    }
    case 0x700011efb0ull:return mobile_boss_mesh(c);
    case 0x7000c90a10ull: { // GameController.Update (Refresh is inlined by IL2CPP)
        if(mobile_boss_owner!=c->x[0]) {
            uint64_t controller=guest_invoke2(c,0x7000c95b90ull,0,0);
            if(controller)mobile_boss_button(c,controller);
        }
        if(mobile_boss_owner==c->x[0] && mobile_boss_control)mobile_boss_update(c);
        return 0;
    }
    case 0x7000c8f630ull: // GameController.Init
    case 0x7000c90720ull: // GameController.Free
        mobile_boss_clear(c);return 0;
    case 0x70015bc860ull: // SwitchVideoPlayerController.Update
        // The host owns playback. Complete the original managed lifecycle as
        // soon as the overlay ends, instead of waiting for empty SDK tracks.
        if (c->x[0] && LD8(c->x[0]+0x28) && movie_playback_completed()) {
            guest_invoke2(c,0x70015bca30ull,c->x[0],0);
            LOG("[movie] managed playback completed\n");
            return 1;
        }
        return 0;
    case 0x70015bbf50ull:
        c->x[0]=movie_watched_to_end();return 1;
    case 0x7001227dd0ull: case 0x7001227e40ull: case 0x7001227eb0ull: {
        unsigned bits=site==0x7001227dd0ull?android_primary_touch.held:
                      site==0x7001227e40ull?android_primary_touch.pressed:android_primary_touch.released;
        c->x[0]=c->x[1]==0 && bits!=0;return 1;
    }
    case 0x7001227f20ull:
        c->v[0]=c->v[1]=c->v[2]=0;
        memcpy(&c->v[0],&android_primary_touch.x,4);
        memcpy(&c->v[1],&android_primary_touch.y,4);
        return 1;
    case 0x7000c95bf0ull: // GameControllerSwitch.Init
        // The mobile ActionButtons already display these cooldowns on the
        // right. Keep the Switch module's state/callbacks, hide its duplicate HUD.
        hide_ui_module(c, c->x[0]);
        return 0;
    case 0x7000e1a610ull: // AssemblyController.ShowController
        c->x[0] = 1; return 1;
    case 0x7000c8f80cull: // InitTouchControl: LeftContainer.SetActive
    case 0x7000c8f830ull: // InitTouchControl: ActionButtons.SetVisible
        c->x[1] = 1; return 0;
    case 0x7000c970b0ull: // TouchControlData.InputType
        // Sparring assigns a gamepad; Dojo can use the default touch team. Follow the
        // current player's native assignment, including its control index.
        {
            uint64_t data=guest_invoke2(c,0x7000c92820ull,0,0);
            if(data) {
                ST32(c->x[0]+0x14,LD32(data+0x18));
                c->x[0]=LD32(data+0x14);
            } else c->x[0]=0;
            return 1;
        }
    case 0x70012283e0ull: c->x[0] = android_touch_count; return 1;
    case 0x70012285c0ull: c->x[0] = 1; return 1;
    case 0x7001228450ull: { // Input.GetTouch returns a struct through x8
        unsigned index = (uint32_t)c->x[1];
        MenuTouch value = {.finger = -1, .phase = TOUCH_CANCELED};
        if (index < android_touch_count) value = android_touches[index];
        memcpy((void *)(uintptr_t)c->x[8], &value, sizeof value);
        return 1;
    }
    default: break;
    }
#endif
    return -1;
}
