#ifndef QA_NATIVE_PROTOCOL_H
#define QA_NATIVE_PROTOCOL_H

#include "internal.h"
#include "wire_constants.h"

typedef struct native_wire_buffer {
    uint8_t *data;
    size_t size;
    size_t capacity;
} native_wire_buffer;

typedef struct native_wire_reader {
    qa_bytes bytes;
    size_t offset;
} native_wire_reader;

typedef struct native_wire_frame {
    uint16_t opcode;
    uint32_t depth;
    uint64_t sequence;
    uint64_t reply_to;
    qa_buffer payload;
} native_wire_frame;

struct native_runner_connection {
    intptr_t input;
    intptr_t output;
    intptr_t process;
    uint64_t sequence;
    uint32_t depth;
    size_t maximum_frame;
    bool child;
    bool poisoned;
    qa_error failure;
    qa_native_instance *instance;
    char *temporary_directory;
    qa_native_runner_validate_fn validate;
    void *validation_context;
};

void native_wire_buffer_free(native_wire_buffer *buffer);
bool native_wire_put_raw(native_wire_buffer *buffer, const void *bytes, size_t size,
                         qa_error *error);
bool native_wire_put_u8(native_wire_buffer *buffer, uint8_t value, qa_error *error);
bool native_wire_put_u16(native_wire_buffer *buffer, uint16_t value, qa_error *error);
bool native_wire_put_u32(native_wire_buffer *buffer, uint32_t value, qa_error *error);
bool native_wire_put_u64(native_wire_buffer *buffer, uint64_t value, qa_error *error);
bool native_wire_put_bytes(native_wire_buffer *buffer, qa_bytes bytes, qa_error *error);
bool native_wire_put_string(native_wire_buffer *buffer, const char *text, qa_error *error);
bool native_wire_put_value(native_wire_buffer *buffer, const qa_native_value *value,
                           qa_error *error);
bool native_wire_put_signature(native_wire_buffer *buffer, const qa_native_signature *signature,
                               qa_error *error);
bool native_wire_put_error(native_wire_buffer *buffer, const qa_error *error,
                           qa_error *write_error);
bool native_wire_put_state(native_wire_buffer *buffer, const qa_native_processor_state *state,
                           qa_error *error);

bool native_wire_get_u8(native_wire_reader *reader, uint8_t *out, qa_error *error);
bool native_wire_get_u16(native_wire_reader *reader, uint16_t *out, qa_error *error);
bool native_wire_get_u32(native_wire_reader *reader, uint32_t *out, qa_error *error);
bool native_wire_get_u64(native_wire_reader *reader, uint64_t *out, qa_error *error);
bool native_wire_get_raw(native_wire_reader *reader, size_t size, const uint8_t **out,
                         qa_error *error);
bool native_wire_get_bytes(native_wire_reader *reader, qa_bytes *out, qa_error *error);
bool native_wire_get_string(native_wire_reader *reader, qa_buffer *out, qa_error *error);
bool native_wire_get_value(native_wire_reader *reader, qa_native_value *out, qa_buffer *storage,
                           qa_error *error);
bool native_wire_get_signature(native_wire_reader *reader, qa_native_signature *out,
                               qa_error *error);
void native_wire_signature_free(qa_native_signature *signature);
bool native_wire_get_error(native_wire_reader *reader, bool *ok, qa_error *error);
bool native_wire_get_state(native_wire_reader *reader, qa_native_processor_state *out,
                           qa_error *error);
bool native_wire_end(native_wire_reader *reader, qa_error *error);

bool native_wire_send(native_runner_connection *connection, uint16_t opcode, uint64_t reply_to,
                      qa_bytes payload, uint64_t *sequence, qa_error *error);
bool native_wire_receive(native_runner_connection *connection, native_wire_frame *out,
                         qa_error *error);
void native_wire_frame_free(native_wire_frame *frame);
void native_wire_poison(native_runner_connection *connection, const qa_error *error);

#endif
