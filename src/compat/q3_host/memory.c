#include "internal.h"

bool q3_fail(qa_error *error, qa_status status, size_t offset, const char *text)
{
    qa_error_set(error, status, offset, "%s", text);
    return false;
}

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
    const float values[] = {value.x, value.y, value.z};
    for (size_t i = 0; i < 3; ++i) {
        uint32_t bits; memcpy(&bits, &values[i], sizeof(bits));
        if (!q3_write_word(call, address + i * 4, bits, error)) return false;
    }
    return true;
}

static bool record_write(void *context, size_t offset, qa_bytes bytes, qa_error *error)
{
    q3_record *record = context;
    return q3_write(record->call, record->address + offset, bytes, error);
}

bool q3_record_open(const q3_call *call, uint64_t address, size_t size,
                     q3_record *record, qa_error *error)
{
    *record = (q3_record){.call = call, .address = address};
    qa_bytes bytes;
    if (call->vm) {
        if (!q3_vm_span(call->vm, address, size, &bytes, error))
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
