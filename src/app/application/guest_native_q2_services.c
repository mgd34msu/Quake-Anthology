#include "guest_native_q2_private.h"
#include "qa/network_q2_messages.h"

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

qa_native_host_engine_services application_native_q2_services(struct application_native_q2 *engine)
{
    return (qa_native_host_engine_services){.context = engine, .print = print,
        .configstring_get = config_get, .configstring_set = config_set, .resource_index = resource,
        .command = command, .message = message, .sound = sound, .server_frame = server_frame,
        .checkpoint = application_native_q2_capture_engine, .restore = application_native_q2_restore_engine,
        .content_files = engine->provider->launch->content, .cvars = engine->cvars};
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
    (void)host; (void)slot;
    struct application_native_q2 *engine = opaque;
    return qa_session_bind_execution(engine->provider->application->session, actor,
        engine->provider->owner, error);
}
