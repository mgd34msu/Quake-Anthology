#include "guest_q3_private.h"
#include "guest_projection_private.h"
#include "q3_world_restart.h"
#include "bots_private.h"
#include "guest_q3_restart.h"
#include "guest_q3_console.h"
#include "guest_q3_client_console.h"
#include "q3_campaign_launch.h"
#include "guest_q3_factory.h"
#include "guest_q3_weapons_services.h"
#include "guest_q3_combat.h"
#include "guest_q3_pickups.h"
#include "qa/game_q3_source_types.h"

struct application_q3_guest *q3g_engine(application_provider *provider)
{
    if (!provider) return NULL;
    return provider->kind == APPLICATION_PROVIDER_QVM ? provider->state.qvm.engine :
        provider->kind == APPLICATION_PROVIDER_NATIVE ? provider->state.native.engine : NULL;
}

bool q3g_call(q3g_role *role, int32_t command, const int32_t *arguments, size_t count,
               int32_t *result, qa_error *error)
{
    if (!role || !role->host || role->retired || role->engine->restore_pending ||
        (role->source_cleared && role->engine->initializing_role != role) ||
        (role->engine->round.phase != Q3G_ROUND_NONE && !role->engine->round.source_entry &&
            !(role->kind == QA_QVM_GAME && role->shutdown_entry && role->engine->calls && command == 6)) ||
        !result || count > 9 || (count && !arguments))
        return application_fail(error, QA_ERROR_ARGUMENT, "invalid Q3 guest entry");
    if (role->kind == QA_QVM_CGAME && role->abi == QA_QVM_Q3_116N && command > 5)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Legacy CGAME has no requested export");
    bool client_init = role->kind != QA_QVM_GAME && command == (role->kind == QA_QVM_UI ? 1 : 0);
    if (client_init && (count != (role->kind == QA_QVM_UI ? 1u : 3u) ||
        (role->kind == QA_QVM_UI && arguments[0] != 0 && arguments[0] != 1)))
        return application_fail(error, QA_ERROR_ARGUMENT, "Client Init requires its actual source invocation arguments");
    if (!q3g_role_activate(role, error)) return false;
    q3g_fire_scope fire = {0};
    if (!q3g_fire_begin(role, &fire, error)) return false;
    bool drawing = role->kind == QA_QVM_CGAME && command == 3;
    if (drawing && (count != 3 || role->draw_entry)) {
        qa_error cleanup = {0}; q3g_fire_end(&fire, &cleanup);
        return application_fail(error, QA_ERROR_ARGUMENT, "CGAME Draw requires its exact unentered argument tuple");
    }
    q3g_role *previous = role->engine->entered_role;
    role->engine->entered_role = role;
    ++role->engine->calls;
    if (drawing) {
        memcpy(role->draw_arguments, arguments, sizeof(role->draw_arguments));
        role->draw_source = (qa_application_q3_client_context){0};
        if (role->local_client && !qa_application_q3_client_context_read(role->engine->provider->application,
            role->engine->provider->owner, role->seat, &role->draw_source, error)) {
            --role->engine->calls; role->engine->entered_role = previous;
            qa_error cleanup = {0}; q3g_fire_end(&fire, &cleanup);
            return false;
        }
        role->draw_entry = true;
    }
    if (drawing && ((role->equipment && !application_q3_equipment_draw_begin(role->equipment, error)) ||
        (role->body && !application_q3_body_draw_begin(role->body, error)))) {
        if (role->body) application_q3_body_draw_end(role->body);
        if (role->equipment) application_q3_equipment_draw_end(role->equipment);
        role->draw_entry = false;
        --role->engine->calls;
        role->engine->entered_role = previous;
        qa_error fire_error = {0};
        q3g_fire_end(&fire, &fire_error);
        return false;
    }
    bool init = command == (role->kind == QA_QVM_UI ? 1 : 0);
    if (init) role->init_succeeded = false;
    if (role->kind == QA_QVM_GAME)
        application_snapshot_mutated(role->engine->provider->application);
    bool ok;
    if (role->native) {
        ok = qa_native_host_q3_vm_call(role->native, command, arguments, count, result, error);
        if (command == (role->kind == QA_QVM_UI ? 1 : 0) &&
            qa_native_get_lifecycle(qa_native_host_instance(role->native)) == QA_NATIVE_INITIALIZED)
            role->initialized = true;
    }
    else {
        int32_t words[10] = {command};
        if (count) memcpy(words + 1, arguments, count * sizeof(*arguments));
        ok = command == (role->kind == QA_QVM_UI ? 1 : 0) ?
            qa_qvm_invoke_started(role->vm, 0, words, count + 1, result, &role->initialized, error) :
            qa_qvm_invoke(role->vm, 0, words, count + 1, result, error);
    }
    if (drawing) {
        if (role->body) application_q3_body_draw_end(role->body);
        if (role->equipment) application_q3_equipment_draw_end(role->equipment);
        role->draw_entry = false;
        role->draw_source = (qa_application_q3_client_context){0};
    }
    --role->engine->calls;
    role->engine->entered_role = previous;
    qa_error fire_error = {0};
    if (!q3g_fire_end(&fire, &fire_error)) {
        if (ok && error) *error = fire_error;
        ok = false;
    }
    if (init && ok && role->initialized) role->init_succeeded = true;
    if (init && ok && role->initialized && role->kind != QA_QVM_GAME) {
        memcpy(role->init_arguments, arguments, count * sizeof(*arguments));
        role->init_argument_count = (uint8_t)count;
    }
    if (init && ok && role->kind == QA_QVM_GAME) ok = q3g_role_catalog_refresh(role, error);
    return ok;
}

qa_qvm_role q3g_primary_role(const char *path)
{
    const char *name = strrchr(path, '/'); name = name ? name + 1 : path;
    if (!strncmp(name, "cgame", 5)) return QA_QVM_CGAME;
    if (!strncmp(name, "ui", 2)) return QA_QVM_UI;
    return QA_QVM_GAME;
}

static bool begin_frame(void *state, qa_session *session, const qa_source_frame *frame, qa_error *error)
{
    (void)session;
    struct application_q3_guest *engine = state;
    if (engine->restore_pending)
        return application_fail(error, QA_ERROR_ARGUMENT, "Restored Q3 source owners have not finished qualification");
    if (engine->round.phase == Q3G_ROUND_FAILED)
        return q3g_round_fail(engine, &engine->round.failure, error);
    if (engine->round.phase != Q3G_ROUND_NONE)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 round requires its admitted settlement source frames");
    engine->milliseconds = (int32_t)(uint32_t)(frame->time_ns / UINT64_C(1000000));
    if (!engine->map_ready || !engine->game || !engine->game->initialized) return true;
    int32_t result;
    qa_cvars *cvars = application_guest_q3_console_registry(engine->provider);
    const qa_cvar_view *bots = qa_cvars_read(cvars,
        application_guest_q3_console_control(engine->provider, APPLICATION_Q3_CVAR_BOT_ENABLE));
    if (!q3g_publish_information(engine->game, false, error)) return false;
    if (bots && bots->integer && application_bots_guest_runtime(engine->provider->application, engine->provider) &&
        !q3g_call(engine->game, 10, &engine->milliseconds, 1, &result, error)) return false;
    bool ok = q3g_call(engine->game, 8, &engine->milliseconds, 1, &result, error);
    if (ok) ok = application_guest_bots_admit(engine->provider, error);
    if (ok) ok = application_guest_clients_drain(engine->provider, error);
    return ok;
}

static void actor_released(void *state, qa_session *session, qa_actor_record actor)
{
    (void)session;
    struct application_q3_guest *engine = state;
    qa_error error = {0};
    if (!application_q3_guest_actor_released(engine->provider, actor, &error))
        application_fault(engine->provider->application, &error);
}

static bool command_actor(void *state, qa_session *session, qa_actor_id actor)
{
    struct application_q3_guest *engine = state;
    application_provider *provider = engine ? engine->provider : NULL;
    if (!provider || provider->application->session != session || !provider->constructed ||
        !provider->attached || engine->restore_pending || !engine->map_ready || !engine->game ||
        !engine->game->initialized || engine->game->retired || !engine->game->host ||
        !qa_actors_get(qa_session_actors(session), actor)) return false;
    uint32_t slot;
    if (!qa_q3_host_actor_slot(engine->game->host, actor, &slot, NULL) || slot >= 64) return false;
    const q3g_client *client = &engine->clients[slot];
    return client->allocated && client->connected && client->begun && !client->pending_retirement &&
        qa_actor_id_equal(client->actor, actor);
}

bool application_guest_q3_create_empty(qa_application *application, application_provider *provider,
                                      qa_world *world, const qa_product *product,
                                      const qa_launch_choices *choices, bool restoring, qa_error *error)
{
    if (!application || !provider || !world || !product || !choices ||
        product->family != QA_GAME_Q3 || !provider->launch->selection.artifact ||
        (provider->kind != APPLICATION_PROVIDER_QVM && provider->kind != APPLICATION_PROVIDER_NATIVE) ||
        q3g_engine(provider) || (restoring && application->operation != APPLICATION_PERSISTING))
        return application_fail(error, QA_ERROR_ARGUMENT, "invalid Q3 guest construction");
    struct application_q3_guest *engine = calloc(1, sizeof(*engine));
    if (!engine) return application_fail(error, QA_ERROR_MEMORY, "allocating Q3 guest owner");
    engine->provider = provider; engine->world = world;
    engine->restore_pending = restoring;
    if (!restoring) {
        const qa_launch_snapshot *published = qa_configuration_current(application->configuration);
        const qa_launch_instance *previous = published ? qa_launch_snapshot_find(published,
            provider->launch->selection.instance) : NULL;
        application_provider *old = previous ? previous->state : NULL;
        struct application_q3_guest *old_engine = old != provider ? q3g_engine(old) : NULL;
        if (old_engine) {
            const qa_launch_snapshot *candidate = application->routing_snapshot;
            const qa_launch_instance *selected = candidate ? qa_launch_snapshot_find(candidate,
                provider->launch->selection.instance) : NULL;
            if (old->application != application || old->owner != provider->owner ||
                old_engine->provider != old || !old->constructed || !old->attached || old->close_pending ||
                old_engine->calls || old_engine->restore_pending || !qa_world_idle(old_engine->world) ||
                !old->launch || previous->storage != old->launch->storage ||
                (previous->identity != old->launch->identity) || !selected ||
                selected->state != provider || selected->storage != provider->launch->storage ||
                (selected->identity != provider->launch->identity) ||
                old_engine->role_sequence == UINT64_MAX) {
                free(engine);
                return application_fail(error, QA_ERROR_ARGUMENT,
                    "Fresh Q3 role lifetimes lost their actual published and candidate registration owners");
            }
            for (q3g_role *role = old_engine->roles; role; role = role->next)
                if (role->engine != old_engine || !role->service_owner || !role->service_sequence ||
                    role->service_sequence > old_engine->role_sequence) {
                    free(engine);
                    return application_fail(error, QA_ERROR_ARGUMENT,
                        "Published Q3 role leaves its actual registration sequence authority");
                }
            engine->role_sequence = old_engine->role_sequence;
        }
    }
    engine->milliseconds = (int32_t)(uint32_t)(provider->launch->selection.clock.initial_time_ns /
        UINT64_C(1000000));
    engine->random_seed = (int32_t)(provider->owner * UINT32_C(2246822519));
    application_q3_world_startup startup;
    if (application_q3_world_restart_source(application, provider, &startup)) {
        engine->milliseconds = (int32_t)(uint32_t)(startup.initial_time_ns / UINT64_C(1000000));
        memcpy(&engine->random_seed, &startup.random_seed, sizeof(engine->random_seed));
        engine->startup_restart = startup.restart;
    }
    engine->product = !strcmp(product->campaign, "missionpack") ? QA_Q3_TEAM_ARENA : QA_Q3_ARENA;
    qa_q3_gamestate_init(&engine->gamestate);
    for (size_t i = 0; i < 64; ++i) {
        qa_q3_reliable_init(&engine->clients[i].reliable);
        engine->clients[i].sensitivity = 1; engine->seats[i] = UINT32_MAX;
    }
    if (choices->seat_count > 64) {
        free(engine); return application_fail(error, QA_ERROR_ARGUMENT, "Q3 guest exceeds the source client limit");
    }
    for (size_t i = 0; i < choices->seat_count; ++i) engine->seats[i] = choices->seats[i].id;
    if (q3g_primary_role(provider->launch->selection.artifact) == QA_QVM_GAME &&
        !application_guest_q3_console_create(engine, choices->world.map, restoring, error)) {
        free(engine); return false;
    }
    if (provider->kind == APPLICATION_PROVIDER_QVM) provider->state.qvm.engine = engine;
    else provider->state.native.engine = engine;
    provider->component = (qa_component){.owner = provider->owner,
        .clock = provider->launch->selection.clock, .state = engine,
        .begin_frame = begin_frame, .actor_released = actor_released, .command_actor = command_actor};
    return true;
}

bool q3g_selected_client_seat(const application_provider *provider,
    const qa_launch_choices *choices, qa_qvm_role kind, size_t index)
{
    if (!provider || !choices || index >= choices->seat_count || choices->seats[index].bot) return false;
    qa_launch_role selected_roles[2] = {kind == QA_QVM_CGAME ? QA_ROLE_HUD : QA_ROLE_MENU, QA_ROLE_ARSENAL};
    size_t count = kind == QA_QVM_CGAME && provider->kind == APPLICATION_PROVIDER_QVM &&
        q3g_primary_role(provider->launch->selection.artifact) == QA_QVM_GAME ? 2 : 1;
    const qa_launch_seat *seat = &choices->seats[index];
    for (size_t n = 0; n < count; ++n) {
        qa_launch_role selected_role = selected_roles[n];
        const qa_launch_binding *binding = NULL;
        if (seat->actor.generation)
            for (size_t i = 0; i < choices->binding_count; ++i) {
                const qa_launch_binding *candidate = &choices->bindings[i];
                if (candidate->role == selected_role && candidate->scope.kind == QA_SCOPE_ACTOR &&
                    qa_actor_id_equal(candidate->scope.actor, seat->actor) && !*candidate->selector) {
                    binding = candidate; break;
                }
            }
        if (!binding) binding = qa_launch_binding_for(choices,
            (qa_launch_scope){.kind = QA_SCOPE_SEAT, .seat = seat->id}, selected_role, "");
        if (!binding) binding = qa_launch_binding_for(choices,
            (qa_launch_scope){.kind = QA_SCOPE_WORLD}, selected_role, "");
        if (binding && !strcmp(binding->instance, provider->launch->selection.instance)) return true;
    }
    return false;
}

bool application_construct_q3_guest(qa_application *application, application_provider *provider,
                                      qa_world *world, const qa_product *product,
                                      const qa_launch_choices *choices, qa_error *error)
{
    bool prepared = q3g_engine(provider) != NULL;
    if (prepared ? !application_guest_q3_factory_reuse(application, provider, world, product, choices, error) :
        !application_guest_q3_create_empty(application, provider, world, product, choices, false, error))
        return false;
    struct application_q3_guest *engine = q3g_engine(provider);
    const char *path = provider->launch->selection.artifact;
    qa_qvm_role kind = q3g_primary_role(path);
    uint32_t seat = choices->seat_count ? choices->seats[0].id : UINT32_MAX;
    if (kind != QA_QVM_GAME) {
        seat = UINT32_MAX;
        for (size_t i = 0; i < choices->seat_count; ++i) {
            if (q3g_selected_client_seat(provider, choices, kind, i)) {
                seat = choices->seats[i].id;
                break;
            }
        }
        if (seat == UINT32_MAX)
            return application_fail(error, QA_ERROR_ARGUMENT, "Q3 primary client role has no selected actual seat");
    }
    q3g_role *role = NULL;
    if (!q3g_role_create(engine, kind, seat, path, true, &role, error)) return false;
    role->next = engine->roles; engine->roles = role;
    if (kind == QA_QVM_GAME) {
        engine->game = role;
        q3g_game_aliases(engine, role);
    } else {
        for (size_t i = 0; i < choices->seat_count; ++i) {
            if (choices->seats[i].id == seat || !q3g_selected_client_seat(provider, choices, kind, i)) continue;
            q3g_role *companion = NULL;
            if (!q3g_role_create(engine, kind, choices->seats[i].id, path, false, &companion, error)) return false;
            companion->next = engine->roles;
            engine->roles = companion;
        }
    }
    /* The source client owns its UI helper even when another provider owns
     * the visible MENU. Selection does not transfer this source lifetime. */
    if (kind == QA_QVM_CGAME)
        for (size_t i = 0; i < choices->seat_count; ++i) {
            if (!q3g_selected_client_seat(provider, choices, QA_QVM_CGAME, i)) continue;
            q3g_role *ui = NULL;
            if (!application_guest_q3_source_ui_create(engine, choices->seats[i].id,
                &ui, error)) return false;
            ui->next = engine->roles; engine->roles = ui;
        }
    return true;
}

bool application_q3_guest_client_sources_rebuild(application_provider *provider,
    application_provider *next_game, const qa_launch_choices *choices, qa_error *error)
{
    struct application_q3_guest *engine = q3g_engine(provider);
    if (!engine || engine->game) return true;
    qa_application *application = provider->application;
    if (!choices || choices->seat_count > 64 || !provider->constructed || !provider->attached ||
        engine->calls || engine->draining_clients || engine->restore_pending ||
        application->operation != APPLICATION_CONFIGURING ||
        !qa_session_safe(application->session) || !qa_world_idle(engine->world) ||
        qa_actors_count(qa_session_actors(application->session)) ||
        !application_q3_guest_idle(provider) || !next_game || next_game->application != application ||
        q3g_game_source(application) != next_game)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 client reconstruction requires its admitted empty world topology");
    const char *path = provider->launch->selection.artifact;
    qa_qvm_role kind = q3g_primary_role(path);
    if (kind == QA_QVM_GAME)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 standalone client reconstruction has a GAME artifact");
    size_t selected = 0;
    for (size_t i = 0; i < choices->seat_count; ++i)
        selected += q3g_selected_client_seat(provider, choices, kind, i);
    if (!selected)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 standalone client artifact has no selected actual seat");
    for (q3g_role *role = engine->roles; role; role = role->next)
        if (role->local_client && role->client_source && role->client_source != next_game)
            return application_fail(error, QA_ERROR_ARGUMENT, "Q3 client still borrows its old physical GAME source");
    for (size_t i = 0; i < 64; ++i)
        engine->seats[i] = i < choices->seat_count ? choices->seats[i].id : UINT32_MAX;
    q3g_role **position = &engine->roles;
    while (*position) {
        q3g_role *role = *position;
        bool admitted = false;
        for (size_t i = 0; i < choices->seat_count; ++i)
            if (choices->seats[i].id == role->seat &&
                q3g_selected_client_seat(provider, choices, role->kind, i)) admitted = true;
        if (role->kind == kind && !strcmp(role->path, path) && !admitted) {
            if (role->initialized || role->host || role->vm || role->native || role->native_client)
                return application_fail(error, QA_ERROR_ARGUMENT, "Unselected Q3 client role has not physically retired");
            q3g_role *next = role->next;
            if (!q3g_role_destroy(role, error)) return false;
            *position = next;
        } else position = &role->next;
    }
    q3g_role *primary = NULL;
    for (size_t i = 0; i < choices->seat_count; ++i) {
        if (!q3g_selected_client_seat(provider, choices, kind, i)) continue;
        q3g_role *role = NULL;
        for (q3g_role *current = engine->roles; current; current = current->next)
            if (current->kind == kind && current->seat == choices->seats[i].id &&
                !strcmp(current->path, path)) { role = current; break; }
        if (role && role->retired && !role->host) {
            q3g_role *replacement = NULL;
            if (!q3g_role_restart(role, &replacement, error)) return false;
            role = replacement;
        } else if (role && (!role->ready || role->retired || !role->host)) {
            return application_fail(error, QA_ERROR_ARGUMENT, "Q3 client role has an incomplete physical source owner");
        } else if (!role) {
            if (!q3g_role_create(engine, kind, choices->seats[i].id, path, false, &role, error)) return false;
            role->next = engine->roles;
            engine->roles = role;
        }
        if (!primary) primary = role;
    }
    for (q3g_role *role = engine->roles; role;) {
        q3g_role *next = role->next;
        if (role->kind == kind && !strcmp(role->path, path)) role->primary = role == primary;
        else if (role->kind != QA_QVM_GAME && role->retired && !role->host) {
            q3g_role *replacement = NULL;
            if (!q3g_role_restart(role, &replacement, error)) return false;
        }
        role = next;
    }
    return true;
}

bool q3g_world_begin(struct application_q3_guest *engine, qa_error *error)
{
    application_provider *provider = engine ? engine->provider : NULL;
    qa_application *application = provider ? provider->application : NULL;
    if (!application || !engine->game || !engine->game->primary ||
        engine->game->kind != QA_QVM_GAME || engine->restore_pending ||
        application->operation == APPLICATION_PERSISTING ||
        engine->world != application->world || !application->physics ||
        application->physics->world != engine->world ||
        application->physics->world_actor.registry)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "Original Q3 world admission requires its fresh primary map owner");
    qa_string_id definition;
    if (!qa_strings_intern_cstr(qa_session_strings(application->session),
            "worldspawn", &definition, error) ||
        !qa_session_allocate(application->session, provider->owner, definition,
            true, QA_Q3_SOURCE_WORLD, &application->physics->world_actor, error))
        return false;
    return qa_world_body_create(engine->world, application->physics->world_actor,
                                &(qa_body_state){0}, error);
}

bool application_q3_guest_spawn_map(application_provider *provider, const qa_bsp_view *map,
                                      const qa_entities *entities, qa_string_id map_name,
                                      qa_string_id spawn_point, qa_error *error)
{
    (void)entities; (void)spawn_point;
    struct application_q3_guest *engine = q3g_engine(provider);
    if (!engine || !map || engine->calls || engine->round.phase != Q3G_ROUND_NONE)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 guest map publication requires an idle owner");
    if (!engine->game)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 map entity publication requires a game role");
    const char *path=qa_strings_cstr(qa_session_strings(provider->application->session),map_name);
    qa_cvars *map_cvars=application_guest_q3_console_registry(provider);
    if (!path || !map_cvars)
        return application_fail(error,QA_ERROR_FORMAT,"Original GAME map lacks its actual source registry/path");
    if (!qa_cvars_set(map_cvars,"mapname",path,true,error) ||
        !qa_cvars_set(map_cvars,"sv_mapname",path,true,error)) return false;
    qa_bytes text = map->lumps[QA_BSP_ENTITIES].bytes;
    if (text.size == SIZE_MAX) return application_fail(error, QA_ERROR_MEMORY, "Q3 entity text exceeds capacity");
    char *copy = malloc(text.size + 1);
    if (!copy) return application_fail(error, QA_ERROR_MEMORY, "retaining Q3 map entity text");
    if (text.size) memcpy(copy, text.data, text.size);
    copy[text.size] = 0;
    if (engine->map_ready) {
        if (engine->game) {
            q3g_role *replacement;
            if (!q3g_role_restart(engine->game, &replacement, error)) { free(copy); return false; }
        }
        for (q3g_role *role = engine->roles; role;) {
            q3g_role *next = role->next;
            if (role->kind == QA_QVM_CGAME) {
                q3g_role *replacement;
                if (!q3g_role_restart(role, &replacement, error)) { free(copy); return false; }
            }
            role = next;
        }
    }
    free(engine->entity_text); engine->entity_text = copy;
    qa_q3_gamestate_init(&engine->gamestate);
    q3g_clients_clear(engine);
    for (q3g_role *role = engine->roles; role; role = role->next)
        if (!qa_q3_host_set_entity_text(role->host, (qa_bytes){(const uint8_t *)copy, text.size}, error)) return false;
    engine->map_ready = true;
    engine->loaded_compatibility = false;
    engine->loaded_game_type = engine->loaded_max_clients = 0;
    if (!q3g_world_begin(engine, error)) return false;
    int32_t arguments[] = {engine->milliseconds, engine->random_seed, engine->startup_restart ? 1 : 0}, result;
    bool ok = q3g_call(engine->game, 0, arguments, 3, &result, error);
    if (ok) { engine->game->initialized = true; engine->startup_restart = false; }
    if (ok) ok = q3g_publish_information(engine->game, true, error);
    if (ok) ok = application_guest_clients_drain(provider, error);
    if (ok) {
        qa_cvars *cvars = application_guest_q3_console_registry(engine->provider);
        const qa_cvar_view *game_type = qa_cvars_read(cvars,
            application_guest_q3_console_control(engine->provider, APPLICATION_Q3_CVAR_GAME_TYPE));
        const qa_cvar_view *clients = qa_cvars_read(cvars,
            application_guest_q3_console_control(engine->provider, APPLICATION_Q3_CVAR_MAX_CLIENTS));
        if (game_type && clients && clients->integer >= 1 && clients->integer <= 64) {
            engine->loaded_game_type = game_type->integer;
            engine->loaded_max_clients = clients->integer;
            engine->loaded_compatibility = true;
        }
        if (cvars) {
            qa_cvars_clear_modified(cvars, "g_gametype");
            qa_cvars_clear_modified(cvars, "sv_maxclients");
        }
        provider->map_bound = true;
    }
    return ok;
}

bool application_q3_guest_actor_released(application_provider *provider, qa_actor_record actor, qa_error *error)
{
    struct application_q3_guest *engine = q3g_engine(provider);
    if (!engine) return true;
    for (q3g_role *role = engine->roles; role; role = role->next) {
        application_q3_weapons_services_actor_released(role->weapon_services, actor);
        if (!application_q3_combat_actor_released(role->combat, actor, error)) return false;
        if (role->host && !qa_q3_host_actor_released(role->host, actor, error)) return false;
    }
    for (size_t i = 0; i < 64; ++i)
        if (qa_actor_id_equal(engine->clients[i].actor, actor.id)) {
            q3g_client *client = &engine->clients[i];
            client->fire = (q3g_fire_continuation){0};
            client->reserved = false;
            if (engine->round.phase == Q3G_ROUND_RETIRING &&
                (engine->round.carried & (UINT64_C(1) << i))) {
                client->actor = (qa_actor_id){0};
                client->connected = client->begun = client->roster_attached = false;
                continue;
            }
            if (client->bot || client->roster_attached || client->pending_retirement) {
                client->pending_retirement = true;
                client->pending_bot = client->disconnect_pending = false;
            } else client->actor = (qa_actor_id){0};
            engine->clients[i].connected = engine->clients[i].allocated = false;
        }
    return true;
}

bool application_q3_guest_deconstruct(application_provider *provider, qa_error *error)
{
    struct application_q3_guest *engine = q3g_engine(provider);
    if (!engine) return true;
    if (engine->calls || engine->client_leases || !qa_world_idle(engine->world) || !application_guest_q3_console_idle(engine))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 guest owner or world callbacks are executing");
    qa_error first = {0};
    bool ok = true;
    while (engine->roles) {
        q3g_role *role = engine->roles;
        qa_error current = {0};
        bool shut_down = q3g_role_shutdown(role, false, &current);
        if (!shut_down && ok) { ok = false; first = current; }
        if (!shut_down && role->initialized) {
            if (error) *error = first;
            return false;
        }
        if (role->projection) {
            application_guest_projection *projection = role->projection;
            for (guest_projection_actor *actor = projection->actors; actor; actor = actor->next)
                if (!application_guest_projection_detach(role, actor->actor, &current) && ok) {
                    ok = false; first = current;
                }
        }
        q3g_role *next = role->next;
        bool was_game = engine->game == role;
        if (!q3g_role_destroy(role, &current)) {
            if (error) *error = ok ? current : first;
            return false;
        }
        engine->roles = next;
        if (was_game) {
            engine->game = NULL;
            q3g_game_aliases(engine, NULL);
        }
    }
    if (!application_guest_q3_client_console_destroy(engine, error) ||
        !application_guest_q3_console_destroy(engine, error)) return false;
    q3g_game_aliases(engine, NULL);
    if (provider->kind == APPLICATION_PROVIDER_QVM) provider->state.qvm.engine = NULL;
    else provider->state.native.engine = NULL;
    q3g_clients_clear(engine); qa_command_tokens_free(&engine->arguments);
    while (engine->artifacts) {
        q3g_artifact *artifact = engine->artifacts; engine->artifacts = artifact->next;
        qa_qvm_image_release(artifact->image); qa_native_module_release(artifact->module);
        qa_native_declaration_destroy(artifact->declaration); qa_buffer_free(&artifact->primary);
        qa_resource_release(artifact->resource); qa_vfs_acquisition_dispose(&artifact->acquisition);
        qa_resource_release(artifact->items_resource);
        qa_vfs_acquisition_dispose(&artifact->items_acquisition);
        qa_resource_release(artifact->body_resource);
        qa_vfs_acquisition_dispose(&artifact->body_acquisition);
        application_q3_body_profile_free(&artifact->body_profile);
        qa_resource_release(artifact->weapon_models_resource);
        qa_vfs_acquisition_dispose(&artifact->weapon_models_acquisition);
        application_q3_weapon_models_profile_free(&artifact->weapon_models_profile);
        qa_launch_instance_lease_release(artifact->descriptor);
        qa_buffer_free(&artifact->equipment_presentation);
        qa_buffer_free(&artifact->collision_scene);
        application_q3_equipment_profile_free(&artifact->equipment_profile);
        application_q3_grapple_profile_destroy(artifact->grapple_profile);
        application_q3_combat_profile_destroy(artifact->combat_profile);
        free(artifact->path); free(artifact);
    }
    application_guest_q3_save_clear(engine);
    qa_launch_instance_lease_release(engine->client_descriptor);
    free(engine->entity_text); free(engine);
    if (!ok && error) *error = first;
    return ok;
}

bool application_q3_guest_retire_map(application_provider *provider, qa_error *error)
{
    struct application_q3_guest *engine = q3g_engine(provider);
    if (!engine) return true;
    if (engine->calls || !qa_world_idle(engine->world))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 guest map retirement requires idle source and world callbacks");
    qa_error first = {0};
    bool ok = true;
    bool restart = application_q3_world_restart_guest_shutdown(provider->application, provider);
    for (q3g_role *role = engine->roles; role; role = role->next) {
        if (role->kind == QA_QVM_UI) continue;
        qa_error current = {0};
        bool was_initialized = role->initialized;
        bool shut_down = q3g_role_shutdown(role, restart, &current);
        if (!shut_down && ok) { ok = false; first = current; }
        if (!shut_down && role->initialized) {
            if (error) *error = first;
            return false;
        }
        if (shut_down && was_initialized && role->kind == QA_QVM_GAME) {
            qa_cvars *cvars = NULL;
            qa_q3_host_console(role->host, &cvars, NULL);
            bool handed_off = application_q3_campaign_launch_guest_handoff(provider,
                cvars, role->service_owner, &current);
            if (!handed_off && ok) { ok = false; first = current; }
        }
        if (shut_down && was_initialized && restart && role->kind == QA_QVM_GAME) {
            qa_cvars *cvars = NULL;
            qa_q3_host_console(role->host, &cvars, NULL);
            engine->handoff_ready = true;
            bool handed_off = application_q3_world_restart_guest_handoff(provider->application,
                provider, cvars, &current);
            engine->handoff_ready = false;
            if (!handed_off && ok) { ok = false; first = current; }
        }
        if (role->host && !qa_q3_host_close_map(role->host, &current) && ok) { ok = false; first = current; }
    }
    if (!ok && error) *error = first;
    return ok;
}

bool application_q3_guest_idle(const application_provider *provider)
{
    const struct application_q3_guest *engine = !provider ? NULL :
        provider->kind == APPLICATION_PROVIDER_QVM ? provider->state.qvm.engine :
        provider->kind == APPLICATION_PROVIDER_NATIVE ? provider->state.native.engine : NULL;
    if (!engine) return true;
    if (engine->calls || engine->entered_role || !application_guest_q3_console_idle(engine)) return false;
    for (const q3g_role *role = engine->roles; role; role = role->next) {
        if (role->equipment && !application_q3_equipment_idle(role->equipment)) return false;
        if (role->body && !application_q3_body_idle(role->body)) return false;
        if (role->weapon_models && !application_q3_weapon_models_idle(role->weapon_models)) return false;
        if (role->weapons && !application_q3_weapons_idle(role->weapons)) return false;
        if (role->weapon_services && !application_q3_weapons_services_idle(role->weapon_services)) return false;
        if (role->combat && !application_q3_combat_idle(role->combat)) return false;
        if (role->pickups && !application_q3_pickups_idle(role->pickups)) return false;
        if (role->vm && !qa_qvm_can_destroy(role->vm)) return false;
        if (role->native && !qa_native_can_destroy(qa_native_host_instance(role->native))) return false;
    }
    return true;
}
