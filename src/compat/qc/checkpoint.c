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
    free(checkpoint->globals); free(checkpoint->fields); free(checkpoint->slots);
    for (uint32_t i = 0; checkpoint->strings && i < checkpoint->string_count; ++i)
        free(checkpoint->strings[i].data);
    free(checkpoint->strings);
    for (uint32_t i = 0; checkpoint->engine_strings && i < checkpoint->engine_count; ++i)
        free(checkpoint->engine_strings[i].name);
    free(checkpoint->engine_strings);
    qa_buffer_free(&checkpoint->host);
    free(checkpoint);
}

static bool checkpoint_allocate(qa_qc_checkpoint *checkpoint, qa_error *error)
{
    checkpoint->globals = calloc(checkpoint->global_count, sizeof(*checkpoint->globals));
    checkpoint->fields = calloc(checkpoint->field_count, sizeof(*checkpoint->fields));
    checkpoint->slots = calloc(checkpoint->slot_count, sizeof(*checkpoint->slots));
    checkpoint->strings = calloc(checkpoint->string_count, sizeof(*checkpoint->strings));
    checkpoint->engine_strings = calloc(checkpoint->engine_count,
                                        sizeof(*checkpoint->engine_strings));
    if ((checkpoint->global_count != 0 && checkpoint->globals == NULL)
        || (checkpoint->field_count != 0 && checkpoint->fields == NULL)
        || (checkpoint->slot_count != 0 && checkpoint->slots == NULL)
        || (checkpoint->string_count != 0 && checkpoint->strings == NULL)
        || (checkpoint->engine_count != 0 && checkpoint->engine_strings == NULL))
        return qc_fail(error, QA_ERROR_MEMORY, 0, "Cannot allocate QuakeC checkpoint state");
    return true;
}

static uint32_t slot_freed_at(const qa_qc_instance *instance, uint32_t slot)
{
    const uint8_t *edict = instance->entities + (size_t)slot * instance->layout.stride_bytes;
    return qc_load_word(edict, instance->layout.variables_offset_bytes / 4u - 1u);
}

static bool retained_slot(const qa_qc_instance *instance, uint32_t slot)
{
    return instance->slots[slot].kind != QA_QC_SLOT_FREE || slot_freed_at(instance, slot) != 0;
}

static bool capture_storage(qa_qc_instance *instance, qa_qc_checkpoint *checkpoint,
                            const qa_actor_registry *actors, qa_error *error)
{
    checkpoint->entity_count = instance->entity_count;
    checkpoint->global_words = instance->program->info.global_words;
    checkpoint->string_base = instance->program->string_bytes;
    checkpoint->string_used = instance->strings.used;
    checkpoint->engine_count = instance->strings.engine_count;
    checkpoint->random_state = instance->random_state;
    for (uint32_t word = 0; word < checkpoint->global_words; ++word)
        if (qc_load_word(instance->globals, word) != qc_load_word(instance->program->initial_globals, word))
            ++checkpoint->global_count;
    for (uint32_t slot = 0; slot < checkpoint->entity_count; ++slot) {
        if (retained_slot(instance, slot)) ++checkpoint->slot_count;
        if (instance->slots[slot].kind == QA_QC_SLOT_FREE) continue;
        const uint8_t *fields = qc_entity_words_const(instance, slot);
        for (uint32_t word = 0; word < checkpoint->layout.field_words; ++word)
            if (qc_load_word(fields, word) != 0) ++checkpoint->field_count;
    }
    /* Keep addressable bytes after a shortened engine string, but not zero
     * padding or immutable literals. String IDs retain their original offsets. */
    for (uint32_t offset = checkpoint->string_base; offset < checkpoint->string_used;) {
        if (instance->strings.bytes[offset++] == 0) continue;
        ++checkpoint->string_count;
        while (offset < checkpoint->string_used && instance->strings.bytes[offset] != 0) ++offset;
    }
    if (!checkpoint_allocate(checkpoint, error)) return false;
    uint32_t next = 0;
    for (uint32_t word = 0; word < checkpoint->global_words; ++word) {
        uint32_t value = qc_load_word(instance->globals, word);
        if (value != qc_load_word(instance->program->initial_globals, word))
            checkpoint->globals[next++] = (qc_saved_word){word, value};
    }
    uint32_t next_field = 0;
    next = 0;
    for (uint32_t slot = 0; slot < checkpoint->entity_count; ++slot) {
        qc_slot binding = instance->slots[slot];
        if (retained_slot(instance, slot)) {
            qc_saved_slot *saved = &checkpoint->slots[next++];
            *saved = (qc_saved_slot){.slot = slot, .kind = binding.kind,
                .freed_at = slot_freed_at(instance, slot), .owner = binding.owner,
                .source_slot = binding.source_slot};
            if (binding.kind == QA_QC_SLOT_OWNED || binding.kind == QA_QC_SLOT_BORROWED) {
                if (actors == NULL || !qa_actors_save_reference(actors, binding.actor, &saved->actor, error))
                    return false;
                saved->has_actor = true;
            }
        }
        if (binding.kind == QA_QC_SLOT_FREE) continue;
        const uint8_t *fields = qc_entity_words_const(instance, slot);
        for (uint32_t word = 0; word < checkpoint->layout.field_words; ++word) {
            uint32_t value = qc_load_word(fields, word);
            if (value != 0)
                checkpoint->fields[next_field++] = (qc_saved_word){slot * checkpoint->layout.field_words + word, value};
        }
    }
    next = 0;
    for (uint32_t offset = checkpoint->string_base; offset < checkpoint->string_used;) {
        if (instance->strings.bytes[offset] == 0) { ++offset; continue; }
        uint32_t start = offset++;
        while (offset < checkpoint->string_used && instance->strings.bytes[offset] != 0) ++offset;
        qc_saved_bytes *range = &checkpoint->strings[next++];
        range->offset = start;
        range->size = offset - start;
        range->data = malloc(range->size);
        if (range->data == NULL)
            return qc_fail(error, QA_ERROR_MEMORY, start, "Cannot capture runtime QuakeC strings");
        memcpy(range->data, instance->strings.bytes + start, range->size);
    }
    for (uint32_t i = 0; i < checkpoint->engine_count; ++i) {
        checkpoint->engine_strings[i] = (qc_engine_string){
            .name = qc_strdup(instance->strings.engines[i].name, error),
            .offset = instance->strings.engines[i].offset,
            .capacity = instance->strings.engines[i].capacity};
        if (checkpoint->engine_strings[i].name == NULL) return false;
    }
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
    checkpoint->first_dynamic_slot = instance->options.first_dynamic_slot;
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
    /* Snapshot after the host callback has refreshed actual guest projections. */
    if (!capture_storage(instance, checkpoint, actors, error)) goto failed;
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

static bool checkpoint_valid(const qa_qc_checkpoint *checkpoint, qa_error *error)
{
    if (checkpoint->profile > QA_QC_RERELEASE || checkpoint->entity_count == 0
        || checkpoint->first_dynamic_slot == 0
        || checkpoint->first_dynamic_slot > checkpoint->entity_count
        || checkpoint->global_words < QC_RESERVED_WORDS
        || checkpoint->layout.stride_bytes == 0 || (checkpoint->layout.stride_bytes & 3u) != 0
        || checkpoint->layout.variables_offset_bytes < 8u
        || (checkpoint->layout.variables_offset_bytes & 3u) != 0
        || checkpoint->layout.field_words == 0
        || checkpoint->layout.variables_offset_bytes + (uint64_t)checkpoint->layout.field_words * 4u
            > checkpoint->layout.stride_bytes
        || (uint64_t)checkpoint->entity_count * checkpoint->layout.stride_bytes > (uint32_t)INT32_MAX
        || checkpoint->string_base == 0 || checkpoint->string_base > checkpoint->string_used
        || checkpoint->string_used > (uint32_t)INT32_MAX
        || checkpoint->slot_count == 0 || checkpoint->slot_count > checkpoint->entity_count
        || checkpoint->global_count > checkpoint->global_words
        || checkpoint->field_count > (uint64_t)checkpoint->entity_count * checkpoint->layout.field_words
        || checkpoint->slots == NULL
        || (checkpoint->global_count && checkpoint->globals == NULL)
        || (checkpoint->field_count && checkpoint->fields == NULL)
        || (checkpoint->string_count && checkpoint->strings == NULL)
        || (checkpoint->engine_count && checkpoint->engine_strings == NULL)
        || (checkpoint->host.size && checkpoint->host.data == NULL))
        return qc_fail(error, QA_ERROR_FORMAT, 0, "Invalid QuakeC checkpoint state");
    for (uint32_t i = 0; i < checkpoint->global_count; ++i)
        if (checkpoint->globals[i].word >= checkpoint->global_words
            || (i && checkpoint->globals[i - 1u].word >= checkpoint->globals[i].word))
            return qc_fail(error, QA_ERROR_FORMAT, i, "Invalid checkpoint global word");
    for (uint32_t i = 0; i < checkpoint->slot_count; ++i) {
        const qc_saved_slot *slot = &checkpoint->slots[i];
        if (slot->slot >= checkpoint->entity_count || slot->kind > QA_QC_SLOT_BORROWED
            || (i && checkpoint->slots[i - 1u].slot >= slot->slot)
            || (i == 0 && (slot->slot != 0 || slot->kind != QA_QC_SLOT_WORLD))
            || (i != 0 && slot->kind == QA_QC_SLOT_WORLD)
            || ((slot->kind == QA_QC_SLOT_OWNED || slot->kind == QA_QC_SLOT_BORROWED) != slot->has_actor)
            || (!slot->has_actor && (slot->owner || slot->source_slot || slot->actor.generation || slot->actor.slot))
            || (slot->kind == QA_QC_SLOT_FREE && slot->freed_at == 0))
            return qc_fail(error, QA_ERROR_FORMAT, i, "Invalid checkpoint actor slot");
    }
    uint32_t row = 0;
    for (uint32_t i = 0; i < checkpoint->field_count; ++i) {
        uint32_t slot = checkpoint->fields[i].word / checkpoint->layout.field_words;
        while (row < checkpoint->slot_count && checkpoint->slots[row].slot < slot) ++row;
        if (row == checkpoint->slot_count || checkpoint->slots[row].slot != slot
            || checkpoint->slots[row].kind == QA_QC_SLOT_FREE || checkpoint->fields[i].value == 0
            || (i && checkpoint->fields[i - 1u].word >= checkpoint->fields[i].word))
            return qc_fail(error, QA_ERROR_FORMAT, i, "Invalid checkpoint edict word");
    }
    uint32_t end = checkpoint->string_base;
    for (uint32_t i = 0; i < checkpoint->string_count; ++i) {
        const qc_saved_bytes *range = &checkpoint->strings[i];
        if (range->size == 0 || range->offset < end || range->offset > checkpoint->string_used
            || range->size > checkpoint->string_used - range->offset || range->data == NULL
            || memchr(range->data, 0, range->size) != NULL)
            return qc_fail(error, QA_ERROR_FORMAT, i, "Invalid checkpoint runtime string range");
        end = range->offset + range->size;
    }
    return true;
}

static bool checkpoint_matches(const qa_qc_instance *instance,
                               const qa_qc_checkpoint *checkpoint, qa_error *error)
{
    if (!checkpoint_valid(checkpoint, error)) return false;
    if (!qa_sha256_equal(&instance->program->info.digest, &checkpoint->program)
        || instance->options.profile != checkpoint->profile
        || instance->layout.stride_bytes != checkpoint->layout.stride_bytes
        || instance->layout.variables_offset_bytes != checkpoint->layout.variables_offset_bytes
        || instance->layout.field_words != checkpoint->layout.field_words
        || instance->options.first_dynamic_slot != checkpoint->first_dynamic_slot
        || checkpoint->entity_count > instance->options.entity_capacity
        || checkpoint->global_words != instance->program->info.global_words
        || checkpoint->string_base != instance->program->string_bytes)
        return qc_fail(error, QA_ERROR_FORMAT, 0, "QuakeC checkpoint does not match this instance");
    return true;
}

static bool stage_strings(const qa_qc_instance *instance,
                          const qa_qc_checkpoint *checkpoint, qc_strings *out,
                          qa_error *error)
{
    bool quakeworld = instance->program->info.api == QA_QC_API_QUAKEWORLD;
    if (quakeworld && checkpoint->engine_count > 1023u)
        return qc_fail(error, QA_ERROR_FORMAT, 0, "Checkpoint exceeds QuakeWorld engine strings");
    if (!qc_strings_create(out, (qa_bytes){instance->program->strings, instance->program->string_bytes},
                            quakeworld, error)) return false;
    uint32_t offset;
    if (!qc_strings_reserve(out, checkpoint->string_used - checkpoint->string_base, &offset, error)) {
        qc_strings_destroy(out); return false;
    }
    for (uint32_t i = 0; i < checkpoint->string_count; ++i) {
        const qc_saved_bytes *range = &checkpoint->strings[i];
        memcpy(out->bytes + range->offset, range->data, range->size);
    }
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
            || source->offset < checkpoint->string_base
            || source->offset > checkpoint->string_used
            || source->capacity > checkpoint->string_used - source->offset
            || memchr(out->bytes + source->offset, 0,
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
                        const qa_qc_checkpoint *checkpoint, qc_slot **out, qa_error *error)
{
    qc_slot *slots = calloc(instance->options.entity_capacity, sizeof(*slots));
    if (slots == NULL)
        return qc_fail(error, QA_ERROR_MEMORY, 0, "Cannot restore QuakeC actor map");
    const qa_actor_registry *actors = instance->options.host.session == NULL
        ? NULL : qa_session_actors(instance->options.host.session);
    for (uint32_t i = 0; i < checkpoint->slot_count; ++i) {
        const qc_saved_slot *saved = &checkpoint->slots[i];
        uint32_t slot = saved->slot;
        if (saved->kind == QA_QC_SLOT_WORLD || saved->kind == QA_QC_SLOT_FREE) {
            slots[slot].kind = saved->kind;
            continue;
        }
        if (actors == NULL) goto missing;
        const qa_actor_record *record;
        if (saved->kind == QA_QC_SLOT_OWNED) {
            if (saved->owner != instance->options.host.owner || saved->source_slot != slot) goto invalid;
            record = qa_actors_at_source(actors, instance->options.host.owner, saved->source_slot);
        } else record = qa_actors_resolve_saved(actors, saved->actor);
        if (record == NULL) goto missing;
        if (record->owner != saved->owner
            || (record->has_source ? record->source_slot : 0u) != saved->source_slot
            || (saved->kind == QA_QC_SLOT_OWNED && (!record->has_source || record->source_slot != slot)))
            goto invalid;
        const qa_actor_record *saved_record = qa_actors_resolve_saved(actors, saved->actor);
        if (saved_record == NULL || !qa_actor_id_equal(saved_record->id, record->id)) goto missing;
        for (uint32_t previous = 1; previous < slot; ++previous)
            if ((slots[previous].kind == QA_QC_SLOT_OWNED || slots[previous].kind == QA_QC_SLOT_BORROWED)
                && qa_actor_id_equal(slots[previous].actor, record->id)) goto invalid;
        slots[slot] = (qc_slot){saved->kind, record->id, record->owner,
            saved->kind == QA_QC_SLOT_OWNED ? saved->source_slot : (record->has_source ? record->source_slot : 0)};
    }
    *out = slots;
    return true;
invalid:
    free(slots);
    return qc_fail(error, QA_ERROR_FORMAT, 0, "Invalid QuakeC checkpoint actor map");
missing:
    free(slots);
    return qc_fail(error, QA_ERROR_NOT_FOUND, 0, "QuakeC checkpoint actor has not been restored");
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
    if (!stage_strings(instance, checkpoint, &strings, error)
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
    memcpy(instance->globals, instance->program->initial_globals, (size_t)checkpoint->global_words * 4u);
    for (uint32_t i = 0; i < checkpoint->global_count; ++i)
        qc_store_word(instance->globals, checkpoint->globals[i].word, checkpoint->globals[i].value);
    memset(instance->entities, 0, (size_t)instance->options.entity_capacity * instance->layout.stride_bytes);
    for (uint32_t slot = 1; slot < checkpoint->entity_count; ++slot)
        qa_store_u32le(instance->entities + (size_t)slot * instance->layout.stride_bytes,
                       slots[slot].kind == QA_QC_SLOT_FREE ? 1u : 0u);
    for (uint32_t i = 0; i < checkpoint->slot_count; ++i) {
        const qc_saved_slot *saved = &checkpoint->slots[i];
        qc_store_word(instance->entities + (size_t)saved->slot * instance->layout.stride_bytes,
            instance->layout.variables_offset_bytes / 4u - 1u, saved->freed_at);
    }
    for (uint32_t i = 0; i < checkpoint->field_count; ++i) {
        qc_saved_word field = checkpoint->fields[i];
        qc_store_word(qc_entity_words(instance, field.word / checkpoint->layout.field_words),
            field.word % checkpoint->layout.field_words, field.value);
    }
    memcpy(instance->slots, slots, (size_t)instance->options.entity_capacity * sizeof(*slots));
    free(slots);
    qc_strings_destroy(&instance->strings);
    instance->strings = strings;
    strings = (qc_strings){0};
    instance->entity_count = checkpoint->entity_count;
    qc_actor_slots_rebuild(instance);
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
        if ((instance->slots[slot].kind == QA_QC_SLOT_OWNED ||
             instance->slots[slot].kind == QA_QC_SLOT_BORROWED) &&
            !qc_prepare_entity_access(instance, slot, 0, 0,
                QA_QC_ENTITY_BIND, false, error)) {
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

/* The same record traversal writes and reads the sole checkpoint format. */
static bool checkpoint_io(qa_source_save_io *io, qa_qc_checkpoint *checkpoint)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    uint32_t magic = QC_CHECKPOINT_MAGIC, profile = (uint32_t)checkpoint->profile;
    size_t host_size = checkpoint->host.size;
    if (!qa_source_save_u32(io, &magic)) return false;
    if (magic != QC_CHECKPOINT_MAGIC)
        return qc_fail(io->error, QA_ERROR_FORMAT, 0, "Invalid QuakeC checkpoint header");
    if (!qa_source_save_bytes(io, checkpoint->program.bytes, sizeof(checkpoint->program.bytes))
        || !qa_source_save_u32(io, &profile)
        || !qa_source_save_u32(io, &checkpoint->layout.stride_bytes)
        || !qa_source_save_u32(io, &checkpoint->layout.variables_offset_bytes)
        || !qa_source_save_u32(io, &checkpoint->layout.field_words)
        || !qa_source_save_u32(io, &checkpoint->entity_count)
        || !qa_source_save_u32(io, &checkpoint->first_dynamic_slot)
        || !qa_source_save_u32(io, &checkpoint->global_words)
        || !qa_source_save_u32(io, &checkpoint->global_count)
        || !qa_source_save_u32(io, &checkpoint->field_count)
        || !qa_source_save_u32(io, &checkpoint->slot_count)
        || !qa_source_save_u32(io, &checkpoint->string_base)
        || !qa_source_save_u32(io, &checkpoint->string_used)
        || !qa_source_save_u32(io, &checkpoint->string_count)
        || !qa_source_save_u32(io, &checkpoint->engine_count)
        || !qa_source_save_count(io, &host_size, UINT32_MAX)
        || !qa_source_save_u32(io, &checkpoint->random_state)) return false;
    if (reading) {
        checkpoint->profile = (qa_qc_profile)profile;
        uint64_t minimum = (uint64_t)checkpoint->global_count * 8u
            + (uint64_t)checkpoint->field_count * 8u + (uint64_t)checkpoint->slot_count * 33u
            + (uint64_t)checkpoint->string_count * 8u + (uint64_t)checkpoint->engine_count * 12u
            + host_size;
        if (profile > QA_QC_RERELEASE || minimum > io->input.size - io->offset)
            return qc_fail(io->error, QA_ERROR_FORMAT, io->offset, "Invalid QuakeC checkpoint sizes");
        if (!checkpoint_allocate(checkpoint, io->error)) return false;
    }
    for (uint32_t i = 0; i < checkpoint->global_count; ++i)
        if (!qa_source_save_u32(io, &checkpoint->globals[i].word)
            || !qa_source_save_u32(io, &checkpoint->globals[i].value)) return false;
    for (uint32_t i = 0; i < checkpoint->field_count; ++i)
        if (!qa_source_save_u32(io, &checkpoint->fields[i].word)
            || !qa_source_save_u32(io, &checkpoint->fields[i].value)) return false;
    for (uint32_t i = 0; i < checkpoint->slot_count; ++i) {
        qc_saved_slot *slot = &checkpoint->slots[i];
        uint32_t kind = (uint32_t)slot->kind;
        if (!qa_source_save_u32(io, &slot->slot) || !qa_source_save_u32(io, &slot->freed_at)
            || !qa_source_save_u32(io, &kind) || !qa_source_save_u32(io, &slot->owner)
            || !qa_source_save_u32(io, &slot->source_slot) || !qa_source_save_bool(io, &slot->has_actor)
            || !qa_source_save_u64(io, &slot->actor.generation)
            || !qa_source_save_u32(io, &slot->actor.slot)) return false;
        if (reading) slot->kind = (qa_qc_slot_kind)kind;
    }
    for (uint32_t i = 0; i < checkpoint->string_count; ++i) {
        qc_saved_bytes *range = &checkpoint->strings[i];
        if (!qa_source_save_u32(io, &range->offset) || !qa_source_save_u32(io, &range->size)) return false;
        if (reading) {
            qa_bytes bytes;
            if (!qa_source_save_span(io, range->size, &bytes)) return false;
            range->data = malloc(range->size);
            if (range->size && range->data == NULL)
                return qc_fail(io->error, QA_ERROR_MEMORY, io->offset, "Cannot decode runtime QuakeC strings");
            if (range->size) memcpy(range->data, bytes.data, range->size);
        } else if (!qa_source_save_bytes(io, range->data, range->size)) return false;
    }
    for (uint32_t i = 0; i < checkpoint->engine_count; ++i) {
        qc_engine_string *entry = &checkpoint->engine_strings[i];
        if (!qa_source_save_u32(io, &entry->offset) || !qa_source_save_u32(io, &entry->capacity)
            || !qa_source_save_owned_text(io, &entry->name)) return false;
    }
    if (reading) {
        qa_bytes bytes;
        if (!qa_source_save_span(io, host_size, &bytes)) return false;
        checkpoint->host.data = malloc(host_size);
        if (host_size && checkpoint->host.data == NULL)
            return qc_fail(io->error, QA_ERROR_MEMORY, io->offset, "Cannot decode QuakeC host state");
        checkpoint->host.size = host_size;
        if (host_size) memcpy(checkpoint->host.data, bytes.data, host_size);
    } else if (!qa_source_save_bytes(io, checkpoint->host.data, host_size)) return false;
    return true;
}

bool qa_qc_checkpoint_encode(const qa_qc_checkpoint *checkpoint,
                             qa_buffer *out, qa_error *error)
{
    if (checkpoint == NULL || out == NULL)
        return qc_fail(error, QA_ERROR_ARGUMENT, 0, "Invalid QuakeC checkpoint output");
    if (!checkpoint_valid(checkpoint, error)) return false;
    qa_source_save_io writer = {0};
    if (!qa_source_save_writer(&writer, NULL, error)) return false;
    /* WRITE primitives do not mutate the checkpoint; one traversal owns both directions. */
    bool ok = checkpoint_io(&writer, (qa_qc_checkpoint *)checkpoint)
        && qa_source_save_finish(&writer, out);
    qa_source_save_dispose(&writer);
    return ok;
}

bool qa_qc_checkpoint_decode(qa_bytes bytes, qa_qc_checkpoint **out, qa_error *error)
{
    if (out == NULL || (bytes.size && bytes.data == NULL))
        return qc_fail(error, QA_ERROR_ARGUMENT, 0, "Invalid QuakeC checkpoint input");
    qa_source_save_io reader = {0};
    if (!qa_source_save_reader(&reader, NULL, bytes, error)) return false;
    qa_qc_checkpoint *checkpoint = calloc(1, sizeof(*checkpoint));
    if (checkpoint == NULL)
        return qc_fail(error, QA_ERROR_MEMORY, 0, "Cannot allocate QuakeC checkpoint");
    bool ok = checkpoint_io(&reader, checkpoint) && qa_source_save_finish(&reader, NULL)
        && checkpoint_valid(checkpoint, error);
    qa_source_save_dispose(&reader);
    if (!ok) { qa_qc_checkpoint_destroy(checkpoint); return false; }
    *out = checkpoint;
    return true;
}
