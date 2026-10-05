#include "startup_arena.h"
#include "config_store.h"
#include "../application/q3_product.h"
#include "qa/team_arena_campaign_progress.h"
#include "qa/text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct frontend_startup_arena {
    qa_frontend *frontend;
    qa_catalog *catalog;
    qa_product_id product;
    qa_base_arena_catalog *base;
    qa_cvars *preview;
    qa_arena_progress progress;
    qa_team_arena_campaign *team;
    qa_team_arena_skirmish *plan;
    qa_ui_library_choice *teams;
    size_t team_count, player_team, opponent_team;
    qa_application_q3_setting source_settings[20], client_settings[3];
    size_t source_count, client_count;
    char skill[12];
};

static bool fail(qa_error *error,qa_status code,const char *text)
{ qa_error_set(error,code,0,"%s",text); return false; }

static const qa_launch_provider *source(const qa_launch_draft *draft)
{
    const qa_launch_choices *choices=qa_launch_draft_choices(draft);
    const qa_launch_binding *binding=choices?qa_launch_binding_for(choices,
        (qa_launch_scope){.kind=QA_SCOPE_WORLD},QA_ROLE_ENTITIES,""):NULL;
    for (size_t i=0;binding && i<choices->provider_count;++i)
        if (!strcmp(choices->providers[i].instance,binding->instance)) return choices->providers+i;
    return NULL;
}

static void clear(frontend_startup_arena *owner)
{
    qa_team_arena_skirmish_destroy(owner->plan); owner->plan=NULL;
    free(owner->teams); owner->teams=NULL; owner->team_count=0;
    qa_team_arena_campaign_destroy(owner->team); owner->team=NULL;
    qa_base_arena_catalog_destroy(owner->base); owner->base=NULL;
    qa_cvars_destroy(owner->preview); owner->preview=NULL;
    owner->progress=(qa_arena_progress){0};
    qa_catalog_release(owner->catalog); owner->catalog=NULL; owner->product=QA_PRODUCT_NONE;
    owner->source_count=owner->client_count=0;
}

bool frontend_startup_arena_create(qa_frontend *frontend,frontend_startup_arena **out,qa_error *error)
{
    if (!frontend || !out || *out) return fail(error,QA_ERROR_ARGUMENT,"Startup arenas require their frontend and empty owner");
    frontend_startup_arena *owner=calloc(1,sizeof(*owner));
    if (!owner) return fail(error,QA_ERROR_MEMORY,"Retaining startup arena selection");
    owner->frontend=frontend; *out=owner; return true;
}

void frontend_startup_arena_destroy(frontend_startup_arena *owner)
{ if (owner) { clear(owner); free(owner); } }

static bool load(frontend_startup_arena *owner,const qa_launch_draft *draft,qa_error *error)
{
    const qa_launch_provider *selected=source(draft);
    qa_catalog *catalog=qa_launch_draft_catalog(draft);
    const qa_product *product=selected?qa_catalog_product(catalog,selected->product):NULL;
    if (!owner || !product || product->family!=QA_GAME_Q3 || selected->clock.kind!=QA_CLOCK_Q3 ||
        product->availability!=QA_CONTENT_INSTALLED ||
        (strcmp(product->campaign,"baseq3") && strcmp(product->campaign,"missionpack")))
        return fail(error,QA_ERROR_ARGUMENT,"Startup arenas require an installed official Q3 Source selection");
    if (owner->catalog==catalog && owner->product==product->id) return true;
    clear(owner);
    application_q3_product_preparation prepared={0};
    if (!application_q3_product_prepare_draft(owner->frontend->application,draft,&prepared,error)) return false;
    qa_catalog *admitted=qa_launch_draft_catalog(prepared.draft);
    const qa_launch_provider *admitted_source=source(prepared.draft);
    qa_vfs *content=NULL;
    if (!qa_catalog_open(admitted,admitted_source->product,&content,error)) {
        application_q3_product_finish(owner->frontend->application,&prepared,false); return false;
    }
    bool team=!strcmp(product->campaign,"missionpack"),ok;
    if (team) {
        ok=qa_team_arena_campaign_create(content,prepared.policy.prerelease_team_arena_demo?
            QA_TEAM_ARENA_CATALOG_DEMO:QA_TEAM_ARENA_CATALOG_RETAIL,&owner->team,error);
        if (ok) {
            owner->team_count=qa_team_arena_campaign_team_count(owner->team);
            if (owner->team_count>SIZE_MAX/sizeof(*owner->teams) ||
                !(owner->teams=calloc(owner->team_count?owner->team_count:1,sizeof(*owner->teams))))
                ok=fail(error,QA_ERROR_MEMORY,"Retaining authored Team Arena choices");
        }
        if (ok) {
            owner->player_team=owner->opponent_team=SIZE_MAX;
            for (size_t i=0;i<owner->team_count;++i) {
                const char *name=qa_team_arena_campaign_team_at(owner->team,i);
                owner->teams[i]=(qa_ui_library_choice){.id=name,.label=name};
                if (!strcmp(name,"pagans")) owner->player_team=i;
                if (!strcmp(name,"stroggs")) owner->opponent_team=i;
            }
            if (owner->player_team==SIZE_MAX || owner->opponent_team==SIZE_MAX)
                ok=fail(error,QA_ERROR_FORMAT,"Team Arena metadata lacks its authored Pagans/Stroggs defaults");
        }
    } else ok=qa_base_arena_catalog_create(content,&owner->base,error);
    qa_vfs_destroy(content);
    application_q3_product_finish(owner->frontend->application,&prepared,false);
    if (!ok) { clear(owner); return false; }
    owner->catalog=catalog; qa_catalog_retain(catalog); owner->product=product->id;
    return true;
}

static qa_cvars *live_registry(frontend_startup_arena *owner,const qa_launch_draft *draft)
{
    const qa_launch_provider *selected=source(draft);
    frontend_config_source *configured=selected?frontend_config_store_named_source(
        owner->frontend->config_store,selected->instance):NULL;
    qa_application_startup_source actual;
    if (!frontend_config_source_published(configured) || !frontend_config_source_primary(configured) ||
        !frontend_config_source_tuple(configured,&actual)) return NULL;
    const qa_product *product=qa_catalog_product(qa_launch_instance_catalog(actual.descriptor),
        actual.descriptor->selection.product);
    const qa_product *chosen=qa_catalog_product(qa_launch_draft_catalog(draft),selected->product);
    if (!product || !chosen || strcmp(product->identity,chosen->identity) ||
        strcmp(actual.descriptor->selection.implementation,selected->implementation) ||
        actual.descriptor->selection.runtime!=selected->runtime ||
        qa_cvars_dialect(actual.cvars)!=QA_CONSOLE_Q3) return NULL;
    return actual.cvars;
}

static bool progress(frontend_startup_arena *owner,const qa_launch_draft *draft,bool refresh,qa_error *error)
{
    qa_cvars *registry=live_registry(owner,draft);
    if (registry) {
        /* Existing physical Source owns these declarations and archive writes. */
        owner->progress=(qa_arena_progress){registry,qa_base_arena_catalog_levels(owner->base)};
        return true;
    }
    if (refresh || !owner->preview) {
        qa_cvars *preview=qa_cvars_create(&(qa_cvar_options){.dialect=QA_CONSOLE_Q3},error);
        qa_cvar_archive archive={0}; qa_arena_progress view={0};
        bool ok=preview && qa_arena_progress_init(&view,preview,
            qa_base_arena_catalog_levels(owner->base),0,error) &&
            qa_cvars_register(preview,"ui_spSelection","",QA_CVAR_ARCHIVE,0,NULL,error) &&
            frontend_config_store_draft_archive(owner->frontend->config_store,draft,&archive,error);
        for (size_t i=0;ok && i<archive.count;++i) {
            if (!qa_cvars_find(preview,archive.entries[i].name)) continue;
            qa_cvar_archive one={archive.entries+i,1};
            ok=qa_cvar_archive_apply(preview,&one,error);
        }
        qa_cvar_archive_free(&archive);
        if (!ok) { qa_cvars_destroy(preview); return false; }
        qa_cvars_destroy(owner->preview); owner->preview=preview;
    }
    owner->progress=(qa_arena_progress){owner->preview,qa_base_arena_catalog_levels(owner->base)};
    return true;
}

bool frontend_startup_arena_prepare(void *context,qa_ui_library *library,qa_error *error)
{
    frontend_startup_arena *owner=context;
    qa_launch_draft *draft=qa_ui_library_draft(library);
    if (!load(owner,draft,error)) return false;
    return owner->base?progress(owner,draft,true,error):true;
}

bool frontend_startup_arena_arenas(void *context,qa_ui_library *library,
    const qa_base_arena_catalog **catalog,const qa_arena_progress **out,qa_error *error)
{
    frontend_startup_arena *owner=context; qa_launch_draft *draft=qa_ui_library_draft(library);
    if (!catalog || !out)
        return fail(error,QA_ERROR_ARGUMENT,"Arena pages require their selected Base Arena catalog");
    if (!load(owner,draft,error)) return false;
    if (!owner->base) return fail(error,QA_ERROR_ARGUMENT,"Arena pages require their selected Base Arena catalog");
    if (!progress(owner,draft,false,error)) return false;
    *catalog=owner->base; *out=&owner->progress; return true;
}

bool frontend_startup_arena_choices(void *context,qa_ui_library *library,qa_ui_library_field field,
    const char *classname,const qa_ui_library_choice **out,size_t *count,const char **selected,qa_error *error)
{
    frontend_startup_arena *owner=context; (void)classname;
    if ((field!=QA_UI_LIBRARY_TEAM_PLAYER && field!=QA_UI_LIBRARY_TEAM_OPPONENT) || !out || !count || !selected)
        return fail(error,QA_ERROR_ARGUMENT,"Team choices require a genuine Team Arena side");
    if (!load(owner,qa_ui_library_draft(library),error)) return false;
    if (!owner->team) return fail(error,QA_ERROR_ARGUMENT,"Team choices require the selected Team Arena campaign");
    *out=owner->teams; *count=owner->team_count;
    *selected=owner->teams[field==QA_UI_LIBRARY_TEAM_PLAYER?owner->player_team:owner->opponent_team].id;
    return true;
}

static bool clear_bots(qa_launch_draft *draft,qa_error *error)
{
    for (size_t i=0;i<qa_launch_draft_choices(draft)->seat_count;) {
        const qa_launch_seat *seat=qa_launch_draft_choices(draft)->seats+i;
        if (seat->bot) { if (!qa_launch_remove_seat(draft,seat->id,error)) return false; }
        else ++i;
    }
    return true;
}

static bool mode_set(qa_launch_draft *draft,qa_mode_kind kind,bool team,const qa_base_arena *arena,qa_error *error)
{
    const qa_launch_choices *choices=qa_launch_draft_choices(draft);
    qa_launch_mode mode={0}; bool found=false;
    for (size_t i=0;i<choices->mode_count;++i) if (choices->modes[i].primary_score) {
        mode=choices->modes[i]; found=true; break;
    }
    if (!found) return fail(error,QA_ERROR_ARGUMENT,"Authored arena launch requires its selected scoring owner");
    mode.rules=qa_mode_defaults(team?QA_MODE_TEAM_ARENA:QA_MODE_Q3,kind);
    mode.rules.single_player_active=true;
    if (arena) {
        mode.rules.frag_limit=qa_source_float_to_i32((float)arena->frag_limit);
        mode.rules.time_limit_minutes=(float)arena->time_limit;
        if (!mode.rules.frag_limit && mode.rules.time_limit_minutes==0) mode.rules.frag_limit=10;
    }
    if (team) { mode.teams[0]="Red"; mode.teams[1]="Blue"; mode.teams[2]="free"; }
    return qa_launch_set_mode(draft,&mode,error);
}

static bool team_launch(frontend_startup_arena *owner,qa_launch_draft *draft,int32_t skill,qa_error *error)
{
    qa_team_arena_skirmish *plan=NULL;
    if (!qa_team_arena_campaign_plan(owner->team,NULL,(uint32_t)skill,
        owner->teams[owner->player_team].id,owner->teams[owner->opponent_team].id,&plan,error)) return false;
    const qa_team_arena_skirmish_view *view=qa_team_arena_skirmish_read(plan);
    qa_launch_world world=qa_launch_draft_choices(draft)->world;
    world.map=view->map; world.start_command=NULL; world.spawn_point=NULL; world.explicit_spawn_point=false; world.skill=1;
    qa_mode_kind kind=view->game_type==1?QA_MODE_DUEL:view->game_type==4?QA_MODE_CTF:
        view->game_type==5?QA_MODE_ONE_FLAG:view->game_type==6?QA_MODE_OVERLOAD:QA_MODE_HARVESTER;
    bool ok=clear_bots(draft,error) && qa_launch_set_world(draft,&world,error) && mode_set(draft,kind,true,NULL,error);
    const qa_launch_choices *choices=qa_launch_draft_choices(draft);
    size_t player=0; while (player<choices->seat_count && !choices->seats[player].local) ++player;
    if (ok && player==choices->seat_count) ok=fail(error,QA_ERROR_ARGUMENT,"Team Arena requires its existing local player seat");
    if (ok) {
        qa_launch_seat seat=choices->seats[player]; seat.team=view->player_team;
        seat.character_model=view->player_model; seat.character_skin="default";
        seat.character_head_model=view->player_head_model; seat.character_head_skin="default";
        ok=qa_launch_set_seat(draft,&seat,error);
    }
    for (size_t i=0;ok && i<view->bot_count;++i) {
        choices=qa_launch_draft_choices(draft); uint32_t id=0;
        for (size_t j=0;j<choices->seat_count;++j) {
            if (choices->seats[j].id==UINT32_MAX) { ok=fail(error,QA_ERROR_MEMORY,"Team Arena seat identities are exhausted"); break; }
            if (choices->seats[j].id>=id) id=choices->seats[j].id+1;
        }
        const qa_team_arena_campaign_bot *bot=view->bots+i;
        qa_launch_seat seat={.id=id,.name=bot->name,.team=bot->team,.bot=true,
            .bot_skill=(float)skill,.bot_definition=bot->ai,.bot_delay_ms=(int32_t)bot->delay_ms};
        if (ok) ok=qa_launch_set_seat(draft,&seat,error);
    }
    if (!ok) { qa_team_arena_skirmish_destroy(plan); return false; }
    qa_team_arena_skirmish_destroy(owner->plan); owner->plan=plan;
    owner->source_count=view->source_cvar_count; owner->client_count=view->client_cvar_count;
    for (size_t i=0;i<owner->source_count;++i)
        owner->source_settings[i]=(qa_application_q3_setting){view->source_cvars[i].name,view->source_cvars[i].value};
    for (size_t i=0;i<owner->client_count;++i)
        owner->client_settings[i]=(qa_application_q3_setting){view->client_cvars[i].name,view->client_cvars[i].value};
    return true;
}

bool frontend_startup_arena_select(void *context,qa_ui_library *library,qa_ui_library_field field,
    const char *classname,const char *choice,qa_error *error)
{
    frontend_startup_arena *owner=context; (void)classname;
    if ((field!=QA_UI_LIBRARY_TEAM_PLAYER && field!=QA_UI_LIBRARY_TEAM_OPPONENT) || !choice)
        return fail(error,QA_ERROR_ARGUMENT,"Team selection requires an authored team side and name");
    qa_launch_draft *draft=qa_ui_library_draft(library);
    if (!load(owner,draft,error)) return false;
    if (!owner->team) return fail(error,QA_ERROR_ARGUMENT,"Team selection requires its selected Team Arena campaign");
    size_t index=0; while (index<owner->team_count && strcmp(choice,owner->teams[index].id)) ++index;
    if (index==owner->team_count) return fail(error,QA_ERROR_ARGUMENT,"Unknown authored Team Arena team");
    size_t *side=field==QA_UI_LIBRARY_TEAM_PLAYER?&owner->player_team:&owner->opponent_team;
    size_t previous=*side; *side=index;
    if (team_launch(owner,draft,2,error)) return true;
    *side=previous; return false;
}

bool frontend_startup_arena_launch(frontend_startup_arena *owner,qa_launch_draft *draft,
    const char *map,int32_t skill,qa_error *error)
{
    if (skill<1 || skill>5) return fail(error,QA_ERROR_ARGUMENT,"Q3 campaign difficulty must be between one and five");
    if (!load(owner,draft,error)) return false;
    owner->source_count=owner->client_count=0;
    if (owner->team) {
        if (map) return fail(error,QA_ERROR_ARGUMENT,"Base Arena map selection cannot override Team Arena's authored skirmish");
        return team_launch(owner,draft,skill,error);
    }
    if (!progress(owner,draft,false,error)) return false;
    const qa_base_arena *arena=map?qa_base_arena_catalog_find(owner->base,map):NULL;
    if (!map) {
        int32_t level;
        if (!qa_arena_progress_current(&owner->progress,&level,error)) return false;
        for (size_t i=0;i<qa_base_arena_catalog_count(owner->base);++i)
            if (qa_base_arena_catalog_at(owner->base,i)->number==level) { arena=qa_base_arena_catalog_at(owner->base,i); break; }
    }
    bool available=false;
    if (!arena) return fail(error,QA_ERROR_NOT_FOUND,"Selected Base Arena map has no authored arena record");
    if (!qa_arena_progress_available(&owner->progress,arena->number,&available,error)) return false;
    if (!available) return fail(error,QA_ERROR_ARGUMENT,"That authored arena is not unlocked in the selected Source profile");
    qa_launch_world world=qa_launch_draft_choices(draft)->world;
    world.map=arena->map; world.start_command=NULL; world.spawn_point=NULL; world.explicit_spawn_point=false; world.skill=1;
    if (!clear_bots(draft,error) || !qa_launch_set_world(draft,&world,error) ||
        !mode_set(draft,QA_MODE_SINGLE_PLAYER,false,arena,error)) return false;
    (void)snprintf(owner->skill,sizeof(owner->skill),"%d",skill);
    const qa_application_q3_setting settings[]={ {"g_gametype","2"},{"sv_cheats","0"},
        {"g_doWarmup","0"},{"sv_maxclients","8"},{"g_spSkill",owner->skill} };
    memcpy(owner->source_settings,settings,sizeof(settings)); owner->source_count=5; return true;
}

bool frontend_startup_arena_settings(const frontend_startup_arena *owner,
    const qa_application_q3_setting **out,size_t *count,qa_error *error)
{
    if (!owner || !out || !count || !owner->source_count)
        return fail(error,QA_ERROR_ARGUMENT,"Campaign settings require their prepared startup arena launch");
    *out=owner->source_settings; *count=owner->source_count; return true;
}

bool frontend_startup_arena_client_settings(const frontend_startup_arena *owner,
    const qa_application_q3_setting **out,size_t *count,qa_error *error)
{
    if (!owner || !out || !count || !owner->source_count)
        return fail(error,QA_ERROR_ARGUMENT,"Client settings require their prepared startup arena launch");
    *out=owner->client_settings; *count=owner->client_count; return true;
}

static bool baseline_apply(qa_cvars *registry,const qa_team_arena_setting *rows,size_t count,qa_error *error)
{
    char **values=calloc(count,sizeof(*values));
    if (!values) return fail(error,QA_ERROR_MEMORY,"Retaining actual Team Arena baseline values");
    bool ok=true;
    for (size_t i=0;ok && i<count;++i) {
        values[i]=SDL_strdup(rows[i].value);
        if (!values[i]) ok=fail(error,QA_ERROR_MEMORY,"Retaining actual Team Arena baseline text");
    }
    for (size_t i=0;ok && i<count;++i)
        ok=qa_cvars_set(registry,rows[i].name,values[i],true,error);
    for (size_t i=0;i<count;++i) SDL_free(values[i]);
    free(values); return ok;
}

bool frontend_startup_arena_prepare_settings(qa_cvars *server,qa_cvars *client,
    bool first_source,const qa_application_q3_setting *client_rows,size_t client_count,qa_error *error)
{
    if (!server || qa_cvars_dialect(server)!=QA_CONSOLE_Q3 ||
        (client && (client==server || qa_cvars_dialect(client)!=QA_CONSOLE_Q3)) ||
        (client_count && !client_rows))
        return fail(error,QA_ERROR_ARGUMENT,"Team Arena preparation requires its actual separate GAME and client registries");
    if (first_source) {
        qa_team_arena_setting baseline[7];
        if (!qa_cvars_apply_latched(server,NULL,error) ||
            !qa_team_arena_source_baseline(server,baseline,error) ||
            !baseline_apply(server,baseline,7,error)) return false;
    }
    if (!client) return true;
    qa_team_arena_setting baseline;
    if (!qa_team_arena_client_baseline(client,&baseline,error) ||
        !baseline_apply(client,&baseline,1,error)) return false;
    for (size_t i=0;i<client_count;++i)
        if (!qa_cvars_set(client,client_rows[i].name,client_rows[i].value,true,error)) return false;
    return true;
}
