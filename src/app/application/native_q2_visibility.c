#include "native_q2_visibility.h"
#include "qa/native_host_q2_wire.h"
#include "qa/network.h"
#include "native_q2_callbacks.h"

typedef struct native_visibility_row {
    qa_actor_id actor;
    uint32_t source_slot;
    uint64_t visible[4];
} native_visibility_row;

struct application_native_q2_visibility {
    uint64_t frame, time_ns;
    uint32_t source_capacity, clients;
    qa_actor_id viewers[257];
    native_visibility_row *rows;
    size_t count, capacity;
};

typedef struct native_visibility_call {
    struct application_native_q2 *engine;
    uint32_t entity_slot, viewer_slot;
    qa_actor_id entity, viewer;
    bool visible;
} native_visibility_call;

static bool invoke_visibility(void *context, qa_error *error)
{
    native_visibility_call *call = context;
    ++call->engine->calls;
    bool ok = qa_native_host_q2_entity_visible(call->engine->provider->state.native.host,
        call->entity_slot, call->entity, call->viewer_slot, call->viewer, &call->visible, error);
    --call->engine->calls;
    return ok;
}

void application_native_q2_visibility_destroy(application_native_q2_visibility **owner)
{
    if (!owner || !*owner) return;
    free((*owner)->rows); free(*owner); *owner = NULL;
}

void application_native_q2_visibility_invalidate(struct application_native_q2 *engine)
{
    if (engine) application_native_q2_visibility_destroy(&engine->visibility);
}

static bool remember(application_native_q2_visibility *owner,
    const qa_native_host_q2_entity *entity, qa_error *error)
{
    if (owner->count == owner->capacity) {
        size_t capacity = owner->capacity ? owner->capacity * 2 : 16;
        if (capacity < owner->capacity || capacity > SIZE_MAX / sizeof(*owner->rows))
            return application_fail(error, QA_ERROR_MEMORY, "Q2 visibility receipts exceed addressable storage");
        native_visibility_row *rows = realloc(owner->rows, capacity * sizeof(*rows));
        if (!rows) return application_fail(error, QA_ERROR_MEMORY, "Retaining actual Q2 per-player visibility decisions");
        owner->rows = rows; owner->capacity = capacity;
    }
    owner->rows[owner->count++] = (native_visibility_row){
        .actor = entity->binding.actor, .source_slot = entity->binding.source_slot};
    return true;
}

bool application_native_q2_visibility_complete(struct application_native_q2 *engine, qa_error *error)
{
    if (!engine || engine->profile != QA_NATIVE_Q2_GAME_API2023) return true;
    qa_native_host *host = engine->provider->state.native.host;
    qa_native_entity_table table;
    const qa_cvar_view *clients = qa_cvars_find(engine->cvars, "maxclients");
    qa_source_frame frame;
    application_provider *clock_owner = engine->callbacks ?
        application_world_provider(engine->provider->application, QA_ROLE_ENTITIES, "") : engine->provider;
    qa_frame_phase phase = engine->callbacks ? QA_FRAME_EXIT : QA_CLIENT_END_FRAME;
    if (!engine->map_ready || !host || !clients || clients->integer < 1 || clients->integer > 256 ||
        !clock_owner || !qa_session_active_frame(engine->provider->application->session, clock_owner->owner, &frame) ||
        frame.phase != phase || frame.number != engine->frame.number ||
        frame.time_ns != engine->frame.time_ns ||
        !qa_native_entity_table_get(qa_native_host_instance(host), &table, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 visibility capture requires its completed actual Source stage");
    if (table.count > table.capacity || table.capacity > 65536 || table.capacity <= (uint32_t)clients->integer)
        return application_fail(error, QA_ERROR_FORMAT, "Q2 visibility source table has invalid entity or client extent");
    application_native_q2_visibility *candidate = calloc(1, sizeof(*candidate));
    if (!candidate) return application_fail(error, QA_ERROR_MEMORY, "Retaining completed Q2 Source visibility");
    candidate->frame = frame.number; candidate->time_ns = frame.time_ns;
    candidate->source_capacity = table.capacity; candidate->clients = (uint32_t)clients->integer;
    bool ok = true;
    for (uint32_t slot = 1; ok && slot <= candidate->clients; ++slot) {
        const application_native_q2_client *client = &engine->clients[slot];
        if (!client->connected || !client->begun || client->disconnect_started) continue;
        qa_native_host_q2_entity entity;
        ok = qa_native_host_q2_wire_entity_stage(host, slot, &entity, error);
        if (ok && (!entity.in_use || !qa_actor_id_equal(entity.binding.actor, client->actor)))
            ok = application_fail(error, QA_ERROR_ARGUMENT, "Q2 visibility viewer lost its admitted physical Source client");
        if (ok) candidate->viewers[slot] = client->actor;
    }
    for (uint32_t slot = 1; ok && slot < table.count; ++slot) {
        qa_native_host_q2_entity entity;
        ok = qa_native_host_q2_wire_entity_stage(host, slot, &entity, error);
        if (!ok || !entity.in_use || !(entity.server_flags & 256)) continue;
        ok = remember(candidate, &entity, error);
        native_visibility_row *row = ok ? &candidate->rows[candidate->count - 1] : NULL;
        for (uint32_t viewer = 1; ok && viewer <= candidate->clients; ++viewer) {
            if (!candidate->viewers[viewer].registry) continue;
            native_visibility_call call = {engine, slot, viewer, entity.binding.actor, candidate->viewers[viewer], false};
            ok = engine->callbacks ? application_native_q2_callbacks_transfer(engine->callbacks, invoke_visibility, &call, error) :
                invoke_visibility(&call, error);
            if (ok && call.visible) row->visible[(viewer - 1) / 64] |= UINT64_C(1) << ((viewer - 1) % 64);
        }
    }
    qa_native_entity_table after;
    qa_source_frame returned;
    if (ok) ok = qa_native_entity_table_get(qa_native_host_instance(host), &after, error) &&
        qa_session_active_frame(engine->provider->application->session, clock_owner->owner, &returned);
    if (ok && (after.base != table.base || after.stride != table.stride || after.count != table.count ||
        after.capacity != table.capacity || returned.phase != phase ||
        returned.number != frame.number || returned.time_ns != frame.time_ns ||
        engine->frame.number != frame.number || engine->frame.time_ns != frame.time_ns))
        ok = application_fail(error, QA_ERROR_ARGUMENT, "Q2 visibility callbacks changed the completed Source table or frame");
    for (size_t i = 0; ok && i < candidate->count; ++i) {
        qa_native_host_q2_entity entity;
        ok = qa_native_host_q2_wire_entity_stage(host, candidate->rows[i].source_slot, &entity, error);
        if (ok && (!entity.in_use || !(entity.server_flags & 256) ||
            !qa_actor_id_equal(entity.binding.actor, candidate->rows[i].actor)))
            ok = application_fail(error, QA_ERROR_ARGUMENT, "Q2 visibility callback retired another retained Source entity");
    }
    for (uint32_t slot = 1; ok && slot <= candidate->clients; ++slot) if (candidate->viewers[slot].registry) {
        qa_native_host_q2_entity entity;
        ok = qa_native_host_q2_wire_entity_stage(host, slot, &entity, error);
        if (ok && (!entity.in_use || !qa_actor_id_equal(entity.binding.actor, candidate->viewers[slot])))
            ok = application_fail(error, QA_ERROR_ARGUMENT, "Q2 visibility callback retired another retained Source viewer");
    }
    if (ok) { application_native_q2_visibility_destroy(&engine->visibility); engine->visibility = candidate; candidate = NULL; }
    application_native_q2_visibility_destroy(&candidate);
    return ok;
}

bool application_native_q2_visibility_read(struct application_native_q2 *engine,
    uint32_t source_slot, qa_actor_id actor, uint32_t viewer_slot, qa_actor_id viewer,
    bool *out, qa_error *error)
{
    const application_native_q2_visibility *owner = engine ? engine->visibility : NULL;
    if (!owner || !out || !viewer_slot || viewer_slot > owner->clients ||
        owner->frame != engine->frame.number || owner->time_ns != engine->frame.time_ns ||
        !qa_actor_id_equal(owner->viewers[viewer_slot], viewer) ||
        !engine->clients[viewer_slot].connected || !engine->clients[viewer_slot].begun ||
        engine->clients[viewer_slot].disconnect_started ||
        !qa_actor_id_equal(engine->clients[viewer_slot].actor, viewer))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 visibility has no completed decision for this physical Source viewer");
    for (size_t i = 0; i < owner->count; ++i) {
        const native_visibility_row *row = &owner->rows[i];
        if (row->source_slot != source_slot || !qa_actor_id_equal(row->actor, actor)) continue;
        *out = (row->visible[(viewer_slot - 1) / 64] & (UINT64_C(1) << ((viewer_slot - 1) % 64))) != 0;
        return true;
    }
    return application_fail(error, QA_ERROR_ARGUMENT, "Q2 instanced entity has no completed Source visibility decision");
}

bool application_native_q2_visibility_ready(struct application_native_q2 *engine,
    const qa_network_q2_player *players, size_t count, bool *out, qa_error *error)
{
    if (!engine || !players || !count || !out)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 visibility readiness requires actual Source players");
    *out = true;
    if (engine->profile != QA_NATIVE_Q2_GAME_API2023) return true;
    uint32_t extent;
    qa_native_host *host = engine->provider->state.native.host;
    if (!qa_native_host_q2_wire_count(host, &extent, error)) return false;
    const application_native_q2_visibility *owner = engine->visibility;
    for (uint32_t slot = 1; slot < extent; ++slot) {
        qa_native_host_q2_entity entity;
        if (!qa_native_host_q2_wire_entity(host, slot, &entity, error)) return false;
        if (!entity.in_use || !(entity.server_flags & 256) || (entity.server_flags & 1)) continue;
        if (!owner || owner->frame != engine->frame.number || owner->time_ns != engine->frame.time_ns) { *out = false; return true; }
        const native_visibility_row *row = NULL;
        for (size_t i = 0; i < owner->count; ++i)
            if (owner->rows[i].source_slot == slot && qa_actor_id_equal(owner->rows[i].actor, entity.binding.actor)) { row = &owner->rows[i]; break; }
        if (!row) { *out = false; return true; }
        for (size_t i = 0; i < count; ++i) {
            uint32_t viewer = players[i].source_slot;
            if (!viewer || viewer > owner->clients ||
                !qa_actor_id_equal(owner->viewers[viewer], players[i].actor)) { *out = false; return true; }
        }
    }
    return true;
}

bool application_native_q2_visibility_validate(struct application_native_q2 *engine, qa_error *error)
{
    const application_native_q2_visibility *owner = engine ? engine->visibility : NULL;
    if (!owner) return true;
    qa_native_entity_table table;
    const qa_cvar_view *clients = qa_cvars_find(engine->cvars, "maxclients");
    qa_native_host *host = engine->provider->state.native.host;
    if (engine->profile != QA_NATIVE_Q2_GAME_API2023 || !engine->map_ready || !clients ||
        clients->integer != (int32_t)owner->clients || owner->frame != engine->frame.number ||
        owner->time_ns != engine->frame.time_ns ||
        !qa_native_entity_table_get(qa_native_host_instance(host), &table, error) || table.capacity != owner->source_capacity)
        return application_fail(error, QA_ERROR_FORMAT, "Q2 visibility continuation differs from its restored physical Source");
    for (uint32_t slot = 1; slot <= owner->clients; ++slot) if (owner->viewers[slot].registry) {
        qa_native_host_q2_entity entity;
        if (!engine->clients[slot].connected || !engine->clients[slot].begun ||
            engine->clients[slot].disconnect_started || !qa_actor_id_equal(engine->clients[slot].actor, owner->viewers[slot]) ||
            !qa_native_host_q2_wire_entity(host, slot, &entity, error) || !entity.in_use ||
            !qa_actor_id_equal(entity.binding.actor, owner->viewers[slot]))
            return application_fail(error, QA_ERROR_FORMAT, "Q2 visibility continuation lost a restored Source viewer");
    }
    for (size_t i = 0; i < owner->count; ++i) {
        qa_native_host_q2_entity entity;
        if (!qa_native_host_q2_wire_entity(host, owner->rows[i].source_slot, &entity, error) ||
            !entity.in_use || !(entity.server_flags & 256) || !qa_actor_id_equal(entity.binding.actor, owner->rows[i].actor))
            return application_fail(error, QA_ERROR_FORMAT, "Q2 visibility continuation lost a restored instanced Source entity");
    }
    return true;
}

void application_native_q2_visibility_released(struct application_native_q2 *engine, qa_actor_id actor)
{
    application_native_q2_visibility *owner = engine ? engine->visibility : NULL;
    if (!owner) return;
    for (uint32_t slot = 1; slot <= owner->clients; ++slot) {
        if (!qa_actor_id_equal(owner->viewers[slot], actor)) continue;
        owner->viewers[slot] = (qa_actor_id){0};
        for (size_t i = 0; i < owner->count; ++i)
            owner->rows[i].visible[(slot - 1) / 64] &= ~(UINT64_C(1) << ((slot - 1) % 64));
    }
    for (size_t i = 0; i < owner->count;) {
        if (!qa_actor_id_equal(owner->rows[i].actor, actor)) { ++i; continue; }
        --owner->count;
        memmove(owner->rows + i, owner->rows + i + 1, (owner->count - i) * sizeof(*owner->rows));
    }
}

static bool actor_write(qa_net_writer *writer, const qa_actor_registry *registry,
    qa_actor_id actor, qa_error *error)
{
    qa_saved_actor_id saved;
    return qa_actors_save_reference(registry, actor, &saved, error) &&
        qa_net_write_u32(writer, saved.slot) && qa_net_write_u64(writer, saved.generation);
}

static bool actor_read(qa_net_reader *reader, const qa_actor_registry *registry,
    qa_actor_id *out, qa_error *error)
{
    qa_saved_actor_id saved = {.slot = qa_net_read_u32(reader)};
    saved.generation = qa_net_read_u64(reader);
    return !reader->failed && qa_actors_reference_saved(registry, saved, false, out, error);
}

bool application_native_q2_visibility_capture(struct application_native_q2 *engine,
    qa_buffer *out, qa_error *error)
{
    if (!engine || !out || out->data) return application_fail(error, QA_ERROR_ARGUMENT, "Q2 visibility continuation requires an empty owner output");
    const application_native_q2_visibility *owner = engine->visibility;
    if (!owner) return true;
    if (engine->profile != QA_NATIVE_Q2_GAME_API2023 || !engine->map_ready ||
        owner->frame != engine->frame.number || owner->time_ns != engine->frame.time_ns ||
        owner->count > (SIZE_MAX - 36 - 256 * 16) / 48)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 visibility continuation lost its completed Source frame");
    qa_buffer buffer = {.data = malloc(36 + 256 * 16 + owner->count * 48)};
    if (!buffer.data) return application_fail(error, QA_ERROR_MEMORY, "Saving actual Q2 visibility decisions");
    qa_net_writer writer; qa_net_writer_init(&writer, buffer.data, 36 + 256 * 16 + owner->count * 48, error);
    const qa_actor_registry *registry = qa_session_actors(engine->provider->application->session);
    uint32_t viewers = 0;
    for (uint32_t slot = 1; slot <= owner->clients; ++slot) viewers += owner->viewers[slot].registry != 0;
    bool ok = qa_net_write_u32(&writer, 1) && qa_net_write_u64(&writer, owner->frame) &&
        qa_net_write_u64(&writer, owner->time_ns) && qa_net_write_u32(&writer, owner->source_capacity) &&
        qa_net_write_u32(&writer, owner->clients) && qa_net_write_u32(&writer, viewers);
    for (uint32_t slot = 1; ok && slot <= owner->clients; ++slot) if (owner->viewers[slot].registry)
        ok = qa_net_write_u32(&writer, slot) && actor_write(&writer, registry, owner->viewers[slot], error);
    if (ok) ok = qa_net_write_u32(&writer, (uint32_t)owner->count);
    for (size_t i = 0; ok && i < owner->count; ++i) {
        ok = qa_net_write_u32(&writer, owner->rows[i].source_slot) && actor_write(&writer, registry, owner->rows[i].actor, error);
        for (size_t word = 0; ok && word < 4; ++word) ok = qa_net_write_u64(&writer, owner->rows[i].visible[word]);
    }
    if (ok) { buffer.size = qa_net_writer_size(&writer); *out = buffer; }
    else qa_buffer_free(&buffer);
    return ok;
}

bool application_native_q2_visibility_restore(struct application_native_q2 *engine, qa_bytes bytes,
    const qa_source_frame *frame, const application_native_q2_client *clients, bool map_ready,
    application_native_q2_visibility **out, qa_error *error)
{
    if (!engine || !frame || !clients || !out || *out)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 visibility restore requires an empty actual Source candidate");
    if (!bytes.size) return true;
    if (!map_ready || engine->profile != QA_NATIVE_Q2_GAME_API2023)
        return application_fail(error, QA_ERROR_FORMAT, "Q2 visibility continuation has no matching API2023 GAME");
    application_native_q2_visibility *candidate = calloc(1, sizeof(*candidate));
    if (!candidate) return application_fail(error, QA_ERROR_MEMORY, "Restoring actual Q2 visibility decisions");
    qa_net_reader reader; qa_net_reader_init(&reader, bytes, error);
    uint32_t version = qa_net_read_u32(&reader);
    candidate->frame = qa_net_read_u64(&reader); candidate->time_ns = qa_net_read_u64(&reader);
    candidate->source_capacity = qa_net_read_u32(&reader); candidate->clients = qa_net_read_u32(&reader);
    uint32_t viewers = qa_net_read_u32(&reader);
    bool ok = !reader.failed && version == 1 && candidate->frame == frame->number && candidate->time_ns == frame->time_ns &&
        candidate->clients && candidate->clients <= 256 && candidate->source_capacity > candidate->clients &&
        candidate->source_capacity <= 65536 && viewers <= candidate->clients;
    const qa_actor_registry *registry = qa_session_actors(engine->provider->application->session);
    uint32_t previous = 0;
    for (uint32_t i = 0; ok && i < viewers; ++i) {
        uint32_t slot = qa_net_read_u32(&reader); qa_actor_id actor;
        ok = slot > previous && slot <= candidate->clients && actor_read(&reader, registry, &actor, error);
        if (ok) ok = clients[slot].connected && clients[slot].begun && !clients[slot].disconnect_started &&
            qa_actor_id_equal(clients[slot].actor, actor);
        if (ok) candidate->viewers[slot] = actor;
        previous = slot;
    }
    uint32_t count = qa_net_read_u32(&reader);
    if (ok) ok = !reader.failed && count < candidate->source_capacity && count <= qa_net_reader_remaining(&reader) / 48;
    if (ok && count) {
        candidate->rows = calloc(count, sizeof(*candidate->rows));
        if (!candidate->rows) ok = application_fail(error, QA_ERROR_MEMORY, "Restoring instanced Q2 entity visibility");
    }
    previous = 0;
    for (uint32_t i = 0; ok && i < count; ++i) {
        native_visibility_row *row = &candidate->rows[i];
        row->source_slot = qa_net_read_u32(&reader);
        ok = row->source_slot > previous && row->source_slot < candidate->source_capacity &&
            actor_read(&reader, registry, &row->actor, error);
        for (size_t word = 0; ok && word < 4; ++word) { row->visible[word] = qa_net_read_u64(&reader); ok = !reader.failed; }
        for (uint32_t slot = 1; ok && slot <= 256; ++slot)
            if (!candidate->viewers[slot].registry && (row->visible[(slot - 1) / 64] & (UINT64_C(1) << ((slot - 1) % 64)))) ok = false;
        previous = row->source_slot;
    }
    if (ok) ok = qa_net_reader_finish(&reader);
    if (ok) { candidate->count = candidate->capacity = count; *out = candidate; candidate = NULL; }
    if (!ok && (!error || error->code == QA_OK)) application_fail(error, QA_ERROR_FORMAT, "Q2 visibility continuation lost its real actors, viewers or frame");
    application_native_q2_visibility_destroy(&candidate);
    return ok;
}
