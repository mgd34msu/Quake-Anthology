#include "protocol.h"

#if defined(_WIN32)
__declspec(dllexport) __declspec(noinline)
#else
__attribute__((visibility("default"), noinline))
#endif
void qa_native_runner_hook_control(native_hook_control *control) {
    /* The instrumentation insertion sets status before this application marker
     * executes. Keep a real ABI argument and symbol in uninstrumented helpers. */
    volatile uint64_t magic = control->magic;
    (void)magic;
}

bool native_hooks_control(native_hook_control *control) {
    control->magic = NATIVE_HOOK_CONTROL_MAGIC;
    control->status = 0;
    qa_native_runner_hook_control(control);
    return ((volatile native_hook_control *)control)->status == 1u;
}

void native_hooks_depth(uint32_t depth) {
    native_hook_control control = {.operation = NATIVE_HOOK_DEPTH, .size = depth};
    (void)native_hooks_control(&control);
}

static bool observer_boundary(qa_native_instance *instance, qa_error *error) {
    if (!instance || instance->checkpointing || instance->destroying || instance->unloading)
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "native observation requires a live instance");
    bool owned = instance->backend == QA_NATIVE_BACKEND_OWNED_PROCESS && instance->guest &&
        qa_native_guest_execution(instance->guest) == QA_NATIVE_GUEST_EMULATED;
    if ((!owned && instance->backend != QA_NATIVE_BACKEND_RUNNER) ||
        (!instance->options.observe && !instance->region_count))
        return native_fail(error, QA_ERROR_UNSUPPORTED, 0,
                           "native observation requires its actual instrumented execution owner");
    return true;
}

static bool next_id(qa_native_instance *instance, uint64_t *id, qa_error *error) {
    if (instance->next_observer_id == UINT64_MAX)
        return native_fail(error, QA_ERROR_ARGUMENT, 0, "native observer identity is exhausted");
    *id = ++instance->next_observer_id;
    return true;
}

static bool copy_signature(const qa_native_signature *signature, qa_native_signature *out,
                            qa_error *error) {
    native_wire_buffer encoded = {0};
    bool ok = native_wire_put_signature(&encoded, signature, error);
    if (ok) {
        native_wire_reader reader = {.bytes = {encoded.data, encoded.size}};
        ok = native_wire_get_signature(&reader, out, error) && native_wire_end(&reader, error);
    }
    native_wire_buffer_free(&encoded);
    return ok;
}

static bool process_entry(void *context, qa_native_guest *guest, uint64_t id, qa_error *error) {
    qa_native_entry_observer *binding = context;
    qa_native_instance *instance = binding->instance;
    if (instance->guest != guest || binding->guest_id != id || native_active_instance != instance)
        return native_fail(error, QA_ERROR_ARGUMENT, id, "observed entry lost its actual source owner");
    size_t count = binding->signature.parameter_count;
    if (count > NATIVE_MAX_ARGUMENTS) return native_fail(error, QA_ERROR_ARGUMENT, count, "observed source ABI exceeds its argument limit");
    qa_native_value arguments[NATIVE_MAX_ARGUMENTS] = {{0}};
    qa_native_value result = {.type = binding->signature.result.kind};
    qa_buffer storage = {0}, output = {0};
    if (result.type == QA_NATIVE_BYTES) {
        output.size = guest_abi_result_bytes(binding->guest_plan);
        output.data = calloc(1, output.size);
        if (!output.data) return native_fail(error, QA_ERROR_MEMORY, id, "owning actual observed aggregate result");
        result.as.bytes = (qa_native_memory){output.data, output.size};
    }
    bool okay = guest_abi_decode(binding->guest_plan, guest, arguments, count, &storage, error);
    if (okay) {
        ++binding->active_calls; ++instance->callback_depth;
        okay = binding->callback(binding->context, instance, binding, arguments, count, &result, error);
        --instance->callback_depth; --binding->active_calls;
    }
    if (okay) okay = guest_abi_return(binding->guest_plan, guest, &result, error);
    qa_buffer_free(&storage); qa_buffer_free(&output); return okay;
}

static bool process_entry_bind(qa_native_entry_observer *binding, qa_error *error) {
    qa_native_instance *instance = binding->instance;
    const native_profile_spec *profile = native_profile(instance->module->info.profile);
    uint64_t slots = profile->q3_vm ? 1 : profile->import_count;
    if (slots > UINT64_MAX - instance->first_callback ||
        binding->id > UINT64_MAX - instance->first_callback - slots)
        return native_fail(error, QA_ERROR_ARGUMENT, binding->id, "source observer callback namespace overflows");
    binding->guest_id = instance->first_callback + slots + binding->id;
    if (instance->process_kind == QA_NATIVE_PROCESS_WINDOWS &&
        binding->guest_id >= qa_native_windows_process_callback_minimum())
        return native_fail(error, QA_ERROR_ARGUMENT, binding->id, "source observer overlaps the actual Windows service namespace");
    if (!guest_abi_plan_native(&binding->signature, NULL, 0, &binding->guest_plan, error)) return false;
    qa_native_guest_callback callback = {binding->guest_id, binding->address, process_entry, binding};
    return qa_native_guest_bind(instance->guest, &callback, error);
}

bool qa_native_observe_entry(qa_native_instance *instance, qa_native_address entry,
                             const qa_native_signature *signature,
                             qa_native_entry_observer_fn callback, void *context,
                             qa_native_entry_observer **out, qa_error *error) {
    if (!observer_boundary(instance, error))
        return false;
    if (!callback || !signature || !out || entry < instance->image_base ||
        entry - instance->image_base >= instance->image_bytes ||
        signature->abi != instance->module->info.image.target.abi || signature->variadic ||
        instance->region_depth || instance->write_depth)
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "native entry requires an image address and exact nonvariadic ABI");
    for (qa_native_entry_observer *other = instance->entry_observers; other; other = other->next)
        if (other->address == entry)
            return native_fail(error, QA_ERROR_ARGUMENT, 0, "native entry is already intercepted");
    qa_native_entry_observer *binding = calloc(1, sizeof(*binding));
    if (!binding)
        return native_fail(error, QA_ERROR_MEMORY, 0, "allocating native entry observer");
    binding->instance = instance;
    binding->address = entry;
    binding->callback = callback;
    binding->context = context;
    bool ok = next_id(instance, &binding->id, error) &&
              copy_signature(signature, &binding->signature, error) &&
              (instance->backend == QA_NATIVE_BACKEND_OWNED_PROCESS ?
                  process_entry_bind(binding, error) : native_runner_observer_entry_add(binding, error));
    if (!ok) {
        native_wire_signature_free(&binding->signature);
        guest_abi_plan_destroy(binding->guest_plan);
        free(binding);
        return false;
    }
    binding->next = instance->entry_observers;
    instance->entry_observers = binding;
    *out = binding;
    return true;
}

bool qa_native_unobserve_entry(qa_native_entry_observer *binding, qa_error *error) {
    if (!binding)
        return true;
    qa_native_instance *instance = binding->instance;
    if (!observer_boundary(instance, error))
        return false;
    if (binding->active_calls || instance->region_depth || instance->write_depth)
        return native_fail(error, QA_ERROR_ARGUMENT, 0, "active native entry cannot be removed");
    if (instance->backend == QA_NATIVE_BACKEND_OWNED_PROCESS) {
        if (!qa_native_guest_unbind(instance->guest, binding->guest_id, error)) return false;
    } else if ((!qa_native_terminal(instance) || instance->active_depth || instance->callback_depth) &&
        !native_runner_observer_entry_remove(binding, error))
        return false;
    qa_native_entry_observer **cursor = &instance->entry_observers;
    while (*cursor && *cursor != binding)
        cursor = &(*cursor)->next;
    if (*cursor)
        *cursor = binding->next;
    native_wire_signature_free(&binding->signature);
    guest_abi_plan_destroy(binding->guest_plan);
    free(binding);
    return true;
}

bool qa_native_invoke_original(qa_native_entry_observer *binding,
                               const qa_native_value *arguments, size_t count,
                               qa_native_value *result, qa_error *error) {
    if (!binding || !observer_boundary(binding->instance, error))
        return false;
    qa_native_instance *instance = binding->instance;
    if ((instance->region_depth && (instance->backend != QA_NATIVE_BACKEND_RUNNER ||
                                   instance->region_service_depth != instance->region_depth)) ||
        instance->write_depth ||
        instance->lifecycle != QA_NATIVE_INITIALIZED ||
        count != binding->signature.parameter_count || (count && !arguments))
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "original native invocation requires an application call boundary");
    ++binding->active_calls;
    ++instance->active_depth;
    qa_native_instance *previous = native_active_instance;
    native_active_instance = instance;
    bool ok = instance->backend == QA_NATIVE_BACKEND_OWNED_PROCESS ?
        (instance->process_kind == QA_NATIVE_PROCESS_SYSV ?
            qa_native_sysv_process_invoke_original(instance->sysv_process, binding->guest_id, binding->address,
                &binding->signature, arguments, count, result, error) :
            qa_native_windows_process_invoke_original(instance->windows_process, binding->guest_id, binding->address,
                &binding->signature, arguments, count, result, error)) :
        native_runner_observer_original(binding, arguments, count, result, error);
    native_active_instance = previous;
    --instance->active_depth;
    --binding->active_calls;
    return ok;
}

bool native_process_write_commit(void *context, qa_native_guest *guest,
    const qa_native_guest_commit *commit, qa_error *error) {
    qa_native_instance *instance = context;
    if (!instance || instance->guest != guest || !commit || !commit->bytes || commit->bytes > UINT64_MAX - commit->address)
        return native_fail(error, QA_ERROR_ARGUMENT, 0, "native write receipt lost its actual memory owner");
    size_t count = 0;
    for (qa_native_write_observer *b = instance->write_observers; b; b = b->next) ++count;
    if (count > SIZE_MAX / sizeof(uint64_t)) return native_fail(error, QA_ERROR_MEMORY, count, "native write observer inventory overflows");
    uint64_t *ids = count ? malloc(count * sizeof(*ids)) : NULL;
    if (count && !ids) return native_fail(error, QA_ERROR_MEMORY, count, "retaining actual write publication subscriptions");
    size_t at = 0;
    for (qa_native_write_observer *b = instance->write_observers; b; b = b->next) ids[at++] = b->id;
    bool okay = true;
    for (size_t i = 0; okay && i < count; ++i) {
        qa_native_write_observer *binding = instance->write_observers;
        while (binding && binding->id != ids[i]) binding = binding->next;
        if (!binding) continue;
        uint64_t begin = commit->address > binding->address ? commit->address : binding->address;
        uint64_t end = commit->address + commit->bytes;
        if (end > binding->address + binding->size) end = binding->address + binding->size;
        if (begin < end) {
            uint8_t *before = malloc(binding->size), *after = malloc(binding->size);
            if (!before || !after) {
                free(before); free(after); okay = native_fail(error, QA_ERROR_MEMORY, binding->id, "owning committed watch before/after bytes"); break;
            }
            memcpy(before, binding->snapshot.data, binding->size);
            okay = qa_native_guest_read(guest, binding->address, after, binding->size, error);
            if (okay) {
                qa_native_write_event event = {.instruction = commit->instruction, .address = binding->address,
                    .offset = (size_t)(begin - binding->address), .size = (size_t)(end - begin),
                    .before = {before, binding->size}, .after = {after, binding->size}};
                ++binding->active_calls; ++instance->callback_depth; ++instance->write_depth;
                okay = binding->callback(binding->context, instance, &event, error);
                --instance->write_depth; --instance->callback_depth; --binding->active_calls;
            }
            free(before); free(after);
        }
    }
    if (okay && commit->instruction_last)
        for (qa_native_write_observer *b = instance->write_observers; okay && b; b = b->next)
            okay = qa_native_guest_read(guest, b->address, b->snapshot.data, b->size, error);
    free(ids); return okay;
}

bool qa_native_observe_writes(qa_native_instance *instance, qa_native_address address,
                              size_t bytes, qa_native_write_observer_fn callback, void *context,
                              qa_native_write_observer **out, qa_error *error) {
    if (!observer_boundary(instance, error))
        return false;
    if (!address || !bytes || bytes > NATIVE_HOOK_MAX_WATCH_BYTES ||
        bytes > UINT64_MAX - address || !callback || !out ||
        (instance->backend == QA_NATIVE_BACKEND_RUNNER &&
            (!instance->runner || instance->runner->maximum_frame < 48u ||
             bytes > (instance->runner->maximum_frame - 48u) / 2u)))
        return native_fail(error, QA_ERROR_ARGUMENT, bytes, "native write watch range is invalid");
    qa_native_write_observer *binding = calloc(1, sizeof(*binding));
    if (!binding)
        return native_fail(error, QA_ERROR_MEMORY, 0, "allocating native write observer");
    binding->instance = instance;
    binding->address = address;
    binding->size = bytes;
    binding->callback = callback;
    binding->context = context;
    native_hook_control control = {.operation = NATIVE_HOOK_WATCH_ADD,
                                   .address = address, .size = bytes};
    bool ok = next_id(instance, &binding->id, error);
    control.id = binding->id;
    if (ok && instance->backend == QA_NATIVE_BACKEND_OWNED_PROCESS) {
        binding->snapshot.data = malloc(bytes); binding->snapshot.size = bytes;
        if (!binding->snapshot.data) ok = native_fail(error, QA_ERROR_MEMORY, binding->id, "owning actual write watch snapshot");
        if (ok) ok = qa_native_guest_read(instance->guest, address, binding->snapshot.data, bytes, error);
        if (ok && !instance->process_observing) {
            ok = qa_native_guest_observe(instance->guest, native_process_write_commit, instance, error);
            if (ok) instance->process_observing = true;
        }
    } else if (ok) ok = native_runner_observer_control(instance, control, error);
    if (!ok) {
        qa_buffer_free(&binding->snapshot); free(binding);
        return false;
    }
    binding->next = instance->write_observers;
    instance->write_observers = binding;
    *out = binding;
    return true;
}

bool qa_native_unobserve_writes(qa_native_write_observer *binding, qa_error *error) {
    if (!binding)
        return true;
    qa_native_instance *instance = binding->instance;
    if (!observer_boundary(instance, error))
        return false;
    if (binding->active_calls)
        return native_fail(error, QA_ERROR_ARGUMENT, 0, "active native write watch cannot be removed");
    native_hook_control control = {.operation = NATIVE_HOOK_WATCH_REMOVE, .id = binding->id};
    if (instance->backend != QA_NATIVE_BACKEND_OWNED_PROCESS &&
        (!qa_native_terminal(instance) || instance->active_depth || instance->callback_depth || instance->region_depth || instance->write_depth) &&
        !native_runner_observer_control(instance, control, error))
        return false;
    qa_native_write_observer **cursor = &instance->write_observers;
    while (*cursor && *cursor != binding)
        cursor = &(*cursor)->next;
    if (*cursor)
        *cursor = binding->next;
    qa_buffer_free(&binding->snapshot); free(binding);
    return true;
}

void native_observers_destroy(qa_native_instance *instance) {
    while (instance->entry_observers) {
        qa_native_entry_observer *binding = instance->entry_observers;
        instance->entry_observers = binding->next;
        if (binding->closure)
            ffi_closure_free(binding->closure);
        native_ffi_destroy(&binding->ffi);
        native_wire_signature_free(&binding->signature);
        guest_abi_plan_destroy(binding->guest_plan);
        free(binding);
    }
    while (instance->write_observers) {
        qa_native_write_observer *binding = instance->write_observers;
        instance->write_observers = binding->next;
        qa_buffer_free(&binding->snapshot); free(binding);
    }
}
