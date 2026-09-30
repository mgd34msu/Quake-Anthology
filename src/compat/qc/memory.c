/* Quake pr_edict.c/pr_exec.c compatible private storage. */
#include "internal.h"

#include <stdint.h>

#define QC_STRING_MIN_CAPACITY 4096u
#define QC_QW_ENGINE_STRINGS 1023u

static bool memory_fail(qa_error *error, qa_status status, size_t offset,
                        const char *message)
{
    return qc_fail(error, status, offset, message);
}

static bool string_aliases(const qc_strings *strings, const char *text)
{
    if (strings == NULL || strings->bytes == NULL || text == NULL) return false;
    uintptr_t address = (uintptr_t)(const void *)text;
    uintptr_t base = (uintptr_t)(const void *)strings->bytes;
    return address >= base && address - base < strings->used;
}

static bool string_reserve(qc_strings *strings, uint32_t length,
                           uint32_t *out, qa_error *error)
{
    if (strings->used > strings->capacity || strings->used > (uint32_t)INT32_MAX ||
        (strings->bytes == NULL && strings->capacity != 0))
        return memory_fail(error, QA_ERROR_FORMAT, strings->used,
                           "corrupt QC string arena");
    if (length > (uint32_t)INT32_MAX - strings->used)
        return memory_fail(error, QA_ERROR_MEMORY, strings->used,
                           "QC string arena overflow");

    uint32_t offset = strings->used;
    uint32_t required = offset + length;
    if (required > strings->capacity) {
        uint32_t capacity = strings->capacity;
        if (capacity < QC_STRING_MIN_CAPACITY) capacity = QC_STRING_MIN_CAPACITY;
        while (capacity < required) {
            if (capacity > (uint32_t)INT32_MAX / 2u) {
                capacity = (uint32_t)INT32_MAX;
                break;
            }
            capacity *= 2u;
        }
        if (capacity < required)
            return memory_fail(error, QA_ERROR_MEMORY, strings->used,
                               "QC string arena overflow");
        uint8_t *grown = realloc(strings->bytes, capacity);
        if (grown == NULL)
            return memory_fail(error, QA_ERROR_MEMORY, strings->used,
                               "growing QC string arena");
        strings->bytes = grown;
        strings->capacity = capacity;
    }

    if (length != 0) memset(strings->bytes + offset, 0, length);
    strings->used = required;
    *out = offset;
    return true;
}

static bool engine_reserve(qc_strings *strings, qa_error *error)
{
    if (strings->engine_count < strings->engine_capacity) return true;
    uint32_t capacity;
    if (strings->engine_capacity == 0) capacity = 8u;
    else if (strings->engine_capacity > UINT32_MAX / 2u) capacity = UINT32_MAX;
    else capacity = strings->engine_capacity * 2u;
    if (capacity <= strings->engine_count ||
        (size_t)capacity > SIZE_MAX / sizeof(*strings->engines))
        return memory_fail(error, QA_ERROR_MEMORY, strings->engine_count,
                           "QC engine string index overflow");
    qc_engine_string *grown = realloc(strings->engines,
                                      (size_t)capacity * sizeof(*grown));
    if (grown == NULL)
        return memory_fail(error, QA_ERROR_MEMORY, strings->engine_count,
                           "growing QC engine string index");
    strings->engines = grown;
    strings->engine_capacity = capacity;
    return true;
}

static void string_encode(qc_strings *strings, uint32_t offset,
                          const char *text, size_t length)
{
    memmove(strings->bytes + offset, text, length);
    strings->bytes[offset + length] = 0;
}

bool qc_strings_create(qc_strings *strings, qa_bytes program, bool quakeworld,
                       qa_error *error)
{
    if (strings == NULL || (program.data == NULL && program.size != 0))
        return memory_fail(error, QA_ERROR_ARGUMENT, 0,
                           "invalid QC string storage request");
    if (program.size > (size_t)INT32_MAX)
        return memory_fail(error, QA_ERROR_MEMORY, program.size,
                           "QC program strings exceed the guest address space");

    uint32_t capacity = program.size > QC_STRING_MIN_CAPACITY
                      ? (uint32_t)program.size : QC_STRING_MIN_CAPACITY;
    uint8_t *bytes = calloc(capacity, 1u);
    if (bytes == NULL)
        return memory_fail(error, QA_ERROR_MEMORY, 0,
                           "allocating QC string arena");
    if (program.size != 0) memcpy(bytes, program.data, program.size);

    *strings = (qc_strings){
        .bytes = bytes,
        .used = (uint32_t)program.size,
        .capacity = capacity,
        .quakeworld = quakeworld
    };
    return true;
}

void qc_strings_destroy(qc_strings *strings)
{
    if (strings == NULL) return;
    for (uint32_t i = 0; i < strings->engine_count; ++i)
        free(strings->engines[i].name);
    free(strings->engines);
    free(strings->bytes);
    *strings = (qc_strings){0};
}

bool qc_strings_get(const qc_strings *strings, int32_t id, const char **out,
                    qa_error *error)
{
    if (strings == NULL || out == NULL ||
        strings->used > strings->capacity || strings->used > (uint32_t)INT32_MAX ||
        (strings->bytes == NULL && strings->capacity != 0) ||
        strings->engine_count > strings->engine_capacity ||
        (strings->engines == NULL && strings->engine_count != 0))
        return memory_fail(error, QA_ERROR_ARGUMENT, 0,
                           "invalid QC string lookup");

    uint32_t offset;
    size_t available;
    if (id < 0) {
        if (!strings->quakeworld)
            return memory_fail(error, QA_ERROR_ARGUMENT, 0,
                               "negative engine string in a NetQuake program");
        uint64_t wide_index = (uint64_t)(-(int64_t)id - 1);
        if (wide_index >= strings->engine_count)
            return memory_fail(error, QA_ERROR_ARGUMENT, 0,
                               "invalid QuakeWorld engine string");
        const qc_engine_string *entry = &strings->engines[(uint32_t)wide_index];
        if (entry->capacity == 0 || entry->offset > strings->used ||
            entry->capacity > strings->used - entry->offset)
            return memory_fail(error, QA_ERROR_FORMAT, entry->offset,
                               "corrupt QuakeWorld engine string");
        offset = entry->offset;
        available = entry->capacity;
    } else {
        offset = (uint32_t)id;
        available = offset < strings->used ? strings->used - offset : 0;
    }

    if (offset >= strings->used)
        return memory_fail(error, QA_ERROR_ARGUMENT, offset,
                           "QC string offset is outside the arena");
    if (memchr(strings->bytes + offset, 0, available) == NULL)
        return memory_fail(error, QA_ERROR_FORMAT, offset,
                           "unterminated QC string");
    *out = (const char *)strings->bytes + offset;
    return true;
}

bool qc_strings_allocate(qc_strings *strings, const char *text, int32_t *out,
                         qa_error *error)
{
    if (strings == NULL || text == NULL || out == NULL ||
        strings->used > strings->capacity || strings->used > (uint32_t)INT32_MAX ||
        (strings->bytes == NULL && strings->capacity != 0))
        return memory_fail(error, QA_ERROR_ARGUMENT, 0,
                           "invalid QC string allocation");
    size_t length = strlen(text);
    if (length >= (size_t)INT32_MAX)
        return memory_fail(error, QA_ERROR_MEMORY, length,
                           "QC string exceeds the guest address space");

    char *staged = NULL;
    uint32_t required = (uint32_t)length + 1u;
    if (required > strings->capacity - strings->used &&
        string_aliases(strings, text)) {
        staged = qc_strdup(text, error);
        if (staged == NULL) return false;
        text = staged;
    }

    uint32_t offset;
    if (!string_reserve(strings, required, &offset, error)) {
        free(staged);
        return false;
    }
    string_encode(strings, offset, text, length);
    free(staged);
    *out = (int32_t)offset;
    return true;
}

bool qc_strings_engine(qc_strings *strings, const char *name, const char *text,
                       size_t capacity, int32_t *out, qa_error *error)
{
    if (strings == NULL || name == NULL || name[0] == '\0'
        || text == NULL || out == NULL ||
        strings->used > strings->capacity || strings->used > (uint32_t)INT32_MAX ||
        (strings->bytes == NULL && strings->capacity != 0) ||
        strings->engine_count > strings->engine_capacity ||
        (strings->engines == NULL && strings->engine_count != 0))
        return memory_fail(error, QA_ERROR_ARGUMENT, 0,
                           "invalid QC engine string request");
    size_t length = strlen(text);
    for (uint32_t i = 0; i < strings->engine_count; ++i) {
        qc_engine_string *entry = &strings->engines[i];
        if (entry->name == NULL || entry->capacity == 0 ||
            entry->offset > strings->used ||
            entry->capacity > strings->used - entry->offset)
            return memory_fail(error, QA_ERROR_FORMAT, entry->offset,
                               "corrupt QC engine string index");
        if (strcmp(entry->name, name) != 0) continue;
        if (length >= entry->capacity)
            return memory_fail(error, QA_ERROR_ARGUMENT, length,
                               "QC engine string exceeds its buffer");
        string_encode(strings, entry->offset, text, length);
        *out = strings->quakeworld ? -(int32_t)i - 1
                                   : (int32_t)entry->offset;
        return true;
    }

    if (capacity == 0 || capacity > (size_t)INT32_MAX || length >= capacity)
        return memory_fail(error, QA_ERROR_ARGUMENT, length,
                           "invalid QC engine string capacity");
    if (strings->quakeworld && strings->engine_count >= QC_QW_ENGINE_STRINGS)
        return memory_fail(error, QA_ERROR_MEMORY, strings->engine_count,
                           "MAX_PRSTR");

    char *owned_name = qc_strdup(name, error);
    if (owned_name == NULL) return false;
    if (!engine_reserve(strings, error)) {
        free(owned_name);
        return false;
    }

    char *staged = NULL;
    if (capacity > strings->capacity - strings->used &&
        string_aliases(strings, text)) {
        staged = qc_strdup(text, error);
        if (staged == NULL) {
            free(owned_name);
            return false;
        }
        text = staged;
    }

    uint32_t offset;
    if (!string_reserve(strings, (uint32_t)capacity, &offset, error)) {
        free(staged);
        free(owned_name);
        return false;
    }
    string_encode(strings, offset, text, length);
    free(staged);

    uint32_t index = strings->engine_count++;
    strings->engines[index] = (qc_engine_string){
        .name = owned_name,
        .offset = offset,
        .capacity = (uint32_t)capacity
    };
    *out = strings->quakeworld ? -(int32_t)index - 1 : (int32_t)offset;
    return true;
}

bool qc_global_range(const qa_qc_instance *instance, uint32_t word,
                     uint32_t count, qa_error *error)
{
    if (instance == NULL || instance->program == NULL ||
        (instance->globals == NULL && instance->program->info.global_words != 0))
        return memory_fail(error, QA_ERROR_ARGUMENT, word,
                           "invalid QC global storage");
    uint32_t words = instance->program->info.global_words;
    if (word > words || count > words - word)
        return memory_fail(error, QA_ERROR_ARGUMENT, word,
                           "QC global word range is outside storage");
    return true;
}

bool qc_entity_slot(const qa_qc_instance *instance, int32_t reference,
                    uint32_t *slot, qa_error *error)
{
    if (instance == NULL || slot == NULL || instance->layout.stride_bytes == 0)
        return memory_fail(error, QA_ERROR_ARGUMENT, 0,
                           "invalid QC entity lookup");
    if (reference < 0 ||
        (uint32_t)reference % instance->layout.stride_bytes != 0)
        return memory_fail(error, QA_ERROR_ARGUMENT, 0,
                           "invalid QC entity reference");
    uint32_t value = (uint32_t)reference / instance->layout.stride_bytes;
    if (value >= instance->entity_count)
        return memory_fail(error, QA_ERROR_ARGUMENT, (size_t)reference,
                           "QC entity reference is outside active storage");
    *slot = value;
    return true;
}

uint8_t *qc_entity_words(qa_qc_instance *instance, uint32_t slot)
{
    return instance->entities + (size_t)slot * instance->layout.stride_bytes
         + instance->layout.variables_offset_bytes;
}

const uint8_t *qc_entity_words_const(const qa_qc_instance *instance,
                                     uint32_t slot)
{
    return instance->entities + (size_t)slot * instance->layout.stride_bytes
         + instance->layout.variables_offset_bytes;
}

bool qc_entity_range(const qa_qc_instance *instance, uint32_t slot,
                     uint32_t word, uint32_t count, qa_error *error)
{
    if (instance == NULL || instance->entities == NULL ||
        instance->layout.stride_bytes == 0 ||
        instance->layout.variables_offset_bytes > instance->layout.stride_bytes ||
        instance->layout.field_words >
            (instance->layout.stride_bytes -
             instance->layout.variables_offset_bytes) / 4u)
        return memory_fail(error, QA_ERROR_ARGUMENT, word,
                           "invalid QC entity storage");
    if (slot >= instance->entity_count)
        return memory_fail(error, QA_ERROR_ARGUMENT, slot,
                           "QC entity slot is outside active storage");
    uint32_t words = instance->layout.field_words;
    if (word > words || count > words - word)
        return memory_fail(error, QA_ERROR_ARGUMENT, word,
                           "QC entity field range is outside storage");
    return true;
}

bool qc_pointer(const qa_qc_instance *instance, int32_t pointer,
                uint32_t count, uint32_t *slot, uint32_t *word,
                qa_error *error)
{
    if (instance == NULL || slot == NULL || word == NULL || count == 0 ||
        instance->layout.stride_bytes == 0)
        return memory_fail(error, QA_ERROR_ARGUMENT, 0,
                           "invalid QC entity pointer lookup");
    if (pointer < 0 || ((uint32_t)pointer & 3u) != 0)
        return memory_fail(error, QA_ERROR_ARGUMENT, 0,
                           "invalid QC entity pointer");

    uint32_t address = (uint32_t)pointer;
    uint32_t entity = address / instance->layout.stride_bytes;
    uint32_t within = address % instance->layout.stride_bytes;
    if (entity >= instance->entity_count ||
        within < instance->layout.variables_offset_bytes ||
        ((within - instance->layout.variables_offset_bytes) & 3u) != 0)
        return memory_fail(error, QA_ERROR_ARGUMENT, address,
                           "QC pointer does not address entity variables");
    uint32_t field = (within - instance->layout.variables_offset_bytes) / 4u;
    if (field > instance->layout.field_words ||
        count > instance->layout.field_words - field)
        return memory_fail(error, QA_ERROR_ARGUMENT, address,
                           "QC pointer range exceeds entity variables");
    *slot = entity;
    *word = field;
    return true;
}

static uint32_t input_word(const qa_qc_instance *instance,
                           const uint32_t *values, uint32_t index)
{
    const uint8_t *address = (const uint8_t *)(const void *)values
                           + (size_t)index * sizeof(*values);
    uintptr_t value = (uintptr_t)(const void *)address;
    uintptr_t globals = (uintptr_t)(const void *)instance->globals;
    size_t global_bytes = (size_t)instance->program->info.global_words * 4u;
    if (value >= globals && value - globals <= global_bytes &&
        global_bytes - (size_t)(value - globals) >= 4u)
        return qa_load_u32le(address);

    uintptr_t entities = (uintptr_t)(const void *)instance->entities;
    size_t entity_bytes = (size_t)instance->entity_count
                        * instance->layout.stride_bytes;
    if (value >= entities && value - entities <= entity_bytes &&
        entity_bytes - (size_t)(value - entities) >= 4u)
        return qa_load_u32le(address);

    uint32_t result;
    memcpy(&result, address, sizeof(result));
    return result;
}

static bool notify_store(qa_qc_instance *instance,
                         const qa_qc_store_event *event, qa_error *error)
{
    qa_qc_store_observer_fn observer = instance->options.observers.stored;
    if (observer == NULL) return true;
    if (instance->callback_depth == UINT32_MAX)
        return memory_fail(error, QA_ERROR_ARGUMENT, event->word,
                           "QC callback depth exhausted");

    qa_error failure = {0};
    ++instance->callback_depth;
    ++instance->store_observer_depth;
    bool success = observer(instance->options.observers.context, instance,
                            event, &failure);
    --instance->store_observer_depth;
    --instance->callback_depth;
    if (success) return true;
    if (failure.code == QA_OK)
        qa_error_set(&failure, QA_ERROR_ARGUMENT, event->word,
                     "QC store observer failed");
    if (error != NULL) *error = failure;
    return false;
}

bool qc_write_global(qa_qc_instance *instance, uint32_t word,
                     const uint32_t *values, uint32_t count, qa_error *error)
{
    if (instance == NULL || values == NULL || count == 0 || count > 3)
        return memory_fail(error, QA_ERROR_ARGUMENT, word,
                           "invalid QC global store");
    if (instance->checkpointing)
        return memory_fail(error, QA_ERROR_ARGUMENT, word,
                           "QC guest storage is read-only during checkpoint callbacks");
    if (!qc_global_range(instance, word, count, error)) return false;

    qa_qc_store_event event = {
        .kind = QA_QC_STORE_GLOBAL,
        .word = word,
        .count = count
    };
    if (instance->frame_count != 0) {
        const qc_frame *frame = &instance->frames[instance->frame_count - 1u];
        event.function = frame->function;
        event.statement = frame->statement;
        event.depth = instance->frame_count;
    }
    for (uint32_t i = 0; i < count; ++i)
        event.before[i] = qc_load_word(instance->globals, word + i);
    /* Read each source only when its destination is reached. This deliberately
     * preserves Quake's sequential behavior for overlapping global stores. */
    for (uint32_t i = 0; i < count; ++i)
        qc_store_word(instance->globals, word + i,
                      input_word(instance, values, i));
    for (uint32_t i = 0; i < count; ++i)
        event.after[i] = qc_load_word(instance->globals, word + i);
    return notify_store(instance, &event, error);
}

bool qc_write_entity(qa_qc_instance *instance, uint32_t slot, uint32_t word,
                     const uint32_t *values, uint32_t count, qa_error *error)
{
    if (instance == NULL || values == NULL || count == 0 || count > 3)
        return memory_fail(error, QA_ERROR_ARGUMENT, word,
                           "invalid QC entity store");
    if (instance->checkpointing)
        return memory_fail(error, QA_ERROR_ARGUMENT, word,
                           "QC guest storage is read-only during checkpoint callbacks");
    if (!qc_prepare_entity_access(instance, slot, word, count,
                                  QA_QC_ENTITY_WRITE, error)) return false;

    uint64_t reference = (uint64_t)slot * instance->layout.stride_bytes;
    if (reference > (uint64_t)INT32_MAX)
        return memory_fail(error, QA_ERROR_ARGUMENT, slot,
                           "QC entity reference exceeds guest address space");
    uint8_t *fields = qc_entity_words(instance, slot);
    qa_qc_store_event event = {
        .kind = QA_QC_STORE_ENTITY,
        .entity_reference = (int32_t)reference,
        .word = word,
        .count = count
    };
    if (instance->frame_count != 0) {
        const qc_frame *frame = &instance->frames[instance->frame_count - 1u];
        event.function = frame->function;
        event.statement = frame->statement;
        event.depth = instance->frame_count;
    }
    for (uint32_t i = 0; i < count; ++i)
        event.before[i] = qc_load_word(fields, word + i);
    for (uint32_t i = 0; i < count; ++i)
        qc_store_word(fields, word + i, input_word(instance, values, i));
    for (uint32_t i = 0; i < count; ++i)
        event.after[i] = qc_load_word(fields, word + i);
    return notify_store(instance, &event, error);
}

bool qc_project_entity(qa_qc_instance *instance, uint32_t slot, uint32_t word,
                       const uint32_t *values, uint32_t count,
                       qa_error *error)
{
    if (instance == NULL || values == NULL || count == 0 || count > 3)
        return memory_fail(error, QA_ERROR_ARGUMENT, word,
                           "invalid QC entity projection");
    if (!qc_entity_range(instance, slot, word, count, error)) return false;
    uint8_t *fields = qc_entity_words(instance, slot);
    for (uint32_t i = 0; i < count; ++i)
        qc_store_word(fields, word + i, input_word(instance, values, i));
    return true;
}

static bool argument_word(const qa_qc_instance *instance, uint32_t argument,
                          uint32_t count, uint32_t *word, qa_error *error)
{
    if (argument >= 8u)
        return memory_fail(error, QA_ERROR_ARGUMENT, argument,
                           "invalid QC argument index");
    uint32_t offset = QC_ARGUMENT_WORD(argument);
    if (!qc_global_range(instance, offset, count, error)) return false;
    *word = offset;
    return true;
}

bool qa_qc_arg_int(const qa_qc_instance *instance, uint32_t argument,
                   int32_t *out, qa_error *error)
{
    if (out == NULL)
        return memory_fail(error, QA_ERROR_ARGUMENT, argument,
                           "missing QC argument output");
    uint32_t word;
    if (!argument_word(instance, argument, 1u, &word, error)) return false;
    *out = qc_load_int(instance->globals, word);
    return true;
}

bool qa_qc_arg_float(const qa_qc_instance *instance, uint32_t argument,
                     float *out, qa_error *error)
{
    if (out == NULL)
        return memory_fail(error, QA_ERROR_ARGUMENT, argument,
                           "missing QC argument output");
    uint32_t word;
    if (!argument_word(instance, argument, 1u, &word, error)) return false;
    *out = qc_load_float(instance->globals, word);
    return true;
}

bool qa_qc_arg_vector(const qa_qc_instance *instance, uint32_t argument,
                      qa_vec3 *out, qa_error *error)
{
    if (out == NULL)
        return memory_fail(error, QA_ERROR_ARGUMENT, argument,
                           "missing QC argument output");
    uint32_t word;
    if (!argument_word(instance, argument, 3u, &word, error)) return false;
    *out = qa_v3(qc_load_float(instance->globals, word),
                 qc_load_float(instance->globals, word + 1u),
                 qc_load_float(instance->globals, word + 2u));
    return true;
}

bool qa_qc_arg_string(const qa_qc_instance *instance, uint32_t argument,
                      const char **out, qa_error *error)
{
    if (out == NULL)
        return memory_fail(error, QA_ERROR_ARGUMENT, argument,
                           "missing QC argument output");
    uint32_t word;
    if (!argument_word(instance, argument, 1u, &word, error)) return false;
    return qc_strings_get(&instance->strings,
                          qc_load_int(instance->globals, word), out, error);
}

static uint32_t float_word(float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

bool qa_qc_return_int(qa_qc_instance *instance, int32_t value,
                      qa_error *error)
{
    uint32_t word = (uint32_t)value;
    return qc_write_global(instance, QC_RETURN_WORD, &word, 1u, error);
}

bool qa_qc_return_float(qa_qc_instance *instance, float value,
                        qa_error *error)
{
    uint32_t word = float_word(value);
    return qc_write_global(instance, QC_RETURN_WORD, &word, 1u, error);
}

bool qa_qc_return_vector(qa_qc_instance *instance, qa_vec3 value,
                         qa_error *error)
{
    uint32_t words[3] = {
        float_word(value.x), float_word(value.y), float_word(value.z)
    };
    return qc_write_global(instance, QC_RETURN_WORD, words, 3u, error);
}

bool qa_qc_global_int(const qa_qc_instance *instance, uint32_t word,
                      int32_t *out, qa_error *error)
{
    if (out == NULL)
        return memory_fail(error, QA_ERROR_ARGUMENT, word,
                           "missing QC global output");
    if (!qc_global_range(instance, word, 1u, error)) return false;
    *out = qc_load_int(instance->globals, word);
    return true;
}

bool qa_qc_global_float(const qa_qc_instance *instance, uint32_t word,
                        float *out, qa_error *error)
{
    if (out == NULL)
        return memory_fail(error, QA_ERROR_ARGUMENT, word,
                           "missing QC global output");
    if (!qc_global_range(instance, word, 1u, error)) return false;
    *out = qc_load_float(instance->globals, word);
    return true;
}

bool qa_qc_global_vector(const qa_qc_instance *instance, uint32_t word,
                         qa_vec3 *out, qa_error *error)
{
    if (out == NULL)
        return memory_fail(error, QA_ERROR_ARGUMENT, word,
                           "missing QC global output");
    if (!qc_global_range(instance, word, 3u, error)) return false;
    *out = qa_v3(qc_load_float(instance->globals, word),
                 qc_load_float(instance->globals, word + 1u),
                 qc_load_float(instance->globals, word + 2u));
    return true;
}

bool qa_qc_set_global_int(qa_qc_instance *instance, uint32_t word,
                          int32_t value, qa_error *error)
{
    uint32_t bits = (uint32_t)value;
    return qc_write_global(instance, word, &bits, 1u, error);
}

bool qa_qc_stage_globals(qa_qc_instance *instance, uint32_t word,
                          const uint32_t *values, uint32_t count, qa_error *error)
{
    if (!instance || instance->destroying || instance->checkpointing ||
        (count && !values))
        return memory_fail(error, QA_ERROR_ARGUMENT, word, "invalid QC host staging");
    if (!qc_global_range(instance, word, count, error)) return false;
    for (uint32_t i = 0; i < count; ++i)
        qc_store_word(instance->globals, word + i, values[i]);
    return true;
}

bool qa_qc_set_global_float(qa_qc_instance *instance, uint32_t word,
                            float value, qa_error *error)
{
    uint32_t bits = float_word(value);
    return qc_write_global(instance, word, &bits, 1u, error);
}

bool qa_qc_set_global_vector(qa_qc_instance *instance, uint32_t word,
                             qa_vec3 value, qa_error *error)
{
    uint32_t bits[3] = {
        float_word(value.x), float_word(value.y), float_word(value.z)
    };
    return qc_write_global(instance, word, bits, 3u, error);
}

bool qa_qc_entity_int(qa_qc_instance *instance, int32_t reference,
                      uint32_t word, int32_t *out, qa_error *error)
{
    if (out == NULL)
        return memory_fail(error, QA_ERROR_ARGUMENT, word,
                           "missing QC entity output");
    uint32_t slot;
    if (!qc_entity_slot(instance, reference, &slot, error)
        || !qc_prepare_entity_access(instance, slot, word, 1u,
                                     QA_QC_ENTITY_READ, error)) return false;
    *out = qc_load_int(qc_entity_words_const(instance, slot), word);
    return true;
}

bool qa_qc_entity_float(qa_qc_instance *instance, int32_t reference,
                        uint32_t word, float *out, qa_error *error)
{
    if (out == NULL)
        return memory_fail(error, QA_ERROR_ARGUMENT, word,
                           "missing QC entity output");
    uint32_t slot;
    if (!qc_entity_slot(instance, reference, &slot, error)
        || !qc_prepare_entity_access(instance, slot, word, 1u,
                                     QA_QC_ENTITY_READ, error)) return false;
    *out = qc_load_float(qc_entity_words_const(instance, slot), word);
    return true;
}

bool qa_qc_entity_vector(qa_qc_instance *instance, int32_t reference,
                         uint32_t word, qa_vec3 *out, qa_error *error)
{
    if (out == NULL)
        return memory_fail(error, QA_ERROR_ARGUMENT, word,
                           "missing QC entity output");
    uint32_t slot;
    if (!qc_entity_slot(instance, reference, &slot, error)
        || !qc_prepare_entity_access(instance, slot, word, 3u,
                                     QA_QC_ENTITY_READ, error)) return false;
    const uint8_t *fields = qc_entity_words_const(instance, slot);
    *out = qa_v3(qc_load_float(fields, word),
                 qc_load_float(fields, word + 1u),
                 qc_load_float(fields, word + 2u));
    return true;
}

bool qa_qc_set_entity_int(qa_qc_instance *instance, int32_t reference,
                          uint32_t word, int32_t value, qa_error *error)
{
    uint32_t slot;
    if (!qc_entity_slot(instance, reference, &slot, error)) return false;
    uint32_t bits = (uint32_t)value;
    return qc_write_entity(instance, slot, word, &bits, 1u, error);
}

bool qa_qc_set_entity_float(qa_qc_instance *instance, int32_t reference,
                            uint32_t word, float value, qa_error *error)
{
    uint32_t slot;
    if (!qc_entity_slot(instance, reference, &slot, error)) return false;
    uint32_t bits = float_word(value);
    return qc_write_entity(instance, slot, word, &bits, 1u, error);
}

bool qa_qc_set_entity_vector(qa_qc_instance *instance, int32_t reference,
                             uint32_t word, qa_vec3 value, qa_error *error)
{
    uint32_t slot;
    if (!qc_entity_slot(instance, reference, &slot, error)) return false;
    uint32_t bits[3] = {
        float_word(value.x), float_word(value.y), float_word(value.z)
    };
    return qc_write_entity(instance, slot, word, bits, 3u, error);
}

bool qa_qc_project_entity_int(qa_qc_instance *instance, int32_t reference,
                              uint32_t word, int32_t value, qa_error *error)
{
    uint32_t slot;
    if (!qc_entity_slot(instance, reference, &slot, error)) return false;
    uint32_t bits = (uint32_t)value;
    return qc_project_entity(instance, slot, word, &bits, 1u, error);
}

bool qa_qc_project_entity_float(qa_qc_instance *instance, int32_t reference,
                                uint32_t word, float value, qa_error *error)
{
    uint32_t slot;
    if (!qc_entity_slot(instance, reference, &slot, error)) return false;
    uint32_t bits = float_word(value);
    return qc_project_entity(instance, slot, word, &bits, 1u, error);
}

bool qa_qc_project_entity_vector(qa_qc_instance *instance, int32_t reference,
                                 uint32_t word, qa_vec3 value,
                                 qa_error *error)
{
    uint32_t slot;
    if (!qc_entity_slot(instance, reference, &slot, error)) return false;
    uint32_t bits[3] = {
        float_word(value.x), float_word(value.y), float_word(value.z)
    };
    return qc_project_entity(instance, slot, word, bits, 3u, error);
}

bool qa_qc_string(const qa_qc_instance *instance, int32_t id,
                  const char **out, qa_error *error)
{
    if (instance == NULL)
        return memory_fail(error, QA_ERROR_ARGUMENT, 0,
                           "invalid QC string lookup");
    return qc_strings_get(&instance->strings, id, out, error);
}

bool qa_qc_string_allocate(qa_qc_instance *instance, const char *text,
                           int32_t *out, qa_error *error)
{
    if (instance == NULL || instance->checkpointing)
        return memory_fail(error, QA_ERROR_ARGUMENT, 0,
                           "invalid QC string allocation");
    return qc_strings_allocate(&instance->strings, text, out, error);
}

bool qa_qc_engine_string(qa_qc_instance *instance, const char *name,
                         const char *text, size_t capacity, int32_t *out,
                         qa_error *error)
{
    bool checkpoint_projection = instance != NULL && instance->checkpointing
        && ((instance->capturing_checkpoint && instance->callback_depth != 0)
            || instance->entity_access_depth != 0);
    if (instance == NULL || (instance->checkpointing && !checkpoint_projection))
        return memory_fail(error, QA_ERROR_ARGUMENT, 0,
                           "invalid QC engine string request");
    return qc_strings_engine(&instance->strings, name, text, capacity,
                             out, error);
}
