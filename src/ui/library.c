#include "library_internal.h"
#include "qa/ui_menu_save.h"
#include "qa/application_character_selection.h"
#include <stdio.h>

static const struct { qa_game_family family; qa_product_edition edition; const char *label; } families[] = {
    {QA_GAME_Q1,QA_EDITION_CLASSIC,"Quake classic"}, {QA_GAME_Q1,QA_EDITION_RERELEASE,"Quake rerelease"},
    {QA_GAME_Q2,QA_EDITION_CLASSIC,"Quake II classic"}, {QA_GAME_Q2,QA_EDITION_RERELEASE,"Quake II rerelease"},
    {QA_GAME_Q3,QA_EDITION_CLASSIC,"Quake III Arena"}
};
static const char *field_labels[] = {"Game / mod","Map content","Starting map","Movement","Character source",
    "Character model","Local players","Weapons","Monsters","Difficulty","Game mode","Match rules",
    "Hook","Hook style","Offhand grenades","Monster source","Monster","Your team","Opponent","Environment","Doppler"};
static const qa_ui_library_field group_fields[][5] = {
    {QA_UI_LIBRARY_PRODUCT,QA_UI_LIBRARY_MAP_PRODUCT,QA_UI_LIBRARY_MAP},
    {QA_UI_LIBRARY_MOVEMENT,QA_UI_LIBRARY_CHARACTER,QA_UI_LIBRARY_MODEL,QA_UI_LIBRARY_SEATS},
    {QA_UI_LIBRARY_WEAPONS,QA_UI_LIBRARY_ENEMIES,QA_UI_LIBRARY_SKILL,QA_UI_LIBRARY_MODE,QA_UI_LIBRARY_RULES},
    {QA_UI_LIBRARY_GRAPPLE,QA_UI_LIBRARY_GRAPPLE_STYLE,QA_UI_LIBRARY_GRENADES}
};
static const size_t group_counts[] = {3,4,5,3};
static const char *group_titles[] = {"World","Player","Combat","Equipment"};
static bool action(void *,uint32_t,qa_ui_id,const qa_ui_action *,qa_error *);
static const char *edition_name(qa_product_edition edition) {
    static const char *names[]={"classic","rerelease","quakeworld","demo"};
    return (unsigned)edition<4?names[edition]:"";
}
static bool preset(const qa_product *p) {
    return p && p->builtin && p->availability==QA_CONTENT_INSTALLED &&
        (p->edition==QA_EDITION_CLASSIC || p->edition==QA_EDITION_RERELEASE) &&
        strcmp(p->campaign,"ctf") && strcmp(p->campaign,"lmctf");
}
qa_launch_draft *qa_ui_library_draft(qa_ui_library *m) { return m?m->draft:NULL; }
const qa_launch_choices *qa_ui_library_choices(const qa_ui_library *m) { return m?qa_launch_draft_choices(m->draft):NULL; }
const qa_catalog *qa_ui_library_catalog(const qa_ui_library *m) { return m?m->catalog:NULL; }
bool qa_ui_library_bind_services(qa_ui_library *m,const qa_ui_library_services *services,qa_error *error) {
    if (!m || !services || m->ui->handling || m->ui->drawing) return ui_fail(error,"selection services require an idle menu owner");
    m->services=*services; return true;
}
static qa_ui_control button(qa_ui_library *m,qa_ui_id id,const char *label,unsigned row,bool wide,bool enabled) {
    return (qa_ui_control){.id=id,.kind=QA_UI_BUTTON,.label=label,.rect={64,118+(float)row*34,wide?512:224,30},
        .visible=true,.enabled=enabled,.context=m,.action=action};
}
static bool open(qa_ui_library *m,qa_ui_id id,qa_error *error) { return qa_ui_open(m->ui,id,m->ui->time_ms,error); }
static void choices_clear(qa_ui_library *m) {
    for (size_t i=0;i<m->choice_count;++i) { free((void *)m->choices[i].id); free((void *)m->choices[i].label); free((void *)m->choices[i].unavailable); }
    m->choice_count=0; m->selected[0]=0;
}
static char *copy_text(const char *s,qa_error *error) {
    if (!s) return NULL;
    char *out=malloc(strlen(s)+1);
    if (!out) qa_error_set(error,QA_ERROR_MEMORY,0,"retaining startup selection text");
    else strcpy(out,s);
    return out;
}
static bool choice_add(qa_ui_library *m,const char *id,const char *label,const char *reason,qa_error *error) {
    if (!ui_reserve((void **)&m->choices,&m->choice_capacity,m->choice_count+1,sizeof(*m->choices),error)) return false;
    qa_ui_library_choice c={copy_text(id,error),copy_text(label,error),copy_text(reason,error)};
    if (!c.id || !c.label || (reason && !c.unavailable)) { free((void *)c.id); free((void *)c.label); free((void *)c.unavailable); return false; }
    m->choices[m->choice_count++]=c; return true;
}
static const qa_launch_provider *role_provider(const qa_launch_choices *v,qa_launch_role role) {
    const qa_launch_binding *b=qa_launch_binding_for(v,(qa_launch_scope){.kind=QA_SCOPE_DEFAULT_PLAYER},role,"");
    for (size_t i=0;b && i<v->provider_count;++i) if (!strcmp(v->providers[i].instance,b->instance)) return v->providers+i;
    return NULL;
}
static bool launch_seat(qa_launch_draft *draft,const qa_launch_seat *input,qa_error *error) {
    qa_launch_seat seat=*input;
    if (seat.local && !seat.character_model && !seat.character_skin && !seat.character_head_model && !seat.character_head_skin) {
        const qa_launch_provider *provider=role_provider(qa_launch_draft_choices(draft),QA_ROLE_CHARACTER);
        const qa_product *p=provider?qa_catalog_product(qa_launch_draft_catalog(draft),provider->product):NULL;
        qa_native_q3_character_declaration d;
        if (!p || !qa_native_q3_character_default_declaration(p->family,&d,error)) return false;
        seat.character_model=d.model; seat.character_skin=d.skin; seat.character_head_model=d.head_model; seat.character_head_skin=d.head_skin;
    }
    return qa_launch_set_seat(draft,&seat,error);
}
static bool select_preset(qa_ui_library *m,qa_product_id product,bool authored,qa_error *error) {
    qa_launch_draft *draft=NULL;
    if (!qa_launch_draft_create(m->catalog,product,NULL,&draft,error)) return false;
    size_t count=authored?1:m->local_player_count;
    for (size_t i=0;i<count;++i) if (!launch_seat(draft,&m->local_players[i].seat,error)) { qa_launch_draft_destroy(draft); return false; }
    const qa_product *p=qa_catalog_product(m->catalog,product);
    qa_launch_world world=qa_launch_draft_choices(draft)->world;
    if (authored && p->family==QA_GAME_Q3) world.skill=1;
    if (!qa_launch_set_world(draft,&world,error)) { qa_launch_draft_destroy(draft); return false; }
    qa_launch_draft_destroy(m->draft); m->draft=draft;
    m->mode_preference=authored?QA_MODE_SINGLE_PLAYER:qa_launch_draft_choices(draft)->modes[0].rules.kind;
    if (m->mode_preference!=QA_MODE_SINGLE_PLAYER && m->mode_preference!=QA_MODE_COOPERATIVE) m->mode_preference=QA_MODE_FFA;
    if (authored) { m->native_product=product; m->native_skill=p->family==QA_GAME_Q3?2:1; m->arena_number=-1; }
    m->status[0]=0; return true;
}
bool qa_ui_library_select_mode(qa_ui_library *m,qa_mode_kind kind,qa_error *error) {
    const qa_launch_choices *v=qa_ui_library_choices(m);
    if (!v || !v->mode_count || (kind!=QA_MODE_SINGLE_PLAYER && kind!=QA_MODE_COOPERATIVE && kind!=QA_MODE_FFA))
        return ui_fail(error,"startup mode requires a selected game");
    if (m->services.select) return m->services.select(m->services.context,m,QA_UI_LIBRARY_MODE,NULL,
        kind==QA_MODE_SINGLE_PLAYER?"singleplayer":kind==QA_MODE_COOPERATIVE?"coop":"deathmatch",error);
    qa_launch_mode mode=v->modes[0];
    mode.rules=qa_mode_defaults(mode.rules.source,kind);
    const qa_product *p=qa_catalog_product(m->catalog,v->world.preset);
    mode.rules.q2_rerelease=p && p->family==QA_GAME_Q2 && p->edition==QA_EDITION_RERELEASE;
    if (!qa_launch_set_mode(m->draft,&mode,error)) return false;
    m->mode_preference=kind; return true;
}
static bool select_role(qa_ui_library *m,qa_launch_role role,qa_product_id product,qa_error *error) {
    qa_launch_draft *defaults=NULL;
    if (!qa_launch_draft_create(m->catalog,product,NULL,&defaults,error)) return false;
    const qa_launch_choices *v=qa_launch_draft_choices(defaults);
    qa_launch_provider provider=v->providers[0];
    provider.instance=role==QA_ROLE_MOVEMENT?"startup:movement":"startup:character";
    bool ok=qa_launch_set_provider(m->draft,&provider,error);
    const qa_launch_role character_roles[]={QA_ROLE_CHARACTER,QA_ROLE_BODY,QA_ROLE_SKIN,QA_ROLE_VOICE};
    size_t count=role==QA_ROLE_MOVEMENT?1:4;
    for (size_t i=0;i<count && ok;++i) {
        qa_launch_role actual=role==QA_ROLE_MOVEMENT?role:character_roles[i];
        const qa_launch_binding *original=qa_launch_binding_for(v,(qa_launch_scope){.kind=QA_SCOPE_DEFAULT_PLAYER},actual,"");
        qa_launch_binding b=*original; b.instance=provider.instance; ok=qa_launch_bind(m->draft,&b,error);
    }
    qa_launch_draft_destroy(defaults); return ok;
}
static bool product_choices(qa_ui_library *m,qa_ui_library_field field,qa_error *error) {
    const qa_launch_choices *v=qa_ui_library_choices(m);
    qa_product_id selected=v->world.preset;
    if (field==QA_UI_LIBRARY_MAP_PRODUCT) selected=v->world.geometry;
    if (field==QA_UI_LIBRARY_MOVEMENT || field==QA_UI_LIBRARY_CHARACTER) {
        const qa_launch_provider *p=role_provider(v,field==QA_UI_LIBRARY_MOVEMENT?QA_ROLE_MOVEMENT:QA_ROLE_CHARACTER);
        selected=p?p->product:0;
    }
    const qa_product *selected_product=qa_catalog_product(m->catalog,selected);
    if (selected_product) snprintf(m->selected,sizeof(m->selected),"%s",selected_product->key);
    for (size_t i=0;i<qa_catalog_count(m->catalog);++i) {
        const qa_product *p=qa_catalog_at(m->catalog,i);
        if (field==QA_UI_LIBRARY_MOVEMENT || field==QA_UI_LIBRARY_CHARACTER) {
            bool base=(!strcmp(p->campaign,"id1") && p->edition!=QA_EDITION_DEMO) ||
                (!strcmp(p->campaign,"baseq2") && p->edition!=QA_EDITION_DEMO) || !strcmp(p->key,"q3-baseq3");
            if (p->edition==QA_EDITION_QUAKEWORLD) base=field==QA_UI_LIBRARY_MOVEMENT;
            if (!base && p->id!=selected && p->id!=v->world.preset) continue;
        }
        char label[512]; snprintf(label,sizeof(label),"%s (%s)",p->title,edition_name(p->edition));
        if (!choice_add(m,p->key,label,p->availability==QA_CONTENT_INSTALLED?NULL:"Content is not installed",error)) return false;
    }
    return true;
}
static int choice_compare(const void *a,const void *b) { return strcmp(((const qa_ui_library_choice *)a)->id,((const qa_ui_library_choice *)b)->id); }
static bool map_choices(qa_ui_library *m,qa_error *error) {
    const qa_launch_choices *v=qa_ui_library_choices(m); snprintf(m->selected,sizeof(m->selected),"%s",v->world.map);
    size_t count; const qa_catalog_start *starts=qa_catalog_starts(m->catalog,v->world.geometry,NULL,&count);
    for (size_t i=0;i<count;++i) {
        bool duplicate=false; for (size_t j=0;j<m->choice_count;++j) duplicate|=!strcmp(m->choices[j].id,starts[i].path);
        if (!duplicate && !choice_add(m,starts[i].path,starts[i].title,NULL,error)) return false;
    }
    size_t authored=m->choice_count; const qa_catalog_map *maps=qa_catalog_maps(m->catalog,v->world.geometry,&count);
    for (size_t i=0;i<count;++i) {
        bool duplicate=false; for (size_t j=0;j<authored;++j) duplicate|=!strcmp(m->choices[j].id,maps[i].path);
        if (!duplicate && !choice_add(m,maps[i].path,maps[i].path,NULL,error)) return false;
    }
    if (m->choice_count>authored) qsort(m->choices+authored,m->choice_count-authored,sizeof(*m->choices),choice_compare);
    return true;
}
static bool selection_choices(qa_ui_library *m,qa_ui_library_field field,qa_error *error) {
    choices_clear(m); const qa_launch_choices *v=qa_ui_library_choices(m);
    if (!v) return true;
    if (field<=QA_UI_LIBRARY_CHARACTER && field!=QA_UI_LIBRARY_MAP) return product_choices(m,field,error);
    if (field==QA_UI_LIBRARY_MAP) return map_choices(m,error);
    if (field==QA_UI_LIBRARY_DOPPLER) {
        snprintf(m->selected,sizeof(m->selected),"%s",v->world.doppler?"source":"disabled");
        return choice_add(m,"source","Game default",NULL,error) && choice_add(m,"disabled","Off",NULL,error);
    }
    if (field==QA_UI_LIBRARY_SKILL) {
        static const char *labels[]={"Easy","Normal","Hard","Nightmare"};
        snprintf(m->selected,sizeof(m->selected),"%d",v->world.skill);
        for (unsigned i=0;i<4;++i) { char id[2]={(char)('0'+i),0}; if (!choice_add(m,id,labels[i],NULL,error)) return false; }
        return true;
    }
    if (field==QA_UI_LIBRARY_MODE) {
        static const char *ids[]={"singleplayer","coop","deathmatch"},*labels[]={"Single player","Cooperative","Deathmatch"};
        qa_mode_kind kind=m->mode_preference;
        snprintf(m->selected,sizeof(m->selected),"%s",ids[kind==QA_MODE_SINGLE_PLAYER?0:kind==QA_MODE_COOPERATIVE?1:2]);
        for (unsigned i=0;i<3;++i) if (!choice_add(m,ids[i],labels[i],NULL,error)) return false;
        return true;
    }
    if (m->services.choices) {
        const qa_ui_library_choice *choices=NULL; size_t count=0; const char *selected=NULL;
        if (!m->services.choices(m->services.context,m,field,m->monster_classname,&choices,&count,&selected,error)) return false;
        snprintf(m->selected,sizeof(m->selected),"%s",selected?selected:"");
        for (size_t i=0;i<count;++i) if (!choice_add(m,choices[i].id,choices[i].label,choices[i].unavailable,error)) return false;
    }
    return true;
}
static const char *selected_label(qa_ui_library *m) {
    for (size_t i=0;i<m->choice_count;++i) if (!strcmp(m->selected,m->choices[i].id)) return m->choices[i].label;
    return m->selected;
}
static bool select_custom_product(qa_ui_library *m,qa_product_id product,qa_error *error) {
    qa_launch_draft *candidate=NULL,*defaults=NULL;
    if (!qa_launch_draft_copy(m->draft,&candidate,error) || !qa_launch_draft_create(m->catalog,product,NULL,&defaults,error)) {
        qa_launch_draft_destroy(candidate); return false;
    }
    const qa_launch_choices *old=qa_ui_library_choices(m),*next=qa_launch_draft_choices(defaults);
    qa_ui_library staged=*m; staged.draft=candidate;
    bool ok=true;
    const qa_launch_role selected_roles[]={QA_ROLE_MOVEMENT,QA_ROLE_CHARACTER};
    for (size_t i=0;i<2 && ok;++i) {
        const qa_launch_provider *p=role_provider(old,selected_roles[i]);
        if (p && !strcmp(p->instance,"native:primary")) {
            ok=select_role(&staged,selected_roles[i],p->product,error);
            if (selected_roles[i]==QA_ROLE_CHARACTER) {
                const qa_launch_role definitions[]={QA_ROLE_BODY,QA_ROLE_SKIN,QA_ROLE_VOICE};
                for (size_t j=0;j<3 && ok;++j) {
                    const qa_launch_binding *b=qa_launch_binding_for(old,(qa_launch_scope){.kind=QA_SCOPE_DEFAULT_PLAYER},definitions[j],"");
                    if (b) { qa_launch_binding retained=*b; retained.instance="startup:character"; ok=qa_launch_bind(candidate,&retained,error); }
                }
            }
        }
    }
    qa_launch_world world=next->world;
    world.skill=old->world.skill; world.doppler=old->world.doppler;
    world.environment=old->world.environment; world.environment_product=old->world.environment_product; world.environment_path=old->world.environment_path;
    if (ok) ok=qa_launch_set_world(candidate,&world,error) && qa_launch_set_provider(candidate,next->providers,error);
    const qa_launch_role campaign_roles[]={QA_ROLE_CAMPAIGN,QA_ROLE_TRANSITION};
    for (size_t i=0;i<2 && ok;++i) if (!qa_launch_binding_for(next,(qa_launch_scope){.kind=QA_SCOPE_WORLD},campaign_roles[i],""))
        ok=qa_launch_unbind(candidate,(qa_launch_scope){.kind=QA_SCOPE_WORLD},campaign_roles[i],"",error);
    for (size_t i=0;i<next->binding_count && ok;++i) if (next->bindings[i].scope.kind==QA_SCOPE_WORLD) ok=qa_launch_bind(candidate,next->bindings+i,error);
    if (ok && old->mode_count) {
        if (strcmp(old->modes[0].instance,"native:primary")) ok=qa_launch_remove_mode(candidate,old->modes[0].instance,error);
        qa_launch_mode mode=old->modes[0]; mode.instance="native:primary";
        if (ok) ok=qa_launch_set_mode(candidate,&mode,error);
    }
    qa_launch_draft_destroy(defaults);
    if (!ok) { qa_launch_draft_destroy(candidate); return false; }
    qa_launch_draft_destroy(m->draft); m->draft=candidate;
    return true;
}
static bool select_choice(qa_ui_library *m,const char *id,qa_error *error) {
    qa_ui_library_field field=m->field;
    if (field==QA_UI_LIBRARY_DOPPLER) {
        qa_launch_world world=qa_ui_library_choices(m)->world; world.doppler=strcmp(id,"disabled")!=0;
        return qa_launch_set_world(m->draft,&world,error);
    }
    if (field==QA_UI_LIBRARY_PRODUCT) {
        const qa_product *p=qa_catalog_find(m->catalog,id);
        if (!p || !select_custom_product(m,p->id,error)) return false;
        return !m->services.select || m->services.select(m->services.context,m,field,NULL,id,error);
    }
    if (field==QA_UI_LIBRARY_MAP_PRODUCT || field==QA_UI_LIBRARY_MAP) {
        qa_launch_world world=qa_ui_library_choices(m)->world;
        if (field==QA_UI_LIBRARY_MAP_PRODUCT) {
            const qa_product *p=qa_catalog_find(m->catalog,id); if (!p) return false;
            qa_launch_draft *defaults=NULL; if (!qa_launch_draft_create(m->catalog,p->id,NULL,&defaults,error)) return false;
            world.geometry=p->id; world.map=qa_launch_draft_choices(defaults)->world.map;
            world.start_command=qa_launch_draft_choices(defaults)->world.start_command;
            bool ok=qa_launch_set_world(m->draft,&world,error); qa_launch_draft_destroy(defaults); return ok;
        }
        world.map=id; world.start_command="";
        size_t count; const qa_catalog_episode *episode;
        const qa_catalog_start *starts=qa_catalog_starts(m->catalog,world.geometry,&episode,&count);
        for (size_t i=0;i<count;++i) if (!strcmp(id,starts[i].path)) {
            world.start_command=i==0 && episode && episode->command && *episode->command?episode->command:starts[i].bsp; break;
        }
        return qa_launch_set_world(m->draft,&world,error);
    }
    if (field==QA_UI_LIBRARY_MOVEMENT || field==QA_UI_LIBRARY_CHARACTER) {
        const qa_product *p=qa_catalog_find(m->catalog,id);
        if (!p || !select_role(m,field==QA_UI_LIBRARY_MOVEMENT?QA_ROLE_MOVEMENT:QA_ROLE_CHARACTER,p->id,error)) return false;
        return field!=QA_UI_LIBRARY_CHARACTER || !m->services.select || m->services.select(m->services.context,m,field,NULL,id,error);
    }
    if (field==QA_UI_LIBRARY_SKILL) { qa_launch_world world=qa_ui_library_choices(m)->world; world.skill=id[0]-'0'; return qa_launch_set_world(m->draft,&world,error); }
    if (field==QA_UI_LIBRARY_MODE) return qa_ui_library_select_mode(m,!strcmp(id,"singleplayer")?QA_MODE_SINGLE_PLAYER:!strcmp(id,"coop")?QA_MODE_COOPERATIVE:QA_MODE_FFA,error);
    return m->services.select && m->services.select(m->services.context,m,field,m->monster_classname,id,error);
}
static bool launch(qa_ui_library *m,bool authored,qa_error *error) {
    m->last_authored=authored;
    qa_launch_world world=qa_ui_library_choices(m)->world;
    const qa_product *p=qa_catalog_product(m->catalog,world.preset);
    const char *arena_map=NULL;
    if (authored) {
        world.skill=p->family==QA_GAME_Q3?1:m->native_skill;
        if (m->arena_number>=0 && m->services.arenas) {
            const qa_base_arena_catalog *catalog=NULL; const qa_arena_progress *progress=NULL;
            if (!m->services.arenas(m->services.context,m,&catalog,&progress,error)) return false;
            for (size_t i=0;i<qa_base_arena_catalog_count(catalog);++i) {
                const qa_base_arena *arena=qa_base_arena_catalog_at(catalog,i);
                if (arena->number==m->arena_number) { arena_map=arena->map; break; }
            }
        }
        if (!qa_launch_set_world(m->draft,&world,error)) return false;
    }
    bool ok=m->services.play?m->services.play(m->services.context,m,m->draft,authored,arena_map,m->native_skill,error):
        qa_application_apply(m->application,m->draft,error);
    if (!ok) { snprintf(m->status,sizeof(m->status),"%s",error?error->message:"Launch rejected"); if (error) *error=(qa_error){0}; return true; }
    return qa_ui_close_all(m->ui,m->ui->time_ms,error);
}
static qa_ui_id current_menu(qa_ui_library *m) { return m->ui->depth?m->ui->stack[m->ui->depth-1].menu:0; }
static bool action_impl(void *context,uint32_t seat,qa_ui_id control,const qa_ui_action *event,qa_error *error) {
    qa_ui_library *m=context; (void)seat; qa_ui_id page=current_menu(m);
    if (control==99 && event->kind==QA_UI_ACTIVATE) return qa_ui_close(m->ui,m->ui->time_ms,error);
    if (page==m->menu && event->kind==QA_UI_ACTIVATE) {
        m->status[0]=0;
        if (control==6) return open(m,QA_UI_LIBRARY_CUSTOM,error);
        if (control>=1 && control<=5) { m->family=families[control-1].family; m->edition=families[control-1].edition; return open(m,QA_UI_LIBRARY_CAMPAIGN,error); }
    }
    if (page==QA_UI_LIBRARY_CAMPAIGN && event->kind==QA_UI_ACTIVATE && control>=1 && control<=m->choice_count) {
        const qa_product *p=qa_catalog_find(m->catalog,m->choices[control-1].id);
        if (!p || !select_preset(m,p->id,true,error)) return false;
        if (!strcmp(p->key,"q3-baseq3") && m->services.prepare_arenas && m->services.arenas) {
            if (!m->services.prepare_arenas(m->services.context,m,error)) return false;
            m->arena_tier=INT32_MIN; return open(m,QA_UI_LIBRARY_ARENAS,error);
        }
        return open(m,QA_UI_LIBRARY_DIFFICULTY,error);
    }
    if (page==QA_UI_LIBRARY_DIFFICULTY && event->kind==QA_UI_ACTIVATE) {
        const qa_product *p=qa_catalog_product(m->catalog,m->native_product);
        if (control>=1 && control<=(p->family==QA_GAME_Q3?5u:4u)) { m->native_skill=(int32_t)control-(p->family==QA_GAME_Q3?0:1); return true; }
        if (control==8) return launch(m,true,error);
        if (control==20 || control==21) { m->field=control==20?QA_UI_LIBRARY_TEAM_PLAYER:QA_UI_LIBRARY_TEAM_OPPONENT; m->page=0; return open(m,QA_UI_LIBRARY_CHOICES,error); }
    }
    if (page==QA_UI_LIBRARY_CUSTOM && event->kind==QA_UI_ACTIVATE) {
        if (control>=1 && control<=4) { m->group=(unsigned)control-1; return open(m,QA_UI_LIBRARY_CATEGORY,error); }
        if (control==5) return open(m,m->services.hosting_menu,error);
        if (control==6) return open(m,m->services.mods_menu,error);
        if (control==7) return launch(m,false,error);
        if (control==9) return open(m,m->services.browser_menu,error);
        if (control==10) return open(m,m->services.lobby_menu,error);
    }
    if (page==QA_UI_LIBRARY_CATEGORY && (event->kind==QA_UI_ACTIVATE || event->kind==QA_UI_SELECT || event->kind==QA_UI_CHANGE_NUMBER)) {
        if (control>=1 && control<=group_counts[m->group]) {
            m->field=group_fields[m->group][control-1]; m->page=0; free(m->monster_classname); m->monster_classname=NULL;
            if (m->field==QA_UI_LIBRARY_GRAPPLE && event->kind==QA_UI_SELECT) {
                if (!selection_choices(m,m->field,error) || event->value.row>=m->choice_count) return false;
                size_t row=0;
                for (size_t i=0;i<m->choice_count;++i) if (!m->choices[i].unavailable) {
                    if (row++==event->value.row) return select_choice(m,m->choices[i].id,error);
                }
                return true;
            }
            if (m->field==QA_UI_LIBRARY_GRENADES && event->kind==QA_UI_CHANGE_NUMBER) return select_choice(m,event->value.number!=0?"enabled":"disabled",error);
            return open(m,QA_UI_LIBRARY_CHOICES,error);
        }
    }
    if (page==QA_UI_LIBRARY_CHOICES && event->kind==QA_UI_ACTIVATE) {
        size_t pages=(m->choice_count+6)/7; if (!pages) pages=1;
        if (control==90 || control==91) { m->page=(control==90?(m->page?m->page-1:pages-1):(m->page+1)%pages); return true; }
        size_t index=m->page*7+(size_t)control-1;
        if (control>=1 && control<=7 && index<m->choice_count) {
            bool roster=m->field==QA_UI_LIBRARY_ENEMIES && !strcmp(m->choices[index].id,"custom");
            if (!select_choice(m,m->choices[index].id,error)) return false;
            m->status[0]=0;
            if (!qa_ui_close(m->ui,m->ui->time_ms,error)) return false;
            if (roster) { m->roster_page=0; return open(m,QA_UI_LIBRARY_ROSTER,error); }
            return true;
        }
    }
    if (page==QA_UI_LIBRARY_ROSTER && event->kind==QA_UI_ACTIVATE && m->services.roster) {
        const qa_ui_library_roster_row *rows=NULL; size_t count=0; const char *source=NULL;
        if (!m->services.roster(m->services.context,m,&rows,&count,&source,error)) return false;
        size_t pages=(count+6)/7; if (!pages) pages=1;
        if (control==90 || control==91) { m->roster_page=(control==90?(m->roster_page?m->roster_page-1:pages-1):(m->roster_page+1)%pages); return true; }
        if (control==20) { m->field=QA_UI_LIBRARY_MONSTER_SOURCE; free(m->monster_classname); m->monster_classname=NULL; }
        else {
            size_t index=m->roster_page*7+(size_t)control-1; if (!control || control>7 || index>=count) return true;
            m->field=QA_UI_LIBRARY_MONSTER_CLASS; free(m->monster_classname); m->monster_classname=copy_text(rows[index].classname,error);
            if (rows[index].classname && !m->monster_classname) return false;
        }
        m->page=0; return open(m,QA_UI_LIBRARY_CHOICES,error);
    }
    if (page==QA_UI_LIBRARY_ARENAS) {
        if (control==1 && event->kind==QA_UI_SELECT && event->value.row<m->tier_count) { m->arena_tier=m->tiers[event->value.row]; m->arena_number=-1; return true; }
        if (control==2 && (event->kind==QA_UI_SELECT || event->kind==QA_UI_ROW_ACTIVATE) && event->value.row<m->arena_count) {
            m->arena_number=m->arena_numbers[event->value.row]; if (event->kind==QA_UI_SELECT) return true;
            control=5;
        }
        if (control==5 && (event->kind==QA_UI_ACTIVATE || event->kind==QA_UI_ROW_ACTIVATE)) {
            const qa_base_arena_catalog *catalog=NULL; const qa_arena_progress *progress=NULL; bool available=false;
            if (!m->services.arenas(m->services.context,m,&catalog,&progress,error) || !qa_arena_progress_available(progress,m->arena_number,&available,error)) return false;
            return !available || open(m,QA_UI_LIBRARY_DIFFICULTY,error);
        }
    }
    return true;
}
static bool action(void *context,uint32_t seat,qa_ui_id control,const qa_ui_action *event,qa_error *error) {
    qa_ui_library *m=context;
    if (action_impl(context,seat,control,event,error)) return true;
    if (error && error->code==QA_ERROR_MEMORY) return false;
    snprintf(m->status,sizeof(m->status),"%s",error && *error->message?error->message:"Selection is unavailable");
    if (error) *error=(qa_error){0};
    return true;
}
static qa_ui_control text_control(qa_ui_library *m,qa_ui_id id,const char *label,float x,float y,float scale,float width,bool accent) {
    return (qa_ui_control){.id=id,.kind=QA_UI_TEXT,.label=label,.rect={x,y,width,8*scale},.visible=true,.context=m,
        .value.text={.scale=scale,.fit_width=width,.accent=accent,.source=true}};
}
static int32_t arena_tier(const qa_base_arena *arena) {
    char special[9]={0}; size_t length=strlen(arena->special);
    if (length<sizeof(special)) for (size_t i=0;i<length;++i) special[i]=arena->special[i]>='A' && arena->special[i]<='Z'?(char)(arena->special[i]+'a'-'A'):arena->special[i];
    return !strcmp(special,"training")?-1:!strcmp(special,"final")?-2:arena->number/4+1;
}
static bool arena_menu(qa_ui_library *m,size_t *count,qa_error *error) {
    const qa_base_arena_catalog *catalog=NULL; const qa_arena_progress *progress=NULL;
    if (!m->services.arenas || !m->services.arenas(m->services.context,m,&catalog,&progress,error) || !catalog || !progress)
        return ui_fail(error,"arena selection requires its authored catalog and profile");
    size_t maximum=qa_base_arena_catalog_count(catalog);
    if (!ui_reserve((void **)&m->arena_rows,&m->arena_capacity,maximum,sizeof(*m->arena_rows),error) ||
        !ui_reserve((void **)&m->arena_numbers,&m->arena_number_capacity,maximum,sizeof(*m->arena_numbers),error) ||
        !ui_reserve((void **)&m->tier_labels,&m->tier_capacity,maximum,sizeof(*m->tier_labels),error) ||
        !ui_reserve((void **)&m->tiers,&m->tier_number_capacity,maximum,sizeof(*m->tiers),error)) return false;
    for (size_t i=0;i<m->tier_count;++i) free((void *)m->tier_labels[i]);
    m->tier_count=0;
    for (size_t i=0;i<m->arena_count;++i) free((void *)m->arena_rows[i].detail);
    m->arena_count=0;
    qa_arena_catalog levels=qa_base_arena_catalog_levels(catalog);
    for (size_t i=0;i<maximum;++i) {
        const qa_base_arena *a=qa_base_arena_catalog_at(catalog,i);
        if (!*a->special && a->number>=levels.regular_levels) continue;
        int32_t tier=arena_tier(a);
        bool found=false; for (size_t j=0;j<m->tier_count;++j) found|=m->tiers[j]==tier;
        if (found) continue;
        char label[32]; snprintf(label,sizeof(label),tier==-1?"Training":tier==-2?"Final arena":"Tier %d",tier);
        char *owned=copy_text(label,error); if (!owned) return false;
        m->tier_labels[m->tier_count]=owned; m->tiers[m->tier_count++]=tier;
    }
    /* UI arena selection order is authored, independently of GAME numbering. */
    for (size_t i=0;i<m->tier_count;++i) for (size_t j=i+1;j<m->tier_count;++j) {
        int32_t ai=INT32_MAX,aj=INT32_MAX;
        for (size_t k=0;k<maximum;++k) {
            const qa_base_arena *a=qa_base_arena_catalog_at(catalog,k);
            int32_t tier=arena_tier(a);
            if (tier==m->tiers[i] && a->selection<ai) ai=a->selection;
            if (tier==m->tiers[j] && a->selection<aj) aj=a->selection;
        }
        if (aj<ai) { int32_t t=m->tiers[i]; m->tiers[i]=m->tiers[j]; m->tiers[j]=t;
            const char *s=m->tier_labels[i]; m->tier_labels[i]=m->tier_labels[j]; m->tier_labels[j]=s; }
    }
    if (m->arena_tier==INT32_MIN && m->tier_count) {
        int32_t selected=-1;
        const qa_cvar_view *saved=qa_cvars_find(progress->cvars,"ui_spSelection");
        if (saved && strspn(saved->value," \t\r\n\v\f")!=strlen(saved->value)) {
            double selection=0; qa_error ignored={0};
            if (qa_parse_ecmascript_number((qa_bytes){(const uint8_t *)saved->value,strlen(saved->value)},&selection,&ignored)) {
                for (size_t i=0;i<maximum;++i) {
                    const qa_base_arena *a=qa_base_arena_catalog_at(catalog,i); bool available=false;
                    if ((double)a->selection==selection && (*a->special || a->number<levels.regular_levels)) {
                        if (!qa_arena_progress_available(progress,a->number,&available,error)) return false;
                        if (available) { selected=a->number; break; }
                    }
                }
            }
        }
        if (selected<0 && !qa_arena_progress_current(progress,&selected,error)) return false;
        m->arena_tier=m->tiers[0];
        for (size_t i=0;i<maximum;++i) { const qa_base_arena *a=qa_base_arena_catalog_at(catalog,i); if (a->number==selected) { m->arena_number=selected; m->arena_tier=arena_tier(a); break; } }
    }
    size_t selected_tier=0,selected_row=0; const qa_base_arena *current=NULL;
    for (size_t i=0;i<m->tier_count;++i) if (m->tiers[i]==m->arena_tier) selected_tier=i;
    for (size_t i=0;i<maximum;++i) {
        const qa_base_arena *a=qa_base_arena_catalog_at(catalog,i);
        if (!*a->special && a->number>=levels.regular_levels) continue;
        int32_t tier=arena_tier(a);
        if (tier!=m->arena_tier) continue;
        bool available=false; qa_arena_best best;
        if (!qa_arena_progress_available(progress,a->number,&available,error) || !qa_arena_progress_best(progress,a->number,&best,error)) return false;
        char detail[96];
        if (!available) snprintf(detail,sizeof(detail),"Locked");
        else if (!best.rank) snprintf(detail,sizeof(detail),"Not completed");
        else snprintf(detail,sizeof(detail),"Rank %d · Skill %d",best.rank,best.skill);
        char *owned=copy_text(detail,error); if (!owned) return false;
        m->arena_rows[m->arena_count]=(qa_ui_row){.key=a->map,.label=a->title,.detail=owned,.enabled=true};
        m->arena_numbers[m->arena_count++]=a->number;
    }
    for (size_t i=0;i<m->arena_count;++i) {
        if (m->arena_number<0) m->arena_number=m->arena_numbers[0];
        if (m->arena_numbers[i]==m->arena_number) selected_row=i;
    }
    for (size_t i=0;i<maximum;++i) { const qa_base_arena *a=qa_base_arena_catalog_at(catalog,i); if (a->number==m->arena_number) { current=a; break; } }
    m->controls[0]=button(m,1,"Tier",0,true,m->tier_count!=0); m->controls[0].kind=QA_UI_CHOICE;
    m->controls[0].rect=(qa_scene_rect_f){48,94,544,32};
    m->controls[0].value.choice.labels=m->tier_labels; m->controls[0].value.choice.count=m->tier_count; m->controls[0].value.choice.selected=selected_tier;
    m->controls[1]=button(m,2,"Arenas",0,true,m->arena_count!=0); m->controls[1].kind=QA_UI_LIST;
    m->controls[1].rect=(qa_scene_rect_f){48,144,544,156};
    m->controls[1].value.list.rows=m->arena_rows; m->controls[1].value.list.count=m->arena_count;
    m->controls[1].value.list.selected=selected_row; m->controls[1].value.list.row_height=39; m->controls[1].value.list.revision=++m->revision;
    snprintf(m->labels[2],sizeof(m->labels[2]),"%s",current?"Opponents: ":"No single-player arenas are available.");
    if (current) for (size_t i=0;i<current->bot_count;++i) { size_t used=strlen(m->labels[2]); snprintf(m->labels[2]+used,sizeof(m->labels[2])-used,"%s%s",i?", ":"",current->bots[i]); }
    if (current && !current->bot_count) snprintf(m->labels[2],sizeof(m->labels[2]),"Opponents: None");
    m->labels[3][0]=0;
    if (current) {
        char number[32];
        if (current->frag_limit>0) { if (!qa_format_number(current->frag_limit,number,error)) return false; snprintf(m->labels[3],sizeof(m->labels[3]),"%s frags",number); }
        if (current->time_limit>0) { if (!qa_format_number(current->time_limit,number,error)) return false; size_t used=strlen(m->labels[3]); snprintf(m->labels[3]+used,sizeof(m->labels[3])-used,"%s%s minutes",used?" · ":"",number); }
    }
    m->controls[2]=button(m,3,m->labels[2],0,true,false); m->controls[2].rect=(qa_scene_rect_f){48,318,544,30};
    m->controls[3]=button(m,4,m->labels[3],0,true,false); m->controls[3].rect=(qa_scene_rect_f){48,350,544,30};
    bool available=false;
    if (current && !qa_arena_progress_available(progress,current->number,&available,error)) return false;
    m->controls[4]=button(m,5,available?"Choose difficulty":"Win the preceding tier to unlock",0,true,available); m->controls[4].rect=(qa_scene_rect_f){48,392,544,32};
    m->controls[5]=button(m,99,"Back",0,true,true); m->controls[5].rect=(qa_scene_rect_f){48,436,544,32};
    *count=6; return true;
}
static bool summary(qa_ui_library *m,size_t *count,qa_error *error) {
    static const qa_ui_library_field fields[]={QA_UI_LIBRARY_PRODUCT,QA_UI_LIBRARY_MAP_PRODUCT,QA_UI_LIBRARY_MAP,
        QA_UI_LIBRARY_MOVEMENT,QA_UI_LIBRARY_CHARACTER,QA_UI_LIBRARY_MODEL,QA_UI_LIBRARY_WEAPONS,QA_UI_LIBRARY_ENEMIES,
        QA_UI_LIBRARY_GRAPPLE,QA_UI_LIBRARY_GRAPPLE_STYLE,QA_UI_LIBRARY_GRENADES,QA_UI_LIBRARY_MODE};
    const char *values[12]; char owned[12][512]; size_t rows=0;
    for (size_t i=0;i<12;++i) {
        if (!selection_choices(m,fields[i],error)) return false;
        if (fields[i]==QA_UI_LIBRARY_GRAPPLE_STYLE && !m->choice_count) continue;
        snprintf(owned[rows],sizeof(owned[rows]),"%s",selected_label(m)); values[rows]=field_labels[fields[i]]; ++rows;
    }
    m->controls[(*count)++]=text_control(m,150,"Your game",316,118,2,260,true);
    float height=fminf(30,300.f/(float)(rows?rows:1)),density=fminf(1,(height-2)/(8*(1.35f+2.1f)+1));
    for (size_t i=0;i<rows;++i) {
        size_t at=*count; snprintf(m->labels[at],sizeof(m->labels[at]),"%s",values[i]);
        m->controls[(*count)++]=text_control(m,151+i*2,m->labels[at],316,142+(float)i*height,1.35f*density,260,true);
        at=*count; snprintf(m->labels[at],sizeof(m->labels[at]),"%.511s",owned[i]);
        m->controls[(*count)++]=text_control(m,152+i*2,m->labels[at],316,142+(float)i*height+8*1.35f*density+density,2.1f*density,260,false);
    }
    return true;
}
static bool factory(void *context,uint32_t seat,qa_ui_menu *out,qa_error *error) {
    library_page *page=context; qa_ui_library *m=page->owner; qa_ui_id id=page->id; (void)seat;
    size_t count=0; const char *title="";
    if (id==m->menu) {
        title="Play a game";
        for (size_t i=0;i<5;++i) {
            bool available=false;
            for (size_t j=0;j<qa_catalog_count(m->catalog);++j) { const qa_product *p=qa_catalog_at(m->catalog,j); available|=preset(p) && p->family==families[i].family && p->edition==families[i].edition; }
            m->controls[count++]=button(m,i+1,families[i].label,(unsigned)i,true,available);
        }
        m->controls[count++]=button(m,6,"Custom game",5,true,m->draft!=NULL);
    } else if (id==QA_UI_LIBRARY_CAMPAIGN) {
        title=m->family==QA_GAME_Q3?"Quake III Arena":m->family==QA_GAME_Q2?(m->edition==QA_EDITION_CLASSIC?"Quake II classic":"Quake II rerelease"):(m->edition==QA_EDITION_CLASSIC?"Quake classic":"Quake rerelease");
        choices_clear(m);
        for (size_t i=0;i<qa_catalog_count(m->catalog);++i) {
            const qa_product *p=qa_catalog_at(m->catalog,i);
            if (!preset(p) || p->family!=m->family || p->edition!=m->edition) continue;
            char label[512]; snprintf(label,sizeof(label),"%s (%s)",p->title,edition_name(p->edition));
            if (!choice_add(m,p->key,label,NULL,error)) return false;
            m->controls[count]=button(m,count+1,m->choices[count].label,(unsigned)count,true,true); ++count;
        }
    } else if (id==QA_UI_LIBRARY_DIFFICULTY) {
        title="Difficulty";
        const qa_product *p=qa_catalog_product(m->catalog,m->native_product); if (!p) return ui_fail(error,"difficulty lost its selected campaign");
        static const char *q3[]={"I Can Win","Bring It On","Hurt Me Plenty","Hardcore","Nightmare"},*classic[]={"Easy","Normal","Hard","Nightmare"};
        for (unsigned i=0;i<(p->family==QA_GAME_Q3?5u:4u);++i) {
            int32_t skill=(int32_t)i+(p->family==QA_GAME_Q3?1:0);
            snprintf(m->labels[count],sizeof(m->labels[count]),"%s%s",m->native_skill==skill?"> ":"",p->family==QA_GAME_Q3?q3[i]:classic[i]);
            m->controls[count]=button(m,i+1,m->labels[count],i,true,true); ++count;
        }
        if (!strcmp(p->key,"q3-missionpack") && m->services.choices) for (unsigned side=0;side<2;++side) {
            qa_ui_library_field field=side?QA_UI_LIBRARY_TEAM_OPPONENT:QA_UI_LIBRARY_TEAM_PLAYER;
            if (!selection_choices(m,field,error)) return false;
            if (!m->choice_count) continue;
            snprintf(m->labels[count],sizeof(m->labels[count]),"%s: %.480s",field_labels[field],selected_label(m));
            m->controls[count]=button(m,20+side,m->labels[count],5+side,true,true); ++count;
        }
        m->controls[count++]=button(m,8,"Play",7,true,true);
        snprintf(m->labels[count],sizeof(m->labels[count]),"%s (%s)",p->title,edition_name(p->edition));
        m->controls[count]=text_control(m,180,m->labels[count],64,86,1.6f,512,false); ++count;
    } else if (id==QA_UI_LIBRARY_CUSTOM) {
        title="Custom game";
        for (unsigned i=0;i<4;++i) m->controls[count++]=button(m,i+1,group_titles[i],i,false,true);
        m->controls[count++]=button(m,5,m->services.hosting_label?m->services.hosting_label(m->services.context):"Network: local only",4,false,m->services.hosting_menu!=0);
        m->controls[count++]=button(m,6,"Mods",5,false,m->services.mods_menu!=0);
        m->controls[count++]=button(m,7,"Play",6,false,true);
        m->controls[count++]=button(m,99,"Back",7,false,true);
        if (m->services.browser_menu) m->controls[count++]=button(m,9,"Find servers",8,false,true);
        if (m->services.lobby_menu) m->controls[count++]=button(m,10,"Local lobby",9,false,true);
        if (!summary(m,&count,error)) return false;
    } else if (id==QA_UI_LIBRARY_CATEGORY) {
        title=group_titles[m->group]; unsigned row=0;
        for (size_t i=0;i<group_counts[m->group];++i) {
            qa_ui_library_field field=group_fields[m->group][i];
            if (!selection_choices(m,field,error)) return false;
            if (field==QA_UI_LIBRARY_GRAPPLE_STYLE && !m->choice_count) continue;
            if (field==QA_UI_LIBRARY_GRAPPLE) {
                size_t selected=0; for (size_t j=0;j<m->choice_count;++j) if (!strcmp(m->selected,m->choices[j].id)) selected=j;
                /* The donor removes unavailable inline choices. */
                size_t n=0;
                for (size_t j=0;j<m->choice_count && n<3;++j) if (!m->choices[j].unavailable) {
                    snprintf(m->inline_labels[n],sizeof(m->inline_labels[n]),"%.511s",m->choices[j].label);
                    m->inline_choices[n]=m->inline_labels[n]; ++n;
                }
                m->controls[count]=button(m,i+1,"Hook",row,true,n!=0); m->controls[count].kind=QA_UI_CHOICE;
                m->controls[count].value.choice.labels=m->inline_choices; m->controls[count].value.choice.count=n; m->controls[count].value.choice.selected=selected<n?selected:0;
            } else if (field==QA_UI_LIBRARY_GRENADES) {
                bool enabled=!strcmp(m->selected,"enabled"),available=false;
                for (size_t j=0;j<m->choice_count;++j) if (!strcmp(m->choices[j].id,"enabled")) available=!m->choices[j].unavailable;
                m->controls[count]=button(m,i+1,"Offhand grenades",row,true,enabled || available); m->controls[count].kind=QA_UI_TOGGLE; m->controls[count].value.checked=enabled;
            } else {
                snprintf(m->labels[count],sizeof(m->labels[count]),"%s: %.470s",field_labels[field],selected_label(m));
                m->controls[count]=button(m,i+1,m->labels[count],row,true,m->choice_count!=0);
            }
            ++count; ++row;
        }
    } else if (id==QA_UI_LIBRARY_CHOICES) {
        title=field_labels[m->field]; if (!selection_choices(m,m->field,error)) return false;
        size_t pages=(m->choice_count+6)/7; if (!pages) pages=1; if (m->page>=pages) m->page=pages-1;
        for (size_t i=m->page*7;i<m->choice_count && i<(m->page+1)*7;++i) {
            snprintf(m->labels[count],sizeof(m->labels[count]),"%s%.455s%s",!strcmp(m->selected,m->choices[i].id)?"> ":"",m->choices[i].label,m->choices[i].unavailable?" (unavailable)":"");
            m->controls[count]=button(m,count+1,m->labels[count],(unsigned)count,true,m->choices[i].unavailable==NULL); ++count;
        }
        if (pages>1) { m->controls[count++]=button(m,90,"Previous",7,true,true); snprintf(m->labels[count],sizeof(m->labels[count]),"Next (%zu/%zu)",m->page+1,pages); m->controls[count]=button(m,91,m->labels[count],8,true,true); ++count; }
    } else if (id==QA_UI_LIBRARY_ROSTER) {
        title="Custom roster";
        const qa_ui_library_roster_row *rows=NULL; size_t rows_count=0; const char *source="";
        if (!m->services.roster || !m->services.roster(m->services.context,m,&rows,&rows_count,&source,error)) return false;
        snprintf(m->labels[count],sizeof(m->labels[count]),"Monster source: %.480s",source?source:"");
        m->controls[count]=button(m,20,m->labels[count],0,true,true); m->controls[count].rect=(qa_scene_rect_f){64,78,512,26}; ++count;
        size_t pages=(rows_count+6)/7; if (!pages) pages=1; if (m->roster_page>=pages) m->roster_page=pages-1;
        for (size_t i=m->roster_page*7;i<rows_count && i<(m->roster_page+1)*7;++i) {
            snprintf(m->labels[count],sizeof(m->labels[count]),"%.220s: %.280s",rows[i].label,rows[i].effective_label);
            m->controls[count]=button(m,i-m->roster_page*7+1,m->labels[count],(unsigned)(i-m->roster_page*7),true,true); ++count;
        }
        if (pages>1) { m->controls[count++]=button(m,90,"Previous",7,true,true); snprintf(m->labels[count],sizeof(m->labels[count]),"Next (%zu/%zu)",m->roster_page+1,pages); m->controls[count]=button(m,91,m->labels[count],8,true,true); ++count; }
        m->controls[count++]=text_control(m,181,"Map counts shown. * Custom override.",64,460,1.5f,512,false);
    } else if (id==QA_UI_LIBRARY_ARENAS) {
        title="Choose an arena"; if (!arena_menu(m,&count,error)) return false;
    }
    if (id!=QA_UI_LIBRARY_CUSTOM && id!=QA_UI_LIBRARY_ARENAS) m->controls[count++]=button(m,99,"Back",9,true,true);
    if (*m->status) { m->controls[count]=text_control(m,182,m->status,64,395,1.8f,512,true); m->controls[count++].value.text.overlay=true; }
    *out=(qa_ui_menu){.id=id,.title=title,.controls=m->controls,.count=count,.fullscreen=true,.source_title=true}; return true;
}
static bool register_pages(qa_ui_library *m,qa_error *error) {
    const qa_ui_id ids[]={m->menu,QA_UI_LIBRARY_CAMPAIGN,QA_UI_LIBRARY_DIFFICULTY,QA_UI_LIBRARY_CUSTOM,
        QA_UI_LIBRARY_CATEGORY,QA_UI_LIBRARY_CHOICES,QA_UI_LIBRARY_ROSTER,QA_UI_LIBRARY_ARENAS};
    for (size_t i=0;i<sizeof(ids)/sizeof(*ids);++i) {
        m->pages[i]=(library_page){m,ids[i]};
        if (!qa_ui_register(m->ui,&(qa_ui_menu_registration){.id=ids[i],.context=m->pages+i,.factory=factory},error)) {
            for (size_t j=0;j<i;++j) qa_ui_unregister(m->ui,ids[j],m->ui->time_ms,NULL);
            return false;
        }
    }
    return true;
}
static void release_profiles(qa_ui_library *m) {
    for (size_t i=0;i<m->local_player_count;++i) {
        free(m->local_players[i].name); free(m->local_players[i].team);
        free(m->local_players[i].character_model); free(m->local_players[i].character_skin);
        free(m->local_players[i].character_head_model); free(m->local_players[i].character_head_skin);
    }
    free(m->local_players);
}
void ui_library_clear(qa_ui_library *m) {
    qa_launch_draft_destroy(m->draft); qa_catalog_release(m->catalog); release_profiles(m); choices_clear(m);
    free(m->choices); free(m->monster_classname);
    for (size_t i=0;i<m->tier_count;++i) free((void *)m->tier_labels[i]);
    for (size_t i=0;i<m->arena_count;++i) free((void *)m->arena_rows[i].detail);
    free(m->arena_rows); free(m->arena_numbers); free(m->tier_labels); free(m->tiers);
}
bool qa_ui_library_refresh(qa_ui_library *m,qa_error *error) {
    if (!m || m->ui->drawing) return ui_fail(error,"invalid selection refresh");
    if (!qa_application_rediscover(m->application,true,error)) return false;
    qa_catalog *catalog=qa_application_catalog(m->application); qa_launch_draft *draft=NULL;
    if (m->draft && !qa_launch_draft_rebase(m->draft,catalog,&draft,error)) return false;
    qa_catalog_retain(catalog); qa_launch_draft_destroy(m->draft); qa_catalog_release(m->catalog);
    m->catalog=catalog; m->draft=draft; choices_clear(m); return true;
}
bool qa_ui_library_create_restored(qa_ui *ui,qa_application *application,qa_ui_id id,qa_ui_library **out,qa_error *error) {
    if (!ui || !application || !id || !out || *out || ui->handling || ui->drawing) return ui_fail(error,"selection restore requires an idle controller and empty output");
    qa_ui_library *m=calloc(1,sizeof(*m));
    if (!m) { qa_error_set(error,QA_ERROR_MEMORY,0,"allocating restored startup selection"); return false; }
    m->ui=ui; m->application=application; m->menu=id;
    if (!register_pages(m,error)) { free(m); return false; } *out=m; return true;
}
bool qa_ui_library_create(qa_ui *ui, qa_application *application, qa_ui_id id,
                          const qa_launch_seat *local_players, size_t local_player_count,
                          qa_ui_library **out, qa_error *error) {
    if (!ui || !application || !id || !out || !local_players || !local_player_count ||
        local_player_count > SIZE_MAX / sizeof(library_profile))
        return ui_fail(error, "game library requires an actual local roster");
    bool own_seat = false;
    for (size_t i = 0; i < local_player_count; ++i) {
        const qa_launch_seat *player = &local_players[i];
        if (!player->local || player->bot || player->actor.registry || !player->name)
            return ui_fail(error, "game library local profile is invalid");
        for (size_t j = 0; j < i; ++j)
            if (player->id == local_players[j].id)
                return ui_fail(error, "game library local roster repeats a seat");
        own_seat |= player->id == ui->options.seat;
    }
    if (!own_seat) return ui_fail(error, "game library roster does not include its menu seat");
    qa_ui_library *menu = calloc(1, sizeof(*menu));
    if (!menu) { qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating game library"); return false; }
    menu->ui = ui; menu->application = application; menu->menu = id;
    menu->local_players = calloc(local_player_count, sizeof(*menu->local_players));
    if (!menu->local_players) {
        free(menu); qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating local roster profiles"); return false;
    }
    menu->local_player_count = local_player_count;
    for (size_t i = 0; i < local_player_count; ++i) {
        const qa_launch_seat *player = &local_players[i];
        const char *team = player->team ? player->team : "";
        size_t name_length = strlen(player->name), team_length = strlen(team);
        library_profile *profile = &menu->local_players[i];
        profile->name = malloc(name_length + 1); profile->team = malloc(team_length + 1);
        if (!profile->name || !profile->team) {
            release_profiles(menu); free(menu);
            qa_error_set(error, QA_ERROR_MEMORY, 0, "retaining local roster profiles"); return false;
        }
        memcpy(profile->name, player->name, name_length + 1);
        memcpy(profile->team, team, team_length + 1);
        profile->seat = *player;
        profile->seat.name = profile->name; profile->seat.team = profile->team;
        const char *source[] = {player->character_model, player->character_skin,
            player->character_head_model, player->character_head_skin};
        char **owned[] = {&profile->character_model, &profile->character_skin,
            &profile->character_head_model, &profile->character_head_skin};
        for (size_t j = 0; j < 4; ++j) {
            if (!source[j]) continue;
            size_t length = strlen(source[j]);
            *owned[j] = malloc(length + 1);
            if (!*owned[j]) {
                release_profiles(menu); free(menu);
                qa_error_set(error, QA_ERROR_MEMORY, 0, "retaining menu CHARACTER choices"); return false;
            }
            memcpy(*owned[j], source[j], length + 1);
        }
        profile->seat.character_model = profile->character_model;
        profile->seat.character_skin = profile->character_skin;
        profile->seat.character_head_model = profile->character_head_model;
        profile->seat.character_head_skin = profile->character_head_skin;
    }
    menu->catalog=qa_application_catalog(application); qa_catalog_retain(menu->catalog);
    menu->arena_number=-1; menu->arena_tier=INT32_MIN; menu->native_skill=1;
    const qa_launch_snapshot *active=qa_application_launch(application);
    if (active) {
        if (!qa_launch_snapshot_draft_copy(active,&menu->draft,error)) { ui_library_clear(menu); free(menu); return false; }
        const qa_launch_choices *current=qa_launch_draft_choices(menu->draft);
        menu->mode_preference=current->mode_count?current->modes[0].rules.kind:QA_MODE_SINGLE_PLAYER;
        if (menu->mode_preference!=QA_MODE_COOPERATIVE && menu->mode_preference!=QA_MODE_FFA) menu->mode_preference=QA_MODE_SINGLE_PLAYER;
    } else for (size_t i=0;i<qa_catalog_count(menu->catalog);++i) {
        const qa_product *p=qa_catalog_at(menu->catalog,i);
        if (p->availability==QA_CONTENT_INSTALLED) {
            if (!select_preset(menu,p->id,false,error)) { ui_library_clear(menu); free(menu); return false; }
            break;
        }
    }
    if (!register_pages(menu,error)) { ui_library_clear(menu); free(menu); return false; }
    *out=menu; return true;
}
bool qa_ui_library_destroy(qa_ui_library *m,double time,qa_error *error) {
    if (!m) return true;
    if (m->ui->handling) return ui_fail(error,"startup selection callback is active");
    for (size_t i=0;i<8;++i) if (!qa_ui_unregister(m->ui,m->pages[i].id,time,error)) return false;
    ui_library_clear(m); free(m); return true;
}

bool qa_ui_library_launch_failed(qa_ui_library *m,const char *message,qa_error *error) {
    if (!m || m->ui->handling || m->ui->drawing) return ui_fail(error,"launch failure requires the returned UI callback boundary");
    snprintf(m->status,sizeof(m->status),"%s",message?message:"Launch rejected");
    return open(m,m->last_authored?QA_UI_LIBRARY_DIFFICULTY:QA_UI_LIBRARY_CUSTOM,error);
}

bool qa_ui_library_open_arenas(qa_ui_library *m,qa_error *error) {
    const qa_product *p=m?qa_catalog_find(m->catalog,"q3-baseq3"):NULL;
    if (!p || !m->services.prepare_arenas || !m->services.arenas) return ui_fail(error,"arena page requires the installed authored campaign service");
    if (!select_preset(m,p->id,true,error) || !m->services.prepare_arenas(m->services.context,m,error)) return false;
    m->arena_tier=INT32_MIN;
    return open(m,QA_UI_LIBRARY_ARENAS,error);
}
bool qa_ui_library_input(qa_ui_library *m,const qa_input_event *event,bool *handled,qa_error *error) {
    if (!m || !event || !handled) return ui_fail(error,"selection input requires its actual owner and event");
    *handled=false;
    if (event->kind!=QA_INPUT_EVENT_WHEEL || event->delta.y==0) return true;
    qa_ui_id active=current_menu(m); size_t *page=NULL,count=0;
    if (active==QA_UI_LIBRARY_CHOICES) {
        if (!selection_choices(m,m->field,error)) return false;
        page=&m->page; count=m->choice_count;
    } else if (active==QA_UI_LIBRARY_ROSTER && m->services.roster) {
        const qa_ui_library_roster_row *rows=NULL; const char *source=NULL;
        if (!m->services.roster(m->services.context,m,&rows,&count,&source,error)) return false;
        page=&m->roster_page;
    } else return true;
    size_t pages=(count+6)/7; if (!pages) pages=1;
    if (event->delta.y<0 && *page<pages-1) ++*page;
    else if (event->delta.y>0 && *page) --*page;
    *handled=true; return true;
}

bool qa_ui_library_selection_choices(qa_ui_library *m,qa_ui_library_field field,const qa_ui_library_choice **choices,size_t *count,const char **selected,qa_error *error) {
    if (!m || !choices || !count || !selected || (unsigned)field>QA_UI_LIBRARY_DOPPLER) return ui_fail(error,"selection rows require their actual field and outputs");
    if (!selection_choices(m,field,error)) return false;
    *choices=m->choices; *count=m->choice_count; *selected=m->selected; return true;
}
bool qa_ui_library_select(qa_ui_library *m,qa_ui_library_field field,const char *choice,qa_error *error) {
    if (!m || !choice || (unsigned)field>QA_UI_LIBRARY_DOPPLER) return ui_fail(error,"selection requires its authored field");
    char *id=copy_text(choice,error); if (!id) return false;
    if (!selection_choices(m,field,error)) { free(id); return false; }
    bool admitted=false;
    for (size_t i=0;i<m->choice_count;++i) if (!strcmp(id,m->choices[i].id) && !m->choices[i].unavailable) admitted=true;
    if (!admitted) { free(id); return ui_fail(error,"startup choice is unavailable for the selected field"); }
    qa_ui_library_field previous=m->field; m->field=field;
    bool ok=select_choice(m,id,error); m->field=previous; free(id); return ok;
}

qa_mode_kind qa_ui_library_mode_preference(const qa_ui_library *m) { return m?m->mode_preference:QA_MODE_SINGLE_PLAYER; }
bool qa_ui_library_mode_preference_set(qa_ui_library *m,qa_mode_kind kind,qa_error *error) {
    if (!m || (kind!=QA_MODE_SINGLE_PLAYER && kind!=QA_MODE_COOPERATIVE && kind!=QA_MODE_FFA)) return ui_fail(error,"startup mode preference is outside its authored choices");
    m->mode_preference=kind; return true;
}
bool qa_ui_library_select_preset(qa_ui_library *m,const char *key,const char *start,qa_error *error) {
    const qa_product *p=m && key?qa_catalog_find(m->catalog,key):NULL;
    if (!p || p->availability!=QA_CONTENT_INSTALLED) return ui_fail(error,"selected preset is not installed");
    if (!select_preset(m,p->id,false,error)) return false;
    return !start || !*start || qa_ui_library_select(m,QA_UI_LIBRARY_MAP,start,error);
}
bool qa_ui_library_apply(qa_ui_library *m,qa_error *error) {
    if (!m || !m->draft) return ui_fail(error,"Play requires the selected launch draft");
    return launch(m,false,error);
}

bool qa_ui_library_weapon_bindings(qa_ui_library *m,const qa_input_weapon_binding **bindings,size_t *count,qa_error *error) {
    if (!m || !bindings || !count) return ui_fail(error,"bindable items require their actual selection owner");
    *bindings=NULL; *count=0;
    return !m->services.weapon_bindings || m->services.weapon_bindings(m->services.context,m,bindings,count,error);
}
