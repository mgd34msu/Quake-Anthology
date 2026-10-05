#include "internal.h"

bool q3_vm_span(qa_qvm *vm, uint64_t address, size_t size, qa_bytes *out, qa_error *error)
{
    uint64_t bias = qa_qvm_memory_size(vm);
    if (address < bias || address - bias > bias || !bias)
        return q3_fail(error, QA_ERROR_ARGUMENT, 0, "Q3 source pointer exceeds QVM memory");
    /* Arguments are masked once, then biased so nonnull offset zero remains
     * distinct from NULL. Field displacements never wrap through the VM mask. */
    return qa_qvm_span(vm, 1, (int64_t)(address - bias) - 1, size, out, error);
}

bool q3_read(const q3_call *call, uint64_t address, void *out, size_t size, qa_error *error)
{
    if ((!out && size) || address > UINT64_MAX - size)
        return q3_fail(error, QA_ERROR_ARGUMENT, 0, "invalid Q3 source memory read");
    return call->memory.read(call->memory.context, address, out, size, error);
}

bool q3_write(const q3_call *call, uint64_t address, qa_bytes bytes, qa_error *error)
{
    if ((!bytes.data && bytes.size) || address > UINT64_MAX - bytes.size)
        return q3_fail(error, QA_ERROR_ARGUMENT, 0, "invalid Q3 source memory write");
    return call->memory.write(call->memory.context, address, bytes, error);
}

bool q3_string(const q3_call *call, uint64_t address, qa_buffer *out, qa_error *error)
{
    if (!address)
        return q3_fail(error, QA_ERROR_ARGUMENT, 0, "Q3 source string is null");
    return call->memory.read_string(call->memory.context, address,
                                     call->host->options.maximum_string_bytes, out, error);
}

bool q3_write_string(const q3_call *call, uint64_t address, const char *text,
                      int32_t capacity, qa_error *error)
{
    if (!address || capacity < 1)
        return q3_fail(error, QA_ERROR_ARGUMENT, 0,
                          !address ? "Q_strncpyz: NULL dest" : "Q_strncpyz: destsize < 1");
    if (call->vm) {
        qa_bytes admitted;
        if (!q3_vm_span(call->vm, address, (size_t)capacity, &admitted, error)) return false;
    }
    size_t count = strlen(text);
    if (count >= (size_t)capacity) count = (size_t)capacity - 1u;
    uint8_t *copy = qa_arena_alloc(&call->host->scratch, (size_t)capacity, 1, error);
    if (!copy) return false;
    if (count) memcpy(copy, text, count);
    memset(copy + count, 0, (size_t)capacity - count);
    return q3_write(call, address, (qa_bytes){copy, (size_t)capacity}, error);
}

bool q3_write_word(const q3_call *call, uint64_t address, uint32_t word, qa_error *error)
{
    uint8_t bytes[4];
    qa_store_u32le(bytes, word);
    return q3_write(call, address, (qa_bytes){bytes, sizeof(bytes)}, error);
}

int32_t q3_float_bits(float value)
{
    int32_t bits; memcpy(&bits, &value, sizeof(bits)); return bits;
}

bool q3_write_float(const q3_call *call, uint64_t address, float value, qa_error *error)
{
    return q3_write_word(call, address, (uint32_t)q3_float_bits(value), error);
}

int32_t q3_integer(const q3_call *call, size_t index)
{
    uint32_t bits = (uint32_t)call->arguments[index];
    int32_t value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

float q3_float(const q3_call *call, size_t index)
{
    uint32_t bits = (uint32_t)call->arguments[index];
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

bool q3_vector(const q3_call *call, uint64_t address, qa_vec3 *out, qa_error *error)
{
    uint8_t bytes[12];
    if (!q3_read(call, address, bytes, sizeof(bytes), error)) return false;
    *out = qa_v3(qa_load_f32le(bytes), qa_load_f32le(bytes + 4), qa_load_f32le(bytes + 8));
    return true;
}

bool q3_write_vector(const q3_call *call, uint64_t address, qa_vec3 value, qa_error *error)
{
    uint8_t bytes[12];
    qa_store_f32le(bytes, value.x);
    qa_store_f32le(bytes + 4, value.y);
    qa_store_f32le(bytes + 8, value.z);
    return q3_write(call, address, (qa_bytes){bytes, sizeof(bytes)}, error);
}

static bool record_write(void *context, size_t offset, qa_bytes bytes, qa_error *error)
{
    q3_record *record = context;
    return q3_write(record->call, record->address + offset, bytes, error);
}

static bool entity_span_current(const q3_call *call, const q3_entity_span *span)
{
    const q3_game_data *game = call->host->game;
    return span && game && span->vm == call->vm && game->entities == span->address &&
        game->entity_count == span->count && game->entity_stride == span->stride;
}

bool q3_entity_span_begin(q3_call *call, q3_entity_span *span, qa_error *error)
{
    *span = (q3_entity_span){0};
    q3_game_data *game = call->host->game;
    if (!call->vm || !game || !game->entities || !game->entity_count) return true;
    uint64_t size = (uint64_t)game->entity_count * game->entity_stride;
    if (size > SIZE_MAX)
        return q3_fail(error, QA_ERROR_ARGUMENT, 0, "Q3 entity table exceeds its VM span");
    qa_bytes bytes;
    if (!q3_vm_span(call->vm, game->entities, (size_t)size, &bytes, error)) return false;
    *span = (q3_entity_span){.previous = call->host->entity_span, .vm = call->vm,
        .address = game->entities, .count = game->entity_count, .stride = game->entity_stride,
        .bytes = bytes};
    call->host->entity_span = span;
    return true;
}

bool q3_entity_span_end(q3_call *call, q3_entity_span *span, bool okay, qa_error *error)
{
    if (!span->vm) return okay;
    bool ordered = call->host->entity_span == span;
    call->host->entity_span = span->previous;
    return ordered ? okay : q3_fail(error, QA_ERROR_ARGUMENT, 0, "Q3 entity spans unwound out of order");
}

bool q3_record_open(const q3_call *call, uint64_t address, size_t size,
                     q3_record *record, qa_error *error)
{
    *record = (q3_record){.call = call, .address = address};
    qa_bytes bytes;
    if (call->vm) {
        const q3_entity_span *span = call->host->entity_span;
        if (entity_span_current(call, span) && address >= span->address &&
            address - span->address <= span->bytes.size && size <= span->bytes.size - (size_t)(address - span->address))
            bytes = (qa_bytes){span->bytes.data + (size_t)(address - span->address), size};
        else if (!q3_vm_span(call->vm, address, size, &bytes, error))
            return false;
    } else {
        uint8_t *copy = size ? qa_arena_alloc(&call->host->scratch, size, 1, error) : NULL;
        if ((size && !copy) || !q3_read(call, address, copy, size, error)) return false;
        bytes = (qa_bytes){copy, size};
    }
    record->abi = (qa_q3_abi_record){.abi = call->host->options.abi, .bytes = bytes,
                                      .context = record, .write = record_write};
    return true;
}
