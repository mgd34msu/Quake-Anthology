#include "internal.h"
#include "qa/bot_runtime.h"

#include <stdio.h>

static void report(q3_call *call, const char *text)
{
    if (call->host->options.common.print)
        call->host->options.common.print(call->host->options.common.context, text);
}

static qa_bot_chat *state(q3_call *call, qa_bot_runtime *runtime)
{
    int32_t handle = q3_integer(call, 0);
    qa_bot_chat *chat = handle > 0 ? qa_bot_runtime_chat(runtime, (uint32_t)handle) : NULL;
    if (!chat) {
        char text[96];
        snprintf(text, sizeof(text), handle < 1 ? "chat state handle %d out of range" :
                     "invalid chat state %d", handle);
        report(call, text);
    }
    return chat;
}

static bool nullable(q3_call *call, size_t index, qa_buffer *buffer, qa_error *error)
{
    return !call->arguments[index] || q3_string(call, call->arguments[index], buffer, error);
}

typedef struct chat_memory {
    q3_call *call;
    uint64_t address;
    qa_buffer snapshot;
} chat_memory;

static bool text_address(chat_memory *memory, size_t offset, uint64_t *out, qa_error *error)
{
    if (!memory->address || offset > UINT64_MAX - memory->address)
        return q3_fail(error, QA_ERROR_ARGUMENT, 0, "Q3 chat buffer address overflow or NULL");
    *out = memory->address + offset; return true;
}

static bool text_admit(void *context, size_t offset, size_t size, qa_error *error)
{
    chat_memory *memory = context; q3_record record; uint64_t address;
    return text_address(memory, offset, &address, error) && q3_record_open(memory->call, address, size, &record, error);
}

static bool text_read(void *context, size_t offset, void *out, size_t size, qa_error *error)
{
    chat_memory *memory = context; uint64_t address;
    return text_address(memory, offset, &address, error) && q3_read(memory->call, address, out, size, error);
}

static bool text_write(void *context, size_t offset, qa_bytes bytes, qa_error *error)
{
    chat_memory *memory = context; uint64_t address;
    return text_address(memory, offset, &address, error) && q3_write(memory->call, address, bytes, error);
}

static bool text_copy(void *context, size_t destination, size_t source, size_t size, qa_error *error)
{
    chat_memory *memory = context;
    if (!text_admit(context, destination, size, error)) return false;
    uint8_t *bytes = size ? qa_arena_alloc(&memory->call->host->scratch, size, 1, error) : NULL;
    return (!size || bytes) && text_read(context, source, bytes, size, error) &&
        text_write(context, destination, (qa_bytes){bytes, size}, error);
}

static bool text_clear(void *context, size_t offset, size_t size, qa_error *error)
{
    chat_memory *memory = context;
    uint8_t *bytes = size ? qa_arena_alloc(&memory->call->host->scratch, size, 1, error) : NULL;
    if (size && !bytes) return false;
    if (size) memset(bytes, 0, size);
    return text_write(context, offset, (qa_bytes){bytes, size}, error);
}

static bool text_snapshot(void *context, qa_bytes *out, qa_error *error)
{
    chat_memory *memory = context;
    if (memory->call->vm) {
        qa_bytes view;
        if (!q3_vm_span(memory->call->vm, memory->address, 0, &view, error)) return false;
        size_t size = qa_qvm_memory_size(memory->call->vm);
        size_t available = size - (size_t)(memory->address - size);
        const uint8_t *end = memchr(view.data, 0, available);
        if (!end) return q3_fail(error, QA_ERROR_ARGUMENT, 0, "Q3 chat string has no terminator");
        *out = (qa_bytes){view.data, (size_t)(end - view.data)}; return true;
    }
    qa_buffer_free(&memory->snapshot);
    if (!q3_string(memory->call, memory->address, &memory->snapshot, error)) return false;
    *out = (qa_bytes){memory->snapshot.data, memory->snapshot.size}; return true;
}

static bool capture_offset(void *context, uint32_t index, int32_t *out, qa_error *error)
{
    uint8_t byte;
    if (!text_read(context, 264 + index * 8, &byte, 1, error)) return false;
    *out = byte < 128 ? byte : (int32_t)byte - 256; return true;
}

static bool capture_write_offset(void *context, uint32_t index, int32_t value, qa_error *error)
{
    uint8_t byte = (uint8_t)value;
    return text_write(context, 264 + index * 8, (qa_bytes){&byte, 1}, error);
}

static bool capture_word(void *context, size_t offset, int32_t value, qa_error *error)
{
    uint8_t bytes[4]; qa_store_u32le(bytes, (uint32_t)value);
    return text_write(context, offset, (qa_bytes){bytes, sizeof(bytes)}, error);
}

static bool capture_write_length(void *context, uint32_t index, int32_t value, qa_error *error)
{
    return capture_word(context, 268 + index * 8, value, error);
}

static bool capture_write_type(void *context, bool subtype, int32_t value, qa_error *error)
{
    return capture_word(context, subtype ? 260 : 256, value, error);
}

static bool text_operation(q3_call *call, qa_bot_runtime *runtime, int32_t *result, qa_error *error)
{
    chat_memory memory = {.call = call, .address = call->arguments[call->service == 518 ? 1 : 0]};
    qa_bot_chat_text_io text = {.context = &memory, .admit = text_admit, .read = text_read, .write = text_write,
        .copy = text_copy, .clear = text_clear, .snapshot = text_snapshot};
    bool ok;
    if (call->service == 518) {
        qa_buffer input = {0};
        qa_bot_chat_match_io match = {.text = text, .read_offset = capture_offset, .write_offset = capture_write_offset,
            .write_length = capture_write_length, .write_type = capture_write_type};
        bool found;
        ok = text_admit(&memory, 0, 328, error) && q3_string(call, call->arguments[0], &input, error) &&
            qa_bot_chat_find_match_into(qa_bot_runtime_chat_system(runtime), (const char *)input.data,
                                        (uint32_t)q3_integer(call, 2), &match, &found, error);
        if (ok) *result = found;
        qa_buffer_free(&input);
    } else if (call->service == 520) ok = qa_bot_chat_unify_whitespace_into(&text, error);
    else ok = qa_bot_chat_replace_synonyms_into(qa_bot_runtime_chat_system(runtime), &text,
                                                 (uint32_t)q3_integer(call, 1), error);
    qa_buffer_free(&memory.snapshot); return ok;
}

static bool write_message(void *context, const char *message, qa_error *error)
{
    q3_call *call = context;
    return q3_write_string(call, call->arguments[1], message, q3_integer(call, 2), error);
}

static bool match_variable(q3_call *call, qa_error *error)
{
    q3_record record;
    if (!q3_record_open(call, call->arguments[0], 328, &record, error)) return false;
    int32_t index = q3_integer(call, 1), capacity = q3_integer(call, 3);
    uint8_t offset = 255;
    if (index < 0 || index >= 8) report(call, "BotMatchVariable: variable out of range");
    else if (!q3_read(call, call->arguments[0] + 264 + (uint32_t)index * 8, &offset, 1, error)) return false;
    /* match_t uses signed char offsets even though the native owner can
     * address the complete 255-byte message. Negative offsets mean absent. */
    if (offset >= 128) {
        uint8_t zero = 0;
        return q3_write(call, call->arguments[2], (qa_bytes){&zero, 1}, error);
    }
    uint8_t bytes[4];
    if (!q3_read(call, call->arguments[0] + 268 + (uint32_t)index * 8, bytes, sizeof(bytes), error)) return false;
    int32_t length = qa_load_i32le(bytes);
    if (length < capacity) capacity = length + 1;
    qa_buffer text = {0};
    bool ok = q3_string(call, call->arguments[0] + offset, &text, error) &&
              q3_write_string(call, call->arguments[2], (const char *)text.data, capacity, error);
    qa_buffer_free(&text); return ok;
}

static bool console_first(q3_call *call, qa_bot_chat *chat, int32_t *result, qa_error *error)
{
    qa_bot_console_message message;
    if (!qa_bot_chat_console_first(chat, &message)) return true;
    q3_record record; uint64_t address = call->arguments[1];
    if (!q3_record_open(call, address, 276, &record, error) ||
        !q3_write_word(call, address, message.handle, error) ||
        !q3_write_float(call, address + 4, message.time, error) ||
        !q3_write_word(call, address + 8, (uint32_t)message.type, error) ||
        !q3_write_string(call, address + 12, message.text, 256, error) ||
        !q3_write_word(call, address + 268, 0, error) || !q3_write_word(call, address + 272, 0, error)) return false;
    *result = (int32_t)message.handle; return true;
}

static bool construct(q3_call *call, qa_bot_runtime *runtime, int32_t *result, qa_error *error)
{
    qa_buffer text = {0}, values[8] = {0};
    const char *variables[8] = {0};
    bool reply = call->service == 514;
    bool ok = reply ? q3_string(call, call->arguments[1], &text, error) : nullable(call, 1, &text, error);
    size_t first = reply ? 4 : 3;
    for (size_t i = 0; ok && i < 8; ++i) {
        ok = nullable(call, first + i, values + i, error);
        variables[i] = (const char *)values[i].data;
    }
    qa_bot_chat *chat = ok ? state(call, runtime) : NULL;
    if (chat) {
        bool found;
        float time = qa_bot_runtime_time(runtime);
        if (reply) {
            ok = qa_bot_chat_reply_message(chat, (const char *)text.data, (uint32_t)q3_integer(call, 2),
                    (uint32_t)q3_integer(call, 3), variables, time, &found, error);
            if (ok) *result = found;
        } else ok = qa_bot_chat_initial(chat, (const char *)text.data, (uint32_t)q3_integer(call, 2),
                                          variables, time, &found, error);
    }
    for (size_t i = 0; i < 8; ++i) qa_buffer_free(values + i);
    qa_buffer_free(&text); return ok;
}

q3_service_result q3_bot_chat(q3_call *call, int32_t *result, qa_error *error)
{
    if (call->host->options.role != QA_QVM_GAME) return Q3_UNHANDLED;
    switch (call->service) {
    case 507: case 508: case 509: case 510: case 511: case 512:
    case 513: case 514: case 515: case 516: case 517: case 518: case 519: case 520:
    case 521: case 522: case 523: case 524: case 569: case 570: break;
    default: return Q3_UNHANDLED;
    }
    qa_bot_runtime *runtime = q3_bot_runtime(call);
    if (!runtime) { q3_fail(error, QA_ERROR_UNSUPPORTED, 0, "Q3 bot runtime is unbound"); return Q3_FAILED; }
    if (call->service == 518 || call->service == 520 || call->service == 521)
        return text_operation(call, runtime, result, error) ? Q3_COMPLETED : Q3_FAILED;
    if (call->service == 519) return match_variable(call, error) ? Q3_COMPLETED : Q3_FAILED;
    if (call->service == 517) {
        qa_buffer text = {0}, part = {0};
        bool ok = nullable(call, 0, &text, error) && nullable(call, 1, &part, error);
        if (ok) *result = qa_bot_chat_contains((const char *)text.data, (const char *)part.data, q3_integer(call, 2) != 0);
        qa_buffer_free(&part); qa_buffer_free(&text);
        return ok ? Q3_COMPLETED : Q3_FAILED;
    }
    if (call->service == 507) {
        uint32_t handle;
        if (!qa_bot_runtime_chat_allocate(runtime, &handle, error)) return Q3_FAILED;
        *result = (int32_t)handle; return Q3_COMPLETED;
    }
    if (call->service == 513 || call->service == 514)
        return construct(call, runtime, result, error) ? Q3_COMPLETED : Q3_FAILED;
    if (call->service == 522) {
        qa_buffer path = {0}, name = {0};
        bool ok = q3_string(call, call->arguments[1], &path, error) && q3_string(call, call->arguments[2], &name, error);
        qa_bot_chat *chat = ok ? state(call, runtime) : NULL;
        if (ok && !chat) *result = 8;
        if (chat) ok = qa_bot_runtime_chat_load(runtime, (uint32_t)q3_integer(call, 0), (const char *)path.data,
                                                (const char *)name.data, result, error);
        qa_buffer_free(&name); qa_buffer_free(&path); return ok ? Q3_COMPLETED : Q3_FAILED;
    }
    qa_buffer text = {0};
    bool ok = call->service == 509 ? q3_string(call, call->arguments[2], &text, error) :
              call->service == 524 ? q3_string(call, call->arguments[1], &text, error) :
              call->service != 569 || nullable(call, 1, &text, error);
    qa_bot_chat *chat = ok ? state(call, runtime) : NULL;
    if (chat) switch (call->service) {
    case 508: ok = qa_bot_runtime_chat_free(runtime, (uint32_t)q3_integer(call, 0), error); break;
    case 509: ok = qa_bot_chat_console_queue(chat, q3_integer(call, 1), (const char *)text.data,
                                               qa_bot_runtime_time(runtime), NULL, error); break;
    case 510: (void)qa_bot_chat_console_remove(chat, (uint32_t)q3_integer(call, 1)); break;
    case 511: ok = console_first(call, chat, result, error); break;
    case 512: *result = (int32_t)qa_bot_chat_console_count(chat); break;
    case 515: *result = (int32_t)strlen(qa_bot_chat_message(chat)); break;
    case 516: {
        int32_t recipient = q3_integer(call, 1), destination = q3_integer(call, 2);
        bool legacy = call->host->options.abi == QA_QVM_Q3_116N;
        qa_bot_chat_destination target = destination == 1 ? QA_BOT_CHAT_TEAM :
            !legacy && destination == 2 ? QA_BOT_CHAT_TELL : QA_BOT_CHAT_ALL;
        int32_t client=recipient;
        ok = (!legacy || q3_bot_client_number(call,recipient,&client,error)) &&
            qa_bot_chat_enter_from(chat, legacy ? &client : NULL, recipient, target, error); break;
    }
    case 523: qa_bot_chat_set_gender(chat, (uint32_t)q3_integer(call, 1)); break;
    case 524: {
        int32_t client = q3_integer(call, 2);
        if(call->host->options.abi!=QA_QVM_Q3_116N && !q3_bot_client_number(call,client,&client,error)) {ok=false;break;}
        qa_bot_chat_set_identity(chat, (const char *)text.data,
                                   call->host->options.abi == QA_QVM_Q3_116N ? NULL : &client); break;
    }
    case 569: *result = (int32_t)qa_bot_chat_initial_count(chat, (const char *)text.data); break;
    case 570: ok = qa_bot_chat_write_message(chat, call, write_message, error); break;
    }
    qa_buffer_free(&text);
    return ok ? Q3_COMPLETED : Q3_FAILED;
}
