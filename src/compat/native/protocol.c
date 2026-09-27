#include "protocol.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <errno.h>
#include <unistd.h>
#endif

#define NATIVE_WIRE_HEADER 40u
#define NATIVE_WIRE_MAX_TYPES 1024u

static bool wire_grow(native_wire_buffer *buffer, size_t added, qa_error *error) {
    size_t required;
    if (!native_size_add(buffer->size, added, &required))
        return native_fail(error, QA_ERROR_MEMORY, 0, "native runner message size overflows");
    if (required <= buffer->capacity)
        return true;
    size_t capacity = buffer->capacity ? buffer->capacity : 256u;
    while (capacity < required) {
        size_t next = capacity <= SIZE_MAX / 2u ? capacity * 2u : required;
        if (next < capacity)
            return native_fail(error, QA_ERROR_MEMORY, 0,
                               "native runner message capacity overflows");
        capacity = next;
    }
    uint8_t *grown = realloc(buffer->data, capacity);
    if (!grown)
        return native_fail(error, QA_ERROR_MEMORY, 0, "allocating native runner message");
    buffer->data = grown;
    buffer->capacity = capacity;
    return true;
}

void native_wire_buffer_free(native_wire_buffer *buffer) {
    if (!buffer)
        return;
    free(buffer->data);
    *buffer = (native_wire_buffer){0};
}

bool native_wire_put_raw(native_wire_buffer *buffer, const void *bytes, size_t size,
                         qa_error *error) {
    if ((!bytes && size) || !wire_grow(buffer, size, error))
        return false;
    if (size)
        memcpy(buffer->data + buffer->size, bytes, size);
    buffer->size += size;
    return true;
}

bool native_wire_put_u8(native_wire_buffer *buffer, uint8_t value, qa_error *error) {
    return native_wire_put_raw(buffer, &value, 1, error);
}

bool native_wire_put_u16(native_wire_buffer *buffer, uint16_t value, qa_error *error) {
    uint8_t bytes[2];
    qa_store_u16le(bytes, value);
    return native_wire_put_raw(buffer, bytes, sizeof(bytes), error);
}

bool native_wire_put_u32(native_wire_buffer *buffer, uint32_t value, qa_error *error) {
    uint8_t bytes[4];
    qa_store_u32le(bytes, value);
    return native_wire_put_raw(buffer, bytes, sizeof(bytes), error);
}

bool native_wire_put_u64(native_wire_buffer *buffer, uint64_t value, qa_error *error) {
    uint8_t bytes[8];
    qa_store_u64le(bytes, value);
    return native_wire_put_raw(buffer, bytes, sizeof(bytes), error);
}

bool native_wire_put_bytes(native_wire_buffer *buffer, qa_bytes bytes, qa_error *error) {
    return native_wire_put_u64(buffer, bytes.size, error) &&
           native_wire_put_raw(buffer, bytes.data, bytes.size, error);
}

bool native_wire_put_string(native_wire_buffer *buffer, const char *text, qa_error *error) {
    if (!text)
        text = "";
    return native_wire_put_bytes(buffer, (qa_bytes){(const uint8_t *)text, strlen(text)}, error);
}

static bool wire_put_raw_value(native_wire_buffer *buffer, const qa_native_value *value,
                               qa_error *error) {
    switch (value->type) {
    case QA_NATIVE_VOID:
        return true;
    case QA_NATIVE_I8:
        return native_wire_put_raw(buffer, &value->as.i8, sizeof(value->as.i8), error);
    case QA_NATIVE_U8:
        return native_wire_put_raw(buffer, &value->as.u8, sizeof(value->as.u8), error);
    case QA_NATIVE_I16:
        return native_wire_put_u16(buffer, (uint16_t)value->as.i16, error);
    case QA_NATIVE_U16:
        return native_wire_put_u16(buffer, value->as.u16, error);
    case QA_NATIVE_I32:
        return native_wire_put_u32(buffer, (uint32_t)value->as.i32, error);
    case QA_NATIVE_U32:
        return native_wire_put_u32(buffer, value->as.u32, error);
    case QA_NATIVE_I64:
        return native_wire_put_u64(buffer, (uint64_t)value->as.i64, error);
    case QA_NATIVE_U64:
        return native_wire_put_u64(buffer, value->as.u64, error);
    case QA_NATIVE_F32: {
        uint32_t bits;
        memcpy(&bits, &value->as.f32, sizeof(bits));
        return native_wire_put_u32(buffer, bits, error);
    }
    case QA_NATIVE_F64: {
        uint64_t bits;
        memcpy(&bits, &value->as.f64, sizeof(bits));
        return native_wire_put_u64(buffer, bits, error);
    }
    case QA_NATIVE_ADDRESS:
        return native_wire_put_u64(buffer, value->as.address, error);
    case QA_NATIVE_BYTES:
        return native_wire_put_bytes(buffer, (qa_bytes){value->as.bytes.data, value->as.bytes.size},
                                     error);
    }
    return native_fail(error, QA_ERROR_ARGUMENT, value->type, "unknown native runner value type");
}

bool native_wire_put_value(native_wire_buffer *buffer, const qa_native_value *value,
                           qa_error *error) {
    if (!value || value->type > QA_NATIVE_BYTES ||
        (value->type == QA_NATIVE_BYTES && !value->as.bytes.data && value->as.bytes.size))
        return native_fail(error, QA_ERROR_ARGUMENT, 0, "valid native runner value is required");
    return native_wire_put_u32(buffer, (uint32_t)value->type, error) &&
           wire_put_raw_value(buffer, value, error);
}

static bool wire_put_type(native_wire_buffer *buffer, const qa_native_type *type, unsigned depth,
                          size_t *count, qa_error *error) {
    if (!type || !type->count || type->kind > QA_NATIVE_BYTES || depth > 16u ||
        ++*count > NATIVE_WIRE_MAX_TYPES)
        return native_fail(error, QA_ERROR_ARGUMENT, depth, "invalid native runner ABI type tree");
    if ((type->kind == QA_NATIVE_BYTES) != (type->field_count != 0) ||
        (type->field_count && !type->fields))
        return native_fail(error, QA_ERROR_ARGUMENT, depth,
                           "native runner aggregate fields are invalid");
    if (!native_wire_put_u32(buffer, (uint32_t)type->kind, error) ||
        !native_wire_put_u64(buffer, type->count, error) ||
        !native_wire_put_u64(buffer, type->field_count, error))
        return false;
    for (size_t index = 0; index < type->field_count; ++index)
        if (!wire_put_type(buffer, &type->fields[index], depth + 1u, count, error))
            return false;
    return true;
}

bool native_wire_put_signature(native_wire_buffer *buffer, const qa_native_signature *signature,
                               qa_error *error) {
    if (!signature || signature->parameter_count > NATIVE_MAX_ARGUMENTS ||
        (signature->parameter_count && !signature->parameters))
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "valid native runner signature is required");
    if (!native_wire_put_u32(buffer, (uint32_t)signature->abi, error) ||
        !native_wire_put_u8(buffer, signature->variadic ? 1u : 0u, error) ||
        !native_wire_put_u64(buffer, signature->parameter_count, error))
        return false;
    size_t count = 0;
    for (size_t index = 0; index < signature->parameter_count; ++index)
        if (!wire_put_type(buffer, &signature->parameters[index], 0, &count, error))
            return false;
    return wire_put_type(buffer, &signature->result, 0, &count, error);
}

bool native_wire_put_error(native_wire_buffer *buffer, const qa_error *error,
                           qa_error *write_error) {
    qa_status status = error ? error->code : QA_OK;
    if (error && status == QA_OK)
        status = QA_ERROR_ARGUMENT;
    const char *message = !error              ? ""
                          : error->message[0] ? error->message
                                              : "native runner operation failed";
    size_t offset = error ? error->offset : 0;
    return native_wire_put_u32(buffer, (uint32_t)status, write_error) &&
           native_wire_put_u64(buffer, offset, write_error) &&
           native_wire_put_string(buffer, message, write_error);
}

bool native_wire_put_state(native_wire_buffer *buffer, const qa_native_processor_state *state,
                           qa_error *error) {
    if (!state)
        return native_fail(error, QA_ERROR_ARGUMENT, 0, "native processor state is required");
    for (size_t index = 0; index < QA_NATIVE_REGISTER_COUNT; ++index)
        if (!native_wire_put_u64(buffer, state->registers[index], error))
            return false;
    return native_wire_put_raw(buffer, state->simd, sizeof(state->simd), error) &&
           native_wire_put_u64(buffer, state->flags, error) &&
           native_wire_put_u64(buffer, state->instruction, error);
}

static bool wire_take(native_wire_reader *reader, size_t size, const uint8_t **out,
                      qa_error *error) {
    if (!reader || reader->offset > reader->bytes.size ||
        size > reader->bytes.size - reader->offset)
        return native_fail(error, QA_ERROR_FORMAT, reader ? reader->offset : 0,
                           "native runner message is truncated");
    *out = reader->bytes.data + reader->offset;
    reader->offset += size;
    return true;
}

bool native_wire_get_raw(native_wire_reader *reader, size_t size, const uint8_t **out,
                         qa_error *error) {
    return out && wire_take(reader, size, out, error);
}

bool native_wire_get_u8(native_wire_reader *reader, uint8_t *out, qa_error *error) {
    const uint8_t *bytes;
    if (!out || !wire_take(reader, 1, &bytes, error))
        return false;
    *out = bytes[0];
    return true;
}

bool native_wire_get_u16(native_wire_reader *reader, uint16_t *out, qa_error *error) {
    const uint8_t *bytes;
    if (!out || !wire_take(reader, 2, &bytes, error))
        return false;
    *out = qa_load_u16le(bytes);
    return true;
}

bool native_wire_get_u32(native_wire_reader *reader, uint32_t *out, qa_error *error) {
    const uint8_t *bytes;
    if (!out || !wire_take(reader, 4, &bytes, error))
        return false;
    *out = qa_load_u32le(bytes);
    return true;
}

bool native_wire_get_u64(native_wire_reader *reader, uint64_t *out, qa_error *error) {
    const uint8_t *bytes;
    if (!out || !wire_take(reader, 8, &bytes, error))
        return false;
    *out = qa_load_u64le(bytes);
    return true;
}

bool native_wire_get_bytes(native_wire_reader *reader, qa_bytes *out, qa_error *error) {
    uint64_t size;
    if (!out || !native_wire_get_u64(reader, &size, error))
        return false;
#if SIZE_MAX < UINT64_MAX
    if (size > (uint64_t)SIZE_MAX)
        return native_fail(error, QA_ERROR_FORMAT, reader->offset,
                           "native runner byte span exceeds the process");
#endif
    const uint8_t *bytes;
    if (!wire_take(reader, (size_t)size, &bytes, error))
        return false;
    *out = (qa_bytes){bytes, (size_t)size};
    return true;
}

bool native_wire_get_string(native_wire_reader *reader, qa_buffer *out, qa_error *error) {
    qa_bytes bytes;
    if (!out || !native_wire_get_bytes(reader, &bytes, error))
        return false;
    if (bytes.size == SIZE_MAX)
        return native_fail(error, QA_ERROR_MEMORY, reader->offset,
                           "native runner string length overflows");
    uint8_t *copy = malloc(bytes.size + 1u);
    if (!copy)
        return native_fail(error, QA_ERROR_MEMORY, reader->offset,
                           "allocating native runner string");
    if (bytes.size)
        memcpy(copy, bytes.data, bytes.size);
    copy[bytes.size] = 0;
    *out = (qa_buffer){copy, bytes.size};
    return true;
}

bool native_wire_get_value(native_wire_reader *reader, qa_native_value *out, qa_buffer *storage,
                           qa_error *error) {
    uint32_t encoded;
    if (!out || !storage || !native_wire_get_u32(reader, &encoded, error) ||
        encoded > QA_NATIVE_BYTES)
        return native_fail(error, QA_ERROR_FORMAT, reader ? reader->offset : 0,
                           "native runner value type is invalid");
    qa_native_value value = {.type = (qa_native_value_type)encoded};
    const uint8_t *bytes;
    switch (value.type) {
    case QA_NATIVE_VOID:
        break;
    case QA_NATIVE_I8:
        if (!wire_take(reader, 1, &bytes, error))
            return false;
        value.as.i8 = (int8_t)bytes[0];
        break;
    case QA_NATIVE_U8:
        if (!wire_take(reader, 1, &bytes, error))
            return false;
        value.as.u8 = bytes[0];
        break;
    case QA_NATIVE_I16:
        if (!wire_take(reader, 2, &bytes, error))
            return false;
        value.as.i16 = (int16_t)qa_load_u16le(bytes);
        break;
    case QA_NATIVE_U16:
        if (!wire_take(reader, 2, &bytes, error))
            return false;
        value.as.u16 = qa_load_u16le(bytes);
        break;
    case QA_NATIVE_I32:
        if (!wire_take(reader, 4, &bytes, error))
            return false;
        value.as.i32 = (int32_t)qa_load_u32le(bytes);
        break;
    case QA_NATIVE_U32:
        if (!wire_take(reader, 4, &bytes, error))
            return false;
        value.as.u32 = qa_load_u32le(bytes);
        break;
    case QA_NATIVE_F32: {
        if (!wire_take(reader, 4, &bytes, error))
            return false;
        uint32_t bits = qa_load_u32le(bytes);
        memcpy(&value.as.f32, &bits, sizeof(bits));
        break;
    }
    case QA_NATIVE_I64:
        if (!wire_take(reader, 8, &bytes, error))
            return false;
        value.as.i64 = (int64_t)qa_load_u64le(bytes);
        break;
    case QA_NATIVE_U64:
        if (!wire_take(reader, 8, &bytes, error))
            return false;
        value.as.u64 = qa_load_u64le(bytes);
        break;
    case QA_NATIVE_F64: {
        if (!wire_take(reader, 8, &bytes, error))
            return false;
        uint64_t bits = qa_load_u64le(bytes);
        memcpy(&value.as.f64, &bits, sizeof(bits));
        break;
    }
    case QA_NATIVE_ADDRESS:
        if (!wire_take(reader, 8, &bytes, error))
            return false;
        value.as.address = qa_load_u64le(bytes);
        break;
    case QA_NATIVE_BYTES: {
        qa_bytes aggregate;
        if (!native_wire_get_bytes(reader, &aggregate, error) ||
            !native_copy_bytes(aggregate, storage, error))
            return false;
        value.as.bytes = (qa_native_memory){storage->data, storage->size};
        break;
    }
    }
    *out = value;
    return true;
}

static void wire_type_free(qa_native_type *type) {
    if (!type || !type->fields)
        return;
    qa_native_type *fields = (qa_native_type *)type->fields;
    for (size_t index = 0; index < type->field_count; ++index)
        wire_type_free(&fields[index]);
    free(fields);
    type->fields = NULL;
    type->field_count = 0;
}

static bool wire_get_type(native_wire_reader *reader, qa_native_type *out, unsigned depth,
                          size_t *count, qa_error *error) {
    uint32_t kind;
    uint64_t repetition, field_count;
    if (depth > 16u || ++*count > NATIVE_WIRE_MAX_TYPES ||
        !native_wire_get_u32(reader, &kind, error) ||
        !native_wire_get_u64(reader, &repetition, error) ||
        !native_wire_get_u64(reader, &field_count, error) || kind > QA_NATIVE_BYTES ||
        !repetition ||
#if SIZE_MAX < UINT64_MAX
        repetition > (uint64_t)SIZE_MAX || field_count > (uint64_t)SIZE_MAX ||
#endif
        field_count > NATIVE_WIRE_MAX_TYPES || ((kind == QA_NATIVE_BYTES) != (field_count != 0)))
        return native_fail(error, QA_ERROR_FORMAT, reader->offset,
                           "native runner ABI type tree is invalid");
    qa_native_type type = {.kind = (qa_native_value_type)kind, .count = (size_t)repetition};
    if (field_count) {
        qa_native_type *fields = calloc((size_t)field_count, sizeof(*fields));
        if (!fields)
            return native_fail(error, QA_ERROR_MEMORY, reader->offset,
                               "allocating native runner ABI fields");
        type.fields = fields;
        type.field_count = (size_t)field_count;
        for (size_t index = 0; index < type.field_count; ++index) {
            if (!wire_get_type(reader, &fields[index], depth + 1u, count, error)) {
                wire_type_free(&type);
                return false;
            }
        }
    }
    *out = type;
    return true;
}

bool native_wire_get_signature(native_wire_reader *reader, qa_native_signature *out,
                               qa_error *error) {
    uint32_t abi;
    uint8_t variadic;
    uint64_t parameter_count;
    if (!out || !native_wire_get_u32(reader, &abi, error) ||
        !native_wire_get_u8(reader, &variadic, error) || variadic > 1u ||
        !native_wire_get_u64(reader, &parameter_count, error) || abi > QA_NATIVE_ABI_AAPCS64 ||
        parameter_count > NATIVE_MAX_ARGUMENTS)
        return native_fail(error, QA_ERROR_FORMAT, reader ? reader->offset : 0,
                           "native runner signature header is invalid");
    qa_native_signature signature = {.abi = (qa_native_abi)abi,
                                     .variadic = variadic != 0,
                                     .parameter_count = (size_t)parameter_count};
    qa_native_type *parameters = NULL;
    if (signature.parameter_count) {
        parameters = calloc(signature.parameter_count, sizeof(*parameters));
        if (!parameters)
            return native_fail(error, QA_ERROR_MEMORY, reader->offset,
                               "allocating native runner signature");
        signature.parameters = parameters;
    }
    size_t count = 0;
    for (size_t index = 0; index < signature.parameter_count; ++index) {
        if (!wire_get_type(reader, &parameters[index], 0, &count, error)) {
            native_wire_signature_free(&signature);
            return false;
        }
    }
    if (!wire_get_type(reader, &signature.result, 0, &count, error)) {
        native_wire_signature_free(&signature);
        return false;
    }
    *out = signature;
    return true;
}

void native_wire_signature_free(qa_native_signature *signature) {
    if (!signature)
        return;
    qa_native_type *parameters = (qa_native_type *)signature->parameters;
    for (size_t index = 0; index < signature->parameter_count; ++index)
        wire_type_free(&parameters[index]);
    free(parameters);
    wire_type_free(&signature->result);
    *signature = (qa_native_signature){0};
}

bool native_wire_get_error(native_wire_reader *reader, bool *ok, qa_error *error) {
    uint32_t status;
    uint64_t offset;
    qa_buffer message = {0};
    if (!ok || !native_wire_get_u32(reader, &status, error) ||
        !native_wire_get_u64(reader, &offset, error) ||
        !native_wire_get_string(reader, &message, error))
        return false;
    if (status > QA_ERROR_NOT_FOUND
#if SIZE_MAX < UINT64_MAX
        || offset > (uint64_t)SIZE_MAX
#endif
    ) {
        qa_buffer_free(&message);
        return native_fail(error, QA_ERROR_FORMAT, reader->offset,
                           "native runner error record is invalid");
    }
    *ok = status == QA_OK;
    if (!*ok)
        qa_error_set(error, (qa_status)status, (size_t)offset, "%s",
                     message.data ? (const char *)message.data : "");
    qa_buffer_free(&message);
    return true;
}

bool native_wire_get_state(native_wire_reader *reader, qa_native_processor_state *out,
                           qa_error *error) {
    if (!out)
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "native processor state output is required");
    qa_native_processor_state state = {0};
    for (size_t index = 0; index < QA_NATIVE_REGISTER_COUNT; ++index)
        if (!native_wire_get_u64(reader, &state.registers[index], error))
            return false;
    const uint8_t *simd;
    if (!wire_take(reader, sizeof(state.simd), &simd, error) ||
        !native_wire_get_u64(reader, &state.flags, error) ||
        !native_wire_get_u64(reader, &state.instruction, error))
        return false;
    memcpy(state.simd, simd, sizeof(state.simd));
    *out = state;
    return true;
}

bool native_wire_end(native_wire_reader *reader, qa_error *error) {
    if (!reader || reader->offset != reader->bytes.size)
        return native_fail(error, QA_ERROR_FORMAT, reader ? reader->offset : 0,
                           "native runner message has trailing bytes");
    return true;
}

static bool wire_os_error(qa_error *error, const char *operation) {
#if defined(_WIN32)
    qa_error_set(error, QA_ERROR_IO, (size_t)GetLastError(), "%s failed", operation);
#else
    qa_error_set(error, QA_ERROR_IO, (size_t)errno, "%s: %s", operation, strerror(errno));
#endif
    return false;
}

static bool wire_write_exact(intptr_t output, const void *source, size_t size, qa_error *error) {
    const uint8_t *bytes = source;
    size_t offset = 0;
    while (offset < size) {
#if defined(_WIN32)
        DWORD amount = size - offset > UINT32_MAX ? UINT32_MAX : (DWORD)(size - offset);
        DWORD written = 0;
        if (!WriteFile((HANDLE)output, bytes + offset, amount, &written, NULL) || !written)
            return wire_os_error(error, "writing native runner pipe");
#else
        ssize_t written = write((int)output, bytes + offset, size - offset);
        if (written < 0 && errno == EINTR)
            continue;
        if (written <= 0)
            return wire_os_error(error, "writing native runner pipe");
#endif
        offset += (size_t)written;
    }
    return true;
}

static bool wire_read_exact(intptr_t input, void *destination, size_t size, qa_error *error) {
    uint8_t *bytes = destination;
    size_t offset = 0;
    while (offset < size) {
#if defined(_WIN32)
        DWORD amount = size - offset > UINT32_MAX ? UINT32_MAX : (DWORD)(size - offset);
        DWORD read_count = 0;
        if (!ReadFile((HANDLE)input, bytes + offset, amount, &read_count, NULL) || !read_count)
            return wire_os_error(error, "reading native runner pipe");
        size_t received = read_count;
#else
        ssize_t read_count = read((int)input, bytes + offset, size - offset);
        if (read_count < 0 && errno == EINTR)
            continue;
        if (read_count <= 0)
            return wire_os_error(error, "reading native runner pipe");
        size_t received = (size_t)read_count;
#endif
        offset += received;
    }
    return true;
}

void native_wire_poison(native_runner_connection *connection, const qa_error *error) {
    if (!connection || connection->poisoned)
        return;
    connection->poisoned = true;
    connection->failure = error ? *error : (qa_error){.code = QA_ERROR_IO};
    if (!connection->failure.message[0])
        snprintf(connection->failure.message, sizeof(connection->failure.message),
                 "native runner connection failed");
}

bool native_wire_send(native_runner_connection *connection, uint16_t opcode, uint64_t reply_to,
                      qa_bytes payload, uint64_t *sequence, qa_error *error) {
    if (!connection || connection->poisoned || (!payload.data && payload.size) ||
        payload.size > connection->maximum_frame) {
        if (connection && connection->poisoned && error)
            *error = connection->failure;
        else
            native_fail(error, QA_ERROR_ARGUMENT, payload.size, "native runner frame is invalid");
        return false;
    }
    uint64_t current = ++connection->sequence;
    if (!current)
        current = ++connection->sequence;
    uint8_t header[NATIVE_WIRE_HEADER] = {0};
    qa_store_u32le(header, NATIVE_WIRE_MAGIC);
    qa_store_u16le(header + 4, NATIVE_WIRE_VERSION);
    qa_store_u16le(header + 6, opcode);
    qa_store_u32le(header + 8, connection->depth);
    qa_store_u64le(header + 16, current);
    qa_store_u64le(header + 24, reply_to);
    qa_store_u64le(header + 32, payload.size);
    if (!wire_write_exact(connection->output, header, sizeof(header), error) ||
        !wire_write_exact(connection->output, payload.data, payload.size, error)) {
        native_wire_poison(connection, error);
        return false;
    }
    if (sequence)
        *sequence = current;
    return true;
}

bool native_wire_receive(native_runner_connection *connection, native_wire_frame *out,
                         qa_error *error) {
    if (!connection || !out || connection->poisoned) {
        if (connection && connection->poisoned && error)
            *error = connection->failure;
        else
            native_fail(error, QA_ERROR_ARGUMENT, 0, "live native runner connection is required");
        return false;
    }
    uint8_t header[NATIVE_WIRE_HEADER];
    if (!wire_read_exact(connection->input, header, sizeof(header), error)) {
        native_wire_poison(connection, error);
        return false;
    }
    uint32_t magic = qa_load_u32le(header);
    uint16_t version = qa_load_u16le(header + 4);
    uint16_t opcode = qa_load_u16le(header + 6);
    uint32_t depth = qa_load_u32le(header + 8);
    uint64_t sequence = qa_load_u64le(header + 16);
    uint64_t reply_to = qa_load_u64le(header + 24);
    uint64_t payload_size = qa_load_u64le(header + 32);
    if (magic != NATIVE_WIRE_MAGIC || version != NATIVE_WIRE_VERSION || !sequence ||
        payload_size > connection->maximum_frame
#if SIZE_MAX < UINT64_MAX
        || payload_size > (uint64_t)SIZE_MAX
#endif
    ) {
        native_fail(error, QA_ERROR_FORMAT, 0, "native runner frame header is invalid");
        native_wire_poison(connection, error);
        return false;
    }
    qa_buffer payload = {0};
    if (payload_size) {
        payload.data = malloc((size_t)payload_size);
        if (!payload.data) {
            native_fail(error, QA_ERROR_MEMORY, 0, "allocating native runner frame payload");
            native_wire_poison(connection, error);
            return false;
        }
        payload.size = (size_t)payload_size;
        if (!wire_read_exact(connection->input, payload.data, payload.size, error)) {
            qa_buffer_free(&payload);
            native_wire_poison(connection, error);
            return false;
        }
    }
    *out = (native_wire_frame){opcode, depth, sequence, reply_to, payload};
    return true;
}

void native_wire_frame_free(native_wire_frame *frame) {
    if (!frame)
        return;
    qa_buffer_free(&frame->payload);
    *frame = (native_wire_frame){0};
}

#undef NATIVE_WIRE_HEADER
#undef NATIVE_WIRE_MAX_TYPES
