#include "internal.h"

typedef union native_scalar_storage {
    int8_t i8;
    uint8_t u8;
    int16_t i16;
    uint16_t u16;
    int32_t i32;
    uint32_t u32;
    int64_t i64;
    uint64_t u64;
    float f32;
    double f64;
    void *pointer;
} native_scalar_storage;

static bool remember_type(native_ffi_signature *signature, ffi_type *type, ffi_type **elements,
                          qa_error *error) {
    if (signature->owned_count == signature->owned_capacity) {
        size_t capacity = signature->owned_capacity ? signature->owned_capacity * 2u : 8u;
        if (capacity < signature->owned_capacity || capacity > SIZE_MAX / sizeof(*signature->owned))
            return native_fail(error, QA_ERROR_MEMORY, 0, "native ABI type count overflow");
        native_owned_ffi_type *owned = realloc(signature->owned, capacity * sizeof(*owned));
        if (!owned)
            return native_fail(error, QA_ERROR_MEMORY, 0, "allocating native ABI types");
        signature->owned = owned;
        signature->owned_capacity = capacity;
    }
    signature->owned[signature->owned_count++] = (native_owned_ffi_type){type, elements};
    return true;
}

static ffi_type *scalar_type(qa_native_value_type type) {
    switch (type) {
    case QA_NATIVE_VOID:
        return &ffi_type_void;
    case QA_NATIVE_I8:
        return &ffi_type_sint8;
    case QA_NATIVE_U8:
        return &ffi_type_uint8;
    case QA_NATIVE_I16:
        return &ffi_type_sint16;
    case QA_NATIVE_U16:
        return &ffi_type_uint16;
    case QA_NATIVE_I32:
        return &ffi_type_sint32;
    case QA_NATIVE_U32:
        return &ffi_type_uint32;
    case QA_NATIVE_I64:
        return &ffi_type_sint64;
    case QA_NATIVE_U64:
        return &ffi_type_uint64;
    case QA_NATIVE_F32:
        return &ffi_type_float;
    case QA_NATIVE_F64:
        return &ffi_type_double;
    case QA_NATIVE_ADDRESS:
        return &ffi_type_pointer;
    case QA_NATIVE_BYTES:
        return NULL;
    }
    return NULL;
}

static ffi_type *build_type(native_ffi_signature *signature, const qa_native_type *type,
                            bool result, qa_error *error) {
    if (!type || !type->count) {
        native_fail(error, QA_ERROR_ARGUMENT, 0,
                    "native ABI types require a positive repetition count");
        return NULL;
    }
    if (type->kind == QA_NATIVE_VOID) {
        if (!result || type->count != 1 || type->field_count) {
            native_fail(error, QA_ERROR_ARGUMENT, 0, "void is valid only as one function result");
            return NULL;
        }
        return &ffi_type_void;
    }
    ffi_type *base = scalar_type(type->kind);
    if (type->kind == QA_NATIVE_BYTES) {
        if (!type->fields || !type->field_count) {
            native_fail(error, QA_ERROR_ARGUMENT, 0,
                        "native byte aggregates require field descriptors");
            return NULL;
        }
        if (type->field_count == SIZE_MAX ||
            type->field_count + 1u > SIZE_MAX / sizeof(ffi_type *)) {
            native_fail(error, QA_ERROR_MEMORY, 0, "native aggregate field count overflow");
            return NULL;
        }
        ffi_type **elements = calloc(type->field_count + 1u, sizeof(*elements));
        ffi_type *structure = calloc(1, sizeof(*structure));
        if (!elements || !structure) {
            free(elements);
            free(structure);
            native_fail(error, QA_ERROR_MEMORY, 0, "allocating native aggregate descriptor");
            return NULL;
        }
        structure->type = FFI_TYPE_STRUCT;
        structure->elements = elements;
        if (!remember_type(signature, structure, elements, error)) {
            free(elements);
            free(structure);
            return NULL;
        }
        for (size_t index = 0; index < type->field_count; ++index) {
            elements[index] = build_type(signature, &type->fields[index], false, error);
            if (!elements[index])
                return NULL;
        }
        base = structure;
    } else if (type->fields || type->field_count) {
        native_fail(error, QA_ERROR_ARGUMENT, 0, "scalar native ABI type has aggregate fields");
        return NULL;
    }
    if (type->count == 1)
        return base;
    if (type->count == SIZE_MAX || type->count + 1u > SIZE_MAX / sizeof(ffi_type *)) {
        native_fail(error, QA_ERROR_MEMORY, 0, "native ABI array count overflow");
        return NULL;
    }
    ffi_type **elements = calloc(type->count + 1u, sizeof(*elements));
    ffi_type *array = calloc(1, sizeof(*array));
    if (!elements || !array) {
        free(elements);
        free(array);
        native_fail(error, QA_ERROR_MEMORY, 0, "allocating native ABI array descriptor");
        return NULL;
    }
    array->type = FFI_TYPE_STRUCT;
    array->elements = elements;
    for (size_t index = 0; index < type->count; ++index)
        elements[index] = base;
    if (!remember_type(signature, array, elements, error)) {
        free(elements);
        free(array);
        return NULL;
    }
    return array;
}

bool native_ffi_prepare(native_ffi_signature *out, const qa_native_signature *signature,
                        qa_error *error) {
    if (!out || !signature || (signature->parameter_count && !signature->parameters))
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "native ABI signature and output are required");
    if (signature->variadic)
        return native_fail(error, QA_ERROR_UNSUPPORTED, 0,
                           "generic native ABI closures cannot be variadic");
    if (signature->parameter_count > NATIVE_MAX_ARGUMENTS)
        return native_fail(error, QA_ERROR_ARGUMENT, signature->parameter_count,
                           "native ABI signature exceeds the argument limit");
    qa_native_target host = qa_native_host_target();
    if (signature->abi != host.abi)
        return native_fail(error, QA_ERROR_UNSUPPORTED, 0,
                           "native signature ABI differs from the process ABI");
    native_ffi_signature prepared = {0};
    if (signature->parameter_count) {
        if (signature->parameter_count > SIZE_MAX / sizeof(*prepared.arguments))
            return native_fail(error, QA_ERROR_MEMORY, 0, "native ABI argument count overflow");
        prepared.arguments = calloc(signature->parameter_count, sizeof(*prepared.arguments));
        if (!prepared.arguments)
            return native_fail(error, QA_ERROR_MEMORY, 0, "allocating native ABI arguments");
    }
    for (size_t index = 0; index < signature->parameter_count; ++index) {
        prepared.arguments[index] =
            build_type(&prepared, &signature->parameters[index], false, error);
        if (!prepared.arguments[index]) {
            native_ffi_destroy(&prepared);
            return false;
        }
    }
    prepared.result = build_type(&prepared, &signature->result, true, error);
    if (!prepared.result) {
        native_ffi_destroy(&prepared);
        return false;
    }
    ffi_status status =
        ffi_prep_cif(&prepared.cif, FFI_DEFAULT_ABI, (unsigned int)signature->parameter_count,
                     prepared.result, prepared.arguments);
    if (status != FFI_OK) {
        native_ffi_destroy(&prepared);
        qa_error_set(error, QA_ERROR_UNSUPPORTED, status,
                     "libffi rejected the native ABI signature");
        return false;
    }
    *out = prepared;
    return true;
}

void native_ffi_destroy(native_ffi_signature *signature) {
    if (!signature)
        return;
    for (size_t index = 0; index < signature->owned_count; ++index) {
        free(signature->owned[index].elements);
        free(signature->owned[index].type);
    }
    free(signature->owned);
    free(signature->arguments);
    memset(signature, 0, sizeof(*signature));
}

static bool scalar_argument(const qa_native_value *value, native_scalar_storage *storage,
                            void **out, qa_error *error) {
    switch (value->type) {
    case QA_NATIVE_I8:
        storage->i8 = value->as.i8;
        *out = &storage->i8;
        return true;
    case QA_NATIVE_U8:
        storage->u8 = value->as.u8;
        *out = &storage->u8;
        return true;
    case QA_NATIVE_I16:
        storage->i16 = value->as.i16;
        *out = &storage->i16;
        return true;
    case QA_NATIVE_U16:
        storage->u16 = value->as.u16;
        *out = &storage->u16;
        return true;
    case QA_NATIVE_I32:
        storage->i32 = value->as.i32;
        *out = &storage->i32;
        return true;
    case QA_NATIVE_U32:
        storage->u32 = value->as.u32;
        *out = &storage->u32;
        return true;
    case QA_NATIVE_I64:
        storage->i64 = value->as.i64;
        *out = &storage->i64;
        return true;
    case QA_NATIVE_U64:
        storage->u64 = value->as.u64;
        *out = &storage->u64;
        return true;
    case QA_NATIVE_F32:
        storage->f32 = value->as.f32;
        *out = &storage->f32;
        return true;
    case QA_NATIVE_F64:
        storage->f64 = value->as.f64;
        *out = &storage->f64;
        return true;
    case QA_NATIVE_ADDRESS:
        storage->pointer = (void *)(uintptr_t)value->as.address;
        *out = &storage->pointer;
        return true;
    case QA_NATIVE_BYTES:
        *out = value->as.bytes.data;
        return true;
    case QA_NATIVE_VOID:
        break;
    }
    return native_fail(error, QA_ERROR_ARGUMENT, 0, "void native argument is invalid");
}

static bool read_result(const qa_native_signature *signature, const native_ffi_signature *prepared,
                        const native_scalar_storage *storage, const void *aggregate,
                        qa_native_value *out, qa_error *error) {
    qa_native_value_type type = signature->result.kind;
    if (type == QA_NATIVE_VOID)
        return true;
    if (!out)
        return native_fail(error, QA_ERROR_ARGUMENT, 0, "nonvoid native call requires a result");
    if (type == QA_NATIVE_BYTES) {
        if (out->type != QA_NATIVE_BYTES || !out->as.bytes.data ||
            out->as.bytes.size < prepared->result->size)
            return native_fail(error, QA_ERROR_ARGUMENT, 0,
                               "native aggregate result storage is too small");
        memcpy(out->as.bytes.data, aggregate, prepared->result->size);
        out->as.bytes.size = prepared->result->size;
        return true;
    }
    out->type = type;
    switch (type) {
    case QA_NATIVE_I8:
        out->as.i8 = storage->i8;
        break;
    case QA_NATIVE_U8:
        out->as.u8 = storage->u8;
        break;
    case QA_NATIVE_I16:
        out->as.i16 = storage->i16;
        break;
    case QA_NATIVE_U16:
        out->as.u16 = storage->u16;
        break;
    case QA_NATIVE_I32:
        out->as.i32 = storage->i32;
        break;
    case QA_NATIVE_U32:
        out->as.u32 = storage->u32;
        break;
    case QA_NATIVE_I64:
        out->as.i64 = storage->i64;
        break;
    case QA_NATIVE_U64:
        out->as.u64 = storage->u64;
        break;
    case QA_NATIVE_F32:
        out->as.f32 = storage->f32;
        break;
    case QA_NATIVE_F64:
        out->as.f64 = storage->f64;
        break;
    case QA_NATIVE_ADDRESS:
        out->as.address = (qa_native_address)(uintptr_t)storage->pointer;
        break;
    case QA_NATIVE_VOID:
    case QA_NATIVE_BYTES:
        break;
    }
    return true;
}

bool native_ffi_call(qa_native_instance *instance, qa_native_address address,
                     const qa_native_signature *signature, native_ffi_signature *prepared,
                     const qa_native_value *arguments, size_t argument_count,
                     qa_native_value *result, qa_error *error) {
    if (!address || !signature || !prepared || argument_count != signature->parameter_count ||
        (argument_count && !arguments))
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "native call address and exact arguments are required");
    if (argument_count > NATIVE_MAX_ARGUMENTS)
        return native_fail(error, QA_ERROR_ARGUMENT, argument_count,
                           "native call exceeds the argument limit");
    if (signature->result.kind != QA_NATIVE_VOID && !result)
        return native_fail(error, QA_ERROR_ARGUMENT, 0, "nonvoid native call requires a result");
    if (signature->result.kind == QA_NATIVE_BYTES &&
        (result->type != QA_NATIVE_BYTES || !result->as.bytes.data ||
         result->as.bytes.size < prepared->result->size))
        return native_fail(error, QA_ERROR_ARGUMENT, prepared->result->size,
                           "native aggregate result storage is too small");
    native_scalar_storage storage[NATIVE_MAX_ARGUMENTS] = {{0}};
    void *values[NATIVE_MAX_ARGUMENTS] = {0};
    for (size_t index = 0; index < argument_count; ++index) {
        qa_native_value_type expected = signature->parameters[index].kind;
        if (arguments[index].type != expected) {
            qa_error_set(error, QA_ERROR_ARGUMENT, index,
                         "native argument %zu has type %u; expected %u", index,
                         arguments[index].type, expected);
            return false;
        }
        if (!scalar_argument(&arguments[index], &storage[index], &values[index], error))
            return false;
        if (expected == QA_NATIVE_BYTES &&
            arguments[index].as.bytes.size != prepared->arguments[index]->size) {
            qa_error_set(error, QA_ERROR_ARGUMENT, index,
                         "native aggregate argument %zu has %zu bytes; expected %zu", index,
                         arguments[index].as.bytes.size, prepared->arguments[index]->size);
            return false;
        }
    }
    native_scalar_storage scalar_result = {0};
    void *aggregate_result = NULL;
    void *result_storage = &scalar_result;
    if (signature->result.kind == QA_NATIVE_BYTES) {
        aggregate_result = calloc(1, prepared->result->size);
        if (!aggregate_result)
            return native_fail(error, QA_ERROR_MEMORY, 0, "allocating native aggregate result");
        result_storage = aggregate_result;
    }
    union {
        uintptr_t integer;
        void (*function)(void);
    } target = {.integer = (uintptr_t)address};
    native_call_started(instance);
    ffi_call(&prepared->cif, target.function, result_storage, values);
    bool ok = read_result(signature, prepared, &scalar_result, aggregate_result, result, error);
    free(aggregate_result);
    return ok;
}

void native_latch_error(qa_native_instance *instance, const qa_error *error) {
    if (instance->failed) {
        native_runner_child_failure(instance, &instance->failure);
        return;
    }
    instance->failed = true;
    instance->failure = error ? *error : (qa_error){.code = QA_ERROR_ARGUMENT};
    if (!instance->failure.message[0])
        snprintf(instance->failure.message, sizeof(instance->failure.message),
                 "native import callback failed");
    native_runner_child_failure(instance, &instance->failure);
}

static qa_native_value decode_value(qa_native_value_type type, const ffi_type *ffi,
                                    void *argument) {
    qa_native_value value = {.type = type};
    switch (type) {
    case QA_NATIVE_I8:
        value.as.i8 = *(int8_t *)argument;
        break;
    case QA_NATIVE_U8:
        value.as.u8 = *(uint8_t *)argument;
        break;
    case QA_NATIVE_I16:
        value.as.i16 = *(int16_t *)argument;
        break;
    case QA_NATIVE_U16:
        value.as.u16 = *(uint16_t *)argument;
        break;
    case QA_NATIVE_I32:
        value.as.i32 = *(int32_t *)argument;
        break;
    case QA_NATIVE_U32:
        value.as.u32 = *(uint32_t *)argument;
        break;
    case QA_NATIVE_I64:
        value.as.i64 = *(int64_t *)argument;
        break;
    case QA_NATIVE_U64:
        value.as.u64 = *(uint64_t *)argument;
        break;
    case QA_NATIVE_F32:
        value.as.f32 = *(float *)argument;
        break;
    case QA_NATIVE_F64:
        value.as.f64 = *(double *)argument;
        break;
    case QA_NATIVE_ADDRESS:
        value.as.address = (qa_native_address)(uintptr_t)*(void **)argument;
        break;
    case QA_NATIVE_BYTES:
        value.as.bytes = (qa_native_memory){argument, ffi->size};
        break;
    case QA_NATIVE_VOID:
        break;
    }
    return value;
}

static bool encode_result(qa_native_value value, qa_native_value_type expected, ffi_type *ffi,
                          void *result, qa_error *error) {
    if (value.type != expected) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "native import returned type %u; expected %u",
                     value.type, expected);
        return false;
    }
    switch (expected) {
    case QA_NATIVE_VOID:
        return true;
    case QA_NATIVE_I8:
        *(int8_t *)result = value.as.i8;
        return true;
    case QA_NATIVE_U8:
        *(uint8_t *)result = value.as.u8;
        return true;
    case QA_NATIVE_I16:
        *(int16_t *)result = value.as.i16;
        return true;
    case QA_NATIVE_U16:
        *(uint16_t *)result = value.as.u16;
        return true;
    case QA_NATIVE_I32:
        *(int32_t *)result = value.as.i32;
        return true;
    case QA_NATIVE_U32:
        *(uint32_t *)result = value.as.u32;
        return true;
    case QA_NATIVE_I64:
        *(int64_t *)result = value.as.i64;
        return true;
    case QA_NATIVE_U64:
        *(uint64_t *)result = value.as.u64;
        return true;
    case QA_NATIVE_F32:
        *(float *)result = value.as.f32;
        return true;
    case QA_NATIVE_F64:
        *(double *)result = value.as.f64;
        return true;
    case QA_NATIVE_ADDRESS:
#if UINTPTR_MAX < UINT64_MAX
        if (value.as.address > (uint64_t)UINTPTR_MAX)
            return native_fail(error, QA_ERROR_ARGUMENT, 0,
                               "native import pointer result exceeds its actual ABI width");
#endif
        *(void **)result = (void *)(uintptr_t)value.as.address;
        return true;
    case QA_NATIVE_BYTES:
        if (!value.as.bytes.data || value.as.bytes.size != ffi->size)
            return native_fail(error, QA_ERROR_ARGUMENT, 0,
                               "native import aggregate result has wrong size");
        if (result != value.as.bytes.data)
            memcpy(result, value.as.bytes.data, ffi->size);
        return true;
    }
    return false;
}

bool native_dispatch_import(qa_native_instance *instance, const native_signature_spec *spec,
                            const qa_native_value *arguments, size_t count,
                            qa_native_value *result) {
    qa_error error = {0};
    if (instance->module->info.profile == QA_NATIVE_QUAKE_LIVE_GAME_API10 && spec->slot == 22) {
        if (count != 5 || arguments[0].type != QA_NATIVE_ADDRESS ||
            arguments[1].type != QA_NATIVE_I32 || arguments[2].type != QA_NATIVE_I32 ||
            arguments[3].type != QA_NATIVE_ADDRESS || arguments[4].type != QA_NATIVE_I32 ||
            arguments[1].as.i32 < 0 || arguments[2].as.i32 <= 0 || arguments[4].as.i32 <= 0) {
            qa_error_set(&error, QA_ERROR_ARGUMENT, spec->slot,
                         "Quake Live LocateGameData arguments are invalid");
            native_latch_error(instance, &error);
            return false;
        }
        qa_native_entity_table table = {.base = arguments[0].as.address,
                                        .stride = (size_t)arguments[2].as.i32,
                                        .count = (uint32_t)arguments[1].as.i32,
                                        .capacity = (uint32_t)arguments[1].as.i32};
        if (!native_entity_table_store(instance, table, &error)) {
            native_latch_error(instance, &error);
            return false;
        }
    }
    if (!strcmp(spec->name, "TagMalloc") && count == 2) {
        uint64_t requested;
        if (arguments[0].type == QA_NATIVE_I32) {
            if (arguments[0].as.i32 < 0) {
                qa_error_set(&error, QA_ERROR_ARGUMENT, 0, "native TagMalloc size is negative");
                native_latch_error(instance, &error);
                return false;
            }
            requested = (uint32_t)arguments[0].as.i32;
        } else {
            requested = arguments[0].as.u64;
        }
#if SIZE_MAX < UINT64_MAX
        if (requested > (uint64_t)SIZE_MAX) {
            qa_error_set(&error, QA_ERROR_MEMORY, 0, "native TagMalloc size exceeds the host");
            native_latch_error(instance, &error);
            return false;
        }
#endif
        qa_native_address address;
        if (!qa_native_allocate(instance, (size_t)requested, arguments[1].as.i32, &address,
                                &error)) {
            native_latch_error(instance, &error);
            return false;
        }
        *result = (qa_native_value){.type = QA_NATIVE_ADDRESS, .as.address = address};
        return true;
    }
    if (!strcmp(spec->name, "TagFree") && count == 1) {
        if (!qa_native_free(instance, arguments[0].as.address, &error)) {
            native_latch_error(instance, &error);
            return false;
        }
        *result = (qa_native_value){.type = QA_NATIVE_VOID};
        return true;
    }
    if (!strcmp(spec->name, "FreeTags") && count == 1) {
        qa_native_free_tag(instance, arguments[0].as.i32);
        *result = (qa_native_value){.type = QA_NATIVE_VOID};
        return true;
    }
    if (!instance->options.import) {
        qa_error_set(&error, QA_ERROR_UNSUPPORTED, spec->slot, "native import %s is not bound",
                     spec->name);
        native_latch_error(instance, &error);
        return false;
    }
    qa_native_import_call call = {.profile = instance->module->info.profile,
                                  .slot = spec->slot,
                                  .name = spec->name,
                                  .signature = &spec->signature,
                                  .arguments = arguments,
                                  .argument_count = count};
    ++instance->callback_depth;
    bool ok = instance->options.import(instance->options.context, instance, &call, result, &error);
    --instance->callback_depth;
    if (!ok)
        native_latch_error(instance, &error);
    return ok;
}

void native_import_dispatch(ffi_cif *cif, void *result, void **arguments, void *context) {
    native_import_binding *binding = context;
    qa_native_instance *instance = binding->instance;
    qa_native_value values[NATIVE_MAX_ARGUMENTS] = {{0}};
    qa_native_value output = {.type = binding->spec.signature.result.kind};
    size_t result_size = binding->ffi.result->size;
    if (result_size && result)
        memset(result, 0, result_size);
    if (output.type == QA_NATIVE_BYTES)
        output.as.bytes = (qa_native_memory){result, result_size};
    if (!instance || native_active_instance != instance) {
        /* A module-created thread cannot mutate the owner-thread latch or
         * transport. Actual runner children terminate; direct owners retain
         * their existing zero-result rejection without touching that latch. */
        native_runner_child_failure(instance, NULL);
        return;
    }
    size_t count = binding->spec.signature.parameter_count;
    if (count > NATIVE_MAX_ARGUMENTS) {
        native_runner_child_failure(instance, NULL);
        return;
    }
    for (size_t index = 0; index < count; ++index)
        values[index] = decode_value(binding->spec.signature.parameters[index].kind,
                                     cif->arg_types[index], arguments[index]);
    if (!native_dispatch_import(instance, &binding->spec, values, count, &output))
        return;
    qa_error encode_error = {0};
    if (!encode_result(output, binding->spec.signature.result.kind, binding->ffi.result, result,
                       &encode_error))
        native_latch_error(instance, &encode_error);
}

bool native_import_bind(native_import_binding *binding, qa_native_instance *instance,
                        const native_signature_spec *spec, qa_error *error) {
    memset(binding, 0, sizeof(*binding));
    binding->instance = instance;
    binding->spec = *spec;
    binding->spec.signature.abi = instance->module->info.image.target.abi;
    if (!native_ffi_prepare(&binding->ffi, &binding->spec.signature, error))
        return false;
    binding->closure = ffi_closure_alloc(sizeof(*binding->closure), &binding->code);
    if (!binding->closure) {
        native_ffi_destroy(&binding->ffi);
        return native_fail(error, QA_ERROR_MEMORY, spec->slot, "allocating native import closure");
    }
    ffi_status status = ffi_prep_closure_loc(binding->closure, &binding->ffi.cif,
                                             native_import_dispatch, binding, binding->code);
    if (status != FFI_OK) {
        ffi_closure_free(binding->closure);
        binding->closure = NULL;
        binding->code = NULL;
        native_ffi_destroy(&binding->ffi);
        qa_error_set(error, QA_ERROR_UNSUPPORTED, status, "libffi rejected native import %s",
                     spec->name);
        return false;
    }
    return true;
}

void native_import_unbind(native_import_binding *binding) {
    if (!binding)
        return;
    if (binding->closure)
        ffi_closure_free(binding->closure);
    native_ffi_destroy(&binding->ffi);
    memset(binding, 0, sizeof(*binding));
}

void native_observer_dispatch(ffi_cif *cif, void *result, void **arguments, void *context) {
    qa_native_entry_observer *binding = context;
    qa_native_instance *instance = binding->instance;
    if (cif->rtype->size && result)
        memset(result, 0, cif->rtype->size);
    if (!instance || native_active_instance != instance || !binding->callback)
        return;
    qa_native_value values[NATIVE_MAX_ARGUMENTS] = {{0}};
    size_t count = binding->signature.parameter_count;
    for (size_t index = 0; index < count; ++index)
        values[index] = decode_value(binding->signature.parameters[index].kind,
                                     cif->arg_types[index], arguments[index]);
    qa_native_value output = {.type = binding->signature.result.kind};
    if (output.type == QA_NATIVE_BYTES)
        output.as.bytes = (qa_native_memory){result, cif->rtype->size};
    qa_error error = {0};
    ++binding->active_calls;
    ++instance->callback_depth;
    bool ok = binding->callback(binding->context, instance, binding, values, count, &output,
                                &error);
    --instance->callback_depth;
    --binding->active_calls;
    if (ok)
        ok = encode_result(output, binding->signature.result.kind, cif->rtype, result, &error);
    if (!ok)
        native_latch_error(instance, &error);
}
