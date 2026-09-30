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
    if (instance->backend != QA_NATIVE_BACKEND_RUNNER ||
        (!instance->options.observe && !instance->region_count))
        return native_fail(error, QA_ERROR_UNSUPPORTED, 0,
                           "native observation requires an instrumented runner");
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
              native_runner_observer_entry_add(binding, error);
    if (!ok) {
        native_wire_signature_free(&binding->signature);
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
    if ((!qa_native_terminal(instance) || instance->active_depth || instance->callback_depth) &&
        !native_runner_observer_entry_remove(binding, error))
        return false;
    qa_native_entry_observer **cursor = &instance->entry_observers;
    while (*cursor && *cursor != binding)
        cursor = &(*cursor)->next;
    if (*cursor)
        *cursor = binding->next;
    native_wire_signature_free(&binding->signature);
    free(binding);
    return true;
}

bool qa_native_invoke_original(qa_native_entry_observer *binding,
                               const qa_native_value *arguments, size_t count,
                               qa_native_value *result, qa_error *error) {
    if (!binding || !observer_boundary(binding->instance, error))
        return false;
    qa_native_instance *instance = binding->instance;
    if (instance->region_depth || instance->write_depth ||
        instance->lifecycle != QA_NATIVE_INITIALIZED ||
        count != binding->signature.parameter_count || (count && !arguments))
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "original native invocation requires an application call boundary");
    ++binding->active_calls;
    ++instance->active_depth;
    qa_native_instance *previous = native_active_instance;
    native_active_instance = instance;
    bool ok = native_runner_observer_original(binding, arguments, count, result, error);
    native_active_instance = previous;
    --instance->active_depth;
    --binding->active_calls;
    return ok;
}

bool qa_native_observe_writes(qa_native_instance *instance, qa_native_address address,
                              size_t bytes, qa_native_write_observer_fn callback, void *context,
                              qa_native_write_observer **out, qa_error *error) {
    if (!observer_boundary(instance, error))
        return false;
    if (!address || !bytes || bytes > NATIVE_HOOK_MAX_WATCH_BYTES ||
        bytes > UINT64_MAX - address || !callback || !out ||
        !instance->runner || instance->runner->maximum_frame < 48u ||
        bytes > (instance->runner->maximum_frame - 48u) / 2u)
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
    if (ok)
        ok = native_runner_observer_control(instance, control, error);
    if (!ok) {
        free(binding);
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
    if ((!qa_native_terminal(instance) || instance->active_depth || instance->callback_depth || instance->region_depth || instance->write_depth) &&
        !native_runner_observer_control(instance, control, error))
        return false;
    qa_native_write_observer **cursor = &instance->write_observers;
    while (*cursor && *cursor != binding)
        cursor = &(*cursor)->next;
    if (*cursor)
        *cursor = binding->next;
    free(binding);
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
        free(binding);
    }
    while (instance->write_observers) {
        qa_native_write_observer *binding = instance->write_observers;
        instance->write_observers = binding->next;
        free(binding);
    }
}
