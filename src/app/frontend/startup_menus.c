#include "startup_menus.h"
#include "startup_selection.h"
#include "startup_arena.h"
#include "host_menu.h"
#include "content_library_services.h"
#include "settings_menu.h"
#include "demo_dispatch.h"
#include "startup_server_profile.h"
#include "global_settings_storage.h"
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
    (void)library;
    if (f->startup_launch || qa_application_startup_pending(f->application))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "A game is already loading");
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
frontend_seats_resize_state *frontend_startup_launch_resize_state(qa_frontend *f, unsigned next)
{
    struct frontend_startup_launch *request = f ? f->startup_launch : NULL;
    if (!request || request->begun || request->end_game) return NULL;
    const qa_launch_choices *selected = qa_launch_draft_choices(request->draft);
    unsigned local = 0;
    for (size_t i = 0; i < selected->seat_count; ++i) if (selected->seats[i].local) ++local;
    return local == next ? &request->resizing : NULL;
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
        if (!qa_application_startup_pending(f->application)) frontend_startup_launch_discard(f);
        return true;
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
    unsigned local = 0;
    for (size_t i = 0; i < selected->seat_count; ++i) if (selected->seats[i].local) ++local;
    qa_error failure = {0};
    bool complete = false;
    bool ok = local && local <= QA_INPUT_LOCAL_SEATS &&
        frontend_seats_resize(f, local, &complete, &failure);
    if (ok && !complete) return true;
    if (!ok && f->input_settings) { if (error) *error = failure; return false; }
    if (ok) ok = frontend_demo_dispatch_stop(f->demos, &failure) && frontend_network_destroy(f, &failure);
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
