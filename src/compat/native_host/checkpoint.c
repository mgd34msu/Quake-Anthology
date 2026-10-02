#include "internal.h"
#include <math.h>

#define HOST_CHECKPOINT_VERSION 6u
#define HOST_CHECKPOINT_HEADER 72u
#define HOST_CHECKPOINT_SLOT 248u

typedef struct saved_slot {
    qa_native_slot_kind kind;
    uint32_t slot, owner, source_slot;
    qa_saved_actor_id actor;
    qa_vec3 creation_origin;
    uint64_t creation_frame;
    qa_native_host_q2_origin origins[8];
    bool creation_present, linked;
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
    if (!host || !host->instance || !out || host->destroying || host->restoring || host->reconstruction ||
        host->message_failed)
        return native_host_fail(error, QA_ERROR_ARGUMENT, 0,
                                "idle native host and checkpoint output are required");
    if ((host->engine.checkpoint == NULL) != (host->engine.restore == NULL) ||
        (host->engine.source_before == NULL) != (host->engine.source_after == NULL) ||
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
                 add_records(&total, host->message_reference_count, 24u, error) &&
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
    put_u64(&cursor, host->message_reference_count);
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
        const native_host_q2_lifetime *lifetime = slot < host->q2_lifetime_capacity ?
            &host->q2_lifetimes[slot] : NULL;
        bool present = lifetime && lifetime->present;
        bool linked = lifetime && lifetime->linked;
        if ((present || linked) && (!qa_actor_id_equal(lifetime->actor, binding.actor) ||
            !qa_vec_finite(lifetime->creation_origin) || (host->engine.source_frame &&
                lifetime->creation_frame > host->engine.source_frame(host->engine.context)))) {
            free(data); qa_buffer_free(&engine); qa_buffer_free(&bridge);
            return native_host_fail(error, QA_ERROR_FORMAT, slot, "Native Q2 creation continuation lost its Source actor");
        }
        put_u32(&cursor, (present ? 1u : 0u) | (linked ? 2u : 0u));
        put_u64(&cursor, present ? lifetime->creation_frame : 0u);
        const float components[3] = {present ? lifetime->creation_origin.x : 0,
            present ? lifetime->creation_origin.y : 0, present ? lifetime->creation_origin.z : 0};
        for (size_t axis = 0; axis < 3; ++axis) {
            uint32_t bits; memcpy(&bits, &components[axis], sizeof(bits)); put_u32(&cursor, bits);
        }
        for (size_t i = 0; i < 8; ++i) {
            qa_native_host_q2_origin origin = lifetime ? lifetime->origins[i] : (qa_native_host_q2_origin){0};
            if (!qa_vec_finite(origin.origin) || (origin.present ? (!present ||
                origin.source_frame < lifetime->creation_frame || (origin.source_frame & 7u) != i ||
                (host->engine.source_frame && origin.source_frame > host->engine.source_frame(host->engine.context))) :
                (origin.source_frame || origin.origin.x || origin.origin.y || origin.origin.z))) {
                free(data); qa_buffer_free(&engine); qa_buffer_free(&bridge);
                return native_host_fail(error, QA_ERROR_FORMAT, slot, "Native Q2 origin history lost its real Source link frame");
            }
            put_u32(&cursor, origin.present ? 1u : 0u);
            put_u64(&cursor, origin.source_frame);
            const float coordinates[3] = {origin.origin.x, origin.origin.y, origin.origin.z};
            for (size_t axis = 0; axis < 3; ++axis) {
                uint32_t bits; memcpy(&bits, &coordinates[axis], sizeof(bits)); put_u32(&cursor, bits);
            }
        }
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
    for (size_t i = 0; i < host->message_reference_count; ++i) {
        const qa_native_host_message_reference *reference = host->message_references + i;
        qa_saved_actor_id saved = {0};
        if (!actors || reference->offset > host->message_size || host->message_size - reference->offset < 2 ||
            (i && reference->offset < host->message_references[i - 1].offset + 2) ||
            !qa_actors_save_reference(actors, reference->actor, &saved, error)) {
            free(data); qa_buffer_free(&engine); qa_buffer_free(&bridge);
            if (!error || error->code == QA_OK)
                native_host_fail(error, QA_ERROR_FORMAT, i, "Native Q2 message lost its written Source entity reference");
            return false;
        }
        put_u64(&cursor, reference->offset);
        put_u32(&cursor, saved.slot);
        put_u32(&cursor, 0);
        put_u64(&cursor, saved.generation);
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

static bool apply_saved_cvars(qa_native_host *host, const saved_cvar *cvars,
                              uint32_t count, qa_error *error)
{
    for (uint32_t index = 0; index < count; ++index) {
        const qa_cvar_view *existing = qa_cvars_find(host->cvars, cvars[index].name);
        if (!existing && !qa_cvars_register(host->cvars, cvars[index].name, cvars[index].value,
            cvars[index].flags, host->world.owner, NULL, error)) return false;
        if (!qa_cvars_full_set(host->cvars, cvars[index].name, cvars[index].value,
            cvars[index].flags, error)) return false;
    }
    return native_host_refresh_cvars(host, error);
}

static bool restore_checkpoint(qa_native_host *host, qa_bytes state, bool cvars_only,
                                qa_error *error)
{
    if (!host || !host->instance || host->destroying || host->restoring || host->reconstruction ||
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
    uint64_t reference_count = take_u64(&cursor);
    if (version != HOST_CHECKPOINT_VERSION || profile != (uint32_t)host->profile ||
        q3_role != (uint32_t)host->q3_role || q3_abi != (uint32_t)host->q3_abi ||
        owner != host->world.owner || message_size > host->message_capacity ||
        engine_size > SIZE_MAX || bridge_size > SIZE_MAX ||
        reference_count > message_size / 2 || reference_count > SIZE_MAX / sizeof(qa_native_host_message_reference))
        return native_host_fail(error, QA_ERROR_FORMAT, 0,
                                "native host checkpoint identity or sizes are invalid");
    qa_native_entity_table table = {0};
    if (!cvars_only && (slot_count || retained_count) &&
        !qa_native_entity_table_get(host->instance, &table, error))
        return false;
    if ((!cvars_only && (slot_count > table.capacity || retained_count > table.capacity)) ||
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
        uint32_t lifetime_flags = take_u32(&cursor);
        uint32_t creation = lifetime_flags & 1u;
        slots[index].creation_present = creation != 0;
        slots[index].linked = (lifetime_flags & 2u) != 0;
        slots[index].creation_frame = take_u64(&cursor);
        slots[index].creation_origin = (qa_vec3){qa_load_f32le(cursor), qa_load_f32le(cursor + 4), qa_load_f32le(cursor + 8)};
        cursor += 12;
        bool history_valid = true;
        for (size_t i = 0; i < 8; ++i) {
            qa_native_host_q2_origin *origin = &slots[index].origins[i];
            uint32_t present = take_u32(&cursor);
            origin->present = present != 0;
            origin->source_frame = take_u64(&cursor);
            origin->origin = (qa_vec3){qa_load_f32le(cursor), qa_load_f32le(cursor + 4), qa_load_f32le(cursor + 8)};
            cursor += 12;
            if (present > 1 || !qa_vec_finite(origin->origin) || (present ? (!creation ||
                origin->source_frame < slots[index].creation_frame || (origin->source_frame & 7u) != i) :
                (origin->source_frame || origin->origin.x || origin->origin.y || origin->origin.z))) history_valid = false;
        }
        if (slots[index].kind == QA_NATIVE_SLOT_FREE ||
            slots[index].kind > QA_NATIVE_SLOT_BORROWED ||
            lifetime_flags > 3 || !history_valid || !qa_vec_finite(slots[index].creation_origin) ||
            (!creation && (slots[index].creation_frame || slots[index].creation_origin.x ||
                slots[index].creation_origin.y || slots[index].creation_origin.z)) ||
            ((creation || slots[index].linked) && (host->kind != NATIVE_HOST_Q2_GAME || !slots[index].slot)) ||
            (index && slots[index].slot <= slots[index - 1].slot) ||
            (!cvars_only && slots[index].slot >= table.capacity)) {
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
        if (!cvars_only && retained[index] >= table.capacity) {
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
    qa_native_host_message_reference *references = NULL;
    for (uint32_t index = 0; index < cvar_count; ++index) {
        if (!span(cursor, end, 12))
            goto truncated;
        uint32_t name = take_u32(&cursor), value = take_u32(&cursor);
        cvars[index].flags = take_u32(&cursor);
        size_t remaining = (size_t)(end - cursor);
        size_t name_size = (size_t)name + 1u, value_size = (size_t)value + 1u;
        if (!name || name > remaining || value > remaining - name ||
            name_size <= name || value_size <= value ||
            memchr(cursor, 0, name) || memchr(cursor + name, 0, value))
            goto truncated;
        cvars[index].name = malloc(name_size);
        cvars[index].value = malloc(value_size);
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
    if (reference_count > remaining / 24u) goto truncated;
    remaining -= (size_t)reference_count * 24u;
    if (engine_size > remaining)
        goto truncated;
    remaining -= (size_t)engine_size;
    if (bridge_size != remaining)
        goto truncated;
    qa_bytes message = {cursor, (size_t)message_size};
    cursor += message.size;
    if (!cvars_only && reference_count) {
        references = calloc((size_t)reference_count, sizeof(*references));
        if (!references) {
            free(slots); free(retained); free_saved_cvars(cvars, cvar_count);
            return native_host_fail(error, QA_ERROR_MEMORY, 0, "Restoring written native Q2 message entity references");
        }
    }
    const qa_actor_registry *actors = host->world.session ? qa_session_actors(host->world.session) : NULL;
    uint64_t previous_offset = 0;
    for (size_t i = 0; i < (size_t)reference_count; ++i) {
        uint64_t offset = take_u64(&cursor);
        qa_saved_actor_id saved = {.slot = take_u32(&cursor)};
        uint32_t reserved = take_u32(&cursor);
        saved.generation = take_u64(&cursor);
        if (reserved || offset > message_size || message_size - offset < 2 ||
            (i && offset < previous_offset + 2) || !saved.generation) goto truncated;
        previous_offset = offset;
        if (!cvars_only) {
            references[i].offset = (size_t)offset;
            if (!actors || !qa_actors_reference_saved(actors, saved, true, &references[i].actor, error)) {
                free(slots); free(retained); free(references); free_saved_cvars(cvars, cvar_count);
                return false;
            }
        }
    }
    qa_bytes engine = {cursor, (size_t)engine_size};
    cursor += engine.size;
    qa_bytes bridge = {cursor, (size_t)bridge_size};
    host->restoring = true;
    if (cvars_only) {
        bool ok = apply_saved_cvars(host, cvars, cvar_count, error);
        host->restoring = false;
        free(slots);
        free(retained);
        free_saved_cvars(cvars, cvar_count);
        return ok;
    }
    bool ok = true;
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
    if (ok && host->kind == NATIVE_HOST_Q2_GAME && table.capacity) {
        native_host_q2_lifetime *values = calloc(table.capacity, sizeof(*values));
        if (!values) ok = native_host_fail(error, QA_ERROR_MEMORY, 0, "Restoring native Q2 Source creation metadata");
        for (uint32_t index = 0; ok && index < slot_count; ++index) {
            const saved_slot *saved = &slots[index];
            if (!saved->creation_present && !saved->linked) continue;
            const qa_actor_record *record = qa_actors_resolve_saved(actors, saved->actor);
            if (!record || values[saved->slot].present) {
                ok = native_host_fail(error, QA_ERROR_FORMAT, saved->slot, "Native Q2 creation continuation aliases its Source slot");
                break;
            }
            values[saved->slot] = (native_host_q2_lifetime){.actor = record->id,
                .creation_origin = saved->creation_origin, .creation_frame = saved->creation_frame,
                .present = saved->creation_present, .linked = saved->linked};
            memcpy(values[saved->slot].origins, saved->origins, sizeof(saved->origins));
        }
        if (ok) {
            free(host->q2_lifetimes); host->q2_lifetimes = values; host->q2_lifetime_capacity = table.capacity;
        } else free(values);
    }
    if (ok) ok = apply_saved_cvars(host, cvars, cvar_count, error);
    if (ok) {
        memcpy(host->message, message.data, message.size);
        host->message_size = message.size;
        free(host->message_references);
        host->message_references = references; references = NULL;
        host->message_reference_count = host->message_reference_capacity = (size_t)reference_count;
        host->message_failed = false;
    }
    if (ok && engine.size) {
        if (!host->engine.restore)
            ok = native_host_fail(error, QA_ERROR_UNSUPPORTED, 0,
                                  "native engine checkpoint restore is unbound");
        else
            ok = host->engine.restore(host->engine.context, engine, error);
    }
    if (ok && host->engine.source_frame) {
        uint64_t frame = host->engine.source_frame(host->engine.context);
        for (size_t slot = 1; ok && slot < host->q2_lifetime_capacity; ++slot) {
            const native_host_q2_lifetime *lifetime = &host->q2_lifetimes[slot];
            if (lifetime->present && lifetime->creation_frame > frame)
                ok = native_host_fail(error, QA_ERROR_FORMAT, slot, "Native Q2 creation exceeds its restored Source clock");
            for (size_t i = 0; ok && i < 8; ++i)
                if (lifetime->origins[i].present && lifetime->origins[i].source_frame > frame)
                    ok = native_host_fail(error, QA_ERROR_FORMAT, slot, "Native Q2 link history exceeds its restored Source clock");
        }
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
    free(references);
    free_saved_cvars(cvars, cvar_count);
    return ok;

truncated:
    free(slots);
    free(retained);
    free(references);
    free_saved_cvars(cvars, cvar_count);
    return native_host_fail(error, QA_ERROR_FORMAT, (size_t)(cursor - state.data),
                            "native host checkpoint payload is truncated");
}

bool qa_native_host_restore(qa_native_host *host, qa_bytes state, qa_error *error)
{
    if (!host || host->callback_depth)
        return native_host_fail(error, QA_ERROR_ARGUMENT, 0, "native host restore requires an idle callback owner");
    ++host->callback_depth;
    bool ok = restore_checkpoint(host, state, false, error);
    --host->callback_depth;
    return ok;
}

bool qa_native_host_restore_cvars(qa_native_host *host, qa_bytes state, qa_error *error)
{
    if (!host || host->callback_depth || host->kind != NATIVE_HOST_Q2_GAME ||
        !host->cvars || !host->instance ||
        qa_native_get_lifecycle(host->instance) != QA_NATIVE_LOADED)
        return native_host_fail(error, QA_ERROR_ARGUMENT, 0,
            "native cvar import requires its idle loaded original Q2 GAME owner");
    ++host->callback_depth;
    bool ok = restore_checkpoint(host, state, true, error);
    --host->callback_depth;
    return ok;
}
