#include "internal.h"

#include <math.h>

_Thread_local qa_native_instance *native_active_instance;
static void free_allocations(qa_native_instance *);

static bool exact_target(qa_native_target left, qa_native_target right) {
    return left.os == right.os && left.arch == right.arch && left.abi == right.abi &&
           left.pointer_bytes == right.pointer_bytes;
}

void native_original_dependencies_destroy(qa_native_instance *instance) {
    for (size_t i = 0; i < instance->original_dependency_count; ++i) {
        free((void *)instance->original_dependencies[i].path);
        free((void *)instance->original_dependencies[i].bytes.data);
        free(instance->original_dependency_sonames ? instance->original_dependency_sonames[i] : NULL);
    }
    free(instance->original_dependencies);
    free(instance->original_dependency_sonames);
    instance->original_dependencies = NULL;
    instance->original_dependency_sonames = NULL;
    instance->original_dependency_count = 0;
}

static bool original_dependencies_copy(qa_native_instance *instance,
    const qa_native_options *options, qa_error *error) {
    if (!options->dependency_count) return true;
    instance->original_dependencies = calloc(options->dependency_count,
        sizeof(*instance->original_dependencies));
    instance->original_dependency_sonames = calloc(options->dependency_count,
        sizeof(*instance->original_dependency_sonames));
    if (!instance->original_dependencies || !instance->original_dependency_sonames) {
        free(instance->original_dependencies); free(instance->original_dependency_sonames);
        instance->original_dependencies = NULL; instance->original_dependency_sonames = NULL;
        return native_fail(error, QA_ERROR_MEMORY, 0, "retaining original native dependencies");
    }
    instance->original_dependency_count = options->dependency_count;
    for (size_t i = 0; i < options->dependency_count; ++i) {
        const qa_native_dependency *source = &options->dependencies[i];
        if (!source->path || (!source->bytes.data && source->bytes.size)) {
            native_original_dependencies_destroy(instance);
            return native_fail(error, QA_ERROR_ARGUMENT, i, "native dependency requires its original path and bytes");
        }
        char *path = native_strdup(source->path, error);
        uint8_t *bytes = malloc(source->bytes.size ? source->bytes.size : 1u);
        if (!path || !bytes) {
            free(path); free(bytes);
            native_original_dependencies_destroy(instance);
            return native_fail(error, QA_ERROR_MEMORY, i, "retaining original native dependency bytes");
        }
        if (source->bytes.size) memcpy(bytes, source->bytes.data, source->bytes.size);
        instance->original_dependencies[i] = (qa_native_dependency){path, {bytes, source->bytes.size}};
        qa_bytes soname = {0};
        if (!native_image_soname(instance->original_dependencies[i].bytes, &soname, error)) {
            native_original_dependencies_destroy(instance);
            return false;
        }
        if (soname.data) {
            instance->original_dependency_sonames[i] = native_strdup((const char *)soname.data, error);
            if (!instance->original_dependency_sonames[i]) {
                native_original_dependencies_destroy(instance);
                return false;
            }
        }
    }
    return true;
}

bool native_instance_setup_identity(qa_native_instance *instance, const qa_native_options *options,
                                    qa_error *error) {
    if ((unsigned)options->q3_role > QA_QVM_UI ||
        (instance->module->info.profile != QA_NATIVE_Q3_VMMAIN &&
         options->q3_role != QA_QVM_GAME))
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "native vmMain role does not match the module profile");
    if (options->dependency_count && !options->dependencies)
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "native dependency count requires dependency records");
    const qa_sha256_digest *declared =
        options->declaration ? &options->declaration->digest : options->declaration_digest;
    if (options->declaration && options->declaration_digest &&
        !qa_sha256_equal(&options->declaration->digest, options->declaration_digest))
        return native_fail(error, QA_ERROR_FORMAT, 0, "native declaration digest options disagree");
    if (declared) {
        instance->declaration = *declared;
        instance->has_declaration = true;
    }
    if (!original_dependencies_copy(instance, options, error)) return false;
    if (native_regions_copy(instance, options->declaration, error)) return true;
    native_original_dependencies_destroy(instance);
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

bool qa_native_create_direct(qa_native_module *module, const qa_native_options *options,
                             qa_native_instance **out, qa_error *error) {
    if (!module || !options || !out || *out)
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "native module, options and output are required");
    if (options->isolate)
        return native_fail(error, QA_ERROR_UNSUPPORTED, 0,
                           "native source isolation requires the runner backend");
    if (options->observe || (options->declaration && options->declaration->region_count))
        return native_fail(error, QA_ERROR_UNSUPPORTED, 0,
                           "native regions and observers require the instrumented runner backend");
    if ((module->info.profile == QA_NATIVE_Q3_VMMAIN) &&
        (!options->describe_syscall || !options->syscall))
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "native Q3 requires syscall description and dispatch");
    if (module->info.profile == QA_NATIVE_QUAKE_LIVE_GAME_API10 && !options->import)
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "Quake Live API 10 requires engine import dispatch");
    if ((module->info.profile == QA_NATIVE_Q2_GAME_API2023 ||
         module->info.profile == QA_NATIVE_Q2_CGAME_API2023) &&
        (!options->tick_rate || !isfinite(options->frame_seconds) ||
         options->frame_seconds <= 0.0f || !options->frame_milliseconds))
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "Q2 API 2023 requires a positive tick interval");
    qa_native_instance *instance = calloc(1, sizeof(*instance));
    if (!instance)
        return native_fail(error, QA_ERROR_MEMORY, 0, "allocating native module instance");
    instance->module = module;
    instance->options = *options;
    instance->backend = QA_NATIVE_BACKEND_DIRECT;
    if (!native_instance_setup_identity(instance, options, error)) {
        free(instance);
        return false;
    }
    instance->options.declaration = NULL;
    instance->options.declaration_digest = NULL;
    instance->lifecycle = QA_NATIVE_LOADED;
    qa_native_module_retain(module);
    if (!native_direct_open(instance, error))
        goto fail;
    instance->options.dependencies = NULL;
    instance->options.dependency_count = 0;
    qa_native_instance *previous = native_active_instance;
    native_active_instance = instance;
    instance->active_depth = 1;
    bool bound = native_profile_bind(instance, error);
    instance->active_depth = 0;
    native_active_instance = previous;
    if (!bound || !report_latched(instance, error))
        goto fail;
    *out = instance;
    return true;

fail: {
    qa_native_instance *previous_close = native_active_instance;
    instance->destroying = instance->unloading = true;
    native_active_instance = instance; instance->active_depth = 1;
    qa_error unload_error = {0};
    bool unloaded = native_direct_unload(instance, &unload_error);
    instance->active_depth = 0; native_active_instance = previous_close;
    instance->unloading = false;
    if (!unloaded) {
        instance->destroying = false;
        instance->lifecycle = QA_NATIVE_SHUT_DOWN;
        qa_error callback_error = {0};
        bool callbacks_ok = report_latched(instance, &callback_error);
        *out = instance;
        if (error && error->code == QA_OK)
            *error = callbacks_ok ? unload_error : callback_error;
        return false;
    }
    native_direct_close(instance);
    native_profile_unbind(instance);
    free_allocations(instance);
    qa_native_module_release(instance->module);
    free(instance->slots);
    native_regions_destroy(instance);
    native_original_dependencies_destroy(instance);
    free(instance);
    return false;
}
}

bool qa_native_create(qa_native_module *module, const qa_native_options *options,
                      const qa_native_runner_config *runner, qa_native_instance **out,
                      qa_error *error) {
    if (!module)
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "native module is required for backend selection");
    bool instrumented = options && (options->observe ||
                        (options->declaration && options->declaration->region_count));
    bool isolated = options && options->isolate;
    return exact_target(module->info.image.target, qa_native_host_target()) && !instrumented && !isolated
               ? qa_native_create_direct(module, options, out, error)
               : qa_native_create_runner(module, options, runner, out, error);
}

qa_native_backend qa_native_get_backend(const qa_native_instance *instance) {
    return instance ? instance->backend : QA_NATIVE_BACKEND_DIRECT;
}

static void free_allocations(qa_native_instance *instance) {
    native_allocation *allocation = instance->allocations;
    while (allocation) {
        native_allocation *next = allocation->next;
        free(allocation->bytes);
        free(allocation);
        allocation = next;
    }
    instance->allocations = NULL;
}

bool qa_native_can_destroy(const qa_native_instance *instance) {
    return instance && !instance->active_depth && !instance->callback_depth &&
        !instance->checkpointing && !instance->destroying;
}

static bool instrumented_image(const qa_native_instance *instance, uint64_t *generation,
    qa_error *error) {
    if (!instance->instrumented_child) { *generation = 0; return true; }
    native_hook_control image = {.operation = NATIVE_HOOK_IMAGE};
    if (!native_hooks_control(&image) || image.address != instance->image_base ||
        image.size != instance->image_bytes || !image.id)
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
            "native restart requires its actual idle instrumented original image");
    *generation = image.id;
    return true;
}

bool qa_native_restart_ready(const qa_native_instance *instance, qa_error *error) {
    if (!instance ||
        (instance->module->info.profile != QA_NATIVE_QUAKE_LIVE_GAME_API10 &&
         (instance->module->info.profile != QA_NATIVE_Q3_VMMAIN || instance->options.q3_role != QA_QVM_GAME)) ||
        (instance->lifecycle != QA_NATIVE_INITIALIZED && instance->lifecycle != QA_NATIVE_RESTART_READY) ||
        instance->active_depth || instance->callback_depth || instance->region_depth ||
        instance->region_service_depth || instance->write_depth || instance->checkpointing ||
        instance->destroying || instance->unloading || instance->failed ||
        instance->pending_shutdown || instance->pending_initialize || instance->pending_restart ||
        instance->entry_observers || instance->write_observers || qa_native_terminal(instance))
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
            "native Q3 restart requires an idle source owner without raw-address observers");
    uint64_t generation;
    return instrumented_image(instance, &generation, error);
}

bool qa_native_restart_original(qa_native_instance *instance, qa_error *error) {
    if (!qa_native_restart_ready(instance, error)) return false;
    if (instance->lifecycle != QA_NATIVE_RESTART_READY || instance->restart_original_ready)
        return native_fail(error, QA_ERROR_ARGUMENT, 0, "native original reload requires consumed restart shutdown");
    for (uint32_t i = 0; i < instance->slot_capacity; ++i)
        if (instance->slots[i].kind != QA_NATIVE_SLOT_FREE || instance->slots[i].actor.registry)
            return native_fail(error, QA_ERROR_ARGUMENT, i, "retire all native canonical slot bindings before reload");
    uint64_t image_generation;
    if (!instrumented_image(instance, &image_generation, error)) return false;
    if (instance->instrumented_child && image_generation > UINT64_MAX - 2)
        return native_fail(error, QA_ERROR_ARGUMENT, 0, "native original image generation is exhausted");
    qa_native_instance *previous = native_active_instance;
    native_active_instance = instance;
    instance->active_depth = 1;
    instance->lifecycle = QA_NATIVE_SHUT_DOWN;
    bool ok;
    if (instance->backend == QA_NATIVE_BACKEND_RUNNER) {
        ok = native_runner_restart_original(instance, error);
    } else {
        qa_error unload_error = {0};
        instance->unloading = true;
        ok = native_direct_unload(instance, &unload_error);
        instance->unloading = false;
        if (ok) {
            native_direct_close(instance);
            native_profile_unbind(instance);
            instance->entities = (qa_native_entity_table){0};
            if (instance->slot_capacity)
                memset(instance->slots, 0, (size_t)instance->slot_capacity * sizeof(*instance->slots));
            ok = report_latched(instance, error);
            if (ok) ok = native_direct_open(instance, error);
            if (ok && instance->instrumented_child) {
                uint64_t refreshed_generation;
                ok = instrumented_image(instance, &refreshed_generation, error);
                if (ok && refreshed_generation != image_generation + 2)
                    ok = native_fail(error, QA_ERROR_ARGUMENT, 0,
                        "instrumented original native image did not genuinely unload and reload");
            }
            if (ok) ok = native_profile_bind(instance, error);
            if (!report_latched(instance, error)) ok = false;
        } else {
            qa_error callback_error = {0};
            bool callbacks_ok = report_latched(instance, &callback_error);
            if (error) *error = callbacks_ok ? unload_error : callback_error;
        }
    }
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
    if (instance->lifecycle == QA_NATIVE_INITIALIZED && !qa_native_terminal(instance) && !qa_native_shutdown(instance, &current)) {
        completed = false; first = current;
    }
    instance->destroying = true;
    if (instance->backend == QA_NATIVE_BACKEND_DIRECT) {
        qa_native_instance *previous = native_active_instance;
        instance->failed = false; instance->failure = (qa_error){0};
        instance->unloading = true; instance->active_depth = 1;
        native_active_instance = instance;
        bool unloaded = native_direct_unload(instance, &current);
        instance->active_depth = 0; native_active_instance = previous;
        instance->unloading = false;
        if (!unloaded) {
            instance->destroying = false;
            qa_error callback_error = {0};
            bool callbacks_ok = report_latched(instance, &callback_error);
            if (error) *error = completed ? (callbacks_ok ? current : callback_error) : first;
            return false;
        }
        native_direct_close(instance);
        if (!report_latched(instance, &current) && completed) { completed = false; first = current; }
        native_profile_unbind(instance);
    } else {
        qa_native_instance *previous = native_active_instance;
        instance->unloading = true; instance->active_depth = 1;
        native_active_instance = instance;
        if (!native_runner_close(instance, &current) && completed) { completed = false; first = current; }
        instance->active_depth = 0; native_active_instance = previous;
        instance->unloading = false;
        if (instance->runner) {
            instance->destroying = false;
            if (error) *error = first;
            return false;
        }
        native_profile_unbind(instance);
    }
    free_allocations(instance);
    qa_native_module_release(instance->module);
    free(instance->slots);
    native_regions_destroy(instance);
    native_original_dependencies_destroy(instance);
    native_observers_destroy(instance);
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
    if (!instance || !binding || !binding->address)
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
        native_ffi_call(instance, binding->address, &binding->spec.signature,
                        (native_ffi_signature *)&binding->ffi, arguments, count, result, error);
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
    bool called;
    if (instance->backend == QA_NATIVE_BACKEND_DIRECT) {
        called = native_call_binding(instance, binding, arguments, argument_count, result, error);
    } else {
        qa_native_instance *previous = native_active_instance;
        native_active_instance = instance;
        ++instance->active_depth;
        called = native_runner_call(instance, entry, arguments, argument_count, result, error);
        --instance->active_depth;
        native_active_instance = previous;
    }
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
    return instance->backend == QA_NATIVE_BACKEND_DIRECT
               ? native_direct_export(instance, name, out, error)
               : native_runner_export((qa_native_instance *)instance, name, out, error);
}

bool qa_native_entry_address(const qa_native_instance *instance, const char *name,
                             qa_native_address *out, qa_error *error) {
    if (!instance || !name || !out || instance->destroying || instance->unloading)
        return native_fail(error, QA_ERROR_ARGUMENT, 0, "native source entry and live instance are required");
    if (instance->backend == QA_NATIVE_BACKEND_RUNNER)
        return native_runner_entry_address((qa_native_instance *)instance, name, out, error);
    const native_entry_binding *entry = native_entry(instance, name);
    if (!entry || !entry->address)
        return native_fail(error, QA_ERROR_NOT_FOUND, 0, "native source API callback is unavailable");
    *out = entry->address;
    return true;
}

bool qa_native_restore_ready(const qa_native_instance *instance, qa_error *error) {
    if (!instance || instance->lifecycle != QA_NATIVE_INITIALIZED || instance->active_depth ||
        instance->callback_depth || instance->region_depth || instance->write_depth ||
        instance->checkpointing || instance->destroying || instance->unloading ||
        instance->failed || instance->pending_shutdown || instance->entry_observers || instance->write_observers)
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
    if (!instance || !entry || !signature || instance->lifecycle != QA_NATIVE_INITIALIZED ||
        instance->checkpointing || instance->destroying ||
        (instance->region_depth && (instance->backend != QA_NATIVE_BACKEND_RUNNER ||
                                   instance->region_service_depth != instance->region_depth)) ||
        instance->write_depth)
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "initialized native instance and declared entry are required");
    if (instance->backend == QA_NATIVE_BACKEND_RUNNER) {
        qa_native_instance *previous = native_active_instance;
        native_active_instance = instance;
        ++instance->active_depth;
        bool ok = native_runner_invoke(instance, entry, signature, arguments, argument_count,
                                       result, error);
        --instance->active_depth;
        native_active_instance = previous;
        return ok;
    }
    native_entry_binding binding = {
        .spec = {.name = "declared-source-entry", .signature = *signature}, .address = entry};
    if (!native_ffi_prepare(&binding.ffi, signature, error))
        return false;
    bool ok = native_call_binding(instance, &binding, arguments, argument_count, result, error);
    native_ffi_destroy(&binding.ffi);
    return ok;
}
