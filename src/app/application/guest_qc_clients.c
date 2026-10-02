#include "guest_qc_profile.h"
#include "qa/application_network_qw.h"
#include "guest_qc_rerelease.h"
#include <stdio.h>

static bool classic_qw(const struct application_qc_state *engine)
{
    return engine && !engine->provider->state.qc.qualified && engine->profile == QA_QC_QUAKEWORLD &&
        engine->max_clients == 32 && engine->provider->state.qc.instance;
}
static bool source_binding(struct application_qc_state *engine, uint32_t slot,
    qa_actor_id actor, qa_error *error)
{
    qa_qc_slot_binding binding;
    const qa_actor_record *record = qa_actors_get(qa_session_actors(engine->services.session), actor);
    if (!record || !qa_qc_slot(engine->provider->state.qc.instance, slot, &binding) ||
        !qa_actor_id_equal(binding.actor, actor) || binding.owner != record->owner ||
        binding.source_slot != (record->has_source ? record->source_slot : 0) ||
        (binding.kind != QA_QC_SLOT_BORROWED && (binding.kind != QA_QC_SLOT_OWNED ||
         !classic_qw(engine) || record->owner != engine->provider->owner ||
         !record->has_source || record->source_slot != slot)))
        return application_fail(error, QA_ERROR_FORMAT, "QC client differs from its actual physical source binding");
    return true;
}
bool application_qc_control_source_client(const application_provider *provider,
    qa_actor_id actor, bool *member, qa_error *error)
{
    struct application_qc_state *engine = provider && provider->kind == APPLICATION_PROVIDER_QC ?
        provider->state.qc.engine : NULL;
    if (!engine || !provider->state.qc.instance || !engine->clients || !member)
        return application_fail(error, QA_ERROR_ARGUMENT, "QC source membership requires its actual imported client owner");
    *member = false;
    if (!qa_actors_get(qa_session_actors(engine->services.session), actor)) return true;
    for (uint32_t slot = 1; slot <= engine->max_clients; ++slot) {
        const application_qc_client *client = &engine->clients[slot];
        if (!client->connected || !client->spawned || !qa_actor_id_equal(client->actor, actor)) continue;
        if (!source_binding(engine, slot, actor, error)) return false;
        *member = true; return true;
    }
    return true;
}
bool application_qc_player_source_actor(application_provider *provider, uint32_t slot,
    qa_actor_id *out, qa_error *error)
{
    struct application_qc_state *engine = provider && provider->kind == APPLICATION_PROVIDER_QC ?
        provider->state.qc.engine : NULL;
    if (!engine || !out || !slot)
        return application_fail(error, QA_ERROR_ARGUMENT, "QC player source actor requires its actual source owner");
    *out = (qa_actor_id){0};
    if (!classic_qw(engine)) return true;
    qa_qc_slot_binding binding;
    if (slot > engine->max_clients || !qa_qc_slot(provider->state.qc.instance, slot, &binding) ||
        binding.kind != QA_QC_SLOT_OWNED || !source_binding(engine, slot, binding.actor, error))
        return application_fail(error, QA_ERROR_FORMAT, "QC player has no retained reserved source actor");
    *out = binding.actor; return true;
}
bool application_qc_source_clients_initialize(struct application_qc_state *engine, qa_error *error)
{
    if (!classic_qw(engine)) return true;
    const qa_actor_registry *actors = qa_session_actors(engine->services.session);
    qa_actor_definition definition;
    if (!qa_strings_intern_cstr(qa_session_strings(engine->services.session),
        "quakec:reserved-client", &definition, error)) return false;
    for (uint32_t slot = 1; slot <= engine->max_clients; ++slot) {
        qa_qc_slot_binding binding;
        if (!qa_qc_slot(engine->provider->state.qc.instance, slot, &binding))
            return application_fail(error, QA_ERROR_FORMAT, "QC reserved physical source row is absent");
        if (binding.kind != QA_QC_SLOT_FREE) {
            if (!source_binding(engine, slot, binding.actor, error)) return false;
            continue;
        }
        const qa_actor_record *existing = qa_actors_at_source(actors, engine->provider->owner, slot);
        qa_actor_id actor;
        if (existing) actor = existing->id;
        else {
            if (engine->provider->application->operation == APPLICATION_PERSISTING) continue;
            if (!qa_session_allocate(engine->services.session, engine->provider->owner,
                definition, true, slot, &actor, error)) return false;
        }
        if (!qa_qc_bind_actor(engine->provider->state.qc.instance, slot, actor, QA_QC_SLOT_OWNED, error)) {
            if (!existing) (void)qa_session_release(engine->services.session, actor, NULL);
            return false;
        }
    }
    return true;
}
bool application_qc_source_client_released(struct application_qc_state *engine,
    qa_actor_record released, qa_error *error)
{
    if (!classic_qw(engine) || engine->loading || !engine->initialized ||
        !qa_qc_reserved_actor_rebind_ready(engine->provider->state.qc.instance) ||
        engine->provider->application->operation == APPLICATION_PERSISTING ||
        !qa_session_actor_allocation_ready(engine->services.session, engine->provider->owner)) return true;
    for (uint32_t slot = 1; slot <= engine->max_clients; ++slot) {
        qa_qc_slot_binding binding;
        if (!qa_qc_slot(engine->provider->state.qc.instance, slot, &binding) ||
            (binding.kind != QA_QC_SLOT_OWNED && binding.kind != QA_QC_SLOT_BORROWED) ||
            !qa_actor_id_equal(binding.actor, released.id)) continue;
        qa_actor_definition definition; qa_actor_id actor;
        if (!qa_strings_intern_cstr(qa_session_strings(engine->services.session),
            "quakec:reserved-client", &definition, error) ||
            !qa_session_allocate(engine->services.session, engine->provider->owner,
                definition, true, slot, &actor, error)) return false;
        if (!qa_qc_rebind_reserved_actor(engine->provider->state.qc.instance, slot,
            released.id, actor, QA_QC_SLOT_OWNED, error)) {
            (void)qa_session_release(engine->services.session, actor, NULL);
            return false;
        }
        break;
    }
    return true;
}

static bool qw_name_value(struct application_qc_state *engine, uint32_t slot,
    const char *name, int32_t *out, qa_error *error)
{
    char key[32], text[32];
    snprintf(key, sizeof(key), "qw-name:%u", slot);
    size_t length = strlen(name);
    if (length >= sizeof(text)) length = sizeof(text) - 1;
    memcpy(text, name, length); text[length] = 0;
    return qa_qc_engine_string(engine->provider->state.qc.instance, key, text, sizeof(text), out, error);
}
static bool qw_name(struct application_qc_state *engine, uint32_t slot,
    int32_t reference, const char *name, qa_error *error)
{
    const qa_qc_definition *field = application_qc_field(engine, "netname", QA_QC_STRING, error);
    int32_t string;
    return field && qw_name_value(engine, slot, name, &string, error) &&
        qa_qc_set_entity_int(engine->provider->state.qc.instance, reference, field->offset, string, error);
}

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
    if (!ok || !application_qc_publish_client_outputs(engine, error)) return false;
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
static bool bind_player(application_provider *provider, uint32_t slot, uint32_t seat,
                                 qa_actor_id actor, const char *name, bool spectator,
                                 bool new_player, bool primary_character, bool reserve, qa_error *error)
{
    struct application_qc_state *engine = provider->state.qc.engine;
    const struct application_qc_profile *qualified = provider->state.qc.qualified;
    if (engine == NULL || slot == 0 || (!qualified && slot > engine->max_clients) || name == NULL ||
        qa_actors_get(qa_session_actors(engine->services.session), actor) == NULL)
        return application_fail(error, QA_ERROR_ARGUMENT, "Invalid QuakeC client admission");
    if (reserve && qualified)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "QC component requires its own deferred source admission contract");
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
    if (classic_qw(engine)) {
        qa_qc_slot_binding binding;
        if (!qa_qc_slot(provider->state.qc.instance, slot, &binding))
            return application_fail(error, QA_ERROR_FORMAT, "QW client physical source row is absent");
        if (qa_actor_id_equal(binding.actor, actor)) {
            if (!source_binding(engine, slot, actor, error)) return false;
        } else {
            if (client->connected || binding.kind != QA_QC_SLOT_OWNED ||
                !source_binding(engine, slot, binding.actor, error))
                return application_fail(error, QA_ERROR_FORMAT, "QW client has no disconnected source reservation");
            qa_actor_id previous = binding.actor;
            if (!qa_qc_rebind_reserved_actor(provider->state.qc.instance, slot, previous,
                actor, QA_QC_SLOT_BORROWED, error) ||
                !qa_session_release(engine->services.session, previous, error)) return false;
        }
    } else if (!qa_qc_game_bind_client(provider->state.qc.game, slot, actor, error)) return false;
    if (!client->connected || !qa_actor_id_equal(client->actor, actor)) {
        client->receipt_seen = false; client->receipt_sequence = client->receipt_ordinal = 0;
    }
    if (!client->connected && new_player) client->colors = 0;
    client->actor = actor; client->seat = seat; client->connected = true; client->spectator = spectator;
    client->primary_character = primary_character;
    if (qualified) {
        application_qc_inputs inputs = {.self = actor, .time_ns = engine->source_time_ns};
        if (!application_qc_seed_fields(engine, actor, error) || !application_qc_prepare_markers(engine, error)) return false;
        client->spawned = true;
        if (!application_qc_run_calls(engine, &qualified->admit, &inputs, error)) return false;
        if (!qa_actors_get(qa_session_actors(engine->services.session), actor))
            return application_fail(error, QA_ERROR_NOT_FOUND, "QC component removed its client during admission");
        return application_qc_admit_client_outputs(engine, slot, error);
    }
    qa_qc_instance *vm = provider->state.qc.instance;
    int32_t reference;
    const qa_qc_definition *netname = application_qc_field(engine, "netname", QA_QC_STRING, error);
    int32_t string;
    if (netname == NULL || !application_qc_reference(engine, actor, &reference, error)) return false;
    if (engine->profile == QA_QC_QUAKEWORLD) {
        if (!qw_name(engine, slot, reference, name, error)) return false;
    } else if (!qa_qc_string_allocate(vm, name, &string, error) ||
        !qa_qc_set_entity_int(vm, reference, netname->offset, string, error)) return false;
    if ((new_player || !client->has_parms) &&
        (!application_qc_named(engine, "SetNewParms", (qa_actor_id){0}, error) || !parms(engine, client, true, error))) return false;
    client->has_parms = true;
    if (reserve) return true;
    if (classic_qw(engine))
        return application_qc_prepare_player(provider, actor, error) &&
            application_qc_begin_player(provider, actor, error);
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
bool application_qc_bind_player(application_provider *provider, uint32_t slot, uint32_t seat,
    qa_actor_id actor, const char *name, bool spectator, bool new_player, bool primary_character, qa_error *error)
{ return bind_player(provider, slot, seat, actor, name, spectator, new_player, primary_character, false, error); }
bool application_qc_reserve_player(application_provider *provider, uint32_t slot, uint32_t seat,
    qa_actor_id actor, const char *name, bool spectator, bool new_player, bool primary_character, qa_error *error)
{ return bind_player(provider, slot, seat, actor, name, spectator, new_player, primary_character, true, error); }
bool application_qc_prepare_player(application_provider *provider, qa_actor_id actor, qa_error *error)
{
    struct application_qc_state *engine = provider && provider->kind == APPLICATION_PROVIDER_QC ?
        provider->state.qc.engine : NULL;
    if (!classic_qw(engine) || !qa_qc_idle(provider->state.qc.instance) || !application_qc_input_idle(provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "QW source Prepare requires its idle actual client owner");
    for (uint32_t slot = 1; slot <= engine->max_clients; ++slot) {
        application_qc_client *client = &engine->clients[slot];
        if (!client->connected || !qa_actor_id_equal(client->actor, actor)) continue;
        if (client->spawned || !client->has_parms || !source_binding(engine, slot, actor, error))
            return application_fail(error, QA_ERROR_ARGUMENT, "QW source Prepare requires an inactive retained client");
        if (client->prepared) return true;
        const char *raw; qa_qw_info info = {0};
        if (!qa_application_network_qw_userinfo_read(provider->application, actor, &raw, error) ||
            !qa_qw_info_parse(raw, &info, error)) return false;
        const char *name = qa_qw_info_get(&info, "name");
        qa_qc_instance *vm = provider->state.qc.instance;
        int32_t reference, name_string;
        qa_qc_program_info program = qa_qc_program_describe(provider->state.qc.program);
        const qa_qc_definition *colormap = application_qc_field(engine, "colormap", QA_QC_FLOAT, error);
        const qa_qc_definition *team = application_qc_field(engine, "team", QA_QC_FLOAT, error);
        const qa_qc_definition *netname = application_qc_field(engine, "netname", QA_QC_STRING, error);
        const qa_cvar_view *maximum = qa_cvars_find(engine->cvars, "sv_maxspeed");
        bool ok = colormap && team && netname && qa_qc_slot_reference(vm, slot, &reference, error);
        if (ok && (!maximum || !isfinite(maximum->number)))
            ok = application_fail(error, QA_ERROR_FORMAT, "QW source Prepare has no finite source speed cvar");
        const char *fields[] = {"gravity", "maxspeed"};
        float values[] = {1, maximum ? maximum->number : 0};
        const qa_qc_definition *optional[2];
        for (size_t i = 0; ok && i < sizeof(fields) / sizeof(*fields); ++i) {
            optional[i] = qa_qc_program_find_field(provider->state.qc.program, fields[i]);
            if (optional[i] && optional[i]->type != QA_QC_FLOAT)
                ok = application_fail(error, QA_ERROR_FORMAT, "QW source Prepare optional movement field is not a float");
        }
        if (ok) ok = qw_name_value(engine, slot, name ? name : "unnamed", &name_string, error);
        qa_qw_info_free(&info);
        for (uint32_t word = 0; ok && word < program.entity_field_words; ++word)
            ok = qa_qc_project_entity_int(vm, reference, word, 0, error);
        ok = ok && qa_qc_project_entity_float(vm, reference, colormap->offset, (float)slot, error) &&
            qa_qc_project_entity_float(vm, reference, team->offset, 0, error) &&
            qa_qc_project_entity_int(vm, reference, netname->offset, name_string, error);
        for (size_t i = 0; ok && i < sizeof(fields) / sizeof(*fields); ++i)
            if (optional[i]) ok = qa_qc_project_entity_float(vm, reference, optional[i]->offset, values[i], error);
        if (!ok) return false;
        const qa_think *pending = qa_scheduler_pending(qa_session_scheduler(engine->services.session), actor);
        if (pending && pending->execution_provider == provider->owner)
            qa_scheduler_cancel(qa_session_scheduler(engine->services.session), actor);
        client->prepared = true;
        return true;
    }
    return application_fail(error, QA_ERROR_NOT_FOUND, "QW source Prepare has no connected physical client");
}
bool application_qc_begin_player(application_provider *provider, qa_actor_id actor, qa_error *error)
{
    struct application_qc_state *engine = provider && provider->kind == APPLICATION_PROVIDER_QC ? provider->state.qc.engine : NULL;
    if (!engine || provider->state.qc.qualified ||
        !qa_qc_idle(provider->state.qc.instance) || !application_qc_input_idle(provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "QC source begin requires its idle reserved classic client");
    for (uint32_t slot = 1; slot <= engine->max_clients; ++slot) {
        application_qc_client *client = &engine->clients[slot];
        if (!client->connected || !qa_actor_id_equal(client->actor, actor)) continue;
        if (client->spawned) return true;
        if (!client->has_parms || !source_binding(engine, slot, actor, error) ||
            (classic_qw(engine) && !client->prepared))
            return application_fail(error, QA_ERROR_ARGUMENT, "QC source begin lacks its retained actor or spawn parameters");
        int32_t reference;
        if (!application_qc_reference(engine, actor, &reference, error)) return false;
        if (!classic_qw(engine) && (!application_qc_set_float(engine, reference, "colormap", (float)slot, error) ||
            !application_qc_set_float(engine, reference, "team", (float)((client->colors & 15u) + 1u), error))) return false;
        if (!parms(engine, client, false, error) || !(client->spectator ?
            application_qc_spectator_callback(engine, "SpectatorConnect", actor, error) :
            application_qc_named(engine, "ClientConnect", actor, error))) return false;
        if (!client->spectator && !application_qc_named(engine, "PutClientInServer", actor, error)) return false;
        if (!qa_actors_get(qa_session_actors(engine->services.session), actor))
            return application_fail(error, QA_ERROR_NOT_FOUND, "QC source removed its reserved client during begin");
        client->spawned = true;
        qa_body_state body; application_control_record *control;
        return qa_world_body_read(engine->world, actor, &body, error) &&
            application_control_ensure(provider->application, actor, body.angles, &control, error);
    }
    return application_fail(error, QA_ERROR_NOT_FOUND, "QC source begin has no reserved connected client");
}
bool application_qc_client_colors(application_provider *provider, qa_actor_id actor,
    int32_t top, int32_t bottom, qa_error *error)
{
    struct application_qc_state *engine = provider && provider->kind == APPLICATION_PROVIDER_QC ? provider->state.qc.engine : NULL;
    if (!engine || provider->state.qc.qualified || !qa_qc_idle(provider->state.qc.instance) ||
        !application_qc_input_idle(provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "QC client colors require an idle classic source client");
    uint32_t shirt = (uint32_t)top & 15u, pants = (uint32_t)bottom & 15u;
    if (shirt > 13) shirt = 13;
    if (pants > 13) pants = 13;
    for (uint32_t slot = 1; slot <= engine->max_clients; ++slot) {
        application_qc_client *client = &engine->clients[slot];
        if (!client->connected || !qa_actor_id_equal(client->actor, actor)) continue;
        int32_t reference;
        if (!source_binding(engine, slot, actor, error) || !application_qc_reference(engine, actor, &reference, error))
            return application_fail(error, QA_ERROR_ARGUMENT, "QC colors lack the connected physical client binding");
        if (!application_qc_set_float(engine, reference, "team", (float)(pants + 1u), error)) return false;
        client->colors = (uint8_t)((shirt << 4) | pants);
        return true;
    }
    return application_fail(error, QA_ERROR_NOT_FOUND, "QC colors have no connected source client");
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
bool application_qc_player_receive(application_provider *provider, qa_actor_id actor,
                                    uint64_t ordinal, qa_movement_command *command, qa_error *error)
{
    struct application_qc_state *engine = provider && provider->kind == APPLICATION_PROVIDER_QC ?
        provider->state.qc.engine : NULL;
    if (!engine || !command)
        return application_fail(error, QA_ERROR_ARGUMENT, "QC receipt needs its actual source and mutable command");
    if (engine->profile != QA_QC_RERELEASE) return true;
    for (uint32_t slot = 1; slot <= engine->max_clients; ++slot) {
        application_qc_client *client = &engine->clients[slot];
        if (!client->connected || !client->spawned || !qa_actor_id_equal(client->actor, actor)) continue;
        if (!source_binding(engine, slot, actor, error)) return false;
        if (client->receipt_seen && (command->sequence < client->receipt_sequence ||
            (command->sequence == client->receipt_sequence && ordinal <= client->receipt_ordinal)))
            return application_fail(error, QA_ERROR_ARGUMENT, "QC receipt does not advance its physical client sequence");
        int32_t reference;
        if (!application_qc_reference(engine, actor, &reference, error)) return false;
        bool source_impulse = command->kind != QA_MOVEMENT_Q3 && command->kind != QA_MOVEMENT_Q2_RERELEASE;
        if (source_impulse && command->impulse) {
            application_qc_weapon_command(engine,actor);
            if(!application_qc_set_float(engine, reference, "impulse", command->impulse, error)) return false;
        }
        if (!application_qc_rerelease_command(provider, actor, command, error)) return false;
        client->receipt_seen = true; client->receipt_sequence = command->sequence; client->receipt_ordinal = ordinal;
        return true;
    }
    return application_fail(error, QA_ERROR_NOT_FOUND, "QC receipt has no admitted physical source client");
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
        !application_qc_set_float(engine, reference, "button0", (command->buttons & 1u) ? 1.0f : 0.0f, error) ||
        !application_qc_set_float(engine, reference, "button2", (command->buttons & 2u) ? 1.0f : 0.0f, error)) return false;
    bool received = false;
    for (uint32_t slot = 1; slot <= engine->max_clients; ++slot) {
        const application_qc_client *client = &engine->clients[slot];
        received |= engine->profile == QA_QC_RERELEASE && client->connected && client->spawned &&
            qa_actor_id_equal(client->actor, actor) && client->receipt_seen &&
            command->sequence <= client->receipt_sequence;
    }
    if (!received && command->impulse != 0) {
        application_qc_weapon_command(engine,actor);
        if(!application_qc_set_float(engine, reference, "impulse", command->impulse, error)) return false;
    }
    return true;
}
bool application_qc_client_userinfo(application_provider *provider, qa_actor_id actor, qa_error *error)
{
    struct application_qc_state *engine = provider->state.qc.engine;
    if (!engine) return application_fail(error, QA_ERROR_ARGUMENT, "QC userinfo has no engine owner");
    for (uint32_t i = 1; i <= engine->max_clients; ++i) {
        application_qc_client *client = &engine->clients[i];
        if (!client->connected || !qa_actor_id_equal(client->actor, actor)) continue;
        const struct application_qc_profile *profile = provider->state.qc.qualified;
        application_qc_inputs inputs = {.self = actor, .time_ns = engine->source_time_ns};
        if (profile) return client->spawned ? application_qc_run_calls(engine, &profile->userinfo, &inputs, error) :
            application_fail(error, QA_ERROR_NOT_FOUND, "QC userinfo client has not completed declared admission");
        if (engine->profile != QA_QC_QUAKEWORLD) {
            if (client->spawned) return true;
            continue;
        }
        const char *raw; qa_qw_info info = {0}; int32_t reference;
        if (!qa_application_network_qw_userinfo_read(provider->application, actor, &raw, error) ||
            !qa_qw_info_parse(raw, &info, error)) return false;
        const char *name = qa_qw_info_get(&info, "name");
        bool ok = application_qc_reference(engine, actor, &reference, error) &&
            qw_name(engine, i, reference, name ? name : "unnamed", error);
        qa_qw_info_free(&info);
        return ok;
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
        if (ok && qa_actor_id_equal(client->actor, actor)) {
            client->spawned = false;
            client->prepared = false;
            client->output_published = false;
            client->outputs = (application_client_outputs){0};
            client->receipt_seen = false; client->receipt_sequence = client->receipt_ordinal = 0;
            client->pending_weapon=0; client->pending_weapon_following=false;
        }
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
