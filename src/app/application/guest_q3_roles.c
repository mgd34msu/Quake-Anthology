#include "guest_q3_private.h"
#include "guest_input_private.h"
#include "guest_projection_private.h"
#include "bots_private.h"

static bool qvm_path(const char *path)
{
    size_t length = strlen(path);
    if (length < 4) return false;
    const char *suffix = path + length - 4;
    return suffix[0] == '.' && (suffix[1] == 'q' || suffix[1] == 'Q') &&
        (suffix[2] == 'v' || suffix[2] == 'V') && (suffix[3] == 'm' || suffix[3] == 'M');
}

bool q3g_role_destroy(q3g_role *role, qa_error *error)
{
    if (!role || role->engine->calls || role->initialized)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 role must be idle and shut down before release");
    if (role->vm && qa_qvm_active(role->vm))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 role executor is active");
    if (role->native && !qa_native_host_destroy_ready(role->native))
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q3 role executor is active");
    if (!application_guest_projection_close(role, error)) return false;
    if (role->host) {
        if (!qa_q3_host_destroy_ready(role->host))
            return application_fail(error, QA_ERROR_ARGUMENT, "Q3 role host still owns live bindings or service callbacks");
        if (!application_guest_input_detach(role, error)) return false;
    }
    qa_error first = {0};
    bool ok = true;
    /* OS loader destructors can still use imports and the shared bridge. */
    if (role->native) {
        ++role->engine->calls;
        ok = qa_native_host_destroy(role->native, &first);
        --role->engine->calls;
        role->native = NULL;
        if (role == role->engine->game) q3g_game_aliases(role->engine, role);
    }
    if (role->host) {
        qa_error current = {0};
        if (!qa_q3_host_destroy(role->host, &current)) {
            if (error) *error = ok ? current : first;
            return false;
        }
        role->host = NULL;
        if (role == role->engine->game) q3g_game_aliases(role->engine, role);
    }
    if (role->vm) {
        if (!qa_qvm_destroy(role->vm, error)) return false;
        role->vm = NULL;
        if (role == role->engine->game) q3g_game_aliases(role->engine, role);
    }
    /* Keep only the descriptor for a later close retry; consumed executors and
     * source shutdown callbacks must not be repeated after a cleanup fault. */
    if (!ok) { if (error) *error = first; return false; }
    qa_qvm_image_release(role->image);
    qa_native_module_release(role->module);
    if (!role->artifact) qa_native_declaration_destroy(role->declaration);
    qa_command_tokens_free(&role->arguments);
    free(role->path); free(role);
    return true;
}

bool q3g_role_create(struct application_q3_guest *engine, qa_qvm_role kind,
                      uint32_t seat, const char *path, bool primary,
                      q3g_role **out, qa_error *error)
{
    if (!engine || !path || !*path || !out || kind > QA_QVM_UI)
        return application_fail(error, QA_ERROR_ARGUMENT, "invalid Q3 role artifact");
    *out = NULL;
    q3g_role *role = calloc(1, sizeof(*role));
    if (!role) return application_fail(error, QA_ERROR_MEMORY, "allocating Q3 role");
    role->engine = engine; role->kind = kind; role->seat = seat; role->primary = primary;
    role->client = UINT32_MAX; role->path = q3g_copy_text(path, error);
    if (!role->path) { free(role); return false; }
    qa_q3_host_options options = {0};
    qa_qvm_compatibility compatibility = {0};
    application_provider *provider = engine->provider;
    ++engine->calls;
    bool services_ready = application_q3_guest_services(provider->application, provider, kind, seat, &options, error);
    --engine->calls;
    if (!services_ready) goto failed;
    options.world = engine->world;
    if (engine->role_sequence == UINT64_MAX) {
        application_fail(error, QA_ERROR_MEMORY, "Q3 role registration sequence exhausted"); goto failed;
    }
    char identity[65], service_name[160];
    qa_sha256_hex(&provider->launch->identity, identity);
    snprintf(service_name, sizeof(service_name), "q3-service:%u:%s:%llu",
        provider->owner, identity, (unsigned long long)++engine->role_sequence);
    qa_string_id service_owner;
    if (!qa_strings_intern_cstr(qa_session_strings(provider->application->session),
                                service_name, &service_owner, error)) goto failed;
    options.service_owner = service_owner;
    if (engine->entity_text)
        options.entity_text = (qa_bytes){(const uint8_t *)engine->entity_text, strlen(engine->entity_text)};
    bool use_qvm = primary ? provider->kind == APPLICATION_PROVIDER_QVM : qvm_path(path);
    for (q3g_artifact *shared = engine->artifacts; shared; shared = shared->next)
        if (shared->kind == kind && shared->qvm == use_qvm && !strcmp(shared->path, path)) {
            role->artifact = shared; break;
        }
    if (role->artifact) {
        role->image = role->artifact->image; qa_qvm_image_retain(role->image);
        role->module = role->artifact->module; qa_native_module_retain(role->module);
        role->declaration = role->artifact->declaration; options.abi = role->artifact->abi;
    } else if (use_qvm) {
        if (primary && provider->kind == APPLICATION_PROVIDER_QVM) {
            role->image = provider->state.qvm.image; qa_qvm_image_retain(role->image);
            bool ok = provider->launch->declaration ?
                qa_qvm_compatibility_parse(qa_resource_bytes(provider->launch->declaration), path,
                    qa_qvm_image_digest(role->image), kind, &compatibility, error) :
                qa_qvm_compatibility_read(provider->launch->content, path,
                    qa_qvm_image_digest(role->image), kind, &compatibility, error);
            if (!ok) goto failed;
        } else {
            for (q3g_role *shared = engine->roles; shared; shared = shared->next)
                if (shared->image && shared->kind == kind && !strcmp(shared->path, path)) {
                    role->image = shared->image; qa_qvm_image_retain(role->image); break;
                }
            bool ok = role->image ? qa_qvm_compatibility_read(provider->launch->content, path,
                qa_qvm_image_digest(role->image), kind, &compatibility, error) :
                qa_qvm_image_open(provider->launch->content, path, kind, &role->image, &compatibility, error);
            if (!ok) goto failed;
        }
        options.abi = compatibility.abi;
    } else {
        if (primary && provider->state.native.module) {
            role->module = provider->state.native.module; qa_native_module_retain(role->module);
        } else {
            for (q3g_role *shared = engine->roles; shared; shared = shared->next)
                if (shared->module && shared->kind == kind && !strcmp(shared->path, path)) {
                    role->module = shared->module; qa_native_module_retain(role->module); break;
                }
            qa_resource *resource = NULL;
            const qa_resource *artifact = primary ? provider->launch->artifact : NULL;
            if (!role->module && !artifact && !qa_vfs_acquire(provider->launch->content, path, &resource, NULL, error)) goto failed;
            bool ok = role->module || qa_native_module_load(qa_resource_bytes(artifact ? artifact : resource), path,
                            QA_NATIVE_Q3_VMMAIN, NULL, &role->module, error);
            qa_resource_release(resource);
            if (!ok) goto failed;
            if (primary) {
                provider->state.native.module = role->module;
                qa_native_module_retain(role->module);
            }
        }
        if (primary && provider->launch->declaration &&
            !qa_native_declaration_load(qa_resource_bytes(provider->launch->declaration),
                                       path, role->module, &role->declaration, error)) goto failed;
    }
    if (!role->artifact) {
        q3g_artifact *shared = calloc(1, sizeof(*shared));
        if (!shared) { application_fail(error, QA_ERROR_MEMORY, "retaining immutable Q3 artifact"); goto failed; }
        shared->path = q3g_copy_text(path, error);
        if (!shared->path) { free(shared); goto failed; }
        shared->kind = kind; shared->abi = options.abi; shared->qvm = use_qvm;
        shared->image = role->image; qa_qvm_image_retain(shared->image);
        shared->module = role->module; qa_native_module_retain(shared->module);
        shared->declaration = role->declaration;
        shared->primary = compatibility.primary; compatibility.primary = (qa_buffer){0};
        shared->next = engine->artifacts; engine->artifacts = shared; role->artifact = shared;
    }
    q3g_server_bind(role, &options);
    if (!q3g_client_bind(role, &options, error)) goto failed;
    role->abi = options.abi; role->client_services = options.client;
    if (!qa_q3_host_create(&options, &role->host, error)) goto failed;
    options.frontend_lifetime=NULL;options.release_frontend=NULL;
    if (role->image) {
        qa_qvm_options vm = qa_q3_host_qvm_options(role->host, QA_QVM_COMPILED_SEMANTICS);
        if (!qa_qvm_create(role->image, &vm, &role->vm, error) ||
            !qa_q3_host_attach_qvm(role->host, role->vm, error)) goto failed;
    } else {
        qa_application *app = provider->application;
        qa_native_host_q3_options native = {.role = kind, .abi = options.abi,
            .cvars = options.cvars, .console = options.console,
            .command_context = options.command_context,
            .maximum_string_bytes = options.maximum_string_bytes,
            .bridge = qa_q3_host_native_bridge(role->host),
            .world = {.session = app->session, .world = engine->world,
                .physics = app->physics, .combat = app->combat,
                .inventory = app->inventory, .targets = app->targets, .owner = provider->owner},
        };
        if (!application_q3_guest_native_options(app, provider, kind, seat, &native.instance, error)) goto failed;
        if (role->declaration) {
            native.instance.declaration = role->declaration;
            native.instance.declaration_digest = qa_native_declaration_digest(role->declaration);
        }
        role->native_options = native;
    }
    if (kind == QA_QVM_GAME && role->vm) {
        qa_bytes primary = role->artifact->image ?
            (qa_bytes){role->artifact->primary.data, role->artifact->primary.size} :
            qa_native_declaration_primary(role->artifact->declaration);
        if (!application_guest_input_attach(role, primary, error)) goto failed;
        if (!application_guest_projection_prepare(role, primary, error)) goto failed;
    }
    qa_qvm_compatibility_free(&compatibility);
    role->ready = true; *out = role; return true;
failed:
    if(options.release_frontend) options.release_frontend(options.frontend_lifetime);
    qa_qvm_compatibility_free(&compatibility);
    /* Keep a failed cleanup reachable by the provider's retirement path. */
    if (!q3g_role_destroy(role, NULL)) { role->next = engine->roles; engine->roles = role; }
    return false;
}

bool q3g_role_activate(q3g_role *role, qa_error *error)
{
    if (!role || !role->ready || !role->host || role->retired)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 activation requires an admitted role");
    if (role->activation_failed) {
        if (error) *error = role->activation_error;
        return false;
    }
    if (role->committed) return true;
    struct application_q3_guest *engine = role->engine;
    if (engine->calls || !engine->provider->constructed || !engine->provider->attached)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 source activation requires a committed idle provider");
    qa_bot_runtime *bots = application_bots_runtime(engine->provider->application);
    if (role->kind == QA_QVM_GAME && bots &&
        !application_bots_guest_bind(engine->provider->application,engine->provider,role->host,error)) return false;
    if (role->vm) { role->committed = true; return true; }
    if (!role->module)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 native role has no staged module");
    qa_error failure = {0};
    ++engine->calls;
    bool ok = qa_native_host_create_q3(role->module, &role->native_options, &role->native, &failure);
    if (ok) ok = qa_q3_host_attach_native(role->host, role->native, &failure);
    if (ok && role->kind == QA_QVM_GAME)
        ok = application_guest_input_attach(role,
            qa_native_declaration_primary(role->artifact->declaration), &failure);
    --engine->calls;
    if (!ok) {
        if (failure.code == QA_OK)
            qa_error_set(&failure, QA_ERROR_ARGUMENT, 0, "Q3 native activation failed");
        role->activation_error = failure; role->activation_failed = true;
        if (error) *error = failure;
        if (role == engine->game) q3g_game_aliases(engine, role);
        return false;
    }
    role->committed = true;
    if (role == engine->game) q3g_game_aliases(engine, role);
    return true;
}

bool q3g_role_shutdown(q3g_role *role, qa_error *error)
{
    if (!role || role->engine->calls || !qa_world_idle(role->engine->world) ||
        (role->vm && qa_qvm_active(role->vm)) ||
        (role->native && !qa_native_can_destroy(qa_native_host_instance(role->native))))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 shutdown requires idle source and world callbacks");
    if (!role->initialized) return true;
    /* Consume the callback attempt before source code can reenter its owner.
     * Failure does not authorize a second partially performed shutdown. */
    role->initialized = false;
    bool ok;
    if (role->native) {
        ++role->engine->calls;
        ok = qa_native_shutdown(qa_native_host_instance(role->native), error);
        --role->engine->calls;
    } else {
        int32_t restart = 0, result;
        ok = q3g_call(role, role->kind == QA_QVM_UI ? 2 : 1,
            role->kind == QA_QVM_GAME ? &restart : NULL,
            role->kind == QA_QVM_GAME ? 1 : 0, &result, error);
    }
    role->retired = true;
    return ok;
}

void q3g_game_aliases(struct application_q3_guest *engine, q3g_role *role)
{
    application_provider *provider = engine->provider;
    if (provider->kind == APPLICATION_PROVIDER_QVM) {
        provider->state.qvm.host = role ? role->host : NULL;
        provider->state.qvm.machine = role ? role->vm : NULL;
    } else {
        provider->state.native.q3_host = role ? role->host : NULL;
        provider->state.native.host = role ? role->native : NULL;
    }
}

bool q3g_role_restart(q3g_role *role, q3g_role **out, qa_error *error)
{
    struct application_q3_guest *engine = role->engine;
    q3g_role **position = &engine->roles;
    while (*position && *position != role) position = &(*position)->next;
    if (!*position || engine->calls || !qa_world_idle(engine->world))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 role restart requires its idle published owner");
    char *path = q3g_copy_text(role->path, error); if (!path) return false;
    qa_qvm_role kind = role->kind;
    uint32_t seat = role->seat;
    bool primary = role->primary, game = role == engine->game;
    qa_error shutdown_error = {0};
    bool shut_down = q3g_role_shutdown(role, &shutdown_error);
    q3g_role *next = role->next;
    if (!q3g_role_destroy(role, error)) {
        if (!shut_down && error) *error = shutdown_error;
        free(path); return false;
    }
    *position = next;
    if (game) { engine->game = NULL; q3g_game_aliases(engine, NULL); }
    if (!shut_down) { if (error) *error = shutdown_error; free(path); return false; }
    q3g_role *replacement = NULL;
    bool ok = q3g_role_create(engine, kind, seat, path, primary, &replacement, error);
    free(path);
    if (!ok) return false;
    replacement->next = engine->roles; engine->roles = replacement;
    if (game) { engine->game = replacement; q3g_game_aliases(engine, replacement); }
    *out = replacement; return true;
}
