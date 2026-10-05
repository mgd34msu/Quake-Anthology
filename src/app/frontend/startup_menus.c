#include "startup_menus.h"
#include "startup_selection.h"
#include "startup_arena.h"
#include "host_menu.h"
#include "content_library_services.h"
#include "settings_menu.h"
#include "demo_dispatch.h"
#include "startup_server_profile.h"
#include "global_settings_storage.h"
#include "input_profile.h"
#include "config_store.h"
#include "qa/application_players.h"
#include "qa/application_character_selection.h"
#include "qa/application_q3_campaign.h"
#include <stdio.h>

struct frontend_startup_launch {
    qa_launch_draft *draft;
    frontend_host_settings hosting;
    qa_application_q3_setting *settings;
    size_t setting_count;
    size_t source_setting_count;
    uint32_t seat;
    bool arena, team, begun, end_game;
    bool local_players, drop, admitted;
    uint32_t logical;
    uint64_t generation;
    unsigned first;
    int old_slots[QA_INPUT_LOCAL_SEATS];
    frontend_seats_resize_state resizing;
    qa_server_profile *server_profile;
};
static char *retain_text(const char *text, qa_error *error)
{
    size_t size = strlen(text) + 1;
    char *copy = malloc(size);
    if (copy) memcpy(copy, text, size);
    else frontend_fail(error, QA_ERROR_MEMORY, "Retaining authored arena settings");
    return copy;
}
static void launch_free(struct frontend_startup_launch *request)
{
    if (!request) return;
    for (size_t i = 0; i < request->setting_count; ++i) {
        free((void *)request->settings[i].name);
        free((void *)request->settings[i].value);
    }
    free(request->settings);
    qa_server_profile_destroy(request->server_profile);
    qa_launch_draft_destroy(request->draft);
    free(request);
}
void frontend_startup_launch_discard(qa_frontend *f)
{
    launch_free(f->startup_launch);
    f->startup_launch = NULL;
}
static bool choices(void *context, qa_ui_library *library, qa_ui_library_field field,
    const char *classname, const qa_ui_library_choice **rows, size_t *count,
    const char **selected, qa_error *error)
{
    frontend_seat *seat = context;
    if (field == QA_UI_LIBRARY_TEAM_PLAYER || field == QA_UI_LIBRARY_TEAM_OPPONENT)
        return frontend_startup_arena_choices(seat->startup_arena, library, field,
            classname, rows, count, selected, error);
    return frontend_startup_selection_choices(seat->startup_selection, library, field,
        classname, rows, count, selected, error);
}
static bool select_choice(void *context, qa_ui_library *library, qa_ui_library_field field,
    const char *classname, const char *choice, qa_error *error)
{
    frontend_seat *seat = context;
    if (field == QA_UI_LIBRARY_TEAM_PLAYER || field == QA_UI_LIBRARY_TEAM_OPPONENT)
        return frontend_startup_arena_select(seat->startup_arena, library, field, classname, choice, error);
    return frontend_startup_selection_select(seat->startup_selection, library, field, classname, choice, error);
}
static bool roster(void *context, qa_ui_library *library,
    const qa_ui_library_roster_row **rows, size_t *count, const char **source, qa_error *error)
{
    return frontend_startup_selection_roster(((frontend_seat *)context)->startup_selection,
        library, rows, count, source, error);
}
static bool prepare_arenas(void *context, qa_ui_library *library, qa_error *error)
{ return frontend_startup_arena_prepare(((frontend_seat *)context)->startup_arena, library, error); }
static bool arenas(void *context, qa_ui_library *library,
    const qa_base_arena_catalog **catalog, const qa_arena_progress **progress, qa_error *error)
{ return frontend_startup_arena_arenas(((frontend_seat *)context)->startup_arena, library, catalog, progress, error); }
static const char *hosting_label(void *context)
{ return frontend_host_menu_label(((frontend_seat *)context)->host_menu); }
static bool stage(void *context, qa_ui_library *library, qa_launch_draft *draft, bool authored,
    const char *arena_map, int32_t bot_skill, qa_error *error)
{
    frontend_seat *seat = context;
    qa_frontend *f = seat->frontend;
    if (f->startup_launch || qa_application_startup_pending(f->application))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "A game is already loading");
    if (!frontend_startup_selection_complete(library, error)) return false;
    struct frontend_startup_launch *request = calloc(1, sizeof(*request));
    if (!request) return frontend_fail(error, QA_ERROR_MEMORY, "Preparing game selection");
    request->seat = seat->id;
    bool ok = qa_launch_draft_copy(draft, &request->draft, error) &&
        frontend_host_menu_read(seat->host_menu, draft, &request->hosting, error);
    const qa_launch_choices *selected = qa_launch_draft_choices(draft);
    const qa_product *product = qa_catalog_product(qa_launch_draft_catalog(draft), selected->world.preset);
    request->arena = authored && product && product->family == QA_GAME_Q3;
    if (ok && request->arena) {
        const qa_application_q3_setting *settings = NULL, *client_settings = NULL;
        size_t count = 0, client_count = 0;
        ok = frontend_startup_arena_launch(seat->startup_arena, request->draft, arena_map, bot_skill, error) &&
            frontend_startup_arena_settings(seat->startup_arena, &settings, &count, error) &&
            frontend_startup_arena_client_settings(seat->startup_arena, &client_settings, &client_count, error);
        request->source_setting_count = count;
        request->team = !strcmp(product->campaign, "missionpack");
        if (ok && count + client_count) {
            request->settings = calloc(count + client_count, sizeof(*request->settings));
            if (!request->settings) ok = frontend_fail(error, QA_ERROR_MEMORY, "Preparing authored arena settings");
        }
        for (size_t i = 0; ok && i < count + client_count; ++i) {
            const qa_application_q3_setting *row = i < count ? settings + i : client_settings + i - count;
            request->setting_count = i + 1;
            request->settings[i].name = retain_text(row->name, error);
            request->settings[i].value = retain_text(row->value, error);
            ok = request->settings[i].name && request->settings[i].value;
        }
    }
    if (ok) ok = frontend_startup_server_profile_capture(
        frontend_global_settings_storage_user_store(f->global_settings_storage),seat->server_profile_path,
        request->draft,&request->server_profile,error);
    if (!ok) { launch_free(request); return false; }
    f->startup_launch = request;
    return true;
}
static bool addon(void *context, const char *product, const char *map, qa_error *error)
{
    frontend_seat *seat = context;
    return qa_ui_library_select_preset(seat->library, product, map, error) &&
        qa_ui_library_apply(seat->library, error);
}
static bool select_profile(void *context, const char *relative, qa_error *error)
{ return frontend_startup_server_profile_select(relative,&((frontend_seat *)context)->server_profile_path,error); }
bool frontend_startup_menus_create(frontend_seat *seat, qa_error *error)
{
    frontend_library_service services[FRONTEND_LIBRARY_COUNT] = {0};
    if (!frontend_startup_selection_create(&seat->startup_selection, error) ||
        !frontend_startup_arena_create(seat->frontend, &seat->startup_arena, error) ||
        !frontend_host_menu_create(seat, seat->library, 260, &seat->host_menu, error) ||
        !frontend_content_library_services_create(seat, &seat->library_services, error) ||
        !frontend_content_library_services_bind_selection(seat->library_services,
            &(frontend_content_library_selection){.context = seat, .play_addon = addon,
                .select_server_profile = select_profile}, error) ||
        !frontend_content_library_services_read(seat->library_services, services, error) ||
        (seat->frontend->demos && !frontend_content_library_services_bind_demo(seat->library_services,
            frontend_demo_dispatch_service(seat->frontend->demos,seat->id),error))) return false;
    if (seat->id && seat->frontend->seats[0].library_services) {
        frontend_library_service shared[FRONTEND_LIBRARY_COUNT] = {0};
        if (!frontend_content_library_services_read(seat->frontend->seats[0].library_services,shared,error)) return false;
        services[FRONTEND_LIBRARY_ADDONS] = shared[FRONTEND_LIBRARY_ADDONS];
    }
    if (!frontend_content_library_menu_create(seat,services,&seat->content_library,error)) return false;
    return qa_ui_library_bind_services(seat->library, &(qa_ui_library_services){
        .context = seat, .hosting_menu = 260, .browser_menu = 200, .mods_menu = FRONTEND_MODS,
        .hosting_label = hosting_label, .play = stage, .choices = choices, .select = select_choice,
        .roster = roster, .prepare_arenas = prepare_arenas, .arenas = arenas,
        .weapon_bindings = frontend_startup_selection_weapon_bindings}, error);
}
bool frontend_startup_menus_pump(qa_frontend *f, qa_error *error)
{
    for (unsigned i = 0; i < f->options.seats; ++i)
        if (f->seats[i].library_services &&
            !frontend_content_library_services_pump(f->seats[i].library_services, error)) return false;
    return true;
}
bool frontend_startup_menus_bind(qa_frontend *f, qa_error *error)
{
    if (!f->demos && !frontend_demo_dispatch_create(f,&f->demos,error)) return false;
    if (!frontend_demo_dispatch_register(f->demos,qa_application_console(f->application),
        0,QA_FRONTEND_COMMAND_OWNER,error)) return false;
    for (unsigned i = 0; !f->options.dedicated && i < f->options.seats; ++i)
        if (!frontend_content_library_services_bind_demo(f->seats[i].library_services,
            frontend_demo_dispatch_service(f->demos,i),error)) return false;
    return true;
}
bool frontend_startup_end_stage(frontend_seat *seat, qa_error *error)
{
    qa_frontend *f = seat->frontend;
    if (f->startup_launch || qa_application_startup_pending(f->application))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "A game is already changing");
    struct frontend_startup_launch *request = calloc(1, sizeof(*request));
    if (!request) return frontend_fail(error, QA_ERROR_MEMORY, "Returning to the startup menu");
    request->seat = seat->id;
    request->end_game = true;
    f->startup_launch = request;
    return qa_ui_close_all(seat->ui, (double)f->time_ns / 1000000.0, error);
}
static bool local_id(qa_frontend *f,const qa_launch_choices *choices,uint32_t *out,qa_error *error)
{
    for (uint32_t id=0;;++id) {
        bool used=false; qa_actor_id actor;
        for (size_t i=0;i<choices->seat_count;++i) used|=choices->seats[i].id==id;
        if (!used && !qa_application_player_actor(f->application,id,&actor)) { *out=id; return true; }
        if (id==UINT32_MAX) return frontend_fail(error,QA_ERROR_MEMORY,"Local player identities are exhausted");
    }
}
bool frontend_startup_local_players_read(frontend_seat *seat,bool *join,bool *drop,qa_error *error)
{
    if (!seat || !seat->frontend || !join || !drop)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Local player actions require their actual menu seat");
    *join=*drop=false;
    qa_frontend *f=seat->frontend;
    const qa_launch_choices *choices=qa_launch_snapshot_choices(qa_application_launch(f->application));
    if (!choices || f->options.dedicated || frontend_network_remote(f) || f->startup_launch ||
        qa_application_startup_pending(f->application) || seat->id>=f->options.seats ||
        seat!=f->seats+seat->id) return true;
    unsigned local=0; bool current=false; uint32_t logical;
    if (!frontend_seat_launch_id_read(f,seat->id,&logical)) return true;
    for (size_t i=0;i<choices->seat_count;++i) {
        const qa_launch_seat *row=choices->seats+i; qa_actor_id actor;
        if (!row->local || row->bot || !qa_application_player_actor(f->application,row->id,&actor)) continue;
        ++local; current|=row->id==logical;
    }
    if (!current || local!=f->options.seats) return true;
    *drop=local>1;
    if (local<QA_INPUT_LOCAL_SEATS) {
        uint32_t id;
        if (!local_id(f,choices,&id,error) ||
            !qa_application_local_player_available(f->application,id,join,error)) return false;
    }
    return true;
}
bool frontend_local_seat_read(const qa_launch_choices *choices,unsigned physical,uint32_t *logical)
{
    if (!choices || !logical) return false;
    unsigned ordinal=0;
    for (size_t i=0;i<choices->seat_count;++i) {
        const qa_launch_seat *row=choices->seats+i;
        if (row->local && !row->bot && ordinal++==physical) { *logical=row->id; return true; }
    }
    return false;
}
unsigned frontend_local_seat_count(const qa_launch_choices *choices)
{
    unsigned count=0; uint32_t logical;
    while (frontend_local_seat_read(choices,count,&logical)) ++count;
    return count;
}
bool frontend_local_seat_ordinal_read(const qa_launch_choices *choices,uint32_t logical,unsigned *physical)
{
    uint32_t actual;
    for (unsigned slot=0;frontend_local_seat_read(choices,slot,&actual);++slot)
        if (actual==logical) { *physical=slot; return true; }
    return false;
}
bool frontend_startup_launch_seat_read(const qa_frontend *f,unsigned physical,uint32_t *logical)
{
    const struct frontend_startup_launch *request=f?f->startup_launch:NULL;
    if (!request || !request->local_players || request->begun || !request->draft || !logical ||
        request->generation!=qa_application_configuration_generation(f->application)) return false;
    return frontend_local_seat_read(qa_launch_draft_choices(request->draft),physical,logical);
}
static bool local_stage(frontend_seat *seat,bool drop,qa_error *error)
{
    qa_frontend *f=seat->frontend; bool can_join,can_drop;
    if (!frontend_startup_local_players_read(seat,&can_join,&can_drop,error)) return false;
    if (!(drop?can_drop:can_join))
        return frontend_fail(error,QA_ERROR_ARGUMENT,drop?"This local player cannot leave":"The game has no available local player slot");
    const qa_launch_snapshot *published=qa_application_launch(f->application);
    const qa_launch_choices *old=qa_launch_snapshot_choices(published);
    struct frontend_startup_launch *request=calloc(1,sizeof(*request));
    if (!request) return frontend_fail(error,QA_ERROR_MEMORY,"Preparing actual local player change");
    request->seat=seat->id; request->local_players=true; request->drop=drop;
    request->generation=qa_application_configuration_generation(f->application);
    bool ok=qa_launch_snapshot_draft_copy(published,&request->draft,error);
    if (ok && drop) {
        ok=frontend_seat_launch_id_read(f,seat->id,&request->logical);
        for (;;) {
            const qa_launch_choices *choices=qa_launch_draft_choices(request->draft);
            const qa_launch_binding *binding=NULL;
            for (size_t i=0;i<choices->binding_count;++i)
                if (choices->bindings[i].scope.kind==QA_SCOPE_SEAT &&
                    choices->bindings[i].scope.seat==request->logical) { binding=choices->bindings+i; break; }
            if (!ok || !binding) break;
            ok=qa_launch_unbind(request->draft,binding->scope,binding->role,binding->selector,error);
        }
        if (ok) ok=qa_launch_remove_seat(request->draft,request->logical,error);
    } else if (ok) {
        ok=local_id(f,old,&request->logical,error);
        const qa_launch_binding *binding=qa_launch_binding_for(old,
            (qa_launch_scope){.kind=QA_SCOPE_SEAT,.seat=request->logical},QA_ROLE_CHARACTER,"");
        const qa_launch_instance *selected=binding?qa_launch_snapshot_find(published,binding->instance):NULL;
        const qa_product *product=selected?qa_catalog_product(qa_launch_snapshot_catalog(published),selected->selection.product):NULL;
        qa_native_q3_character_declaration declaration;
        if (ok && !product) ok=frontend_fail(error,QA_ERROR_ARGUMENT,"Joining player has no selected character product");
        if (ok) ok=qa_native_q3_character_default_declaration(product->family,&declaration,error);
        char name[32]; snprintf(name,sizeof(name),"Player %u",f->options.seats+1);
        if (ok) ok=qa_launch_set_seat(request->draft,&(qa_launch_seat){.id=request->logical,.name=name,
            .local=true,.input_device=f->options.seats,.character_model=declaration.model,
            .character_skin=declaration.skin,.character_head_model=declaration.head_model,
            .character_head_skin=declaration.head_skin},error);
    }
    unsigned next=0; request->first=f->options.seats;
    for (size_t i=0;ok && i<qa_launch_draft_choices(request->draft)->seat_count;++i) {
        qa_launch_seat row=qa_launch_draft_choices(request->draft)->seats[i];
        if (!row.local || row.bot) continue;
        unsigned previous=0; int origin=-1;
        for (size_t j=0;j<old->seat_count;++j) {
            const qa_launch_seat *prior=old->seats+j;
            if (!prior->local || prior->bot) continue;
            if (prior->id==row.id) origin=(int)previous;
            ++previous;
        }
        request->old_slots[next]=origin;
        if (origin!=(int)next && next<request->first) request->first=next;
        row.input_device=next++;
        ok=qa_launch_set_seat(request->draft,&row,error);
    }
    if (next<request->first) request->first=next;
    if (ok) ok=qa_launch_validate(request->draft,error);
    if (ok) ok=qa_ui_close_all(seat->ui,(double)f->time_ns/1000000.0,error);
    if (!ok) { launch_free(request); return false; }
    f->startup_launch=request; return true;
}
bool frontend_startup_local_join_stage(frontend_seat *seat,qa_error *error)
{ return local_stage(seat,false,error); }
bool frontend_startup_local_drop_stage(frontend_seat *seat,qa_error *error)
{ return local_stage(seat,true,error); }
frontend_seats_resize_state *frontend_startup_launch_resize_state(qa_frontend *f, unsigned next)
{
    struct frontend_startup_launch *request = f ? f->startup_launch : NULL;
    if (!request || request->begun || request->end_game) return NULL;
    const qa_launch_choices *selected = qa_launch_draft_choices(request->draft);
    unsigned local=frontend_local_seat_count(selected);
    return local == next ? &request->resizing : NULL;
}
bool frontend_startup_launch_complete(qa_frontend *f,qa_error *error)
{
    struct frontend_startup_launch *request=f?f->startup_launch:NULL;
    if (!request || !request->begun || qa_application_startup_pending(f->application)) return true;
    if (request->local_players) {
        if (!frontend_config_store_local_seats_route(f->config_store,error) ||
            !qa_application_local_player_clients_retire(f->application,error) ||
            !frontend_config_store_local_seats_retire(f->config_store,error)) return false;
        if (!request->drop && !request->admitted) {
            qa_application_startup_source source; bool present=false;
            if (!frontend_config_store_primary_server_read(f->config_store,&source,&present,error)) return false;
            frontend_config_source *profile=present?
                frontend_config_store_named_source(f->config_store,source.descriptor->selection.instance):NULL;
            qa_cvars *cvars=frontend_config_source_seat_cvars(profile,request->logical);
            if (!cvars) return frontend_fail(error,QA_ERROR_ARGUMENT,"Joining local player lost its actual published profile");
            qa_buffer info={0}; qa_actor_id actor;
            bool ok=qa_cvars_info(cvars,QA_CVAR_USERINFO,0,&info,error) &&
                qa_application_local_player_attach(f->application,request->logical,(const char *)info.data,
                    NULL,&actor,error);
            qa_buffer_free(&info);
            if (!ok) return false;
            request->admitted=true;
        }
        if (!frontend_startup_menus_bind(f,error)) return false;
    }
    frontend_startup_launch_discard(f); return true;
}
static bool desired_setting(void *context, const char *name, const char **out, qa_error *error)
{
    const qa_cvar_view *row = qa_cvars_find(context,name);
    if (!row) return frontend_fail(error,QA_ERROR_ARGUMENT,"Server profile setting is unavailable in this game");
    *out = row->latched_value ? row->latched_value : row->value;
    return true;
}
static bool initial_setting(void *context, const char *name, const char *value, qa_error *error)
{ return qa_cvars_set(context,name,value,true,error); }
bool frontend_startup_launch_settings(qa_frontend *f, const qa_launch_snapshot *candidate,
    const qa_application_startup_source *source, qa_cvars *client, bool first_source, qa_error *error)
{
    struct frontend_startup_launch *request = f->startup_launch;
    if (!request || (!request->team && !request->server_profile)) return true;
    if (!request->begun || qa_application_startup_candidate(f->application) != candidate)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Authored Team settings lost their loading game");
    const qa_launch_binding *binding = qa_launch_binding_for(qa_launch_snapshot_choices(candidate),
        (qa_launch_scope){.kind = QA_SCOPE_WORLD}, QA_ROLE_ENTITIES, "");
    if (!binding || !source->descriptor || strcmp(binding->instance, source->descriptor->selection.instance))
        return true;
    if (request->team && !frontend_startup_arena_prepare_settings(source->cvars, client, first_source,
        request->settings + request->source_setting_count,
        request->setting_count - request->source_setting_count, error)) return false;
    return !first_source || frontend_startup_server_profile_apply(request->server_profile,candidate,source,
        &(qa_server_profile_owner){.context = source->cvars,.read_desired = desired_setting,
            .write_initial = initial_setting},error);
}
bool frontend_startup_menus_destroy(frontend_seat *seat, qa_error *error)
{
    if (!frontend_content_library_menu_destroy(&seat->content_library, error) ||
        !frontend_content_library_services_destroy(&seat->library_services, error) ||
        !frontend_host_menu_destroy(&seat->host_menu, error)) return false;
    frontend_startup_arena_destroy(seat->startup_arena); seat->startup_arena = NULL;
    frontend_startup_selection_destroy(seat->startup_selection); seat->startup_selection = NULL;
    free(seat->server_profile_path); seat->server_profile_path = NULL;
    return true;
}
bool frontend_startup_launch_drain(qa_frontend *f, qa_error *error)
{
    struct frontend_startup_launch *request = f->startup_launch;
    if (!request) return true;
    if (request->begun) {
        return frontend_startup_launch_complete(f,error);
    }
    if (request->end_game) {
        if (!frontend_demo_dispatch_stop(f->demos,error) ||
            !frontend_network_destroy(f,error) ||
            !qa_application_end_game(f->application,error)) return false;
        f->options.game = NULL;
        f->options.network_connect = NULL;
        f->options.network_host = NULL;
        f->server_stop_owner = 0;
        f->server_stopped = false;
        request->begun = true;
        return qa_application_startup_bootstrap(f->application,error);
    }
    const qa_launch_choices *selected = qa_launch_draft_choices(request->draft);
    unsigned local=frontend_local_seat_count(selected);
    if (request->local_players) {
        if (request->generation!=qa_application_configuration_generation(f->application))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Local player request belongs to a replaced game");
        bool complete=false;
        if (!frontend_seats_recompose(f,local,request->first,request->old_slots,&complete,error)) return false;
        if (!complete) return true;
        if (request->drop) {
            if (!qa_application_local_player_detach(f->application,request->logical,error)) return false;
        } else {
            const qa_launch_seat *joining=NULL;
            for (size_t i=0;i<selected->seat_count;++i)
                if (selected->seats[i].id==request->logical) joining=selected->seats+i;
            if (!frontend_config_store_local_seat_prepare(f->config_store,joining,local-1,error)) return false;
        }
        request->begun=true;
        if (!qa_application_apply(f->application,request->draft,error)) return false;
        return frontend_startup_launch_complete(f,error);
    }
    qa_error failure = {0};
    bool complete = false;
    bool ok = local && local <= QA_INPUT_LOCAL_SEATS &&
        frontend_seats_resize(f, local, &complete, &failure);
    if (ok && !complete) return true;
    if (!ok && f->input_settings) { if (error) *error = failure; return false; }
    if (ok) frontend_demo_dispatch_manual_game(f->demos);
    if (ok) ok = frontend_demo_dispatch_stop(f->demos, &failure) && frontend_network_destroy(f, &failure);
    if (ok) ok = frontend_input_profile_bind_product(f,qa_launch_draft_catalog(request->draft),
        selected->world.preset,&failure);
    if (ok) {
        f->options.network_connect = NULL;
        f->options.network_host = request->hosting.kind == FRONTEND_HOST_OFFLINE ? NULL : "0.0.0.0";
        f->options.network_port = request->hosting.port;
        const qa_product *product = qa_catalog_product(qa_launch_draft_catalog(request->draft), selected->world.preset);
        f->options.network_protocol = request->hosting.kind == FRONTEND_HOST_UNIFIED ?
            (qa_net_protocol_id){.kind = QA_NET_UNIFIED_1} :
            product->family == QA_GAME_Q1 ? request->hosting.q1_protocol :
            product->family == QA_GAME_Q2 ? (qa_net_protocol_id){.kind =
                product->edition == QA_EDITION_RERELEASE ? QA_NET_Q2KEX_2023 : QA_NET_Q2_34} :
            (qa_net_protocol_id){.kind = QA_NET_Q3_68};
        request->begun = true;
        ok = request->arena ? qa_application_q3_campaign_start(f->application, request->draft,
            request->settings, request->source_setting_count, &failure) :
            qa_application_apply(f->application, request->draft, &failure);
    }
    uint32_t seat = request->seat < f->options.seats ? request->seat : 0;
    if (!ok) {
        frontend_startup_launch_discard(f);
        frontend_print(f, failure.message);
        return qa_ui_library_launch_failed(f->seats[seat].library,
            failure.code == QA_OK ? "Game selection has no local players" : failure.message, error);
    }
    return true;
}
