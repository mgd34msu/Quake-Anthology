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
        snprintf(text, sizeof(text), handle < 1 || handle>64 ? "chat state handle %d out of range" :
                     "invalid chat state %d", handle);
        report(call, text);
    }
    return chat;
}

static bool nullable(q3_call *call, size_t index, qa_bytes *buffer, qa_error *error)
{
    return !call->arguments[index] || q3_string(call, call->arguments[index], buffer, error);
}
typedef struct chat_argument {
    q3_call *call;
    uint64_t address;
    qa_bytes bytes;
} chat_argument;
static bool argument_read(void *context,size_t maximum,qa_bytes *out,qa_error *error)
{
    chat_argument *argument=context;argument->bytes=(qa_bytes){0};
    if(!argument->address) return q3_fail(error,QA_ERROR_ARGUMENT,0,"Q3 chat source argument is NULL");
    if(maximum==SIZE_MAX) {
        if(!q3_string(argument->call,argument->address,&argument->bytes,error)) return false;
        *out=(qa_bytes){argument->bytes.data,argument->bytes.size};return true;
    }
    size_t limit=argument->call->host->options.maximum_string_bytes;
    if(maximum>limit) return q3_fail(error,QA_ERROR_ARGUMENT,0,"Q3 chat source prefix exceeds the admitted string extent");
    uint8_t *bytes=maximum?qa_arena_alloc(&argument->call->host->scratch,maximum,1,error):NULL;
    if(maximum && !bytes) return q3_fail(error,QA_ERROR_MEMORY,0,"Retaining Q3 chat source prefix");
    argument->bytes.data=bytes;
    for(size_t index=0;index<maximum;++index) {
        uint8_t byte;
        if(index>UINT64_MAX-argument->address ||
           !q3_read(argument->call,argument->address+index,&byte,1,error)) return false;
        if(!byte) break;
        bytes[argument->bytes.size++]=byte;
    }
    *out=(qa_bytes){argument->bytes.data,argument->bytes.size};return true;
}

typedef struct chat_memory {
    q3_call *call;
    uint64_t address;
    qa_bytes snapshot;
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
        return q3_vm_string_span(memory->call->vm, memory->address, SIZE_MAX, out, error);
    }
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
        qa_bytes input = {0};
        qa_bot_chat_match_io match = {.text = text, .read_offset = capture_offset, .write_offset = capture_write_offset,
            .write_length = capture_write_length, .write_type = capture_write_type};
        bool found;
        ok = text_admit(&memory, 0, 328, error) && q3_string(call, call->arguments[0], &input, error) &&
            qa_bot_chat_find_match_into(qa_bot_runtime_chat_system(runtime), (const char *)input.data,
                                        (uint32_t)q3_integer(call, 2), &match, &found, error);
        if (ok) *result = found;

    } else if (call->service == 520) ok = qa_bot_chat_unify_whitespace_into(&text, error);
    else ok = qa_bot_chat_replace_synonyms_into(qa_bot_runtime_chat_system(runtime), &text,
                                                 (uint32_t)q3_integer(call, 1), error);
    return ok;
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
    qa_bytes text = {0};
    bool ok = q3_string(call, call->arguments[0] + offset, &text, error) &&
              q3_write_string(call, call->arguments[2], (const char *)text.data, capacity, error);
    return ok;
}

static bool console_first(q3_call *call, qa_bot_chat *chat, int32_t *result, qa_error *error)
{
    qa_bot_console_message message;bool found;
    if(!qa_bot_chat_console_first_source(chat,&message,&found,error)) return false;
    if(!found) return true;
    q3_record record; uint64_t address = call->arguments[1];
    if (!q3_record_open(call, address, 276, &record, error) ||
        !q3_write_word(call, address, message.handle, error) ||
        !q3_write_float(call, address + 4, message.time, error) ||
        !q3_write_word(call, address + 8, (uint32_t)message.type, error) ||
        !q3_write(call,address+12,(qa_bytes){(const uint8_t *)message.text,256},error) ||
        !q3_write_word(call, address + 268, 0, error) || !q3_write_word(call, address + 272, 0, error)) return false;
    *result = (int32_t)message.handle; return true;
}

static bool construct(q3_call *call, qa_bot_runtime *runtime, int32_t *result, qa_error *error)
{
    bool reply=call->service==514;
    qa_bot_chat *chat=state(call,runtime);if(!chat) return true;
    chat_argument name={.call=call,.address=call->arguments[1]},values[8]={0};
    qa_bot_chat_text_source name_source={&name,argument_read},sources[8]={0};
    for(size_t index=0;index<8;++index) {
        values[index]=(chat_argument){.call=call,.address=call->arguments[(reply?4u:3u)+index]};
        if(values[index].address) sources[index]=(qa_bot_chat_text_source){&values[index],argument_read};
    }
    bool found;
    bool ok=reply?qa_bot_chat_reply_message_from(chat,&name_source,(uint32_t)q3_integer(call,2),
        (uint32_t)q3_integer(call,3),sources,qa_bot_runtime_time(runtime),&found,error):
        qa_bot_chat_initial_from(chat,name.address?&name_source:NULL,
            (uint32_t)q3_integer(call,2),sources,qa_bot_runtime_time(runtime),&found,error);
    if(ok && reply) *result=found;
    return ok;
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
        qa_bytes text = {0}, part = {0};
        bool ok = nullable(call, 0, &text, error) && nullable(call, 1, &part, error);
        if (ok) *result = qa_bot_chat_contains((const char *)text.data, (const char *)part.data, q3_integer(call, 2) != 0);

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
        qa_bot_chat *chat=state(call,runtime);bool ok=true;*result=8;
        chat_argument path={.call=call,.address=call->arguments[1]},name={.call=call,.address=call->arguments[2]};
        qa_bot_chat_text_source path_source={&path,argument_read},name_source={&name,argument_read};
        if(chat) ok=qa_bot_runtime_chat_load_from(runtime,(uint32_t)q3_integer(call,0),&path_source,&name_source,result,error);
        return ok?Q3_COMPLETED:Q3_FAILED;
    }
    bool ok = true;
    qa_bot_chat *chat = state(call, runtime);
    if (chat) switch (call->service) {
    case 508: ok = qa_bot_runtime_chat_free(runtime, (uint32_t)q3_integer(call, 0), error); break;
    case 509: {
        chat_argument input={.call=call,.address=call->arguments[2]};qa_bot_chat_text_source source={&input,argument_read};
        ok=qa_bot_chat_console_queue_from(chat,q3_integer(call,1),&source,qa_bot_runtime_time(runtime),NULL,error);
        break;
    }
    case 510: {bool removed;ok=qa_bot_chat_console_remove_source(chat,(uint32_t)q3_integer(call,1),&removed,error);break;}
    case 511: ok = console_first(call, chat, result, error); break;
    case 512: ok=qa_bot_chat_console_count_source(chat,result,error);break;
    case 515: {const char *message;ok=qa_bot_chat_message_source(chat,&message,error);if(ok) *result=(int32_t)strlen(message);break;}
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
        chat_argument input={.call=call,.address=call->arguments[1]};qa_bot_chat_text_source source={&input,argument_read};
        ok=qa_bot_chat_set_identity_from(chat,&source,call->host->options.abi==QA_QVM_Q3_116N?NULL:&client,error);
        break;
    }
    case 569: {
        chat_argument name={.call=call,.address=call->arguments[1]};qa_bot_chat_text_source source={&name,argument_read};
        ok=qa_bot_chat_initial_count_from(chat,name.address?&source:NULL,result,error);
        break;
    }
    case 570: ok = qa_bot_chat_write_message(chat, call, write_message, error); break;
    }
    return ok ? Q3_COMPLETED : Q3_FAILED;
}
