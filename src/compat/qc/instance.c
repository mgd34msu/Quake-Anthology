/* External QuakeC instance and shared-authority host bindings. */
#include "internal.h"

#include <math.h>

static bool valid_profile(const qa_qc_program *program, qa_qc_profile profile)
{
    if (profile < QA_QC_NETQUAKE || profile > QA_QC_RERELEASE) return false;
    return profile == QA_QC_QUAKEWORLD
        ? program->info.api == QA_QC_API_QUAKEWORLD
        : program->info.api == QA_QC_API_NETQUAKE;
}

static int32_t reference_of(const qa_qc_instance *instance, uint32_t slot)
{
    return (int32_t)((uint64_t)slot * instance->layout.stride_bytes);
}
static int32_t source_float_int(float);
static bool source_ground(const qa_qc_instance *instance, uint32_t slot)
{
    return instance->options.host.declared_projection
        ? instance->slots[slot].kind == QA_QC_SLOT_OWNED
        : instance->program->info.api == QA_QC_API_NETQUAKE;
}
static bool body_ground_flags(const qa_qc_instance *instance,uint32_t slot,
    uint32_t *out,qa_error *error)
{
    const qa_qc_definition *flags=qa_qc_program_find_field(instance->program,"flags");
    *out=0;
    if (!flags) return true;
    if (flags->type!=QA_QC_FLOAT || flags->offset>=instance->layout.field_words)
        return qc_fail(error,QA_ERROR_FORMAT,slot,"QuakeC flags field has the wrong type");
    float value=qc_load_float(qc_entity_words_const(instance,slot),flags->offset);
    if (!isfinite(value)) return qc_fail(error,QA_ERROR_FORMAT,slot,"QuakeC body flags are nonfinite");
    *out=(uint32_t)source_float_int(value); return true;
}

static uint8_t *edict_bytes(qa_qc_instance *instance, uint32_t slot)
{
    return instance->entities + (size_t)slot * instance->layout.stride_bytes;
}

static void set_free_metadata(qa_qc_instance *instance, uint32_t slot,
                              bool free, float freed_at)
{
    uint8_t *edict = edict_bytes(instance, slot);
    qa_store_u32le(edict, free ? 1u : 0u);
    qc_store_float(edict,
        instance->layout.variables_offset_bytes / 4u - 1u, freed_at);
}

static const qa_qc_builtin_binding *host_binding(const qa_qc_instance *instance,
                                                  qa_qc_builtin builtin,
                                                  const char *name)
{
    bool named_request = builtin == QA_QC_BUILTIN_NAMED
        || (builtin >= QA_QC_BUILTIN_EX_BPRINT
            && builtin <= QA_QC_BUILTIN_EX_CLEARPROMPT);
    for (size_t i = 0; i < instance->options.host.builtin_count; ++i) {
        const qa_qc_builtin_binding *candidate = &instance->bindings[i];
        if (candidate->call == NULL) continue;
        if (candidate->builtin == builtin && builtin != QA_QC_BUILTIN_NAMED)
            return candidate;
        if (named_request && candidate->builtin == QA_QC_BUILTIN_NAMED
            && name != NULL
            && candidate->name != NULL && strcmp(candidate->name, name) == 0)
            return candidate;
    }
    return NULL;
}

static const char *named_binding_identity(const qa_qc_instance *instance,
                                          const qa_qc_builtin_binding *binding)
{
    if (binding->builtin == QA_QC_BUILTIN_NAMED) return binding->name;
    size_t count;
    const qa_qc_builtin_requirement *requirements = qa_qc_builtin_requirements(
        instance->options.profile, &count);
    for (size_t i = 0; i < count; ++i)
        if (requirements[i].number == 0
            && requirements[i].builtin == binding->builtin)
            return requirements[i].name;
    return NULL;
}

static bool copy_host(qa_qc_instance *instance, qa_error *error)
{
    size_t binding_count = instance->options.host.builtin_count;
    if (binding_count != 0) {
        if (instance->options.host.builtins == NULL
            || binding_count > SIZE_MAX / sizeof(*instance->bindings))
            return qc_fail(error, QA_ERROR_ARGUMENT, 0, "invalid QuakeC builtin bindings");
        instance->bindings = calloc(binding_count, sizeof(*instance->bindings));
        if (instance->bindings == NULL)
            return qc_fail(error, QA_ERROR_MEMORY, 0, "Cannot allocate QuakeC builtin bindings");
        for (size_t i = 0; i < binding_count; ++i) {
            qa_qc_builtin_binding source = instance->options.host.builtins[i];
            instance->bindings[i] = source;
            instance->bindings[i].name = NULL;
            if (source.call == NULL)
                return qc_fail(error, QA_ERROR_ARGUMENT, i, "QuakeC builtin binding has no callback");
            if (source.builtin < QA_QC_BUILTIN_SETORIGIN
                || source.builtin > QA_QC_BUILTIN_NAMED
                || (source.builtin == QA_QC_BUILTIN_NAMED
                    && (source.name == NULL || source.name[0] == '\0')))
                return qc_fail(error, QA_ERROR_ARGUMENT, i,
                               "QuakeC builtin binding has an invalid identity");
            for (size_t previous = 0; previous < i; ++previous) {
                bool duplicate = source.builtin == instance->bindings[previous].builtin;
                const char *source_name = named_binding_identity(instance,
                                                                 &source);
                const char *previous_name = named_binding_identity(
                    instance, &instance->bindings[previous]);
                if (source.builtin == QA_QC_BUILTIN_NAMED
                    && instance->bindings[previous].builtin
                        == QA_QC_BUILTIN_NAMED)
                    duplicate = strcmp(source.name, previous_name) == 0;
                else if (source_name != NULL && previous_name != NULL)
                    duplicate = strcmp(source_name, previous_name) == 0;
                if (duplicate)
                    return qc_fail(error, QA_ERROR_ARGUMENT, i,
                                   "QuakeC builtin binding is duplicated");
            }
            if (source.name != NULL) {
                char *name = qc_strdup(source.name, error);
                if (name == NULL) return false;
                instance->bindings[i].name = name;
            }
        }
        instance->options.host.builtins = instance->bindings;
    }
    size_t extension_count = instance->options.host.extension_count;
    if (extension_count != 0) {
        if (instance->options.host.extensions == NULL
            || extension_count > SIZE_MAX / sizeof(*instance->extensions))
            return qc_fail(error, QA_ERROR_ARGUMENT, 0, "invalid QuakeC extension list");
        instance->extensions = calloc(extension_count, sizeof(*instance->extensions));
        if (instance->extensions == NULL)
            return qc_fail(error, QA_ERROR_MEMORY, 0, "Cannot allocate QuakeC extensions");
        for (size_t i = 0; i < extension_count; ++i) {
            const char *extension = instance->options.host.extensions[i];
            if (extension == NULL || extension[0] == '\0')
                return qc_fail(error, QA_ERROR_ARGUMENT, i, "QuakeC extension name is missing");
            for (size_t previous = 0; previous < i; ++previous)
                if (strcmp(instance->extensions[previous], extension) == 0)
                    return qc_fail(error, QA_ERROR_ARGUMENT, i,
                                   "QuakeC extension name is duplicated");
            instance->extensions[i] = qc_strdup(extension, error);
            if (instance->extensions[i] == NULL) return false;
        }
        instance->options.host.extensions = (const char *const *)instance->extensions;
    }
    return true;
}

static uint32_t function_end(const qa_qc_program *program,
                             const qa_qc_function *function)
{
    uint32_t end = program->info.statement_count;
    for (uint32_t i = 1; i < program->info.function_count; ++i) {
        const qa_qc_function *candidate = &program->functions[i];
        if (candidate->first_statement > function->first_statement
            && (uint32_t)candidate->first_statement < end)
            end = (uint32_t)candidate->first_statement;
    }
    return end;
}

static bool copy_inline_regions(qa_qc_instance *instance, qa_error *error)
{
    size_t count = instance->options.inline_region_count;
    if (count == 0) return true;
    if (instance->options.inline_regions == NULL
        || count > SIZE_MAX / sizeof(*instance->inline_regions))
        return qc_fail(error, QA_ERROR_ARGUMENT, 0,
                       "invalid QuakeC inline region list");
    instance->inline_regions = malloc(count * sizeof(*instance->inline_regions));
    if (instance->inline_regions == NULL)
        return qc_fail(error, QA_ERROR_MEMORY, 0,
                       "Cannot allocate QuakeC inline regions");
    for (size_t i = 0; i < count; ++i) {
        qa_qc_inline_region region = instance->options.inline_regions[i];
        const qa_qc_function *function = qa_qc_program_function(
            instance->program, region.function);
        if (function == NULL || region.function == 0
            || function->named_builtin || function->first_statement <= 0) {
            qa_error_set(error, QA_ERROR_ARGUMENT, i,
                         "inline region requires an interpreted QuakeC function");
            return false;
        }
        uint32_t end = function_end(instance->program, function);
        if (region.entry < (uint32_t)function->first_statement
            || region.exit <= region.entry || region.exit >= end
            || region.saved_scope < QA_QC_INLINE_NOT_STANDALONE
            || region.saved_scope > QA_QC_INLINE_GLOBAL) {
            qa_error_set(error, QA_ERROR_ARGUMENT, i,
                         "invalid QuakeC inline source region");
            return false;
        }
        for (size_t previous = 0; previous < i; ++previous)
            if (instance->inline_regions[previous].function == region.function
                && instance->inline_regions[previous].entry == region.entry) {
                qa_error_set(error, QA_ERROR_ARGUMENT, i,
                             "duplicate QuakeC inline region entry");
                return false;
            }
        if (region.saved_scope == QA_QC_INLINE_FRAME) {
            if (region.saved_word < function->parameter_start
                || region.saved_word >= function->parameter_start
                                      + function->local_words) {
                qa_error_set(error, QA_ERROR_ARGUMENT, i,
                             "inline frame result is outside function locals");
                return false;
            }
        } else if (region.saved_scope == QA_QC_INLINE_GLOBAL) {
            bool declared = false;
            for (uint32_t global = 0;
                 global < instance->program->info.global_count; ++global) {
                const qa_qc_definition *definition =
                    &instance->program->globals[global];
                if (definition->offset == region.saved_word
                    && definition->type == QA_QC_FLOAT) {
                    declared = true;
                    break;
                }
            }
            if (region.saved_word < QC_RESERVED_WORDS
                || region.saved_word >= instance->program->info.global_words
                || (region.saved_word >= function->parameter_start
                    && region.saved_word < function->parameter_start
                                           + function->local_words)
                || !declared) {
                qa_error_set(error, QA_ERROR_ARGUMENT, i,
                             "inline global result is not a standalone float");
                return false;
            }
        }
        instance->inline_regions[i] = region;
    }
    instance->options.inline_regions = instance->inline_regions;
    return true;
}

static void free_instance(qa_qc_instance *instance)
{
    if (instance == NULL) return;
    if (instance->bindings != NULL)
        for (size_t i = 0; i < instance->options.host.builtin_count; ++i)
            free((char *)instance->bindings[i].name);
    if (instance->extensions != NULL)
        for (size_t i = 0; i < instance->options.host.extension_count; ++i)
            free(instance->extensions[i]);
    free(instance->bindings); free(instance->extensions);
    free(instance->inline_regions);
    qc_strings_destroy(&instance->strings);
    free(instance->globals); free(instance->entities); free(instance->slots);
    free(instance->bodies); free(instance->profiles); free(instance->frames);
    free(instance->locals); free(instance);
}

bool qa_qc_instance_create(const qa_qc_program *program,
                           const qa_qc_options *options,
                           qa_qc_instance **out, qa_error *error)
{
    if (program == NULL || options == NULL || out == NULL
        || (options->host.declared_projection && !options->host.prepare_entity)
        || !valid_profile(program, options->profile))
        return qc_fail(error, QA_ERROR_ARGUMENT, 0, "invalid QuakeC program profile or output");
    if (options->host.world != NULL
        && (options->host.session == NULL
            || qa_world_actors(options->host.world)
                != qa_session_actor_registry(options->host.session)
            || (qa_session_world(options->host.session) != NULL
                && qa_session_world(options->host.session) != options->host.world)))
        return qc_fail(error, QA_ERROR_ARGUMENT, 0,
                       "QuakeC host world is not the session's shared world");
    qa_qc_instance *instance = calloc(1, sizeof(*instance));
    if (instance == NULL) return qc_fail(error, QA_ERROR_MEMORY, 0, "Cannot allocate QuakeC instance");
    instance->program = program;
    instance->options = *options;
    if (instance->options.statement_limit == 0) instance->options.statement_limit = 100000;
    if (instance->options.call_limit == 0) instance->options.call_limit = 32;
    if (instance->options.local_word_limit == 0) instance->options.local_word_limit = 2048;
    if (instance->options.entity_capacity == 0) instance->options.entity_capacity = 8192;
    if (instance->options.first_dynamic_slot == 0)
        instance->options.first_dynamic_slot = 1;
    instance->layout = options->entity_layout.stride_bytes == 0
        ? qa_qc_default_entity_layout(program, options->profile) : options->entity_layout;
    uint64_t entity_bytes = (uint64_t)instance->options.entity_capacity * instance->layout.stride_bytes;
    if (program->info.global_words > UINT32_MAX / 4u
        || (program->info.function_count != 0
            && SIZE_MAX / program->info.function_count < sizeof(*instance->profiles))
        || instance->layout.field_words != program->info.entity_field_words
        || instance->layout.stride_bytes == 0
        || (instance->layout.stride_bytes & 3u) != 0
        || (instance->layout.variables_offset_bytes & 3u) != 0
        || instance->layout.variables_offset_bytes < 8u
        || instance->layout.variables_offset_bytes
            + (uint64_t)instance->layout.field_words * 4u > instance->layout.stride_bytes
        || entity_bytes > SIZE_MAX || entity_bytes > INT32_MAX
        || instance->options.first_dynamic_slot > instance->options.entity_capacity
        || (instance->options.entity_capacity != 0
            && SIZE_MAX / instance->options.entity_capacity < sizeof(*instance->slots))
        || (instance->options.entity_capacity != 0
            && SIZE_MAX / instance->options.entity_capacity < sizeof(*instance->bodies))
        || (instance->options.call_limit != 0
            && SIZE_MAX / instance->options.call_limit < sizeof(*instance->frames))
        || (instance->options.local_word_limit != 0
            && SIZE_MAX / instance->options.local_word_limit < sizeof(*instance->locals))) {
        free_instance(instance);
        return qc_fail(error, QA_ERROR_ARGUMENT, 0, "invalid QuakeC entity layout or limits");
    }
    size_t global_bytes = (size_t)program->info.global_words * 4u;
    instance->globals = malloc(global_bytes);
    instance->entities = calloc(1, (size_t)entity_bytes);
    instance->slots = calloc(instance->options.entity_capacity, sizeof(*instance->slots));
    instance->bodies = calloc(instance->options.entity_capacity, sizeof(*instance->bodies));
    instance->profiles = calloc(program->info.function_count, sizeof(*instance->profiles));
    instance->frames = calloc(instance->options.call_limit, sizeof(*instance->frames));
    instance->locals = calloc(instance->options.local_word_limit, sizeof(*instance->locals));
    if (instance->globals == NULL || instance->entities == NULL || instance->slots == NULL
        || instance->bodies == NULL || instance->profiles == NULL || instance->frames == NULL
        || instance->locals == NULL) {
        free_instance(instance);
        return qc_fail(error, QA_ERROR_MEMORY, 0, "Cannot allocate QuakeC private state");
    }
    memcpy(instance->globals, program->initial_globals, global_bytes);
    if (!qc_strings_create(&instance->strings,
            (qa_bytes){program->strings, program->string_bytes},
            program->info.api == QA_QC_API_QUAKEWORLD, error)
        || !copy_host(instance, error)
        || !copy_inline_regions(instance, error)) {
        free_instance(instance); return false;
    }
    instance->slots[0] = (qc_slot){.kind = QA_QC_SLOT_WORLD};
    instance->entity_count = instance->options.first_dynamic_slot;
    for (uint32_t slot = 1; slot < instance->entity_count; ++slot)
        set_free_metadata(instance, slot, true, 0.0f);
    instance->next_invocation = 0;
    instance->random_state = qa_load_u32le(program->info.digest.bytes);
    if (instance->random_state == 0) instance->random_state = UINT32_C(0x6d2b79f5);
    for (uint32_t i = 0; i < instance->options.entity_capacity; ++i) {
        instance->bodies[i].instance = instance;
        instance->bodies[i].slot = i;
    }
    if (instance->options.require_complete_host_profile) {
        size_t count;
        const qa_qc_builtin_requirement *requirements =
            qa_qc_builtin_requirements(instance->options.profile, &count);
        for (size_t i = 0; i < count; ++i) {
            if (!qc_builtin_available(instance, &requirements[i])) {
                qa_error_set(error, QA_ERROR_UNSUPPORTED, i,
                             "missing required QuakeC host builtin %s",
                             requirements[i].name);
                free_instance(instance); return false;
            }
        }
    }
    *out = instance;
    return true;
}

bool qa_qc_idle(const qa_qc_instance *instance)
{
    return instance != NULL && instance->execution_depth == 0
        && instance->callback_depth == 0 && instance->frame_count == 0
        && instance->boundary == NULL && instance->cancelling == NULL
        && instance->inline_boundary == NULL && !instance->checkpointing;
}

bool qa_qc_instance_destroy(qa_qc_instance *instance, qa_error *error)
{
    if (instance == NULL) return true;
    if (!qa_qc_idle(instance))
        return qc_fail(error, QA_ERROR_ARGUMENT, 0, "QuakeC destruction requires an idle boundary");
    if ((instance->options.host.combat != NULL
         && !qa_combat_idle(instance->options.host.combat))
        || (instance->options.host.pickups != NULL
            && !qa_pickups_idle(instance->options.host.pickups)))
        return qc_fail(error, QA_ERROR_ARGUMENT, 0,
                       "QuakeC destruction requires idle shared gameplay");
    instance->destroying = true;
    if (instance->options.host.session != NULL) {
        for (uint32_t slot = 1; slot < instance->entity_count; ++slot) {
            qc_slot binding = instance->slots[slot];
            if (binding.kind != QA_QC_SLOT_OWNED) continue;
            const qa_actor_record *record = qa_actors_get(
                qa_session_actors(instance->options.host.session), binding.actor);
            if (record != NULL && !qa_session_release(instance->options.host.session,
                                                       binding.actor, error)) {
                instance->destroying = false;
                return false;
            }
            instance->slots[slot] = (qc_slot){0};
        }
    }
    free_instance(instance);
    return true;
}

const qa_qc_program *qa_qc_instance_program(const qa_qc_instance *instance)
{
    return instance == NULL ? NULL : instance->program;
}

qa_qc_profile qa_qc_instance_profile(const qa_qc_instance *instance)
{
    return instance == NULL ? QA_QC_NETQUAKE : instance->options.profile;
}

uint32_t qa_qc_entity_count(const qa_qc_instance *instance)
{
    return instance == NULL ? 0 : instance->entity_count;
}

uint32_t qa_qc_argument_count(const qa_qc_instance *instance)
{
    return instance == NULL ? 0 : instance->argument_count;
}

static bool actor_live(const qa_qc_instance *instance, qa_actor_id actor,
                       const qa_actor_record **out)
{
    if (instance->options.host.session == NULL) return false;
    const qa_actor_record *record = qa_actors_get(
        qa_session_actors(instance->options.host.session), actor);
    if (record == NULL) return false;
    if (out != NULL) *out = record;
    return true;
}

static bool read_body_optional(qa_world *world, qa_actor_id actor,
                               qa_body_state *out, bool *found,
                               qa_error *error)
{
    qa_error failure = {0};
    if (qa_world_body_read(world, actor, out, &failure)) {
        *found = true;
        return true;
    }
    if (failure.code == QA_ERROR_NOT_FOUND) {
        *found = false;
        return true;
    }
    if (failure.code == QA_OK)
        qa_error_set(&failure, QA_ERROR_ARGUMENT, actor.slot,
                     "Shared body callback failed without an error");
    if (error != NULL) *error = failure;
    return false;
}

/* Canonical-to-guest projection is synchronization, not a guest store. It
 * deliberately bypasses the store observer to avoid feeding a write-through
 * adapter back into the shared authority that originated the value. */
static bool raw_entity_vector(qa_qc_instance *instance, uint32_t slot,
                              uint32_t word, qa_vec3 value, qa_error *error)
{
    if (!qc_entity_range(instance, slot, word, 3, error)) return false;
    uint8_t *fields = qc_entity_words(instance, slot);
    qc_store_float(fields, word, value.x);
    qc_store_float(fields, word + 1u, value.y);
    qc_store_float(fields, word + 2u, value.z);
    return true;
}

static bool raw_entity_int(qa_qc_instance *instance, uint32_t slot,
                           uint32_t word, int32_t value, qa_error *error)
{
    if (!qc_entity_range(instance, slot, word, 1, error)) return false;
    qc_store_word(qc_entity_words(instance, slot), word, (uint32_t)value);
    return true;
}

static bool raw_body_vector(qa_qc_instance *instance, uint32_t slot,
                            const char *name, qa_vec3 value,
                            qa_error *error)
{
    const qa_qc_definition *field = qa_qc_program_find_field(
        instance->program, name);
    if (field == NULL) return true;
    if (field->type != QA_QC_VECTOR)
        return qc_fail(error, QA_ERROR_FORMAT, field->offset,
                       "QuakeC body field is not a vector");
    return raw_entity_vector(instance, slot, field->offset, value, error);
}

static double source_time(const qa_qc_instance *instance)
{
    if (instance->options.host.source_time_seconds)
        return instance->options.host.source_time_seconds(instance->options.host.context);
    const qa_qc_definition *time = qa_qc_program_find_global(
        instance->program, "time");
    if (time == NULL || time->type != QA_QC_FLOAT
        || time->offset >= instance->program->info.global_words)
        return 0.0f;
    float value = qc_load_float(instance->globals, time->offset);
    return isfinite(value) ? value : 0.0f;
}

static bool slot_matches(const qa_qc_instance *instance, uint32_t slot,
                         qa_qc_slot_kind kind, qa_actor_id actor)
{
    return slot < instance->entity_count
        && instance->slots[slot].kind == kind
        && qa_actor_id_equal(instance->slots[slot].actor, actor);
}

static void clear_freed_fields(qa_qc_instance *instance, uint32_t slot)
{
    static const char *const scalars[] = {
        "model", "takedamage", "modelindex", "colormap", "skin", "frame",
        "solid"
    };
    uint8_t *fields = qc_entity_words(instance, slot);
    for (size_t i = 0; i < sizeof(scalars) / sizeof(*scalars); ++i) {
        const qa_qc_definition *field = qa_qc_program_find_field(
            instance->program, scalars[i]);
        if (field != NULL && field->offset < instance->layout.field_words)
            qc_store_word(fields, field->offset, 0u);
    }
    const qa_qc_definition *origin = qa_qc_program_find_field(
        instance->program, "origin");
    const qa_qc_definition *angles = qa_qc_program_find_field(
        instance->program, "angles");
    const qa_qc_definition *nextthink = qa_qc_program_find_field(
        instance->program, "nextthink");
    if (origin != NULL && origin->type == QA_QC_VECTOR
        && (uint64_t)origin->offset + 3u <= instance->layout.field_words)
        for (uint32_t i = 0; i < 3u; ++i)
            qc_store_word(fields, origin->offset + i, 0u);
    if (angles != NULL && angles->type == QA_QC_VECTOR
        && (uint64_t)angles->offset + 3u <= instance->layout.field_words)
        for (uint32_t i = 0; i < 3u; ++i)
            qc_store_word(fields, angles->offset + i, 0u);
    if (nextthink != NULL && nextthink->offset < instance->layout.field_words)
        qc_store_float(fields, nextthink->offset, -1.0f);
}

static void mark_slot_freed(qa_qc_instance *instance, uint32_t slot)
{
    if (slot == 0 || slot >= instance->entity_count) return;
    clear_freed_fields(instance, slot);
    set_free_metadata(instance, slot, true, (float)source_time(instance));
    instance->slots[slot] = (qc_slot){0};
}

static bool slot_reusable(const qa_qc_instance *instance, uint32_t slot,
                          double now)
{
    if (instance->slots[slot].kind != QA_QC_SLOT_FREE) return false;
    const uint8_t *edict = instance->entities
        + (size_t)slot * instance->layout.stride_bytes;
    float freed_at = qa_load_f32le(edict
        + instance->layout.variables_offset_bytes - 4u);
    return !isfinite(freed_at) || freed_at < 2.0f || now - freed_at > 0.5f;
}

static bool available_slot(qa_qc_instance *instance, bool qw_overwrite,
                           uint32_t *out, qa_error *error)
{
    double now = source_time(instance);
    for (uint32_t slot = instance->options.first_dynamic_slot;
         slot < instance->entity_count; ++slot) {
        if (slot_reusable(instance, slot, now)) {
            *out = slot;
            return true;
        }
    }
    if (instance->entity_count < instance->options.entity_capacity) {
        *out = instance->entity_count;
        return true;
    }
    if (!qw_overwrite || instance->options.profile != QA_QC_QUAKEWORLD
        || instance->options.first_dynamic_slot
            >= instance->options.entity_capacity)
        return qc_fail(error, QA_ERROR_MEMORY, 0,
                       "QuakeC entity capacity exhausted");

    uint32_t slot = instance->options.entity_capacity - 1u;
    qc_slot previous = instance->slots[slot];
    if (previous.kind == QA_QC_SLOT_OWNED) {
        if (instance->options.host.session == NULL
            || !qa_session_release(instance->options.host.session,
                                   previous.actor, error)) return false;
        if (slot_matches(instance, slot, QA_QC_SLOT_OWNED, previous.actor))
            mark_slot_freed(instance, slot);
    } else if (previous.kind == QA_QC_SLOT_BORROWED) {
        mark_slot_freed(instance, slot);
    }
    if (instance->slots[slot].kind != QA_QC_SLOT_FREE)
        return qc_fail(error, QA_ERROR_ARGUMENT, slot,
                       "QuakeWorld overflow slot changed during release");
    *out = slot;
    return true;
}

static bool refresh_borrowed_impl(qa_qc_instance *instance, uint32_t slot,
                                  qa_error *error)
{
    qc_body_context *context = &instance->bodies[slot];
    if (context->refreshing) return true;
    qa_actor_id actor = instance->slots[slot].actor;
    context->refreshing = true;
    qa_body_state body;
    bool has_body;
    bool body_ok = read_body_optional(instance->options.host.world, actor,
                                      &body, &has_body, error);
    if (!body_ok || !has_body) {
        context->refreshing = false;
        if (!slot_matches(instance, slot, QA_QC_SLOT_BORROWED, actor))
            return qc_fail(error, QA_ERROR_NOT_FOUND, slot,
                           "Borrowed QuakeC actor changed during body refresh");
        return body_ok;
    }
    if (!slot_matches(instance, slot, QA_QC_SLOT_BORROWED, actor)) {
        context->refreshing = false;
        return qc_fail(error, QA_ERROR_NOT_FOUND, slot,
                       "Borrowed QuakeC actor changed during body refresh");
    }
    bool ok = raw_body_vector(instance, slot, "origin", body.origin, error)
        && raw_body_vector(instance, slot, "angles", body.angles, error)
        && raw_body_vector(instance, slot, "velocity", body.velocity, error)
        && raw_body_vector(instance, slot, "mins", body.bounds.mins, error)
        && raw_body_vector(instance, slot, "maxs", body.bounds.maxs, error)
        && raw_body_vector(instance, slot, "size",
                           qa_vec_sub(body.bounds.maxs, body.bounds.mins), error);
    qa_linked_body linked;
    qa_bounds absolute = qa_world_linked(instance->options.host.world, actor,
                                         &linked)
        ? linked.absolute_bounds
        : (qa_bounds){qa_vec_add(body.origin, body.bounds.mins),
                      qa_vec_add(body.origin, body.bounds.maxs)};
    ok = ok && raw_body_vector(instance, slot, "absmin", absolute.mins, error)
        && raw_body_vector(instance, slot, "absmax", absolute.maxs, error);
    const qa_qc_definition *field = qa_qc_program_find_field(
        instance->program, "groundentity");
    if (ok && field != NULL) {
        if (field->type != QA_QC_ENTITY) {
            context->refreshing = false;
            return qc_fail(error, QA_ERROR_FORMAT, field->offset,
                           "QuakeC groundentity field has the wrong type");
        }
        int32_t ground = 0;
        if (body.ground.registry != 0
            && !qa_qc_actor_reference(instance, body.ground, true,
                                      &ground, error)) ok = false;
        if (ok && !slot_matches(instance, slot, QA_QC_SLOT_BORROWED,
                                actor)) {
            ok = qc_fail(error, QA_ERROR_NOT_FOUND, slot,
                         "Borrowed QuakeC actor changed during body refresh");
        }
        if (ok && (!source_ground(instance, slot) || body.ground.registry!=0) &&
            !raw_entity_int(instance, slot, field->offset,
                                  ground, error)) ok = false;
    }
    context->refreshing = false;
    return ok;
}

bool qc_refresh_borrowed(qa_qc_instance *instance, uint32_t slot,
                         qa_error *error)
{
    if (slot >= instance->entity_count
        || instance->slots[slot].kind != QA_QC_SLOT_BORROWED
        || instance->options.host.world == NULL || instance->options.host.declared_projection) return true;
    if (instance->callback_depth == UINT32_MAX)
        return qc_fail(error, QA_ERROR_ARGUMENT, slot,
                       "QuakeC projection callback depth exhausted");
    ++instance->callback_depth;
    bool ok = refresh_borrowed_impl(instance, slot, error);
    --instance->callback_depth;
    return ok;
}

bool qc_prepare_entity_access(qa_qc_instance *instance, uint32_t slot,
                              uint32_t word, uint32_t count,
                              qa_qc_entity_access_kind kind,
                              qa_error *error)
{
    if (!qc_entity_range(instance, slot, word, count, error)) return false;
    /* A committed-store observer must see the transaction it is observing,
     * even if it reads adjacent fields to update a shared authority. */
    if (instance->store_observer_depth != 0) return true;
    qc_slot before = instance->slots[slot];
    if (!qc_refresh_borrowed(instance, slot, error)) return false;
    if (instance->options.host.prepare_entity == NULL
        || instance->entity_access_depth != 0) return true;
    qa_qc_entity_access access = {
        .kind = kind,
        .binding = {before.kind, slot, before.actor, before.owner,
                    before.source_slot},
        .reference = reference_of(instance, slot),
        .word = word,
        .count = count
    };
    qa_error failure = {0};
    ++instance->callback_depth;
    ++instance->entity_access_depth;
    bool ok = instance->options.host.prepare_entity(
        instance->options.host.context, instance, &access, &failure);
    --instance->entity_access_depth;
    --instance->callback_depth;
    if (!ok) {
        if (failure.code == QA_OK)
            qa_error_set(&failure, QA_ERROR_ARGUMENT, word,
                         "QuakeC entity projection callback failed");
        if (error != NULL) *error = failure;
        return false;
    }
    qc_slot after = instance->slots[slot];
    if (after.kind != before.kind
        || (before.kind != QA_QC_SLOT_FREE
            && before.kind != QA_QC_SLOT_WORLD
            && !qa_actor_id_equal(after.actor, before.actor)))
        return qc_fail(error, QA_ERROR_NOT_FOUND, slot,
                       "QuakeC entity changed during projection");
    return true;
}

bool qa_qc_bind_actor(qa_qc_instance *instance, uint32_t slot,
                      qa_actor_id actor, qa_qc_slot_kind kind,
                      qa_error *error)
{
    bool capture_projection = instance != NULL && instance->checkpointing
        && instance->capturing_checkpoint && instance->callback_depth != 0
        && kind == QA_QC_SLOT_BORROWED;
    if (instance == NULL || instance->destroying
        || (instance->checkpointing && !capture_projection)
        || slot == 0 || slot >= instance->options.entity_capacity
        || (kind != QA_QC_SLOT_OWNED && kind != QA_QC_SLOT_BORROWED))
        return qc_fail(error, QA_ERROR_ARGUMENT, slot, "invalid QuakeC actor binding");
    const qa_actor_record *record;
    if (!actor_live(instance, actor, &record))
        return qc_fail(error, QA_ERROR_NOT_FOUND, slot, "QuakeC actor is not live");
    if (instance->slots[slot].kind != QA_QC_SLOT_FREE)
        return qc_fail(error, QA_ERROR_ARGUMENT, slot, "QuakeC entity slot is occupied");
    for (uint32_t i = 1; i < instance->entity_count; ++i)
        if (instance->slots[i].kind != QA_QC_SLOT_FREE
            && qa_actor_id_equal(instance->slots[i].actor, actor))
            return qc_fail(error, QA_ERROR_ARGUMENT, slot, "Actor already has a QuakeC projection");
    if (kind == QA_QC_SLOT_OWNED && (record->owner != instance->options.host.owner
        || !record->has_source || record->source_slot != slot))
        return qc_fail(error, QA_ERROR_ARGUMENT, slot, "Owned QuakeC actor source slot disagrees");
    uint32_t previous_count = instance->entity_count;
    for (uint32_t gap = previous_count; gap < slot; ++gap)
        set_free_metadata(instance, gap, true, 0.0f);
    memset(edict_bytes(instance, slot), 0, instance->layout.stride_bytes);
    set_free_metadata(instance, slot, false, 0.0f);
    instance->slots[slot] = (qc_slot){kind, actor, record->owner,
                                      record->has_source ? record->source_slot : 0};
    if (instance->entity_count <= slot) instance->entity_count = slot + 1u;
    bool synchronized = kind == QA_QC_SLOT_BORROWED
        ? qc_refresh_borrowed(instance, slot, error)
        : qc_sync_body_from_fields(instance, slot, error);
    if (!synchronized) {
        if (slot_matches(instance, slot, kind, actor))
            mark_slot_freed(instance, slot);
        return false;
    }
    if (!slot_matches(instance, slot, kind, actor))
        return qc_fail(error, QA_ERROR_NOT_FOUND, slot,
                       "QuakeC actor changed while it was being bound");
    return true;
}

bool qa_qc_reserved_actor_rebind_ready(const qa_qc_instance *instance)
{
    return instance && !instance->destroying && !instance->checkpointing;
}
bool qa_qc_rebind_reserved_actor(qa_qc_instance *instance, uint32_t slot,
                                  qa_actor_id previous, qa_actor_id actor,
                                  qa_qc_slot_kind kind, qa_error *error)
{
    if (!qa_qc_reserved_actor_rebind_ready(instance) || !slot ||
        slot >= instance->options.first_dynamic_slot || slot >= instance->entity_count ||
        (kind != QA_QC_SLOT_OWNED && kind != QA_QC_SLOT_BORROWED))
        return qc_fail(error, QA_ERROR_ARGUMENT, slot, "Invalid reserved QuakeC source handoff");
    qc_slot before = instance->slots[slot];
    if ((before.kind != QA_QC_SLOT_OWNED && before.kind != QA_QC_SLOT_BORROWED) ||
        !qa_actor_id_equal(before.actor, previous))
        return qc_fail(error, QA_ERROR_NOT_FOUND, slot, "Reserved QuakeC handoff changed its source generation");
    const qa_actor_record *record;
    if (!actor_live(instance, actor, &record))
        return qc_fail(error, QA_ERROR_NOT_FOUND, slot, "Reserved QuakeC handoff actor is not live");
    if (kind == QA_QC_SLOT_OWNED && (record->owner != instance->options.host.owner ||
        !record->has_source || record->source_slot != slot))
        return qc_fail(error, QA_ERROR_ARGUMENT, slot, "Reserved QuakeC handoff differs from its source owner");
    for (uint32_t i = 1; i < instance->entity_count; ++i)
        if (i != slot && instance->slots[i].kind != QA_QC_SLOT_FREE &&
            qa_actor_id_equal(instance->slots[i].actor, actor))
            return qc_fail(error, QA_ERROR_ARGUMENT, slot, "Reserved QuakeC actor already owns another source row");
    instance->slots[slot] = (qc_slot){kind, actor, record->owner,
        record->has_source ? record->source_slot : 0};
    set_free_metadata(instance, slot, false, 0.0f);
    bool ok = kind == QA_QC_SLOT_OWNED ? qc_sync_body_from_fields(instance, slot, error) :
        qc_refresh_borrowed(instance, slot, error);
    if (!ok && slot_matches(instance, slot, kind, actor)) instance->slots[slot] = before;
    return ok && (slot_matches(instance, slot, kind, actor) ||
        qc_fail(error, QA_ERROR_NOT_FOUND, slot, "Reserved QuakeC actor changed during source handoff"));
}

bool qa_qc_unbind_actor(qa_qc_instance *instance, uint32_t slot,
                        qa_error *error)
{
    if (instance == NULL || instance->destroying || instance->checkpointing
        || slot == 0 || slot >= instance->entity_count)
        return qc_fail(error, QA_ERROR_ARGUMENT, slot, "invalid QuakeC entity slot");
    if (instance->slots[slot].kind == QA_QC_SLOT_OWNED)
        return qc_fail(error, QA_ERROR_ARGUMENT, slot, "Release an owned actor through the session");
    if (instance->slots[slot].kind == QA_QC_SLOT_FREE) return true;
    mark_slot_freed(instance, slot);
    return true;
}

bool qa_qc_slot(const qa_qc_instance *instance, uint32_t slot,
                qa_qc_slot_binding *out)
{
    if (instance == NULL || out == NULL || slot >= instance->entity_count) return false;
    qc_slot source = instance->slots[slot];
    *out = (qa_qc_slot_binding){source.kind, slot, source.actor,
                                source.owner, source.source_slot};
    return true;
}

bool qa_qc_slot_reference(const qa_qc_instance *instance, uint32_t slot,
                           int32_t *out, qa_error *error)
{
    if (instance == NULL || out == NULL || slot >= instance->entity_count)
        return qc_fail(error, QA_ERROR_ARGUMENT, slot, "invalid QuakeC physical entity reference");
    *out = reference_of(instance, slot);
    return true;
}

bool qa_qc_actor_movement_flags_read(const qa_qc_instance *instance, qa_actor_id actor,
    uint32_t *out, bool *found, qa_error *error)
{
    if (!instance || instance->destroying || !out || !found)
        return qc_fail(error, QA_ERROR_ARGUMENT, actor.slot,
            "QuakeC flags require their retained instance and outputs");
    const qa_actor_record *record;
    if (!actor_live(instance, actor, &record))
        return qc_fail(error, QA_ERROR_NOT_FOUND, actor.slot,
            "QuakeC flags actor is not live");
    for (uint32_t slot = 1; slot < instance->entity_count; ++slot) {
        qc_slot binding = instance->slots[slot];
        if ((binding.kind != QA_QC_SLOT_OWNED && binding.kind != QA_QC_SLOT_BORROWED) ||
            !qa_actor_id_equal(binding.actor, actor)) continue;
        if (binding.owner != record->owner || (binding.kind == QA_QC_SLOT_OWNED &&
            (record->owner != instance->options.host.owner || !record->has_source ||
             record->source_slot != slot || binding.source_slot != slot)))
            return qc_fail(error, QA_ERROR_FORMAT, slot,
                "QuakeC flags row lost its actual source owner");
        const qa_qc_definition *field = qa_qc_program_find_field(instance->program, "flags");
        if (!field) break;
        uint32_t flags;
        if (!body_ground_flags(instance, slot, &flags, error)) return false;
        *out = flags;
        *found = true;
        return true;
    }
    *out = 0;
    *found = false;
    return true;
}

bool qa_qc_actor_reference(qa_qc_instance *instance, qa_actor_id actor,
                           bool project, int32_t *out, qa_error *error)
{
    if (instance == NULL || out == NULL || !actor_live(instance, actor, NULL))
        return qc_fail(error, QA_ERROR_NOT_FOUND, 0, "Cannot reference a stale actor from QuakeC");
    for (uint32_t slot = 1; slot < instance->entity_count; ++slot) {
        if (instance->slots[slot].kind != QA_QC_SLOT_FREE
            && qa_actor_id_equal(instance->slots[slot].actor, actor)) {
            if (instance->slots[slot].kind == QA_QC_SLOT_BORROWED
                && !qc_refresh_borrowed(instance, slot, error)) return false;
            *out = reference_of(instance, slot); return true;
        }
    }
    if (!project) return qc_fail(error, QA_ERROR_NOT_FOUND, 0, "Actor has no QuakeC projection");
    uint32_t slot;
    if (!available_slot(instance, false, &slot, error)) return false;
    if (!qa_qc_bind_actor(instance, slot, actor, QA_QC_SLOT_BORROWED, error)) return false;
    *out = reference_of(instance, slot);
    return true;
}

bool qa_qc_reference_actor(const qa_qc_instance *instance, int32_t reference,
                           qa_actor_id *out, qa_error *error)
{
    uint32_t slot;
    if (instance == NULL || out == NULL
        || !qc_entity_slot(instance, reference, &slot, error) || slot == 0
        || instance->slots[slot].kind == QA_QC_SLOT_FREE
        || !actor_live(instance, instance->slots[slot].actor, NULL))
        return qc_fail(error, QA_ERROR_NOT_FOUND, 0, "QuakeC reference has no live actor");
    *out = instance->slots[slot].actor;
    return true;
}

void qa_qc_actor_released(qa_qc_instance *instance, qa_actor_record released)
{
    if (instance == NULL) return;
    for (uint32_t slot = 1; slot < instance->entity_count; ++slot) {
        if (instance->slots[slot].kind != QA_QC_SLOT_FREE
            && qa_actor_id_equal(instance->slots[slot].actor, released.id))
            mark_slot_freed(instance, slot);
    }
}

bool qa_qc_rebind_sources(qa_qc_instance *instance, qa_error *error)
{
    if (instance == NULL || instance->destroying || instance->checkpointing
        || instance->execution_depth != 0 || instance->callback_depth != 0
        || instance->options.host.session == NULL)
        return qc_fail(error, QA_ERROR_ARGUMENT, 0, "QuakeC source rebind needs a session");
    const qa_actor_registry *actors = qa_session_actors(instance->options.host.session);
    for (uint32_t slot = 1; slot < instance->entity_count; ++slot) {
        qc_slot *binding = &instance->slots[slot];
        if (binding->kind != QA_QC_SLOT_OWNED) continue;
        const qa_actor_record *record = qa_actors_at_source(actors,
            instance->options.host.owner, binding->source_slot);
        if (record == NULL)
            return qc_fail(error, QA_ERROR_NOT_FOUND, slot, "Restored QuakeC source actor is missing");
        binding->actor = record->id;
        binding->owner = record->owner;
        if (!qc_sync_body_from_fields(instance, slot, error)) return false;
    }
    return true;
}

static bool field_vector(qa_qc_instance *instance, uint32_t slot,
                         const char *name, qa_vec3 fallback, qa_vec3 *out,
                         qa_error *error)
{
    const qa_qc_definition *field = qa_qc_program_find_field(instance->program, name);
    if (field == NULL) { *out = fallback; return true; }
    if (field->type != QA_QC_VECTOR)
        return qc_fail(error, QA_ERROR_FORMAT, field->offset,
                       "QuakeC body field is not a vector");
    return qa_qc_entity_vector(instance, reference_of(instance, slot),
                               field->offset, out, error);
}

static bool body_read_impl(void *context, qa_body_state *out, qa_error *error)
{
    qc_body_context *body = context;
    qa_qc_instance *instance = body->instance;
    if (body->slot >= instance->entity_count
        || instance->slots[body->slot].kind != QA_QC_SLOT_OWNED)
        return qc_fail(error, QA_ERROR_NOT_FOUND, body->slot, "QuakeC body binding is retired");
    qa_actor_id actor = instance->slots[body->slot].actor;
    qa_body_state state = {0};
    if (!field_vector(instance, body->slot, "origin", qa_v3(0,0,0), &state.origin, error)
        || !field_vector(instance, body->slot, "angles", qa_v3(0,0,0), &state.angles, error)
        || !field_vector(instance, body->slot, "velocity", qa_v3(0,0,0), &state.velocity, error)
        || !field_vector(instance, body->slot, "mins", qa_v3(0,0,0), &state.bounds.mins, error)
        || !field_vector(instance, body->slot, "maxs", qa_v3(0,0,0), &state.bounds.maxs, error)) return false;
    const qa_qc_definition *ground = qa_qc_program_find_field(instance->program, "groundentity");
    int32_t reference;
    qa_error ignored = {0};
    if (ground != NULL) {
        if (ground->type != QA_QC_ENTITY)
            return qc_fail(error, QA_ERROR_FORMAT, ground->offset,
                           "QuakeC groundentity field has the wrong type");
        if (!qa_qc_entity_int(instance, reference_of(instance, body->slot),
                              ground->offset, &reference, error)) return false;
        uint32_t flags=512;
        if (source_ground(instance, body->slot) && !body_ground_flags(instance,body->slot,&flags,error)) return false;
        if (reference != 0 && (flags&512u)!=0)
            (void)qa_qc_reference_actor(instance, reference, &state.ground, &ignored);
    }
    if (!slot_matches(instance, body->slot, QA_QC_SLOT_OWNED, actor))
        return qc_fail(error, QA_ERROR_NOT_FOUND, body->slot,
                       "QuakeC body changed during source read");
    *out = state;
    return true;
}

static bool body_read(void *context, qa_body_state *out, qa_error *error)
{
    qc_body_context *body = context;
    qa_qc_instance *instance = body->instance;
    if (instance->callback_depth == UINT32_MAX)
        return qc_fail(error, QA_ERROR_ARGUMENT, body->slot,
                       "QuakeC body callback depth exhausted");
    ++instance->callback_depth;
    bool ok = body_read_impl(context, out, error);
    --instance->callback_depth;
    return ok;
}

static bool body_write_impl(void *context, const qa_body_state *state,
                            qa_error *error)
{
    qc_body_context *body = context;
    qa_qc_instance *instance = body->instance;
    if (body->slot >= instance->entity_count
        || instance->slots[body->slot].kind != QA_QC_SLOT_OWNED)
        return qc_fail(error, QA_ERROR_NOT_FOUND, body->slot, "QuakeC body binding is retired");
    qa_actor_id actor = instance->slots[body->slot].actor;
    if (source_ground(instance, body->slot)) {
        uint32_t value;
        if (!body_ground_flags(instance,body->slot,&value,error)) return false;
        const qa_qc_definition *flags=qa_qc_program_find_field(instance->program,"flags");
        /* A zero canonical actor also represents world ground. Only the
         * actual physics/control flags owner can clear that distinction. */
        if (flags && state->ground.registry) qc_store_float(qc_entity_words(instance,body->slot),flags->offset,
            (float)(int32_t)(value|512u));
    }
#define WRITE_VECTOR(name_, value_) do { \
    if (!raw_body_vector(instance, body->slot, name_, value_, error)) return false; \
} while (0)
    WRITE_VECTOR("origin", state->origin); WRITE_VECTOR("angles", state->angles);
    WRITE_VECTOR("velocity", state->velocity); WRITE_VECTOR("mins", state->bounds.mins);
    WRITE_VECTOR("maxs", state->bounds.maxs);
    WRITE_VECTOR("size", qa_vec_sub(state->bounds.maxs, state->bounds.mins));
#undef WRITE_VECTOR
    const qa_qc_definition *field = qa_qc_program_find_field(
        instance->program, "groundentity");
    if (field != NULL) {
        if (field->type != QA_QC_ENTITY)
            return qc_fail(error, QA_ERROR_FORMAT, field->offset,
                           "QuakeC groundentity field has the wrong type");
        int32_t ground = 0;
        if (state->ground.registry != 0
            && !qa_qc_actor_reference(instance, state->ground, true, &ground, error)) return false;
        if (!slot_matches(instance, body->slot, QA_QC_SLOT_OWNED, actor))
            return qc_fail(error, QA_ERROR_NOT_FOUND, body->slot,
                           "QuakeC body changed during source write");
        if ((!source_ground(instance, body->slot) || state->ground.registry!=0) &&
            !raw_entity_int(instance, body->slot, field->offset, ground, error)) return false;
    }
    return slot_matches(instance, body->slot, QA_QC_SLOT_OWNED, actor)
        || qc_fail(error, QA_ERROR_NOT_FOUND, body->slot,
                   "QuakeC body changed during source write");
}

static bool body_write(void *context, const qa_body_state *state,
                       qa_error *error)
{
    qc_body_context *body = context;
    qa_qc_instance *instance = body->instance;
    if (instance->callback_depth == UINT32_MAX)
        return qc_fail(error, QA_ERROR_ARGUMENT, body->slot,
                       "QuakeC body callback depth exhausted");
    ++instance->callback_depth;
    bool ok = body_write_impl(context, state, error);
    --instance->callback_depth;
    return ok;
}

static void body_linked(void *context, const qa_linked_body *linked)
{
    qc_body_context *body = context;
    qa_qc_instance *instance = body->instance;
    if (linked == NULL
        || !slot_matches(instance, body->slot, QA_QC_SLOT_OWNED,
                         linked->actor)) return;
    (void)raw_body_vector(instance, body->slot, "absmin",
                          linked->absolute_bounds.mins, NULL);
    (void)raw_body_vector(instance, body->slot, "absmax",
                          linked->absolute_bounds.maxs, NULL);
}

bool qc_sync_body_from_fields(qa_qc_instance *instance, uint32_t slot,
                              qa_error *error)
{
    if (instance->options.host.world == NULL) return true;
    if (slot >= instance->entity_count
        || instance->slots[slot].kind != QA_QC_SLOT_OWNED)
        return qc_fail(error, QA_ERROR_NOT_FOUND, slot,
                       "QuakeC body source is unavailable");
    qa_actor_id actor = instance->slots[slot].actor;
    qa_body_state state;
    if (!body_read(&instance->bodies[slot], &state, error)) return false;
    if (!slot_matches(instance, slot, QA_QC_SLOT_OWNED, actor))
        return qc_fail(error, QA_ERROR_NOT_FOUND, slot,
                       "QuakeC body source changed during synchronization");
    qa_world *world = instance->options.host.world;
    qa_body_state existing;
    bool has_body;
    if (!read_body_optional(world, actor, &existing, &has_body, error)) return false;
    if (!slot_matches(instance, slot, QA_QC_SLOT_OWNED, actor))
        return qc_fail(error, QA_ERROR_NOT_FOUND, slot,
                       "QuakeC body source changed during world read");
    if (!has_body && !qa_world_body_create(world, actor, &state, error)) return false;
    qa_body_binding binding = {
        &instance->bodies[slot], body_read, body_write, body_linked
    };
    return qa_world_body_bind(world, actor, &binding, true, error);
}

bool qc_host_spawn(qa_qc_instance *instance, int32_t *reference,
                   qa_error *error)
{
    if (instance->destroying || instance->checkpointing)
        return qc_fail(error, QA_ERROR_ARGUMENT, 0,
                       "spawn on a destroying QuakeC instance");
    if (instance->options.host.session == NULL || instance->options.host.owner == 0)
        return qc_fail(error, QA_ERROR_UNSUPPORTED, 0, "spawn needs shared session ownership");
    uint32_t slot;
    if (!available_slot(instance, true, &slot, error)) return false;
    qa_actor_id actor;
    if (!qa_session_allocate(instance->options.host.session,
            instance->options.host.owner, instance->options.host.default_definition,
            true, slot, &actor, error)) return false;
    if (!qa_qc_bind_actor(instance, slot, actor, QA_QC_SLOT_OWNED, error)) {
        (void)qa_session_release(instance->options.host.session, actor, NULL);
        if (slot_matches(instance, slot, QA_QC_SLOT_OWNED, actor))
            mark_slot_freed(instance, slot);
        return false;
    }
    *reference = reference_of(instance, slot);
    return true;
}

bool qc_host_remove(qa_qc_instance *instance, int32_t reference,
                    qa_error *error)
{
    uint32_t slot;
    if (!qc_entity_slot(instance, reference, &slot, error) || slot == 0)
        return qc_fail(error, QA_ERROR_ARGUMENT, 0, "remove requires an owned QuakeC actor");
    if (instance->slots[slot].kind == QA_QC_SLOT_FREE) {
        mark_slot_freed(instance, slot);
        return true;
    }
    qa_actor_id removed = instance->slots[slot].actor;
    if (instance->slots[slot].kind != QA_QC_SLOT_OWNED)
        return qc_fail(error, QA_ERROR_ARGUMENT, 0, "remove requires an owned QuakeC actor");
    if (instance->options.host.world && !qa_world_unlink(instance->options.host.world, removed, error)) return false;
    if (!slot_matches(instance, slot, QA_QC_SLOT_OWNED, removed))
        return qc_fail(error, QA_ERROR_NOT_FOUND, slot, "QuakeC actor changed during source unlink");
    mark_slot_freed(instance, slot);
    return qa_session_release(instance->options.host.session, removed, error);
}

static bool may_move(qa_qc_instance *instance, uint32_t slot)
{
    if (instance->options.host.may_move != NULL) {
        qc_slot binding = instance->slots[slot];
        bool allowed = instance->options.host.may_move(instance->options.host.context, binding.actor);
        return allowed && slot_matches(instance, slot, binding.kind, binding.actor)
            && actor_live(instance, binding.actor, NULL);
    }
    if (instance->slots[slot].kind == QA_QC_SLOT_OWNED) return true;
    qa_actor_owner execution;
    return instance->options.host.session != NULL
        && qa_session_execution(instance->options.host.session,
                                instance->slots[slot].actor, &execution)
        && execution == instance->options.host.owner;
}

bool qa_qc_spawn_entity(qa_qc_instance *instance, int32_t *reference, qa_error *error)
{
    if (!instance || !reference)
        return qc_fail(error, QA_ERROR_ARGUMENT, 0, "invalid QC map allocation");
    return qc_host_spawn(instance, reference, error);
}

bool qa_qc_remove_entity(qa_qc_instance *instance, int32_t reference, qa_error *error)
{
    if (!instance)
        return qc_fail(error, QA_ERROR_ARGUMENT, 0, "invalid QC map removal");
    return qc_host_remove(instance, reference, error);
}

static bool set_field_vector(qa_qc_instance *instance, uint32_t slot,
                             const char *name, qa_vec3 value, qa_error *error)
{
    const qa_qc_definition *field = qa_qc_program_find_field(instance->program, name);
    if (field == NULL || field->type != QA_QC_VECTOR)
        return qc_fail(error, QA_ERROR_FORMAT, 0,
                       "required QuakeC vector field is missing");
    return qa_qc_set_entity_vector(instance, reference_of(instance, slot),
                                   field->offset, value, error);
}

static bool setorigin(qa_qc_instance *instance, qa_error *error)
{
    int32_t reference;
    qa_vec3 origin;
    if (!qa_qc_arg_int(instance, 0, &reference, error)
        || !qa_qc_arg_vector(instance, 1, &origin, error)) return false;
    if (!qa_vec_finite(origin))
        return qc_fail(error, QA_ERROR_ARGUMENT, 0, "setorigin requires a finite origin");
    uint32_t slot;
    if (!qc_entity_slot(instance, reference, &slot, error) || slot == 0
        || instance->slots[slot].kind == QA_QC_SLOT_FREE || !may_move(instance, slot))
        return qc_fail(error, QA_ERROR_ARGUMENT, 0, "setorigin lacks actor movement authority");
    qc_slot binding = instance->slots[slot];
    if (!set_field_vector(instance, slot, "origin", origin, error)) return false;
    if (!slot_matches(instance, slot, binding.kind, binding.actor))
        return qc_fail(error, QA_ERROR_NOT_FOUND, slot,
                       "setorigin actor changed during source store");
    if (instance->options.host.world == NULL) return true;
    qa_body_state body;
    if (!qa_world_body_read(instance->options.host.world,
            binding.actor, &body, error)) return false;
    body.origin = origin;
    if (!qa_world_body_write(instance->options.host.world,
            binding.actor, &body, error)
        || !qa_world_link(instance->options.host.world,
            binding.actor, NULL, error)) return false;
    return slot_matches(instance, slot, binding.kind, binding.actor)
        || qc_fail(error, QA_ERROR_NOT_FOUND, slot,
                   "setorigin actor changed during world update");
}

static bool setsize(qa_qc_instance *instance, qa_error *error)
{
    int32_t reference;
    qa_vec3 mins, maxs;
    if (!qa_qc_arg_int(instance, 0, &reference, error)
        || !qa_qc_arg_vector(instance, 1, &mins, error)
        || !qa_qc_arg_vector(instance, 2, &maxs, error)) return false;
    if (!qa_vec_finite(mins) || !qa_vec_finite(maxs)
        || mins.x > maxs.x || mins.y > maxs.y || mins.z > maxs.z)
        return qc_fail(error, QA_ERROR_ARGUMENT, 0, "setsize bounds are inverted");
    uint32_t slot;
    if (!qc_entity_slot(instance, reference, &slot, error) || slot == 0
        || instance->slots[slot].kind == QA_QC_SLOT_FREE || !may_move(instance, slot))
        return qc_fail(error, QA_ERROR_ARGUMENT, 0, "setsize lacks actor movement authority");
    qc_slot binding = instance->slots[slot];
    if (!set_field_vector(instance, slot, "mins", mins, error)) return false;
    if (!slot_matches(instance, slot, binding.kind, binding.actor))
        return qc_fail(error, QA_ERROR_NOT_FOUND, slot,
                       "setsize actor changed during source store");
    if (!set_field_vector(instance, slot, "maxs", maxs, error)) return false;
    if (!slot_matches(instance, slot, binding.kind, binding.actor))
        return qc_fail(error, QA_ERROR_NOT_FOUND, slot,
                       "setsize actor changed during source store");
    const qa_qc_definition *size = qa_qc_program_find_field(instance->program,
                                                             "size");
    if (size != NULL && size->type != QA_QC_VECTOR)
        return qc_fail(error, QA_ERROR_FORMAT, size->offset,
                       "QuakeC size field is not a vector");
    if (size != NULL
        && !qa_qc_set_entity_vector(instance, reference, size->offset,
                                    qa_vec_sub(maxs, mins), error)) return false;
    if (!slot_matches(instance, slot, binding.kind, binding.actor))
        return qc_fail(error, QA_ERROR_NOT_FOUND, slot,
                       "setsize actor changed during source store");
    if (instance->options.host.world == NULL) return true;
    qa_body_state body;
    if (!qa_world_body_read(instance->options.host.world,
            binding.actor, &body, error)) return false;
    body.bounds = (qa_bounds){mins, maxs};
    if (!qa_world_body_write(instance->options.host.world,
            binding.actor, &body, error)
        || !qa_world_link(instance->options.host.world,
            binding.actor, NULL, error)) return false;
    return slot_matches(instance, slot, binding.kind, binding.actor)
        || qc_fail(error, QA_ERROR_NOT_FOUND, slot,
                   "setsize actor changed during world update");
}

static bool set_global_float_named(qa_qc_instance *instance, const char *name,
                                   float value, qa_error *error)
{
    const qa_qc_definition *global = qa_qc_program_find_global(instance->program, name);
    if (global == NULL)
        return qc_fail(error, QA_ERROR_FORMAT, 0,
                       "required QuakeC trace global is missing");
    if (global->type != QA_QC_FLOAT)
        return qc_fail(error, QA_ERROR_FORMAT, global->offset,
                       "QuakeC trace global has the wrong type");
    return qa_qc_set_global_float(instance, global->offset, value, error);
}

static bool set_global_int_named(qa_qc_instance *instance, const char *name,
                                 int32_t value, qa_error *error)
{
    const qa_qc_definition *global = qa_qc_program_find_global(instance->program, name);
    if (global == NULL)
        return qc_fail(error, QA_ERROR_FORMAT, 0,
                       "required QuakeC trace entity global is missing");
    if (global->type != QA_QC_ENTITY)
        return qc_fail(error, QA_ERROR_FORMAT, global->offset,
                       "QuakeC trace entity global has the wrong type");
    return qa_qc_set_global_int(instance, global->offset, value, error);
}

static bool set_global_vector_named(qa_qc_instance *instance, const char *name,
                                    qa_vec3 value, qa_error *error)
{
    const qa_qc_definition *global = qa_qc_program_find_global(instance->program, name);
    if (global == NULL)
        return qc_fail(error, QA_ERROR_FORMAT, 0,
                       "required QuakeC trace vector global is missing");
    if (global->type != QA_QC_VECTOR)
        return qc_fail(error, QA_ERROR_FORMAT, global->offset,
                       "QuakeC trace vector global has the wrong type");
    return qa_qc_set_global_vector(instance, global->offset, value, error);
}

static bool traceline(qa_qc_instance *instance, qa_error *error)
{
    if (instance->options.host.world == NULL)
        return qc_fail(error, QA_ERROR_UNSUPPORTED, 0, "traceline needs the shared world");
    qa_vec3 start, end;
    float no_monsters;
    int32_t pass_reference;
    if (!qa_qc_arg_vector(instance, 0, &start, error)
        || !qa_qc_arg_vector(instance, 1, &end, error)
        || !qa_qc_arg_float(instance, 2, &no_monsters, error)
        || !qa_qc_arg_int(instance, 3, &pass_reference, error)) return false;
    qa_actor_id pass = {0};
    if (pass_reference != 0
        && !qa_qc_reference_actor(instance, pass_reference, &pass, error)) return false;
    qa_trace_query query = {.start = start, .end = end,
        .shape = {QA_SHAPE_POINT, {qa_v3(0,0,0), qa_v3(0,0,0)}},
        .policy = qa_collision_default_policy(QA_COLLISION_Q1),
        .pass_actor = pass};
    float mode = truncf(no_monsters);
    query.policy.q1_move = mode == 2.0f ? QA_Q1_MOVE_MISSILE
        : mode == 1.0f ? QA_Q1_MOVE_NO_MONSTERS : QA_Q1_MOVE_NORMAL;
    qa_trace_result trace;
    if (!qa_world_trace(instance->options.host.world, &query, &trace, error)) return false;
    int32_t hit = 0;
    if (trace.hit == QA_TRACE_HIT_ACTOR
        && !qa_qc_actor_reference(instance, trace.actor, true, &hit, error)) return false;
    return set_global_float_named(instance, "trace_allsolid", trace.all_solid, error)
        && set_global_float_named(instance, "trace_startsolid", trace.start_solid, error)
        && set_global_float_named(instance, "trace_fraction", trace.fraction, error)
        && set_global_vector_named(instance, "trace_endpos", trace.end, error)
        && set_global_vector_named(instance, "trace_plane_normal", trace.plane.normal, error)
        && set_global_float_named(instance, "trace_plane_dist", trace.plane.distance, error)
        && set_global_int_named(instance, "trace_ent", hit, error)
        && set_global_float_named(instance, "trace_inopen", trace.in_open, error)
        && set_global_float_named(instance, "trace_inwater", trace.in_water, error);
}

static bool pointcontents(qa_qc_instance *instance, qa_error *error)
{
    if (instance->options.host.world == NULL)
        return qc_fail(error, QA_ERROR_UNSUPPORTED, 0, "pointcontents needs the shared world");
    qa_vec3 point;
    if (!qa_qc_arg_vector(instance, 0, &point, error)) return false;
    qa_point_query query = {.point = point,
        .policy = qa_collision_default_policy(QA_COLLISION_Q1)};
    qa_point_contents contents;
    if (!qa_world_point_contents(instance->options.host.world, &query, &contents, error)) return false;
    int32_t value = contents.family == QA_COLLISION_Q1 ? contents.contents
        : qa_collision_convert_contents(contents.contents, contents.family, QA_COLLISION_Q1);
    return qa_qc_return_float(instance, (float)value, error);
}

static int reference_order(const void *left, const void *right)
{
    int32_t a = *(const int32_t *)left;
    int32_t b = *(const int32_t *)right;
    return (a > b) - (a < b);
}

static int32_t source_float_int(float value)
{
    if (!isfinite(value)) return INT32_MIN;
    double wrapped = fmod(trunc((double)value), 4294967296.0);
    if (wrapped < 0.0) wrapped += 4294967296.0;
    uint32_t bits = (uint32_t)wrapped;
    int32_t result;
    memcpy(&result, &bits, sizeof(result));
    return result;
}

static bool findradius(qa_qc_instance *instance, qa_error *error)
{
    if (instance->options.host.world == NULL || instance->options.host.session == NULL)
        return qc_fail(error, QA_ERROR_UNSUPPORTED, 0, "findradius needs shared actors and world");
    qa_vec3 origin;
    float radius;
    if (!qa_qc_arg_vector(instance, 0, &origin, error)
        || !qa_qc_arg_float(instance, 1, &radius, error)) return false;
    if (!qa_vec_finite(origin) || !isfinite(radius) || radius < 0)
        return qc_fail(error, QA_ERROR_ARGUMENT, 0, "findradius requires finite bounds");
    const qa_qc_definition *chain = qa_qc_program_find_field(instance->program, "chain");
    const qa_qc_definition *solid = qa_qc_program_find_field(instance->program, "solid");
    if (chain == NULL || chain->type != QA_QC_ENTITY)
        return qc_fail(error, QA_ERROR_FORMAT, 0,
                       "findradius needs the chain entity field");
    if (solid == NULL || solid->type != QA_QC_FLOAT)
        return qc_fail(error, QA_ERROR_FORMAT, 0,
                       "findradius needs the solid float field");
    const qa_actor_registry *actors = qa_session_actors(instance->options.host.session);
    size_t actor_count = qa_actors_count(actors);
    if (actor_count > SIZE_MAX / sizeof(qa_actor_id)
        || actor_count > SIZE_MAX / sizeof(int32_t))
        return qc_fail(error, QA_ERROR_MEMORY, actor_count,
                       "findradius actor snapshot is too large");
    qa_actor_id *snapshot = actor_count == 0 ? NULL
        : malloc(actor_count * sizeof(*snapshot));
    int32_t *references = actor_count == 0 ? NULL
        : malloc(actor_count * sizeof(*references));
    if (actor_count != 0 && (snapshot == NULL || references == NULL)) {
        free(snapshot); free(references);
        return qc_fail(error, QA_ERROR_MEMORY, 0, "Cannot snapshot findradius actors");
    }
    size_t written = 0;
    uint32_t cursor = 0;
    const qa_actor_record *record;
    while (written < actor_count && qa_actors_next(actors, &cursor, &record))
        snapshot[written++] = record->id;
    size_t reference_count = 0;
    for (size_t i = 0; i < written; ++i) {
        if (qa_actors_get(actors, snapshot[i]) == NULL) continue;
        qa_actor_collision collision;
        qa_error collision_error = {0};
        if (!qa_world_get_collision(instance->options.host.world,
                                    snapshot[i], &collision, &collision_error)) {
            if (collision_error.code != QA_OK) {
                if (error) *error = collision_error;
                free(snapshot); free(references); return false;
            }
            continue;
        }
        qa_body_state body;
        bool has_body;
        if (!read_body_optional(instance->options.host.world, snapshot[i],
                                &body, &has_body, error)) {
            free(snapshot); free(references); return false;
        }
        if (!has_body) continue;
        qa_vec3 center = qa_vec_add(body.origin,
            qa_vec_scale(qa_vec_add(body.bounds.mins, body.bounds.maxs), 0.5f));
        if (qa_vec_length(qa_vec_sub(center, origin)) > radius) continue;
        int32_t reference;
        if (!qa_qc_actor_reference(instance, snapshot[i], true,
                                   &reference, error)) {
            free(snapshot); free(references); return false;
        }
        float solidity;
        if (!qa_qc_entity_float(instance, reference, solid->offset, &solidity, error)) {
            free(snapshot); free(references); return false;
        }
        if (solidity == 0.0f) continue;
        references[reference_count++] = reference;
    }
    free(snapshot);
    if (reference_count > 1u)
        qsort(references, reference_count, sizeof(*references), reference_order);
    int32_t head = 0;
    for (size_t i = 0; i < reference_count; ++i) {
        qa_actor_id actor;
        if (!qa_qc_reference_actor(instance, references[i], &actor, NULL)) continue;
        float solidity;
        if (!qa_qc_entity_float(instance, references[i], solid->offset, &solidity, error)) {
            free(references); return false;
        }
        if (solidity == 0.0f) continue;
        qa_actor_collision collision;
        qa_body_state body;
        qa_error collision_error = {0};
        if (!qa_world_get_collision(instance->options.host.world,
                                    actor, &collision, &collision_error)) {
            if (collision_error.code != QA_OK) {
                if (error) *error = collision_error;
                free(references); return false;
            }
            continue;
        }
        bool has_body;
        if (!read_body_optional(instance->options.host.world, actor,
                                &body, &has_body, error)) {
            free(references); return false;
        }
        if (!has_body) continue;
        qa_vec3 center = qa_vec_add(body.origin,
            qa_vec_scale(qa_vec_add(body.bounds.mins, body.bounds.maxs), 0.5f));
        if (qa_vec_length(qa_vec_sub(center, origin)) > radius) continue;
        uint32_t slot;
        if (!qc_entity_slot(instance, references[i], &slot, NULL)) continue;
        qc_slot binding = instance->slots[slot];
        if (head != 0) {
            qa_actor_id prior;
            if (!qa_qc_reference_actor(instance, head, &prior, NULL)) head = 0;
        }
        if (!qa_qc_set_entity_int(instance, references[i], chain->offset,
                                  head, error)) {
            free(references); return false;
        }
        if (!slot_matches(instance, slot, binding.kind, binding.actor)) continue;
        head = references[i];
    }
    free(references);
    return qa_qc_return_int(instance, head, error);
}

static bool droptofloor(qa_qc_instance *instance, qa_error *error)
{
    if (instance->options.host.world == NULL)
        return qc_fail(error, QA_ERROR_UNSUPPORTED, 0, "droptofloor needs the shared world");
    const qa_qc_definition *self = qa_qc_program_find_global(instance->program, "self");
    if (self == NULL || self->type != QA_QC_ENTITY)
        return qc_fail(error, QA_ERROR_FORMAT, 0,
                       "droptofloor needs an entity self global");
    int32_t reference;
    if (!qa_qc_global_int(instance, self->offset, &reference, error)) return false;
    uint32_t slot;
    if (!qc_entity_slot(instance, reference, &slot, error) || slot == 0
        || instance->slots[slot].kind == QA_QC_SLOT_FREE || !may_move(instance, slot))
        return qc_fail(error, QA_ERROR_ARGUMENT, 0, "droptofloor lacks actor movement authority");
    qc_slot binding = instance->slots[slot];
    qa_body_state body;
    if (!qa_world_body_read(instance->options.host.world,
            binding.actor, &body, error)) return false;
    qa_trace_query query = {.start = body.origin,
        .end = qa_vec_add(body.origin, qa_v3(0,0,-256)),
        .shape = {QA_SHAPE_BOX, body.bounds},
        .policy = qa_collision_default_policy(QA_COLLISION_Q1),
        .pass_actor = binding.actor};
    qa_trace_result trace;
    if (!qa_world_trace(instance->options.host.world, &query, &trace, error)) return false;
    if (trace.fraction == 1.0f || trace.all_solid)
        return qa_qc_return_float(instance, 0, error);
    body.origin = trace.end;
    body.ground = trace.hit == QA_TRACE_HIT_ACTOR ? trace.actor : (qa_actor_id){0};
    if (!set_field_vector(instance, slot, "origin", trace.end, error)) return false;
    if (!slot_matches(instance, slot, binding.kind, binding.actor))
        return qc_fail(error, QA_ERROR_NOT_FOUND, slot,
                       "droptofloor actor changed during source store");
    const qa_qc_definition *ground = qa_qc_program_find_field(instance->program,
                                                               "groundentity");
    if (ground != NULL) {
        if (ground->type != QA_QC_ENTITY)
            return qc_fail(error, QA_ERROR_FORMAT, ground->offset,
                           "QuakeC groundentity field has the wrong type");
        int32_t ground_reference = 0;
        if (body.ground.registry != 0
            && !qa_qc_actor_reference(instance, body.ground, true,
                                      &ground_reference, error)) return false;
        if (!qa_qc_set_entity_int(instance, reference, ground->offset,
                                  ground_reference, error)) return false;
    }
    if (!slot_matches(instance, slot, binding.kind, binding.actor))
        return qc_fail(error, QA_ERROR_NOT_FOUND, slot,
                       "droptofloor actor changed during source store");
    if (!qa_world_body_write(instance->options.host.world,
            binding.actor, &body, error)
        || !qa_world_link(instance->options.host.world,
            binding.actor, NULL, error)) return false;
    if (!slot_matches(instance, slot, binding.kind, binding.actor))
        return qc_fail(error, QA_ERROR_NOT_FOUND, slot,
                       "droptofloor actor changed during world update");
    const qa_qc_definition *flags = qa_qc_program_find_field(instance->program, "flags");
    if (flags != NULL) {
        if (flags->type != QA_QC_FLOAT)
            return qc_fail(error, QA_ERROR_FORMAT, flags->offset,
                           "QuakeC flags field has the wrong type");
        float value;
        if (!qa_qc_entity_float(instance, reference, flags->offset, &value, error)
            || !qa_qc_set_entity_float(instance, reference, flags->offset,
                    (float)(int32_t)((uint32_t)source_float_int(value) | 512u),
                    error)) return false;
        if (!slot_matches(instance, slot, binding.kind, binding.actor))
            return qc_fail(error, QA_ERROR_NOT_FOUND, slot,
                           "droptofloor actor changed during flags store");
    }
    return qa_qc_return_float(instance, 1, error);
}

bool qc_host_builtin(qa_qc_instance *instance,
                     const qa_qc_builtin_requirement *requirement,
                     qa_error *error)
{
    const qa_qc_builtin_binding *custom = host_binding(instance,
        requirement->builtin, requirement->name);
    if (custom != NULL) {
        qa_error failure = {0};
        ++instance->callback_depth;
        bool ok = custom->call(custom->context, instance,
                               requirement->builtin, requirement->name,
                               &failure);
        --instance->callback_depth;
        if (ok) return true;
        if (failure.code == QA_OK)
            qa_error_set(&failure, QA_ERROR_ARGUMENT, 0,
                         "QuakeC host builtin callback failed");
        if (error != NULL) *error = failure;
        return false;
    }
    switch (requirement->builtin) {
    case QA_QC_BUILTIN_SETORIGIN: return setorigin(instance, error);
    case QA_QC_BUILTIN_SETSIZE: return setsize(instance, error);
    case QA_QC_BUILTIN_SPAWN: {
        int32_t reference;
        return qc_host_spawn(instance, &reference, error)
            && qa_qc_return_int(instance, reference, error);
    }
    case QA_QC_BUILTIN_REMOVE: {
        int32_t reference;
        return qa_qc_arg_int(instance, 0, &reference, error)
            && qc_host_remove(instance, reference, error);
    }
    case QA_QC_BUILTIN_TRACELINE: return traceline(instance, error);
    case QA_QC_BUILTIN_FINDRADIUS: return findradius(instance, error);
    case QA_QC_BUILTIN_DROPTOFLOOR: return droptofloor(instance, error);
    case QA_QC_BUILTIN_POINTCONTENTS: return pointcontents(instance, error);
    default:
        qa_error_set(error, QA_ERROR_UNSUPPORTED, 0,
                     "unbound QuakeC host builtin %s", requirement->name);
        return false;
    }
}
