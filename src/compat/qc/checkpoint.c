/* Portable external QuakeC continuation checkpoints. */
#include "internal.h"
#include "qa/source_save.h"

#define QC_CHECKPOINT_MAGIC UINT32_C(0x43514151) /* "QAQC" little-endian. */

static bool checkpoint_idle(const qa_qc_instance *instance, qa_error *error)
{
    if (!qa_qc_idle(instance))
        return qc_fail(error, QA_ERROR_ARGUMENT, 0,
                       "QuakeC checkpoint requires an idle VM boundary");
    if (instance->options.host.session != NULL
        && !qa_session_safe(instance->options.host.session))
        return qc_fail(error, QA_ERROR_ARGUMENT, 0,
                       "QuakeC checkpoint requires a session safe point");
    if (instance->options.host.combat != NULL
        && !qa_combat_idle(instance->options.host.combat))
        return qc_fail(error, QA_ERROR_ARGUMENT, 0,
                       "QuakeC checkpoint requires idle combat");
    if (instance->options.host.pickups != NULL
        && !qa_pickups_idle(instance->options.host.pickups))
        return qc_fail(error, QA_ERROR_ARGUMENT, 0,
                       "QuakeC checkpoint requires idle pickups");
    return true;
}

void qa_qc_checkpoint_destroy(qa_qc_checkpoint *checkpoint)
{
    if (checkpoint == NULL) return;
    free(checkpoint->globals); free(checkpoint->entities); free(checkpoint->slots);
    free(checkpoint->profiles); free(checkpoint->strings);
    for (uint32_t i = 0; checkpoint->engine_strings && i < checkpoint->engine_count; ++i)
        free(checkpoint->engine_strings[i].name);
    free(checkpoint->engine_strings);
    qa_buffer_free(&checkpoint->host);
    free(checkpoint);
}

static bool checkpoint_allocate(qa_qc_checkpoint *checkpoint,
                                qa_error *error)
{
    checkpoint->globals = malloc(checkpoint->global_bytes);
    checkpoint->entities = malloc(checkpoint->entity_bytes);
    checkpoint->slots = calloc(checkpoint->entity_capacity,
                               sizeof(*checkpoint->slots));
    checkpoint->profiles = calloc(checkpoint->function_count,
                                  sizeof(*checkpoint->profiles));
    checkpoint->strings = malloc(checkpoint->string_used);
    checkpoint->engine_strings = calloc(checkpoint->engine_count,
                                        sizeof(*checkpoint->engine_strings));
    if ((checkpoint->global_bytes != 0 && checkpoint->globals == NULL)
        || (checkpoint->entity_bytes != 0 && checkpoint->entities == NULL)
        || (checkpoint->entity_capacity != 0 && checkpoint->slots == NULL)
        || (checkpoint->function_count != 0 && checkpoint->profiles == NULL)
        || (checkpoint->string_used != 0 && checkpoint->strings == NULL)
        || (checkpoint->engine_count != 0 && checkpoint->engine_strings == NULL))
        return qc_fail(error, QA_ERROR_MEMORY, 0,
                       "Cannot allocate QuakeC checkpoint state");
    return true;
}

bool qa_qc_checkpoint_capture(qa_qc_instance *instance,
                              qa_qc_checkpoint **out, qa_error *error)
{
    if (instance == NULL || out == NULL)
        return qc_fail(error, QA_ERROR_ARGUMENT, 0,
                       "Invalid QuakeC checkpoint capture request");
    if (!checkpoint_idle(instance, error)) return false;
    instance->checkpointing = true;
    instance->capturing_checkpoint = true;
    qa_qc_checkpoint *checkpoint = calloc(1, sizeof(*checkpoint));
    if (checkpoint == NULL) {
        instance->capturing_checkpoint = false;
        instance->checkpointing = false;
        return qc_fail(error, QA_ERROR_MEMORY, 0, "Cannot allocate QuakeC checkpoint");
    }
    checkpoint->program = instance->program->info.digest;
    checkpoint->profile = instance->options.profile;
    checkpoint->layout = instance->layout;
    checkpoint->entity_capacity = instance->options.entity_capacity;
    checkpoint->first_dynamic_slot = instance->options.first_dynamic_slot;
    checkpoint->global_bytes = instance->program->info.global_words * 4u;
    checkpoint->entity_bytes = instance->options.entity_capacity
                             * instance->layout.stride_bytes;
    checkpoint->function_count = instance->program->info.function_count;
    const qa_actor_registry *actors = instance->options.host.session == NULL
        ? NULL : qa_session_actors(instance->options.host.session);
    if (instance->options.host.checkpoint != NULL) {
        uint64_t actor_revision = actors == NULL ? 0
                                                 : qa_actors_revision(actors);
        qa_error failure = {0};
        ++instance->callback_depth;
        bool ok = instance->options.host.checkpoint(instance->options.host.context,
                                                     &checkpoint->host, &failure);
        --instance->callback_depth;
        if (!ok) {
            if (failure.code == QA_OK)
                qa_error_set(&failure, QA_ERROR_ARGUMENT, 0,
                             "QuakeC host checkpoint callback failed");
            if (error != NULL) *error = failure;
            goto failed;
        }
        if (actors != NULL && qa_actors_revision(actors) != actor_revision) {
            qc_fail(error, QA_ERROR_ARGUMENT, 0,
                    "QuakeC host checkpoint changed the shared actor registry");
            goto failed;
        }
        if (checkpoint->host.size != 0 && checkpoint->host.data == NULL) {
            qc_fail(error, QA_ERROR_ARGUMENT, 0,
                    "QuakeC host returned an invalid checkpoint buffer");
            goto failed;
        }
        if (checkpoint->host.size > UINT32_MAX) {
            qc_fail(error, QA_ERROR_MEMORY, checkpoint->host.size,
                    "QuakeC host checkpoint exceeds the portable format");
            goto failed;
        }
    }
    /* Entity inspection during the host callback may refresh canonical fields
     * through prepare_entity. Snapshot the guest image after those projections. */
    checkpoint->entity_count = instance->entity_count;
    checkpoint->string_used = instance->strings.used;
    checkpoint->engine_count = instance->strings.engine_count;
    checkpoint->trace_enabled = instance->trace_enabled;
    checkpoint->random_state = instance->random_state;
    if (!checkpoint_allocate(checkpoint, error)) goto failed;
    memcpy(checkpoint->globals, instance->globals, checkpoint->global_bytes);
    memcpy(checkpoint->entities, instance->entities, checkpoint->entity_bytes);
    memcpy(checkpoint->profiles, instance->profiles,
           (size_t)checkpoint->function_count * sizeof(*checkpoint->profiles));
    memcpy(checkpoint->strings, instance->strings.bytes, checkpoint->string_used);
    for (uint32_t i = 0; i < checkpoint->engine_count; ++i) {
        checkpoint->engine_strings[i] = (qc_engine_string){
            .name = qc_strdup(instance->strings.engines[i].name, error),
            .offset = instance->strings.engines[i].offset,
            .capacity = instance->strings.engines[i].capacity
        };
        if (checkpoint->engine_strings[i].name == NULL) goto failed;
    }
    for (uint32_t slot = 0; slot < checkpoint->entity_capacity; ++slot) {
        qc_slot binding = instance->slots[slot];
        qc_saved_slot *saved = &checkpoint->slots[slot];
        saved->kind = binding.kind;
        saved->owner = binding.owner;
        saved->source_slot = binding.source_slot;
        if (binding.kind != QA_QC_SLOT_OWNED && binding.kind != QA_QC_SLOT_BORROWED)
            continue;
        if (actors == NULL || !qa_actors_save_reference(actors, binding.actor,
                                                        &saved->actor, error)) goto failed;
        saved->has_actor = true;
    }
    *out = checkpoint;
    instance->capturing_checkpoint = false;
    instance->checkpointing = false;
    return true;
failed:
    qa_qc_checkpoint_destroy(checkpoint);
    instance->capturing_checkpoint = false;
    instance->checkpointing = false;
    return false;
}

static bool checkpoint_matches(const qa_qc_instance *instance,
                               const qa_qc_checkpoint *checkpoint,
                               qa_error *error)
{
    uint32_t globals = instance->program->info.global_words * 4u;
    uint32_t entities = instance->options.entity_capacity
                      * instance->layout.stride_bytes;
    if (!qa_sha256_equal(&instance->program->info.digest, &checkpoint->program)
        || instance->options.profile != checkpoint->profile
        || instance->layout.stride_bytes != checkpoint->layout.stride_bytes
        || instance->layout.variables_offset_bytes
            != checkpoint->layout.variables_offset_bytes
        || instance->layout.field_words != checkpoint->layout.field_words
        || instance->options.entity_capacity != checkpoint->entity_capacity
        || instance->options.first_dynamic_slot
            != checkpoint->first_dynamic_slot
        || checkpoint->entity_count == 0
        || checkpoint->entity_count < checkpoint->first_dynamic_slot
        || checkpoint->entity_count > checkpoint->entity_capacity
        || checkpoint->global_bytes != globals
        || checkpoint->entity_bytes != entities
        || checkpoint->function_count != instance->program->info.function_count
        || checkpoint->string_used < instance->program->string_bytes
        || checkpoint->string_used > (uint32_t)INT32_MAX
        || checkpoint->strings == NULL
        || (checkpoint->engine_count != 0
            && checkpoint->engine_strings == NULL))
        return qc_fail(error, QA_ERROR_FORMAT, 0,
                       "QuakeC checkpoint does not match this instance");
    if (checkpoint->slots == NULL || checkpoint->globals == NULL
        || checkpoint->entities == NULL || checkpoint->profiles == NULL)
        return qc_fail(error, QA_ERROR_FORMAT, 0,
                       "QuakeC checkpoint storage is incomplete");
    if (memcmp(checkpoint->strings, instance->program->strings,
               instance->program->string_bytes) != 0)
        return qc_fail(error, QA_ERROR_FORMAT, 0,
                       "QuakeC checkpoint changed immutable program strings");
    for (uint32_t i = 0; i < checkpoint->engine_count; ++i)
        if (checkpoint->engine_strings[i].offset
            < instance->program->string_bytes)
            return qc_fail(error, QA_ERROR_FORMAT,
                           checkpoint->engine_strings[i].offset,
                           "QuakeC engine string overlaps program strings");
    return true;
}

static bool stage_strings(const qa_qc_checkpoint *checkpoint, qc_strings *out,
                          bool quakeworld, qa_error *error)
{
    if (checkpoint->strings[0] != 0)
        return qc_fail(error, QA_ERROR_FORMAT, 0,
                       "Checkpoint QuakeC string zero is not empty");
    if (quakeworld && checkpoint->engine_count > 1023u)
        return qc_fail(error, QA_ERROR_FORMAT, 0,
                       "Checkpoint exceeds QuakeWorld engine strings");
    uint32_t capacity = checkpoint->string_used < 4096u ? 4096u
                                                        : checkpoint->string_used;
    out->bytes = calloc(capacity, 1u);
    if (out->bytes == NULL)
        return qc_fail(error, QA_ERROR_MEMORY, 0, "Cannot restore QuakeC strings");
    out->capacity = capacity;
    out->used = checkpoint->string_used;
    out->quakeworld = quakeworld;
    memcpy(out->bytes, checkpoint->strings, checkpoint->string_used);
    if (checkpoint->engine_count != 0) {
        out->engines = calloc(checkpoint->engine_count, sizeof(*out->engines));
        if (out->engines == NULL) {
            qc_strings_destroy(out);
            return qc_fail(error, QA_ERROR_MEMORY, 0,
                           "Cannot restore QuakeC engine strings");
        }
        out->engine_capacity = checkpoint->engine_count;
    }
    for (uint32_t i = 0; i < checkpoint->engine_count; ++i) {
        const qc_engine_string *source = &checkpoint->engine_strings[i];
        if (source->name == NULL || source->name[0] == '\0'
            || source->capacity == 0
            || source->offset > checkpoint->string_used
            || source->capacity > checkpoint->string_used - source->offset
            || memchr(checkpoint->strings + source->offset, 0,
                      source->capacity) == NULL) {
            qc_strings_destroy(out);
            return qc_fail(error, QA_ERROR_FORMAT, source->offset,
                           "Invalid checkpoint engine string");
        }
        for (uint32_t previous = 0; previous < i; ++previous) {
            const qc_engine_string *other = &checkpoint->engine_strings[previous];
            uint64_t source_end = (uint64_t)source->offset + source->capacity;
            uint64_t other_end = (uint64_t)other->offset + other->capacity;
            if (strcmp(source->name, other->name) == 0
                || ((uint64_t)source->offset < other_end
                    && (uint64_t)other->offset < source_end)) {
                qc_strings_destroy(out);
                return qc_fail(error, QA_ERROR_FORMAT, source->offset,
                               "Overlapping or duplicate checkpoint engine string");
            }
        }
        out->engines[i] = (qc_engine_string){qc_strdup(source->name, error),
                                              source->offset, source->capacity};
        if (out->engines[i].name == NULL) {
            out->engine_count = i;
            qc_strings_destroy(out); return false;
        }
        out->engine_count = i + 1u;
    }
    return true;
}

static bool stage_slots(qa_qc_instance *instance,
                        const qa_qc_checkpoint *checkpoint,
                        qc_slot **out, qa_error *error)
{
    qc_slot *slots = calloc(checkpoint->entity_capacity, sizeof(*slots));
    if (slots == NULL)
        return qc_fail(error, QA_ERROR_MEMORY, 0, "Cannot restore QuakeC actor map");
    const qa_actor_registry *actors = instance->options.host.session == NULL
        ? NULL : qa_session_actors(instance->options.host.session);
    for (uint32_t slot = 0; slot < checkpoint->entity_capacity; ++slot) {
        const qc_saved_slot *saved = &checkpoint->slots[slot];
        const uint8_t *edict = checkpoint->entities
            + (size_t)slot * checkpoint->layout.stride_bytes;
        uint32_t free_flag = qa_load_u32le(edict);
        bool expected_free = slot != 0 && slot < checkpoint->entity_count
                          && saved->kind == QA_QC_SLOT_FREE;
        if (free_flag > 1u || (free_flag != 0u) != expected_free) goto invalid;
        if (slot >= checkpoint->entity_count
            && saved->kind != QA_QC_SLOT_FREE) goto invalid;
        if (slot == 0) {
            if (saved->kind != QA_QC_SLOT_WORLD || saved->has_actor
                || saved->owner != 0 || saved->source_slot != 0
                || saved->actor.generation != 0
                || saved->actor.slot != 0) goto invalid;
            slots[slot].kind = QA_QC_SLOT_WORLD;
            continue;
        }
        if (saved->kind == QA_QC_SLOT_FREE) {
            if (saved->has_actor || saved->owner != 0
                || saved->source_slot != 0 || saved->actor.generation != 0
                || saved->actor.slot != 0) goto invalid;
            continue;
        }
        if (saved->kind != QA_QC_SLOT_OWNED && saved->kind != QA_QC_SLOT_BORROWED)
            goto invalid;
        if (!saved->has_actor || actors == NULL) goto missing;
        const qa_actor_record *record;
        if (saved->kind == QA_QC_SLOT_OWNED) {
            if (saved->owner != instance->options.host.owner
                || saved->source_slot != slot) goto invalid;
            record = qa_actors_at_source(actors, instance->options.host.owner,
                                         saved->source_slot);
        } else record = qa_actors_resolve_saved(actors, saved->actor);
        if (record == NULL) goto missing;
        if (record->owner != saved->owner
            || (record->has_source ? record->source_slot : 0u)
                != saved->source_slot
            || (saved->kind == QA_QC_SLOT_OWNED
                && (!record->has_source || record->source_slot != slot)))
            goto invalid;
        const qa_actor_record *saved_record = qa_actors_resolve_saved(
            actors, saved->actor);
        if (saved_record == NULL
            || !qa_actor_id_equal(saved_record->id, record->id)) goto missing;
        for (uint32_t previous = 1; previous < slot; ++previous)
            if ((slots[previous].kind == QA_QC_SLOT_OWNED
                 || slots[previous].kind == QA_QC_SLOT_BORROWED)
                && qa_actor_id_equal(slots[previous].actor, record->id))
                goto invalid;
        slots[slot] = (qc_slot){saved->kind, record->id, record->owner,
                                saved->kind == QA_QC_SLOT_OWNED
                                    ? saved->source_slot
                                    : (record->has_source ? record->source_slot : 0)};
    }
    *out = slots;
    return true;
invalid:
    free(slots);
    return qc_fail(error, QA_ERROR_FORMAT, 0, "Invalid QuakeC checkpoint actor map");
missing:
    free(slots);
    return qc_fail(error, QA_ERROR_NOT_FOUND, 0,
                   "QuakeC checkpoint actor has not been restored");
}

static bool staged_owned_actor(const qc_slot *slots, uint32_t count,
                               qc_slot current)
{
    for (uint32_t slot = 1; slot < count; ++slot)
        if (slots[slot].kind == QA_QC_SLOT_OWNED
            && slots[slot].source_slot == current.source_slot
            && qa_actor_id_equal(slots[slot].actor, current.actor)) return true;
    return false;
}

static bool release_replaced_sources(qa_qc_instance *instance,
                                     const qc_slot *slots,
                                     uint32_t count, qa_error *error)
{
    if (instance->options.host.session == NULL) return true;
    uint32_t current_count = instance->entity_count;
    for (uint32_t slot = 1; slot < current_count; ++slot) {
        qc_slot current = instance->slots[slot];
        if (current.kind != QA_QC_SLOT_OWNED
            || staged_owned_actor(slots, count, current)) continue;
        const qa_actor_record *record = qa_actors_get(
            qa_session_actors(instance->options.host.session), current.actor);
        if (record != NULL
            && !qa_session_release(instance->options.host.session,
                                   current.actor, error)) return false;
    }
    return true;
}

bool qa_qc_checkpoint_restore(qa_qc_instance *instance,
                              const qa_qc_checkpoint *checkpoint,
                              qa_error *error)
{
    if (instance == NULL || checkpoint == NULL)
        return qc_fail(error, QA_ERROR_ARGUMENT, 0,
                       "Invalid QuakeC checkpoint restore request");
    if (!checkpoint_idle(instance, error)
        || !checkpoint_matches(instance, checkpoint, error)) return false;
    instance->checkpointing = true;
    qc_strings strings = {0};
    qc_slot *slots = NULL;
    if (!stage_strings(checkpoint, &strings,
            instance->program->info.api == QA_QC_API_QUAKEWORLD, error)
        || !stage_slots(instance, checkpoint, &slots, error)) {
        qc_strings_destroy(&strings); free(slots);
        instance->checkpointing = false;
        return false;
    }
    if (checkpoint->host.size != 0 && instance->options.host.restore == NULL) {
        qc_strings_destroy(&strings); free(slots);
        instance->checkpointing = false;
        return qc_fail(error, QA_ERROR_UNSUPPORTED, 0,
                       "QuakeC checkpoint has host state but no restore callback");
    }
    if (!release_replaced_sources(instance, slots,
                                  checkpoint->entity_count, error)) {
        qc_strings_destroy(&strings); free(slots);
        instance->checkpointing = false;
        return false;
    }
    qc_slot *verified_slots = NULL;
    if (!stage_slots(instance, checkpoint, &verified_slots, error)) {
        qc_strings_destroy(&strings); free(slots);
        instance->checkpointing = false;
        return false;
    }
    free(slots);
    slots = verified_slots;
    memcpy(instance->globals, checkpoint->globals, checkpoint->global_bytes);
    memcpy(instance->entities, checkpoint->entities, checkpoint->entity_bytes);
    memcpy(instance->profiles, checkpoint->profiles,
           (size_t)checkpoint->function_count * sizeof(*checkpoint->profiles));
    memcpy(instance->slots, slots,
           (size_t)checkpoint->entity_capacity * sizeof(*slots));
    free(slots);
    qc_strings_destroy(&instance->strings);
    instance->strings = strings;
    strings = (qc_strings){0};
    instance->entity_count = checkpoint->entity_count;
    qc_actor_slots_rebuild(instance);
    instance->trace_enabled = checkpoint->trace_enabled;
    instance->random_state = checkpoint->random_state;
    /* Coupled adapters rebuild from the restored guest image. This order also
     * matches the QVM/QuakeC continuation contract. A callback failure leaves
     * the committed guest image visible and is a failed load boundary. */
    if (instance->options.host.restore != NULL) {
        const qa_actor_registry *actors = instance->options.host.session == NULL
            ? NULL : qa_session_actors(instance->options.host.session);
        uint64_t actor_revision = actors == NULL ? 0
                                                 : qa_actors_revision(actors);
        qa_error failure = {0};
        ++instance->callback_depth;
        bool ok = instance->options.host.restore(instance->options.host.context,
                    (qa_bytes){checkpoint->host.data, checkpoint->host.size},
                    &failure);
        --instance->callback_depth;
        if (!ok) {
            if (failure.code == QA_OK)
                qa_error_set(&failure, QA_ERROR_ARGUMENT, 0,
                             "QuakeC host restore callback failed");
            if (error != NULL) *error = failure;
            instance->checkpointing = false;
            return false;
        }
        if (actors != NULL && qa_actors_revision(actors) != actor_revision) {
            instance->checkpointing = false;
            return qc_fail(error, QA_ERROR_ARGUMENT, 0,
                           "QuakeC host restore changed the shared actor registry");
        }
    }
    const qa_actor_registry *actors = instance->options.host.session == NULL
        ? NULL : qa_session_actors(instance->options.host.session);
    uint64_t actor_revision = actors == NULL ? 0 : qa_actors_revision(actors);
    for (uint32_t slot = 1; slot < instance->entity_count; ++slot) {
        if (instance->slots[slot].kind == QA_QC_SLOT_OWNED
            && !qc_sync_body_from_fields(instance, slot, error)) {
            instance->checkpointing = false;
            return false;
        }
        if (actors != NULL && qa_actors_revision(actors) != actor_revision) {
            instance->checkpointing = false;
            return qc_fail(error, QA_ERROR_ARGUMENT, slot,
                           "QuakeC body restore changed the shared actor registry");
        }
    }
    instance->checkpointing = false;
    return true;
}

static bool write_bytes(qa_source_save_io *writer, const void *data, size_t size)
{
    return qa_source_save_bytes(writer, (void *)data, size);
}

static bool write_u32(qa_source_save_io *writer, uint32_t value)
{
    return qa_source_save_u32(writer, &value);
}

static bool write_u64(qa_source_save_io *writer, uint64_t value)
{
    return qa_source_save_u64(writer, &value);
}

bool qa_qc_checkpoint_encode(const qa_qc_checkpoint *checkpoint,
                             qa_buffer *out, qa_error *error)
{
    if (checkpoint == NULL || out == NULL)
        return qc_fail(error, QA_ERROR_ARGUMENT, 0, "Invalid QuakeC checkpoint output");
    qa_source_save_io writer = {0};
    if (!qa_source_save_writer(&writer, NULL, error)) return false;
#define W32(value_) do { if (!write_u32(&writer, (uint32_t)(value_))) goto failed; } while (0)
    W32(QC_CHECKPOINT_MAGIC);
    if (!write_bytes(&writer, checkpoint->program.bytes,
                     sizeof(checkpoint->program.bytes))) goto failed;
    W32(checkpoint->profile); W32(checkpoint->layout.stride_bytes);
    W32(checkpoint->layout.variables_offset_bytes); W32(checkpoint->layout.field_words);
    W32(checkpoint->entity_capacity); W32(checkpoint->entity_count);
    W32(checkpoint->first_dynamic_slot);
    W32(checkpoint->global_bytes); W32(checkpoint->entity_bytes);
    W32(checkpoint->function_count); W32(checkpoint->string_used);
    W32(checkpoint->engine_count); W32(checkpoint->host.size);
    W32(checkpoint->random_state); W32(checkpoint->trace_enabled ? 1u : 0u);
    for (uint32_t i = 0; i < checkpoint->entity_capacity; ++i) {
        const qc_saved_slot *slot = &checkpoint->slots[i];
        W32(slot->kind); W32(slot->owner); W32(slot->source_slot);
        W32(slot->has_actor ? 1u : 0u);
        if (!write_u64(&writer, slot->actor.generation)) goto failed;
        W32(slot->actor.slot);
    }
    for (uint32_t i = 0; i < checkpoint->function_count; ++i)
        if (!write_u64(&writer, checkpoint->profiles[i])) goto failed;
    if (!write_bytes(&writer, checkpoint->globals, checkpoint->global_bytes)
        || !write_bytes(&writer, checkpoint->entities, checkpoint->entity_bytes)
        || !write_bytes(&writer, checkpoint->strings, checkpoint->string_used)) goto failed;
    for (uint32_t i = 0; i < checkpoint->engine_count; ++i) {
        const qc_engine_string *entry = &checkpoint->engine_strings[i];
        size_t length = strlen(entry->name);
        if (length > UINT32_MAX) { qc_fail(error, QA_ERROR_MEMORY, writer.output.size, "Engine string name is too long"); goto failed; }
        W32(entry->offset); W32(entry->capacity); W32(length);
        if (!write_bytes(&writer, entry->name, length)) goto failed;
    }
    if (!write_bytes(&writer, checkpoint->host.data, checkpoint->host.size)) goto failed;
    if (!qa_source_save_finish(&writer, out)) goto failed;
    qa_source_save_dispose(&writer);
#undef W32
    return true;
failed:
#undef W32
    qa_source_save_dispose(&writer);
    return false;
}

static bool read_bytes(qa_source_save_io *reader, size_t size, const uint8_t **out)
{
    qa_bytes bytes;
    if (!qa_source_save_span(reader, size, &bytes)) return false;
    *out = bytes.data;
    return true;
}

bool qa_qc_checkpoint_decode(qa_bytes bytes, qa_qc_checkpoint **out,
                             qa_error *error)
{
    if (out == NULL || (bytes.size != 0 && bytes.data == NULL))
        return qc_fail(error, QA_ERROR_ARGUMENT, 0, "Invalid QuakeC checkpoint input");
    qa_source_save_io reader = {0};
    if (!qa_source_save_reader(&reader, NULL, bytes, error)) return false;
    uint32_t magic, profile, trace, host_size;
    qa_qc_checkpoint *checkpoint = calloc(1, sizeof(*checkpoint));
    if (checkpoint == NULL)
        return qc_fail(error, QA_ERROR_MEMORY, 0, "Cannot allocate QuakeC checkpoint");
#define R32(target_) do { if (!qa_source_save_u32(&reader, &(target_))) goto failed; } while (0)
    R32(magic);
    if (magic != QC_CHECKPOINT_MAGIC) {
        qc_fail(error, QA_ERROR_FORMAT, 0, "Unsupported QuakeC checkpoint header"); goto failed;
    }
    if (!qa_source_save_bytes(&reader, checkpoint->program.bytes,
                     sizeof(checkpoint->program.bytes))) goto failed;
    R32(profile); R32(checkpoint->layout.stride_bytes);
    R32(checkpoint->layout.variables_offset_bytes); R32(checkpoint->layout.field_words);
    R32(checkpoint->entity_capacity); R32(checkpoint->entity_count);
    R32(checkpoint->first_dynamic_slot);
    R32(checkpoint->global_bytes); R32(checkpoint->entity_bytes);
    R32(checkpoint->function_count); R32(checkpoint->string_used);
    R32(checkpoint->engine_count); R32(host_size);
    R32(checkpoint->random_state); R32(trace);
    if (profile > QA_QC_RERELEASE || trace > 1u
        || checkpoint->entity_capacity == 0
        || checkpoint->entity_count == 0
        || checkpoint->entity_count > checkpoint->entity_capacity
        || checkpoint->first_dynamic_slot == 0
        || checkpoint->first_dynamic_slot > checkpoint->entity_count
        || checkpoint->global_bytes < QC_RESERVED_WORDS * 4u
        || (checkpoint->global_bytes & 3u) != 0
        || checkpoint->function_count == 0 || checkpoint->string_used == 0
        || checkpoint->string_used > (uint32_t)INT32_MAX
        || checkpoint->layout.stride_bytes == 0
        || (checkpoint->layout.stride_bytes & 3u) != 0
        || checkpoint->layout.variables_offset_bytes < 8u
        || (checkpoint->layout.variables_offset_bytes & 3u) != 0
        || checkpoint->layout.field_words == 0
        || checkpoint->layout.variables_offset_bytes
            + (uint64_t)checkpoint->layout.field_words * 4u
            > checkpoint->layout.stride_bytes
        || (uint64_t)checkpoint->entity_capacity * checkpoint->layout.stride_bytes
            != checkpoint->entity_bytes
        || checkpoint->global_bytes > bytes.size
        || checkpoint->entity_bytes > bytes.size
        || checkpoint->string_used > bytes.size
        || checkpoint->engine_count > bytes.size / 12u
        || host_size > bytes.size) {
        qc_fail(error, QA_ERROR_FORMAT, reader.offset, "Invalid QuakeC checkpoint sizes"); goto failed;
    }
    uint64_t minimum = (uint64_t)checkpoint->entity_capacity * 28u
        + (uint64_t)checkpoint->function_count * 8u
        + checkpoint->global_bytes + checkpoint->entity_bytes
        + checkpoint->string_used + (uint64_t)checkpoint->engine_count * 12u
        + host_size;
    if (minimum > bytes.size - reader.offset) {
        qc_fail(error, QA_ERROR_FORMAT, reader.offset,
                "Truncated QuakeC checkpoint payload"); goto failed;
    }
    checkpoint->profile = (qa_qc_profile)profile;
    checkpoint->trace_enabled = trace != 0;
    if (!checkpoint_allocate(checkpoint, error)) goto failed;
    for (uint32_t i = 0; i < checkpoint->entity_capacity; ++i) {
        qc_saved_slot *slot = &checkpoint->slots[i];
        uint32_t kind, has_actor;
        R32(kind); R32(slot->owner); R32(slot->source_slot); R32(has_actor);
        if (!qa_source_save_u64(&reader, &slot->actor.generation)) goto failed;
        R32(slot->actor.slot);
        if (kind > QA_QC_SLOT_BORROWED || has_actor > 1u) {
            qc_fail(error, QA_ERROR_FORMAT, reader.offset, "Invalid checkpoint actor slot"); goto failed;
        }
        slot->kind = (qa_qc_slot_kind)kind; slot->has_actor = has_actor != 0;
    }
    for (uint32_t i = 0; i < checkpoint->function_count; ++i)
        if (!qa_source_save_u64(&reader, &checkpoint->profiles[i])) goto failed;
    if (!qa_source_save_bytes(&reader, checkpoint->globals, checkpoint->global_bytes)
        || !qa_source_save_bytes(&reader, checkpoint->entities, checkpoint->entity_bytes)
        || !qa_source_save_bytes(&reader, checkpoint->strings, checkpoint->string_used)) goto failed;
    for (uint32_t i = 0; i < checkpoint->engine_count; ++i) {
        qc_engine_string *entry = &checkpoint->engine_strings[i];
        uint32_t length;
        R32(entry->offset); R32(entry->capacity); R32(length);
        const uint8_t *name;
        size_t allocation = (size_t)length + 1u;
        if (length == 0 || allocation <= (size_t)length) {
            qc_fail(error, QA_ERROR_FORMAT, reader.offset,
                    "Invalid checkpoint engine name length"); goto failed;
        }
        if (!read_bytes(&reader, length, &name)) goto failed;
        if (memchr(name, 0, length) != NULL) {
            qc_fail(error, QA_ERROR_FORMAT, reader.offset - length,
                    "Checkpoint engine name contains NUL"); goto failed;
        }
        entry->name = malloc(allocation);
        if (entry->name == NULL) {
            qc_fail(error, QA_ERROR_MEMORY, reader.offset, "Cannot decode engine name"); goto failed;
        }
        memcpy(entry->name, name, length); entry->name[length] = '\0';
    }
    if (host_size != 0) {
        checkpoint->host.data = malloc(host_size);
        if (checkpoint->host.data == NULL) {
            qc_fail(error, QA_ERROR_MEMORY, reader.offset, "Cannot decode host checkpoint"); goto failed;
        }
        checkpoint->host.size = host_size;
        if (!qa_source_save_bytes(&reader, checkpoint->host.data, host_size)) goto failed;
    }
    if (!qa_source_save_finish(&reader, NULL)) goto failed;
    *out = checkpoint;
#undef R32
    return true;
failed:
#undef R32
    qa_qc_checkpoint_destroy(checkpoint);
    return false;
}
