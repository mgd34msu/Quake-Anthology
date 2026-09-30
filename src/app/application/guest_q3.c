#include "guest_q3_private.h"
#include "guest_projection_private.h"

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
        !result || count > 9 || (count && !arguments))
        return application_fail(error, QA_ERROR_ARGUMENT, "invalid Q3 guest entry");
    if (!q3g_role_activate(role, error)) return false;
    ++role->engine->calls;
    bool ok;
    if (role->native) ok = qa_native_host_q3_vm_call(role->native, command, arguments, count, result, error);
    else {
        int32_t words[10] = {command};
        if (count) memcpy(words + 1, arguments, count * sizeof(*arguments));
        ok = qa_qvm_invoke(role->vm, 0, words, count + 1, result, error);
    }
    --role->engine->calls;
    return ok;
}

static qa_qvm_role primary_role(const char *path)
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
    engine->milliseconds = (int32_t)(uint32_t)(frame->time_ns / UINT64_C(1000000));
    if (!engine->map_ready || !engine->game || !engine->game->initialized) return true;
    int32_t result;
    if (!q3g_call(engine->game, 10, &engine->milliseconds, 1, &result, error)) return false;
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
    if (provider->kind == APPLICATION_PROVIDER_QVM) provider->state.qvm.engine = engine;
    else provider->state.native.engine = engine;
    provider->component = (qa_component){.owner = provider->owner,
        .clock = provider->launch->selection.clock, .state = engine,
        .begin_frame = begin_frame, .actor_released = actor_released};
    return true;
}

bool application_construct_q3_guest(qa_application *application, application_provider *provider,
                                      qa_world *world, const qa_product *product,
                                      const qa_launch_choices *choices, qa_error *error)
{
    if (!application_guest_q3_create_empty(application, provider, world, product, choices, false, error))
        return false;
    struct application_q3_guest *engine = q3g_engine(provider);
    const char *path = provider->launch->selection.artifact;
    qa_qvm_role kind = primary_role(path);
    uint32_t seat = choices->seat_count ? choices->seats[0].id : UINT32_MAX;
    q3g_role *role = NULL;
    if (!q3g_role_create(engine, kind, seat, path, true, &role, error)) return false;
    role->next = engine->roles; engine->roles = role;
    if (kind == QA_QVM_GAME) {
        engine->game = role;
        if (provider->kind == APPLICATION_PROVIDER_QVM) {
            provider->state.qvm.host = role->host; provider->state.qvm.machine = role->vm;
        } else {
            provider->state.native.q3_host = role->host; provider->state.native.host = role->native;
        }
    }
    return true;
}

bool application_q3_guest_spawn_map(application_provider *provider, const qa_bsp_view *map,
                                      const qa_entities *entities, qa_string_id map_name,
                                      qa_string_id spawn_point, qa_error *error)
{
    (void)entities; (void)map_name; (void)spawn_point;
    struct application_q3_guest *engine = q3g_engine(provider);
    if (!engine || !map || engine->calls)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 guest map publication requires an idle owner");
    if (!engine->game)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 map entity publication requires a game role");
    qa_bytes text = map->lumps[QA_BSP_ENTITIES].bytes;
    if (text.size == SIZE_MAX) return application_fail(error, QA_ERROR_MEMORY, "Q3 entity text exceeds capacity");
    char *copy = malloc(text.size + 1);
    if (!copy) return application_fail(error, QA_ERROR_MEMORY, "retaining Q3 map entity text");
    if (text.size) memcpy(copy, text.data, text.size);
    copy[text.size] = 0;
    if (engine->map_ready) {
        for (q3g_role *role = engine->roles; role;) {
            q3g_role *next = role->next;
            if (role->kind == QA_QVM_CGAME) {
                q3g_role *replacement;
                if (!q3g_role_restart(role, &replacement, error)) { free(copy); return false; }
            }
            role = next;
        }
        if (engine->game) {
            q3g_role *replacement;
            if (!q3g_role_restart(engine->game, &replacement, error)) { free(copy); return false; }
        }
    }
    free(engine->entity_text); engine->entity_text = copy;
    qa_q3_gamestate_init(&engine->gamestate);
    q3g_clients_clear(engine);
    for (q3g_role *role = engine->roles; role; role = role->next)
        if (!qa_q3_host_set_entity_text(role->host, (qa_bytes){(const uint8_t *)copy, text.size}, error)) return false;
    engine->map_ready = true;
    int32_t arguments[] = {engine->milliseconds,
        (int32_t)(provider->owner * UINT32_C(2246822519)), 0}, result;
    bool ok = q3g_call(engine->game, 0, arguments, 3, &result, error);
    if (ok) engine->game->initialized = true;
    if (ok) ok = application_guest_clients_drain(provider, error);
    return ok;
}

bool application_q3_guest_actor_released(application_provider *provider, qa_actor_record actor, qa_error *error)
{
    struct application_q3_guest *engine = q3g_engine(provider);
    if (!engine) return true;
    for (q3g_role *role = engine->roles; role; role = role->next)
        if (role->host && !qa_q3_host_actor_released(role->host, actor, error)) return false;
    for (size_t i = 0; i < 64; ++i)
        if (qa_actor_id_equal(engine->clients[i].actor, actor.id)) {
            q3g_client *client = &engine->clients[i];
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
    if (engine->calls || !qa_world_idle(engine->world))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 guest owner or world callbacks are executing");
    qa_error first = {0};
    bool ok = true;
    while (engine->roles) {
        q3g_role *role = engine->roles;
        qa_error current = {0};
        if (!q3g_role_shutdown(role, &current) && ok) { ok = false; first = current; }
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
            if (provider->kind == APPLICATION_PROVIDER_QVM) {
                provider->state.qvm.host = NULL; provider->state.qvm.machine = NULL;
            } else { provider->state.native.q3_host = NULL; provider->state.native.host = NULL; }
        }
    }
    if (provider->kind == APPLICATION_PROVIDER_QVM) {
        provider->state.qvm.host = NULL; provider->state.qvm.machine = NULL; provider->state.qvm.engine = NULL;
    } else { provider->state.native.q3_host = NULL; provider->state.native.host = NULL; provider->state.native.engine = NULL; }
    q3g_clients_clear(engine); qa_command_tokens_free(&engine->arguments);
    while (engine->artifacts) {
        q3g_artifact *artifact = engine->artifacts; engine->artifacts = artifact->next;
        qa_qvm_image_release(artifact->image); qa_native_module_release(artifact->module);
        qa_native_declaration_destroy(artifact->declaration); qa_buffer_free(&artifact->primary);
        free(artifact->path); free(artifact);
    }
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
    for (q3g_role *role = engine->roles; role; role = role->next) {
        if (role->kind == QA_QVM_UI) continue;
        qa_error current = {0};
        if (!q3g_role_shutdown(role, &current) && ok) { ok = false; first = current; }
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
    if (engine->calls) return false;
    for (const q3g_role *role = engine->roles; role; role = role->next) {
        if (role->vm && qa_qvm_active(role->vm)) return false;
        if (role->native && !qa_native_can_destroy(qa_native_host_instance(role->native))) return false;
    }
    return true;
}
