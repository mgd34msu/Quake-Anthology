#include "internal.h"
#include "map_players_private.h"
#include "guest_qc_internal.h"
#include "network_q1_signon.h"
#include "control_frame.h"
#include "qa/application_network_qw.h"
#include "qa/application_network.h"

bool qa_application_network_qw_userinfo_read(qa_application *app,
    qa_actor_id actor, const char **out, qa_error *error)
{
    if (!app || !out || !app->session || !app->players ||
        !qa_actors_get(qa_session_actors(app->session), actor))
        return application_fail(error, QA_ERROR_NOT_FOUND,
            "QuakeWorld userinfo requires its live canonical player generation");
    for (size_t i = 0; i < app->players->count; ++i) {
        const application_player_record *record = app->players->records + i;
        if (!qa_actor_id_equal(record->actor, actor)) continue;
        if (!record->userinfo)
            return application_fail(error, QA_ERROR_NOT_FOUND,
                "QuakeWorld canonical player has no retained source userinfo owner");
        *out = record->userinfo; return true;
    }
    return application_fail(error, QA_ERROR_NOT_FOUND,
        "QuakeWorld userinfo actor is absent from its canonical player roster");
}

static struct application_qc_state *qw_source(qa_application *app,
    qa_application_network_qw_source *out, qa_error *error)
{
    application_provider *provider = app ? application_world_provider(app, QA_ROLE_ENTITIES, "") : NULL;
    struct application_qc_state *engine = provider && provider->kind == APPLICATION_PROVIDER_QC ?
        provider->state.qc.engine : NULL;
    qa_clock_state clock;
    qa_collision_geometry *geometry = app ? qa_world_geometry(app->world) : NULL;
    uint32_t count = engine ? qa_qc_entity_count(provider->state.qc.instance) : 0;
    if (!app || app->destroy_requested || app->state != QA_APPLICATION_RUNNING ||
        !engine || !provider->constructed || !provider->attached || provider->close_pending ||
        provider->state.qc.qualified || !engine->initialized || engine->loading || engine->projecting ||
        engine->profile != QA_QC_QUAKEWORLD || engine->protocol.kind != QA_NET_QW28 ||
        engine->protocol.flags || engine->protocol.revision || engine->max_clients != 32 ||
        !engine->clients || !engine->cvars || count < 33 || count > 512 || !geometry ||
        qa_collision_geometry_family(geometry) != QA_COLLISION_Q1 ||
        !qa_session_safe(app->session) || qa_session_faulted(app->session) ||
        !qa_world_idle(app->world) || !qa_qc_idle(provider->state.qc.instance) ||
        !application_qc_input_idle(provider) ||
        !qa_session_clock(app->session, provider->owner, &clock) ||
        clock.frame.provider != provider->owner || clock.frame.kind != QA_CLOCK_QUAKEWORLD ||
        clock.frame.phase != QA_FRAME_EXIT) {
        application_fail(error, QA_ERROR_UNSUPPORTED,
            "QuakeWorld wire requires its completed classic QC source and 32 physical client rows");
        return NULL;
    }
    if (out) *out = (qa_application_network_qw_source){.owner = provider->owner,
        .entity_count = count, .source_time_ns = engine->source_time_ns,
        .completed_time_ns = clock.frame.time_ns, .cvars = engine->cvars};
    return engine;
}

bool qa_application_network_qw_source_read(qa_application *app,
    qa_application_network_qw_source *out, qa_error *error)
{
    if (!out) return application_fail(error, QA_ERROR_ARGUMENT, "Missing QuakeWorld physical source output");
    return qw_source(app, out, error) != NULL;
}

static bool qw_scalar(struct application_qc_state *engine, int32_t reference,
    const char *name, float *out, qa_error *error)
{
    return application_qc_float(engine, reference, name, out, error) &&
        (isfinite(*out) || application_fail(error, QA_ERROR_FORMAT, "Nonfinite QuakeWorld source scalar"));
}

static bool qw_truncated(struct application_qc_state *engine, int32_t reference,
    const char *name, double *out, qa_error *error)
{
    float value;
    if (!qw_scalar(engine, reference, name, &value, error)) return false;
    *out = trunc((double)value);
    return true;
}

static bool qw_vector(struct application_qc_state *engine, int32_t reference,
    const char *name, float out[3], qa_error *error)
{
    const qa_qc_definition *field = application_qc_field(engine, name, QA_QC_VECTOR, error);
    qa_vec3 value;
    if (!field || !qa_qc_entity_vector(engine->provider->state.qc.instance,
            reference, field->offset, &value, error)) return false;
    if (!qa_vec_finite(value))
        return application_fail(error, QA_ERROR_FORMAT, "Nonfinite QuakeWorld source vector");
    out[0] = value.x; out[1] = value.y; out[2] = value.z;
    return true;
}

static bool qw_string(struct application_qc_state *engine, int32_t reference,
    const char *name, const char **out, qa_error *error)
{
    const qa_qc_definition *field = application_qc_field(engine, name, QA_QC_STRING, error);
    int32_t string;
    if (!field || !qa_qc_entity_int(engine->provider->state.qc.instance,
            reference, field->offset, &string, error)) return false;
    return qa_qc_string(engine->provider->state.qc.instance, string, out, error);
}

static bool qw_entity(struct application_qc_state *engine, uint32_t slot,
    int32_t reference, qa_application_network_qw_entity *out, qa_error *error)
{
    qa_application_network_qw_entity value = {.number = slot};
    if (!qw_truncated(engine, reference, "modelindex", &value.model, error) ||
        !qw_truncated(engine, reference, "frame", &value.frame, error) ||
        !qw_truncated(engine, reference, "colormap", &value.colormap, error) ||
        !qw_truncated(engine, reference, "skin", &value.skin, error) ||
        !qw_truncated(engine, reference, "effects", &value.effects, error) ||
        !qw_vector(engine, reference, "origin", value.origin, error) ||
        !qw_vector(engine, reference, "angles", value.angles, error)) return false;
    *out = value;
    return true;
}

static int32_t qw_integer(float value)
{
    double integer = fmod(trunc((double)value), 4294967296.0);
    if (integer < 0) integer += 4294967296.0;
    uint32_t word = (uint32_t)integer;
    int32_t result;
    memcpy(&result, &word, sizeof(result));
    return result;
}

static bool qw_global(struct application_qc_state *engine, const char *name,
    float *out, qa_error *error)
{
    const qa_qc_definition *global = qa_qc_program_find_global(engine->provider->state.qc.program, name);
    if (!global || global->type != QA_QC_FLOAT)
        return application_fail(error, QA_ERROR_FORMAT, "QuakeWorld source stat global is absent");
    return qa_qc_global_float(engine->provider->state.qc.instance, global->offset, out, error) &&
        (isfinite(*out) || application_fail(error, QA_ERROR_FORMAT, "Nonfinite QuakeWorld source stat global"));
}

bool qa_application_network_qw_world_read(qa_application *app,
    qa_application_network_qw_world *out, qa_error *error)
{
    if (!out) return application_fail(error, QA_ERROR_ARGUMENT, "Missing QuakeWorld source world output");
    qa_application_network_qw_world value = {0};
    struct application_qc_state *engine = qw_source(app, &value.source, error);
    if (!engine) return false;
    const qa_product *product = engine->provider->product;
    qa_application_map_view map;
    const qa_qc_definition *mapname = qa_qc_program_find_global(engine->provider->state.qc.program, "mapname");
    int32_t name, reference;
    if (!product || !product->directory || !*product->directory ||
        !qa_application_map_read(app, &map) || !map.resource ||
        !mapname || mapname->type != QA_QC_STRING)
        return application_fail(error, QA_ERROR_FORMAT, "QuakeWorld source world identity is incomplete");
    if (!qa_qc_global_int(engine->provider->state.qc.instance, mapname->offset, &name, error) ||
        !qa_qc_string(engine->provider->state.qc.instance, name, &value.map, error) ||
        !qa_qc_slot_reference(engine->provider->state.qc.instance, 0, &reference, error) ||
        !qw_string(engine, reference, "message", &value.level, error)) return false;
    const char *separator = strrchr(product->directory, '/');
    value.game_directory = separator ? separator + 1 : product->directory;
    value.map_bytes = qa_resource_bytes(map.resource);
    if (!*value.game_directory || !value.map_bytes.data || !value.map_bytes.size)
        return application_fail(error, QA_ERROR_FORMAT, "QuakeWorld source world lacks its admitted map or product directory");
    value.protocol = engine->protocol;
    value.max_clients = engine->max_clients;
    static const char *const names[] = {"sv_gravity", "sv_stopspeed", "sv_maxspeed", "sv_spectatormaxspeed",
        "sv_accelerate", "sv_airaccelerate", "sv_wateraccelerate", "sv_friction", "sv_waterfriction"};
    float *const values[] = {&value.movement.gravity, &value.movement.stop_speed, &value.movement.max_speed,
        &value.movement.spectator_max_speed, &value.movement.accelerate, &value.movement.air_accelerate,
        &value.movement.water_accelerate, &value.movement.friction, &value.movement.water_friction};
    for (size_t i = 0; i < sizeof(names) / sizeof(*names); ++i) {
        const qa_cvar_view *variable = qa_cvars_find(engine->cvars, names[i]);
        if (!variable || !isfinite(variable->number))
            return application_fail(error, QA_ERROR_FORMAT, "QuakeWorld source move cvar is absent or nonfinite");
        *values[i] = variable->number;
    }
    value.movement.entity_gravity = 1;
    for (size_t i = 0; i < 64; ++i) value.lightstyles[i] = engine->lightstyles[i] ? engine->lightstyles[i] : "";
    *out = value;
    return true;
}

bool qa_application_network_qw_signon_count(qa_application *app, size_t *out, qa_error *error)
{
    if (!out) return application_fail(error, QA_ERROR_ARGUMENT, "Missing QuakeWorld source signon count");
    struct application_qc_state *engine = qw_source(app, NULL, error);
    if (!engine) return false;
    *out = application_q1_signon_count(app, engine->provider->owner);
    return true;
}

bool qa_application_network_qw_signon_at(qa_application *app, size_t index,
    qa_application_protocol_event *out, qa_error *error)
{
    struct application_qc_state *engine = qw_source(app, NULL, error);
    return engine && application_q1_signon_at(app, engine->provider->owner, index, out, error);
}

static bool qw_client_binding(struct application_qc_state *engine, qa_actor_id actor,
    uint32_t *out, qa_error *error)
{
    uint32_t slot = 0;
    for (uint32_t i = 1; i <= engine->max_clients; ++i)
        if (engine->clients[i].connected && qa_actor_id_equal(engine->clients[i].actor, actor)) {
            slot = i; break;
        }
    const qa_actor_record *record = qa_actors_get(qa_session_actors(engine->services.session), actor);
    qa_qc_slot_binding binding;
    if (!slot || !record || !qa_qc_slot(engine->provider->state.qc.instance, slot, &binding) ||
        !qa_actor_id_equal(binding.actor, actor) || binding.owner != record->owner ||
        binding.source_slot != (record->has_source ? record->source_slot : 0) ||
        (binding.kind != QA_QC_SLOT_BORROWED && (binding.kind != QA_QC_SLOT_OWNED ||
         record->owner != engine->provider->owner || !record->has_source || record->source_slot != slot)))
        return application_fail(error, QA_ERROR_ARGUMENT, "QuakeWorld client has no genuine physical source row");
    *out = slot;
    return true;
}

bool qa_application_network_qw_prepare(qa_application *app, qa_actor_id actor, qa_error *error)
{
    struct application_qc_state *engine = qw_source(app, NULL, error);
    if (!engine) return false;
    uint32_t slot;
    if (app->operation != APPLICATION_IDLE || !qw_client_binding(engine, actor, &slot, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "QuakeWorld Prepare requires its idle actual client admission");
    const application_qc_client *client = engine->clients + slot;
    if (client->spawned || !client->has_parms)
        return application_fail(error, QA_ERROR_ARGUMENT, "QuakeWorld Prepare requires an inactive reserved source client");
    bool ok = application_qc_prepare_player(engine->provider, actor, error);
    if (!ok) application_fault(app, error);
    return ok;
}

bool qa_application_network_qw_commands(qa_application *app,
    const qa_network_command_group *group, qa_error *error)
{
    if (!group || !group->commands || !group->count || group->count > 20)
        return application_fail(error, QA_ERROR_ARGUMENT, "Missing literal QuakeWorld source command group");
    if (!qa_application_network_controlled(app, group->client, group->seat,
            group->actor, group->movement, (qa_bytes){0}, error)) return false;
    struct application_qc_state *engine = qw_source(app, NULL, error);
    uint32_t slot;
    if (!engine || !qw_client_binding(engine, group->actor, &slot, error)) return false;
    if (!engine->clients[slot].spawned)
        return application_fail(error, QA_ERROR_ARGUMENT, "QuakeWorld command group requires genuine source Begin");
    for (size_t i = 0; i < group->count; ++i)
        if (group->commands[i].sequence != group->commands[0].sequence)
            return application_fail(error, QA_ERROR_ARGUMENT, "QuakeWorld raw group changes its source packet sequence");
    return qa_application_control_qw_commands(app, group->actor,
        group->commands, group->count, error);
}

bool qa_application_network_qw_client_read(qa_application *app, qa_actor_id actor,
    qa_application_network_qw_client *out, qa_error *error)
{
    if (!out) return application_fail(error, QA_ERROR_ARGUMENT, "Missing QuakeWorld source client output");
    struct application_qc_state *engine = qw_source(app, NULL, error);
    if (!engine) return false;
    uint32_t slot;
    if (!qw_client_binding(engine, actor, &slot, error)) return false;
    qa_qc_instance *vm = engine->provider->state.qc.instance;
    int32_t reference;
    qa_application_network_qw_client value = {.actor = actor, .source_slot = slot, .stat_mask = UINT16_C(0xfffd),
        .begun = engine->clients[slot].spawned, .spectator = engine->clients[slot].spectator};
    if (!qa_qc_slot_reference(vm, slot, &reference, error) ||
        !qw_entity(engine, slot, reference, &value.entity, error) ||
        !qw_vector(engine, reference, "velocity", value.velocity, error) ||
        !qw_vector(engine, reference, "view_ofs", value.view_offset, error) ||
        !qw_vector(engine, reference, "mins", value.minimum, error) ||
        !qw_scalar(engine, reference, "health", &value.health, error) ||
        !qw_scalar(engine, reference, "frags", &value.frags, error) ||
        !qw_truncated(engine, reference, "weaponframe", &value.weapon_frame, error)) return false;
    static const char *const fields[16] = {"health", NULL, NULL, "currentammo", "armorvalue",
        "weaponframe", "ammo_shells", "ammo_nails", "ammo_rockets", "ammo_cells", "weapon"};
    for (uint32_t index = 0; index < 11; ++index) {
        if (!fields[index]) continue;
        float raw;
        if (!qw_scalar(engine, reference, fields[index], &raw, error)) return false;
        value.stats[index] = trunc((double)raw);
    }
    const char *weapon;
    if (!qw_string(engine, reference, "weaponmodel", &weapon, error)) return false;
    for (size_t i = 0; i < engine->resource_count; ++i) {
        const application_qc_resource *resource = &engine->resources[i];
        if (resource->kind == QA_QC_RESOURCE_MODEL && resource->name && !strcmp(resource->name, weapon)) {
            if (!resource->value.index || resource->value.index > 255)
                return application_fail(error, QA_ERROR_FORMAT, "QuakeWorld weapon model exceeds its byte-indexed source precache");
            value.stats[2] = resource->value.index; break;
        }
    }
    static const char *const globals[] = {"total_secrets", "total_monsters", "found_secrets", "killed_monsters"};
    for (uint32_t i = 0; i < 4; ++i) {
        float raw;
        if (!qw_global(engine, globals[i], &raw, error)) return false;
        value.stats[11 + i] = trunc((double)raw);
    }
    float items, flags;
    if (!qw_scalar(engine, reference, "items", &items, error) ||
        !qw_global(engine, "serverflags", &flags, error)) return false;
    uint32_t item_bits = (uint32_t)qw_integer(items) | ((uint32_t)qw_integer(flags) << 28);
    int32_t signed_items;
    memcpy(&signed_items, &item_bits, sizeof(item_bits));
    value.stats[15] = signed_items;
    if (!application_control_last_qw_command(app, actor, &value.command,
            &value.command_time_ns, &value.command_present, error)) return false;
    *out = value;
    return true;
}

bool qa_application_network_qw_client_next(qa_application *app, uint32_t *cursor,
    bool *present, qa_application_network_qw_client *out, qa_error *error)
{
    if (!cursor || !present || !out)
        return application_fail(error, QA_ERROR_ARGUMENT, "Missing QuakeWorld physical client inventory output");
    *present = false;
    struct application_qc_state *engine = qw_source(app, NULL, error);
    if (!engine) return false;
    if (!*cursor) *cursor = 1;
    while (*cursor <= engine->max_clients) {
        uint32_t slot = (*cursor)++;
        if (!engine->clients[slot].connected) continue;
        if (!qa_application_network_qw_client_read(app, engine->clients[slot].actor, out, error)) return false;
        if (out->source_slot != slot)
            return application_fail(error, QA_ERROR_FORMAT, "QuakeWorld client inventory changes its physical slot");
        *present = true; return true;
    }
    return true;
}

bool qa_application_network_qw_visible(qa_application *app, qa_actor_id viewer,
    qa_actor_id target, bool *out, qa_error *error)
{
    if (!out) return application_fail(error, QA_ERROR_ARGUMENT, "Missing QuakeWorld source visibility output");
    struct application_qc_state *engine = qw_source(app, NULL, error);
    uint32_t slot;
    if (!engine || !qw_client_binding(engine, viewer, &slot, error) ||
        !qa_actors_get(qa_session_actors(app->session), target)) return false;
    if (qa_actor_id_equal(viewer, target)) { *out = true; return true; }
    qa_linked_body linked;
    if (!qa_world_linked(app->world, target, &linked)) { *out = false; return true; }
    int32_t reference;
    float origin[3], offset[3];
    if (!qa_qc_slot_reference(engine->provider->state.qc.instance, slot, &reference, error) ||
        !qw_vector(engine, reference, "origin", origin, error) ||
        !qw_vector(engine, reference, "view_ofs", offset, error)) return false;
    qa_vec3 eye = qa_v3(origin[0] + offset[0], origin[1] + offset[1], origin[2] + offset[2]);
    qa_collision_geometry *geometry = qa_world_geometry(app->world);
    if (!qa_vec_finite(eye))
        return application_fail(error, QA_ERROR_FORMAT, "QuakeWorld source eye exceeds finite spatial range");
    size_t size = qa_collision_q1_pvs_bytes(geometry);
    uint8_t *pvs = size ? malloc(size) : NULL;
    if (size && !pvs) return application_fail(error, QA_ERROR_MEMORY, "Observing QuakeWorld source fat PVS");
    bool ok = qa_collision_q1_fat_pvs(geometry, eye, pvs, size, error) &&
        qa_collision_q1_bounds_visible(geometry, (qa_bytes){pvs, size}, linked.absolute_bounds, out, error);
    free(pvs); return ok;
}

bool qa_application_network_qw_receives(qa_application *app, qa_actor_id actor,
    const qa_application_protocol_event *event, bool *out, qa_error *error)
{
    if (!event || !out)
        return application_fail(error, QA_ERROR_ARGUMENT, "Missing QuakeWorld source message routing output");
    struct application_qc_state *engine = qw_source(app, NULL, error);
    uint32_t slot;
    if (!engine || !qw_client_binding(engine, actor, &slot, error)) return false;
    *out = false;
    if (event->provider != engine->provider->owner || event->signon) return true;
    if (event->dialect != QA_CLOCK_QUAKEWORLD)
        return application_fail(error, QA_ERROR_FORMAT, "QuakeWorld source message changes its physical dialect");
    if (!event->multicast) {
        if (event->destination < 0 || event->destination > 3)
            return application_fail(error, QA_ERROR_FORMAT, "QuakeWorld source message has an invalid destination");
        *out = !event->recipient.registry || qa_actor_id_equal(event->recipient, actor);
        return true;
    }
    if (event->destination < 0 || event->destination > 5 || !qa_vec_finite(event->origin))
        return application_fail(error, QA_ERROR_FORMAT, "QuakeWorld source multicast has an invalid destination");
    int32_t mode = event->destination % 3;
    if (!mode) { *out = true; return true; }
    int32_t reference; float origin[3];
    if (!qa_qc_slot_reference(engine->provider->state.qc.instance, slot, &reference, error) ||
        !qw_vector(engine, reference, "origin", origin, error)) return false;
    qa_vec3 point = qa_v3(origin[0], origin[1], origin[2]);
    qa_vec3 delta = qa_vec_sub(point, event->origin);
    if (mode == 1 && qa_vec_dot(delta, delta) <= 1024.0f * 1024.0f) { *out = true; return true; }
    qa_collision_leaf from, to;
    qa_collision_geometry *geometry = qa_world_geometry(app->world);
    return qa_collision_point_leaf(geometry, event->origin, &from, error) &&
        qa_collision_point_leaf(geometry, point, &to, error) &&
        qa_collision_cluster_visible(geometry, (int32_t)from.cluster, (int32_t)to.cluster, mode == 1, out, error);
}

bool qa_application_network_qw_flush(qa_application *app, qa_error *error)
{
    struct application_qc_state *engine = qw_source(app, NULL, error);
    bool ok = engine && app->operation == APPLICATION_IDLE && application_qc_flush(engine, error);
    if (!ok && engine) application_fault(app, error);
    return ok;
}

qa_vfs *qa_application_network_qw_content(qa_application *app, qa_error *error)
{
    struct application_qc_state *engine = qw_source(app, NULL, error);
    if (!engine) return NULL;
    qa_vfs *content = engine->provider->launch ? engine->provider->launch->content : NULL;
    if (!content) application_fail(error, QA_ERROR_ARGUMENT, "QuakeWorld source has no admitted mounted content owner");
    return content;
}

bool qa_application_network_qw_kill(qa_application *app, qa_actor_id actor,
    bool *killed, qa_error *error)
{
    if (!killed) return application_fail(error, QA_ERROR_ARGUMENT, "Missing QuakeWorld ClientKill result");
    *killed = false;
    struct application_qc_state *engine = qw_source(app, NULL, error);
    uint32_t slot; int32_t reference; float health;
    if (!engine || app->operation != APPLICATION_IDLE || !qw_client_binding(engine, actor, &slot, error)) return false;
    if (!engine->clients[slot].spawned || engine->clients[slot].spectator) return true;
    if (!qa_qc_slot_reference(engine->provider->state.qc.instance, slot, &reference, error) ||
        !qw_scalar(engine, reference, "health", &health, error)) return false;
    if (health <= 0) return true;
    qa_qc_game_global globals[] = {
        {"self", {QA_QC_GAME_ACTOR, {.actor = actor}}},
        {"other", {QA_QC_GAME_ACTOR, {.actor = {0}}}},
        {"time", {QA_QC_GAME_FLOAT, {.number = (float)((double)engine->source_time_ns / 1e9)}}}
    };
    bool ok = qa_qc_game_call(engine->provider->state.qc.game, "ClientKill", NULL, 0,
        globals, sizeof(globals) / sizeof(*globals), NULL, error);
    if (!ok) application_fault(app, error); else *killed = true;
    return ok;
}

bool qa_application_network_qw_pause(qa_application *app, qa_actor_id actor,
    qa_buffer *out, bool *changed, qa_error *error)
{
    if (!out || out->data || out->size || !changed)
        return application_fail(error, QA_ERROR_ARGUMENT, "QuakeWorld pause requires empty announcement output");
    struct application_qc_state *engine = qw_source(app, NULL, error);
    uint32_t slot;
    if (!engine || app->operation != APPLICATION_IDLE || !qw_client_binding(engine, actor, &slot, error)) return false;
    const qa_cvar_view *policy = qa_cvars_find(engine->cvars, "pausable");
    const char *denial = policy && policy->number == 0 ? "Pause not allowed.\n" :
        engine->clients[slot].spectator ? "Spectators can not pause.\n" : NULL;
    int32_t reference; const char *name = NULL;
    if (!denial && (!qa_qc_slot_reference(engine->provider->state.qc.instance, slot, &reference, error) ||
        !qw_string(engine, reference, "netname", &name, error))) return false;
    const char *suffix = app->q1_paused ? " unpaused the game\n" : " paused the game\n";
    size_t name_size = denial ? 0 : strlen(name), suffix_size = denial ? 0 : strlen(suffix);
    if (name_size > 1395 || suffix_size > 1395 - name_size)
        return application_fail(error, QA_ERROR_FORMAT, "QuakeWorld pause announcement exceeds its source message extent");
    size_t size = denial ? strlen(denial) : name_size + suffix_size;
    if (size > 1395) return application_fail(error, QA_ERROR_FORMAT, "QuakeWorld pause announcement exceeds its source message extent");
    out->data = malloc(size + 1);
    if (!out->data) return application_fail(error, QA_ERROR_MEMORY, "Retaining QuakeWorld pause announcement");
    if (denial) memcpy(out->data, denial, size + 1);
    else { memcpy(out->data, name, name_size); memcpy(out->data + name_size, suffix, suffix_size + 1); }
    out->size = size; *changed = !denial;
    if (!denial) app->q1_paused = !app->q1_paused;
    return true;
}

bool qa_application_network_qw_userinfo(qa_application *app, qa_actor_id actor,
    const char *text, qa_error *error)
{
    struct application_qc_state *engine = qw_source(app, NULL, error);
    uint32_t slot;
    if (!engine || !text || app->operation != APPLICATION_IDLE ||
        !qw_client_binding(engine, actor, &slot, error)) return false;
    qa_qw_info info = {0};
    if (!qa_qw_info_parse(text, &info, error)) return false;
    const char *spectator = qa_qw_info_get(&info, "*spectator");
    if ((spectator && !strcmp(spectator, "1")) != engine->clients[slot].spectator) {
        qa_qw_info_free(&info);
        return application_fail(error, QA_ERROR_ARGUMENT, "QuakeWorld userinfo changes its trusted source role");
    }
    const char *name = qa_qw_info_get(&info, "name"), *team = qa_qw_info_get(&info, "team"),
        *skin = qa_qw_info_get(&info, "skin");
    const char *values[] = {text, name ? name : "unnamed", team ? team : "", skin ? skin : ""};
    char *copies[4] = {0}; bool ok = true;
    for (size_t i = 0; ok && i < 4; ++i) {
        size_t length = strlen(values[i]) + 1; copies[i] = malloc(length);
        if (copies[i]) memcpy(copies[i], values[i], length);
        else ok = application_fail(error, QA_ERROR_MEMORY, "Retaining QuakeWorld source userinfo");
    }
    application_player_record *record = NULL;
    for (size_t i = 0; app->players && i < app->players->count; ++i)
        if (qa_actor_id_equal(app->players->records[i].actor, actor) && !app->players->records[i].retiring)
            record = app->players->records + i;
    if (ok && !record) ok = application_fail(error, QA_ERROR_ARGUMENT, "QuakeWorld userinfo lost its canonical admission");
    if (ok) {
        free(record->userinfo); free(record->name); free(record->team); free(record->skin);
        record->userinfo = copies[0]; record->name = copies[1]; record->team = copies[2]; record->skin = copies[3];
        memset(copies, 0, sizeof(copies));
        ok = application_qc_client_userinfo(engine->provider, actor, error);
        if (!ok) application_fault(app, error);
    }
    for (size_t i = 0; i < 4; ++i) free(copies[i]);
    qa_qw_info_free(&info); return ok;
}

bool qa_application_network_qw_entity_next(qa_application *app, uint32_t *cursor,
    bool *present, qa_actor_id *actor, qa_application_network_qw_entity *out, qa_error *error)
{
    if (!cursor || !present || !actor || !out)
        return application_fail(error, QA_ERROR_ARGUMENT, "Missing QuakeWorld dynamic source inventory output");
    *present = false;
    qa_application_network_qw_source source;
    struct application_qc_state *engine = qw_source(app, &source, error);
    if (!engine) return false;
    if (*cursor < 33) *cursor = 33;
    while (*cursor < source.entity_count) {
        uint32_t slot = (*cursor)++;
        qa_qc_slot_binding binding;
        qa_qc_instance *vm = engine->provider->state.qc.instance;
        if (!qa_qc_slot(vm, slot, &binding))
            return application_fail(error, QA_ERROR_FORMAT, "QuakeWorld source edict exceeds its actual allocation");
        if (binding.kind == QA_QC_SLOT_FREE || binding.kind == QA_QC_SLOT_BORROWED) continue;
        if (binding.kind != QA_QC_SLOT_OWNED ||
            !qa_actors_get(qa_session_actors(app->session), binding.actor))
            return application_fail(error, QA_ERROR_ARGUMENT, "QuakeWorld dynamic edict lacks its live source-owned actor");
        int32_t reference;
        float model;
        const char *name;
        if (!qa_qc_slot_reference(vm, slot, &reference, error) ||
            !qw_scalar(engine, reference, "modelindex", &model, error) ||
            !qw_string(engine, reference, "model", &name, error)) return false;
        if (!model || !*name) continue;
        if (!qw_entity(engine, slot, reference, out, error)) return false;
        *actor = binding.actor; *present = true;
        return true;
    }
    return true;
}

bool qa_application_network_qw_precache(qa_application *app, bool models,
    const char *names[255], size_t *count, qa_error *error)
{
    if (!names || !count)
        return application_fail(error, QA_ERROR_ARGUMENT, "Missing QuakeWorld indexed source precache output");
    struct application_qc_state *engine = qw_source(app, NULL, error);
    if (!engine) return false;
    const char *retained[255] = {0};
    size_t extent = 0;
    qa_qc_resource_kind kind = models ? QA_QC_RESOURCE_MODEL : QA_QC_RESOURCE_SOUND;
    for (size_t i = 0; i < engine->resource_count; ++i) {
        const application_qc_resource *resource = &engine->resources[i];
        if (resource->kind != kind) continue;
        uint32_t index = resource->value.index;
        if (!index || index > 255 || !resource->name || !*resource->name || retained[index - 1])
            return application_fail(error, QA_ERROR_FORMAT, "QuakeWorld precache differs from its actual byte-indexed source owner");
        retained[index - 1] = resource->name;
        if (extent < index) extent = index;
    }
    for (size_t i = 0; i < extent; ++i)
        if (!retained[i]) return application_fail(error, QA_ERROR_FORMAT, "QuakeWorld source precache has a missing physical index");
    memcpy(names, retained, sizeof(retained));
    *count = extent;
    return true;
}
