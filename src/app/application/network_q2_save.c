#include "network_q2_private.h"

static bool header_write(qa_application_network_q2 *owner, qa_net_writer *writer, qa_error *error)
{
    qa_sha256_digest map;
    qa_sha256(qa_resource_bytes(owner->app->map_resource), &map);
    const qa_application_native_q2_presentation *source = &owner->host.source;
    const char *name = qa_strings_cstr(qa_session_strings(owner->app->session), owner->app->current_map);
    if (!name || !source->launch->selection.instance)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 wire continuation lost its actual source names");
    return qa_net_write_data(writer, "QAQ2WIRE", 8) && qa_net_write_u32(writer, 3) &&
        qa_net_write_u32(writer, owner->host.protocol.kind) && qa_net_write_u32(writer, owner->host.protocol.revision) &&
        qa_net_write_u32(writer, owner->host.protocol.flags) && qa_net_write_i32(writer, owner->server_count) &&
        qa_net_write_u32(writer, source->kind) && qa_net_write_u32(writer, source->edition) &&
        qa_net_write_u32(writer, owner->host.client_slots) && qa_net_write_u32(writer, owner->host.entity_slots) &&
        qa_net_write_u64(writer, source->clock.frame_number) && qa_net_write_u64(writer, source->server_time_ns) &&
        qa_net_write_u64(writer, source->clock_config.interval_ns) &&
        qa_net_write_u32(writer, source->clock_config.kind) &&
        qa_net_write_u64(writer, source->clock_config.initial_time_ns) &&
        qa_net_write_u64(writer, source->clock_config.minimum_frame_ns) &&
        qa_net_write_u64(writer, source->clock_config.maximum_frame_ns) &&
        qa_net_write_u64(writer, source->clock_config.initial_lead_ns) &&
        qa_net_write_u32(writer, source->clock_config.maximum_steps) &&
        qa_net_write_data(writer, owner->identity.bytes, sizeof(owner->identity.bytes)) &&
        qa_net_write_data(writer, map.bytes, sizeof(map.bytes)) &&
        qa_net_write_string(writer, source->launch->selection.instance) && qa_net_write_string(writer, name);
}

bool qa_application_network_q2_capture(qa_application_network_q2 *owner, qa_buffer *out, qa_error *error)
{
    if (!out || out->data || out->size || !application_network_q2_current(owner, error) ||
        !application_network_q2_observe(owner, error)) return false;
    size_t capacity = 320;
    const char *instance = owner->host.source.launch->selection.instance;
    const char *map = qa_strings_cstr(qa_session_strings(owner->app->session), owner->app->current_map);
    if (!instance || !map || strlen(instance) > 1023 || strlen(map) > 1023)
        return application_fail(error, QA_ERROR_FORMAT, "Q2 wire continuation source name exceeds its record bound");
    capacity += strlen(instance) + strlen(map);
    uint32_t configs = 0, events = 0;
    for (uint32_t i = 0; i < owner->config_count; ++i) if (owner->configs[i]) {
        size_t size = strlen(owner->configs[i]) + 5;
        if (size > SIZE_MAX - capacity) return application_fail(error, QA_ERROR_MEMORY, "Q2 wire continuation config extent overflows");
        capacity += size; ++configs;
    }
    for (unsigned i = 0; i < 3; ++i) {
        const application_q2_resource_table *table = &owner->resources[i];
        for (uint32_t j = 1; j <= table->count; ++j) {
            size_t size = strlen(table->paths[j]) + 1;
            if (size > SIZE_MAX - capacity) return application_fail(error, QA_ERROR_MEMORY, "Q2 wire continuation resource extent overflows");
            capacity += size;
        }
    }
    for (size_t slot = 1; slot < owner->entity_capacity; ++slot)
        if (owner->events[slot] && owner->event_actors[slot].registry) ++events;
    if (events > (SIZE_MAX - capacity) / 20)
        return application_fail(error, QA_ERROR_MEMORY, "Q2 wire continuation event extent overflows");
    capacity += (size_t)events * 20;
    qa_buffer holders = {0};
    if (!application_network_q2_resources_capture(owner, &holders, error)) return false;
    if (holders.size > UINT32_MAX || capacity > SIZE_MAX - 4 || holders.size > SIZE_MAX - capacity - 4) {
        qa_buffer_free(&holders);
        return application_fail(error, QA_ERROR_MEMORY, "Q2 actual resource holder continuation extent overflows");
    }
    capacity += 4 + holders.size;
    qa_buffer saved = {.data = malloc(capacity)};
    if (!saved.data) { qa_buffer_free(&holders); return application_fail(error, QA_ERROR_MEMORY, "Retaining actual Q2 wire publication continuation"); }
    qa_net_writer writer;
    qa_net_writer_init(&writer, saved.data, capacity, error);
    bool ok = header_write(owner, &writer, error) && qa_net_write_u32(&writer, owner->config_count) &&
        qa_net_write_u32(&writer, configs);
    for (uint32_t i = 0; ok && i < owner->config_count; ++i) if (owner->configs[i])
        ok = qa_net_write_u32(&writer, i) && qa_net_write_string(&writer, owner->configs[i]);
    for (unsigned i = 0; ok && i < 3; ++i) {
        const application_q2_resource_table *table = &owner->resources[i];
        ok = qa_net_write_u32(&writer, table->count);
        for (uint32_t j = 1; ok && j <= table->count; ++j) ok = qa_net_write_string(&writer, table->paths[j]);
    }
    ok = ok && qa_net_write_u64(&writer, owner->event_frame) && qa_net_write_u32(&writer, events);
    const qa_actor_registry *registry = qa_session_actors(owner->app->session);
    for (size_t slot = 1; ok && slot < owner->entity_capacity; ++slot) {
        if (!owner->events[slot] || !owner->event_actors[slot].registry) continue;
        qa_saved_actor_id actor;
        qa_q2_wire_binding physical;
        ok = owner->host.source.kind == QA_APPLICATION_NATIVE_Q2_BUILTIN &&
            qa_q2_wire_actor(owner->host.source.source.game, owner->event_actors[slot], &physical, error) &&
            physical.source_slot == slot && qa_actors_save_reference(registry, physical.actor, &actor, error) &&
            qa_net_write_u32(&writer, (uint32_t)slot) && qa_net_write_u32(&writer, actor.slot) &&
            qa_net_write_u64(&writer, actor.generation) && qa_net_write_u32(&writer, owner->events[slot]);
    }
    if (ok) ok = qa_net_write_u32(&writer, (uint32_t)holders.size) && qa_net_write_data(&writer, holders.data, holders.size);
    qa_buffer_free(&holders);
    if (ok && !qa_application_native_q2_presentation_current(owner->app, &owner->host.source))
        ok = application_fail(error, QA_ERROR_ARGUMENT, "Q2 wire continuation changed its real Source cut");
    if (ok) { saved.size = qa_net_writer_size(&writer); *out = saved; }
    else qa_buffer_free(&saved);
    return ok;
}

static bool header_read(qa_application_network_q2 *owner, qa_net_reader *reader, qa_error *error)
{
    uint8_t magic[8];
    uint32_t version, kind, revision, flags, source_kind, edition, clients, entities;
    uint64_t frame, time, interval;
    qa_clock_config policy = {0};
    int32_t server_count;
    qa_sha256_digest identity, saved_map, actual_map;
    char instance[1024], map[1024];
    if (!qa_net_read_data(reader, magic, sizeof(magic))) return false;
    version = qa_net_read_u32(reader); kind = qa_net_read_u32(reader);
    revision = qa_net_read_u32(reader); flags = qa_net_read_u32(reader);
    server_count = qa_net_read_i32(reader); source_kind = qa_net_read_u32(reader);
    edition = qa_net_read_u32(reader); clients = qa_net_read_u32(reader); entities = qa_net_read_u32(reader);
    frame = qa_net_read_u64(reader); time = qa_net_read_u64(reader); interval = qa_net_read_u64(reader);
    policy.kind = (qa_clock_kind)qa_net_read_u32(reader);
    policy.initial_time_ns = qa_net_read_u64(reader);
    policy.minimum_frame_ns = qa_net_read_u64(reader); policy.maximum_frame_ns = qa_net_read_u64(reader);
    policy.initial_lead_ns = qa_net_read_u64(reader); policy.maximum_steps = qa_net_read_u32(reader);
    if (!qa_net_read_data(reader, identity.bytes, sizeof(identity.bytes)) ||
        !qa_net_read_data(reader, saved_map.bytes, sizeof(saved_map.bytes)) ||
        !qa_net_read_string(reader, instance, sizeof(instance)) || !qa_net_read_string(reader, map, sizeof(map))) return false;
    qa_sha256(qa_resource_bytes(owner->app->map_resource), &actual_map);
    const char *name = qa_strings_cstr(qa_session_strings(owner->app->session), owner->app->current_map);
    const qa_application_native_q2_presentation *source = &owner->host.source;
    if (reader->failed || memcmp(magic, "QAQ2WIRE", 8) || version != 3 ||
        kind != (uint32_t)owner->host.protocol.kind || revision != owner->host.protocol.revision || flags != owner->host.protocol.flags ||
        server_count != owner->server_count || source_kind != (uint32_t)source->kind || edition != (uint32_t)source->edition ||
        clients != owner->host.client_slots || entities != owner->host.entity_slots ||
        frame != source->clock.frame_number || time != source->server_time_ns || interval != source->clock_config.interval_ns ||
        policy.kind != source->clock_config.kind || policy.initial_time_ns != source->clock_config.initial_time_ns ||
        policy.minimum_frame_ns != source->clock_config.minimum_frame_ns ||
        policy.maximum_frame_ns != source->clock_config.maximum_frame_ns ||
        policy.initial_lead_ns != source->clock_config.initial_lead_ns ||
        policy.maximum_steps != source->clock_config.maximum_steps ||
        !qa_sha256_equal(&identity, &owner->identity) || !qa_sha256_equal(&saved_map, &actual_map) ||
        strcmp(instance, source->launch->selection.instance) || !name || strcmp(map, name))
        return application_fail(error, QA_ERROR_FORMAT, "Q2 wire continuation differs from its restored physical Source");
    return true;
}

bool qa_application_network_q2_restore(qa_application_network_q2 *owner, qa_bytes bytes, qa_error *error)
{
    if (!owner || owner->initialized || owner->restored || !bytes.data ||
        !application_network_q2_current(owner, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 wire import requires an empty real restored Source publisher");
    qa_application_network_q2 *candidate = NULL;
    if (!qa_application_network_q2_create(owner->app, owner->host.protocol, owner->server_count, &candidate, error)) return false;
    qa_net_reader reader;
    qa_net_reader_init(&reader, bytes, error);
    bool ok = header_read(candidate, &reader, error);
    uint32_t maximum = qa_net_read_u32(&reader), count = qa_net_read_u32(&reader);
    if (ok && (reader.failed || maximum != candidate->config_count || count > maximum))
        ok = application_fail(error, QA_ERROR_FORMAT, "Q2 wire continuation has invalid configstring extent");
    uint32_t previous = 0;
    for (uint32_t i = 0; ok && i < count; ++i) {
        uint32_t index = qa_net_read_u32(&reader);
        char value[2048];
        ok = qa_net_read_string(&reader, value, sizeof(value));
        if (ok && (index >= maximum || (i && index <= previous) || !*value))
            ok = application_fail(error, QA_ERROR_FORMAT, "Q2 wire continuation configs are empty, aliased or unordered");
        if (ok) ok = application_network_q2_config(candidate, index, value, error);
        previous = index;
    }
    for (unsigned i = 0; ok && i < 3; ++i) {
        application_q2_resource_table *table = &candidate->resources[i];
        uint32_t resources = qa_net_read_u32(&reader);
        if (reader.failed || resources >= table->maximum ||
            (candidate->host.source.kind == QA_APPLICATION_NATIVE_Q2_ORIGINAL && resources)) {
            ok = application_fail(error, QA_ERROR_FORMAT, "Q2 continuation resource numbering leaves its actual producer"); break;
        }
        for (uint32_t j = 1; ok && j <= resources; ++j) {
            char path[2048];
            ok = qa_net_read_string(&reader, path, sizeof(path));
            const char *config = candidate->configs[table->base + j];
            if (ok && (!*path || !config || strcmp(config, path)))
                ok = application_fail(error, QA_ERROR_FORMAT, "Q2 restored resource does not match its original source config index");
            for (uint32_t k = 1; ok && k < j; ++k)
                if (!strcmp(table->paths[k], path)) ok = application_fail(error, QA_ERROR_FORMAT, "Q2 restored resource aliases an earlier index");
            if (ok) { table->paths[j] = application_network_q2_copy(path, error); ok = table->paths[j] != NULL; }
            if (ok) table->count = j;
        }
        for (uint32_t j = resources + 1; ok && j < table->maximum; ++j)
            if (candidate->configs[table->base + j] && candidate->host.source.kind == QA_APPLICATION_NATIVE_Q2_BUILTIN)
                ok = application_fail(error, QA_ERROR_FORMAT, "Q2 continuation omits a published source resource");
    }
    uint64_t frame = qa_net_read_u64(&reader);
    uint32_t events = qa_net_read_u32(&reader);
    if (ok && (reader.failed || frame != candidate->host.source.clock.frame_number || events >= candidate->entity_capacity ||
        (candidate->host.source.kind == QA_APPLICATION_NATIVE_Q2_ORIGINAL && events)))
        ok = application_fail(error, QA_ERROR_FORMAT, "Q2 wire continuation events leave their actual Source frame");
    previous = 0;
    for (uint32_t i = 0; ok && i < events; ++i) {
        uint32_t slot = qa_net_read_u32(&reader);
        qa_saved_actor_id saved = {.slot = qa_net_read_u32(&reader)};
        saved.generation = qa_net_read_u64(&reader);
        uint32_t event = qa_net_read_u32(&reader);
        const qa_actor_record *actor = qa_actors_resolve_saved(qa_session_actors(candidate->app->session), saved);
        qa_q2_wire_binding physical;
        if (reader.failed || !slot || slot >= candidate->entity_capacity || slot <= previous || !event || event > 255 || !actor ||
            !qa_q2_wire_actor(candidate->host.source.source.game, actor->id, &physical, error) || physical.source_slot != slot)
            ok = application_fail(error, QA_ERROR_FORMAT, "Q2 continuation event lost its full restored Source actor");
        if (ok) { candidate->event_actors[slot] = actor->id; candidate->events[slot] = event; }
        previous = slot;
    }
    candidate->event_frame = frame;
    if (ok) {
        uint32_t size = qa_net_read_u32(&reader); qa_bytes holders;
        ok = !reader.failed && qa_net_read_bytes(&reader, size, &holders) &&
            application_network_q2_resources_restore(candidate, holders, error);
    }
    ok = ok && qa_net_reader_finish(&reader);
    char **published = NULL;
    if (ok) {
        published = calloc(candidate->config_count, sizeof(*published));
        if (!published) ok = application_fail(error, QA_ERROR_MEMORY, "Qualifying restored Q2 Source configstrings");
    }
    for (uint32_t i = 0; ok && i < candidate->config_count; ++i) {
        if (!candidate->configs[i]) continue;
        published[i] = application_network_q2_copy(candidate->configs[i], error);
        ok = published[i] != NULL;
    }
    if (ok) ok = application_network_q2_observe(candidate, error);
    for (uint32_t i = 0; ok && i < candidate->config_count; ++i) {
        const char *before = published[i] ? published[i] : "";
        const char *actual = candidate->configs[i] ? candidate->configs[i] : "";
        if (strcmp(before, actual))
            ok = application_fail(error, QA_ERROR_FORMAT, "Q2 restored configstring differs from its actual Source publication");
    }
    if (published) {
        for (uint32_t i = 0; i < candidate->config_count; ++i) free(published[i]);
        free(published);
    }
    if (ok && !qa_application_native_q2_presentation_current(candidate->app, &candidate->host.source))
        ok = application_fail(error, QA_ERROR_ARGUMENT, "Q2 import changed its real restored Source cut");
    if (ok) {
        application_network_q2_free_tables(owner);
        application_network_q2_resources_free(owner);
        free(owner->event_actors); free(owner->events);
        owner->configs = candidate->configs; candidate->configs = NULL;
        owner->entries = candidate->entries; candidate->entries = NULL;
        for (unsigned i = 0; i < 3; ++i) {
            owner->resources[i] = candidate->resources[i];
            candidate->resources[i].paths = NULL; candidate->resources[i].count = 0;
        }
        owner->event_actors = candidate->event_actors; candidate->event_actors = NULL;
        owner->events = candidate->events; candidate->events = NULL;
        owner->held_resources = candidate->held_resources; candidate->held_resources = NULL;
        owner->held_resource_count = candidate->held_resource_count; candidate->held_resource_count = 0;
        owner->held_resource_capacity = candidate->held_resource_capacity; candidate->held_resource_capacity = 0;
        owner->event_frame = frame; owner->initialized = owner->restored = true;
    }
    qa_application_network_q2_destroy(candidate);
    return ok;
}
