#include "guest_qc_profile.h"
#include <stdio.h>

bool application_qc_console_command(application_provider *provider, qa_actor_id actor,
                                      const char *text, bool client_command,
                                      bool *handled, qa_error *error)
{
    if (!provider || provider->kind != APPLICATION_PROVIDER_QC || !text || !handled ||
        !provider->state.qc.engine || !qa_qc_idle(provider->state.qc.instance) ||
        !application_qc_input_idle(provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "QuakeC console command requires an idle source owner");
    *handled = false;
    struct application_qc_state *engine = provider->state.qc.engine;
    bool client = false;
    for (uint32_t i = 1; i <= engine->max_clients; ++i)
        if (engine->clients[i].connected && engine->clients[i].spawned &&
            qa_actor_id_equal(engine->clients[i].actor, actor)) { client = true; break; }
    if (actor.registry && (!client || !qa_actors_get(qa_session_actors(provider->application->session), actor)))
        return application_fail(error, QA_ERROR_ARGUMENT, "QuakeC command actor has no live source client");
    const char *name = client_command ? "SV_ParseClientCommand" : "ConsoleCmd";
    uint32_t function_index;
    const qa_qc_function *function = qa_qc_program_find_function(provider->state.qc.program, name, &function_index);
    if (!function) return true;
    if (!function_index || function->parameter_count > 1 ||
        (function->parameter_count && function->parameter_sizes[0] != 1))
        return application_fail(error, QA_ERROR_FORMAT, "QuakeC console export has an incompatible source signature");
    qa_qc_game_value argument = {QA_QC_GAME_STRING, {.string = text}};
    qa_qc_game_global globals[] = {
        {"self", {QA_QC_GAME_ACTOR, {.actor = actor}}},
        {"time", {QA_QC_GAME_FLOAT, {.number = (float)((double)engine->source_time_ns / 1e9)}}}
    };
    uint32_t result[3];
    int32_t prior = 0;
    qa_qc_instance *vm = provider->state.qc.instance;
    bool staged = !function->parameter_count;
    if (staged) {
        size_t length = strlen(text);
        if (length > SIZE_MAX - 17) return application_fail(error, QA_ERROR_MEMORY, "QuakeC command text extent overflow");
        char *key = malloc(length + 17);
        if (!key) return application_fail(error, QA_ERROR_MEMORY, "Allocating QuakeC command string identity");
        memcpy(key, "qc-command-text:", 16);
        memcpy(key + 16, text, length + 1);
        int32_t reference;
        bool ok = qa_qc_global_int(vm, 4, &prior, error) &&
            qa_qc_engine_string(vm, key, text, length + 1, &reference, error);
        free(key);
        if (!ok) return false;
        uint32_t word;
        memcpy(&word, &reference, sizeof(word));
        if (!qa_qc_stage_globals(vm, 4, &word, 1, error)) return false;
    }
    bool ok = qa_qc_game_call(provider->state.qc.game, name, &argument, function->parameter_count,
                               globals, 2, result, error);
    if (staged) {
        uint32_t word;
        memcpy(&word, &prior, sizeof(word));
        qa_error unwind = {0};
        if (!qa_qc_stage_globals(vm, 4, &word, 1, &unwind)) {
            if (ok && error) *error = unwind;
            ok = false;
        }
    }
    if (!ok) return false;
    float value;
    memcpy(&value, result, sizeof(value));
    *handled = client_command || value != 0;
    return true;
}

static bool parms(struct application_qc_state *engine, application_qc_client *client,
                   bool read, qa_error *error)
{
    for (unsigned i = 0; i < 16; ++i) {
        char name[16]; snprintf(name, sizeof(name), "parm%u", i + 1);
        const qa_qc_definition *def = qa_qc_program_find_global(engine->provider->state.qc.program, name);
        if (def == NULL || def->type != QA_QC_FLOAT)
            return application_fail(error, QA_ERROR_FORMAT, "QuakeC spawn parm global is missing");
        if (read) {
            if (!qa_qc_global_float(engine->provider->state.qc.instance, def->offset, &client->parms[i], error)) return false;
        } else if (!qa_qc_set_global_float(engine->provider->state.qc.instance, def->offset, client->parms[i], error)) return false;
    }
    return true;
}
bool application_qc_bind_player(application_provider *provider, uint32_t slot, uint32_t seat,
                                 qa_actor_id actor, const char *name, bool spectator,
                                 bool new_player, bool primary_character, qa_error *error)
{
    struct application_qc_state *engine = provider->state.qc.engine;
    const struct application_qc_profile *qualified = provider->state.qc.qualified;
    if (engine == NULL || slot == 0 || (!qualified && slot > engine->max_clients) || name == NULL ||
        qa_actors_get(qa_session_actors(engine->services.session), actor) == NULL)
        return application_fail(error, QA_ERROR_ARGUMENT, "Invalid QuakeC client admission");
    bool source_map_owned = application_provider_for(provider->application, actor, QA_ROLE_ENTITIES, "") == provider;
    if (!application_qc_player_map_ready(provider, primary_character, source_map_owned, error)) return false;
    if (qualified) {
        uint32_t available = 0; slot = 0;
        for (uint32_t i = 1; i <= engine->max_clients; ++i) {
            if (engine->clients[i].connected && qa_actor_id_equal(engine->clients[i].actor, actor)) { slot = i; break; }
            if (!available && !engine->clients[i].connected) available = i;
        }
        if (!slot) slot = available;
        if (!slot) return application_fail(error, QA_ERROR_MEMORY, "QC component source client capacity exceeded");
    }
    application_qc_client *client = &engine->clients[slot];
    if (client->connected) {
        if (!qa_actor_id_equal(client->actor, actor) || client->seat != seat ||
            client->spectator != spectator || client->primary_character != primary_character)
            return application_fail(error, QA_ERROR_ARGUMENT, "QuakeC client slot is already occupied");
        if (client->spawned) return true;
    }
    for (uint32_t i = 1; i <= engine->max_clients; ++i)
        if (i != slot && engine->clients[i].connected &&
            (engine->clients[i].seat == seat || qa_actor_id_equal(engine->clients[i].actor, actor)))
            return application_fail(error, QA_ERROR_ARGUMENT, "Duplicate QuakeC client actor or seat");
    const char *connect = spectator ? "SpectatorConnect" : "ClientConnect";
    uint32_t index;
    if (!qualified && spectator && engine->profile != QA_QC_QUAKEWORLD)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "QuakeC spectators require QuakeWorld");
    if (!qualified && !spectator && !qa_qc_program_find_function(provider->state.qc.program, connect, &index))
        return application_fail(error, QA_ERROR_UNSUPPORTED, "QuakeC selected client callback is absent");
    if (!qa_qc_game_bind_client(provider->state.qc.game, slot, actor, error)) return false;
    client->actor = actor; client->seat = seat; client->connected = true; client->spectator = spectator;
    client->primary_character = primary_character;
    if (qualified) {
        application_qc_inputs inputs = {.self = actor, .time_ns = engine->source_time_ns};
        if (!application_qc_seed_fields(engine, actor, error) || !application_qc_prepare_markers(engine, error) ||
            !application_qc_run_calls(engine, &qualified->admit, &inputs, error)) return false;
        if (!qa_actors_get(qa_session_actors(engine->services.session), actor))
            return application_fail(error, QA_ERROR_NOT_FOUND, "QC component removed its client during admission");
        client->spawned = true;
        return true;
    }
    qa_qc_instance *vm = provider->state.qc.instance;
    int32_t reference;
    const qa_qc_definition *netname = application_qc_field(engine, "netname", QA_QC_STRING, error);
    int32_t string;
    if (netname == NULL || !application_qc_reference(engine, actor, &reference, error) ||
        !qa_qc_string_allocate(vm, name, &string, error) || !qa_qc_set_entity_int(vm, reference, netname->offset, string, error)) return false;
    if ((new_player || !client->has_parms) && (!application_qc_named(engine, "SetNewParms", (qa_actor_id){0}, error) || !parms(engine, client, true, error))) return false;
    client->has_parms = true;
    if (!parms(engine, client, false, error) || !(spectator ?
        application_qc_spectator_callback(engine, connect, actor, error) : application_qc_named(engine, connect, actor, error))) return false;
    if (!spectator && !application_qc_named(engine, "PutClientInServer", actor, error)) return false;
    if (qa_actors_get(qa_session_actors(engine->services.session), actor) == NULL)
        return application_fail(error, QA_ERROR_NOT_FOUND, "QuakeC client was removed during admission");
    client->spawned = true;
    qa_body_state body;
    if (!qa_world_body_read(engine->world, actor, &body, error)) return false;
    /* Source fields remain private, while the shared movement continuation is
     * admitted from the committed source spawn. */
    application_control_record *control;
    return application_control_ensure(provider->application, actor, body.angles, &control, error);
}
bool application_qc_change_parms(application_provider *provider, qa_error *error)
{
    struct application_qc_state *engine = provider->state.qc.engine;
    if (engine == NULL || !qa_qc_idle(provider->state.qc.instance))
        return application_fail(error, QA_ERROR_ARGUMENT, "QuakeC travel continuation is not idle");
    if (provider->state.qc.qualified) {
        for (uint32_t i = 1; i <= engine->max_clients; ++i)
            if (engine->clients[i].connected &&
                !application_qc_disconnect_player(provider, engine->clients[i].actor, error)) return false;
        return true;
    }
    for (uint32_t i = 1; i <= engine->max_clients; ++i) {
        application_qc_client *client = &engine->clients[i];
        if (!client->connected || qa_actors_get(qa_session_actors(engine->services.session), client->actor) == NULL) continue;
        if (!application_qc_named(engine, "SetChangeParms", client->actor, error) || !parms(engine, client, true, error)) return false;
    }
    const qa_qc_definition *flags = qa_qc_program_find_global(provider->state.qc.program, "serverflags");
    if (flags != NULL && (flags->type != QA_QC_FLOAT ||
        !qa_qc_global_float(provider->state.qc.instance, flags->offset, &engine->serverflags, error))) return false;
    return true;
}
bool application_qc_player_command(application_provider *provider, qa_actor_id actor,
                                    const qa_movement_command *command, qa_error *error)
{
    struct application_qc_state *engine = provider->state.qc.engine;
    if (engine == NULL || command == NULL)
        return application_fail(error, QA_ERROR_ARGUMENT, "QuakeC player command is missing");
    if (provider->state.qc.qualified)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Qualified QC input requires its mutable phase consumer");
    int32_t reference;
    if (!application_qc_reference(engine, actor, &reference, error)) return false;
    const qa_qc_definition *angles = application_qc_field(engine, "v_angle", QA_QC_VECTOR, error);
    if (angles == NULL || !qa_qc_set_entity_vector(provider->state.qc.instance, reference, angles->offset, command->angles, error) ||
        !application_qc_set_float(engine, reference, "button0", (command->buttons & 1u) ? 1 : 0, error) ||
        !application_qc_set_float(engine, reference, "button2", (command->buttons & 2u) ? 1 : 0, error)) return false;
    if (command->impulse != 0 && !application_qc_set_float(engine, reference, "impulse", command->impulse, error)) return false;
    return true;
}
bool application_qc_client_userinfo(application_provider *provider, qa_actor_id actor, qa_error *error)
{
    struct application_qc_state *engine = provider->state.qc.engine;
    if (!engine) return application_fail(error, QA_ERROR_ARGUMENT, "QC userinfo has no engine owner");
    for (uint32_t i = 1; i <= engine->max_clients; ++i) {
        application_qc_client *client = &engine->clients[i];
        if (!client->spawned || !qa_actor_id_equal(client->actor, actor)) continue;
        const struct application_qc_profile *profile = provider->state.qc.qualified;
        application_qc_inputs inputs = {.self = actor, .time_ns = engine->source_time_ns};
        return profile ? application_qc_run_calls(engine, &profile->userinfo, &inputs, error) : true;
    }
    return application_fail(error, QA_ERROR_NOT_FOUND, "QC userinfo requires a live admitted client");
}
bool application_qc_disconnect_player(application_provider *provider, qa_actor_id actor, qa_error *error)
{
    struct application_qc_state *engine = provider->state.qc.engine;
    if (!engine) return application_fail(error, QA_ERROR_ARGUMENT, "QC disconnect has no engine owner");
    for (uint32_t i = 1; i <= engine->max_clients; ++i) {
        application_qc_client *client = &engine->clients[i];
        if (!client->connected || !qa_actor_id_equal(client->actor, actor)) continue;
        if (!client->spawned) return true;
        const struct application_qc_profile *profile = provider->state.qc.qualified;
        application_qc_inputs inputs = {.self = actor, .time_ns = engine->source_time_ns};
        bool ok = profile ? application_qc_run_calls(engine, &profile->disconnect, &inputs, error) :
            client->spectator ? application_qc_spectator_callback(engine, "SpectatorDisconnect", actor, error) :
            application_qc_named(engine, "ClientDisconnect", actor, error);
        if (ok && qa_actor_id_equal(client->actor, actor)) client->spawned = false;
        return ok;
    }
    return true;
}
bool application_qc_actor_traits(application_provider *provider, qa_actor_id actor, qa_builtin_actor_traits *out)
{
    struct application_qc_state *engine = provider->state.qc.engine;
    if (engine == NULL || out == NULL) return false;
    qa_error ignored = {0}; int32_t reference; float flags, health, damage; int32_t name;
    const qa_qc_definition *classname = qa_qc_program_find_field(provider->state.qc.program, "classname");
    const char *text;
    if (classname == NULL || classname->type != QA_QC_STRING ||
        !application_qc_reference(engine, actor, &reference, &ignored) ||
        !application_qc_float(engine, reference, "flags", &flags, &ignored) ||
        !application_qc_float(engine, reference, "health", &health, &ignored) ||
        !application_qc_float(engine, reference, "takedamage", &damage, &ignored) ||
        !qa_qc_entity_int(provider->state.qc.instance, reference, classname->offset, &name, &ignored) ||
        !qa_qc_string(provider->state.qc.instance, name, &text, &ignored)) return false;
    if (!isfinite(flags) || (double)flags < INT32_MIN || (double)flags > INT32_MAX) return false;
    uint32_t bits = (uint32_t)(int32_t)flags;
    qa_builtin_actor_traits value = {.monster = (bits & 32u) != 0, .no_target = (bits & 128u) != 0,
        .grounded = (bits & 512u) != 0, .aimed_damage = damage == 2, .damageable_target = damage != 0,
        .max_health = health, .view_height = 22};
    for (uint32_t i = 1; i <= engine->max_clients; ++i)
        if (engine->clients[i].connected && qa_actor_id_equal(engine->clients[i].actor, actor)) { value.player = true; break; }
    if (!qa_builtin_resource(&engine->services, text, &value.classname, &ignored)) return false;
    *out = value; return true;
}
