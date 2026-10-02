#include "internal.h"
#include "qa/source_save.h"
#include "guest/sysv_libc_format.h"
#include "guest/internal.h"

static bool source_matches(const qa_native_instance *instance, const qa_native_image_info *image,
    qa_bytes bytes)
{
    const qa_native_module *module = instance->module;
    const qa_native_image_info *expected = &module->info.image;
    return image->format == expected->format && image->target.os == expected->target.os &&
        image->target.arch == expected->target.arch && image->target.abi == expected->target.abi &&
        image->target.pointer_bytes == expected->target.pointer_bytes &&
        image->preferred_base == expected->preferred_base && image->image_bytes == expected->image_bytes &&
        qa_sha256_equal(&image->digest, &expected->digest) && bytes.size == module->size &&
        bytes.data && !memcmp(bytes.data, module->bytes, bytes.size);
}

bool native_process_open(qa_native_instance *instance, const qa_native_process_options *options,
    qa_error *error)
{
    if (!instance || !options || !options->source_id || !options->first_callback ||
        (options->kind != QA_NATIVE_PROCESS_SYSV && options->kind != QA_NATIVE_PROCESS_WINDOWS))
        return native_fail(error, QA_ERROR_ARGUMENT, 0, "native process requires its actual artifact and callback namespace");
    instance->process_kind = options->kind; instance->source_id = options->source_id;
    instance->first_callback = options->first_callback;
    instance->process_resources = options->resources;
    qa_native_process_resources_retain(instance->process_resources);
    bool found = false;
    if (options->kind == QA_NATIVE_PROCESS_SYSV) {
        const qa_native_sysv_process_options *actual = options->fresh.sysv;
        if (!actual || !actual->artifacts)
            return native_fail(error, QA_ERROR_ARGUMENT, 0, "native System V process graph is absent");
        for (size_t i = 0; i < actual->artifact_count; ++i) {
            const qa_native_sysv_artifact *row = actual->artifacts + i;
            if (row->provider != options->source_id) continue;
            if (found || row->role != QA_NATIVE_SYSV_LIBRARY ||
                !source_matches(instance, &row->image, row->bytes) ||
                row->load_bias > UINT64_MAX - row->image.preferred_base)
                return native_fail(error, QA_ERROR_FORMAT, i, "native source differs from its actual System V library graph");
            instance->image_base = row->load_bias + row->image.preferred_base;
            instance->image_bytes = row->image.image_bytes; found = true;
        }
        if (!found) return native_fail(error, QA_ERROR_FORMAT, options->source_id, "native source provider is absent");
        if (!qa_native_sysv_process_create(actual, &instance->sysv_process, error)) return false;
        instance->guest = qa_native_sysv_process_guest(instance->sysv_process);
    } else {
        const qa_native_windows_process_options *actual = options->fresh.windows;
        if (!actual || !actual->artifacts)
            return native_fail(error, QA_ERROR_ARGUMENT, 0, "native Windows process graph is absent");
        for (size_t i = 0; i < actual->artifact_count; ++i) {
            const qa_native_windows_artifact *row = actual->artifacts + i;
            if (row->id != options->source_id) continue;
            if (found || !source_matches(instance, &row->image, row->bytes))
                return native_fail(error, QA_ERROR_FORMAT, i, "native source differs from its actual Windows library graph");
            instance->image_base = row->load_base; instance->image_bytes = row->image.image_bytes;
            instance->source_library = native_strdup(row->path, error);
            if (!instance->source_library) return false;
            found = true;
        }
        if (!found) return native_fail(error, QA_ERROR_FORMAT, options->source_id, "native source image is absent");
        if (!qa_native_windows_process_create(actual, &instance->windows_process, error)) return false;
        instance->guest = qa_native_windows_process_guest(instance->windows_process);
    }
    const native_profile_spec *profile = native_profile(instance->module->info.profile);
    if (instance->region_count && !qa_native_guest_instructions(instance->guest,
        native_process_region_instruction, instance, error)) return false;
    if (instance->options.observe && qa_native_guest_execution(instance->guest) == QA_NATIVE_GUEST_EMULATED) {
        if (!qa_native_guest_observe(instance->guest, native_process_write_commit, instance, error)) return false;
        instance->process_observing = true;
    }
    size_t slots = profile->q3_vm ? 1 : profile->import_count;
    if (!slots || slots > SIZE_MAX / 16 || slots - 1 > UINT64_MAX - instance->first_callback)
        return native_fail(error, QA_ERROR_ARGUMENT, slots, "native callback namespace or extent overflows");
    if (!qa_native_guest_allocate_aligned(instance->guest, slots * 16, 16,
        QA_NATIVE_GUEST_READ | QA_NATIVE_GUEST_WRITE, INT32_C(0x4e434254),
        &instance->callback_base, error)) return false;
    uint8_t trap[16]; memset(trap, 0xcc, sizeof(trap));
    for (size_t i = 0; i < slots; ++i)
        if (!qa_native_guest_write(instance->guest, instance->callback_base + i * 16,
            (qa_bytes){trap, sizeof(trap)}, error)) return false;
    size_t pages = native_align(slots * 16, QA_NATIVE_GUEST_PAGE);
    if (!qa_native_guest_protect_range(instance->guest, instance->callback_base, pages,
        QA_NATIVE_GUEST_READ | QA_NATIVE_GUEST_EXECUTE, error)) return false;
    if (options->kind == QA_NATIVE_PROCESS_SYSV) {
        const qa_native_sysv_process_options *actual = options->fresh.sysv;
        for (size_t i = 0; i < actual->artifact_count; ++i)
            if (!qa_native_sysv_process_initialize(instance->sysv_process, actual->artifacts[i].provider, error)) return false;
    } else {
        const qa_native_windows_process_options *actual = options->fresh.windows;
        for (size_t i = 0; i < actual->artifact_count; ++i)
            if (!qa_native_windows_process_initialize(instance->windows_process, actual->artifacts[i].id, error)) return false;
    }
    return true;
}

bool native_process_close(qa_native_instance *instance, qa_error *error)
{
    bool idle = instance->process_kind == QA_NATIVE_PROCESS_SYSV ?
        qa_native_sysv_process_idle(instance->sysv_process) : qa_native_windows_process_idle(instance->windows_process);
    if (idle && !qa_native_terminal(instance)) {
        qa_native_instance *previous = native_active_instance;
        unsigned active = instance->active_depth;
        bool unloading = instance->unloading;
        native_active_instance = instance; instance->active_depth = 1; instance->unloading = true;
        qa_error finalization = {0};
        bool finalized = instance->process_kind == QA_NATIVE_PROCESS_SYSV ?
            qa_native_sysv_process_finalize_all(instance->sysv_process, &finalization) :
            qa_native_windows_process_finalize_all(instance->windows_process, &finalization);
        instance->active_depth = active; instance->unloading = unloading; native_active_instance = previous;
        if (!finalized) native_latch_error(instance, &finalization);
    }
    bool okay = instance->process_kind == QA_NATIVE_PROCESS_SYSV ?
        qa_native_sysv_process_dispose(&instance->sysv_process, error) :
        qa_native_windows_process_dispose(&instance->windows_process, error);
    if (okay) {
        instance->guest = NULL; free(instance->source_library); instance->source_library = NULL;
        while (instance->process_temporaries) {
            native_process_temporary *temporary = instance->process_temporaries;
            instance->process_temporaries = temporary->next;
            native_remove_tree(temporary->directory);
            free(temporary->directory); free(temporary);
        }
        okay = qa_native_process_resources_release(&instance->process_resources, error);
    }
    return okay;
}

bool native_process_export(const qa_native_instance *instance, const char *name,
    qa_native_address *out, qa_error *error)
{
    return instance->process_kind == QA_NATIVE_PROCESS_SYSV ?
        qa_native_sysv_process_export(instance->sysv_process, instance->source_id, name, NULL, out, error) :
        qa_native_windows_process_export(instance->windows_process, instance->source_library, name, out, error);
}

static void process_dispatch(void *context)
{ native_call_started(context); }

bool native_process_invoke(qa_native_instance *instance, qa_native_address address,
    const qa_native_signature *signature, const qa_native_value *arguments, size_t count,
    qa_native_value *result, qa_error *error)
{
    void (*previous)(void *) = instance->guest->dispatch_started;
    void *context = instance->guest->dispatch_context;
    instance->guest->dispatch_started = process_dispatch;
    instance->guest->dispatch_context = instance;
    bool okay = instance->process_kind == QA_NATIVE_PROCESS_SYSV ?
        qa_native_sysv_process_invoke(instance->sysv_process, address, signature, arguments, count, result, error) :
        qa_native_windows_process_invoke(instance->windows_process, address, signature, arguments, count, result, error);
    instance->guest->dispatch_started = previous; instance->guest->dispatch_context = context;
    return okay;
}

static bool import_entry(void *context, qa_native_guest *guest, uint64_t id, qa_error *error)
{
    native_import_binding *binding = context;
    if (binding->guest_id != id || binding->instance->guest != guest ||
        native_active_instance != binding->instance)
        return native_fail(error, QA_ERROR_ARGUMENT, id, "native import lost its actual process owner");
    size_t count = binding->spec.signature.parameter_count;
    qa_native_value arguments[NATIVE_MAX_ARGUMENTS] = {{0}};
    qa_native_value result = {.type = binding->spec.signature.result.kind};
    qa_buffer storage = {0}, output = {0};
    if (result.type == QA_NATIVE_BYTES) {
        output.size = guest_abi_result_bytes(binding->guest_plan);
        output.data = calloc(1, output.size);
        if (!output.data) return native_fail(error, QA_ERROR_MEMORY, id, "owning native import aggregate result");
        result.as.bytes = (qa_native_memory){output.data, output.size};
    }
    bool okay = guest_abi_decode(binding->guest_plan, guest, arguments, count, &storage, error);
    qa_buffer formatted = {0};
    uint64_t temporary = 0;
    if (okay && binding->spec.signature.variadic) {
        if (!count || arguments[count - 1].type != QA_NATIVE_ADDRESS)
            okay = native_fail(error, QA_ERROR_FORMAT, id, "formatted SDK import lacks its genuine format argument");
        uint64_t format = okay ? arguments[count - 1].as.address : 0;
        if (okay) okay = guest_format_render_entry(guest, &binding->spec.signature,
            format, NATIVE_MAX_STRING - 1, &formatted, error);
        if (okay) okay = qa_native_guest_allocate(guest, formatted.size + 1, INT32_C(0x4e46524d), &temporary, error) &&
            qa_native_guest_write(guest, temporary, (qa_bytes){formatted.data, formatted.size + 1}, error);
        if (okay) {
            arguments[count - 1].as.address = temporary;
        }
    }
    if (okay) okay = native_dispatch_import(binding->instance, &binding->spec, arguments, count, &result);
    if (!okay && error && error->code == QA_OK) *error = binding->instance->failure;
    if (temporary) {
        qa_error cleanup = {0};
        if (!qa_native_guest_free(guest, temporary, &cleanup) && okay) { okay = false; if (error) *error = cleanup; }
    }
    if (okay) okay = guest_abi_return(binding->guest_plan, guest, &result, error);
    qa_buffer_free(&formatted);
    qa_buffer_free(&storage); qa_buffer_free(&output); return okay;
}

bool native_process_import_bind(native_import_binding *binding, qa_native_instance *instance,
    const native_signature_spec *spec, qa_error *error)
{
    memset(binding, 0, sizeof(*binding)); binding->instance = instance; binding->spec = *spec;
    binding->spec.signature.abi = instance->module->info.image.target.abi;
    size_t index = (size_t)(binding - instance->imports);
    binding->guest_id = instance->first_callback + index;
    binding->guest_address = instance->callback_base + index * 16;
    if (!guest_abi_plan_native(&binding->spec.signature, NULL, 0, &binding->guest_plan, error)) return false;
    qa_native_guest_callback callback = {binding->guest_id, binding->guest_address, import_entry, binding};
    return qa_native_guest_bind(instance->guest, &callback, error);
}

static bool syscall_entry(void *context, qa_native_guest *guest, uint64_t id, qa_error *error)
{
    qa_native_instance *instance = context;
    if (guest != instance->guest || id != instance->first_callback || native_active_instance != instance)
        return native_fail(error, QA_ERROR_ARGUMENT, id, "native syscall lost its source owner");
    qa_native_type service_type = {.kind = QA_NATIVE_I32, .count = 1};
    qa_native_signature signature = {.abi = instance->module->info.image.target.abi,
        .parameters = &service_type, .parameter_count = 1, .variadic = true,
        .result = {.kind = instance->module->info.image.target.pointer_bytes == 8 ? QA_NATIVE_I64 : QA_NATIVE_I32, .count = 1}};
    guest_abi_plan *fixed = NULL, *complete = NULL;
    qa_buffer storage = {0}; qa_native_value arguments[NATIVE_MAX_ARGUMENTS + 1] = {{0}};
    const qa_native_value_type *types = NULL; size_t count = 0;
    bool okay = guest_abi_plan_native(&signature, NULL, 0, &fixed, error) &&
        guest_abi_decode(fixed, guest, arguments, 1, &storage, error);
    int32_t service = arguments[0].as.i32;
    if (okay) {
        ++instance->callback_depth;
        okay = instance->options.describe_syscall(instance->options.context, service, &types, &count, error);
        --instance->callback_depth;
    }
    guest_abi_layout extras[NATIVE_MAX_ARGUMENTS];
    if (okay && (count > NATIVE_MAX_ARGUMENTS || (count && !types)))
        okay = native_fail(error, QA_ERROR_ARGUMENT, count, "native syscall descriptor exceeds its actual ABI limit");
    for (size_t i = 0; okay && i < count; ++i) {
        qa_native_value_type type = types[i]; size_t bytes = 4;
        switch (type) {
        case QA_NATIVE_I8: case QA_NATIVE_U8: case QA_NATIVE_I16: case QA_NATIVE_U16: type = QA_NATIVE_I32; break;
        case QA_NATIVE_F32: type = QA_NATIVE_F64; bytes = 8; break;
        case QA_NATIVE_I64: case QA_NATIVE_U64: case QA_NATIVE_F64: bytes = 8; break;
        case QA_NATIVE_ADDRESS: bytes = instance->module->info.image.target.pointer_bytes; break;
        case QA_NATIVE_I32: case QA_NATIVE_U32: break;
        default: okay = native_fail(error, QA_ERROR_ARGUMENT, i, "native syscall descriptor contains a non-scalar variadic argument"); break;
        }
        size_t alignment = signature.abi == QA_NATIVE_ABI_SYSTEM_V_I386 && bytes > 4 ? 4 : bytes;
        extras[i] = (guest_abi_layout){.kind = type, .bytes = bytes, .alignment = alignment};
    }
    qa_buffer_free(&storage);
    if (okay) okay = guest_abi_plan_native(&signature, extras, count, &complete, error) &&
        guest_abi_decode(complete, guest, arguments, count + 1, &storage, error);
    for (size_t i = 0; okay && i < count; ++i) {
        qa_native_value *value = arguments + i + 1;
        switch (types[i]) {
        case QA_NATIVE_I8: value->as.i8 = (int8_t)value->as.i32; break;
        case QA_NATIVE_U8: value->as.u8 = (uint8_t)value->as.i32; break;
        case QA_NATIVE_I16: value->as.i16 = (int16_t)value->as.i32; break;
        case QA_NATIVE_U16: value->as.u16 = (uint16_t)value->as.i32; break;
        case QA_NATIVE_F32: value->as.f32 = (float)value->as.f64; break;
        default: break;
        }
        value->type = types[i];
    }
    intptr_t actual = 0;
    if (okay) {
        ++instance->callback_depth;
        okay = instance->options.syscall(instance->options.context, instance, service, arguments + 1, count, &actual, error);
        --instance->callback_depth;
    }
    qa_native_value result = {.type = signature.result.kind};
    if (result.type == QA_NATIVE_I64) result.as.i64 = actual; else result.as.i32 = (int32_t)actual;
    if (okay) okay = guest_abi_return(complete, guest, &result, error);
    qa_buffer_free(&storage); guest_abi_plan_destroy(fixed); guest_abi_plan_destroy(complete); return okay;
}

bool native_process_q3_bind(qa_native_instance *instance, qa_native_address *out, qa_error *error)
{
    if (!instance->options.describe_syscall || !instance->options.syscall)
        return native_fail(error, QA_ERROR_ARGUMENT, 0, "native Q3 requires its real syscall descriptor and dispatch");
    qa_native_guest_callback callback = {instance->first_callback, instance->callback_base, syscall_entry, instance};
    if (!qa_native_guest_bind(instance->guest, &callback, error)) return false;
    *out = callback.address; return true;
}

static bool capsule_blob(qa_source_save_io *io, qa_bytes *bytes)
{
    size_t size = bytes->size;
    if (!qa_source_save_count(io, &size, SIZE_MAX)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        if (size > io->input.size - io->offset)
            return native_fail(io->error, QA_ERROR_FORMAT, io->offset, "native process capsule is truncated");
        *bytes = (qa_bytes){io->input.data + io->offset, size}; io->offset += size; return true;
    }
    return qa_source_save_bytes(io, (void *)bytes->data, size);
}

static bool capsule_fields(qa_source_save_io *io, qa_native_instance *instance)
{
    uint8_t magic[] = {'Q','N','P','R',1}, expected[] = {'Q','N','P','R',1};
    uint32_t profile = instance->module->info.profile, role = instance->options.q3_role;
    uint32_t kind = instance->process_kind, lifecycle = instance->lifecycle;
    qa_sha256_digest image = instance->module->info.image.digest, declaration = instance->declaration;
    bool has_declaration = instance->has_declaration;
    if (!qa_source_save_bytes(io, magic, sizeof(magic))) return false;
    if (memcmp(magic, expected, sizeof(magic)))
        return native_fail(io->error, QA_ERROR_FORMAT, io->offset, "native process capsule signature differs");
    if (!qa_source_save_u32(io, &profile) || !qa_source_save_u32(io, &role) ||
        !qa_source_save_u32(io, &kind) || !qa_source_save_u32(io, &lifecycle) ||
        !qa_source_save_bytes(io, image.bytes, sizeof(image.bytes)) ||
        !qa_source_save_bool(io, &has_declaration) ||
        !qa_source_save_bytes(io, declaration.bytes, sizeof(declaration.bytes))) return false;
    if (profile != (uint32_t)instance->module->info.profile || role != (uint32_t)instance->options.q3_role ||
        kind > QA_NATIVE_PROCESS_WINDOWS || lifecycle > QA_NATIVE_RESTART_READY ||
        !qa_sha256_equal(&image, &instance->module->info.image.digest) ||
        has_declaration != instance->has_declaration ||
        (has_declaration && !qa_sha256_equal(&declaration, &instance->declaration)))
        return native_fail(io->error, QA_ERROR_FORMAT, io->offset, "native process capsule differs from its actual module profile");
    instance->process_kind = (qa_native_process_kind)kind; instance->lifecycle = (qa_native_lifecycle)lifecycle;
    uint32_t tick_rate = instance->options.tick_rate, milliseconds = instance->options.frame_milliseconds;
    float seconds = instance->options.frame_seconds;
    if (!qa_source_save_u32(io, &tick_rate) || !qa_source_save_u32(io, &milliseconds) ||
        !qa_source_save_f32(io, &seconds)) return false;
    if (tick_rate != instance->options.tick_rate || milliseconds != instance->options.frame_milliseconds ||
        memcmp(&seconds, &instance->options.frame_seconds, sizeof(seconds)))
        return native_fail(io->error, QA_ERROR_FORMAT, io->offset, "native source tick declaration differs from its saved process");
    uint64_t *addresses[] = {&instance->source_id, &instance->first_callback, &instance->callback_base,
        &instance->image_base, &instance->image_bytes, &instance->import_table_address,
        &instance->export_table, &instance->entities.base};
    for (size_t i = 0; i < sizeof(addresses)/sizeof(*addresses); ++i)
        if (!qa_source_save_u64(io, addresses[i])) return false;
    if (!qa_source_save_count(io, &instance->import_table_bytes, SIZE_MAX) ||
        !qa_source_save_count(io, &instance->entities.stride, SIZE_MAX) ||
        !qa_source_save_u32(io, &instance->entities.count) ||
        !qa_source_save_u32(io, &instance->entities.capacity)) return false;
    return instance->source_id && instance->first_callback && instance->callback_base &&
        instance->image_bytes == instance->module->info.image.image_bytes &&
        instance->image_base <= UINT64_MAX - instance->image_bytes &&
        instance->entities.count <= instance->entities.capacity &&
        (!instance->entities.capacity || (instance->entities.base && instance->entities.stride)) ? true :
        native_fail(io->error, QA_ERROR_FORMAT, io->offset, "native process source addresses are invalid");
}

bool native_process_checkpoint_host(qa_native_instance *instance, qa_bytes actual_host,
    qa_buffer *out, qa_error *error)
{
    if (!instance || instance->backend != QA_NATIVE_BACKEND_OWNED_PROCESS || !out || out->data || out->size ||
        instance->active_depth || instance->callback_depth || instance->destroying ||
        qa_native_terminal(instance) || !qa_native_guest_idle(instance->guest))
        return native_fail(error, QA_ERROR_ARGUMENT, 0, "native process capture requires its idle complete source owner");
    qa_buffer process = {0};
    bool okay = instance->process_kind == QA_NATIVE_PROCESS_SYSV ?
        qa_native_sysv_process_checkpoint(instance->sysv_process, &process, error) :
        qa_native_windows_process_checkpoint(instance->windows_process, &process, error);
    qa_source_save_io io;
    bool writer = okay && qa_source_save_writer(&io, NULL, error);
    if (writer) {
        qa_bytes process_bytes = {process.data, process.size}, host_bytes = actual_host;
        okay = capsule_fields(&io, instance) && capsule_blob(&io, &process_bytes) && capsule_blob(&io, &host_bytes);
        size_t count = instance->entry_count;
        if (okay) okay = qa_source_save_count(&io, &count, native_profile(instance->module->info.profile)->entry_count);
        for (size_t i = 0; okay && i < count; ++i) okay = qa_source_save_u64(&io, &instance->entries[i].address);
        count = 0;
        for (native_allocation *a = instance->allocations; a; a = a->next) ++count;
        if (okay) okay = qa_source_save_count(&io, &count, SIZE_MAX);
        for (native_allocation *a = instance->allocations; okay && a; a = a->next)
            okay = qa_source_save_u64(&io, &a->guest_address) && qa_source_save_count(&io, &a->size, SIZE_MAX) &&
                qa_source_save_i32(&io, &a->tag);
        if (okay) okay = qa_source_save_finish(&io, out);
        qa_source_save_dispose(&io);
    } else okay = false;
    qa_buffer_free(&process); return okay;
}

bool qa_native_process_checkpoint(qa_native_instance *instance, qa_buffer *out, qa_error *error)
{
    if (!instance || instance->checkpointing || instance->active_depth || instance->callback_depth ||
        instance->destroying || instance->backend != QA_NATIVE_BACKEND_OWNED_PROCESS ||
        !instance->options.checkpoint || !instance->options.restore)
        return native_fail(error, QA_ERROR_ARGUMENT, 0, "native private capture requires its actual idle process/host binding");
    instance->checkpointing = true; qa_buffer host = {0};
    bool okay = instance->options.checkpoint(instance->options.context, &host, error) &&
        native_process_checkpoint_host(instance, (qa_bytes){host.data, host.size}, out, error);
    qa_buffer_free(&host); instance->checkpointing = false; return okay;
}

static bool restored_callback(void *context, uint64_t id, uint64_t address,
    qa_native_guest_callback *out, qa_error *error)
{
    qa_native_instance *instance = context;
    if (instance->module->info.profile == QA_NATIVE_Q3_VMMAIN) {
        if (id == instance->first_callback && address == instance->callback_base) {
            *out = (qa_native_guest_callback){id, address, syscall_entry, instance}; return true;
        }
    } else {
        for (size_t i = 0; i < instance->import_count; ++i) {
            native_import_binding *binding = instance->imports + i;
            if (binding->guest_id != id) continue;
            if (binding->guest_address != address)
                return native_fail(error, QA_ERROR_FORMAT, id, "native import address differs from its saved SDK slot");
            *out = (qa_native_guest_callback){id, address, import_entry, binding}; return true;
        }
    }
    return native_fail(error, QA_ERROR_NOT_FOUND, id, "native SDK callback identity is absent");
}

static bool retained_allocation(qa_native_instance *instance, uint64_t address,
    size_t bytes, int32_t tag, qa_error *error)
{
    qa_native_allocation_info actual;
    return qa_native_guest_allocation(instance->guest, address, &actual, error) &&
        ((actual.base == address && actual.bytes == bytes && actual.tag == tag) ||
        native_fail(error, QA_ERROR_FORMAT, address, "native saved allocation differs from its true lower owner"));
}

bool native_process_restore(qa_native_instance *instance, const qa_native_process_options *options,
    qa_error *error)
{
    if (!options || !options->continuation.data || !options->continuation.size ||
        !instance->options.checkpoint || !instance->options.restore)
        return native_fail(error, QA_ERROR_ARGUMENT, 0, "native cold construction requires its actual process and host bindings");
    instance->process_resources = options->resources;
    qa_native_process_resources_retain(instance->process_resources);
    qa_source_save_io io;
    if (!qa_source_save_reader(&io, NULL, options->continuation, error)) return false;
    qa_bytes process = {0}, host = {0}; size_t count = 0;
    bool okay = capsule_fields(&io, instance);
    if (okay && (instance->process_kind != options->kind || instance->source_id != options->source_id ||
        instance->first_callback != options->first_callback))
        okay = native_fail(error, QA_ERROR_FORMAT, io.offset, "native capsule differs from its prepared source namespace");
    if (okay) okay = capsule_blob(&io, &process) && capsule_blob(&io, &host);
    const native_profile_spec *profile = native_profile(instance->module->info.profile);
    if (okay) okay = qa_source_save_count(&io, &count, profile->entry_count);
    if (okay && count != profile->entry_count)
        okay = native_fail(error, QA_ERROR_FORMAT, io.offset, "native capsule SDK entry count differs");
    if (okay) okay = native_profile_prepare_remote(instance, error);
    for (size_t i = 0; okay && i < count; ++i) {
        okay = qa_source_save_u64(&io, &instance->entries[i].address);
        if (okay && !instance->entries[i].address && !instance->entries[i].spec.optional)
            okay = native_fail(error, QA_ERROR_FORMAT, i, "native saved required SDK entry is null");
    }
    if (okay) okay = qa_source_save_count(&io, &count, SIZE_MAX);
    if (okay && count > (io.input.size - io.offset) / 20)
        okay = native_fail(error, QA_ERROR_FORMAT, io.offset, "native capsule tagged allocation count exceeds its stored rows");
    native_allocation **tail = &instance->allocations;
    for (size_t i = 0; okay && i < count; ++i) {
        native_allocation *allocation = calloc(1, sizeof(*allocation));
        if (!allocation) { okay = native_fail(error, QA_ERROR_MEMORY, i, "owning saved native tagged allocation"); break; }
        *tail = allocation; tail = &allocation->next;
        okay = qa_source_save_u64(&io, &allocation->guest_address) &&
            qa_source_save_count(&io, &allocation->size, SIZE_MAX) && qa_source_save_i32(&io, &allocation->tag);
        if (okay && (!allocation->guest_address || !allocation->size))
            okay = native_fail(error, QA_ERROR_FORMAT, i, "native saved tagged allocation is empty");
        for (native_allocation *prior = instance->allocations; okay && prior != allocation; prior = prior->next)
            if (prior->guest_address == allocation->guest_address)
                okay = native_fail(error, QA_ERROR_FORMAT, i, "native tagged allocation identity repeats");
    }
    if (okay) okay = qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    if (!okay) return false;
    if (!native_copy_bytes(host, &instance->process_host, error)) return false;
    size_t slots = profile->q3_vm ? 1 : profile->import_count;
    if (!slots || slots - 1 > UINT64_MAX - instance->first_callback ||
        slots > SIZE_MAX / 16 || slots * 16 > UINT64_MAX - instance->callback_base)
        return native_fail(error, QA_ERROR_FORMAT, slots, "native saved callback namespace overflows");
    size_t expected_table = profile->q3_vm ? 0 : profile->quake_live ?
        206u * instance->module->info.image.target.pointer_bytes :
        profile->import_prefix + profile->import_count * instance->module->info.image.target.pointer_bytes;
    if (instance->import_table_bytes != expected_table || instance->callback_base % 16 ||
        (!profile->q3_vm && (!instance->import_table_address || !instance->export_table)))
        return native_fail(error, QA_ERROR_FORMAT, slots, "native saved SDK table extent differs from its genuine profile");
    if (!profile->q3_vm) {
        instance->imports = calloc(slots, sizeof(*instance->imports));
        if (!instance->imports) return native_fail(error, QA_ERROR_MEMORY, slots, "owning detached SDK callback descriptors");
        instance->import_count = slots;
        for (size_t i = 0; i < slots; ++i) {
            native_import_binding *binding = instance->imports + i;
            binding->instance = instance; binding->spec = profile->imports[i];
            binding->spec.signature.abi = instance->module->info.image.target.abi;
            binding->guest_id = instance->first_callback + i; binding->guest_address = instance->callback_base + i * 16;
            if (!guest_abi_plan_native(&binding->spec.signature, NULL, 0, &binding->guest_plan, error)) return false;
        }
    } else if (!instance->options.describe_syscall || !instance->options.syscall)
        return native_fail(error, QA_ERROR_ARGUMENT, 0, "native cold Q3 requires its true syscall descriptor");
    if (instance->process_kind == QA_NATIVE_PROCESS_SYSV) {
        if (!options->restored.sysv) return native_fail(error, QA_ERROR_ARGUMENT, 0, "native cold System V capability bindings are absent");
        qa_native_sysv_process_restore_bindings bindings = *options->restored.sysv;
        bindings.external_callback = restored_callback; bindings.external_context = instance;
        if (!qa_native_sysv_process_restore(process, &bindings, &instance->sysv_process, error)) return false;
        instance->guest = qa_native_sysv_process_guest(instance->sysv_process);
        qa_native_sysv_artifact artifact;
        if (!qa_native_sysv_process_artifact_read(instance->sysv_process, instance->source_id, &artifact, error)) return false;
        if (artifact.role != QA_NATIVE_SYSV_LIBRARY || !source_matches(instance, &artifact.image, artifact.bytes) ||
            artifact.load_bias > UINT64_MAX - artifact.image.preferred_base ||
            artifact.load_bias + artifact.image.preferred_base != instance->image_base)
            return native_fail(error, QA_ERROR_FORMAT, instance->source_id, "native cold System V artifact differs from its source witness");
    } else {
        if (!options->restored.windows) return native_fail(error, QA_ERROR_ARGUMENT, 0, "native cold Windows capability bindings are absent");
        qa_native_windows_process_restore_bindings bindings = *options->restored.windows;
        bindings.external_callback = restored_callback; bindings.external_context = instance;
        if (!qa_native_windows_process_restore(process, &bindings, &instance->windows_process, error)) return false;
        instance->guest = qa_native_windows_process_guest(instance->windows_process);
        qa_native_windows_artifact artifact;
        if (!qa_native_windows_process_artifact_read(instance->windows_process, instance->source_id, &artifact, error)) return false;
        if (!source_matches(instance, &artifact.image, artifact.bytes) || artifact.load_base != instance->image_base)
            return native_fail(error, QA_ERROR_FORMAT, instance->source_id, "native cold Windows artifact differs from its source witness");
        instance->source_library = native_strdup(artifact.path, error);
        if (!instance->source_library) return false;
    }
    if (!retained_allocation(instance, instance->callback_base, slots * 16, INT32_C(0x4e434254), error)) return false;
    if (instance->import_table_bytes && !retained_allocation(instance, instance->import_table_address,
        instance->import_table_bytes, INT32_C(0x4e494d50), error)) return false;
    for (native_allocation *a = instance->allocations; a; a = a->next)
        if (!retained_allocation(instance, a->guest_address, a->size, a->tag, error)) return false;
    if (instance->region_count && !qa_native_guest_instructions(instance->guest,
        native_process_region_instruction, instance, error)) return false;
    if (instance->options.observe && qa_native_guest_execution(instance->guest) == QA_NATIVE_GUEST_EMULATED) {
        if (!qa_native_guest_observe(instance->guest, native_process_write_commit, instance, error)) return false;
        instance->process_observing = true;
    }
    return true;
}

bool native_process_publish(qa_native_instance *instance, qa_native_instance *previous, qa_error *error)
{
    if (previous && (previous == instance || previous->backend != QA_NATIVE_BACKEND_OWNED_PROCESS ||
        previous->process_kind != instance->process_kind || !qa_native_can_destroy(previous)))
        return native_fail(error, QA_ERROR_ARGUMENT, 0, "native process adoption requires its retained drained previous owner");
    /* Distinct retained helper owners hold separate native references, as
     * produced by actual capture/rebind. Their callbacks have distinct owned
     * contexts and must never consume the previous helper's close receipts. */
    bool owned = previous && instance->process_resources && previous->process_resources &&
        instance->process_resources != previous->process_resources;
    bool okay;
    if (instance->process_kind == QA_NATIVE_PROCESS_SYSV)
        okay = owned ? qa_native_sysv_process_adopt_owned(instance->sysv_process,
            previous->sysv_process, error) : qa_native_sysv_process_adopt(instance->sysv_process,
            previous ? previous->sysv_process : NULL, error);
    else
        okay = owned ? qa_native_windows_process_adopt_owned(instance->windows_process,
            previous->windows_process, error) : qa_native_windows_process_adopt(instance->windows_process,
            previous ? previous->windows_process : NULL, error);
    if (okay && previous) previous->lifecycle = QA_NATIVE_SHUT_DOWN;
    return okay;
}
