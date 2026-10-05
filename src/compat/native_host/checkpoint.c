#include "internal.h"
#include <math.h>
#include "qa/cvars_save.h"
#include "qa/native_process.h"

#define HOST_CHECKPOINT_HEADER 80u
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

static void put_u32(qa_source_save_io *io, uint32_t value)
{
    qa_source_save_u32(io, &value);
}

static void put_u64(qa_source_save_io *io, uint64_t value)
{
    qa_source_save_u64(io, &value);
}

static uint32_t take_u32(qa_source_save_io *io)
{
    uint32_t value = 0;
    qa_source_save_u32(io, &value);
    return value;
}

static uint64_t take_u64(qa_source_save_io *io)
{
    uint64_t value = 0;
    qa_source_save_u64(io, &value);
    return value;
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
    qa_buffer engine = {0}, bridge = {0}, registry = {0}, memory = {0};
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
        qa_buffer_free(&bridge); qa_buffer_free(&registry); qa_buffer_free(&memory);
        return false;
    }
    uint32_t slot_count = 0, retained_count = 0;
    for (uint32_t slot = 0; slot < table.capacity; ++slot) {
        qa_native_slot_binding binding;
        if (!qa_native_slot(host->instance, slot, &binding, error)) {
            qa_buffer_free(&engine);
            qa_buffer_free(&bridge); qa_buffer_free(&registry); qa_buffer_free(&memory);
            return false;
        }
        if (binding.kind != QA_NATIVE_SLOT_FREE)
            ++slot_count;
        if (slot < host->retained_capacity && host->retained_clients[slot])
            ++retained_count;
    }
    native_host_memory_state objects={host->strings,host->cvar_shadows};
    qa_source_save_io objects_io={0};
    bool captured=(!host->cvars || qa_cvars_save_capture(host->cvars,&registry,error)) &&
        qa_source_save_writer(&objects_io,NULL,error) && native_host_memory_fields(&objects_io,host,&objects,true) &&
        qa_source_save_finish(&objects_io,&memory);
    qa_source_save_dispose(&objects_io);
    if (!captured) {
        qa_buffer_free(&engine); qa_buffer_free(&bridge); qa_buffer_free(&registry); qa_buffer_free(&memory); return false;
    }
    size_t total = HOST_CHECKPOINT_HEADER;
    bool sized = add_records(&total, slot_count, HOST_CHECKPOINT_SLOT, error) &&
                 add_records(&total, retained_count, 4u, error) &&
                 add_records(&total, host->message_reference_count, 24u, error) &&
                 add_size(&total, host->message_size, error) &&
                 add_size(&total, engine.size, error) && add_size(&total, bridge.size, error) &&
                 add_size(&total, registry.size,error) && add_size(&total,memory.size,error);
    if (!sized) {
        qa_buffer_free(&engine);
        qa_buffer_free(&bridge); qa_buffer_free(&registry); qa_buffer_free(&memory);
        return false;
    }
    qa_source_save_io io = {0};
    if (!qa_source_save_writer(&io, NULL, error) ||
        !qa_source_save_writer_reserve(&io, total)) {
        qa_source_save_dispose(&io);
        qa_buffer_free(&engine);
        qa_buffer_free(&bridge); qa_buffer_free(&registry); qa_buffer_free(&memory);
        return native_host_fail(error, QA_ERROR_MEMORY, total,
                                "allocating native host checkpoint");
    }
    qa_source_save_bytes(&io, "QANHST\0\0", 8);
    put_u32(&io, (uint32_t)host->profile);
    put_u32(&io, host->world.owner);
    put_u32(&io, slot_count);
    put_u32(&io, retained_count);
    put_u64(&io, registry.size);
    put_u64(&io, host->message_size);
    put_u64(&io, engine.size);
    put_u64(&io, bridge.size);
    put_u32(&io, (uint32_t)host->q3_role);
    put_u32(&io, (uint32_t)host->q3_abi);
    put_u64(&io, host->message_reference_count);
    put_u64(&io, memory.size);
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
            qa_source_save_dispose(&io);
            qa_buffer_free(&engine);
            qa_buffer_free(&bridge); qa_buffer_free(&registry); qa_buffer_free(&memory);
            return false;
        }
        put_u32(&io, (uint32_t)binding.kind);
        put_u32(&io, binding.slot);
        put_u32(&io, binding.owner);
        put_u32(&io, binding.source_slot);
        put_u32(&io, saved.slot);
        put_u32(&io, 0);
        put_u64(&io, saved.generation);
        const native_host_q2_lifetime *lifetime = slot < host->q2_lifetime_capacity ?
            &host->q2_lifetimes[slot] : NULL;
        bool present = lifetime && lifetime->present;
        bool linked = lifetime && lifetime->linked;
        if ((present || linked) && (!qa_actor_id_equal(lifetime->actor, binding.actor) ||
            !qa_vec_finite(lifetime->creation_origin) || (host->engine.source_frame &&
                lifetime->creation_frame > host->engine.source_frame(host->engine.context)))) {
            qa_source_save_dispose(&io); qa_buffer_free(&engine); qa_buffer_free(&bridge); qa_buffer_free(&registry); qa_buffer_free(&memory);
            return native_host_fail(error, QA_ERROR_FORMAT, slot, "Native Q2 creation continuation lost its Source actor");
        }
        put_u32(&io, (present ? 1u : 0u) | (linked ? 2u : 0u));
        put_u64(&io, present ? lifetime->creation_frame : 0u);
        const float components[3] = {present ? lifetime->creation_origin.x : 0,
            present ? lifetime->creation_origin.y : 0, present ? lifetime->creation_origin.z : 0};
        for (size_t axis = 0; axis < 3; ++axis) {
            uint32_t bits; memcpy(&bits, &components[axis], sizeof(bits)); put_u32(&io, bits);
        }
        for (size_t i = 0; i < 8; ++i) {
            qa_native_host_q2_origin origin = lifetime ? lifetime->origins[i] : (qa_native_host_q2_origin){0};
            if (!qa_vec_finite(origin.origin) || (origin.present ? (!present ||
                origin.source_frame < lifetime->creation_frame || (origin.source_frame & 7u) != i ||
                (host->engine.source_frame && origin.source_frame > host->engine.source_frame(host->engine.context))) :
                (origin.source_frame || origin.origin.x != 0 || origin.origin.y != 0 || origin.origin.z != 0))) {
                qa_source_save_dispose(&io); qa_buffer_free(&engine); qa_buffer_free(&bridge); qa_buffer_free(&registry); qa_buffer_free(&memory);
                return native_host_fail(error, QA_ERROR_FORMAT, slot, "Native Q2 origin history lost its real Source link frame");
            }
            put_u32(&io, origin.present ? 1u : 0u);
            put_u64(&io, origin.source_frame);
            const float coordinates[3] = {origin.origin.x, origin.origin.y, origin.origin.z};
            for (size_t axis = 0; axis < 3; ++axis) {
                uint32_t bits; memcpy(&bits, &coordinates[axis], sizeof(bits)); put_u32(&io, bits);
            }
        }
    }
    for (uint32_t slot = 0; slot < table.capacity && slot < host->retained_capacity; ++slot)
        if (host->retained_clients[slot])
            put_u32(&io, slot);
    qa_source_save_bytes(&io, registry.data, registry.size);
    qa_source_save_bytes(&io, memory.data, memory.size);
    qa_source_save_bytes(&io, host->message, host->message_size);
    for (size_t i = 0; i < host->message_reference_count; ++i) {
        const qa_native_host_message_reference *reference = host->message_references + i;
        qa_saved_actor_id saved = {0};
        if (!actors || reference->offset > host->message_size || host->message_size - reference->offset < 2 ||
            (i && reference->offset < host->message_references[i - 1].offset + 2) ||
            !qa_actors_save_reference(actors, reference->actor, &saved, error)) {
            qa_source_save_dispose(&io); qa_buffer_free(&engine); qa_buffer_free(&bridge); qa_buffer_free(&registry); qa_buffer_free(&memory);
            if (!error || error->code == QA_OK)
                native_host_fail(error, QA_ERROR_FORMAT, i, "Native Q2 message lost its written Source entity reference");
            return false;
        }
        put_u64(&io, reference->offset);
        put_u32(&io, saved.slot);
        put_u32(&io, 0);
        put_u64(&io, saved.generation);
    }
    qa_source_save_bytes(&io, engine.data, engine.size);
    qa_source_save_bytes(&io, bridge.data, bridge.size);
    qa_buffer_free(&engine);
    qa_buffer_free(&bridge); qa_buffer_free(&registry); qa_buffer_free(&memory);
    bool finished = qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    return finished;
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

static bool restore_checkpoint(qa_native_host *host, qa_bytes state, bool cvars_only,
                                qa_error *error)
{
    if (!host || !host->instance || host->destroying || host->restoring || host->reconstruction ||
        !state.data || state.size < HOST_CHECKPOINT_HEADER)
        return native_host_fail(error, QA_ERROR_ARGUMENT, state.size,
                                "native host checkpoint is truncated or host is busy");
    qa_source_save_io io = {0};
    qa_bytes magic;
    if (!qa_source_save_reader(&io, NULL, state, error) ||
        !qa_source_save_span(&io, 8, &magic))
        return false;
    if (memcmp(magic.data, "QANHST\0\0", 8))
        return native_host_fail(error, QA_ERROR_FORMAT, 0,
                                "native host checkpoint magic is invalid");
    uint32_t profile = take_u32(&io);
    uint32_t owner = take_u32(&io);
    uint32_t slot_count = take_u32(&io);
    uint32_t retained_count = take_u32(&io);
    uint64_t registry_size = take_u64(&io);
    uint64_t message_size = take_u64(&io);
    uint64_t engine_size = take_u64(&io);
    uint64_t bridge_size = take_u64(&io);
    uint32_t q3_role = take_u32(&io);
    uint32_t q3_abi = take_u32(&io);
    uint64_t reference_count = take_u64(&io);
    uint64_t memory_size = take_u64(&io);
    if (profile != (uint32_t)host->profile ||
        q3_role != (uint32_t)host->q3_role || q3_abi != (uint32_t)host->q3_abi ||
        owner != host->world.owner || message_size > host->message_capacity ||
        engine_size > SIZE_MAX || bridge_size > SIZE_MAX || registry_size > SIZE_MAX || memory_size > SIZE_MAX ||
        ((registry_size!=0)!=(host->cvars!=NULL)) ||
        reference_count > message_size / 2 || reference_count > SIZE_MAX / sizeof(qa_native_host_message_reference))
        return native_host_fail(error, QA_ERROR_FORMAT, 0,
                                "native host checkpoint identity or sizes are invalid");
    qa_native_entity_table table = {0};
    if (!cvars_only && (slot_count || retained_count) &&
        !qa_native_entity_table_get(host->instance, &table, error))
        return false;
    if ((!cvars_only && (slot_count > table.capacity || retained_count > table.capacity)) ||
        slot_count > (io.input.size - io.offset) / HOST_CHECKPOINT_SLOT)
        return native_host_fail(error, QA_ERROR_FORMAT, 0,
                                "native host checkpoint slot table is invalid");
    saved_slot *slots = slot_count ? calloc(slot_count, sizeof(*slots)) : NULL;
    if (slot_count && !slots)
        return native_host_fail(error, QA_ERROR_MEMORY, 0,
                                "allocating restored native slots");
    for (uint32_t index = 0; index < slot_count; ++index) {
        slots[index].kind = (qa_native_slot_kind)take_u32(&io);
        slots[index].slot = take_u32(&io);
        slots[index].owner = take_u32(&io);
        slots[index].source_slot = take_u32(&io);
        slots[index].actor.slot = take_u32(&io);
        take_u32(&io);
        slots[index].actor.generation = take_u64(&io);
        uint32_t lifetime_flags = take_u32(&io);
        uint32_t creation = lifetime_flags & 1u;
        slots[index].creation_present = creation != 0;
        slots[index].linked = (lifetime_flags & 2u) != 0;
        slots[index].creation_frame = take_u64(&io);
        qa_source_save_vec3(&io, &slots[index].creation_origin);
        bool history_valid = true;
        for (size_t i = 0; i < 8; ++i) {
            qa_native_host_q2_origin *origin = &slots[index].origins[i];
            uint32_t present = take_u32(&io);
            origin->present = present != 0;
            origin->source_frame = take_u64(&io);
            qa_source_save_vec3(&io, &origin->origin);
            if (present > 1 || !qa_vec_finite(origin->origin) || (present ? (!creation ||
                origin->source_frame < slots[index].creation_frame || (origin->source_frame & 7u) != i) :
                (origin->source_frame || origin->origin.x != 0 || origin->origin.y != 0 || origin->origin.z != 0))) history_valid = false;
        }
        if (slots[index].kind == QA_NATIVE_SLOT_FREE ||
            slots[index].kind > QA_NATIVE_SLOT_BORROWED ||
            lifetime_flags > 3 || !history_valid || !qa_vec_finite(slots[index].creation_origin) ||
            (!creation && (slots[index].creation_frame || slots[index].creation_origin.x != 0 ||
                slots[index].creation_origin.y != 0 || slots[index].creation_origin.z != 0)) ||
            ((creation || slots[index].linked) && (host->kind != NATIVE_HOST_Q2_GAME || !slots[index].slot)) ||
            (index && slots[index].slot <= slots[index - 1].slot) ||
            (!cvars_only && slots[index].slot >= table.capacity)) {
            free(slots);
            return native_host_fail(error, QA_ERROR_FORMAT, index,
                                    "native host checkpoint contains an invalid slot");
        }
    }
    if (retained_count > (io.input.size - io.offset) / 4u) {
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
        retained[index] = take_u32(&io);
        if (!cvars_only && retained[index] >= table.capacity) {
            free(slots);
            free(retained);
            return native_host_fail(error, QA_ERROR_FORMAT, index,
                                    "native host retained-client slot is invalid");
        }
    }
    qa_cvars_restore *cvars=NULL;
    native_host_memory_state memory={0};
    qa_native_host_message_reference *references=NULL;
    size_t registry_extent=(size_t)registry_size,memory_extent=(size_t)memory_size;
    qa_bytes registry, objects;
    if (!qa_source_save_span(&io, registry_extent, &registry) ||
        !qa_source_save_span(&io, memory_extent, &objects)) goto truncated;
    bool bind_memory=!cvars_only && qa_native_process_restore_pending(host->instance);
    qa_source_save_io objects_io={0};
    bool prepared=qa_source_save_reader(&objects_io,NULL,objects,error) &&
        native_host_memory_fields(&objects_io,host,&memory,bind_memory) && qa_source_save_finish(&objects_io,NULL);
    qa_source_save_dispose(&objects_io);
    if (prepared && registry.size) prepared=qa_cvars_save_prepare(host->cvars,registry,&cvars,error);
    if (!prepared) {
        free(slots); free(retained); native_host_memory_dispose(&memory); return false;
    }
    size_t remaining = (io.input.size - io.offset);
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
    qa_bytes message;
    if (!qa_source_save_span(&io, (size_t)message_size, &message)) goto truncated;
    if (!cvars_only && reference_count) {
        references = calloc((size_t)reference_count, sizeof(*references));
        if (!references) {
            free(slots); free(retained); qa_cvars_save_abort(cvars); native_host_memory_dispose(&memory);
            return native_host_fail(error, QA_ERROR_MEMORY, 0, "Restoring written native Q2 message entity references");
        }
    }
    const qa_actor_registry *actors = host->world.session ? qa_session_actors(host->world.session) : NULL;
    uint64_t previous_offset = 0;
    for (size_t i = 0; i < (size_t)reference_count; ++i) {
        uint64_t offset = take_u64(&io);
        qa_saved_actor_id saved = {.slot = take_u32(&io)};
        uint32_t reserved = take_u32(&io);
        saved.generation = take_u64(&io);
        if (reserved || offset > message_size || message_size - offset < 2 ||
            (i && offset < previous_offset + 2) || !saved.generation) goto truncated;
        previous_offset = offset;
        if (!cvars_only) {
            references[i].offset = (size_t)offset;
            if (!actors || !qa_actors_reference_saved(actors, saved, true, &references[i].actor, error)) {
                free(slots); free(retained); free(references); qa_cvars_save_abort(cvars); native_host_memory_dispose(&memory);
                return false;
            }
        }
    }
    qa_bytes engine, bridge;
    if (!qa_source_save_span(&io, (size_t)engine_size, &engine) ||
        !qa_source_save_span(&io, (size_t)bridge_size, &bridge) ||
        !qa_source_save_finish(&io, NULL)) goto truncated;
    qa_source_save_dispose(&io);
    host->restoring = true;
    if (cvars_only) {
        bool ok=!cvars || qa_cvars_save_commit(cvars,error);
        if (ok) { cvars=NULL; ok=native_host_refresh_cvars(host,error); }
        host->restoring = false;
        free(slots);
        free(retained);
        qa_cvars_save_abort(cvars); native_host_memory_dispose(&memory);
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
    if (ok && cvars) {
        ok=qa_cvars_save_commit(cvars,error);
        if (ok) cvars=NULL;
    }
    if (ok && bind_memory) {
        native_host_memory_state previous={host->strings,host->cvar_shadows};
        host->strings=memory.strings; host->cvar_shadows=memory.cvar_shadows;
        memory=(native_host_memory_state){0}; native_host_memory_dispose(&previous);
    }
    if (ok && !bind_memory && host->cvars) ok=native_host_refresh_cvars(host,error);
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
    qa_cvars_save_abort(cvars); native_host_memory_dispose(&memory);
    return ok;

truncated:
    free(slots);
    free(retained);
    free(references);
    qa_cvars_save_abort(cvars); native_host_memory_dispose(&memory);
    return native_host_fail(error, QA_ERROR_FORMAT, io.offset,
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
