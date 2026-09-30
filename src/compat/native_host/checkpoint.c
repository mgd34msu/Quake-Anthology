#include "internal.h"

#define HOST_CHECKPOINT_VERSION 2u
#define HOST_CHECKPOINT_HEADER 64u
#define HOST_CHECKPOINT_SLOT 32u

typedef struct saved_slot {
    qa_native_slot_kind kind;
    uint32_t slot, owner, source_slot;
    qa_saved_actor_id actor;
} saved_slot;

typedef struct saved_cvar {
    char *name, *value;
    uint32_t flags;
} saved_cvar;

static bool add_size(size_t *total, size_t amount, qa_error *error)
{
    if (amount > SIZE_MAX - *total)
        return native_host_fail(error, QA_ERROR_MEMORY, *total,
                                "native host checkpoint size overflows");
    *total += amount;
    return true;
}

static bool add_records(size_t *total, size_t count, size_t stride, qa_error *error)
{
    if (count > SIZE_MAX / stride)
        return native_host_fail(error, QA_ERROR_MEMORY, count,
                                "native host checkpoint record count overflows");
    return add_size(total, count * stride, error);
}

static void put_u32(uint8_t **cursor, uint32_t value)
{
    qa_store_u32le(*cursor, value);
    *cursor += 4;
}

static void put_u64(uint8_t **cursor, uint64_t value)
{
    qa_store_u64le(*cursor, value);
    *cursor += 8;
}

static uint32_t take_u32(const uint8_t **cursor)
{
    uint32_t value = qa_load_u32le(*cursor);
    *cursor += 4;
    return value;
}

static uint64_t take_u64(const uint8_t **cursor)
{
    uint64_t value = qa_load_u64le(*cursor);
    *cursor += 8;
    return value;
}

static void free_saved_cvars(saved_cvar *cvars, size_t count)
{
    if (!cvars)
        return;
    for (size_t index = 0; index < count; ++index) {
        free(cvars[index].name);
        free(cvars[index].value);
    }
    free(cvars);
}

static bool capture_checkpoint(qa_native_host *host, qa_buffer *out, qa_error *error)
{
    if (!host || !host->instance || !out || host->destroying || host->restoring ||
        host->message_failed)
        return native_host_fail(error, QA_ERROR_ARGUMENT, 0,
                                "idle native host and checkpoint output are required");
    if ((host->engine.checkpoint == NULL) != (host->engine.restore == NULL) ||
        (host->q3.checkpoint == NULL) != (host->q3.restore == NULL))
        return native_host_fail(error, QA_ERROR_ARGUMENT, 0,
                                "native host checkpoint services must be paired");
    qa_buffer engine = {0}, bridge = {0};
    if (host->engine.checkpoint &&
        !host->engine.checkpoint(host->engine.context, &engine, error))
        return false;
    if (host->q3.checkpoint && !host->q3.checkpoint(host->q3.context, &bridge, error)) {
        qa_buffer_free(&engine);
        return false;
    }
    qa_native_entity_table table = {0};
    if ((host->kind == NATIVE_HOST_Q2_GAME || host->kind == NATIVE_HOST_Q3) &&
        !qa_native_entity_table_get(host->instance, &table, error)) {
        qa_buffer_free(&engine);
        qa_buffer_free(&bridge);
        return false;
    }
    uint32_t slot_count = 0, retained_count = 0;
    for (uint32_t slot = 0; slot < table.capacity; ++slot) {
        qa_native_slot_binding binding;
        if (!qa_native_slot(host->instance, slot, &binding, error)) {
            qa_buffer_free(&engine);
            qa_buffer_free(&bridge);
            return false;
        }
        if (binding.kind != QA_NATIVE_SLOT_FREE)
            ++slot_count;
        if (slot < host->retained_capacity && host->retained_clients[slot])
            ++retained_count;
    }
    size_t cvar_count = host->cvars ? qa_cvars_count(host->cvars) : 0;
    if (cvar_count > UINT32_MAX) {
        qa_buffer_free(&engine);
        qa_buffer_free(&bridge);
        return native_host_fail(error, QA_ERROR_MEMORY, cvar_count,
                                "native host has too many cvars to checkpoint");
    }
    size_t total = HOST_CHECKPOINT_HEADER;
    bool sized = add_records(&total, slot_count, HOST_CHECKPOINT_SLOT, error) &&
                 add_records(&total, retained_count, 4u, error) &&
                 add_size(&total, host->message_size, error) &&
                 add_size(&total, engine.size, error) && add_size(&total, bridge.size, error);
    for (size_t index = 0; sized && index < cvar_count; ++index) {
        const qa_cvar_view *view = qa_cvars_at(host->cvars, index);
        size_t name = strlen(view->name), value = strlen(view->value);
        if (name > UINT32_MAX || value > UINT32_MAX)
            sized = native_host_fail(error, QA_ERROR_MEMORY, index,
                                     "native cvar text exceeds checkpoint limits");
        else
            sized = add_size(&total, 12u, error) && add_size(&total, name, error) &&
                    add_size(&total, value, error);
    }
    if (!sized) {
        qa_buffer_free(&engine);
        qa_buffer_free(&bridge);
        return false;
    }
    uint8_t *data = malloc(total);
    if (!data) {
        qa_buffer_free(&engine);
        qa_buffer_free(&bridge);
        return native_host_fail(error, QA_ERROR_MEMORY, total,
                                "allocating native host checkpoint");
    }
    uint8_t *cursor = data;
    memcpy(cursor, "QANHST\0\0", 8);
    cursor += 8;
    put_u32(&cursor, HOST_CHECKPOINT_VERSION);
    put_u32(&cursor, (uint32_t)host->profile);
    put_u32(&cursor, host->world.owner);
    put_u32(&cursor, slot_count);
    put_u32(&cursor, retained_count);
    put_u32(&cursor, (uint32_t)cvar_count);
    put_u64(&cursor, host->message_size);
    put_u64(&cursor, engine.size);
    put_u64(&cursor, bridge.size);
    put_u32(&cursor, (uint32_t)host->q3_role);
    put_u32(&cursor, (uint32_t)host->q3_abi);
    const qa_actor_registry *actors = host->world.session
                                         ? qa_session_actors(host->world.session)
                                         : NULL;
    for (uint32_t slot = 0; slot < table.capacity; ++slot) {
        qa_native_slot_binding binding;
        qa_native_slot(host->instance, slot, &binding, NULL);
        if (binding.kind == QA_NATIVE_SLOT_FREE)
            continue;
        qa_saved_actor_id saved = {0};
        if (actors && !qa_actors_save_reference(actors, binding.actor, &saved, error)) {
            free(data);
            qa_buffer_free(&engine);
            qa_buffer_free(&bridge);
            return false;
        }
        put_u32(&cursor, (uint32_t)binding.kind);
        put_u32(&cursor, binding.slot);
        put_u32(&cursor, binding.owner);
        put_u32(&cursor, binding.source_slot);
        put_u32(&cursor, saved.slot);
        put_u32(&cursor, 0);
        put_u64(&cursor, saved.generation);
    }
    for (uint32_t slot = 0; slot < table.capacity && slot < host->retained_capacity; ++slot)
        if (host->retained_clients[slot])
            put_u32(&cursor, slot);
    for (size_t index = 0; index < cvar_count; ++index) {
        const qa_cvar_view *view = qa_cvars_at(host->cvars, index);
        uint32_t name = (uint32_t)strlen(view->name);
        uint32_t value = (uint32_t)strlen(view->value);
        put_u32(&cursor, name);
        put_u32(&cursor, value);
        put_u32(&cursor, view->flags);
        memcpy(cursor, view->name, name);
        cursor += name;
        memcpy(cursor, view->value, value);
        cursor += value;
    }
    if (host->message_size) {
        memcpy(cursor, host->message, host->message_size);
        cursor += host->message_size;
    }
    if (engine.size) {
        memcpy(cursor, engine.data, engine.size);
        cursor += engine.size;
    }
    if (bridge.size)
        memcpy(cursor, bridge.data, bridge.size);
    qa_buffer_free(&engine);
    qa_buffer_free(&bridge);
    *out = (qa_buffer){data, total};
    return true;
}

bool qa_native_host_checkpoint(qa_native_host *host, qa_buffer *out, qa_error *error)
{
    if (!host || host->callback_depth)
        return native_host_fail(error, QA_ERROR_ARGUMENT, 0, "native host checkpoint requires an idle callback owner");
    ++host->callback_depth;
    bool ok = capture_checkpoint(host, out, error);
    --host->callback_depth;
    return ok;
}

static bool span(const uint8_t *cursor, const uint8_t *end, size_t size)
{
    return size <= (size_t)(end - cursor);
}

static bool restore_checkpoint(qa_native_host *host, qa_bytes state, qa_error *error)
{
    if (!host || !host->instance || host->destroying || host->restoring ||
        !state.data || state.size < HOST_CHECKPOINT_HEADER)
        return native_host_fail(error, QA_ERROR_ARGUMENT, state.size,
                                "native host checkpoint is truncated or host is busy");
    const uint8_t *cursor = state.data, *end = state.data + state.size;
    if (memcmp(cursor, "QANHST\0\0", 8))
        return native_host_fail(error, QA_ERROR_FORMAT, 0,
                                "native host checkpoint magic is invalid");
    cursor += 8;
    uint32_t version = take_u32(&cursor);
    uint32_t profile = take_u32(&cursor);
    uint32_t owner = take_u32(&cursor);
    uint32_t slot_count = take_u32(&cursor);
    uint32_t retained_count = take_u32(&cursor);
    uint32_t cvar_count = take_u32(&cursor);
    uint64_t message_size = take_u64(&cursor);
    uint64_t engine_size = take_u64(&cursor);
    uint64_t bridge_size = take_u64(&cursor);
    uint32_t q3_role = take_u32(&cursor);
    uint32_t q3_abi = take_u32(&cursor);
    if (version != HOST_CHECKPOINT_VERSION || profile != (uint32_t)host->profile ||
        q3_role != (uint32_t)host->q3_role || q3_abi != (uint32_t)host->q3_abi ||
        owner != host->world.owner || message_size > host->message_capacity ||
        engine_size > SIZE_MAX || bridge_size > SIZE_MAX)
        return native_host_fail(error, QA_ERROR_FORMAT, 0,
                                "native host checkpoint identity or sizes are invalid");
    qa_native_entity_table table = {0};
    if ((slot_count || retained_count) &&
        !qa_native_entity_table_get(host->instance, &table, error))
        return false;
    if (slot_count > table.capacity || retained_count > table.capacity ||
        slot_count > (size_t)(end - cursor) / HOST_CHECKPOINT_SLOT)
        return native_host_fail(error, QA_ERROR_FORMAT, 0,
                                "native host checkpoint slot table is invalid");
    saved_slot *slots = slot_count ? calloc(slot_count, sizeof(*slots)) : NULL;
    if (slot_count && !slots)
        return native_host_fail(error, QA_ERROR_MEMORY, 0,
                                "allocating restored native slots");
    for (uint32_t index = 0; index < slot_count; ++index) {
        slots[index].kind = (qa_native_slot_kind)take_u32(&cursor);
        slots[index].slot = take_u32(&cursor);
        slots[index].owner = take_u32(&cursor);
        slots[index].source_slot = take_u32(&cursor);
        slots[index].actor.slot = take_u32(&cursor);
        take_u32(&cursor);
        slots[index].actor.generation = take_u64(&cursor);
        if (slots[index].kind == QA_NATIVE_SLOT_FREE ||
            slots[index].kind > QA_NATIVE_SLOT_BORROWED ||
            slots[index].slot >= table.capacity) {
            free(slots);
            return native_host_fail(error, QA_ERROR_FORMAT, index,
                                    "native host checkpoint contains an invalid slot");
        }
    }
    if (retained_count > (size_t)(end - cursor) / 4u) {
        free(slots);
        return native_host_fail(error, QA_ERROR_FORMAT, 0,
                                "native host retained-client set is truncated");
    }
    uint32_t *retained = retained_count ? calloc(retained_count, sizeof(*retained)) : NULL;
    if (retained_count && !retained) {
        free(slots);
        return native_host_fail(error, QA_ERROR_MEMORY, 0,
                                "allocating restored retained-client set");
    }
    for (uint32_t index = 0; index < retained_count; ++index) {
        retained[index] = take_u32(&cursor);
        if (retained[index] >= table.capacity) {
            free(slots);
            free(retained);
            return native_host_fail(error, QA_ERROR_FORMAT, index,
                                    "native host retained-client slot is invalid");
        }
    }
    if (cvar_count > (size_t)(end - cursor) / 12u) {
        free(slots);
        free(retained);
        return native_host_fail(error, QA_ERROR_FORMAT, 0,
                                "native host cvar records are truncated");
    }
    saved_cvar *cvars = cvar_count ? calloc(cvar_count, sizeof(*cvars)) : NULL;
    if (cvar_count && !cvars) {
        free(slots);
        free(retained);
        return native_host_fail(error, QA_ERROR_MEMORY, 0,
                                "allocating restored native cvars");
    }
    for (uint32_t index = 0; index < cvar_count; ++index) {
        if (!span(cursor, end, 12))
            goto truncated;
        uint32_t name = take_u32(&cursor), value = take_u32(&cursor);
        cvars[index].flags = take_u32(&cursor);
        size_t remaining = (size_t)(end - cursor);
        if (!name || name > remaining || value > remaining - name ||
            (size_t)name == SIZE_MAX || (size_t)value == SIZE_MAX)
            goto truncated;
        cvars[index].name = malloc((size_t)name + 1u);
        cvars[index].value = malloc((size_t)value + 1u);
        if (!cvars[index].name || !cvars[index].value) {
            free(slots);
            free(retained);
            free_saved_cvars(cvars, cvar_count);
            return native_host_fail(error, QA_ERROR_MEMORY, index,
                                    "allocating restored native cvar text");
        }
        memcpy(cvars[index].name, cursor, name);
        cvars[index].name[name] = 0;
        cursor += name;
        memcpy(cvars[index].value, cursor, value);
        cvars[index].value[value] = 0;
        cursor += value;
    }
    size_t remaining = (size_t)(end - cursor);
    if (message_size > remaining)
        goto truncated;
    remaining -= (size_t)message_size;
    if (engine_size > remaining)
        goto truncated;
    remaining -= (size_t)engine_size;
    if (bridge_size != remaining)
        goto truncated;
    qa_bytes message = {cursor, (size_t)message_size};
    cursor += message.size;
    qa_bytes engine = {cursor, (size_t)engine_size};
    cursor += engine.size;
    qa_bytes bridge = {cursor, (size_t)bridge_size};
    host->restoring = true;
    bool ok = true;
    const qa_actor_registry *actors = host->world.session
                                         ? qa_session_actors(host->world.session)
                                         : NULL;
    if (slot_count && !actors)
        ok = native_host_fail(error, QA_ERROR_ARGUMENT, 0,
                              "native slot restore requires the shared actor registry");
    for (uint32_t index = 0; ok && index < slot_count; ++index) {
        const qa_actor_record *record = qa_actors_resolve_saved(actors, slots[index].actor);
        if (!record) {
            ok = native_host_fail(error, QA_ERROR_NOT_FOUND, slots[index].slot,
                                  "native checkpoint actor is not live after world restore");
            break;
        }
        qa_native_slot_binding binding = {
            .kind = slots[index].kind,
            .slot = slots[index].slot,
            .actor = record->id,
            .owner = slots[index].owner,
            .source_slot = slots[index].source_slot};
        ok = qa_native_bind_slot(host->instance, &binding, error);
    }
    if (ok && table.capacity) {
        bool *values = calloc(table.capacity, sizeof(*values));
        if (!values)
            ok = native_host_fail(error, QA_ERROR_MEMORY, 0,
                                  "allocating restored native retained clients");
        else {
            for (uint32_t index = 0; index < retained_count; ++index)
                values[retained[index]] = true;
            free(host->retained_clients);
            host->retained_clients = values;
            host->retained_capacity = table.capacity;
        }
    }
    for (uint32_t index = 0; ok && index < cvar_count; ++index) {
        const qa_cvar_view *existing = qa_cvars_find(host->cvars, cvars[index].name);
        if (!existing)
            ok = qa_cvars_register(host->cvars, cvars[index].name, cvars[index].value,
                                   cvars[index].flags, host->world.owner, NULL, error);
        if (ok)
            ok = qa_cvars_full_set(host->cvars, cvars[index].name, cvars[index].value,
                                   cvars[index].flags, error);
    }
    if (ok && !native_host_refresh_cvars(host, error))
        ok = false;
    if (ok) {
        memcpy(host->message, message.data, message.size);
        host->message_size = message.size;
        host->message_failed = false;
    }
    if (ok && engine.size) {
        if (!host->engine.restore)
            ok = native_host_fail(error, QA_ERROR_UNSUPPORTED, 0,
                                  "native engine checkpoint restore is unbound");
        else
            ok = host->engine.restore(host->engine.context, engine, error);
    }
    if (ok && bridge.size) {
        if (!host->q3.restore)
            ok = native_host_fail(error, QA_ERROR_UNSUPPORTED, 0,
                                  "native Q3 bridge checkpoint restore is unbound");
        else
            ok = host->q3.restore(host->q3.context, bridge, error);
    }
    host->restoring = false;
    free(slots);
    free(retained);
    free_saved_cvars(cvars, cvar_count);
    return ok;

truncated:
    free(slots);
    free(retained);
    free_saved_cvars(cvars, cvar_count);
    return native_host_fail(error, QA_ERROR_FORMAT, (size_t)(cursor - state.data),
                            "native host checkpoint payload is truncated");
}

bool qa_native_host_restore(qa_native_host *host, qa_bytes state, qa_error *error)
{
    if (!host || host->callback_depth)
        return native_host_fail(error, QA_ERROR_ARGUMENT, 0, "native host restore requires an idle callback owner");
    ++host->callback_depth;
    bool ok = restore_checkpoint(host, state, error);
    --host->callback_depth;
    return ok;
}
