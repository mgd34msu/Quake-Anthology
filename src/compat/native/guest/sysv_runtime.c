#include "sysv_libc_private.h"

bool sysv_fail(qa_error *error, qa_status code, const char *text)
{ qa_error_set(error, code, 0, "%s", text); return false; }
bool sysv_equal_text(const char *a, const char *b)
{ return a == b || (a && b && !strcmp(a, b)); }
bool sysv_copy_text(const char *text, char **out, qa_error *error)
{
    *out = NULL;
    if (!text) return true;
    size_t bytes = strlen(text);
    if (bytes == SIZE_MAX) return sysv_fail(error, QA_ERROR_MEMORY, "System V text extent overflow");
    char *copy = malloc(bytes + 1);
    if (!copy) return sysv_fail(error, QA_ERROR_MEMORY, "allocating System V owned text");
    memcpy(copy, text, bytes + 1); *out = copy; return true;
}
bool sysv_current(guest_sysv_runtime *runtime, qa_error *error)
{
    if (!runtime || !runtime->guest || runtime->failed || runtime->retired || runtime->detached)
        return sysv_fail(error, QA_ERROR_ARGUMENT, "System V runtime has no live attached process");
    return guest_ready(runtime->guest, error) &&
        runtime->options.bindings.current(runtime->options.bindings.context, runtime->guest, error);
}
bool sysv_read(guest_sysv_runtime *runtime, uint64_t address, void *out,
    size_t bytes, qa_error *error)
{ return sysv_current(runtime, error) && qa_native_guest_read(runtime->guest, address, out, bytes, error); }
bool sysv_write(guest_sysv_runtime *runtime, uint64_t address, const void *data,
    size_t bytes, qa_error *error)
{ return sysv_current(runtime, error) && qa_native_guest_write(runtime->guest, address, (qa_bytes){data, bytes}, error); }
bool sysv_unsigned(guest_sysv_runtime *runtime, uint64_t address, size_t bytes,
    uint64_t *out, qa_error *error)
{
    uint8_t data[8];
    if (!out || !bytes || bytes > 8 || !sysv_read(runtime, address, data, bytes, error)) return false;
    uint64_t value = 0;
    for (size_t i = 0; i < bytes; ++i) value |= (uint64_t)data[i] << (i * 8);
    *out = value; return true;
}
bool sysv_store(guest_sysv_runtime *runtime, uint64_t address, size_t bytes,
    uint64_t value, qa_error *error)
{
    uint8_t data[8];
    if (!bytes || bytes > 8) return sysv_fail(error, QA_ERROR_ARGUMENT, "invalid System V scalar width");
    for (size_t i = 0; i < bytes; ++i) data[i] = (uint8_t)(value >> (i * 8));
    return sysv_write(runtime, address, data, bytes, error);
}
bool sysv_pointer(guest_sysv_runtime *runtime, uint64_t address, uint64_t *out, qa_error *error)
{ return sysv_unsigned(runtime, address, runtime->target.pointer_bytes, out, error); }
bool sysv_put_pointer(guest_sysv_runtime *runtime, uint64_t address, uint64_t value, qa_error *error)
{
    if (runtime->target.pointer_bytes == 4 && value > UINT32_MAX)
        return sysv_fail(error, QA_ERROR_ARGUMENT, "System V pointer exceeds actual target width");
    return sysv_store(runtime, address, runtime->target.pointer_bytes, value, error);
}
bool sysv_errno(guest_sysv_runtime *runtime, int value, qa_error *error)
{ return sysv_store(runtime, runtime->errno_address, 4, (uint32_t)value, error); }
bool sysv_string(guest_sysv_runtime *runtime, uint64_t address, size_t maximum,
    qa_buffer *out, bool *terminated, qa_error *error)
{
    if (!out || out->data || out->size || !terminated || maximum > SYSV_MAX_STRING)
        return sysv_fail(error, QA_ERROR_ARGUMENT, "invalid System V string receipt");
    *terminated = false;
    size_t capacity = 0;
    for (size_t i = 0; i < maximum; ++i) {
        uint8_t byte;
        if (address > UINT64_MAX - i || !sysv_read(runtime, address + i, &byte, 1, error)) {
            qa_buffer_free(out); return false;
        }
        if (!byte) { *terminated = true; return true; }
        if (!guest_grow((void **)&out->data, &capacity, out->size + 1, 1, error)) {
            qa_buffer_free(out); return false;
        }
        out->data[out->size++] = byte;
    }
    return true;
}
bool sysv_allocate(guest_sysv_runtime *runtime, uint64_t bytes, bool heap,
    uint64_t *out, qa_error *error)
{
    if (!out || bytes > SYSV_MAX_ALLOCATION || !sysv_current(runtime, error)) return false;
    if (heap && !guest_grow((void **)&runtime->heap, &runtime->heap_capacity,
        runtime->heap_count + 1, sizeof(*runtime->heap), error)) return false;
    uint64_t address;
    if (!qa_native_guest_allocate(runtime->guest, bytes ? (size_t)bytes : 1,
        heap ? INT32_C(0x53595648) : INT32_C(0x53595652), &address, error)) return false;
    if (heap) runtime->heap[runtime->heap_count++] = (sysv_heap){address, bytes ? bytes : 1};
    *out = address; return true;
}
bool sysv_allocation_size(guest_sysv_runtime *runtime, uint64_t address,
    uint64_t *out, qa_error *error)
{
    if (!out || !sysv_current(runtime, error)) return false;
    for (size_t i = 0; i < runtime->heap_count; ++i)
        if (runtime->heap[i].address == address) { *out = runtime->heap[i].bytes; return true; }
    return sysv_fail(error, QA_ERROR_ARGUMENT, "System V pointer is not a live heap base");
}
bool sysv_free(guest_sysv_runtime *runtime, uint64_t address, qa_error *error)
{
    if (!sysv_current(runtime, error)) return false;
    if (!address) return true;
    for (size_t i = 0; i < runtime->heap_count; ++i) if (runtime->heap[i].address == address) {
        if (!qa_native_guest_free(runtime->guest, address, error)) return false;
        memmove(runtime->heap + i, runtime->heap + i + 1,
            (runtime->heap_count - i - 1) * sizeof(*runtime->heap));
        --runtime->heap_count; return true;
    }
    return sysv_fail(error, QA_ERROR_ARGUMENT, "System V free of a non-live allocation");
}
bool sysv_string_new(guest_sysv_runtime *runtime, const char *text,
    uint64_t *out, qa_error *error)
{
    if (!text) return sysv_fail(error, QA_ERROR_ARGUMENT, "System V string requires actual bytes");
    size_t bytes = strlen(text);
    uint64_t address;
    if (bytes >= SYSV_MAX_ALLOCATION || !sysv_allocate(runtime, bytes + 1, false, &address, error) ||
        !sysv_write(runtime, address, text, bytes + 1, error)) return false;
    *out = address; return true;
}
qa_native_value_type sysv_size_type(const guest_sysv_runtime *runtime)
{ return runtime->target.pointer_bytes == 8 ? QA_NATIVE_U64 : QA_NATIVE_U32; }
qa_native_value_type sysv_signed_type(const guest_sysv_runtime *runtime)
{ return runtime->target.pointer_bytes == 8 ? QA_NATIVE_I64 : QA_NATIVE_I32; }
guest_abi_layout sysv_layout(const guest_sysv_runtime *runtime, qa_native_value_type type)
{
    size_t bytes = type == QA_NATIVE_VOID ? 0 :
        type == QA_NATIVE_I8 || type == QA_NATIVE_U8 ? 1 :
        type == QA_NATIVE_I16 || type == QA_NATIVE_U16 ? 2 :
        type == QA_NATIVE_I32 || type == QA_NATIVE_U32 || type == QA_NATIVE_F32 ? 4 :
        type == QA_NATIVE_ADDRESS ? runtime->target.pointer_bytes : 8;
    return (guest_abi_layout){.kind = type, .bytes = bytes, .alignment = bytes};
}
uint64_t sysv_integer(const qa_native_value *value)
{
    switch (value->type) {
    case QA_NATIVE_I8: return (uint64_t)(int64_t)value->as.i8;
    case QA_NATIVE_U8: return value->as.u8;
    case QA_NATIVE_I16: return (uint64_t)(int64_t)value->as.i16;
    case QA_NATIVE_U16: return value->as.u16;
    case QA_NATIVE_I32: return (uint64_t)(int64_t)value->as.i32;
    case QA_NATIVE_U32: return value->as.u32;
    case QA_NATIVE_I64: return (uint64_t)value->as.i64;
    case QA_NATIVE_U64: return value->as.u64;
    case QA_NATIVE_ADDRESS: return value->as.address;
    default: return 0;
    }
}
static bool service_invoke(void *context, qa_native_guest *guest,
    const qa_native_value *arguments, size_t count, qa_native_value *result, qa_error *error)
{
    sysv_service *service = context;
    guest_sysv_runtime *runtime = service->runtime;
    if (guest != runtime->guest || count != service->parameter_count)
        return sysv_fail(error, QA_ERROR_ARGUMENT, "System V service belongs to a different process or ABI");
    if (!sysv_current(runtime, error)) return false;
    ++runtime->calls;
    bool ok;
    switch (service->group) {
    case SYSV_LIBC: ok = sysv_libc_call(service, arguments, result, error); break;
    case SYSV_STDIO: ok = sysv_stdio_call(service, arguments, result, error); break;
    case SYSV_CXX: ok = sysv_cxx_call(service, arguments, result, error); break;
    case SYSV_LOCALE: ok = sysv_locale_call(service, arguments, result, error); break;
    case SYSV_IOSTREAM: ok = sysv_iostream_call(service, arguments, result, error); break;
    case SYSV_FORMAT: ok = sysv_format_call(service, arguments, result, error); break;
    default:
        qa_error_set(error, QA_ERROR_UNSUPPORTED, service->address,
            "unsupported System V %s:%s@%s: %s", service->library, service->name,
            service->version ? service->version : "<unversioned>", service->detail ? service->detail : "no exact implementation");
        ok = false; break;
    }
    --runtime->calls; return ok;
}
static guest_runtime_function service_descriptor(sysv_service *service)
{
    return (guest_runtime_function){.id = service->id, .address = service->address,
        .signature = {.abi = service->runtime->target.abi, .parameters = service->parameters,
            .parameter_count = service->parameter_count, .result = service->result},
        .invoke = service_invoke, .context = service};
}
static void service_free(sysv_service *service)
{
    if (!service) return;
    free(service->library); free(service->name); free(service->version); free(service->detail); free(service);
}
bool sysv_service_add(guest_sysv_runtime *runtime, sysv_group group, uint32_t operation,
    uint64_t a, uint64_t b, uint64_t c, const char *library, const char *name,
    const char *const *versions, size_t version_count, const qa_native_value_type *parameters,
    size_t count, qa_native_value_type result, const char *detail, uint64_t *address, qa_error *error)
{
    if (!library || !name || !versions || !version_count || count > 8 || (count && !parameters) ||
        !sysv_current(runtime, error)) return false;
    for (size_t v = 0; v < version_count; ++v) {
        if (runtime->next_service == UINT64_MAX || !guest_grow((void **)&runtime->services,
            &runtime->service_capacity, runtime->service_count + 1, sizeof(*runtime->services), error)) return false;
        sysv_service *service = calloc(1, sizeof(*service));
        if (!service) return sysv_fail(error, QA_ERROR_MEMORY, "allocating stable System V callback descriptor");
        if (runtime->trap_used > runtime->options.trap_bytes ||
            runtime->options.trap_bytes - runtime->trap_used < 16 ||
            runtime->options.trap_base > UINT64_MAX - runtime->trap_used) {
            free(service); return sysv_fail(error, QA_ERROR_MEMORY, "System V import trap pages are exhausted");
        }
        service->runtime = runtime; service->id = runtime->next_service;
        service->address = runtime->options.trap_base + runtime->trap_used;
        service->group = group; service->operation = operation;
        service->a = a; service->b = b; service->c = c; service->parameter_count = count;
        service->result = sysv_layout(runtime, result);
        for (size_t i = 0; i < count; ++i) service->parameters[i] = sysv_layout(runtime, parameters[i]);
        if (!sysv_copy_text(library, &service->library, error) || !sysv_copy_text(name, &service->name, error) ||
            !sysv_copy_text(versions[v], &service->version, error) || !sysv_copy_text(detail, &service->detail, error)) {
            service_free(service); return false;
        }
        guest_runtime_import_key key = {.scope = runtime->options.scope, .library = library,
            .kind = GUEST_RUNTIME_SYMBOL_NAME, .name = name, .version = versions[v]};
        guest_runtime_function function = service_descriptor(service);
        ++runtime->next_service; runtime->trap_used += 16;
        runtime->services[runtime->service_count++] = service;
        if (!guest_runtime_imports_function(runtime->imports, &key, &function, error)) { runtime->failed = true; return false; }
        guest_runtime_import_view view;
        if (!guest_runtime_imports_find(runtime->imports, &key, &view, error)) return false;
        service->address = view.address;
        if (address && !v) *address = view.address;
    }
    return true;
}
bool sysv_data(guest_sysv_runtime *runtime, const char *library, const char *name,
    const char *version, uint64_t address, size_t bytes, qa_error *error)
{
    guest_runtime_import_key key = {.scope = runtime->options.scope, .library = library,
        .kind = GUEST_RUNTIME_SYMBOL_NAME, .name = name, .version = version};
    return sysv_current(runtime, error) && guest_range(runtime->guest, address, bytes,
        QA_NATIVE_GUEST_READ, error) && guest_runtime_imports_data(runtime->imports, &key, address, bytes, error);
}
bool sysv_object_add(guest_sysv_runtime *runtime, uint32_t kind, const char *name,
    uint64_t address, uint64_t bytes, const uint64_t *values, size_t count, qa_error *error)
{
    if (count > 8 || (count && !values) || !guest_grow((void **)&runtime->objects,
        &runtime->object_capacity, runtime->object_count + 1, sizeof(*runtime->objects), error)) return false;
    sysv_object object = {.kind = kind, .address = address, .bytes = bytes};
    if (!sysv_copy_text(name, &object.name, error)) return false;
    if (count) memcpy(object.values, values, count * sizeof(*values));
    runtime->objects[runtime->object_count++] = object; return true;
}
sysv_object *sysv_object_find(guest_sysv_runtime *runtime, uint32_t kind, const char *name)
{
    for (size_t i = 0; i < runtime->object_count; ++i)
        if (runtime->objects[i].kind == kind && sysv_equal_text(runtime->objects[i].name, name)) return runtime->objects + i;
    return NULL;
}
static bool execution_valid(qa_native_guest_backend execution,
    const qa_native_target *target, size_t budget)
{
    return (execution == QA_NATIVE_GUEST_EMULATED && budget) ||
        (execution == QA_NATIVE_GUEST_HOST_X86_64 && !budget &&
         target->os == QA_NATIVE_OS_LINUX && target->arch == QA_NATIVE_ARCH_X86_64 &&
         target->abi == QA_NATIVE_ABI_SYSTEM_V_X64 && target->pointer_bytes == 8);
}
static bool return_trap_valid(const qa_native_guest *guest, uint64_t address, qa_error *error)
{
    bool native = qa_native_guest_execution(guest) == QA_NATIVE_GUEST_HOST_X86_64;
    uint32_t permissions = QA_NATIVE_GUEST_EXECUTE | (native ? QA_NATIVE_GUEST_READ : 0);
    if (!guest_range(guest, address, 1, permissions, error)) return false;
    if (native) {
        uint8_t trap;
        if (!qa_native_guest_read(guest, address, &trap, sizeof(trap), error)) return false;
        if (trap != 0xcc) return sysv_fail(error, QA_ERROR_FORMAT, "System V native return trap has no actual breakpoint byte");
    }
    return true;
}
bool sysv_invoke(guest_sysv_runtime *runtime, uint64_t target,
    const qa_native_value_type *types, size_t count, qa_native_value_type result_type,
    const qa_native_value *arguments, qa_native_value *result, qa_error *error)
{
    if (count > 8 || !sysv_current(runtime, error)) return false;
    guest_abi_layout parameters[8];
    for (size_t i = 0; i < count; ++i) parameters[i] = sysv_layout(runtime, types[i]);
    guest_abi_signature signature = {.abi = runtime->target.abi, .parameters = parameters,
        .parameter_count = count, .result = sysv_layout(runtime, result_type)};
    guest_abi_plan *plan = NULL;
    if (!guest_abi_plan_create(&signature, NULL, 0, &plan, error)) return false;
    ++runtime->calls;
    bool ok = runtime->execution == QA_NATIVE_GUEST_HOST_X86_64 ?
        guest_abi_invoke_native(plan, runtime->guest, target, runtime->options.return_trap,
            arguments, count, result, error) :
        guest_abi_invoke(plan, runtime->guest, target, runtime->options.return_trap,
            arguments, count, result, runtime->budget, error);
    --runtime->calls; guest_abi_plan_destroy(plan); return ok;
}
static bool target_valid(const qa_native_target *target)
{
    return target && target->os == QA_NATIVE_OS_LINUX &&
        ((target->arch == QA_NATIVE_ARCH_I386 && target->abi == QA_NATIVE_ABI_SYSTEM_V_I386 && target->pointer_bytes == 4) ||
         (target->arch == QA_NATIVE_ARCH_X86_64 && target->abi == QA_NATIVE_ABI_SYSTEM_V_X64 && target->pointer_bytes == 8));
}
static bool target_equal(const qa_native_target *a,const qa_native_target *b)
{
    return a->os == b->os && a->arch == b->arch && a->abi == b->abi &&
        a->pointer_bytes == b->pointer_bytes;
}
static bool bindings_valid(const guest_sysv_bindings *bindings)
{
    if (!bindings || !bindings->current || (!!bindings->clock_id != (bindings->time != NULL)) ||
        (bindings->open_file && !bindings->resources)) return false;
    for (size_t i = 0; i < 3; ++i) {
        const guest_sysv_stream *stream = bindings->streams+i;
        if (!!stream->id != (stream->read || stream->write || stream->flush)) return false;
        for (size_t j = 0; j < i; ++j) if (stream->id && stream->id == bindings->streams[j].id &&
            (stream->read != bindings->streams[j].read || stream->write != bindings->streams[j].write ||
             stream->flush != bindings->streams[j].flush || stream->context != bindings->streams[j].context)) return false;
    }
    return true;
}
static bool strings_vector(guest_sysv_runtime *runtime, const char *const *values,
    size_t count, uint64_t *out, qa_error *error)
{
    size_t p = runtime->target.pointer_bytes;
    if (count > SYSV_MAX_ALLOCATION / p - 1 || (count && !values) ||
        !sysv_allocate(runtime, (count + 1) * p, false, out, error)) return false;
    for (size_t i = 0; i < count; ++i) {
        uint64_t string;
        if (!sysv_string_new(runtime, values[i], &string, error) ||
            !sysv_put_pointer(runtime, *out + i * p, string, error)) return false;
    }
    return true;
}
bool guest_sysv_create(qa_native_guest *guest, const guest_sysv_options *options,
    guest_sysv_runtime **out, qa_error *error)
{
    if (!guest || !options || !out || *out || !options->scope || !bindings_valid(&options->bindings) ||
        !options->first_function || !target_valid(&guest->options.image.target) ||
        !execution_valid(qa_native_guest_execution(guest), &guest->options.image.target, options->instruction_budget) ||
        options->argc > INT32_MAX || !qa_native_guest_idle(guest))
        return sysv_fail(error, QA_ERROR_ARGUMENT, "System V construction requires an actual idle Linux x86 process and return trap");
    if (!return_trap_valid(guest, options->return_trap, error)) return false;
    guest_sysv_runtime *runtime = calloc(1, sizeof(*runtime));
    if (!runtime) return sysv_fail(error, QA_ERROR_MEMORY, "allocating System V process owner");
    *out = runtime; runtime->guest = guest; runtime->target = guest->options.image.target;
    runtime->execution = qa_native_guest_execution(guest);
    runtime->options = *options; runtime->budget = options->instruction_budget; runtime->next_service = options->first_function;
    size_t p = runtime->target.pointer_bytes;
    if (!guest_runtime_imports_create(guest, &runtime->imports, error) ||
        !guest_runtime_imports_traps(runtime->imports, options->trap_base, options->trap_bytes, error) ||
        !sysv_allocate(runtime, 0x11000, false, &runtime->thread_area, error)) goto failed;
    runtime->thread_pointer = runtime->thread_area + 0x10000;
    if (!sysv_put_pointer(runtime, runtime->thread_pointer, runtime->thread_pointer, error) ||
        !sysv_put_pointer(runtime, runtime->thread_pointer + 2 * p, runtime->thread_pointer, error) ||
        !sysv_allocate(runtime, 1026 * 2 * p, false, &runtime->dtv_base, error)) goto failed;
    runtime->dtv = runtime->dtv_base + 2 * p;
    if (!sysv_store(runtime, runtime->dtv_base, p, 1024, error) ||
        !sysv_store(runtime, runtime->dtv, p, 1, error) ||
        !sysv_put_pointer(runtime, runtime->thread_pointer + p, runtime->dtv, error) ||
        !sysv_allocate(runtime, 4, false, &runtime->errno_address, error) ||
        !strings_vector(runtime, options->argv, options->argc, &runtime->argv, error) ||
        !strings_vector(runtime, options->environment, options->environment_count, &runtime->envp, error) ||
        !sysv_allocate(runtime, p * 4, false, &runtime->empty_string, error) ||
        !sysv_data(runtime, "libstdc++.so.6", "_ZNSs4_Rep20_S_empty_rep_storageE", "GLIBCXX_3.4", runtime->empty_string, p * 4, error) ||
        !sysv_libc_install(runtime, error) || !sysv_cxx_install(runtime, error) || !sysv_iostream_install(runtime, error)) goto failed;
    qa_native_guest_cpu cpu;
    if (!qa_native_guest_cpu_read(guest, &cpu, error)) goto failed;
    cpu.segments[p == 8 ? 4 : 5].base = runtime->thread_pointer;
    if (!qa_native_guest_cpu_write(guest, &cpu, error)) goto failed;
    runtime->options.argv = NULL; runtime->options.environment = NULL;
    return true;
failed:
    runtime->failed = true; return false;
}
bool guest_sysv_idle(const guest_sysv_runtime *runtime)
{
    return runtime && runtime->guest && !runtime->failed && !runtime->retired &&
        !runtime->detached && !runtime->calls && !runtime->loading &&
        qa_native_guest_idle(runtime->guest) && guest_runtime_imports_idle(runtime->imports);
}
guest_runtime_imports *guest_sysv_imports(guest_sysv_runtime *runtime)
{ return runtime ? runtime->imports : NULL; }
qa_native_guest *guest_sysv_guest(const guest_sysv_runtime *runtime)
{ return runtime ? runtime->guest : NULL; }
const guest_sysv_provider *guest_sysv_provider_read(const guest_sysv_runtime *runtime,uint64_t provider)
{
    if (!runtime || !provider) return NULL;
    for (size_t i = 0; i < runtime->image_count; ++i)
        if (runtime->images[i].provider.id == provider) return &runtime->images[i].provider;
    return NULL;
}
bool guest_sysv_provider_tls_read(const guest_sysv_runtime *runtime,uint64_t provider,
    guest_sysv_provider_tls *out)
{
    if (!runtime || !provider || !out) return false;
    guest_sysv_provider_tls receipt = {0};
    for (size_t i = 0; i < runtime->image_count; ++i) {
        const sysv_image *image = runtime->images + i;
        if (image->provider.id == provider) {
            receipt.block = image->tls; *out = receipt; return true;
        }
        if (image->tls.module_id) {
            receipt.prior_used = runtime->thread_pointer - image->tls.address;
            receipt.prior_module = image->tls.module_id;
        }
    }
    return false;
}
uint64_t guest_sysv_thread_pointer(const guest_sysv_runtime *runtime)
{ return runtime ? runtime->thread_pointer : 0; }
bool guest_sysv_retire(guest_sysv_runtime *runtime, qa_error *error)
{
    if (!runtime || runtime->calls || runtime->loading ||
        (runtime->guest && !qa_native_guest_idle(runtime->guest)))
        return sysv_fail(error, QA_ERROR_ARGUMENT, "System V process is still executing or loading");
    if (runtime->retired) return true;
    if (!runtime->failed && !runtime->detached && runtime->guest &&
        !sysv_stdio_retire(runtime,error)) return false;
    if (runtime->imports && !guest_runtime_imports_destroy(&runtime->imports, error)) return false;
    runtime->retired = true; return true;
}
static void image_free(sysv_image *image)
{
    guest_sysv_provider *p = &image->provider;
    free((void *)p->soname);
    for (size_t i = 0; p->exports && i < p->export_count; ++i) {
        free((void *)p->exports[i].name); free((void *)p->exports[i].version);
    }
    for (size_t i = 0; p->needed && i < p->needed_count; ++i) free((void *)p->needed[i]);
    free((void *)p->exports); free((void *)p->needed); free((void *)p->preinitializers);
    free((void *)p->initializers); free((void *)p->finalizers); *image = (sysv_image){0};
}
void guest_sysv_abandon(guest_sysv_runtime **owner)
{
    if (!owner || !*owner) return;
    guest_sysv_runtime *runtime = *owner;
    guest_runtime_imports_abandon(&runtime->imports);
    for (size_t i = 0; runtime->services && i < runtime->service_count; ++i) service_free(runtime->services[i]);
    for (size_t i = 0; runtime->images && i < runtime->image_count; ++i) image_free(runtime->images + i);
    for (size_t i = 0; runtime->unique && i < runtime->unique_count; ++i) free(runtime->unique[i].name);
    for (size_t i = 0; runtime->objects && i < runtime->object_count; ++i) free(runtime->objects[i].name);
    free(runtime->services); free(runtime->heap); free(runtime->images); free(runtime->destructors);
    free(runtime->unique); free(runtime->objects); free(runtime->trace); free(runtime->loading); free(runtime->open_files);
    free(runtime); *owner = NULL;
}

static sysv_image *image_find(guest_sysv_runtime *runtime, uint64_t id)
{
    for (size_t i = 0; i < runtime->image_count; ++i)
        if (runtime->images[i].provider.id == id) return runtime->images + i;
    return NULL;
}
static bool array_copy(const void *source, size_t count, size_t width,
    void **out, qa_error *error)
{
    *out = NULL;
    if (!count) return true;
    if (!source || count > SIZE_MAX / width)
        return sysv_fail(error, QA_ERROR_ARGUMENT, "invalid System V provider array");
    void *copy = calloc(count, width);
    if (!copy) return sysv_fail(error, QA_ERROR_MEMORY, "copying System V provider records");
    memcpy(copy, source, count * width); *out = copy; return true;
}
static bool image_copy(const guest_sysv_provider *source, sysv_image *out, qa_error *error)
{
    *out = (sysv_image){.provider = {.id = source->id, .image = source->image}, .lifecycle = GUEST_SYSV_LOADED};
    guest_sysv_provider *target = &out->provider;
    if (!sysv_copy_text(source->soname, (char **)&target->soname, error)) goto failed;
    if (!array_copy(source->exports, source->export_count, sizeof(*source->exports),
        (void **)&target->exports, error)) goto failed;
    target->export_count = source->export_count;
    for (size_t i = 0; i < target->export_count; ++i) {
        guest_sysv_export *entry = (guest_sysv_export *)target->exports + i;
        entry->name = NULL; entry->version = NULL;
    }
    for (size_t i = 0; i < target->export_count; ++i) {
        guest_sysv_export *entry = (guest_sysv_export *)target->exports + i;
        if (!source->exports[i].name ||
            !sysv_copy_text(source->exports[i].name, (char **)&entry->name, error) ||
            !sysv_copy_text(source->exports[i].version, (char **)&entry->version, error)) goto failed;
    }
    if (!array_copy(source->needed, source->needed_count, sizeof(*source->needed),
        (void **)&target->needed, error)) goto failed;
    target->needed_count = source->needed_count;
    for (size_t i = 0; i < target->needed_count; ++i) ((char **)target->needed)[i] = NULL;
    for (size_t i = 0; i < target->needed_count; ++i)
        if (!source->needed[i] || !sysv_copy_text(source->needed[i], (char **)&target->needed[i], error)) goto failed;
    if (!array_copy(source->preinitializers, source->preinitializer_count, sizeof(uint64_t),
        (void **)&target->preinitializers, error) ||
        !array_copy(source->initializers, source->initializer_count, sizeof(uint64_t),
        (void **)&target->initializers, error) ||
        !array_copy(source->finalizers, source->finalizer_count, sizeof(uint64_t),
        (void **)&target->finalizers, error)) goto failed;
    target->preinitializer_count = source->preinitializer_count;
    target->initializer_count = source->initializer_count;
    target->finalizer_count = source->finalizer_count;
    return true;
failed:
    image_free(out); return false;
}
static bool unsupported_access(guest_sysv_runtime *runtime, bool readable, qa_error *error)
{
    for (size_t i = 0; i < runtime->object_count; ++i) if (runtime->objects[i].kind == 1) {
        if (!qa_native_guest_protect_range(runtime->guest, runtime->objects[i].address,
            4096, readable ? QA_NATIVE_GUEST_READ : 0, error)) return false;
    }
    return true;
}
bool guest_sysv_load_begin(guest_sysv_runtime *runtime, uint64_t provider,
    bool has_tls, uint64_t bytes, uint64_t alignment,
    guest_sysv_load **out, guest_sysv_tls *tls, qa_error *error)
{
    if (!out || *out || !tls || !provider || !guest_sysv_idle(runtime) ||
        !sysv_current(runtime, error) || image_find(runtime, provider))
        return sysv_fail(error, QA_ERROR_ARGUMENT, "System V image load requires its actual idle process and a new provider identity");
    if (!alignment) alignment = 1;
    if (alignment > 4096 || (alignment & (alignment - 1)) || bytes > 0x10000 ||
        (!has_tls && (bytes || alignment != 1))) return sysv_fail(error, QA_ERROR_ARGUMENT, "invalid System V static TLS extent");
    uint64_t used = runtime->tls_used, modules = 0;
    for (size_t i = 0; i < runtime->image_count; ++i) if (runtime->images[i].tls.module_id) ++modules;
    if (modules >= 1024 || used > 0x10000 - bytes)
        return sysv_fail(error, QA_ERROR_MEMORY, "System V static TLS capacity exhausted");
    uint64_t next = (used + bytes + alignment - 1) / alignment * alignment;
    if (next > 0x10000) return sysv_fail(error, QA_ERROR_MEMORY, "System V static TLS alignment exceeds the thread block");
    guest_sysv_load *load = calloc(1, sizeof(*load));
    if (!load) return sysv_fail(error, QA_ERROR_MEMORY, "allocating System V image load lease");
    load->runtime = runtime; load->provider = provider; load->has_tls = has_tls;
    load->next_used = next;
    load->tls = (guest_sysv_tls){.module_id = modules + 1,
        .address = runtime->thread_pointer - next, .bytes = bytes,
        .thread_pointer_offset = -(int64_t)next};
    runtime->loading = load; *out = load; *tls = load->tls;
    if (!unsupported_access(runtime, true, error)) { runtime->failed = true; return false; }
    return true;
}
bool guest_sysv_reload_begin(guest_sysv_runtime *runtime, uint64_t provider,
    guest_sysv_load **out, guest_sysv_tls *tls, qa_error *error)
{
    if (!out || *out || !tls || !guest_sysv_idle(runtime) || !sysv_current(runtime, error))
        return sysv_fail(error, QA_ERROR_ARGUMENT, "System V reload requires its returned process and empty load lease");
    sysv_image *image = image_find(runtime, provider);
    if (!image || image->lifecycle != GUEST_SYSV_FINALIZED)
        return sysv_fail(error, QA_ERROR_ARGUMENT, "System V reload requires the actual finalized module");
    guest_sysv_load *load = calloc(1, sizeof(*load));
    if (!load) return sysv_fail(error, QA_ERROR_MEMORY, "retaining System V module reload lease");
    load->runtime = runtime; load->provider = provider; load->replacing = true;
    load->has_tls = image->tls.module_id != 0; load->tls = image->tls;
    load->next_used = runtime->tls_used;
    runtime->loading = load; *out = load; *tls = load->tls;
    if (!unsupported_access(runtime, true, error)) { runtime->failed = true; return false; }
    return true;
}

bool guest_sysv_load_commit(guest_sysv_load **owner,
    const guest_sysv_provider *provider, qa_bytes tls_template, qa_error *error)
{
    if (!owner || !*owner || !provider)
        return sysv_fail(error, QA_ERROR_ARGUMENT, "System V image commit requires the genuine load lease");
    guest_sysv_load *load = *owner;
    guest_sysv_runtime *runtime = load->runtime;
    if (runtime->loading != load || provider->id != load->provider ||
        !target_valid(&provider->image.target) || !target_equal(&provider->image.target,&runtime->target) ||
        provider->image.format != (runtime->target.pointer_bytes == 4 ? QA_NATIVE_IMAGE_ELF32 : QA_NATIVE_IMAGE_ELF64) ||
        tls_template.size > load->tls.bytes || (tls_template.size && !tls_template.data) ||
        (!load->has_tls && tls_template.size) || !sysv_current(runtime, error))
        return sysv_fail(error, QA_ERROR_ARGUMENT, "System V relocated provider does not match its load lease");
    sysv_image image;
    if (!image_copy(provider, &image, error)) return false;
    for (size_t i = 0; i < provider->export_count; ++i) {
        const guest_sysv_export *entry = provider->exports + i;
        if (entry->bytes > SIZE_MAX || (entry->tls && (!load->has_tls || entry->tls_offset > load->tls.bytes ||
            entry->bytes > load->tls.bytes - entry->tls_offset)) ||
            (!entry->tls && entry->address && !guest_range(runtime->guest, entry->address,
                entry->bytes ? (size_t)entry->bytes : 1, 0, error))) { image_free(&image); return false; }
    }
    if (!guest_grow((void **)&runtime->images, &runtime->image_capacity,
        runtime->image_count + 1, sizeof(*runtime->images), error)) { image_free(&image); return false; }
    if (load->has_tls) {
        image.tls = load->tls;
        if (!sysv_write(runtime, load->tls.address, tls_template.data, tls_template.size, error)) goto failed;
        uint8_t zero[256] = {0};
        for (uint64_t offset = tls_template.size; offset < load->tls.bytes;) {
            size_t count = (size_t)(load->tls.bytes - offset);
            if (count > sizeof(zero)) count = sizeof(zero);
            if (!sysv_write(runtime, load->tls.address + offset, zero, count, error)) goto failed;
            offset += count;
        }
        size_t p = runtime->target.pointer_bytes;
        if (!sysv_put_pointer(runtime, runtime->dtv + load->tls.module_id * 2 * p, load->tls.address, error) ||
            (!load->replacing && !sysv_store(runtime, runtime->dtv, p, load->tls.module_id + 1, error))) goto failed;
        runtime->tls_used = load->next_used;
    }
    if (!unsupported_access(runtime, false, error)) goto failed;
    if (load->replacing) {
        sysv_image *previous = image_find(runtime, load->provider);
        if (!previous || previous->lifecycle != GUEST_SYSV_FINALIZED) goto failed;
        image_free(previous); *previous = image;
    } else runtime->images[runtime->image_count++] = image;
    runtime->loading = NULL; free(load); *owner = NULL; return true;
failed:
    image_free(&image); runtime->failed = true; return false;
}
bool guest_sysv_load_abort(guest_sysv_load **owner, qa_error *error)
{
    if (!owner || !*owner) return true;
    guest_sysv_load *load = *owner;
    if (load->runtime->loading != load || !unsupported_access(load->runtime, false, error)) return false;
    load->runtime->loading = NULL; free(load); *owner = NULL; return true;
}
bool guest_sysv_resolve(guest_sysv_runtime *runtime, uint64_t requester,
    const char *library, const char *name, const char *version,
    bool weak, bool local_definition, bool tls, guest_sysv_symbol *out, qa_error *error)
{
    if (!library || !name || !out || !sysv_current(runtime, error)) return false;
    guest_sysv_symbol result = {0};
    for (size_t i = 0; i < runtime->image_count; ++i) {
        sysv_image *image = runtime->images + i;
        if ((!tls && image->provider.id == requester) ||
            (*library && !sysv_equal_text(library, image->provider.soname))) continue;
        for (size_t j = 0; j < image->provider.export_count; ++j) {
            const guest_sysv_export *entry = image->provider.exports + j;
            if (entry->tls != tls || strcmp(entry->name, name) || !sysv_equal_text(entry->version, version)) continue;
            if (tls && !image->tls.module_id) continue;
            result = (guest_sysv_symbol){.present = true, .provider = image->provider.id,
                .address = entry->address, .bytes = entry->bytes, .size_known = true,
                .tls = tls, .offset = entry->tls_offset, .block = image->tls};
            *out = result; return true;
        }
    }
    if (tls) { *out = result; return true; }
    size_t count = guest_runtime_imports_count(runtime->imports);
    for (size_t i = 0; i < count; ++i) {
        guest_runtime_import_view view;
        if (!guest_runtime_imports_at(runtime->imports, i, &view, error)) return false;
        if (view.key.kind != GUEST_RUNTIME_SYMBOL_NAME || view.key.scope != runtime->options.scope ||
            (*library && strcmp(view.key.library, library)) || strcmp(view.key.name, name) ||
            !sysv_equal_text(view.key.version, version)) continue;
        bool unknown_data = false;
        for (size_t j = 0; j < runtime->object_count; ++j)
            if (runtime->objects[j].kind == 1 && runtime->objects[j].address == view.address) unknown_data = true;
        *out = (guest_sysv_symbol){.present = true, .host = view.kind != GUEST_RUNTIME_IMPORT_DATA,
            .address = view.address, .bytes = view.bytes,
            .size_known = view.kind == GUEST_RUNTIME_IMPORT_DATA && !unknown_data};
        return true;
    }
    if (weak || (!*library && local_definition)) { *out = result; return true; }
    guest_runtime_import_key key = {.scope = runtime->options.scope, .library = library,
        .kind = GUEST_RUNTIME_SYMBOL_NAME, .name = name, .version = version};
    if (!strncmp(name, "_ZTV", 4) || !strncmp(name, "_ZTI", 4)) {
        uint64_t address;
        if (!sysv_allocate(runtime, 4096, false, &address, error) ||
            !sysv_data(runtime, library, name, version, address, 4096, error) ||
            !sysv_object_add(runtime, 1, name, address, 4096, NULL, 0, error) ||
            !qa_native_guest_protect_range(runtime->guest, address, 4096,
                runtime->loading ? QA_NATIVE_GUEST_READ : 0, error)) return false;
        *out = (guest_sysv_symbol){.present = true, .address = address}; return true;
    }
    if (runtime->trap_used > runtime->options.trap_bytes || runtime->options.trap_bytes - runtime->trap_used < 16 ||
        runtime->next_service == UINT64_MAX) return sysv_fail(error, QA_ERROR_MEMORY, "System V unresolved trap capacity exhausted");
    uint64_t address = runtime->options.trap_base + runtime->trap_used, id = runtime->next_service++;
    runtime->trap_used += 16;
    if (!guest_runtime_imports_unresolved(runtime->imports, &key, id, address, error)) { runtime->failed = true; return false; }
    *out = (guest_sysv_symbol){.present = true, .host = true, .address = address}; return true;
}
bool guest_sysv_unique(guest_sysv_runtime *runtime, const char *name,
    uint64_t proposed, uint64_t *out, qa_error *error)
{
    if (!name || !out || !sysv_current(runtime, error)) return false;
    for (size_t i = 0; i < runtime->unique_count; ++i)
        if (!strcmp(runtime->unique[i].name, name)) { *out = runtime->unique[i].address; return true; }
    if (!proposed) { *out = 0; return true; }
    if (!guest_range(runtime->guest, proposed, 1, 0, error) ||
        !guest_grow((void **)&runtime->unique, &runtime->unique_capacity,
            runtime->unique_count + 1, sizeof(*runtime->unique), error)) return false;
    sysv_unique entry = {.address = proposed};
    if (!sysv_copy_text(name, &entry.name, error)) return false;
    runtime->unique[runtime->unique_count++] = entry; *out = proposed; return true;
}
bool guest_sysv_tls_address(guest_sysv_runtime *runtime, uint64_t module,
    uint64_t offset, uint64_t *out, qa_error *error)
{
    if (!out || !sysv_current(runtime, error)) return false;
    const guest_sysv_tls *block = NULL;
    for (size_t i = 0; i < runtime->image_count; ++i)
        if (runtime->images[i].tls.module_id == module && module) { block = &runtime->images[i].tls; break; }
    if (!block || offset >= block->bytes) return sysv_fail(error, QA_ERROR_ARGUMENT, "System V TLS index is outside its actual module block");
    uint64_t dtv, capacity, address;
    size_t p = runtime->target.pointer_bytes;
    if (!sysv_pointer(runtime, runtime->thread_pointer + p, &dtv, error) || !dtv || dtv < 2 * p ||
        !sysv_unsigned(runtime, dtv - 2 * p, p, &capacity, error) || module > capacity ||
        module > (UINT64_MAX - dtv) / (2 * p) ||
        !sysv_pointer(runtime, dtv + module * 2 * p, &address, error) || !address || address > UINT64_MAX - offset)
        return sysv_fail(error, QA_ERROR_ARGUMENT, "System V guest DTV has no allocated requested module");
    *out = address + offset; return true;
}
static bool trace_add(guest_sysv_runtime *runtime, uint64_t provider,
    uint64_t target, bool finalize, qa_error *error)
{
    if (!guest_grow((void **)&runtime->trace, &runtime->trace_capacity,
        runtime->trace_count + 1, sizeof(*runtime->trace), error)) return false;
    runtime->trace[runtime->trace_count++] = (guest_sysv_trace){provider, target, finalize}; return true;
}
static bool initialize_targets(guest_sysv_runtime *runtime, uint64_t id,
    bool pre, qa_error *error)
{
    sysv_image *image = image_find(runtime, id);
    size_t count = pre ? image->provider.preinitializer_count : image->provider.initializer_count;
    const qa_native_value_type types[] = {QA_NATIVE_I32, QA_NATIVE_ADDRESS, QA_NATIVE_ADDRESS};
    qa_native_value arguments[] = {{.type = QA_NATIVE_I32, .as.i32 = (int32_t)runtime->options.argc},
        {.type = QA_NATIVE_ADDRESS, .as.address = runtime->argv},
        {.type = QA_NATIVE_ADDRESS, .as.address = runtime->envp}};
    for (size_t i = 0; i < count; ++i) {
        image = image_find(runtime, id);
        uint64_t target = pre ? image->provider.preinitializers[i] : image->provider.initializers[i];
        qa_native_value result = {.type = QA_NATIVE_VOID};
        if (!sysv_invoke(runtime, target, types, 3, QA_NATIVE_VOID, arguments, &result, error) ||
            !trace_add(runtime, id, target, false, error)) return false;
    }
    return true;
}
bool guest_sysv_initialize(guest_sysv_runtime *runtime, uint64_t id,
    size_t budget, qa_error *error)
{
    if (!sysv_current(runtime, error)) return false;
    if (!execution_valid(runtime->execution, &runtime->target, budget) || runtime->loading)
        return sysv_fail(error, QA_ERROR_ARGUMENT, "System V initialization requires its actual backend call policy");
    sysv_image *image = image_find(runtime, id);
    if (!image) return sysv_fail(error, QA_ERROR_ARGUMENT, "System V initializer image is not loaded");
    if (image->lifecycle == GUEST_SYSV_INITIALIZED || image->lifecycle == GUEST_SYSV_INITIALIZING) return true;
    if (image->lifecycle != GUEST_SYSV_LOADED) return sysv_fail(error, QA_ERROR_ARGUMENT, "System V image cannot initialize from its reached state");
    runtime->budget = budget; image->lifecycle = GUEST_SYSV_INITIALIZING;
    if (!initialize_targets(runtime, id, true, error)) goto failed;
    image = image_find(runtime, id);
    size_t count = image->provider.needed_count;
    for (size_t i = 0; i < count; ++i) {
        image = image_find(runtime, id);
        uint64_t dependency = 0;
        for (size_t j = 0; j < runtime->image_count; ++j)
            if (sysv_equal_text(runtime->images[j].provider.soname, image->provider.needed[i])) {
                dependency = runtime->images[j].provider.id; break;
            }
        if (dependency && !guest_sysv_initialize(runtime, dependency, budget, error)) goto failed;
    }
    if (!initialize_targets(runtime, id, false, error)) goto failed;
    image_find(runtime, id)->lifecycle = GUEST_SYSV_INITIALIZED; return true;
failed:
    image_find(runtime, id)->lifecycle = GUEST_SYSV_FAILED; return false;
}
bool guest_sysv_finalize(guest_sysv_runtime *runtime, uint64_t id,
    size_t budget, qa_error *error)
{
    if (!sysv_current(runtime, error)) return false;
    if (!execution_valid(runtime->execution, &runtime->target, budget) || runtime->loading)
        return sysv_fail(error, QA_ERROR_ARGUMENT, "System V finalization requires its actual backend call policy");
    sysv_image *image = image_find(runtime, id);
    if (!image) return sysv_fail(error, QA_ERROR_ARGUMENT, "System V finalizer image is not loaded");
    if (image->lifecycle == GUEST_SYSV_FINALIZED) return true;
    if (image->lifecycle != GUEST_SYSV_INITIALIZED) return sysv_fail(error, QA_ERROR_ARGUMENT, "Only an initialized System V image can finalize");
    runtime->budget = budget; image->lifecycle = GUEST_SYSV_FINALIZING;
    size_t count = image->provider.finalizer_count;
    for (size_t i = 0; i < count; ++i) {
        uint64_t target = image_find(runtime, id)->provider.finalizers[i];
        qa_native_value result = {.type = QA_NATIVE_VOID};
        if (!sysv_invoke(runtime, target, NULL, 0, QA_NATIVE_VOID, NULL, &result, error) ||
            !trace_add(runtime, id, target, true, error)) { image_find(runtime, id)->lifecycle = GUEST_SYSV_FAILED; return false; }
    }
    image_find(runtime, id)->lifecycle = GUEST_SYSV_FINALIZED; return true;
}
bool guest_sysv_destructor(guest_sysv_runtime *runtime, uint64_t target,
    uint64_t argument, uint64_t dso, qa_error *error)
{
    if (!target || (runtime && runtime->target.pointer_bytes == 4 && target > UINT32_MAX) ||
        !sysv_current(runtime, error) ||
        !guest_range(runtime->guest,target,1,QA_NATIVE_GUEST_EXECUTE,error) ||
        !guest_grow((void **)&runtime->destructors, &runtime->destructor_capacity,
            runtime->destructor_count + 1, sizeof(*runtime->destructors), error)) return false;
    runtime->destructors[runtime->destructor_count++] = (sysv_destructor){target, argument, dso, false}; return true;
}
bool guest_sysv_finalize_destructors(guest_sysv_runtime *runtime, uint64_t dso, qa_error *error)
{
    if (!sysv_current(runtime, error)) return false;
    for (;;) {
        size_t index = runtime->destructor_count;
        while (index) {
            sysv_destructor *entry = runtime->destructors + --index;
            if (!entry->called && (!dso || entry->dso == dso)) break;
        }
        if (!runtime->destructor_count || runtime->destructors[index].called ||
            (dso && runtime->destructors[index].dso != dso)) return true;
        sysv_destructor entry = runtime->destructors[index];
        runtime->destructors[index].called = true;
        const qa_native_value_type type = QA_NATIVE_ADDRESS;
        qa_native_value argument = {.type = type, .as.address = entry.argument}, result = {.type = QA_NATIVE_VOID};
        if (!sysv_invoke(runtime, entry.target, &type, 1, QA_NATIVE_VOID, &argument, &result, error)) return false;
    }
}
bool guest_sysv_finalize_image_destructors(guest_sysv_runtime *runtime, uint64_t provider,
    uint64_t base, uint64_t bytes, qa_error *error)
{
    if (!bytes || base > UINT64_MAX - bytes || !guest_sysv_idle(runtime) || !sysv_current(runtime, error))
        return sysv_fail(error, QA_ERROR_ARGUMENT, "System V module destructors require the actual stopped image range");
    const sysv_image *image = image_find(runtime, provider);
    if (!image || image->lifecycle != GUEST_SYSV_FINALIZED)
        return sysv_fail(error, QA_ERROR_ARGUMENT, "System V module finalizers have not returned");
    uint64_t end = base + bytes;
    for (;;) {
        size_t index = runtime->destructor_count;
        while (index) {
            const sysv_destructor *entry = runtime->destructors + --index;
            if (!entry->called && ((entry->target >= base && entry->target < end) ||
                (entry->dso >= base && entry->dso < end))) break;
        }
        if (!runtime->destructor_count) break;
        sysv_destructor entry = runtime->destructors[index];
        if (entry.called || !((entry.target >= base && entry.target < end) ||
            (entry.dso >= base && entry.dso < end))) break;
        runtime->destructors[index].called = true;
        const qa_native_value_type type = QA_NATIVE_ADDRESS;
        qa_native_value argument = {.type = type, .as.address = entry.argument};
        qa_native_value result = {.type = QA_NATIVE_VOID};
        if (!sysv_invoke(runtime, entry.target, &type, 1, QA_NATIVE_VOID, &argument, &result, error)) return false;
    }
    for (size_t i = runtime->destructor_count; i; --i) {
        sysv_destructor entry = runtime->destructors[i - 1];
        if ((entry.target >= base && entry.target < end) || (entry.dso >= base && entry.dso < end)) {
            memmove(runtime->destructors + i - 1, runtime->destructors + i,
                (runtime->destructor_count - i) * sizeof(*runtime->destructors));
            --runtime->destructor_count;
        }
    }
    for (size_t i = runtime->unique_count; i; --i)
        if (runtime->unique[i - 1].address >= base && runtime->unique[i - 1].address < end) {
            free(runtime->unique[i - 1].name);
            memmove(runtime->unique + i - 1, runtime->unique + i,
                (runtime->unique_count - i) * sizeof(*runtime->unique));
            --runtime->unique_count;
        }
    return true;
}

size_t guest_sysv_trace_count(const guest_sysv_runtime *runtime)
{ return runtime ? runtime->trace_count : 0; }
bool guest_sysv_finalize_all(guest_sysv_runtime *runtime, size_t budget, qa_error *error)
{
    if (!sysv_current(runtime, error)) return false;
    if (!execution_valid(runtime->execution, &runtime->target, budget) || runtime->loading)
        return sysv_fail(error, QA_ERROR_ARGUMENT, "System V unload requires its actual backend call policy");
    runtime->budget = budget;
    if (!guest_sysv_finalize_destructors(runtime, 0, error)) return false;
    for (size_t i = runtime->image_count; i; --i) {
        sysv_image *image = runtime->images + i - 1;
        if (image->lifecycle == GUEST_SYSV_LOADED || image->lifecycle == GUEST_SYSV_FINALIZED) continue;
        uint64_t id = image->provider.id;
        if (!guest_sysv_finalize(runtime, id, budget, error)) return false;
    }
    return guest_sysv_finalize_destructors(runtime, 0, error);
}
bool guest_sysv_trace_at(const guest_sysv_runtime *runtime, size_t index,
    guest_sysv_trace *out, qa_error *error)
{
    if (!runtime || !out || index >= runtime->trace_count)
        return sysv_fail(error, QA_ERROR_ARGUMENT, "System V lifecycle trace ordinal is absent");
    *out = runtime->trace[index]; return true;
}

static bool codec_text(qa_source_save_io *io,char **text)
{
    bool present = io->direction == QA_SOURCE_SAVE_WRITE && *text;
    if (!qa_source_save_bool(io,&present)) return false;
    if (!present) { if (io->direction == QA_SOURCE_SAVE_READ) *text = NULL; return true; }
    size_t bytes = io->direction == QA_SOURCE_SAVE_WRITE ? strlen(*text) : 0;
    if (!qa_source_save_count(io,&bytes,SYSV_MAX_STRING)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        if (io->offset > io->input.size || bytes > io->input.size-io->offset)
            return sysv_fail(io->error,QA_ERROR_FORMAT,"truncated System V owned text");
        *text = malloc(bytes+1);
        if (!*text) return sysv_fail(io->error,QA_ERROR_MEMORY,"decoding System V owned text");
        (*text)[bytes] = 0;
    }
    if (!qa_source_save_bytes(io,*text,bytes)) return false;
    return !memchr(*text,0,bytes) || sysv_fail(io->error,QA_ERROR_FORMAT,"embedded NUL in System V identity");
}
static bool codec_target(qa_source_save_io *io,qa_native_target *target)
{
    uint32_t os = target->os, arch = target->arch, abi = target->abi;
    if (!qa_source_save_u32(io,&os) || !qa_source_save_u32(io,&arch) ||
        !qa_source_save_u32(io,&abi) || !qa_source_save_u8(io,&target->pointer_bytes)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        target->os = (qa_native_os)os; target->arch = (qa_native_arch)arch; target->abi = (qa_native_abi)abi;
    }
    return target_valid(target) || sysv_fail(io->error,QA_ERROR_FORMAT,"invalid System V saved target");
}
static bool codec_image_identity(qa_source_save_io *io,qa_native_image_info *image)
{
    uint32_t format = image->format;
    if (!qa_source_save_u32(io,&format) || !codec_target(io,&image->target) ||
        !qa_source_save_u64(io,&image->preferred_base) || !qa_source_save_u64(io,&image->image_bytes) ||
        !qa_source_save_bytes(io,&image->digest,sizeof(image->digest))) return false;
    if (format != QA_NATIVE_IMAGE_ELF32 && format != QA_NATIVE_IMAGE_ELF64)
        return sysv_fail(io->error,QA_ERROR_FORMAT,"System V provider is not an ELF image");
    image->format = (qa_native_image_format)format;
    return (format == QA_NATIVE_IMAGE_ELF32) == (image->target.pointer_bytes == 4) ||
        sysv_fail(io->error,QA_ERROR_FORMAT,"System V ELF class and ABI disagree");
}
static bool codec_array(qa_source_save_io *io,void **array,size_t *count,
    size_t *capacity,size_t width)
{
    size_t maximum = SYSV_MAX_ALLOCATION/width;
    if (!qa_source_save_count(io,count,maximum)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        if (*count && (*count > io->input.size-io->offset))
            return sysv_fail(io->error,QA_ERROR_FORMAT,"System V record count exceeds complete envelope");
        if (*count) {
            *array = calloc(*count,width);
            if (!*array) return sysv_fail(io->error,QA_ERROR_MEMORY,"allocating detached System V records");
        }
        if (capacity) *capacity = *count;
    }
    return true;
}
static bool codec_addresses(qa_source_save_io *io,const uint64_t **array,size_t *count)
{
    if (!codec_array(io,(void **)array,count,NULL,sizeof(**array))) return false;
    for (size_t i = 0; i < *count; ++i) if (!qa_source_save_u64(io,(uint64_t *)*array+i)) return false;
    return true;
}
static bool codec_layout(qa_source_save_io *io,guest_sysv_runtime *r,guest_abi_layout *layout)
{
    uint32_t kind = layout->kind;
    if (!qa_source_save_u32(io,&kind) || kind > QA_NATIVE_ADDRESS) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) *layout = sysv_layout(r,(qa_native_value_type)kind);
    return true;
}
static bool runtime_fields(qa_source_save_io *io,guest_sysv_runtime *r)
{
    bool read = io->direction == QA_SOURCE_SAVE_READ;
    uint8_t magic[] = {'Q','S','V','R',3};
    uint8_t expected[sizeof(magic)]; memcpy(expected,magic,sizeof(magic));
    if (!qa_source_save_bytes(io,magic,sizeof(magic)) || memcmp(magic,expected,sizeof(magic)) ||
        !codec_target(io,&r->target)) return false;
    uint32_t execution = r->execution;
    if (!qa_source_save_u32(io,&execution) || execution > QA_NATIVE_GUEST_HOST_X86_64) return false;
    r->execution = (qa_native_guest_backend)execution;
    uint64_t *scalars[] = {&r->options.scope,&r->options.first_function,&r->options.trap_base,&r->options.return_trap,
        &r->thread_area,&r->thread_pointer,&r->dtv_base,&r->dtv,&r->errno_address,
        &r->argv,&r->envp,&r->empty_string,&r->strtok_slot,&r->tls_used,&r->next_service,&r->trap_used,
        &r->classic_locale,&r->iostream_refcount,&r->iostream_sync};
    for (size_t i = 0; i < sizeof(scalars)/sizeof(*scalars); ++i)
        if (!qa_source_save_u64(io,scalars[i])) return false;
    if (!qa_source_save_count(io,&r->options.trap_bytes,SYSV_MAX_ALLOCATION) ||
        !qa_source_save_count(io,&r->options.instruction_budget,SIZE_MAX) ||
        !qa_source_save_count(io,&r->options.argc,INT32_MAX) ||
        !qa_source_save_count(io,&r->options.environment_count,SYSV_MAX_ALLOCATION) ||
        !qa_source_save_count(io,&r->budget,SIZE_MAX) ||
        !qa_source_save_u64(io,&r->options.bindings.clock_id)) return false;
    bool clock = r->options.bindings.time != NULL;
    if (!qa_source_save_bool(io,&clock) ||
        !qa_source_save_bool(io,&r->options.bindings.output_is_terminal)) return false;
    if (read) r->clock_present = clock;
    /* Callback shapes are identities, not machine addresses. Detached decode
     * compares them with genuine already-acquired binding capabilities. */
    for (size_t i = 0; i < 3; ++i) {
        bool input = r->options.bindings.streams[i].read != NULL;
        bool output = r->options.bindings.streams[i].write != NULL;
        bool flush = r->options.bindings.streams[i].flush != NULL;
        if (!qa_source_save_u64(io,&r->options.bindings.streams[i].id) ||
            !qa_source_save_bool(io,&input) || !qa_source_save_bool(io,&output) ||
            !qa_source_save_bool(io,&flush) || !qa_source_save_u64(io,r->files+i) ||
            !qa_source_save_u64(io,r->wide_files+i)) return false;
        if (read) r->stream_shapes[i] = (uint8_t)((input ? 1 : 0)|(output ? 2 : 0)|(flush ? 4 : 0));
    }
    if (!sysv_stdio_fields(io,r)) return false;
    if (!codec_array(io,(void **)&r->heap,&r->heap_count,&r->heap_capacity,sizeof(*r->heap))) return false;
    for (size_t i = 0; i < r->heap_count; ++i)
        if (!qa_source_save_u64(io,&r->heap[i].address) || !qa_source_save_u64(io,&r->heap[i].bytes)) return false;
    if (!codec_array(io,(void **)&r->services,&r->service_count,&r->service_capacity,sizeof(*r->services))) return false;
    for (size_t i = 0; i < r->service_count; ++i) {
        if (read) {
            r->services[i] = calloc(1,sizeof(*r->services[i]));
            if (!r->services[i]) return sysv_fail(io->error,QA_ERROR_MEMORY,"decoding stable System V service descriptor");
            r->services[i]->runtime = r;
        }
        sysv_service *s = r->services[i]; uint32_t group = s->group;
        if (!qa_source_save_u64(io,&s->id) || !qa_source_save_u64(io,&s->address) ||
            !qa_source_save_u64(io,&s->a) || !qa_source_save_u64(io,&s->b) || !qa_source_save_u64(io,&s->c) ||
            !qa_source_save_u32(io,&s->operation) || !qa_source_save_u32(io,&group) || group > SYSV_FORMAT ||
            !codec_text(io,&s->library) || !codec_text(io,&s->name) || !codec_text(io,&s->version) ||
            !codec_text(io,&s->detail) || !qa_source_save_count(io,&s->parameter_count,8)) return false;
        s->group = (sysv_group)group;
        for (size_t j = 0; j < s->parameter_count; ++j) if (!codec_layout(io,r,s->parameters+j)) return false;
        if (!codec_layout(io,r,&s->result)) return false;
    }
    if (!codec_array(io,(void **)&r->images,&r->image_count,&r->image_capacity,sizeof(*r->images))) return false;
    for (size_t i = 0; i < r->image_count; ++i) {
        sysv_image *image = r->images+i; guest_sysv_provider *p = &image->provider;
        uint32_t lifecycle = image->lifecycle;
        if (!qa_source_save_u64(io,&p->id) || !codec_image_identity(io,&p->image) ||
            !codec_text(io,(char **)&p->soname) || !qa_source_save_u32(io,&lifecycle) || lifecycle > GUEST_SYSV_FAILED ||
            !qa_source_save_u64(io,&image->tls.module_id) || !qa_source_save_u64(io,&image->tls.address) ||
            !qa_source_save_u64(io,&image->tls.bytes) || !qa_source_save_i64(io,&image->tls.thread_pointer_offset)) return false;
        image->lifecycle = (guest_sysv_lifecycle)lifecycle;
        if (!codec_array(io,(void **)&p->exports,&p->export_count,NULL,sizeof(*p->exports))) return false;
        for (size_t j = 0; j < p->export_count; ++j) {
            guest_sysv_export *e = (guest_sysv_export *)p->exports+j;
            if (!codec_text(io,(char **)&e->name) || !codec_text(io,(char **)&e->version) ||
                !qa_source_save_u64(io,&e->address) || !qa_source_save_u64(io,&e->bytes) ||
                !qa_source_save_u64(io,&e->tls_offset) || !qa_source_save_bool(io,&e->tls)) return false;
        }
        if (!codec_array(io,(void **)&p->needed,&p->needed_count,NULL,sizeof(*p->needed))) return false;
        for (size_t j = 0; j < p->needed_count; ++j) if (!codec_text(io,(char **)&p->needed[j])) return false;
        if (!codec_addresses(io,&p->preinitializers,&p->preinitializer_count) ||
            !codec_addresses(io,&p->initializers,&p->initializer_count) ||
            !codec_addresses(io,&p->finalizers,&p->finalizer_count)) return false;
    }
    if (!codec_array(io,(void **)&r->destructors,&r->destructor_count,&r->destructor_capacity,sizeof(*r->destructors))) return false;
    for (size_t i = 0; i < r->destructor_count; ++i) {
        sysv_destructor *d = r->destructors+i;
        if (!qa_source_save_u64(io,&d->target) || !qa_source_save_u64(io,&d->argument) ||
            !qa_source_save_u64(io,&d->dso) || !qa_source_save_bool(io,&d->called)) return false;
    }
    if (!codec_array(io,(void **)&r->unique,&r->unique_count,&r->unique_capacity,sizeof(*r->unique))) return false;
    for (size_t i = 0; i < r->unique_count; ++i)
        if (!codec_text(io,&r->unique[i].name) || !qa_source_save_u64(io,&r->unique[i].address)) return false;
    bool objects_ok = codec_array(io,(void **)&r->objects,&r->object_count,&r->object_capacity,sizeof(*r->objects));
    if (!objects_ok) return false;
    for (size_t i = 0; i < r->object_count; ++i) {
        sysv_object *object = r->objects+i;
        if (!qa_source_save_u32(io,&object->kind) || !codec_text(io,&object->name) ||
            !qa_source_save_u64(io,&object->address) || !qa_source_save_u64(io,&object->bytes)) { objects_ok = false; break; }
        for (size_t j = 0; j < 8; ++j) if (!qa_source_save_u64(io,object->values+j)) { objects_ok = false; break; }
        if (!objects_ok) break;
    }
    if (!objects_ok || !codec_array(io,(void **)&r->trace,&r->trace_count,&r->trace_capacity,sizeof(*r->trace))) return false;
    for (size_t i = 0; i < r->trace_count; ++i)
        if (!qa_source_save_u64(io,&r->trace[i].provider) || !qa_source_save_u64(io,&r->trace[i].target) ||
            !qa_source_save_bool(io,&r->trace[i].finalize)) return false;
    return true;
}
static bool runtime_valid(guest_sysv_runtime *r,qa_error *error)
{
    size_t p = r->target.pointer_bytes;
    if (!target_valid(&r->target) || !r->options.scope || !r->options.first_function ||
        !execution_valid(r->execution,&r->target,r->options.instruction_budget) ||
        !execution_valid(r->execution,&r->target,r->budget) ||
        !r->thread_area || r->thread_area > UINT64_MAX-0x11000 || r->thread_pointer != r->thread_area+0x10000 ||
        !r->dtv_base || r->dtv_base > UINT64_MAX-1026*2*p || r->dtv != r->dtv_base+2*p ||
        !r->errno_address || !r->argv || !r->envp || !r->empty_string || !r->strtok_slot ||
        !r->classic_locale || !r->iostream_refcount || !r->iostream_sync ||
        r->tls_used > 0x10000 || !r->next_service || r->trap_used > r->options.trap_bytes ||
        r->trap_used%16 || r->options.trap_base%4096 || r->options.trap_bytes%4096 ||
        !r->options.trap_bytes || r->options.trap_base > UINT64_MAX-r->options.trap_bytes ||
        !r->options.return_trap || r->next_service < r->options.first_function ||
        r->options.argc > SYSV_MAX_ALLOCATION/p-1 || r->options.environment_count > SYSV_MAX_ALLOCATION/p-1)
        return sysv_fail(error,QA_ERROR_FORMAT,"invalid System V process allocation identities");
    uint64_t modules = 0, used = 0;
    for (size_t i = 0; i < r->image_count; ++i) {
        sysv_image *image = r->images+i;
        if (!image->provider.id || !target_equal(&image->provider.image.target,&r->target) ||
            image->lifecycle == GUEST_SYSV_INITIALIZING || image->lifecycle == GUEST_SYSV_FINALIZING)
            return sysv_fail(error,QA_ERROR_FORMAT,"invalid System V provider continuation");
        for (size_t j = 0; j < i; ++j) if (r->images[j].provider.id == image->provider.id)
            return sysv_fail(error,QA_ERROR_FORMAT,"duplicate System V provider identity");
        if (image->tls.module_id) {
            guest_sysv_tls *tls = &image->tls;
            if (tls->module_id != ++modules || modules > 1024 || tls->bytes > 0x10000 ||
                tls->thread_pointer_offset > 0 || tls->thread_pointer_offset < -0x10000 ||
                tls->address != r->thread_pointer-(uint64_t)(-tls->thread_pointer_offset) ||
                (uint64_t)(-tls->thread_pointer_offset) < used+tls->bytes)
                return sysv_fail(error,QA_ERROR_FORMAT,"invalid System V TLS module extent or order");
            used = (uint64_t)(-tls->thread_pointer_offset);
        } else if (image->tls.address || image->tls.bytes || image->tls.thread_pointer_offset)
            return sysv_fail(error,QA_ERROR_FORMAT,"unassigned System V TLS block has payload");
        for (size_t j = 0; j < image->provider.export_count; ++j) {
            const guest_sysv_export *e = image->provider.exports+j;
            if (!e->name || (e->tls && (!image->tls.module_id || e->tls_offset > image->tls.bytes ||
                e->bytes > image->tls.bytes-e->tls_offset))) return sysv_fail(error,QA_ERROR_FORMAT,"invalid System V export identity");
        }
        for (size_t j = 0; j < image->provider.needed_count; ++j)
            if (!image->provider.needed[j]) return sysv_fail(error,QA_ERROR_FORMAT,"absent System V dependency name");
    }
    if (used != r->tls_used) return sysv_fail(error,QA_ERROR_FORMAT,"System V TLS allocation cursor differs from ordered blocks");
    for (size_t i = 0; i < r->heap_count; ++i) {
        sysv_heap *h = r->heap+i;
        if (!h->address || !h->bytes || h->bytes > SYSV_MAX_ALLOCATION || h->address > UINT64_MAX-h->bytes)
            return sysv_fail(error,QA_ERROR_FORMAT,"invalid System V live heap extent");
        for (size_t j = 0; j < i; ++j) if (r->heap[j].address == h->address)
            return sysv_fail(error,QA_ERROR_FORMAT,"duplicate System V live heap base");
    }
    for (size_t i = 0; i < r->service_count; ++i) {
        sysv_service *s = r->services[i];
        if (!s || s->id < r->options.first_function || s->id >= r->next_service || !s->library || !s->name ||
            s->address < r->options.trap_base || s->address-r->options.trap_base >= r->trap_used ||
            (s->address-r->options.trap_base)%16 || s->parameter_count > 8)
            return sysv_fail(error,QA_ERROR_FORMAT,"invalid System V service descriptor identity");
        for (size_t j = 0; j < i; ++j) if (r->services[j]->id == s->id || r->services[j]->address == s->address)
            return sysv_fail(error,QA_ERROR_FORMAT,"duplicate System V service descriptor");
        if (!sysv_service_valid(s,error)) return false;
        for (size_t j = 0; j < s->parameter_count; ++j)
            if (s->parameters[j].kind == QA_NATIVE_VOID || s->parameters[j].kind >= QA_NATIVE_BYTES)
                return sysv_fail(error,QA_ERROR_FORMAT,"invalid System V service parameter layout");
    }
    for (size_t i = 0; i < 3; ++i) {
        if (!r->files[i] || !r->wide_files[i])
            return sysv_fail(error,QA_ERROR_FORMAT,"absent System V standard FILE identity");
        for (size_t j = 0; j < i; ++j) if (r->files[j] == r->files[i] || r->wide_files[j] == r->wide_files[i])
            return sysv_fail(error,QA_ERROR_FORMAT,"duplicate System V standard FILE identity");
    }
    size_t streams = 0,classic = 0;
    for (size_t i = 0; i < r->destructor_count; ++i)
        if (!r->destructors[i].target || (p == 4 && (r->destructors[i].target > UINT32_MAX ||
            r->destructors[i].argument > UINT32_MAX || r->destructors[i].dso > UINT32_MAX)))
            return sysv_fail(error,QA_ERROR_FORMAT,"invalid System V destructor target or argument identity");
    for (size_t i = 0; i < r->object_count; ++i) {
        sysv_object *o = r->objects+i;
        if (!o->name || !o->address || !o->bytes || o->address > UINT64_MAX-o->bytes ||
            (p == 4 && o->address+o->bytes > UINT64_C(0x100000000)))
            return sysv_fail(error,QA_ERROR_FORMAT,"invalid System V retained object identity");
        bool valid;
        switch (o->kind) {
        case 1: valid = o->bytes == 4096 && o->address%4096 == 0; break;
        case 10: valid = o->bytes == 2*p || o->bytes == 3*p ||
            (o->bytes >= 2*p+8 && (o->bytes-2*p-8)%(2*p) == 0); break;
        case 11: valid = o->bytes == 9*p && (!strcmp(o->name,"class") || !strcmp(o->name,"si") || !strcmp(o->name,"vmi")); break;
        case 12: valid = o->bytes%p == 0; break;
        case 20: valid = !strcmp(o->name,"classic") && o->address == r->classic_locale && o->bytes == 5*p;
            for (size_t j = 0; j < 7 && valid; ++j) valid = o->values[j] != 0;
            valid = valid && !o->values[7]; ++classic; break;
        case 30: {
            const char *names[] = {"cin","cout","cerr","clog","wcin","wcout","wcerr","wclog"};
            size_t index = 8; for (size_t j = 0; j < 8; ++j) if (!strcmp(o->name,names[j])) { index = j; break; }
            bool wide = index >= 4, input = index%4 == 0;
            size_t prefix = (input ? 2 : 1)*p,bytes = prefix+(p == 8 ? 264 : wide ? 140 : 136);
            valid = index < 8 && o->bytes == bytes && o->values[0] == o->address+prefix &&
                o->values[1] == wide && o->values[2] == input;
            for (size_t j = 3; j < 8 && valid; ++j) valid = !o->values[j];
            ++streams; break;
        }
        default: valid = false; break;
        }
        if (o->kind < 20) for (size_t j = 0; j < 8 && valid; ++j) valid = !o->values[j];
        if (!valid) return sysv_fail(error,QA_ERROR_FORMAT,"saved System V object differs from its producer shape");
        for (size_t j = 0; j < i; ++j)
            if (o->address == r->objects[j].address || (o->kind != 1 && o->kind == r->objects[j].kind && !strcmp(o->name,r->objects[j].name)))
                return sysv_fail(error,QA_ERROR_FORMAT,"duplicate System V retained object identity");
    }
    if (streams != 8 || classic != 1)
        return sysv_fail(error,QA_ERROR_FORMAT,"System V process lost its constructed standard stream or locale inventory");
    for (size_t i = 0; i < r->unique_count; ++i) {
        if (!r->unique[i].name || !r->unique[i].address) return sysv_fail(error,QA_ERROR_FORMAT,"invalid System V GNU unique identity");
        for (size_t j = 0; j < i; ++j) if (!strcmp(r->unique[i].name,r->unique[j].name))
            return sysv_fail(error,QA_ERROR_FORMAT,"duplicate System V GNU unique name");
    }
    for (size_t i = 0; i < r->trace_count; ++i)
        if (!image_find(r,r->trace[i].provider) || !r->trace[i].target)
            return sysv_fail(error,QA_ERROR_FORMAT,"invalid System V lifecycle trace identity");
    return sysv_stdio_valid(r,error);
}
static bool descriptor_resolve(void *context,uint64_t id,guest_runtime_function *out,qa_error *error)
{
    guest_sysv_runtime *r = context;
    for (size_t i = 0; i < r->service_count; ++i) if (r->services[i]->id == id) {
        *out = service_descriptor(r->services[i]); return true;
    }
    return sysv_fail(error,QA_ERROR_FORMAT,"saved System V import has no immutable service descriptor");
}
static bool imports_valid(guest_sysv_runtime *r,qa_error *error)
{
    size_t count = guest_runtime_imports_count(r->imports), functions = 0;
    for (size_t i = 0; i < count; ++i) {
        guest_runtime_import_view view;
        if (!guest_runtime_imports_at(r->imports,i,&view,error)) return false;
        if (view.key.scope != r->options.scope || view.key.kind != GUEST_RUNTIME_SYMBOL_NAME)
            return sysv_fail(error,QA_ERROR_FORMAT,"saved System V import has a foreign namespace");
        if (view.kind == GUEST_RUNTIME_IMPORT_DATA) continue;
        if (view.id < r->options.first_function || view.id >= r->next_service ||
            view.address < r->options.trap_base || view.address-r->options.trap_base >= r->trap_used ||
            (view.address-r->options.trap_base)%16)
            return sysv_fail(error,QA_ERROR_FORMAT,"saved System V import differs from its callback allocation cursor");
        bool first = true;
        for (size_t j = 0; j < i; ++j) {
            guest_runtime_import_view previous;
            if (!guest_runtime_imports_at(r->imports,j,&previous,error)) return false;
            if (previous.kind != GUEST_RUNTIME_IMPORT_DATA && previous.id == view.id) { first = false; break; }
        }
        if (first) ++functions;
        if (view.kind == GUEST_RUNTIME_IMPORT_FUNCTION) {
            sysv_service *service = NULL;
            for (size_t j = 0; j < r->service_count; ++j) if (r->services[j]->id == view.id) { service = r->services[j]; break; }
            if (!service || service->address != view.address)
                return sysv_fail(error,QA_ERROR_FORMAT,"saved System V bound import lacks its actual service record");
        } else {
            for (size_t j = 0; j < r->service_count; ++j) if (r->services[j]->id == view.id)
                return sysv_fail(error,QA_ERROR_FORMAT,"saved System V raw unresolved import fabricated a service signature");
        }
    }
    if (functions != r->trap_used/16 || (uint64_t)functions != r->next_service-r->options.first_function)
        return sysv_fail(error,QA_ERROR_FORMAT,"saved System V callback cursor has missing or invented trap records");
    for (size_t i = 0; i < r->service_count; ++i) {
        sysv_service *s = r->services[i]; bool canonical = false;
        for (size_t j = 0; j < count; ++j) {
            guest_runtime_import_view view;
            if (!guest_runtime_imports_at(r->imports,j,&view,error)) return false;
            if (view.kind == GUEST_RUNTIME_IMPORT_FUNCTION && view.id == s->id &&
                !strcmp(view.key.library,s->library) && !strcmp(view.key.name,s->name) &&
                sysv_equal_text(view.key.version,s->version)) { canonical = true; break; }
        }
        if (!canonical) return sysv_fail(error,QA_ERROR_FORMAT,"saved System V service has no original exact symbol key");
    }
    return true;
}
static bool owned_allocation(const qa_native_guest *guest,uint64_t address,
    uint64_t bytes,int32_t tag,bool exact,qa_error *error)
{
    qa_native_allocation_info info;
    if (!bytes || bytes > SIZE_MAX || !qa_native_guest_allocation(guest,address,&info,error) ||
        info.tag != tag || address < info.base || address-info.base > info.bytes ||
        bytes > info.bytes-(address-info.base) || (exact && (info.base != address || info.bytes != bytes)))
        return sysv_fail(error,QA_ERROR_FORMAT,"saved System V object differs from its actual lower allocation owner");
    return guest_range(guest,address,(size_t)bytes,0,error);
}
static bool lower_valid(guest_sysv_runtime *r,const qa_native_guest *guest,qa_error *error)
{
    if (qa_native_guest_execution(guest) != r->execution)
        return sysv_fail(error,QA_ERROR_FORMAT,"System V runtime differs from its actual saved backend");
    size_t p = r->target.pointer_bytes;
    const uint64_t addresses[] = {r->thread_area,r->dtv_base,r->errno_address,r->argv,r->envp,r->empty_string,r->strtok_slot};
    const uint64_t sizes[] = {0x11000,1026*2*p,4,(r->options.argc+1)*p,
        (r->options.environment_count+1)*p,4*p,p};
    for (size_t i = 0; i < sizeof(addresses)/sizeof(*addresses); ++i)
        if (!owned_allocation(guest,addresses[i],sizes[i],INT32_C(0x53595652),true,error) ||
            !guest_range(guest,addresses[i],(size_t)sizes[i],QA_NATIVE_GUEST_READ|QA_NATIVE_GUEST_WRITE,error)) return false;
    for (size_t i = 0; i < r->heap_count; ++i)
        if (!owned_allocation(guest,r->heap[i].address,r->heap[i].bytes,INT32_C(0x53595648),true,error)) return false;
    for (size_t i = 0; i < r->object_count; ++i) {
        const sysv_object *o = r->objects+i;
        if (!owned_allocation(guest,o->address,o->bytes,
            o->kind == 1 ? INT32_C(0x53595652) : INT32_C(0x53595648),o->kind == 1,error)) return false;
    }
    for (size_t i = 0; i < 3; ++i) {
        if (!owned_allocation(guest,r->files[i],p == 8 ? 224 : 152,INT32_C(0x53595648),true,error) ||
            !owned_allocation(guest,r->wide_files[i],p == 8 ? 312 : 180,INT32_C(0x53595648),true,error)) return false;
        const char *names[] = {"_IO_2_1_stdin_","_IO_2_1_stdout_","_IO_2_1_stderr_"};
        const char *version = p == 4 ? "GLIBC_2.1" : "GLIBC_2.2.5";
        guest_runtime_import_key key = {.scope = r->options.scope,.library = "libc.so.6",
            .kind = GUEST_RUNTIME_SYMBOL_NAME,.name = names[i],.version = version};
        guest_runtime_import_view view;
        if (!guest_runtime_imports_find(r->imports,&key,&view,error) ||
            view.kind != GUEST_RUNTIME_IMPORT_DATA || view.address != r->files[i] || view.bytes != (p == 8 ? 224 : 152))
            return sysv_fail(error,QA_ERROR_FORMAT,"saved System V FILE identity differs from its canonical data declaration");
    }
    if (!owned_allocation(guest,r->iostream_refcount,4,INT32_C(0x53595648),true,error) ||
        !owned_allocation(guest,r->iostream_sync,1,INT32_C(0x53595648),true,error) ||
        !return_trap_valid(guest,r->options.return_trap,error)) return false;
    for (size_t i = 0; i < r->image_count; ++i) {
        const guest_sysv_provider *provider = &r->images[i].provider;
        for (size_t j = 0; j < provider->export_count; ++j) {
            const guest_sysv_export *e = provider->exports+j;
            if (!e->tls && e->address && (e->bytes > SIZE_MAX ||
                !guest_range(guest,e->address,e->bytes ? (size_t)e->bytes : 1,0,error))) return false;
        }
    }
    for (size_t i = 0; i < r->unique_count; ++i)
        if (!guest_range(guest,r->unique[i].address,1,0,error)) return false;
    return sysv_stdio_lower_valid(r,guest,error);
}
bool guest_sysv_checkpoint(const guest_sysv_runtime *runtime,qa_buffer *out,qa_error *error)
{
    if (!out || out->data || out->size || !guest_sysv_idle(runtime) ||
        !sysv_current((guest_sysv_runtime *)runtime,error) || !runtime_valid((guest_sysv_runtime *)runtime,error) ||
        !imports_valid((guest_sysv_runtime *)runtime,error) ||
        !lower_valid((guest_sysv_runtime *)runtime,runtime->guest,error)) return false;
    qa_buffer imports = {0};
    if (!guest_runtime_imports_checkpoint(runtime->imports,&imports,error)) return false;
    qa_source_save_io io;
    if (!qa_source_save_writer(&io,NULL,error)) { qa_buffer_free(&imports); return false; }
    size_t bytes = imports.size;
    bool ok = runtime_fields(&io,(guest_sysv_runtime *)runtime) &&
        qa_source_save_count(&io,&bytes,SYSV_MAX_ALLOCATION) && qa_source_save_bytes(&io,imports.data,bytes) &&
        qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); qa_buffer_free(&imports); return ok;
}
bool guest_sysv_decode(qa_bytes bytes,const qa_native_target *target,
    const guest_sysv_bindings *bindings,guest_sysv_runtime **out,qa_error *error)
{
    if (!out || *out || !target_valid(target) || !bindings_valid(bindings))
        return sysv_fail(error,QA_ERROR_ARGUMENT,"System V detached decode requires its actual target and external bindings");
    guest_sysv_runtime *r = calloc(1,sizeof(*r));
    if (!r) return sysv_fail(error,QA_ERROR_MEMORY,"allocating detached System V process owner");
    r->detached = true; qa_source_save_io io;
    if (!qa_source_save_reader(&io,NULL,bytes,error)) { guest_sysv_abandon(&r); return false; }
    bool ok = runtime_fields(&io,r) && target_equal(&r->target,target) && runtime_valid(r,error);
    if (ok) {
        ok = r->options.bindings.clock_id == bindings->clock_id && r->clock_present == (bindings->time != NULL);
        for (size_t i = 0; i < 3 && ok; ++i) {
            uint8_t shape = (uint8_t)((bindings->streams[i].read ? 1 : 0)|(bindings->streams[i].write ? 2 : 0)|(bindings->streams[i].flush ? 4 : 0));
            ok = r->options.bindings.streams[i].id == bindings->streams[i].id && r->stream_shapes[i] == shape;
            if (!ok) break;
        }
        ok = ok && r->options.bindings.output_is_terminal == bindings->output_is_terminal;
        if (!ok) sysv_fail(error,QA_ERROR_FORMAT,"saved System V external capability identity or shape differs from actual bindings");
    }
    if (ok) {
        ok = r->file_resources == (bindings->resources != NULL) &&
            r->file_opener == (bindings->open_file != NULL);
        if (!ok) sysv_fail(error,QA_ERROR_FORMAT,"saved System V FILE registry differs from actual process bindings");
    }
    if (ok) {
        r->options.bindings = *bindings;
        ok = sysv_stdio_valid(r,error);
        size_t count = 0;
        ok = ok && qa_source_save_count(&io,&count,SYSV_MAX_ALLOCATION) && count <= io.input.size-io.offset;
        if (ok) {
            qa_bytes import_bytes = {io.input.data+io.offset,count}; io.offset += count;
            ok = guest_runtime_imports_decode(import_bytes,target,descriptor_resolve,r,&r->imports,error) &&
                imports_valid(r,error) && qa_source_save_finish(&io,NULL);
        }
    }
    qa_source_save_dispose(&io);
    if (!ok) { guest_sysv_abandon(&r); return false; }
    *out = r; return true;
}
bool guest_sysv_callback(void *context,uint64_t id,uint64_t address,
    qa_native_guest_callback *out,qa_error *error)
{
    guest_sysv_runtime *r = context;
    if (!r || !r->detached) return sysv_fail(error,QA_ERROR_ARGUMENT,"System V callback resolver requires its decoded detached owner");
    return guest_runtime_imports_callback(r->imports,id,address,out,error);
}
bool guest_sysv_attach(guest_sysv_runtime *r,qa_native_guest *guest,qa_error *error)
{
    if (!r || !r->detached || r->guest || !guest || !qa_native_guest_idle(guest) ||
        !target_equal(&guest->options.image.target,&r->target) || !runtime_valid(r,error) ||
        !r->options.bindings.current(r->options.bindings.context,guest,error)) return false;
    if (!lower_valid(r,guest,error) ||
        !guest_runtime_imports_attach(r->imports,guest,error)) return false;
    r->guest = guest; r->detached = false; return true;
}
