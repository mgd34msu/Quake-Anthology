#include "internal.h"

static void copy_function_pointer(void *out, const void *function, size_t function_size) {
    memset(out, 0, sizeof(void *));
    memcpy(out, function, function_size < sizeof(void *) ? function_size : sizeof(void *));
}

bool native_dispatch_formatted(qa_native_instance *instance, uint32_t slot, const char *name,
                               const qa_native_value *prefix, size_t prefix_count,
                               const char *format, va_list values, qa_error *error) {
    if (!instance || native_active_instance != instance || slot >= instance->import_count)
        return native_fail(error, QA_ERROR_ARGUMENT, slot,
                           "variadic native import ran outside its active instance");
    qa_buffer format_copy = {0};
    if (!qa_native_read_string(instance, (qa_native_address)(uintptr_t)format, NATIVE_MAX_STRING,
                               &format_copy, error))
        return false;
    va_list measured;
    va_copy(measured, values);
    int length = vsnprintf(NULL, 0, (const char *)format_copy.data, measured);
    va_end(measured);
    if (length < 0 || (size_t)length >= NATIVE_MAX_STRING) {
        qa_buffer_free(&format_copy);
        return native_fail(error, QA_ERROR_FORMAT, slot,
                           "native formatted message is invalid or too large");
    }
    char *text = malloc((size_t)length + 1u);
    if (!text) {
        qa_buffer_free(&format_copy);
        return native_fail(error, QA_ERROR_MEMORY, slot, "allocating native formatted message");
    }
    va_list written;
    va_copy(written, values);
    int produced = vsnprintf(text, (size_t)length + 1u, (const char *)format_copy.data, written);
    va_end(written);
    qa_buffer_free(&format_copy);
    if (produced != length) {
        free(text);
        return native_fail(error, QA_ERROR_FORMAT, slot,
                           "native formatted message changed while decoding");
    }
    qa_native_value arguments[4] = {{0}};
    if (prefix_count > sizeof(arguments) / sizeof(arguments[0]) - 1u) {
        free(text);
        return native_fail(error, QA_ERROR_ARGUMENT, slot,
                           "native formatted import prefix is too large");
    }
    if (prefix_count)
        memcpy(arguments, prefix, prefix_count * sizeof(*arguments));
    arguments[prefix_count] = (qa_native_value){.type = QA_NATIVE_ADDRESS,
                                                .as.address = (qa_native_address)(uintptr_t)text};
    native_import_binding *binding = &instance->imports[slot];
    if (strcmp(binding->spec.name, name) ||
        binding->spec.signature.parameter_count != prefix_count + 1u) {
        free(text);
        return native_fail(error, QA_ERROR_FORMAT, slot,
                           "native variadic import table does not match its thunk");
    }
    qa_native_value result = {.type = QA_NATIVE_VOID};
    bool ok =
        native_dispatch_import(instance, &binding->spec, arguments, prefix_count + 1u, &result);
    free(text);
    return ok;
}

static qa_native_instance *variadic_instance(void) {
    qa_native_instance *instance = native_active_instance;
    if (!instance)
        native_runner_child_reject_unbound_callback();
    return instance;
}

static void q2_bprintf(int32_t level, const char *format, ...) {
    qa_native_instance *instance = variadic_instance();
    qa_native_value prefix = {.type = QA_NATIVE_I32, .as.i32 = level};
    qa_error error = {0};
    va_list values;
    va_start(values, format);
    bool ok = native_dispatch_formatted(instance, 0, "bprintf", &prefix, 1, format, values, &error);
    va_end(values);
    if (!ok && instance)
        native_latch_error(instance, &error);
}

static void q2_dprintf(const char *format, ...) {
    qa_native_instance *instance = variadic_instance();
    qa_error error = {0};
    va_list values;
    va_start(values, format);
    bool ok = native_dispatch_formatted(instance, 1, "dprintf", NULL, 0, format, values, &error);
    va_end(values);
    if (!ok && instance)
        native_latch_error(instance, &error);
}

static void q2_cprintf(void *entity, int32_t level, const char *format, ...) {
    qa_native_instance *instance = variadic_instance();
    qa_native_value prefix[] = {
        {.type = QA_NATIVE_ADDRESS, .as.address = (qa_native_address)(uintptr_t)entity},
        {.type = QA_NATIVE_I32, .as.i32 = level}};
    qa_error error = {0};
    va_list values;
    va_start(values, format);
    bool ok = native_dispatch_formatted(instance, 2, "cprintf", prefix, 2, format, values, &error);
    va_end(values);
    if (!ok && instance)
        native_latch_error(instance, &error);
}

static void q2_centerprintf(void *entity, const char *format, ...) {
    qa_native_instance *instance = variadic_instance();
    qa_native_value prefix = {.type = QA_NATIVE_ADDRESS,
                              .as.address = (qa_native_address)(uintptr_t)entity};
    qa_error error = {0};
    va_list values;
    va_start(values, format);
    bool ok =
        native_dispatch_formatted(instance, 3, "centerprintf", &prefix, 1, format, values, &error);
    va_end(values);
    if (!ok && instance)
        native_latch_error(instance, &error);
}

static void q2_error(const char *format, ...) {
    qa_native_instance *instance = variadic_instance();
    qa_error error = {0};
    va_list values;
    va_start(values, format);
    bool ok = native_dispatch_formatted(instance, 7, "error", NULL, 0, format, values, &error);
    va_end(values);
    if (!ok && instance)
        native_latch_error(instance, &error);
}

void *native_variadic_import(qa_native_profile profile, uint32_t slot) {
    if (profile != QA_NATIVE_Q2_GAME_API3)
        return NULL;
    void *pointer = NULL;
    switch (slot) {
    case 0: {
        void (*function)(int32_t, const char *, ...) = q2_bprintf;
        copy_function_pointer(&pointer, &function, sizeof(function));
        break;
    }
    case 1: {
        void (*function)(const char *, ...) = q2_dprintf;
        copy_function_pointer(&pointer, &function, sizeof(function));
        break;
    }
    case 2: {
        void (*function)(void *, int32_t, const char *, ...) = q2_cprintf;
        copy_function_pointer(&pointer, &function, sizeof(function));
        break;
    }
    case 3: {
        void (*function)(void *, const char *, ...) = q2_centerprintf;
        copy_function_pointer(&pointer, &function, sizeof(function));
        break;
    }
    case 7: {
        void (*function)(const char *, ...) = q2_error;
        copy_function_pointer(&pointer, &function, sizeof(function));
        break;
    }
    default:
        break;
    }
    return pointer;
}

static qa_native_value variadic_value(qa_native_value_type type, va_list *values) {
    qa_native_value value = {.type = type};
    switch (type) {
    case QA_NATIVE_I8:
        value.as.i8 = (int8_t)va_arg(*values, int);
        break;
    case QA_NATIVE_U8:
        value.as.u8 = (uint8_t)va_arg(*values, int);
        break;
    case QA_NATIVE_I16:
        value.as.i16 = (int16_t)va_arg(*values, int);
        break;
    case QA_NATIVE_U16:
        value.as.u16 = (uint16_t)va_arg(*values, int);
        break;
    case QA_NATIVE_I32:
        value.as.i32 = va_arg(*values, int32_t);
        break;
    case QA_NATIVE_U32:
        value.as.u32 = va_arg(*values, uint32_t);
        break;
    case QA_NATIVE_I64:
        value.as.i64 = va_arg(*values, int64_t);
        break;
    case QA_NATIVE_U64:
        value.as.u64 = va_arg(*values, uint64_t);
        break;
    case QA_NATIVE_F32:
        value.as.f32 = (float)va_arg(*values, double);
        break;
    case QA_NATIVE_F64:
        value.as.f64 = va_arg(*values, double);
        break;
    case QA_NATIVE_ADDRESS:
        value.as.address = (qa_native_address)(uintptr_t)va_arg(*values, void *);
        break;
    case QA_NATIVE_VOID:
    case QA_NATIVE_BYTES:
        break;
    }
    return value;
}

intptr_t native_q3_syscall(int32_t service, ...) {
    qa_native_instance *instance = variadic_instance();
    if (!instance)
        return 0;
    qa_error error = {0};
    if (!instance->options.describe_syscall || !instance->options.syscall) {
        qa_error_set(&error, QA_ERROR_UNSUPPORTED, (size_t)(uint32_t)service,
                     "native Q3 syscall %d has no host binding", service);
        native_latch_error(instance, &error);
        return 0;
    }
    const qa_native_value_type *types = NULL;
    size_t count = 0;
    ++instance->callback_depth;
    bool described = instance->options.describe_syscall(instance->options.context, service, &types,
                                                        &count, &error);
    --instance->callback_depth;
    if (!described || (count && !types) || count > NATIVE_MAX_ARGUMENTS) {
        if (described)
            qa_error_set(&error, QA_ERROR_ARGUMENT, count,
                         "native Q3 syscall description is invalid");
        native_latch_error(instance, &error);
        return 0;
    }
    qa_native_value arguments[NATIVE_MAX_ARGUMENTS] = {{0}};
    va_list values;
    va_start(values, service);
    for (size_t index = 0; index < count; ++index) {
        if (types[index] == QA_NATIVE_VOID || types[index] == QA_NATIVE_BYTES) {
            va_end(values);
            qa_error_set(&error, QA_ERROR_ARGUMENT, index,
                         "native Q3 syscall variadic type %u is invalid", types[index]);
            native_latch_error(instance, &error);
            return 0;
        }
        arguments[index] = variadic_value(types[index], &values);
    }
    va_end(values);
    intptr_t result = 0;
    ++instance->callback_depth;
    bool ok = instance->options.syscall(instance->options.context, instance, service, arguments,
                                        count, &result, &error);
    --instance->callback_depth;
    if (!ok)
        native_latch_error(instance, &error);
    return ok ? result : 0;
}
