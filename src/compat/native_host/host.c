#include "internal.h"

static void free_records(qa_native_host *);

static bool release_process(qa_native_host *host, qa_error *error)
{
    return qa_native_process_resources_release(&host->owned_process, error) &&
        qa_native_process_platform_release(&host->owned_platform, error);
}

static const native_host_classic_layout classic32 = {
    .edict = {.bytes = 260, .inuse = 88, .linkcount = 92,
              .area = 176, .area2 = 180, .flags = 184,
              .mins = 188, .maxs = 200, .absmin = 212, .absmax = 224,
              .size = 236, .solid = 248, .clipmask = 252, .owner = 256,
              .cluster_count = 104, .clusters = 108, .headnode = 172},
    .trace = {.bytes = 56, .surface = 44, .contents = 48, .entity = 52},
    .pmove = {.bytes = 240, .touches = 52, .view_angles = 180, .view_height = 192,
              .mins = 196, .maxs = 208, .ground = 220, .water_type = 224,
              .water_level = 228},
    .cvar = {.bytes = 28, .string = 4, .latched = 8, .flags = 12,
             .modified = 16, .value = 20, .next = 24}};

static const native_host_classic_layout classic64 = {
    .edict = {.bytes = 280, .inuse = 96, .linkcount = 100,
              .area = 192, .area2 = 196, .flags = 200,
              .mins = 204, .maxs = 216, .absmin = 228, .absmax = 240,
              .size = 252, .solid = 264, .clipmask = 268, .owner = 272,
              .cluster_count = 120, .clusters = 124, .headnode = 188},
    .trace = {.bytes = 72, .surface = 48, .contents = 56, .entity = 64},
    .pmove = {.bytes = 384, .touches = 56, .view_angles = 312, .view_height = 324,
              .mins = 328, .maxs = 340, .ground = 352, .water_type = 360,
              .water_level = 364},
    .cvar = {.bytes = 48, .string = 8, .latched = 16, .flags = 24,
             .modified = 28, .value = 32, .next = 40}};

static const native_host_edict_layout rerelease_edict = {
    .bytes = NATIVE_Q2_RR_EDICT_BYTES, .inuse = NATIVE_Q2_RR_INUSE,
    .linkcount = NATIVE_Q2_RR_LINKCOUNT, .area = NATIVE_Q2_RR_AREANUM,
    .area2 = NATIVE_Q2_RR_AREANUM2, .flags = NATIVE_Q2_RR_SVFLAGS,
    .mins = NATIVE_Q2_RR_MINS, .maxs = NATIVE_Q2_RR_MAXS,
    .absmin = NATIVE_Q2_RR_ABSMIN, .absmax = NATIVE_Q2_RR_ABSMAX,
    .size = NATIVE_Q2_RR_SIZE, .solid = NATIVE_Q2_RR_SOLID,
    .clipmask = NATIVE_Q2_RR_CLIPMASK, .owner = NATIVE_Q2_RR_OWNER};

static bool profile_is_q2_game(qa_native_profile profile)
{
    return profile == QA_NATIVE_Q2_GAME_API3 || profile == QA_NATIVE_Q2_GAME_API2023;
}

static void entity_changed(void *context, qa_native_instance *instance,
                            const qa_native_entity_event *event)
{
    qa_native_host *host = context;
    if (host->kind == NATIVE_HOST_Q3 && host->q3_role == QA_QVM_GAME) {
        if (event->change == QA_NATIVE_ENTITIES_INVALIDATE && host->q3.entity_changed)
            host->q3.entity_changed(host->q3.context, event);
        return;
    }
    if (host->kind != NATIVE_HOST_Q2_GAME) return;
    bool bootstrap = host->instance == NULL;
    if (bootstrap) host->instance = instance;
    if (event->change == QA_NATIVE_ENTITIES_INVALIDATE) {
        for (uint32_t slot = 1; slot < host->q2_field_capacity; ++slot) {
            uint64_t address = host->q2_references.base +
                (uint64_t)slot * host->q2_references.stride;
            if (event->address && (address >= event->address + event->bytes ||
                event->address >= address + host->q2_references.stride)) continue;
            host->q2_fields[slot].body = (qa_entity_body_fields){0};
            host->q2_fields[slot].collision = (qa_entity_collision_fields){.family = QA_COLLISION_Q2};
            host->q2_fields_invalidated = true;
        }
        if (!event->address) host->q2_references = (qa_entity_references){0};
    } else {
        qa_error error = {0};
        bool ok = native_host_fields_refresh(host, &error);
        if (ok && event->change == QA_NATIVE_ENTITIES_SLOT &&
            event->slot < host->q2_field_capacity) {
            host->q2_fields[event->slot] = (native_host_q2_fields){0};
            host->q2_fields[event->slot].collision.family = QA_COLLISION_Q2;
            ok = native_host_fields_bind(host, event->slot, &error);
        }
        if (!ok && !host->q2_fields_reported && host->engine.print) {
            qa_native_host_print message = {.kind = QA_NATIVE_HOST_PRINT_DEBUG,
                .text = error.message};
            host->engine.print(host->engine.context, &message);
            host->q2_fields_reported = true;
        }
    }
    if (bootstrap) host->instance = NULL;
}

static void module_retain(void *context)
{ qa_native_module_retain(context); }

static void module_release(void *context)
{ qa_native_module_release(context); }

static bool create_sdk_process(qa_native_host *host, qa_native_module *module,
    const qa_native_host_instance_options *options, qa_native_process_options *process,
    qa_error *error)
{
    qa_native_module_info info = qa_native_module_describe(module);
    qa_native_target target = info.image.target, controller = qa_native_host_target();
    bool emulated = target.arch == QA_NATIVE_ARCH_I386 ||
        controller.os != QA_NATIVE_OS_LINUX || controller.arch != QA_NATIVE_ARCH_X86_64;
    size_t count = options->dependency_count + 1;
    if (!count || count > SIZE_MAX / sizeof(qa_native_process_resource_artifact) ||
        (options->dependency_count && !options->dependencies))
        return native_host_fail(error, QA_ERROR_ARGUMENT, options->dependency_count,
            "Native SDK dependencies require their actual path and byte rows");
    qa_native_process_resource_artifact *artifacts = calloc(count, sizeof(*artifacts));
    if (!artifacts) return native_host_fail(error, QA_ERROR_MEMORY, count,
        "Retaining native SDK artifact order");
    for (size_t i = 0; i < options->dependency_count; ++i)
        artifacts[i] = (qa_native_process_resource_artifact){
            .path = options->dependencies[i].path, .bytes = options->dependencies[i].bytes};
    qa_native_process_resource_owner authority = {.context = module,
        .retain = module_retain, .release = module_release};
    artifacts[options->dependency_count] = (qa_native_process_resource_artifact){
        .path = info.source, .bytes = qa_native_module_bytes(module), .owner = authority};
    qa_native_process_environment environment = {0};
    qa_native_runtime *runtime = NULL;
    qa_native_process_resource_root *roots = NULL;
    size_t root_count = 0;
    uint64_t identity = (uint64_t)(uintptr_t)host;
    qa_native_process_platform_options platform = {.id = identity, .standard_ids = {1, 2, 3}};
    bool ok = qa_native_process_environment_acquire(&environment, error) &&
        qa_native_process_platform_create(&platform, &host->owned_platform, error) &&
        qa_native_runtime_create(&(qa_native_runtime_options){
            .executable_directory = environment.executable_directory,
            .overrides = options->runtime}, &runtime, error);
    if (ok) {
        roots = calloc(environment.root_count + 2, sizeof(*roots));
        ok = roots != NULL;
        if (!ok) native_host_fail(error, QA_ERROR_MEMORY, 0,
            "Retaining actual SDK directory authorities");
    }
    if (ok) {
        roots[0] = (qa_native_process_resource_root){.prefix = "",
            .mode = QA_FS_OPENED_READ | QA_FS_OPENED_WRITE};
        ok = qa_fs_root_open(".", &roots[0].root, error);
        root_count = 1;
    }
#if !defined(_WIN32)
    if (ok) {
        roots[root_count] = (qa_native_process_resource_root){.prefix = "/",
            .mode = QA_FS_OPENED_READ | QA_FS_OPENED_WRITE};
        ok = qa_fs_root_open("/", &roots[root_count++].root, error);
    }
#else
    for (size_t i = 0; ok && i < environment.root_count; ++i) {
        qa_fs_root *root = NULL;
        qa_error unavailable = {0};
        if (!qa_fs_root_open(environment.root_paths[i], &root, &unavailable)) continue;
        roots[root_count++] = (qa_native_process_resource_root){environment.root_paths[i],
            root, QA_FS_OPENED_READ | QA_FS_OPENED_WRITE};
    }
#endif
    if (ok) {
        qa_native_process_resources_options resources = {.identity = identity,
            .authority = authority, .artifacts = artifacts, .artifact_count = count,
            .primary = options->dependency_count, .receiver = host->world.owner,
            .service_owner = identity, .policy = {
                .backend = emulated ? QA_NATIVE_GUEST_EMULATED : QA_NATIVE_GUEST_HOST_X86_64,
                .maximum_image_bytes = 256u * 1024u * 1024u,
                .maximum_backing_bytes = 1024u * 1024u * 1024u,
                .stack_bytes = 8u * 1024u * 1024u,
                .runtime_trap_bytes = 1024u * 1024u,
                .instruction_budget = emulated ? 50000000u : 0},
            .runtime = runtime, .bootstrap = qa_native_runtime_bootstrap(runtime),
            .platform = host->owned_platform, .roots = roots, .root_count = root_count,
            .temporary_root = qa_native_process_platform_temporary_root,
            .environment = (const char *const *)environment.values,
            .environment_count = environment.count,
            .windows_environment = environment.windows_values,
            .windows_environment_units = environment.windows_units};
        ok = qa_native_process_resources_create(&resources, &host->owned_process, error) &&
            qa_native_process_resources_options_read(host->owned_process, process, error);
    }
    qa_native_runtime_release(runtime);
    for (size_t i = 0; i < root_count; ++i) qa_fs_root_close(roots[i].root);
    free(roots);
    qa_native_process_environment_dispose(&environment);
    free(artifacts);
    return ok;
}

static bool configure_instance(qa_native_host *host, qa_native_module *module,
                               const qa_native_host_instance_options *options,
                               qa_error *error)
{
    qa_native_process_options owned = {0};
    if (!options->process && !create_sdk_process(host, module, options, &owned, error))
        return false;
    qa_native_options native = {
        .context = host,
        .q3_role = host->q3_role,
        .import = host->profile == QA_NATIVE_Q3_VMMAIN ? NULL : native_host_import,
        .describe_syscall = host->profile == QA_NATIVE_Q3_VMMAIN
                                ? native_host_describe_syscall
                                : NULL,
        .syscall = host->profile == QA_NATIVE_Q3_VMMAIN ? native_host_syscall : NULL,
        .checkpoint = native_host_capture,
        .restore = native_host_apply,
        .entity_changed = entity_changed,
        .declaration = options->declaration,
        .tick_rate = options->tick_rate,
        .frame_seconds = options->frame_seconds,
        .frame_milliseconds = options->frame_milliseconds,
        .observe = options->observe,
        .process = options->process ? options->process : &owned};
    return qa_native_create(module, &native, &host->instance, error);
}

static qa_native_host *allocate_host(qa_native_module *module, native_host_kind kind,
                                    qa_native_profile expected, size_t maximum_string,
                                    qa_error *error)
{
    if (!module) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "native module is required");
        return NULL;
    }
    qa_native_module_info info = qa_native_module_describe(module);
    if (info.profile != expected) {
        qa_error_set(error, QA_ERROR_ARGUMENT, info.profile,
                     "native module profile does not match its host adapter");
        return NULL;
    }
    qa_native_host *host = calloc(1, sizeof(*host));
    if (!host) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating native host adapter");
        return NULL;
    }
    host->kind = kind;
    host->profile = info.profile;
    host->pointer_bytes = info.image.target.pointer_bytes;
    if (info.profile == QA_NATIVE_Q2_GAME_API3) {
        host->classic = host->pointer_bytes == 4 ? &classic32 : &classic64;
        host->edict = &host->classic->edict;
    } else if (info.profile == QA_NATIVE_Q2_GAME_API2023) {
        host->edict = &rerelease_edict;
    }
    host->maximum_string_bytes = maximum_string ? maximum_string : 1024u * 1024u;
    if (kind == NATIVE_HOST_Q2_GAME && !native_host_movement_prepare(host, error)) {
        free_records(host);
        free(host);
        return NULL;
    }
    return host;
}

static bool creation_failed(qa_native_host *host, qa_native_host **out, qa_error *error)
{
    if (host->instance || !release_process(host, error)) {
        *out = host;
        return false;
    }
    free_records(host);
    free(host);
    return false;
}

bool qa_native_host_create_q2_game(qa_native_module *module,
                                   const qa_native_host_q2_game_options *options,
                                   qa_native_host **out, qa_error *error)
{
    if (!module || !options || !out || *out || !options->world.session || !options->world.world ||
        !options->world.owner || !options->cvars) {
        return native_host_fail(error, QA_ERROR_ARGUMENT, 0,
                                "Q2 native game host requires canonical world services");
    }
    qa_native_profile profile = qa_native_module_describe(module).profile;
    if (!profile_is_q2_game(profile))
        return native_host_fail(error, QA_ERROR_ARGUMENT, profile,
                                "Q2 native game host requires API 3 or API 2023");
    qa_native_host *host = allocate_host(module, NATIVE_HOST_Q2_GAME, profile,
                                         options->services.maximum_string_bytes, error);
    if (!host)
        return false;
    host->engine = options->services.engine;
    host->world = options->world;
    host->movement = options->services.movement;
    host->q2_application = options->services.application;
    host->q2_application_context = options->services.application_context;
    host->cvars = options->cvars;
    host->console = options->console;
    host->command_context = options->command_context;
    host->message_capacity = options->services.maximum_message_bytes
                                 ? options->services.maximum_message_bytes
                                 : 65536u;
    host->message = malloc(host->message_capacity);
    if (!host->message ||
        !configure_instance(host, module, &options->instance,
                            error) || !native_host_fields_refresh(host, error)) {
        return creation_failed(host, out, error);
    }
    *out = host;
    return true;
}

bool qa_native_host_create_q2_cgame(qa_native_module *module,
                                    const qa_native_host_q2_cgame_options *options,
                                    qa_native_host **out, qa_error *error)
{
    if (!module || !options || !out || *out || !options->cvars || !options->application)
        return native_host_fail(error, QA_ERROR_ARGUMENT, 0,
                                "Q2 native cgame host requires cvars and presentation services");
    qa_native_host *host = allocate_host(module, NATIVE_HOST_Q2_CGAME,
                                         QA_NATIVE_Q2_CGAME_API2023,
                                         options->maximum_string_bytes, error);
    if (!host)
        return false;
    host->engine = options->engine;
    host->q2_application = options->application;
    host->q2_application_context = options->application_context;
    host->q2_seat = options->seat;
    host->q2_seat_bound = options->seat_bound;
    host->cvars = options->cvars;
    host->console = options->console;
    host->command_context = options->command_context;
    if (!configure_instance(host, module, &options->instance, error)) {
        return creation_failed(host, out, error);
    }
    *out = host;
    return true;
}

bool qa_native_host_create_q3(qa_native_module *module,
                              const qa_native_host_q3_options *options,
                              qa_native_host **out, qa_error *error)
{
    if (!module || !options || !out || *out || !options->bridge.dispatch ||
        (unsigned)options->abi > QA_QVM_Q3_116N ||
        (options->role != QA_QVM_GAME && options->role != QA_QVM_CGAME &&
         options->role != QA_QVM_UI))
        return native_host_fail(error, QA_ERROR_ARGUMENT, 0,
                                "native Q3 host requires a shared syscall bridge");
    qa_native_profile profile = qa_native_module_describe(module).profile;
    if (profile != QA_NATIVE_Q3_VMMAIN && profile != QA_NATIVE_QUAKE_LIVE_GAME_API10)
        return native_host_fail(error, QA_ERROR_ARGUMENT, profile,
                                "native Q3 host requires vmMain or Quake Live API 10");
    if (profile == QA_NATIVE_QUAKE_LIVE_GAME_API10 && options->role != QA_QVM_GAME)
        return native_host_fail(error, QA_ERROR_ARGUMENT, options->role,
                                "Quake Live API 10 provides only the game role");
    if (profile == QA_NATIVE_Q3_VMMAIN && !options->bridge.describe_native)
        return native_host_fail(error, QA_ERROR_ARGUMENT, 0,
                                "native vmMain host requires syscall descriptions");
    qa_native_host *host = allocate_host(module, NATIVE_HOST_Q3, profile,
                                         options->maximum_string_bytes, error);
    if (!host)
        return false;
    host->world = options->world;
    host->engine = options->engine;
    host->q3 = options->bridge;
    host->q3_role = options->role;
    host->q3_abi = options->abi;
    host->cvars = options->cvars;
    host->console = options->console;
    host->command_context = options->command_context;
    if (!configure_instance(host, module, &options->instance, error)) {
        return creation_failed(host, out, error);
    }
    *out = host;
    return true;
}

static void free_records(qa_native_host *host)
{
    native_host_movement_dispose(host);
    native_host_memory_state memory={host->strings,host->cvar_shadows};
    native_host_memory_dispose(&memory); host->strings=NULL; host->cvar_shadows=NULL;
    while (host->surfaces) {
        native_host_surface *next = host->surfaces->next;
        free(host->surfaces);
        host->surfaces = next;
    }
    free(host->model_entries);
    free(host->message);
    free(host->message_references);
    free(host->retained_clients);
    free(host->q2_lifetimes);
    free(host->q2_fields);
}

bool qa_native_host_destroy_ready(const qa_native_host *host)
{
    return host && !host->destroying && !host->callback_depth &&
        (!host->instance || qa_native_can_destroy(host->instance));
}

bool qa_native_host_terminal_retired(const qa_native_host *host)
{
    return qa_native_host_destroy_ready(host) && qa_native_terminal_retired(host->instance,
        host->world.session ? qa_session_actors(host->world.session) : NULL);
}

bool qa_native_host_destroy_owned(qa_native_host **owner, qa_error *error)
{
    qa_native_host *host = owner ? *owner : NULL;
    if (!qa_native_host_destroy_ready(host) || host->reconstruction ||
        (qa_native_terminal(host->instance) && !qa_native_host_terminal_retired(host)))
        return native_host_fail(error, QA_ERROR_ARGUMENT, 0,
                                "live native host adapter is required");
    host->destroying = true;
    bool ok = !host->instance || qa_native_destroy_owned(&host->instance, error);
    if (host->instance) { host->destroying = false; return false; }
    if (!release_process(host, error)) {
        host->destroying = false;
        return false;
    }
    free_records(host);
    free(host);
    *owner = NULL;
    return ok;
}

qa_native_instance *qa_native_host_instance(qa_native_host *host)
{
    return host ? host->instance : NULL;
}

qa_native_profile qa_native_host_profile(const qa_native_host *host)
{
    return host ? host->profile : QA_NATIVE_Q2_GAME_API3;
}

bool qa_native_host_source_public_bytes(qa_native_host *host, uint32_t slot,
                                        size_t *out, qa_error *error)
{
    qa_native_entity_table table;
    if (!host || !out || !host->instance || !host->edict ||
        !profile_is_q2_game(host->profile) || qa_native_terminal(host->instance))
        return native_host_fail(error, QA_ERROR_ARGUMENT, slot,
                                "Native public entity extent requires its actual Q2 host");
    if (!qa_native_entity_table_get(host->instance, &table, error)) return false;
    if (slot >= table.count || table.stride < host->edict->bytes)
        return native_host_fail(error, QA_ERROR_ARGUMENT, slot,
                                "Native public entity extent exceeds its actual source table");
    *out = host->edict->bytes;
    return true;
}

bool qa_native_host_q3_vm_call(qa_native_host *host, int32_t command,
                               const int32_t *arguments, size_t argument_count,
                               int32_t *result, qa_error *error)
{
    if (!host || host->profile != QA_NATIVE_Q3_VMMAIN ||
        (argument_count && !arguments) || argument_count > 12 || !result)
        return native_host_fail(error, QA_ERROR_ARGUMENT, argument_count,
                                "invalid native Q3 vmMain call");
    qa_native_value words[13] = {0};
    for (size_t index = 0; index < 13; ++index)
        words[index].type = QA_NATIVE_I32;
    words[0].as.i32 = command;
    for (size_t index = 0; index < argument_count; ++index)
        words[index + 1u].as.i32 = arguments[index];
    qa_native_value returned = {.type = QA_NATIVE_I32};
    if (!qa_native_call(host->instance, "vmMain", words, 13, &returned, error))
        return false;
    *result = returned.as.i32;
    return true;
}

bool qa_native_host_q3_cgame_initialize(qa_native_host *host, int32_t server_message_number,
                                       int32_t server_command_sequence, int32_t client_number,
                                       qa_error *error)
{
    if (!host || host->profile != QA_NATIVE_Q3_VMMAIN || host->q3_role != QA_QVM_CGAME)
        return native_host_fail(error, QA_ERROR_ARGUMENT, 0,
                                "native Q3 cgame host is required for client initialization");
    int32_t arguments[] = {server_message_number, server_command_sequence, client_number};
    int32_t result;
    return qa_native_host_q3_vm_call(host, 0, arguments, 3, &result, error);
}

bool qa_native_host_q3_ui_initialize(qa_native_host *host, bool connecting, qa_error *error)
{
    if (!host || host->profile != QA_NATIVE_Q3_VMMAIN || host->q3_role != QA_QVM_UI)
        return native_host_fail(error, QA_ERROR_ARGUMENT, 0,
                                "native Q3 UI host is required for UI initialization");
    int32_t argument = connecting ? 1 : 0, result;
    return qa_native_host_q3_vm_call(host, 1, &argument, 1, &result, error);
}

bool qa_native_host_initialize(qa_native_host *host, int32_t level_time,
                               int32_t random_seed, bool restart, qa_error *error)
{
    if (!host || !host->instance)
        return native_host_fail(error, QA_ERROR_ARGUMENT, 0,
                                "native host adapter is required for initialization");
    if ((host->kind == NATIVE_HOST_Q2_GAME || host->kind == NATIVE_HOST_Q2_CGAME) &&
        !native_host_refresh_cvars(host, error)) return false;
    bool ok;
    if (host->profile == QA_NATIVE_QUAKE_LIVE_GAME_API10) {
        bool restarting = qa_native_get_lifecycle(host->instance) == QA_NATIVE_RESTART_READY;
        ok = (restarting || qa_native_ql_register_cvars(host->instance, error)) &&
             qa_native_ql_initialize(host->instance, level_time, random_seed, restart, error);
    }
    else if (host->profile == QA_NATIVE_Q3_VMMAIN) {
        if (host->q3_role != QA_QVM_GAME)
            return native_host_fail(error, QA_ERROR_ARGUMENT, 0,
                                    "native cgame and UI require their role's initialization arguments");
        int32_t arguments[] = {level_time, random_seed, restart ? 1 : 0};
        int32_t result;
        ok = qa_native_host_q3_vm_call(host, 0, arguments, 3, &result, error);
    }
    else
        ok = qa_native_initialize(host->instance, error);
    return ok;
}

bool qa_native_host_shutdown(qa_native_host *host, bool restart, qa_error *error)
{
    if (!host || !host->instance)
        return native_host_fail(error, QA_ERROR_ARGUMENT, 0,
                                "native host adapter is required for shutdown");
    if (host->profile == QA_NATIVE_QUAKE_LIVE_GAME_API10)
        return qa_native_ql_shutdown(host->instance, restart, error);
    if (host->profile == QA_NATIVE_Q3_VMMAIN) {
        int32_t argument = restart ? 1 : 0, result;
        int32_t command = host->q3_role == QA_QVM_UI ? 2 : 1;
        size_t count = host->q3_role == QA_QVM_GAME ? 1u : 0u;
        return qa_native_host_q3_vm_call(host, command, &argument, count, &result, error);
    }
    return qa_native_shutdown(host->instance, error);
}

static bool call_strings(qa_native_host *host, const char *entry,
                         const char *const *strings, size_t count, qa_error *error)
{
    qa_native_address addresses[3] = {0};
    qa_native_value arguments[3] = {0};
    if (count > 3)
        return native_host_fail(error, QA_ERROR_ARGUMENT, count,
                                "too many native lifecycle strings");
    bool ok = true;
    for (size_t index = 0; index < count; ++index) {
        ok = native_host_temporary_string(host, strings[index] ? strings[index] : "",
                                          &addresses[index], error);
        if (!ok)
            break;
        arguments[index].type = QA_NATIVE_ADDRESS;
        arguments[index].as.address = addresses[index];
    }
    if (ok)
        ok = qa_native_call(host->instance, entry, arguments, count, NULL, error);
    for (size_t index = 0; index < count; ++index)
        native_host_temporary_free(host, addresses[index]);
    return ok;
}

bool qa_native_host_spawn_entities(qa_native_host *host, const char *map,
                                   const char *entities, const char *spawn_point,
                                   qa_error *error)
{
    if (!host || host->kind != NATIVE_HOST_Q2_GAME || !map || !entities || !spawn_point)
        return native_host_fail(error, QA_ERROR_ARGUMENT, 0,
                                "Q2 native spawn strings are required");
    const char *strings[] = {map, entities, spawn_point};
    return call_strings(host, "SpawnEntities", strings, 3, error) &&
           native_host_reconcile(host, error);
}

bool qa_native_host_run_frame(qa_native_host *host, bool main_loop, qa_error *error)
{
    if (!host)
        return native_host_fail(error, QA_ERROR_ARGUMENT, 0,
                                "native host adapter is required for a frame");
    bool ok;
    if (host->profile == QA_NATIVE_Q2_GAME_API2023) {
        qa_native_value argument = {.type = QA_NATIVE_U8,
                                    .as.u8 = main_loop ? 1u : 0u};
        ok = qa_native_call(host->instance, "RunFrame", &argument, 1, NULL, error);
    } else if (host->profile == QA_NATIVE_QUAKE_LIVE_GAME_API10) {
        int32_t time = (int32_t)(host->world.session
                                     ? qa_session_elapsed(host->world.session) / 1000000u
                                     : 0u);
        qa_native_value argument = {.type = QA_NATIVE_I32, .as.i32 = time};
        ok = qa_native_call(host->instance, "RunFrame", &argument, 1, NULL, error);
    } else if (host->profile == QA_NATIVE_Q3_VMMAIN) {
        int32_t time = (int32_t)(host->world.session
                                     ? qa_session_elapsed(host->world.session) / 1000000u
                                     : 0u);
        int32_t result;
        ok = qa_native_host_q3_vm_call(host, 8, &time, 1, &result, error);
    } else {
        ok = qa_native_call(host->instance, "RunFrame", NULL, 0, NULL, error);
    }
    return ok && (host->kind != NATIVE_HOST_Q2_GAME || native_host_reconcile(host, error));
}

bool qa_native_host_prep_frame(qa_native_host *host, qa_error *error)
{
    if (!host || host->profile != QA_NATIVE_Q2_GAME_API2023)
        return native_host_fail(error, QA_ERROR_ARGUMENT, 0,
                                "PrepFrame requires a Q2 API 2023 game host");
    return qa_native_call(host->instance, "PrepFrame", NULL, 0, NULL, error) &&
           native_host_reconcile(host, error);
}

bool qa_native_host_server_command(qa_native_host *host, qa_error *error)
{
    if (!host || !profile_is_q2_game(host->profile))
        return native_host_fail(error, QA_ERROR_ARGUMENT, 0,
                                "ServerCommand requires a Q2 game host");
    return native_host_refresh_cvars(host, error) &&
           qa_native_call(host->instance, "ServerCommand", NULL, 0, NULL, error) &&
           native_host_reconcile(host, error);
}

static bool retain_client(qa_native_host *host, uint32_t slot, bool retained, qa_error *error)
{
    qa_native_entity_table table;
    if (!qa_native_entity_table_get(host->instance, &table, error) || slot >= table.capacity)
        return false;
    if (table.capacity > host->retained_capacity) {
        bool *values = realloc(host->retained_clients, table.capacity * sizeof(*values));
        if (!values)
            return native_host_fail(error, QA_ERROR_MEMORY, 0,
                                    "allocating native retained-client set");
        memset(values + host->retained_capacity, 0,
               (table.capacity - host->retained_capacity) * sizeof(*values));
        host->retained_clients = values;
        host->retained_capacity = table.capacity;
    }
    host->retained_clients[slot] = retained;
    return true;
}

bool qa_native_host_client_retained_set(qa_native_host *host, uint32_t slot,
    bool retained, qa_error *error)
{
    if (!host || host->kind != NATIVE_HOST_Q2_GAME || !slot)
        return native_host_fail(error, QA_ERROR_ARGUMENT, 0,
            "Declared native client retention requires its actual game host and slot");
    return retain_client(host, slot, retained, error) && native_host_reconcile(host, error);
}

bool qa_native_host_client_choose_slot(qa_native_host *host, const char *userinfo,
                                       const char *social_id, bool bot, char *client_info,
                                       size_t client_info_capacity, bool spectator,
                                       uint32_t *slot, qa_error *error)
{
    if (!host || host->profile != QA_NATIVE_Q2_GAME_API2023 || !userinfo || !social_id ||
        (!client_info && client_info_capacity) || !slot)
        return native_host_fail(error, QA_ERROR_ARGUMENT, 0,
                                "invalid Q2 API 2023 slot-selection request");
    qa_native_address values[3] = {0};
    bool ok = native_host_temporary_string(host, userinfo, &values[0], error) &&
              native_host_temporary_string(host, social_id, &values[1], error) &&
              qa_native_allocate(host->instance, client_info_capacity ? client_info_capacity : 1u,
                                 INT32_MIN + 4, &values[2], error);
    if (ok && client_info_capacity)
        ok = native_host_write(host, values[2], client_info, client_info_capacity, error);
    qa_native_value arguments[] = {
        {.type = QA_NATIVE_ADDRESS, .as.address = values[0]},
        {.type = QA_NATIVE_ADDRESS, .as.address = values[1]},
        {.type = QA_NATIVE_U8, .as.u8 = bot ? 1u : 0u},
        {.type = QA_NATIVE_ADDRESS, .as.address = values[2]},
        {.type = QA_NATIVE_U64, .as.u64 = client_info_capacity},
        {.type = QA_NATIVE_U8, .as.u8 = spectator ? 1u : 0u}};
    qa_native_value result = {0};
    if (ok)
        ok = qa_native_call(host->instance, "ClientChooseSlot", arguments, 6, &result,
                            error);
    if (ok && result.as.address)
        ok = qa_native_entity_slot(host->instance, result.as.address, slot, error);
    else if (ok)
        ok = native_host_fail(error, QA_ERROR_NOT_FOUND, 0,
                              "Q2 API 2023 did not choose a client slot");
    if (ok && client_info_capacity)
        ok = native_host_read(host, values[2], client_info, client_info_capacity, error);
    for (size_t index = 0; index < 3; ++index)
        native_host_temporary_free(host, values[index]);
    return ok;
}

static bool q2_userinfo_string(qa_native_host *host, const char *text,
    qa_native_address *out, qa_error *error)
{
    size_t capacity = host->profile == QA_NATIVE_Q2_GAME_API2023 ? 2048u : 512u;
    size_t length = strlen(text);
    if (length >= capacity)
        return native_host_fail(error, QA_ERROR_ARGUMENT, length,
            "Q2 userinfo exceeds its actual Source dictionary extent");
    uint8_t bytes[2048] = {0};
    memcpy(bytes, text, length);
    if (!qa_native_allocate(host->instance, capacity, INT32_MIN + 2, out, error)) return false;
    if (native_host_write(host, *out, bytes, capacity, error)) return true;
    native_host_temporary_free(host, *out); *out = 0;
    return false;
}

bool qa_native_host_client_connect_userinfo(qa_native_host *host,
                                   const qa_native_host_client_request *request,
                                   bool *accepted, qa_buffer *returned, qa_error *error)
{
    if (!host || host->kind != NATIVE_HOST_Q2_GAME || !request || !request->userinfo ||
        !accepted || !returned || returned->data || returned->size || request->slot == 0)
        return native_host_fail(error, QA_ERROR_ARGUMENT, 0,
                                "invalid Q2 native client-connect request");
    *accepted = false;
    qa_native_address entity, userinfo = 0, social = 0;
    if (!qa_native_entity_address(host->instance, request->slot, &entity, error) ||
        !retain_client(host, request->slot, true, error) ||
        !q2_userinfo_string(host, request->userinfo, &userinfo, error))
        return false;
    qa_native_value arguments[4] = {
        {.type = QA_NATIVE_ADDRESS, .as.address = entity},
        {.type = QA_NATIVE_ADDRESS, .as.address = userinfo}};
    size_t count = 2;
    if (host->profile == QA_NATIVE_Q2_GAME_API2023) {
        if (!native_host_temporary_string(host, request->social_id ? request->social_id : "",
                                          &social, error)) {
            native_host_temporary_free(host, userinfo);
            return false;
        }
        arguments[2] = (qa_native_value){.type = QA_NATIVE_ADDRESS, .as.address = social};
        arguments[3] = (qa_native_value){.type = QA_NATIVE_U8,
                                         .as.u8 = request->bot ? 1u : 0u};
        count = 4;
    }
    qa_native_value result = {0};
    bool ok = qa_native_call(host->instance, "ClientConnect", arguments, count, &result,
                             error);
    if (ok)
        *accepted = host->profile == QA_NATIVE_Q2_GAME_API3 ? result.as.i32 != 0
                                                            : result.as.u8 != 0;
    if (ok) ok = qa_native_read_string(host->instance, userinfo,
        host->profile == QA_NATIVE_Q2_GAME_API2023 ? 2048u : 512u, returned, error);
    if (!ok || !*accepted)
        retain_client(host, request->slot, false, NULL);
    native_host_temporary_free(host, social);
    native_host_temporary_free(host, userinfo);
    return ok && native_host_reconcile(host, error);
}

bool qa_native_host_client_connect(qa_native_host *host,
    const qa_native_host_client_request *request, bool *accepted, qa_error *error)
{
    qa_buffer returned = {0};
    bool ok = qa_native_host_client_connect_userinfo(host, request, accepted, &returned, error);
    qa_buffer_free(&returned);
    return ok;
}

static bool q3_game_host(qa_native_host *host, qa_error *error)
{
    if (!host || host->kind != NATIVE_HOST_Q3 || host->q3_role != QA_QVM_GAME)
        return native_host_fail(error, QA_ERROR_ARGUMENT, 0,
                                "native Q3 game host is required");
    return true;
}

static bool q3_game_slot_call(qa_native_host *host, int32_t command,
                              const char *ql_entry, uint32_t slot, qa_error *error)
{
    if (!q3_game_host(host, error) || slot > INT32_MAX)
        return false;
    if (host->profile == QA_NATIVE_Q3_VMMAIN) {
        int32_t argument = (int32_t)slot, result;
        return qa_native_host_q3_vm_call(host, command, &argument, 1, &result, error);
    }
    if (!ql_entry)
        return native_host_fail(error, QA_ERROR_UNSUPPORTED, (size_t)(uint32_t)command,
                                "Quake Live API 10 has no matching game entry");
    qa_native_value argument = {.type = QA_NATIVE_I32, .as.i32 = (int32_t)slot};
    return qa_native_call(host->instance, ql_entry, &argument, 1, NULL, error);
}

bool qa_native_host_q3_client_connect(qa_native_host *host, uint32_t slot,
                                      bool first_time, bool bot, qa_buffer *denial,
                                      qa_error *error)
{
    if (!q3_game_host(host, error) || slot > INT32_MAX || !denial)
        return false;
    *denial = (qa_buffer){0};
    qa_native_address denied = 0;
    if (host->profile == QA_NATIVE_Q3_VMMAIN) {
        int32_t arguments[] = {(int32_t)slot, first_time ? 1 : 0, bot ? 1 : 0};
        int32_t result;
        if (!qa_native_host_q3_vm_call(host, 2, arguments, 3, &result, error))
            return false;
        denied = (uint32_t)result;
    } else {
        qa_native_value arguments[] = {
            {.type = QA_NATIVE_I32, .as.i32 = (int32_t)slot},
            {.type = QA_NATIVE_I32, .as.i32 = first_time ? 1 : 0},
            {.type = QA_NATIVE_I32, .as.i32 = bot ? 1 : 0}};
        qa_native_value result = {.type = QA_NATIVE_ADDRESS};
        if (!qa_native_call(host->instance, "ClientConnect", arguments, 3, &result, error))
            return false;
        denied = result.as.address;
    }
    return !denied || qa_native_read_string(host->instance, denied,
                                            host->maximum_string_bytes, denial, error);
}

bool qa_native_host_q3_client_begin(qa_native_host *host, uint32_t slot,
                                    qa_error *error)
{
    return q3_game_slot_call(host, 3, NULL, slot, error);
}

bool qa_native_host_q3_client_userinfo(qa_native_host *host, uint32_t slot,
                                       qa_error *error)
{
    return q3_game_slot_call(host, 4, "ClientUserinfoChanged", slot, error);
}

bool qa_native_host_q3_client_disconnect(qa_native_host *host, uint32_t slot,
                                         qa_error *error)
{
    return q3_game_slot_call(host, 5, "ClientDisconnect", slot, error);
}

bool qa_native_host_q3_client_command(qa_native_host *host, uint32_t slot,
                                      qa_error *error)
{
    return q3_game_slot_call(host, 6, "ClientCommand", slot, error);
}

bool qa_native_host_q3_client_think(qa_native_host *host, uint32_t slot,
                                    qa_error *error)
{
    return q3_game_slot_call(host, 7, "ClientThink", slot, error);
}

bool qa_native_host_q3_console_command(qa_native_host *host, bool *handled,
                                       qa_error *error)
{
    if (!q3_game_host(host, error) || !handled)
        return false;
    int32_t result;
    if (host->profile == QA_NATIVE_Q3_VMMAIN) {
        if (!qa_native_host_q3_vm_call(host, 9, NULL, 0, &result, error))
            return false;
    } else {
        qa_native_value returned = {.type = QA_NATIVE_I32};
        if (!qa_native_call(host->instance, "ConsoleCommand", NULL, 0, &returned, error))
            return false;
        result = returned.as.i32;
    }
    *handled = result != 0;
    return true;
}

bool qa_native_host_q3_bot_frame(qa_native_host *host, int32_t level_time,
                                 qa_error *error)
{
    if (!q3_game_host(host, error))
        return false;
    if (host->profile != QA_NATIVE_Q3_VMMAIN)
        return native_host_fail(error, QA_ERROR_UNSUPPORTED, 10,
                                "Quake Live API 10 has no bot-frame entry");
    int32_t result;
    return qa_native_host_q3_vm_call(host, 10, &level_time, 1, &result, error);
}

static bool client_entity_call(qa_native_host *host, const char *entry, uint32_t slot,
                               qa_error *error)
{
    qa_native_address entity;
    if (!host || host->kind != NATIVE_HOST_Q2_GAME || slot == 0 ||
        !qa_native_entity_address(host->instance, slot, &entity, error))
        return false;
    qa_native_value argument = {.type = QA_NATIVE_ADDRESS, .as.address = entity};
    return qa_native_call(host->instance, entry, &argument, 1, NULL, error) &&
           native_host_reconcile(host, error);
}

bool qa_native_host_client_begin(qa_native_host *host, uint32_t slot, qa_error *error)
{
    return client_entity_call(host, "ClientBegin", slot, error);
}

bool qa_native_host_client_userinfo_result(qa_native_host *host, uint32_t slot,
    const char *userinfo, qa_buffer *returned, qa_error *error)
{
    qa_native_address entity, text = 0;
    if (!host || host->kind != NATIVE_HOST_Q2_GAME || !userinfo || !returned ||
        returned->data || returned->size || !slot ||
        !qa_native_entity_address(host->instance, slot, &entity, error) ||
        !q2_userinfo_string(host, userinfo, &text, error))
        return false;
    qa_native_value arguments[] = {
        {.type = QA_NATIVE_ADDRESS, .as.address = entity},
        {.type = QA_NATIVE_ADDRESS, .as.address = text}};
    bool ok = qa_native_call(host->instance, "ClientUserinfoChanged", arguments, 2, NULL,
                             error);
    if (ok) ok = qa_native_read_string(host->instance, text,
        host->profile == QA_NATIVE_Q2_GAME_API2023 ? 2048u : 512u, returned, error);
    native_host_temporary_free(host, text);
    return ok && native_host_reconcile(host, error);
}

bool qa_native_host_client_userinfo(qa_native_host *host, uint32_t slot,
    const char *userinfo, qa_error *error)
{
    qa_buffer returned = {0};
    bool ok = qa_native_host_client_userinfo_result(host, slot, userinfo, &returned, error);
    qa_buffer_free(&returned);
    return ok;
}

bool qa_native_host_client_disconnect(qa_native_host *host, uint32_t slot, qa_error *error)
{
    return client_entity_call(host, "ClientDisconnect", slot, error);
}

bool qa_native_host_client_command(qa_native_host *host, uint32_t slot, qa_error *error)
{
    return native_host_refresh_cvars(host, error) &&
           client_entity_call(host, "ClientCommand", slot, error);
}

bool qa_native_host_client_command_region(qa_native_host *host, uint32_t slot,
    const qa_native_region_event *event, qa_error *error)
{
    qa_native_address entity, entry;
    if (!host || host->kind != NATIVE_HOST_Q2_GAME || !slot || !event ||
        qa_native_get_lifecycle(host->instance) != QA_NATIVE_INITIALIZED ||
        !qa_native_entity_address(host->instance, slot, &entity, error) ||
        !qa_native_entry_address(host->instance, "ClientCommand", &entry, error))
        return false;
    const qa_native_signature *signature = qa_native_entry_signature(host->instance, "ClientCommand");
    if (!signature)
        return native_host_fail(error, QA_ERROR_NOT_FOUND, slot,
            "Native Q2 has no actual ClientCommand export");
    qa_native_value argument = {.type = QA_NATIVE_ADDRESS, .as.address = entity};
    return native_host_refresh_cvars(host, error) &&
        qa_native_region_invoke(host->instance, event, entry, signature, &argument, 1, NULL, error) &&
        native_host_reconcile(host, error);
}

bool qa_native_host_client_think(qa_native_host *host, uint32_t slot,
                                 qa_bytes source_usercmd, qa_error *error)
{
    size_t expected = host && host->profile == QA_NATIVE_Q2_GAME_API3 ? 16u : 28u;
    if (!host || host->kind != NATIVE_HOST_Q2_GAME || !source_usercmd.data ||
        source_usercmd.size != expected)
        return native_host_fail(error, QA_ERROR_ARGUMENT, source_usercmd.size,
                                "native Q2 user command has the wrong ABI size");
    qa_native_address entity, command = 0;
    if (!qa_native_entity_address(host->instance, slot, &entity, error) ||
        !qa_native_allocate(host->instance, expected, INT32_MIN + 5, &command, error) ||
        !native_host_write(host, command, source_usercmd.data, expected, error)) {
        native_host_temporary_free(host, command);
        return false;
    }
    qa_native_value arguments[] = {
        {.type = QA_NATIVE_ADDRESS, .as.address = entity},
        {.type = QA_NATIVE_ADDRESS, .as.address = command}};
    bool ok = qa_native_call(host->instance, "ClientThink", arguments, 2, NULL, error);
    native_host_temporary_free(host, command);
    return ok && native_host_reconcile(host, error);
}

bool native_host_import(void *context, qa_native_instance *instance,
                        const qa_native_import_call *call, qa_native_value *result,
                        qa_error *error)
{
    qa_native_host *host = context;
    if (!host || (host->instance && host->instance != instance) || !call)
        return native_host_fail(error, QA_ERROR_ARGUMENT, 0,
                                "native import belongs to another host instance");
    bool bootstrap = host->instance == NULL;
    if (bootstrap)
        host->instance = instance;
    ++host->callback_depth;
    bool paired = (host->engine.source_before == NULL) == (host->engine.source_after == NULL);
    bool prepared = paired && (!host->engine.source_before || host->engine.source_before(host->engine.context, error));
    bool handled=false;
    bool intercepted=prepared&&(!host->engine.source_import||host->engine.source_import(host->engine.context,call,result,&handled,error));
    bool ok = intercepted && (handled || (host->profile == QA_NATIVE_QUAKE_LIVE_GAME_API10
                  ? native_host_q3_import(host, call, result, error)
                  : native_host_q2_import(host, call, result, error)));
    if (!paired) ok = native_host_fail(error, QA_ERROR_ARGUMENT, 0, "Native source-import transfer callbacks must be paired");
    if (prepared && host->engine.source_after) {
        qa_error returned = {0};
        bool refreshed = host->engine.source_after(host->engine.context, &returned);
        if (!refreshed && ok) { ok = false; if (error) *error = returned; }
    }
    --host->callback_depth;
    if (bootstrap)
        host->instance = NULL;
    return ok;
}

bool native_host_capture(void *context, qa_buffer *state, qa_error *error)
{
    return qa_native_host_checkpoint(context, state, error);
}

bool native_host_apply(void *context, qa_bytes state, qa_error *error)
{
    return qa_native_host_restore(context, state, true, error);
}
