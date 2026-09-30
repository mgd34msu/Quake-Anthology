#include "guest_native_q2_private.h"
#include "guest_native_q2_combat.h"
#include "qa/network_q2_messages.h"
#include <math.h>

static bool protocol(struct application_native_q2 *engine, const qa_q2_server_event *source,
                       qa_actor_id recipient, bool reliable, qa_error *error)
{
    qa_q2_codec codec;
    qa_net_protocol_id id = {.kind = engine->profile == QA_NATIVE_Q2_GAME_API3 ? QA_NET_Q2_34 : QA_NET_Q2KEX_2023};
    if (!qa_q2_codec_init(&codec, id, error)) return false;
    uint8_t bytes[65536]; qa_net_writer writer;
    qa_net_writer_init(&writer, bytes, sizeof(bytes), error);
    if (!qa_q2_server_event_write(&codec, &writer, source)) return false;
    qa_application_protocol_event event = {.recipient = recipient,
        .payload = {bytes, qa_net_writer_size(&writer)}, .reliable = reliable,
        .multicast = !recipient.registry};
    return application_emit_protocol(engine->provider, &event, error);
}

static void print(void *opaque, const qa_native_host_print *source)
{
    struct application_native_q2 *engine = opaque;
    qa_command_context context = engine->command_context;
    context.actor = source->client;
    if (source->kind == QA_NATIVE_HOST_PRINT_DEBUG ||
        qa_console_output_redirected(engine->provider->application->console))
        qa_console_emit(engine->provider->application->console, &context, source->text);
    if (source->kind != QA_NATIVE_HOST_PRINT_DEBUG) {
        qa_q2_server_event event = {.kind = source->kind == QA_NATIVE_HOST_PRINT_CENTER ?
            QA_Q2_SVC_CENTERPRINT : QA_Q2_SVC_PRINT,
            .data.print = {.level = (uint8_t)source->level, .text = source->text}};
        qa_error error = {0};
        if (!protocol(engine, &event, source->client, true, &error))
            application_fault(engine->provider->application, &error);
    }
    if (source->kind != QA_NATIVE_HOST_PRINT_DEBUG &&
        !qa_console_output_redirected(engine->provider->application->console) && engine->platform.print)
        engine->platform.print(engine->platform.context, source);
}

static bool config_get(void *opaque, int32_t index, const char **out, qa_error *error)
{
    struct application_native_q2 *engine = opaque;
    if (engine->profile == QA_NATIVE_Q2_CGAME_API2023) {
        uint32_t slot;
        struct application_native_q2 *source = application_native_q2_hud_source(engine, &slot, error);
        if (!source) return false;
        engine = source;
    }
    if (!out || index < 0 || (uint32_t)index >= engine->configstring_count)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 configstring exceeds its original API table");
    *out = engine->configstrings[index] ? engine->configstrings[index] : "";
    return true;
}

static bool config_set(void *opaque, int32_t index, const char *value, qa_error *error)
{
    struct application_native_q2 *engine = opaque;
    const char *prior;
    if (!value || !config_get(engine, index, &prior, error)) return false;
    if (!strcmp(prior, value)) return true;
    size_t size = strlen(value);
    char *copy = malloc(size + 1);
    if (!copy) return application_fail(error, QA_ERROR_MEMORY, "Retaining native Q2 configstring");
    memcpy(copy, value, size + 1);
    free(engine->configstrings[index]); engine->configstrings[index] = copy;
    ++engine->config_revision;
    if (!engine->map_ready) return true;
    qa_q2_server_event event = {.kind = QA_Q2_SVC_CONFIGSTRING,
        .data.config = {.index = (uint16_t)index, .value = copy}};
    return protocol(engine, &event, (qa_actor_id){0}, true, error);
}

static bool resource(void *opaque, qa_native_host_resource_kind kind, const char *name,
                       int32_t *out, qa_error *error)
{
    struct application_native_q2 *engine = opaque;
    if (!out || !name || (unsigned)kind > QA_NATIVE_HOST_IMAGE)
        return application_fail(error, QA_ERROR_ARGUMENT, "Invalid native Q2 resource namespace");
    *out = 0;
    if (!*name) return true;
    uint32_t base = engine->resource_base[kind], maximum = engine->resource_limit[kind];
    for (uint32_t i = 1; i < maximum; ++i) {
        const char *value = engine->configstrings[base + i];
        if (value && !strcmp(value, name)) { *out = (int32_t)i; return true; }
        if (!value || !*value) {
            if (!config_set(engine, (int32_t)(base + i), name, error)) return false;
            *out = (int32_t)i; return true;
        }
    }
    return application_fail(error, QA_ERROR_MEMORY, "Native Q2 source resource namespace is full");
}

static bool command(void *opaque, qa_native_host_command_view *out, qa_error *error)
{
    (void)error;
    struct application_native_q2 *engine = opaque;
    *out = (qa_native_host_command_view){engine->arguments.count,
        (const char *const *)engine->arguments.values, engine->arguments.args_text};
    return true;
}

static bool message(void *opaque, const qa_native_host_message *source, qa_error *error)
{
    struct application_native_q2 *engine = opaque;
    qa_application_protocol_event event = {.recipient = source->client, .origin = source->origin,
        .payload = source->payload, .destination = source->destination,
        .reliable = source->reliable, .multicast = source->target == QA_NATIVE_HOST_MULTICAST};
    if (!application_emit_protocol(engine->provider, &event, error)) return false;
    if (source->payload.size && (source->payload.data[0] == 4 || source->payload.data[0] == 5)) {
        qa_bytes data = source->payload;
        size_t layout_size = 0;
        if (data.data[0] == 4) {
            const uint8_t *end = memchr(data.data + 1, 0, data.size - 1);
            if (!end || (layout_size = (size_t)(end - data.data - 1)) >= 1024)
                return application_fail(error, QA_ERROR_FORMAT, "Native Q2 HUD layout exceeds the source client record");
        } else if (data.size != 513)
            return application_fail(error, QA_ERROR_FORMAT, "Native Q2 inventory message has an invalid source extent");
        for (uint32_t i = 1; i < 257; ++i) {
            application_native_q2_client *client = &engine->clients[i];
            if (!client->connected || (source->target == QA_NATIVE_HOST_UNICAST &&
                !qa_actor_id_equal(client->actor, source->client))) continue;
            if (source->target == QA_NATIVE_HOST_MULTICAST && source->destination != 0 &&
                source->destination != 3) continue;
            if (data.data[0] == 4) {
                memcpy(client->layout, data.data + 1, layout_size);
                client->layout[layout_size] = 0;
            } else for (size_t item = 0; item < 256; ++item)
                client->inventory[item] = (int16_t)qa_load_u16le(data.data + 1 + item * 2);
        }
    }
    return !engine->platform.message || engine->platform.message(engine->platform.context, source, error);
}

static bool sound(void *opaque, const qa_native_host_sound *source, qa_error *error)
{
    struct application_native_q2 *engine = opaque;
    if (source->index <= 0 || (uint32_t)source->index >= engine->resource_limit[QA_NATIVE_HOST_SOUND])
        return application_fail(error, QA_ERROR_FORMAT, "Native Q2 sound references an unregistered source index");
    const char *name = engine->configstrings[engine->resource_base[QA_NATIVE_HOST_SOUND] + source->index];
    if (!name || !*name) return application_fail(error, QA_ERROR_FORMAT, "Native Q2 sound has no admitted source resource");
    qa_native_host_sound named = *source; named.name = name;
    uint32_t slot = 0;
    if (source->actor.registry) {
        qa_native_entity_table table;
        qa_native_instance *instance = qa_native_host_instance(engine->provider->state.native.host);
        if (!instance || !qa_native_entity_table_get(instance, &table, error)) return false;
        bool found = false;
        for (uint32_t i = 0; i < table.capacity; ++i) {
            qa_native_slot_binding binding;
            if (!qa_native_slot(instance, i, &binding, error)) return false;
            if (binding.kind != QA_NATIVE_SLOT_FREE && qa_actor_id_equal(binding.actor, source->actor)) {
                slot = i; found = true; break;
            }
        }
        if (!found) return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 sound actor projection retired");
    }
    qa_q2_server_event event = {.kind = QA_Q2_SVC_SOUND, .data.sound = {
        .flags = (uint8_t)source->flags, .index = (uint16_t)source->index,
        .volume = source->volume, .attenuation = source->attenuation,
        .time_offset = source->time_offset, .entity = slot, .channel = source->channel,
        .has_position = source->positioned,
        .position = {source->origin.x, source->origin.y, source->origin.z}}};
    if (!protocol(engine, &event, source->client, source->reliable, error)) return false;
    return !engine->platform.sound || engine->platform.sound(engine->platform.context, &named, error);
}

static uint32_t server_frame(void *opaque)
{
    return (uint32_t)((struct application_native_q2 *)opaque)->frame.number;
}

static bool hud_view(void *opaque, uint32_t seat, qa_native_host_q2_hud_view *out, qa_error *error)
{
    struct application_native_q2 *engine = opaque;
    return engine->platform.hud_view
        ? engine->platform.hud_view(engine->platform.context, seat, out, error)
        : application_fail(error, QA_ERROR_UNSUPPORTED, "Native Q2 HUD viewport owner is absent");
}

qa_native_host_engine_services application_native_q2_services(struct application_native_q2 *engine)
{
    return (qa_native_host_engine_services){.context = engine, .print = print,
        .configstring_get = config_get, .configstring_set = config_set, .resource_index = resource,
        .command = command, .message = message, .sound = sound, .server_frame = server_frame,
        .checkpoint = application_native_q2_capture_engine, .restore = application_native_q2_restore_engine,
        .content_files = engine->provider->launch->content, .cvars = engine->cvars,
        .hud_view = hud_view};
}

static bool movement_prepare(void *opaque, qa_native_host *host, qa_native_address record,
    qa_movement_input *input, qa_error *error)
{
    (void)record;
    struct application_native_q2 *engine = opaque;
    qa_application *app = engine->provider->application;
    uint32_t slot = engine->current_client;
    if (!slot || slot >= 257 || !engine->clients[slot].connected ||
        !engine->clients[slot].begun || host != engine->provider->state.native.host)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 Pmove requires the active admitted client call");
    qa_actor_id actor = engine->clients[slot].actor;
    if (!qa_actors_get(qa_session_actors(app->session), actor) ||
        application_provider_for(app, actor, QA_ROLE_MOVEMENT, NULL) != engine->provider)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Native Q2 Pmove cannot replace another selected movement owner");
    qa_body_state body; qa_combat_state combat;
    if (!qa_world_body_read(engine->world, actor, &body, error) ||
        !qa_combat_read_traits(app->combat, actor, &combat, error)) return false;
    input->actor = actor;
    input->command.sequence = engine->current_command_sequence;
    input->time_ns = qa_session_elapsed(app->session);
    input->elapsed_ns = (uint64_t)input->command.milliseconds * UINT64_C(1000000);
    input->environment.health = combat.health;
    const qa_cvar_view *air = qa_cvars_find(engine->cvars, "sv_airaccelerate");
    if (air && isfinite(air->number)) input->profile.data.q2.air_accelerate = (float)air->number;
    application_provider *character = application_provider_for(app, actor, QA_ROLE_CHARACTER, NULL);
    if (character != engine->provider) {
        input->environment.has_body_bounds = true; input->environment.body_bounds = body.bounds;
        qa_application_control_view control;
        if (qa_application_control_read(app, actor, &control)) {
            input->standing.bounds = app->controls[actor.slot].standing_bounds;
            input->standing.view_height = control.view_height;
            input->environment.flight = control.flight;
            input->environment.gravity_multiplier = control.gravity_multiplier;
        }
    }
    return true;
}

static bool movement_commit(void *opaque, qa_native_host *host, qa_native_address record,
    const qa_movement_result *result, qa_error *error)
{
    (void)record;
    struct application_native_q2 *engine = opaque;
    qa_application *app = engine->provider->application;
    uint32_t slot = engine->current_client;
    if (host != engine->provider->state.native.host || !slot || slot >= 257 ||
        !qa_actor_id_equal(result->actor, engine->clients[slot].actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 Pmove completion differs from its active source client");
    if (result->status == QA_MOVEMENT_ACTOR_REMOVED ||
        !qa_actors_get(qa_session_actors(app->session), result->actor)) return true;
    if (result->actor.slot >= app->control_capacity)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 Pmove has no shared control projection");
    application_control_record *control = &app->controls[result->actor.slot];
    if (!control->active || !qa_actor_id_equal(control->actor, result->actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 Pmove control generation differs");
    control->state = result->state; control->bounds = result->bounds;
    control->ground = result->ground; control->water_level = result->water_level;
    control->water_type = result->water_type; control->view_height = result->view_height;
    return true;
}

qa_native_host_movement_services application_native_q2_movement_services(struct application_native_q2 *engine)
{
    return (qa_native_host_movement_services){.context = engine, .prepare = movement_prepare,
        .commit = movement_commit};
}

bool application_native_q2_project(void *opaque, qa_native_host *host, uint32_t slot,
    qa_native_address address, qa_actor_id *out, bool *present, qa_error *error)
{
    (void)host; (void)address;
    struct application_native_q2 *engine = opaque;
    *out = (qa_actor_id){0}; *present = false;
    if (slot > 0 && slot < 257 && engine->clients[slot].actor.registry) {
        *out = engine->clients[slot].actor;
        if (!qa_actors_get(qa_session_actors(engine->provider->application->session), *out))
            return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 projected client generation retired");
        *present = true;
    }
    return true;
}

bool application_native_q2_address(void *opaque, qa_native_host *host, qa_actor_id actor,
    qa_native_address *out, bool *present, qa_error *error)
{
    struct application_native_q2 *engine = opaque;
    *out = 0; *present = false;
    if (!qa_actors_get(qa_session_actors(engine->provider->application->session), actor)) return true;
    for (uint32_t i = 1; i < 257; ++i)
        if (qa_actor_id_equal(engine->clients[i].actor, actor)) {
            if (!qa_native_entity_address(qa_native_host_instance(host), i, out, error)) return false;
            *present = true; return true;
        }
    return true;
}

bool application_native_q2_bind(void *opaque, qa_native_host *host, uint32_t slot,
    qa_actor_id actor, qa_error *error)
{
    (void)host;
    struct application_native_q2 *engine = opaque;
    return qa_session_bind_execution(engine->provider->application->session, actor,
        engine->provider->owner, error) &&
        (engine->profile != QA_NATIVE_Q2_GAME_API3 ||
            application_native_q2_combat_admit(engine, slot, actor, true, error));
}
