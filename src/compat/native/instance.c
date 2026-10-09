#include "internal.h"

#include <math.h>

_Thread_local qa_native_instance *native_active_instance;
bool qa_native_terminal(const qa_native_instance *instance)
{
    return instance && (instance->failed || qa_native_guest_terminal(instance->guest));
}
static void free_allocations(qa_native_instance *);

bool native_instance_setup_identity(qa_native_instance *instance, const qa_native_options *options,
                                    qa_error *error) {
    if ((unsigned)options->q3_role > QA_QVM_UI ||
        (instance->module->info.profile != QA_NATIVE_Q3_VMMAIN &&
         options->q3_role != QA_QVM_GAME))
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "native vmMain role does not match the module profile");
    instance->declaration_ref = options->declaration;
    instance->has_declaration = options->declaration != NULL;
    if (options->declaration) ++((qa_native_declaration *)options->declaration)->references;
    if (native_regions_copy(instance, options->declaration, error)) return true;
    native_regions_destroy(instance);
    return false;
}

static bool q2_profile(qa_native_profile profile) {
    return profile == QA_NATIVE_Q2_GAME_API3 || profile == QA_NATIVE_Q2_GAME_API2023 ||
           profile == QA_NATIVE_Q2_CGAME_API2023;
}

static bool report_latched(qa_native_instance *instance, qa_error *error) {
    if (!instance->failed)
        return true;
    if (error)
        *error = instance->failure;
    instance->failed = false;
    memset(&instance->failure, 0, sizeof(instance->failure));
    return false;
}

static bool create_process(qa_native_module *module, const qa_native_options *options,
    qa_native_instance **out, qa_error *error)
{
    if (!module || !options || !options->process || !out || *out)
        return native_fail(error, QA_ERROR_ARGUMENT, 0, "native process creation needs its actual graph and empty owner");
    if (options->process->defer_host_restore &&
        (!options->process->continuation.size || options->process->previous))
        return native_fail(error, QA_ERROR_ARGUMENT, 0, "deferred host construction requires an independent saved process");
    if ((module->info.profile == QA_NATIVE_Q2_GAME_API2023 ||
         module->info.profile == QA_NATIVE_Q2_CGAME_API2023) &&
        (!options->tick_rate || !isfinite(options->frame_seconds) || options->frame_seconds <= 0 ||
         !options->frame_milliseconds))
        return native_fail(error, QA_ERROR_ARGUMENT, 0, "Q2 API 2023 requires its actual positive tick interval");
    qa_native_instance *instance = calloc(1, sizeof(*instance));
    if (!instance) return native_fail(error, QA_ERROR_MEMORY, 0, "owning native source process");
    instance->module = module; instance->options = *options;
    instance->lifecycle = QA_NATIVE_LOADED;
    if (!native_instance_setup_identity(instance, options, error)) { free(instance); return false; }
    qa_native_module_retain(module);
    qa_native_instance *previous = native_active_instance;
    native_active_instance = instance; instance->active_depth = 1;
    bool okay = options->process->continuation.size ?
        native_process_restore(instance, options->process, error) :
        native_process_open(instance, options->process, error) && native_profile_bind(instance, error);
    instance->active_depth = 0; native_active_instance = previous;
    instance->options.process = NULL;
    instance->options.declaration = NULL;
    if (okay && options->process->continuation.size && options->process->defer_host_restore) {
        instance->process_host_pending = true;
    }
    if (okay && options->process->continuation.size && !instance->process_host_pending) {
        /* The actual host constructor owns this output slot. Publish the
         * provisional address solely to its synchronous HOST decoder so its
         * genuine actor/resource bindings can qualify the saved source state. */
        *out = instance;
        okay = instance->options.restore(instance->options.context,
            (qa_bytes){instance->process_host.data, instance->process_host.size}, error) &&
            native_process_publish(instance, options->process->previous, error);
    }
    if (!instance->process_host_pending) qa_buffer_free(&instance->process_host);
    if (okay && report_latched(instance, error)) { *out = instance; return true; }
    instance->lifecycle = QA_NATIVE_SHUT_DOWN;
    qa_error cleanup = {0};
    if (!native_process_close(instance, &cleanup)) { *out = instance; return false; }
    qa_buffer_free(&instance->process_host);
    native_profile_unbind(instance); free_allocations(instance);
    native_regions_destroy(instance);
    qa_native_module_release(module); free(instance->slots); free(instance); *out = NULL; return false;
}

bool qa_native_create(qa_native_module *module, const qa_native_options *options,
                      qa_native_instance **out, qa_error *error) {
    return create_process(module, options, out, error);
}

qa_native_backend qa_native_get_backend(const qa_native_instance *instance) {
    return instance ? QA_NATIVE_BACKEND_OWNED_PROCESS : QA_NATIVE_BACKEND_NONE;
}

static void free_allocations(qa_native_instance *instance) {
    native_allocation *allocation = instance->allocations;
    while (allocation) {
        native_allocation *next = allocation->next;
        free(allocation);
        allocation = next;
    }
    instance->allocations = NULL;
}

bool qa_native_can_destroy(const qa_native_instance *instance) {
    return instance && !instance->active_depth && !instance->callback_depth &&
        !instance->region_depth && !instance->write_depth &&
        !instance->checkpointing && !instance->destroying && !instance->unloading && !instance->region_scopes &&
        !instance->write_scope && !instance->call_scope;
}

bool qa_native_restart_ready(const qa_native_instance *instance, qa_error *error) {
    if (!instance ||
        (instance->module->info.profile != QA_NATIVE_QUAKE_LIVE_GAME_API10 &&
         (instance->module->info.profile != QA_NATIVE_Q3_VMMAIN || instance->options.q3_role != QA_QVM_GAME)) ||
        (instance->lifecycle != QA_NATIVE_INITIALIZED && instance->lifecycle != QA_NATIVE_RESTART_READY) ||
        instance->active_depth || instance->callback_depth || instance->region_depth ||
        instance->write_depth || instance->write_scope ||
        instance->call_scope || instance->checkpointing ||
        instance->destroying || instance->unloading || instance->failed ||
        instance->pending_shutdown || instance->pending_initialize || instance->pending_restart ||
        instance->entry_observers || instance->write_observers || instance->region_scopes || qa_native_terminal(instance))
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
            "native Q3 restart requires an idle source owner without raw-address observers");
    return true;
}

bool qa_native_restart_original(qa_native_instance *instance, qa_error *error) {
    if (!qa_native_restart_ready(instance, error)) return false;
    if (instance->lifecycle != QA_NATIVE_RESTART_READY || instance->restart_original_ready)
        return native_fail(error, QA_ERROR_ARGUMENT, 0, "native original reload requires consumed restart shutdown");
    for (uint32_t i = 0; i < instance->slot_capacity; ++i)
        if (instance->slots[i].kind != QA_NATIVE_SLOT_FREE || instance->slots[i].actor.registry)
            return native_fail(error, QA_ERROR_ARGUMENT, i, "retire all native canonical slot bindings before reload");
    native_entity_changed(instance, QA_NATIVE_ENTITIES_INVALIDATE, UINT32_MAX);
    qa_native_instance *previous = native_active_instance;
    native_active_instance = instance;
    instance->active_depth = 1;
    instance->lifecycle = QA_NATIVE_SHUT_DOWN;
    instance->unloading = true;
    bool ok = native_process_reload(instance, error);
    instance->unloading = false;
    if (!report_latched(instance, error)) ok = false;
    instance->active_depth = 0;
    native_active_instance = previous;
    if (ok) {
        instance->lifecycle = QA_NATIVE_RESTART_READY;
        instance->restart_original_ready = true;
    }
    return ok;
}

bool qa_native_unloading_owner(const qa_native_instance *instance) {
    return instance && instance->unloading && instance->active_depth && native_active_instance == instance;
}

bool qa_native_destroy_owned(qa_native_instance **owner, qa_error *error) {
    qa_native_instance *instance = owner ? *owner : NULL;
    if (!instance)
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "native instance is required for destruction");
    if (!qa_native_can_destroy(instance))
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "active native instance cannot be destroyed");
    bool completed = true;
    qa_error first = {0}, current = {0};
    if (instance->lifecycle == QA_NATIVE_INITIALIZED && !instance->process_host_pending && !instance->pending_entry_observers && !qa_native_terminal(instance) && !qa_native_shutdown(instance, &current)) {
        completed = false; first = current;
    }
    instance->destroying = true;
    native_entity_changed(instance, QA_NATIVE_ENTITIES_INVALIDATE, UINT32_MAX);
    if (!native_process_close(instance, &current)) {
        instance->destroying = false;
        if (error) *error = completed ? current : first;
        return false;
    }
    if (!report_latched(instance, &current) && completed) { completed = false; first = current; }
    native_profile_unbind(instance);
    free_allocations(instance);
    qa_native_module_release(instance->module);
    free(instance->slots);
    native_regions_destroy(instance);

    native_observers_destroy(instance);
    qa_buffer_free(&instance->process_host);
    memset(instance, 0, sizeof(*instance));
    free(instance);
    *owner = NULL;
    if (!completed && error) *error = first;
    return completed;
}

qa_native_lifecycle qa_native_get_lifecycle(const qa_native_instance *instance) {
    return instance ? instance->lifecycle : QA_NATIVE_SHUT_DOWN;
}

const qa_native_module *qa_native_get_module(const qa_native_instance *instance) {
    return instance ? instance->module : NULL;
}

bool qa_native_active(const qa_native_instance *instance) {
    return instance && instance->active_depth;
}

const qa_native_signature *qa_native_entry_signature(const qa_native_instance *instance,
                                                     const char *name) {
    const native_entry_binding *entry = native_entry(instance, name);
    return entry ? &entry->spec.signature : NULL;
}

bool native_call_binding(qa_native_instance *instance, const native_entry_binding *binding,
                         const qa_native_value *arguments, size_t count, qa_native_value *result,
                         qa_error *error) {
    if (!instance || !binding || !binding->address || instance->process_host_pending ||
        instance->pending_entry_observers)
        return native_fail(error, QA_ERROR_ARGUMENT, 0, "live native entry binding is required");
    bool outer = instance->active_depth == 0;
    if (outer) {
        instance->failed = false;
        memset(&instance->failure, 0, sizeof(instance->failure));
    }
    qa_native_instance *previous = native_active_instance;
    native_active_instance = instance;
    ++instance->active_depth;
    bool ok =
        native_process_invoke(instance, binding->address, &binding->spec.signature,
                              arguments, count, result, error);
    --instance->active_depth;
    native_active_instance = previous;
    if (ok && q2_profile(instance->module->info.profile))
        ok = native_profile_refresh_entities(instance, error);
    if (!report_latched(instance, error))
        ok = false;
    return ok;
}

static bool entry_allowed(qa_native_instance *instance, const native_entry_binding *binding,
                          const qa_native_value *arguments, size_t argument_count,
                          qa_error *error) {
    const char *name = binding->spec.name;
    bool shutdown_command = instance->shutdown_entry && instance->active_depth &&
        instance->callback_depth && native_active_instance == instance &&
        (instance->module->info.profile == QA_NATIVE_Q3_VMMAIN ?
            instance->options.q3_role == QA_QVM_GAME && argument_count &&
            arguments[0].type == QA_NATIVE_I32 && arguments[0].as.i32 == 6 :
            instance->module->info.profile == QA_NATIVE_QUAKE_LIVE_GAME_API10 &&
            !strcmp(name, "ClientCommand"));
    if (instance->lifecycle == QA_NATIVE_SHUT_DOWN && !shutdown_command)
        return native_fail(error, QA_ERROR_ARGUMENT, 0, "native instance is already shut down");
    if (instance->checkpointing || instance->destroying)
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "native instance cannot enter during checkpoint or destruction");
    if (instance->region_depth || instance->write_depth)
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "native instance cannot reenter while an instruction callback is suspended");
    if (shutdown_command) return true;
    if (instance->lifecycle == QA_NATIVE_RESTART_READY) {
        bool restart_init = instance->module->info.profile == QA_NATIVE_Q3_VMMAIN ?
            instance->options.q3_role == QA_QVM_GAME && argument_count > 3 &&
            arguments[0].type == QA_NATIVE_I32 && arguments[0].as.i32 == 0 &&
            arguments[3].type == QA_NATIVE_I32 && arguments[3].as.i32 != 0 :
            instance->module->info.profile == QA_NATIVE_QUAKE_LIVE_GAME_API10 &&
            !strcmp(name, "Init") && argument_count > 2 &&
            arguments[2].type == QA_NATIVE_I32 && arguments[2].as.i32 != 0;
        return (restart_init && instance->restart_original_ready) || native_fail(error, QA_ERROR_ARGUMENT, 0,
            "native Q3 restart requires original reload and matching restart initialization");
    }
    if (instance->module->info.profile == QA_NATIVE_Q3_VMMAIN) {
        if (!argument_count || arguments[0].type != QA_NATIVE_I32)
            return native_fail(error, QA_ERROR_ARGUMENT, 0,
                               "native Q3 vmMain requires a command argument");
        int32_t command = arguments[0].as.i32;
        int32_t init = instance->options.q3_role == QA_QVM_UI ? 1 : 0;
        bool api_query = instance->options.q3_role == QA_QVM_UI && command == 0;
        if (command == init && instance->lifecycle != QA_NATIVE_LOADED)
            return native_fail(error, QA_ERROR_ARGUMENT, 0,
                "native Q3 initialization requires a fresh load or admitted restart");
        if (instance->lifecycle == QA_NATIVE_LOADED && command != init && !api_query)
            return native_fail(error, QA_ERROR_ARGUMENT, 0,
                               "native Q3 module must receive its role's initialization first");
        return true;
    }
    if (instance->module->info.profile == QA_NATIVE_QUAKE_LIVE_GAME_API10) {
        if (!strcmp(name, "RegisterCvars"))
            return instance->lifecycle == QA_NATIVE_LOADED ||
                   native_fail(error, QA_ERROR_ARGUMENT, 0,
                               "Quake Live cvars must be registered once after load");
        if (!strcmp(name, "Init"))
            return instance->lifecycle == QA_NATIVE_PREINITIALIZED ||
                   native_fail(error, QA_ERROR_ARGUMENT, 0,
                               "Quake Live Init requires registered cvars");
        if (!strcmp(name, "Shutdown"))
            return instance->lifecycle == QA_NATIVE_INITIALIZED ||
                   native_fail(error, QA_ERROR_ARGUMENT, 0,
                               "Quake Live Shutdown requires initialized state");
        return instance->lifecycle == QA_NATIVE_INITIALIZED ||
               native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "Quake Live gameplay requires initialized state");
    }
    if (!strcmp(name, "PreInit"))
        return instance->lifecycle == QA_NATIVE_LOADED ||
               native_fail(error, QA_ERROR_ARGUMENT, 0, "native PreInit requires loaded state");
    if (!strcmp(name, "Init"))
        return instance->lifecycle == QA_NATIVE_LOADED ||
               instance->lifecycle == QA_NATIVE_PREINITIALIZED ||
               native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "native Init requires loaded or preinitialized state");
    if (!strcmp(name, "Shutdown"))
        return instance->lifecycle == QA_NATIVE_INITIALIZED ||
               native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "native Shutdown requires initialized state");
    return instance->lifecycle == QA_NATIVE_INITIALIZED ||
           native_fail(error, QA_ERROR_ARGUMENT, 0,
                       "native gameplay entry requires initialized state");
}

bool qa_native_call(qa_native_instance *instance, const char *entry,
                    const qa_native_value *arguments, size_t argument_count,
                    qa_native_value *result, qa_error *error) {
    if (!instance || !entry || (argument_count && !arguments))
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "native instance, entry name and counted arguments are required");
    const qa_native_signature *signature = qa_native_entry_signature(instance, entry);
    const native_entry_binding *binding = native_entry(instance, entry);
    if (!signature) {
        qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "native profile has no entry named %s", entry);
        return false;
    }
    if (!entry_allowed(instance, binding, arguments, argument_count, error))
        return false;
    bool shutdown = instance->module->info.profile == QA_NATIVE_Q3_VMMAIN ?
        argument_count && arguments[0].as.i32 == (instance->options.q3_role == QA_QVM_UI ? 2 : 1) :
        !strcmp(entry, "Shutdown");
    instance->pending_shutdown = shutdown;
    instance->pending_restart = shutdown && (instance->module->info.profile == QA_NATIVE_Q3_VMMAIN ?
        instance->options.q3_role == QA_QVM_GAME && argument_count > 1 &&
        arguments[1].type == QA_NATIVE_I32 && arguments[1].as.i32 != 0 :
        instance->module->info.profile == QA_NATIVE_QUAKE_LIVE_GAME_API10 && argument_count &&
        arguments[0].type == QA_NATIVE_I32 && arguments[0].as.i32 != 0);
    instance->pending_initialize = instance->module->info.profile == QA_NATIVE_Q3_VMMAIN ?
        argument_count && arguments[0].as.i32 == (instance->options.q3_role == QA_QVM_UI ? 1 : 0) :
        instance->module->info.profile == QA_NATIVE_QUAKE_LIVE_GAME_API10 && !strcmp(entry, "Init");
    bool restart_shutdown = instance->pending_restart;
    bool previous_shutdown_entry = instance->shutdown_entry;
    bool called = native_call_binding(instance, binding, arguments, argument_count, result, error);
    instance->shutdown_entry = previous_shutdown_entry;
    instance->pending_shutdown = false;
    instance->pending_restart = instance->pending_initialize = false;
    if (!called)
        return false;
    if (instance->module->info.profile == QA_NATIVE_Q3_VMMAIN) {
        int32_t command = arguments[0].as.i32;
        int32_t init = instance->options.q3_role == QA_QVM_UI ? 1 : 0;
        if (command == init)
            instance->lifecycle = QA_NATIVE_INITIALIZED;
        else if (command == init + 1)
            instance->lifecycle = restart_shutdown ? QA_NATIVE_RESTART_READY : QA_NATIVE_SHUT_DOWN;
    } else if (!strcmp(entry, "PreInit") || !strcmp(entry, "RegisterCvars")) {
        instance->lifecycle = QA_NATIVE_PREINITIALIZED;
    } else if (!strcmp(entry, "Init")) {
        instance->lifecycle = QA_NATIVE_INITIALIZED;
    } else if (!strcmp(entry, "Shutdown")) {
        instance->lifecycle = restart_shutdown ? QA_NATIVE_RESTART_READY : QA_NATIVE_SHUT_DOWN;
    }
    return true;
}

bool qa_native_initialize(qa_native_instance *instance, qa_error *error) {
    if (!instance)
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "native instance is required for initialization");
    switch (instance->module->info.profile) {
    case QA_NATIVE_Q2_GAME_API2023:
        if (!qa_native_call(instance, "PreInit", NULL, 0, NULL, error))
            return false;
        return qa_native_call(instance, "Init", NULL, 0, NULL, error);
    case QA_NATIVE_Q2_GAME_API3:
    case QA_NATIVE_Q2_CGAME_API2023:
        return qa_native_call(instance, "Init", NULL, 0, NULL, error);
    case QA_NATIVE_Q3_VMMAIN:
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "Q3 initialization requires GAME_INIT arguments through vmMain");
    case QA_NATIVE_QUAKE_LIVE_GAME_API10:
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "Quake Live initialization requires level time, random seed and "
                           "restart through qa_native_ql_initialize");
    }
    return false;
}

bool qa_native_shutdown(qa_native_instance *instance, qa_error *error) {
    if (!instance)
        return native_fail(error, QA_ERROR_ARGUMENT, 0, "native instance is required for shutdown");
    if (instance->lifecycle == QA_NATIVE_SHUT_DOWN)
        return true;
    if (instance->lifecycle != QA_NATIVE_INITIALIZED)
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "native shutdown requires initialized state");
    if (instance->module->info.profile == QA_NATIVE_Q3_VMMAIN) {
        qa_native_value arguments[13] = {{0}};
        for (size_t index = 0; index < 13; ++index)
            arguments[index].type = QA_NATIVE_I32;
        arguments[0].as.i32 = instance->options.q3_role == QA_QVM_UI ? 2 : 1;
        qa_native_value result = {0};
        return qa_native_call(instance, "vmMain", arguments, 13, &result, error);
    }
    if (instance->module->info.profile == QA_NATIVE_QUAKE_LIVE_GAME_API10)
        return qa_native_ql_shutdown(instance, false, error);
    return qa_native_call(instance, "Shutdown", NULL, 0, NULL, error);
}

bool qa_native_ql_register_cvars(qa_native_instance *instance, qa_error *error) {
    if (!instance || instance->module->info.profile != QA_NATIVE_QUAKE_LIVE_GAME_API10)
        return native_fail(error, QA_ERROR_ARGUMENT, 0, "Quake Live API 10 instance is required");
    return qa_native_call(instance, "RegisterCvars", NULL, 0, NULL, error);
}

bool qa_native_ql_initialize(qa_native_instance *instance, int32_t level_time, int32_t random_seed,
                             bool restart, qa_error *error) {
    if (!instance || instance->module->info.profile != QA_NATIVE_QUAKE_LIVE_GAME_API10)
        return native_fail(error, QA_ERROR_ARGUMENT, 0, "Quake Live API 10 instance is required");
    qa_native_value arguments[] = {{.type = QA_NATIVE_I32, .as.i32 = level_time},
                                   {.type = QA_NATIVE_I32, .as.i32 = random_seed},
                                   {.type = QA_NATIVE_I32, .as.i32 = restart ? 1 : 0}};
    return qa_native_call(instance, "Init", arguments, sizeof(arguments) / sizeof(arguments[0]),
                          NULL, error);
}

bool qa_native_ql_shutdown(qa_native_instance *instance, bool restart, qa_error *error) {
    if (!instance || instance->module->info.profile != QA_NATIVE_QUAKE_LIVE_GAME_API10)
        return native_fail(error, QA_ERROR_ARGUMENT, 0, "Quake Live API 10 instance is required");
    qa_native_value argument = {.type = QA_NATIVE_I32, .as.i32 = restart ? 1 : 0};
    return qa_native_call(instance, "Shutdown", &argument, 1, NULL, error);
}

bool qa_native_export(const qa_native_instance *instance, const char *name, qa_native_address *out,
                      qa_error *error) {
    if (!instance || instance->destroying)
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "native instance is required for export lookup");
    return native_process_export(instance, name, out, error);
}

bool qa_native_entry_address(const qa_native_instance *instance, const char *name,
                             qa_native_address *out, qa_error *error) {
    if (!instance || !name || !out || instance->destroying || instance->unloading)
        return native_fail(error, QA_ERROR_ARGUMENT, 0, "native source entry and live instance are required");
    const native_entry_binding *entry = native_entry(instance, name);
    if (!entry || !entry->address)
        return native_fail(error, QA_ERROR_NOT_FOUND, 0, "native source API callback is unavailable");
    *out = entry->address;
    return true;
}

bool qa_native_restore_ready(const qa_native_instance *instance, qa_error *error) {
    if (!instance || instance->lifecycle != QA_NATIVE_INITIALIZED || instance->active_depth ||
        instance->callback_depth || instance->region_depth || instance->write_depth || instance->write_scope ||
        instance->call_scope ||
        instance->checkpointing || instance->destroying || instance->unloading ||
        instance->failed || instance->pending_shutdown || instance->entry_observers || instance->write_observers ||
        instance->region_scopes)
        return native_fail(error, QA_ERROR_ARGUMENT, 0, "native private restore requires idle detached source observers");
    for (size_t i = 0; i < instance->region_count; ++i)
        if (instance->regions[i].first)
            return native_fail(error, QA_ERROR_ARGUMENT, i, "native private restore requires detached source regions");
    return true;
}

bool qa_native_rva(const qa_native_instance *instance, uint64_t rva, size_t bytes,
                   qa_native_address *out, qa_error *error) {
    if (!instance || !out || rva > instance->image_bytes || bytes > instance->image_bytes - rva ||
        rva > UINT64_MAX - instance->image_base)
        return native_fail(error, QA_ERROR_ARGUMENT, (size_t)rva,
                           "native RVA range is outside the loaded image");
    *out = instance->image_base + rva;
    return true;
}

bool qa_native_invoke(qa_native_instance *instance, qa_native_address entry,
                      const qa_native_signature *signature, const qa_native_value *arguments,
                      size_t argument_count, qa_native_value *result, qa_error *error) {
    bool entered = false;
    return qa_native_invoke_receipt(instance, entry, signature, arguments,
        argument_count, result, &entered, error);
}

bool qa_native_invoke_receipt(qa_native_instance *instance, qa_native_address entry,
    const qa_native_signature *signature, const qa_native_value *arguments,
    size_t argument_count, qa_native_value *result, bool *entered, qa_error *error) {
    if (!entered) return native_fail(error, QA_ERROR_ARGUMENT, 0, "native invocation requires its entered receipt");
    *entered = false;
    bool write_call=instance&&instance->write_scope&&
        instance->write_scope->event==instance->active_write_event&&
        instance->write_scope->depth==instance->write_depth&&
        instance->write_scope->invocation_depth==instance->active_depth;
    if (!instance || !entry || !signature || instance->lifecycle != QA_NATIVE_INITIALIZED ||
        instance->checkpointing || instance->destroying ||
        (instance->region_depth && !write_call) ||
        (instance->write_depth && !write_call))
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "initialized native instance and declared entry are required");
    bool *previous = instance->invoke_entered;
    instance->invoke_entered = entered;
    bool okay = native_invoke_entry(instance, entry, signature, arguments, argument_count, result, error);
    instance->invoke_entered = previous;
    return okay;
}

bool native_invoke_entry(qa_native_instance *instance, qa_native_address entry,
                         const qa_native_signature *signature, const qa_native_value *arguments,
                         size_t argument_count, qa_native_value *result, qa_error *error) {
    const native_entry_binding binding = {
        .spec = {.name = "declared-source-entry", .signature = *signature}, .address = entry};
    return native_call_binding(instance, &binding, arguments, argument_count, result, error);
}
