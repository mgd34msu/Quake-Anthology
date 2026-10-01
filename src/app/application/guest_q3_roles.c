#include "guest_q3_private.h"
#include "guest_input_private.h"
#include "guest_projection_private.h"
#include "bots_private.h"
#include "q3_world_restart.h"
#include "native_q3_wire_state.h"
#include "q3_campaign_launch.h"
#include "guest_q3_console.h"
#include "q3_product.h"
#include "qa/application_q3_equipment_source.h"
#include "native_q3_console.h"
#include "qa/cvars_save.h"
#include "startup_flow.h"
#include "guest_q3_client_console.h"

static bool equipment_entity(void *context, const qa_qvm_call *call, int32_t pointer,
    const qa_q3_ref_entity *entity, bool *suppress, qa_error *error)
{
    q3g_role *role = context;
    if (!role->equipment)
        return application_fail(error, QA_ERROR_ARGUMENT, "Source entity submission lost its actual equipment owner");
    return application_q3_equipment_source_entity(role->equipment, call, pointer, entity, suppress, error);
}

static bool qvm_path(const char *path)
{
    size_t length = strlen(path);
    if (length < 4) return false;
    const char *suffix = path + length - 4;
    return suffix[0] == '.' && (suffix[1] == 'q' || suffix[1] == 'Q') &&
        (suffix[2] == 'v' || suffix[2] == 'V') && (suffix[3] == 'm' || suffix[3] == 'M');
}

bool q3g_role_consume(q3g_role *role, qa_error *error)
{
    if (!role || role->engine->calls || role->initialized)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 role must be idle and shut down before release");
    if (role->vm && !qa_qvm_can_destroy(role->vm))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 role executor has an admitted source or lifecycle callback");
    if (role->native && !qa_native_host_destroy_ready(role->native))
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q3 role executor is active");
    if (role->equipment) {
        if (!application_q3_equipment_destroy(role->equipment, error)) return false;
        role->equipment = NULL;
    }
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
        q3g_role *previous = role->engine->entered_role;
        role->engine->entered_role = role;
        ++role->engine->calls;
        ok = qa_native_host_destroy_owned(&role->native, &first);
        --role->engine->calls;
        role->engine->entered_role = previous;
        if (role->native) { if (error) *error = first; return false; }
        qa_q3_host_native_consumed(role->host);
        if (role == role->engine->game) q3g_game_aliases(role->engine, role);
    }
    if (role->vm) {
        if (!qa_qvm_destroy(role->vm, error)) return false;
        role->vm = NULL;
        qa_q3_host_qvm_consumed(role->host);
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
    if (!application_native_q3_wire_client_unbind(&role->native_client, error)) return false;
    if (role->client_engine && role->client_engine != role->engine)
        --role->client_engine->client_leases;
    role->client_engine = NULL;
    role->client_source = NULL;
    /* Keep only the descriptor for a later close retry; consumed executors and
     * source shutdown callbacks must not be repeated after a cleanup fault. */
    if (!ok) { if (error) *error = first; return false; }
    return true;
}

bool q3g_role_destroy(q3g_role *role, qa_error *error)
{
    if (!q3g_role_consume(role, error)) return false;
    qa_qvm_image_release(role->image);
    qa_native_module_release(role->module);
    if (!role->artifact) qa_native_declaration_destroy(role->declaration);
    qa_command_tokens_free(&role->arguments);
    free(role->path); free(role);
    return true;
}

static bool role_create(struct application_q3_guest *engine, qa_qvm_role kind,
                          uint32_t seat, const char *path, bool primary,
                          uint64_t saved_sequence, qa_string_id saved_owner,
                          q3g_role **out, qa_error *error)
{
    if (!engine || engine->constructing_role || !path || !*path || !out || (unsigned)kind > QA_QVM_UI)
        return application_fail(error, QA_ERROR_ARGUMENT, "invalid Q3 role artifact");
    *out = NULL;
    if (saved_owner)
        for (q3g_role *prior = engine->roles; prior; prior = prior->next)
            if (prior->service_owner == saved_owner || prior->service_sequence == saved_sequence)
                return application_fail(error, QA_ERROR_FORMAT, "Duplicate restored Q3 role lifetime identity");
    q3g_role *role = calloc(1, sizeof(*role));
    if (!role) return application_fail(error, QA_ERROR_MEMORY, "allocating Q3 role");
    role->engine = engine; role->kind = kind; role->seat = seat; role->primary = primary;
    role->client = UINT32_MAX; role->path = q3g_copy_text(path, error);
    if (!role->path) { free(role); return false; }
    qa_q3_host_options options = {0};
    qa_application_q3_equipment_services equipment_services = {0};
    qa_qvm_compatibility compatibility = {0};
    application_provider *provider = engine->provider;
    const qa_launch_instance *descriptor = kind == QA_QVM_GAME ? provider->launch :
        engine->client_candidate ? engine->client_candidate : engine->client_descriptor ?
        qa_launch_instance_lease_view(engine->client_descriptor) : provider->launch;
    role->descriptor = descriptor;
    if (kind != QA_QVM_GAME && !application_guest_q3_client_console_prepare(engine,
        engine->restore_pending ? engine->restored_client_role : kind, seat, error)) goto failed;
    if (!saved_owner && engine->role_sequence == UINT64_MAX) {
        application_fail(error, QA_ERROR_MEMORY, "Q3 role registration sequence exhausted"); goto failed;
    }
    if (saved_owner && (!saved_sequence || saved_sequence > engine->role_sequence ||
        provider->application->operation != APPLICATION_PERSISTING)) {
        application_fail(error, QA_ERROR_FORMAT, "Restored Q3 role leaves its actual source registration generation");
        goto failed;
    }
    char identity[65], service_name[160];
    qa_sha256_hex(&descriptor->identity, identity);
    snprintf(service_name, sizeof(service_name), "q3-service:%u:%s:%llu",
        provider->owner, identity, (unsigned long long)(saved_owner ? saved_sequence : ++engine->role_sequence));
    qa_strings *strings = qa_session_strings(provider->application->session);
    if (saved_owner) {
        qa_string_id admitted = qa_strings_find(strings,
            (qa_bytes){(const uint8_t *)service_name, strlen(service_name)});
        if (admitted != saved_owner) {
            application_fail(error, QA_ERROR_FORMAT, "Restored Q3 service owner differs from its qualified source role");
            goto failed;
        }
        role->service_sequence = saved_sequence;
        role->service_owner = saved_owner;
    } else {
        if (!qa_strings_intern_cstr(strings, service_name, &role->service_owner, error)) goto failed;
        role->service_sequence = engine->role_sequence;
    }
    if (kind != QA_QVM_GAME && !provider->attached &&
        q3g_primary_role(provider->launch->selection.artifact) != QA_QVM_GAME) {
        qa_application_startup_source source;
        bool found = false;
        for (size_t index = 0; application_guest_q3_client_console_source(engine, index, &source); ++index)
            if (source.scope.seat == seat) { found = true; break; }
        bool prepared = found && (engine->restore_pending ?
            application_startup_tuple_restore(provider, &source, error) :
            application_startup_tuple_preinit(provider, &source, error) &&
                qa_cvars_apply_latched(source.cvars, NULL, error));
        if (!prepared) {
            if (!found) application_fail(error, QA_ERROR_ARGUMENT,
                "CLIENT construction lost its completed physical preparation");
            goto failed;
        }
    }
    ++engine->calls;
    engine->constructing_role = role;
    engine->constructing_equipment_services = &equipment_services;
    bool services_ready = application_q3_guest_services_descriptor(provider->application, provider, descriptor, kind, seat,
        role->service_owner, &options, error);
    engine->constructing_role = NULL;
    engine->constructing_equipment_services = NULL;
    --engine->calls;
    if (!services_ready) goto failed;
    if (kind != QA_QVM_GAME) {
        qa_console *console = NULL;
        if (!application_guest_q3_client_console_at(engine, seat, &console, NULL) ||
            options.console != console || !application_guest_q3_client_console_bind(engine, seat, options.cvars, error)) {
            if (error && error->code == QA_OK)
                application_fail(error, QA_ERROR_ARGUMENT, "CLIENT services displaced its retained physical console");
            goto failed;
        }
    }
    if (saved_owner && kind != QA_QVM_GAME) {
        if (engine->restored_client_source_instance) {
            const char *instance = NULL;
            uint32_t source_seat = 0;
            bool found = false;
            if (!provider->application->q3_client_registry_reference) {
                application_fail(error, QA_ERROR_FORMAT, "Restored shared client registry has no physical owner resolver");
                goto failed;
            }
            if (!provider->application->q3_client_registry_reference(provider->application->guest_context,
                options.cvars, &instance, &source_seat, &found, error)) goto failed;
            if (!found || !instance || strcmp(instance, engine->restored_client_source_instance) ||
                source_seat != engine->restored_client_source_seat) {
                application_fail(error, QA_ERROR_FORMAT, "Restored client lost its physical shared registry reference");
                goto failed;
            }
        }
        if (engine->restored_client_registry && options.cvars != engine->restored_client_registry) {
            application_fail(error, QA_ERROR_FORMAT, "Client construction changed its canonical restored registry alias");
            goto failed;
        }
        if (engine->restored_client_cvars.size) {
            qa_cvars_restore *ticket = NULL;
            if (!qa_cvars_save_prepare(options.cvars, engine->restored_client_cvars, &ticket, error)) goto failed;
            if (!qa_cvars_save_commit(ticket, error)) { qa_cvars_save_abort(ticket); goto failed; }
        }
        if (q3g_primary_role(provider->launch->selection.artifact) != QA_QVM_GAME) {
            qa_application_startup_source source;
            bool found = false;
            for (size_t index = 0; application_guest_q3_client_console_source(engine, index, &source); ++index)
                if (source.scope.seat == seat) { found = true; break; }
            if (!found || !application_startup_tuple_restore(provider, &source, error)) {
                if (!found) application_fail(error, QA_ERROR_FORMAT,
                    "Restored CLIENT lost its canonical physical preparation");
                goto failed;
            }
        }
    }
    if (kind == QA_QVM_GAME && !saved_owner && !engine->game) {
        qa_application_startup_source source = {.descriptor = descriptor,
            .scope = {.provider = provider->owner, .kind = QA_APPLICATION_CONSOLE_Q3_GAME},
            .console = options.console, .cvars = options.cvars,
            .command = options.command_context, .declaration_owner = role->service_owner};
        bool carried = false;
        if ((!provider->attached && !application_startup_source_carry(provider, &source, &carried, error)) ||
         !application_q3_world_restart_cvars(provider->application, provider, options.cvars, error) ||
         !application_q3_campaign_launch_cvars(provider, options.cvars, role->service_owner, error) ||
         !application_guest_q3_console_startup(provider, error)) goto failed;
    }
    options.world = engine->world;
    options.service_owner = role->service_owner;
    if (engine->entity_text)
        options.entity_text = (qa_bytes){(const uint8_t *)engine->entity_text, strlen(engine->entity_text)};
    bool use_qvm = primary ? descriptor->selection.runtime == QA_PROGRAM_QVM : qvm_path(path);
    for (q3g_artifact *shared = engine->artifacts; shared; shared = shared->next)
        if (shared->view == descriptor->content && (shared->image || shared->module) && shared->kind == kind && shared->qvm == use_qvm && !strcmp(shared->path, path)) {
            role->artifact = shared; break;
        }
    if (role->artifact) {
        role->image = role->artifact->image; qa_qvm_image_retain(role->image);
        role->module = role->artifact->module; qa_native_module_retain(role->module);
        role->declaration = role->artifact->declaration; options.abi = role->artifact->abi;
    } else {
        q3g_artifact *shared = calloc(1, sizeof(*shared));
        if (!shared) { application_fail(error, QA_ERROR_MEMORY, "retaining immutable Q3 artifact"); goto failed; }
        shared->path = q3g_copy_text(path, error);
        if (!shared->path) { free(shared); goto failed; }
        shared->kind = kind; shared->abi = options.abi; shared->qvm = use_qvm;
        shared->view = descriptor->content;
        shared->next = engine->artifacts; engine->artifacts = shared; role->artifact = shared;
        if (!qa_launch_instance_retain_metadata(descriptor, &shared->descriptor, error)) goto failed;
        if (primary) {
            shared->resource = (qa_resource *)descriptor->artifact;
            qa_resource_retain(shared->resource);
            if (!shared->resource || !q3g_acquisition_copy(descriptor->artifact_acquisition,
                &shared->acquisition, error)) goto failed;
        } else if (!qa_vfs_acquire_receipt(shared->view, path, &shared->resource,
            &shared->acquisition, error)) goto failed;
        if (use_qvm) {
            if (primary && descriptor == provider->launch && provider->kind == APPLICATION_PROVIDER_QVM) {
                role->image = provider->state.qvm.image; qa_qvm_image_retain(role->image);
            } else if (!qa_qvm_image_load(qa_resource_bytes(shared->resource), &role->image, error)) goto failed;
            bool ok = q3g_compatibility(descriptor, path, role->image, kind, primary,
                &compatibility, error);
            if (!ok) goto failed;
            options.abi = shared->abi = compatibility.abi;
        } else {
            if (primary && descriptor == provider->launch && provider->state.native.module) {
                role->module = provider->state.native.module; qa_native_module_retain(role->module);
            } else if (!qa_native_module_load(qa_resource_bytes(shared->resource), path,
                QA_NATIVE_Q3_VMMAIN, NULL, &role->module, error)) goto failed;
            if (primary && descriptor == provider->launch && !provider->state.native.module) {
                provider->state.native.module = role->module; qa_native_module_retain(role->module);
            }
            if (primary && descriptor->declaration && !qa_native_declaration_load(
                qa_resource_bytes(descriptor->declaration), path, role->module, &role->declaration, error)) goto failed;
        }
        if (role->image && kind == QA_QVM_CGAME && !application_q3_equipment_profile_read(role->image, kind,
            shared->abi, (qa_bytes){compatibility.equipment_presentation.data,
                compatibility.equipment_presentation.size}, &shared->equipment_profile, error)) {
            goto failed;
        }
        if (role->image && kind == QA_QVM_GAME && !application_q3_grapple_profile_create(role->image,
            kind, shared->abi, path, &shared->grapple_profile, error)) goto failed;
        shared->image = role->image; qa_qvm_image_retain(shared->image);
        shared->module = role->module; qa_native_module_retain(shared->module);
        shared->declaration = role->declaration;
        shared->primary = compatibility.primary; compatibility.primary = (qa_buffer){0};
        shared->equipment_presentation = compatibility.equipment_presentation;
        compatibility.equipment_presentation = (qa_buffer){0};
    }
    q3g_server_bind(role, &options);
    if (!q3g_client_bind(role, &options, error)) goto failed;
    qa_q3_host_client_services bound_client = options.client;
    qa_q3_host_options bound_services = options;
    if (kind != QA_QVM_GAME && provider->application->q3_client_prepare) {
        application_provider *source = role->client_source ? role->client_source :
            q3g_game_source(provider->application);
        qa_console *console = NULL; qa_cvars *cvars = NULL;
        if (source) {
            if (source->kind == APPLICATION_PROVIDER_Q3)
                application_native_q3_console_at(source, &console, &cvars, NULL);
            else {
                console = application_guest_q3_console_owner(source);
                cvars = application_guest_q3_console_registry(source);
            }
            if (!console || !cvars) {
                application_fail(error, QA_ERROR_ARGUMENT, "Client preparation lost its actual GAME registry");
                goto failed;
            }
        }
        qa_application_q3_client_preparation preparation = {
            .receiver_descriptor = descriptor, .game_descriptor = source ? source->launch : NULL,
            .receiver_catalog = qa_launch_instance_catalog(descriptor),
            .receiver_product = qa_catalog_product(qa_launch_instance_catalog(descriptor), descriptor->selection.product),
            .receiver = provider->owner, .source_owner = source ? source->owner : 0,
            .role = kind, .seat = seat, .source_client = role->client,
            .source_catalog = source ? source->product_catalog : NULL,
            .source_product = source ? source->product : NULL,
            .source_console = console, .source_cvars = cvars,
            .product_policy = application_q3_product_source_policy(provider->application),
            .services = &options, .equipment_services = &equipment_services,
            .restoring = engine->restore_pending, .restored_cvars = engine->restored_client_cvars,
            .cvars_role = engine->restore_pending ? engine->restored_client_role : kind,
            .cvars_seat = engine->restore_pending ? engine->restored_client_seat : seat};
        ++engine->calls;
        bool prepared = provider->application->q3_client_prepare(provider->application->guest_context,
            provider->application, &preparation, error);
        --engine->calls;
        if (!prepared) goto failed;
        if (options.role != kind || options.owner != provider->owner || options.session != provider->application->session ||
            options.service_owner != role->service_owner || options.cvars != bound_services.cvars ||
            options.console != bound_services.console || !options.cvars || !options.console ||
            options.mounts != descriptor->content || options.command_context.owner != provider->owner ||
            options.command_context.seat != seat || options.client.context != bound_client.context ||
            options.client.gamestate != bound_client.gamestate ||
            options.client.current_snapshot != bound_client.current_snapshot ||
            options.client.snapshot != bound_client.snapshot ||
            options.client.server_command != bound_client.server_command ||
            options.client.current_command != bound_client.current_command ||
            options.client.user_command != bound_client.user_command ||
            options.client.command_values != bound_client.command_values ||
            options.client.source_actor != bound_client.source_actor ||
            options.frontend_lifetime != bound_services.frontend_lifetime ||
            options.release_frontend != bound_services.release_frontend ||
            options.client_time_cvars != bound_services.client_time_cvars ||
            options.client_time_owner != bound_services.client_time_owner ||
            options.collision.context != bound_services.collision.context ||
            options.collision.geometry != bound_services.collision.geometry ||
            options.collision.load_map != bound_services.collision.load_map) {
            application_fail(error, QA_ERROR_ARGUMENT, "Client preparation changed its physical role ownership");
            goto failed;
        }
    }
    role->abi = options.abi; role->client_services = options.client;
    if (kind == QA_QVM_GAME) {
        qa_bot_runtime *runtime = application_bots_guest_runtime(provider->application, provider);
        if (runtime) {
            options.bots = runtime;
            options.remapped_bot_namespace = true;
            options.shared_bot_lifetime = false;
            options.script_globals = qa_bot_runtime_global_defines(runtime);
        }
    }
    if (role->image && kind == QA_QVM_CGAME) {
        options.source_entity = equipment_entity;
        options.source_entity_context = role;
    }
    if (!qa_q3_host_create(&options, &role->host, error)) goto failed;
    options.frontend_lifetime=NULL;options.release_frontend=NULL;
    if (role->image) {
        qa_qvm_options vm = qa_q3_host_qvm_options(role->host, QA_QVM_COMPILED_SEMANTICS);
        if (!qa_qvm_create(role->image, &vm, &role->vm, error) ||
            !qa_q3_host_attach_qvm(role->host, role->vm, error)) goto failed;
        if (kind == QA_QVM_CGAME && !application_q3_equipment_create(role,
            &equipment_services, &role->equipment, error)) goto failed;
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

bool q3g_role_create(struct application_q3_guest *engine, qa_qvm_role kind,
                      uint32_t seat, const char *path, bool primary,
                      q3g_role **out, qa_error *error)
{
    return role_create(engine, kind, seat, path, primary, 0, QA_STRING_NONE, out, error);
}

bool q3g_role_create_restored(struct application_q3_guest *engine, qa_qvm_role kind,
                               uint32_t seat, const char *path, bool primary,
                               uint64_t service_sequence, qa_string_id service_owner,
                               q3g_role **out, qa_error *error)
{
    if (!service_owner || !service_sequence)
        return application_fail(error, QA_ERROR_FORMAT, "Restored Q3 role requires its saved source owner");
    return role_create(engine, kind, seat, path, primary, service_sequence, service_owner, out, error);
}

bool q3g_role_activate(q3g_role *role, qa_error *error)
{
    if (!role || !role->ready || !role->host || role->retired || role->engine->restore_pending)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 activation requires an admitted role");
    if (role->activation_failed) {
        if (error) *error = role->activation_error;
        return false;
    }
    if (role->committed) return true;
    struct application_q3_guest *engine = role->engine;
    if (engine->calls || !engine->provider->constructed || !engine->provider->attached)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 source activation requires a committed idle provider");
    qa_bot_runtime *bots = application_bots_guest_runtime(engine->provider->application, engine->provider);
    if (role->kind == QA_QVM_GAME && bots &&
        !application_bots_guest_bind(engine->provider->application,engine->provider,role->host,error)) return false;
    if (role->vm) { role->committed = true; return true; }
    if (!role->module)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 native role has no staged module");
    qa_error failure = {0};
    q3g_role *previous = engine->entered_role;
    engine->entered_role = role;
    ++engine->calls;
    bool ok = qa_native_host_create_q3(role->module, &role->native_options, &role->native, &failure);
    if (ok) ok = qa_q3_host_attach_native(role->host, role->native, &failure);
    if (ok && role->kind == QA_QVM_GAME)
        ok = application_guest_input_attach(role,
            qa_native_declaration_primary(role->artifact->declaration), &failure);
    --engine->calls;
    engine->entered_role = previous;
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

bool q3g_role_shutdown_source(q3g_role *role, bool restart, qa_error *error)
{
    if (!role || role->engine->calls || !qa_world_idle(role->engine->world) ||
        (role->vm && !qa_qvm_can_destroy(role->vm)) ||
        (role->native && !qa_native_can_destroy(qa_native_host_instance(role->native))))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 shutdown requires idle source and world callbacks");
    if (!role->initialized) return true;
    if (role->kind == QA_QVM_GAME && !application_guest_input_detach(role, error)) return false;
    /* Consume the callback attempt before source code can reenter its owner.
     * Failure does not authorize a second partially performed shutdown. */
    role->initialized = false;
    role->init_succeeded = false;
    role->shutdown_entry = role->kind == QA_QVM_GAME;
    bool prior_entry = role->engine->round.source_entry;
    role->engine->round.source_entry = true;
    bool ok, started = false;
    q3g_role *previous = role->engine->entered_role;
    role->engine->entered_role = role;
    ++role->engine->calls;
    if (role->native) {
        ok = qa_native_host_shutdown(role->native, role->kind == QA_QVM_GAME && restart, error);
        qa_native_lifecycle lifecycle = qa_native_get_lifecycle(qa_native_host_instance(role->native));
        started = lifecycle == QA_NATIVE_SHUT_DOWN || lifecycle == QA_NATIVE_RESTART_READY;
    } else {
        int32_t words[] = {role->kind == QA_QVM_UI ? 2 : 1, restart ? 1 : 0}, result;
        ok = qa_qvm_invoke_started(role->vm, 0, words,
            role->kind == QA_QVM_GAME ? 2 : 1, &result, &started, error);
    }
    --role->engine->calls;
    role->engine->entered_role = previous;
    role->engine->round.source_entry = prior_entry;
    role->shutdown_entry = false;
    role->initialized = !started;
    return ok;
}

bool q3g_role_shutdown(q3g_role *role, bool restart, qa_error *error)
{
    bool ok = q3g_role_shutdown_source(role, restart, error);
    if (role && !role->initialized) role->retired = true;
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
    bool shut_down = q3g_role_shutdown(role, false, &shutdown_error);
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
