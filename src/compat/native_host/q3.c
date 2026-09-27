#include "internal.h"

static bool guest_read(void *context, uint64_t address, void *out, size_t size,
                       qa_error *error)
{
    return native_host_read(context, address, out, size, error);
}

static bool guest_write(void *context, uint64_t address, qa_bytes bytes,
                        qa_error *error)
{
    return native_host_write(context, address, bytes.data, bytes.size, error);
}

static bool guest_string(void *context, uint64_t address, size_t maximum,
                         qa_buffer *out, qa_error *error)
{
    qa_native_host *host = context;
    if (!maximum || maximum > host->maximum_string_bytes)
        maximum = host->maximum_string_bytes;
    return qa_native_read_string(host->instance, address, maximum, out, error);
}

static qa_native_host_guest_memory guest_memory(qa_native_host *host)
{
    qa_native_module_info info = qa_native_module_describe(qa_native_get_module(host->instance));
    return (qa_native_host_guest_memory){
        .context = host,
        .pointer_bytes = info.image.target.pointer_bytes,
        .read = guest_read,
        .write = guest_write,
        .read_string = guest_string};
}

static bool dispatch(qa_native_host *host, int32_t service,
                             const qa_native_value *arguments, size_t argument_count,
                             const qa_native_signature *fixed_signature,
                             qa_native_value *result, qa_error *error)
{
    int32_t canonical = service;
    bool engine = true;
    if (host->profile == QA_NATIVE_Q3_VMMAIN &&
        !qa_qvm_classify_syscall(host->q3_role, host->q3_abi, service,
                                 &canonical, &engine, error))
        return false;
    qa_native_host_q3_call call = {
        .host = host,
        .instance = host->instance,
        .profile = host->profile,
        .role = host->q3_role,
        .abi = host->q3_abi,
        .source_service = service,
        .canonical_service = canonical,
        .engine_service = engine,
        .arguments = arguments,
        .argument_count = argument_count,
        .fixed_signature = fixed_signature,
        .memory = guest_memory(host)};
    return host->q3.dispatch(host->q3.context, &call, result, error);
}

bool native_host_q3_dispatch(qa_native_host *host, int32_t service,
                             const qa_native_value *arguments, size_t argument_count,
                             qa_native_value *result, qa_error *error)
{
    return dispatch(host, service, arguments, argument_count, NULL, result, error);
}

bool native_host_describe_syscall(void *context, int32_t service,
                                  const qa_native_value_type **types, size_t *count,
                                  qa_error *error)
{
    qa_native_host *host = context;
    if (!host || host->profile != QA_NATIVE_Q3_VMMAIN || !types || !count ||
        !host->q3.describe_native)
        return native_host_fail(error, QA_ERROR_ARGUMENT, (size_t)(uint32_t)service,
                                "native Q3 syscall description is unavailable");
    return host->q3.describe_native(host->q3.context, host->q3_role, host->q3_abi,
                                    service, types, count, error);
}

static bool syscall_result(const qa_native_value *value, intptr_t *out, qa_error *error)
{
    switch (value->type) {
    case QA_NATIVE_VOID:
        *out = 0;
        return true;
    case QA_NATIVE_I8:
        *out = value->as.i8;
        return true;
    case QA_NATIVE_U8:
        *out = value->as.u8;
        return true;
    case QA_NATIVE_I16:
        *out = value->as.i16;
        return true;
    case QA_NATIVE_U16:
        *out = value->as.u16;
        return true;
    case QA_NATIVE_I32:
        *out = value->as.i32;
        return true;
    case QA_NATIVE_U32:
        *out = (intptr_t)value->as.u32;
        return true;
    case QA_NATIVE_I64:
        *out = (intptr_t)value->as.i64;
        return true;
    case QA_NATIVE_U64:
        *out = (intptr_t)value->as.u64;
        return true;
    case QA_NATIVE_ADDRESS:
        *out = (intptr_t)value->as.address;
        return true;
    case QA_NATIVE_F32: {
        uint32_t bits;
        memcpy(&bits, &value->as.f32, sizeof(bits));
        *out = (intptr_t)bits;
        return true;
    }
    case QA_NATIVE_F64:
    case QA_NATIVE_BYTES:
        return native_host_fail(error, QA_ERROR_FORMAT, value->type,
                                "native Q3 syscall returned an invalid scalar type");
    }
    return false;
}

bool native_host_syscall(void *context, qa_native_instance *instance, int32_t service,
                         const qa_native_value *arguments, size_t argument_count,
                         intptr_t *result, qa_error *error)
{
    qa_native_host *host = context;
    if (!host || (host->instance && host->instance != instance) || !result)
        return native_host_fail(error, QA_ERROR_ARGUMENT, 0,
                                "native Q3 syscall belongs to another host instance");
    bool bootstrap = host->instance == NULL;
    if (bootstrap)
        host->instance = instance;
    qa_native_value value = {.type = QA_NATIVE_I32};
    ++host->callback_depth;
    bool ok = native_host_q3_dispatch(host, service, arguments, argument_count, &value,
                                      error) &&
              syscall_result(&value, result, error);
    --host->callback_depth;
    if (bootstrap)
        host->instance = NULL;
    return ok;
}

static bool locate_game_data(qa_native_host *host, const qa_native_import_call *call,
                             qa_error *error)
{
    int32_t count = native_argument_i32(call, 1);
    int32_t stride = native_argument_i32(call, 2);
    if (count < 0 || count > 1024 || stride <= 0)
        return native_host_fail(error, QA_ERROR_ARGUMENT, call->slot,
                                "Quake Live located game-data descriptor is invalid");
    qa_native_entity_table table;
    if (!qa_native_entity_table_get(host->instance, &table, error) ||
        table.base != native_argument_address(call, 0) || table.stride != (size_t)stride ||
        table.count != (uint32_t)count || table.capacity != (uint32_t)count)
        return native_host_fail(error, QA_ERROR_FORMAT, call->slot,
                                "Quake Live entity table was not recorded consistently");
    if (call->argument_count != 5 || native_argument_i32(call, 4) <= 0)
        return native_host_fail(error, QA_ERROR_ARGUMENT, call->slot,
                                "Quake Live client-data descriptor is invalid");
    if (count == 0)
        return dispatch(host, (int32_t)call->slot, call->arguments,
                          call->argument_count, call->signature, NULL, error);
    if (!table.base)
        return false;
    return dispatch(host, (int32_t)call->slot, call->arguments,
                      call->argument_count, call->signature, NULL, error);
}

bool native_host_q3_import(qa_native_host *host, const qa_native_import_call *call,
                           qa_native_value *result, qa_error *error)
{
    if (host->profile != QA_NATIVE_QUAKE_LIVE_GAME_API10)
        return native_host_fail(error, QA_ERROR_ARGUMENT, call->slot,
                                "fixed Q3 imports require Quake Live API 10");
    if (call->slot == 22)
        return locate_game_data(host, call, error);
    return dispatch(host, (int32_t)call->slot, call->arguments,
                      call->argument_count, call->signature, result, error);
}
