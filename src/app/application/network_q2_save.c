#include "network_q2_private.h"

static bool header_write(qa_application_network_q2 *owner, bool archival, qa_net_writer *writer, qa_error *error)
{
    const qa_application_native_q2_presentation *source = &owner->host.source;
    if (!owner->source_map || !owner->source_instance)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 wire continuation lost its actual source names");
    return qa_net_write_data(writer, "QAQ2WIRE", 8) &&
        qa_net_write_u8(writer, archival ? 1 : 0) &&
        qa_net_write_u32(writer, owner->host.protocol.kind) && qa_net_write_u32(writer, owner->host.protocol.revision) &&
        qa_net_write_u32(writer, owner->host.protocol.flags) && qa_net_write_i32(writer, owner->server_count) &&
        qa_net_write_u8(writer, owner->materials_bound ? 1 : 0) && qa_net_write_u8(writer, owner->materials_capability ? 1 : 0) &&
        qa_net_write_u32(writer, source->kind) && qa_net_write_u32(writer, source->edition) &&
        qa_net_write_u32(writer, source->source_owner) &&
        qa_net_write_u64(writer, source->publication_generation) && qa_net_write_u64(writer, source->map_revision) &&
        qa_net_write_u32(writer, owner->host.client_slots) && qa_net_write_u32(writer, owner->host.entity_slots) &&
        qa_net_write_u64(writer, source->clock.frame_number) && qa_net_write_u64(writer, source->server_time_ns) &&
        qa_net_write_u64(writer, source->clock.host_origin_ns) && qa_net_write_u64(writer, source->clock.elapsed_ns) &&
        qa_net_write_u64(writer, source->clock.debt_ns) && qa_net_write_u8(writer, source->clock.paused ? 1 : 0) &&
        qa_net_write_u32(writer, source->clock.frame.provider) && qa_net_write_u32(writer, source->clock.frame.kind) &&
        qa_net_write_u32(writer, source->clock.frame.phase) && qa_net_write_u64(writer, source->clock.frame.number) &&
        qa_net_write_u64(writer, source->clock.frame.start_ns) && qa_net_write_u64(writer, source->clock.frame.elapsed_ns) &&
        qa_net_write_u64(writer, source->clock.frame.time_ns) &&
        qa_net_write_u64(writer, source->clock_config.interval_ns) &&
        qa_net_write_u32(writer, source->clock_config.kind) &&
        qa_net_write_u64(writer, source->clock_config.initial_time_ns) &&
        qa_net_write_u64(writer, source->clock_config.minimum_frame_ns) &&
        qa_net_write_u64(writer, source->clock_config.maximum_frame_ns) &&
        qa_net_write_u64(writer, source->clock_config.initial_lead_ns) &&
        qa_net_write_u32(writer, source->clock_config.maximum_steps) &&
        qa_net_write_data(writer, owner->identity.bytes, sizeof(owner->identity.bytes)) &&
        qa_net_write_data(writer, owner->map_identity.bytes, sizeof(owner->map_identity.bytes)) &&
        qa_net_write_string(writer, owner->source_instance) && qa_net_write_string(writer, owner->source_map);
}

static bool capture(qa_application_network_q2 *owner, bool retained, qa_buffer *out, qa_error *error)
{
    if (!owner || !owner->app || !owner->app->session || !out || out->data || out->size ||
        (!retained && (!application_network_q2_current(owner, error) || !application_network_q2_observe(owner, error)))) return false;
    size_t capacity = 444;
    const char *instance = owner->source_instance, *map = owner->source_map;
    if (!instance || !map || strlen(instance) > 1023 || strlen(map) > 1023)
        return application_fail(error, QA_ERROR_FORMAT, "Q2 wire continuation source name exceeds its record bound");
    capacity += strlen(instance) + strlen(map);
    uint32_t configs = 0, events = 0, layouts = 0;
    for (uint32_t i = 0; i < owner->config_count; ++i) if (owner->configs[i]) {
        size_t size = strlen(owner->configs[i]) + 5;
        if (size > SIZE_MAX - capacity) return application_fail(error, QA_ERROR_MEMORY, "Q2 wire continuation config extent overflows");
        capacity += size; ++configs;
    }
    for (unsigned i = 0; i < 3; ++i) {
        const application_q2_resource_table *table = &owner->resources[i];
        for (uint32_t j = 1; j <= table->count; ++j) if (table->paths[j]) {
            size_t size = strlen(table->paths[j]) + 5;
            if (size > SIZE_MAX - capacity) return application_fail(error, QA_ERROR_MEMORY, "Q2 wire continuation resource extent overflows");
            capacity += size;
        }
    }
    for (uint32_t slot = 1; slot <= owner->host.client_slots; ++slot) if (owner->layouts[slot].text) {
        size_t size = strlen(owner->layouts[slot].text) + 21;
        if (size > SIZE_MAX - capacity) return application_fail(error, QA_ERROR_MEMORY, "Q2 retained layout extent overflows");
        capacity += size; ++layouts;
    }
    for (size_t slot = 1; slot < owner->entity_capacity; ++slot)
        if (owner->events[slot] && owner->event_actors[slot].registry) ++events;
    if (events > (SIZE_MAX - capacity) / 20)
        return application_fail(error, QA_ERROR_MEMORY, "Q2 wire continuation event extent overflows");
    capacity += (size_t)events * 20;
    qa_buffer holders = {0};
    if (!(retained ? application_network_q2_resources_capture_retained(owner, &holders, error) :
        application_network_q2_resources_capture(owner, &holders, error))) return false;
    if (holders.size > UINT32_MAX || capacity > SIZE_MAX - 4 || holders.size > SIZE_MAX - capacity - 4) {
        qa_buffer_free(&holders);
        return application_fail(error, QA_ERROR_MEMORY, "Q2 actual resource holder continuation extent overflows");
    }
    capacity += 4 + holders.size;
    qa_buffer saved = {.data = malloc(capacity)};
    if (!saved.data) { qa_buffer_free(&holders); return application_fail(error, QA_ERROR_MEMORY, "Retaining actual Q2 wire publication continuation"); }
    qa_net_writer writer;
    qa_net_writer_init(&writer, saved.data, capacity, error);
    bool ok = header_write(owner, retained, &writer, error) && qa_net_write_u32(&writer, owner->config_count) &&
        qa_net_write_u32(&writer, configs);
    for (uint32_t i = 0; ok && i < owner->config_count; ++i) if (owner->configs[i])
        ok = qa_net_write_u32(&writer, i) && qa_net_write_string(&writer, owner->configs[i]);
    for (unsigned i = 0; ok && i < 3; ++i) {
        const application_q2_resource_table *table = &owner->resources[i];
        uint32_t count = 0;
        for (uint32_t j = 1; j <= table->count; ++j) if (table->paths[j]) ++count;
        ok = qa_net_write_u32(&writer, count);
        for (uint32_t j = 1; ok && j <= table->count; ++j) if (table->paths[j])
            ok = qa_net_write_u32(&writer, j) && qa_net_write_string(&writer, table->paths[j]);
    }
    const qa_actor_registry *registry = qa_session_actors(owner->app->session);
    ok = ok && qa_net_write_u32(&writer, layouts);
    for (uint32_t slot = 1; ok && slot <= owner->host.client_slots; ++slot) if (owner->layouts[slot].text) {
        const application_q2_layout_receipt *layout = &owner->layouts[slot]; qa_saved_actor_id actor;
        ok = qa_actors_save_reference(registry, layout->actor, &actor, error) &&
            qa_net_write_u32(&writer, slot) && qa_net_write_u32(&writer, actor.slot) &&
            qa_net_write_u64(&writer, actor.generation) && qa_net_write_u32(&writer, layout->profile) &&
            qa_net_write_string(&writer, layout->text);
    }
    ok = ok && qa_net_write_u64(&writer, owner->event_frame) && qa_net_write_u32(&writer, events);
    for (size_t slot = 1; ok && slot < owner->entity_capacity; ++slot) {
        if (!owner->events[slot] || !owner->event_actors[slot].registry) continue;
        qa_saved_actor_id actor;
        qa_q2_wire_binding physical;
        ok = (retained || (owner->host.source.kind == QA_APPLICATION_NATIVE_Q2_BUILTIN &&
            qa_q2_wire_actor(owner->host.source.source.game, owner->event_actors[slot], &physical, error) &&
            physical.source_slot == slot)) && qa_actors_save_reference(registry, owner->event_actors[slot], &actor, error) &&
            qa_net_write_u32(&writer, (uint32_t)slot) && qa_net_write_u32(&writer, actor.slot) &&
            qa_net_write_u64(&writer, actor.generation) && qa_net_write_u32(&writer, owner->events[slot]);
    }
    if (ok) ok = qa_net_write_u32(&writer, (uint32_t)holders.size) && qa_net_write_data(&writer, holders.data, holders.size);
    qa_buffer_free(&holders);
    if (ok && !retained && !qa_application_native_q2_presentation_current(owner->app, &owner->host.source))
        ok = application_fail(error, QA_ERROR_ARGUMENT, "Q2 wire continuation changed its real Source cut");
    if (ok) { saved.size = qa_net_writer_size(&writer); *out = saved; }
    else qa_buffer_free(&saved);
    return ok;
}

bool qa_application_network_q2_capture(qa_application_network_q2 *owner, qa_buffer *out, qa_error *error)
{ return capture(owner, false, out, error); }

bool qa_application_network_q2_capture_retained(qa_application_network_q2 *owner, qa_buffer *out, qa_error *error)
{ return capture(owner, true, out, error); }

static bool header_read(qa_application_network_q2 *owner, qa_net_reader *reader, qa_error *error)
{
    uint8_t magic[8];
    uint32_t kind, revision, flags, source_kind, edition, clients, entities;
    uint64_t frame, time, interval;
    qa_actor_owner source_owner;
    uint64_t publication_generation, map_revision;
    qa_clock_state clock = {0};
    qa_clock_config policy = {0};
    int32_t server_count;
    qa_sha256_digest identity, saved_map, actual_map;
    char instance[1024], map[1024];
    if (!qa_net_read_data(reader, magic, sizeof(magic))) return false;
    uint8_t archival = qa_net_read_u8(reader);
    kind = qa_net_read_u32(reader);
    revision = qa_net_read_u32(reader); flags = qa_net_read_u32(reader);
    server_count = qa_net_read_i32(reader);
    uint8_t materials_bound = qa_net_read_u8(reader), materials_capability = qa_net_read_u8(reader);
    source_kind = qa_net_read_u32(reader);
    edition = qa_net_read_u32(reader); source_owner = qa_net_read_u32(reader);
    publication_generation = qa_net_read_u64(reader); map_revision = qa_net_read_u64(reader);
    clients = qa_net_read_u32(reader); entities = qa_net_read_u32(reader);
    frame = qa_net_read_u64(reader); time = qa_net_read_u64(reader);
    clock.host_origin_ns = qa_net_read_u64(reader); clock.elapsed_ns = qa_net_read_u64(reader); clock.debt_ns = qa_net_read_u64(reader);
    uint8_t paused = qa_net_read_u8(reader); clock.paused = paused != 0;
    clock.frame.provider = qa_net_read_u32(reader); clock.frame.kind = (qa_clock_kind)qa_net_read_u32(reader);
    clock.frame.phase = (qa_frame_phase)qa_net_read_u32(reader); clock.frame.number = qa_net_read_u64(reader);
    clock.frame.start_ns = qa_net_read_u64(reader); clock.frame.elapsed_ns = qa_net_read_u64(reader);
    clock.frame.time_ns = qa_net_read_u64(reader); clock.frame_number = frame;
    interval = qa_net_read_u64(reader);
    policy.kind = (qa_clock_kind)qa_net_read_u32(reader);
    policy.initial_time_ns = qa_net_read_u64(reader);
    policy.minimum_frame_ns = qa_net_read_u64(reader); policy.maximum_frame_ns = qa_net_read_u64(reader);
    policy.initial_lead_ns = qa_net_read_u64(reader); policy.maximum_steps = qa_net_read_u32(reader);
    if (!qa_net_read_data(reader, identity.bytes, sizeof(identity.bytes)) ||
        !qa_net_read_data(reader, saved_map.bytes, sizeof(saved_map.bytes)) ||
        !qa_net_read_string(reader, instance, sizeof(instance)) || !qa_net_read_string(reader, map, sizeof(map))) return false;
    if (reader->failed || memcmp(magic, "QAQ2WIRE", 8) || archival > 1 || paused > 1 ||
        !source_owner || (uint32_t)clock.frame.kind > QA_CLOCK_Q3 || (uint32_t)clock.frame.phase > QA_FRAME_EXIT ||
        (uint32_t)policy.kind > QA_CLOCK_Q3 ||
        owner->archival != (archival != 0) || materials_bound > 1 || materials_capability > 1 ||
        (!materials_bound && materials_capability))
        return application_fail(error, QA_ERROR_FORMAT, "Q2 continuation has invalid Source custody domain");
    if (owner->archival) {
        qa_q2_codec codec;
        qa_net_protocol_id protocol = {(qa_net_protocol)kind, revision, flags};
        if (!qa_q2_codec_init(&codec, protocol, error) || !clients || clients > 256 ||
            entities <= clients || entities > 65536 || !*instance || !*map ||
            source_kind > QA_APPLICATION_NATIVE_Q2_ORIGINAL || edition > QA_Q2_RERELEASE || !interval)
            return application_fail(error, QA_ERROR_FORMAT, "Q2 archive lost its retained Source metadata");
        owner->host.protocol = protocol; owner->server_count = server_count;
        owner->host.client_slots = clients; owner->host.entity_slots = entities;
        owner->host.source.kind = (qa_application_native_q2_source_kind)source_kind;
        owner->host.source.edition = (qa_q2_edition)edition;
        owner->host.source.source_owner = source_owner; owner->host.source.publication_generation = publication_generation;
        owner->host.source.map_revision = map_revision; owner->host.source.clock = clock;
        owner->host.source.server_time_ns = time;
        policy.interval_ns = interval; owner->host.source.clock_config = policy;
        owner->identity = identity; owner->map_identity = saved_map;
        owner->materials_bound = materials_bound != 0; owner->materials_capability = materials_capability != 0;
        owner->source_instance = application_network_q2_copy(instance, error);
        owner->source_map = application_network_q2_copy(map, error);
        return owner->source_instance && owner->source_map;
    }
    actual_map = *qa_resource_digest(owner->app->map_resource);
    const char *name = qa_strings_cstr(qa_session_strings(owner->app->session), owner->app->current_map);
    const qa_application_native_q2_presentation *source = &owner->host.source;
    if (owner->materials_bound != (materials_bound != 0) || owner->materials_capability != (materials_capability != 0) ||
        kind != (uint32_t)owner->host.protocol.kind || revision != owner->host.protocol.revision || flags != owner->host.protocol.flags ||
        server_count != owner->server_count || source_kind != (uint32_t)source->kind || edition != (uint32_t)source->edition ||
        clients != owner->host.client_slots || entities != owner->host.entity_slots ||
        frame != source->clock.frame_number || time != source->server_time_ns || interval != source->clock_config.interval_ns ||
        clock.host_origin_ns != source->clock.host_origin_ns || clock.elapsed_ns != source->clock.elapsed_ns ||
        clock.debt_ns != source->clock.debt_ns || clock.paused != source->clock.paused ||
        clock.frame.kind != source->clock.frame.kind || clock.frame.phase != source->clock.frame.phase ||
        clock.frame.number != source->clock.frame.number || clock.frame.start_ns != source->clock.frame.start_ns ||
        clock.frame.elapsed_ns != source->clock.frame.elapsed_ns || clock.frame.time_ns != source->clock.frame.time_ns ||
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

static bool restore_payload(qa_application_network_q2 *candidate, qa_bytes bytes, qa_error *error)
{
    qa_net_reader reader;
    qa_net_reader_init(&reader, bytes, error);
    bool ok = header_read(candidate, &reader, error);
    if (ok && candidate->archival) {
        ok = application_network_q2_layout(candidate, error);
        candidate->entity_capacity = candidate->host.entity_slots;
        if (ok) {
            candidate->event_actors = calloc(candidate->entity_capacity, sizeof(*candidate->event_actors));
            candidate->events = calloc(candidate->entity_capacity, sizeof(*candidate->events));
            ok = candidate->event_actors && candidate->events;
            if (!ok) application_fail(error, QA_ERROR_MEMORY, "Restoring retained Q2 event provenance");
        }
    }
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
        if (reader.failed || resources >= table->maximum) {
            ok = application_fail(error, QA_ERROR_FORMAT, "Q2 continuation resource numbering leaves its actual producer"); break;
        }
        previous = 0;
        for (uint32_t j = 0; ok && j < resources; ++j) {
            uint32_t index = qa_net_read_u32(&reader);
            char path[2048];
            ok = qa_net_read_string(&reader, path, sizeof(path));
            uint32_t next = previous + 1;
            if (!i && next == 255) ++next;
            if (ok && (index != next || index >= table->maximum))
                ok = application_fail(error, QA_ERROR_FORMAT, "Q2 restored resource numbering leaves its admitted namespace");
            const char *config = ok ? candidate->configs[table->base + index] : NULL;
            if (ok && (!*path || !config || strcmp(config, path)))
                ok = application_fail(error, QA_ERROR_FORMAT, "Q2 restored resource does not match its original source config index");
            for (uint32_t k = 1; ok && k < index; ++k)
                if (table->paths[k] && !strcmp(table->paths[k], path)) ok = application_fail(error, QA_ERROR_FORMAT, "Q2 restored resource aliases an earlier index");
            if (ok) { table->paths[index] = application_network_q2_copy(path, error); ok = table->paths[index] != NULL; }
            if (ok) table->count = index;
            previous = index;
        }
        for (uint32_t j = 0; ok && j < table->maximum; ++j)
            if (candidate->configs[table->base + j] && !table->paths[j])
                ok = application_fail(error, QA_ERROR_FORMAT, "Q2 continuation omits a published source resource");
    }
    uint32_t layouts = qa_net_read_u32(&reader); previous = 0;
    if (ok && (reader.failed || layouts > candidate->host.client_slots))
        ok = application_fail(error, QA_ERROR_FORMAT, "Q2 layout receipts exceed actual Source clients");
    for (uint32_t i = 0; ok && i < layouts; ++i) {
        uint32_t slot = qa_net_read_u32(&reader);
        qa_saved_actor_id saved = {.slot = qa_net_read_u32(&reader)};
        saved.generation = qa_net_read_u64(&reader);
        uint32_t profile = qa_net_read_u32(&reader);
        char *text = malloc(APPLICATION_Q2_LAYOUT_BYTES);
        qa_actor_id actor = {0}; qa_hud_q2_stat_references references;
        if (!text) { ok = application_fail(error, QA_ERROR_MEMORY, "Restoring the retained Q2 layout message"); break; }
        ok = qa_net_read_string(&reader, text, APPLICATION_Q2_LAYOUT_BYTES) && !reader.failed &&
            slot && slot <= candidate->host.client_slots && slot > previous &&
            (profile == QA_NATIVE_Q2_GAME_API3 || profile == QA_NATIVE_Q2_GAME_API2023) &&
            qa_actors_reference_saved(qa_session_actors(candidate->app->session), saved, candidate->archival, &actor, error) &&
            qa_hud_q2_layout_stat_references(text, profile == QA_NATIVE_Q2_GAME_API2023, &references, error);
        if (ok && !candidate->archival) {
            qa_network_q2_player physical;
            ok = qa_application_network_q2_player(candidate, actor, &physical, error) && physical.source_slot == slot;
        }
        if (ok) { candidate->layouts[slot] = (application_q2_layout_receipt){actor, text, references, (qa_native_profile)profile}; text = NULL; }
        free(text);
        if (!ok && (!error || error->code == QA_OK)) application_fail(error, QA_ERROR_FORMAT, "Q2 retained layout lost its genuine client/text receipt");
        previous = slot;
    }
    uint64_t frame = qa_net_read_u64(&reader);
    uint32_t events = qa_net_read_u32(&reader);
    if (ok && (reader.failed || (candidate->archival ? frame > candidate->host.source.clock.frame_number :
        frame != candidate->host.source.clock.frame_number) || events >= candidate->entity_capacity ||
        (candidate->host.source.kind == QA_APPLICATION_NATIVE_Q2_ORIGINAL && events)))
        ok = application_fail(error, QA_ERROR_FORMAT, "Q2 wire continuation events leave their actual Source frame");
    previous = 0;
    for (uint32_t i = 0; ok && i < events; ++i) {
        uint32_t slot = qa_net_read_u32(&reader);
        qa_saved_actor_id saved = {.slot = qa_net_read_u32(&reader)};
        saved.generation = qa_net_read_u64(&reader);
        uint32_t event = qa_net_read_u32(&reader);
        const qa_actor_registry *registry = qa_session_actors(candidate->app->session);
        qa_actor_id actor = {0};
        const qa_actor_record *live = candidate->archival ? NULL : qa_actors_resolve_saved(registry, saved);
        qa_q2_wire_binding physical;
        if (reader.failed || !slot || slot >= candidate->entity_capacity || slot <= previous || !event || event > 255 ||
            (candidate->archival ? !qa_actors_reference_saved(registry, saved, true, &actor, error) :
                !live || !qa_q2_wire_actor(candidate->host.source.source.game, live->id, &physical, error) || physical.source_slot != slot))
            ok = application_fail(error, QA_ERROR_FORMAT, "Q2 continuation event lost its full restored Source actor");
        if (ok) { candidate->event_actors[slot] = candidate->archival ? actor : live->id; candidate->events[slot] = event; }
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
    if (ok && !candidate->archival) {
        published = calloc(candidate->config_count, sizeof(*published));
        if (!published) ok = application_fail(error, QA_ERROR_MEMORY, "Qualifying restored Q2 Source configstrings");
    }
    for (uint32_t i = 0; ok && !candidate->archival && i < candidate->config_count; ++i) {
        if (!candidate->configs[i]) continue;
        published[i] = application_network_q2_copy(candidate->configs[i], error);
        ok = published[i] != NULL;
    }
    if (ok && !candidate->archival) ok = application_network_q2_observe(candidate, error);
    for (uint32_t i = 0; ok && !candidate->archival && i < candidate->config_count; ++i) {
        const char *before = published[i] ? published[i] : "";
        const char *actual = candidate->configs[i] ? candidate->configs[i] : "";
        if (strcmp(before, actual))
            ok = application_fail(error, QA_ERROR_FORMAT, "Q2 restored configstring differs from its actual Source publication");
    }
    if (published) {
        for (uint32_t i = 0; i < candidate->config_count; ++i) free(published[i]);
        free(published);
    }
    if (ok && !candidate->archival && !qa_application_native_q2_presentation_current(candidate->app, &candidate->host.source))
        ok = application_fail(error, QA_ERROR_ARGUMENT, "Q2 import changed its real restored Source cut");
    if (ok) candidate->initialized = candidate->restored = true;
    return ok;
}

bool qa_application_network_q2_restore_retained(qa_application *app, qa_bytes bytes,
    qa_application_network_q2 **out, qa_error *error)
{
    if (!app || !app->session || !out || *out || !bytes.data)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 archive import requires its retained application custody");
    qa_application_network_q2 *candidate = calloc(1, sizeof(*candidate));
    if (!candidate) return application_fail(error, QA_ERROR_MEMORY, "Restoring retained Q2 publication inventory");
    candidate->app = app; candidate->archival = true;
    bool ok = restore_payload(candidate, bytes, error);
    if (ok) *out = candidate;
    else qa_application_network_q2_destroy(candidate);
    return ok;
}

bool qa_application_network_q2_restore(qa_application_network_q2 *owner, qa_bytes bytes, qa_error *error)
{
    if (!owner || owner->initialized || owner->restored || !bytes.data ||
        !application_network_q2_current(owner, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 wire import requires an empty real restored Source publisher");
    qa_application_network_q2 *candidate = NULL;
    if (!qa_application_network_q2_create(owner->app, owner->host.protocol, owner->server_count, &candidate, error)) return false;
    candidate->materials_bound = owner->materials_bound; candidate->materials_capability = owner->materials_capability;
    bool ok = restore_payload(candidate, bytes, error);
    if (ok) {
        application_network_q2_free_tables(owner);
        application_network_q2_resources_free(owner);
        free(owner->event_actors); free(owner->events);
        owner->configs = candidate->configs; candidate->configs = NULL;
        owner->entries = candidate->entries; candidate->entries = NULL;
        owner->layouts = candidate->layouts; candidate->layouts = NULL;
        for (unsigned i = 0; i < 3; ++i) {
            owner->resources[i] = candidate->resources[i];
            candidate->resources[i].paths = NULL; candidate->resources[i].count = 0;
        }
        owner->event_actors = candidate->event_actors; candidate->event_actors = NULL;
        owner->events = candidate->events; candidate->events = NULL;
        owner->held_resources = candidate->held_resources; candidate->held_resources = NULL;
        owner->held_resource_count = candidate->held_resource_count; candidate->held_resource_count = 0;
        owner->held_resource_capacity = candidate->held_resource_capacity; candidate->held_resource_capacity = 0;
        owner->event_frame = candidate->event_frame; owner->initialized = owner->restored = true;
    }
    qa_application_network_q2_destroy(candidate);
    return ok;
}
