#include "guest_q3_factory.h"
#include "guest_q3_private.h"
#include "guest_q3_console.h"
#include "qa/application_q3_factory.h"

bool q3g_acquisition_copy(const qa_vfs_acquisition *source, qa_vfs_acquisition *out, qa_error *error)
{
    if (!source || !source->mount || !source->resource_id || !source->path || !source->lookup_path ||
        !out || out->resource_id)
        return application_fail(error, QA_ERROR_ARGUMENT, "Artifact receipt requires its genuine acquisition");
    qa_vfs_acquisition copy = {.mount = source->mount, .resource_id = source->resource_id};
    copy.path = q3g_copy_text(source->path, error);
    copy.lookup_path = q3g_copy_text(source->lookup_path, error);
    if (source->link_source) copy.link_source = q3g_copy_text(source->link_source, error);
    if (source->link_target) copy.link_target = q3g_copy_text(source->link_target, error);
    if (!copy.path || !copy.lookup_path || (source->link_source && !copy.link_source) ||
        (source->link_target && !copy.link_target)) { qa_vfs_acquisition_dispose(&copy); return false; }
    *out = copy; return true;
}

bool q3g_compatibility(const qa_launch_instance *descriptor, const char *path,
    const qa_qvm_image *image, qa_qvm_role kind, bool primary,
    qa_qvm_compatibility *out, qa_error *error)
{
    const qa_resource *declaration = primary ? descriptor->declaration : NULL;
    for (size_t i = 0; !declaration && i < descriptor->interface_count; ++i)
        if (!strcmp(descriptor->interfaces[i].path, "qvm-compatibility.json"))
            declaration = descriptor->interfaces[i].resource;
    if (!declaration) { *out = (qa_qvm_compatibility){.abi = QA_QVM_Q3_MODERN}; return true; }
    return qa_qvm_compatibility_parse(qa_resource_bytes(declaration), path,
        qa_qvm_image_digest(image), kind, out, error);
}

static application_provider *receiver_provider(qa_application *app, qa_actor_owner owner)
{
    if (!app || !owner) return NULL;
    application_provider **providers = app->routing_providers ? app->routing_providers : app->providers;
    size_t count = app->routing_providers ? app->routing_provider_count : app->provider_count;
    application_provider *found = NULL;
    for (size_t i = 0; i < count; ++i) if (providers[i] && providers[i]->owner == owner) {
        if (found) return NULL;
        found = providers[i];
    }
    return found;
}

static bool same_descriptor(const qa_launch_instance *actual, const qa_launch_instance *retained)
{
    return actual && retained && actual->storage == retained->storage &&
        actual->content == retained->content && actual->roles == retained->roles &&
        qa_sha256_equal(&actual->identity, &retained->identity);
}

bool qa_application_q3_remote_source_read(qa_application *app, qa_actor_owner receiver,
    uint32_t seat, uint64_t epoch, qa_application_q3_remote_source *out, qa_error *error)
{
    application_provider *provider = receiver_provider(app, receiver);
    struct application_q3_guest *engine = q3g_engine(provider);
    if (!out || !epoch || !engine || (engine->connection_epoch && engine->connection_epoch != epoch))
        return application_fail(error, QA_ERROR_ARGUMENT, "Remote source differs from its actual connection lifetime");
    qa_application_q3_remote_source source = {.descriptor = engine->client_descriptor ?
        qa_launch_instance_lease_view(engine->client_descriptor) : provider->launch,
        .configuration_generation = engine->client_generation ? engine->client_generation :
            qa_application_configuration_generation(app), .connection_epoch = epoch};
    if (!source.configuration_generation || !qa_application_q3_remote_context_read(app,
        receiver, seat, &source.receiver, error)) return false;
    *out = source; return true;
}

bool qa_application_q3_remote_source_current(qa_application *app,
    const qa_application_q3_remote_source *source)
{
    qa_application_q3_remote_source actual;
    return source && qa_application_q3_remote_source_read(app, source->receiver.receiver,
        source->receiver.seat, source->connection_epoch, &actual, NULL) &&
        same_descriptor(actual.descriptor, source->descriptor) && actual.configuration_generation == source->configuration_generation &&
        qa_application_q3_remote_context_current(app, &source->receiver);
}

static bool discard_role(q3g_role **role, qa_error *error)
{
    if (!*role) return true;
    if (!q3g_role_shutdown(*role, false, error) || !q3g_role_destroy(*role, error)) return false;
    *role = NULL; return true;
}

bool qa_application_q3_remote_replace(qa_application *app,
    const qa_application_q3_remote_replacement *request, qa_application_q3_remote_source *out, qa_error *error)
{
    if (!app || !request || !out || app->operation != APPLICATION_IDLE || app->frame_preparing ||
        app->q3_round_active || app->q3_world_restart || app->destroy_requested ||
        !qa_session_safe(app->session) || qa_session_faulted(app->session) || !qa_world_idle(app->world) ||
        !request->catalog || !request->prepared_mounts || !request->cgame_path || !*request->cgame_path ||
        !request->ui_path || !*request->ui_path || request->connection_epoch != request->previous.connection_epoch ||
        request->previous.configuration_generation == UINT64_MAX ||
        !qa_application_q3_remote_source_current(app, &request->previous))
        return application_fail(error, QA_ERROR_ARGUMENT, "Remote replacement requires its current detached content and idle receiver");
    application_provider *provider = receiver_provider(app, request->previous.receiver.receiver);
    struct application_q3_guest *engine = q3g_engine(provider);
    if (!engine || engine->calls || engine->client_candidate || !application_q3_guest_idle(provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "Remote replacement has an active receiver source");
    qa_launch_instance_lease *descriptor = NULL;
    if (!qa_launch_instance_prepare_client_metadata(request->previous.descriptor, request->catalog,
        request->product, request->prepared_mounts, request->cgame_path, &descriptor, error)) return false;
    q3g_role *cgame = NULL, *ui = NULL;
    app->operation = APPLICATION_CONFIGURING;
    engine->client_candidate = qa_launch_instance_lease_view(descriptor);
    bool ok = q3g_role_create(engine, QA_QVM_CGAME, request->previous.receiver.seat,
        request->cgame_path, true, &cgame, error) && q3g_role_create(engine, QA_QVM_UI,
        request->previous.receiver.seat, request->ui_path, false, &ui, error);
    engine->client_candidate = NULL;
    if (ok && (cgame->local_client || ui->local_client))
        ok = application_fail(error, QA_ERROR_ARGUMENT, "Remote replacement acquired a local GAME binding");
    if (ok) {
        q3g_role **position = &engine->roles;
        while (*position && ok) {
            q3g_role *role = *position;
            if (role->kind == QA_QVM_GAME || role->seat != request->previous.receiver.seat) {
                position = &role->next; continue;
            }
            q3g_role *next = role->next;
            ok = q3g_role_shutdown(role, false, error) && q3g_role_destroy(role, error);
            if (ok) *position = next;
        }
    }
    if (ok) {
        cgame->next = engine->roles; engine->roles = cgame;
        ui->next = engine->roles; engine->roles = ui;
        qa_launch_instance_lease_release(engine->client_descriptor);
        engine->client_descriptor = descriptor; descriptor = NULL;
        engine->client_generation = request->previous.configuration_generation + 1;
        engine->connection_epoch = request->connection_epoch;
    } else {
        /* Failed physical cleanup stays reachable by the genuine provider. */
        if (!discard_role(&cgame, NULL)) { cgame->next = engine->roles; engine->roles = cgame; }
        if (!discard_role(&ui, NULL)) { ui->next = engine->roles; engine->roles = ui; }
    }
    qa_launch_instance_lease_release(descriptor);
    app->operation = APPLICATION_IDLE;
    return ok && qa_application_q3_remote_source_read(app, provider->owner,
        request->previous.receiver.seat, request->connection_epoch, out, error);
}

bool qa_application_q3_role_receipt_read(qa_application *app, qa_actor_owner receiver,
    qa_qvm_role kind, uint32_t seat, qa_application_q3_role_receipt *out, qa_error *error)
{
    application_provider *provider = receiver_provider(app, receiver);
    struct application_q3_guest *engine = q3g_engine(provider);
    q3g_role *role = NULL;
    if (engine) for (q3g_role *current = engine->roles; current; current = current->next)
        if (current->kind == kind && current->seat == seat && current->ready && !current->retired) {
            if (role) return application_fail(error, QA_ERROR_ARGUMENT, "Ambiguous actual role receipt");
            role = current;
        }
    if (!out || !provider || !provider->constructed || !provider->attached || provider->close_pending ||
        !app || app->destroy_requested || !role || !role->committed || !role->init_succeeded || !role->artifact ||
        !role->artifact->resource || !qa_vfs_acquisition_retained(role->artifact->view,
            &role->artifact->acquisition, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "Role receipt requires its retained artifact and successful Init");
    *out = (qa_application_q3_role_receipt){.role = kind, .receiver = receiver, .seat = seat,
        .service_owner = role->service_owner, .configuration_generation = engine->client_generation ?
            engine->client_generation : qa_application_configuration_generation(app),
        .connection_epoch = engine->connection_epoch, .descriptor = role->descriptor,
        .artifact = role->artifact->resource, .acquisition = &role->artifact->acquisition,
        .artifact_view = role->artifact->view};
    return true;
}

bool qa_application_q3_remote_initialize(qa_application *app,
    const qa_application_q3_remote_init *request, qa_error *error)
{
    if (!app || !request || app->operation != APPLICATION_IDLE || !request->current ||
        request->server_message < 0 || request->last_executed_server_command < 0 ||
        request->client_number < 0 || request->client_number >= 64 ||
        !qa_application_q3_remote_source_current(app, &request->source) ||
        !request->current(request->connection, request->source.connection_epoch,
            request->server_message, request->last_executed_server_command,
            request->client_number, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "Remote Init requires its actual decoded connection counters");
    application_provider *provider = receiver_provider(app, request->source.receiver.receiver);
    return application_q3_guest_role_initialize(provider, QA_QVM_CGAME, request->source.receiver.seat,
        request->server_message, request->last_executed_server_command, request->client_number, false, error) &&
        request->current(request->connection, request->source.connection_epoch,
            request->server_message, request->last_executed_server_command, request->client_number, error) &&
        application_q3_guest_role_initialize(provider, QA_QVM_UI, request->source.receiver.seat,
            request->server_message, request->last_executed_server_command, request->client_number, false, error);
}

bool qa_application_q3_role_receipt_current(qa_application *app,
    const qa_application_q3_role_receipt *receipt)
{
    qa_application_q3_role_receipt actual;
    return receipt && qa_application_q3_role_receipt_read(app, receipt->receiver, receipt->role,
        receipt->seat, &actual, NULL) && actual.service_owner == receipt->service_owner &&
        actual.configuration_generation == receipt->configuration_generation &&
        actual.connection_epoch == receipt->connection_epoch && same_descriptor(actual.descriptor, receipt->descriptor) &&
        actual.artifact == receipt->artifact && actual.acquisition == receipt->acquisition &&
        actual.artifact_view == receipt->artifact_view;
}

bool qa_application_q3_content_visit(const qa_application *app,
    const qa_application_content_visitor *visitor, qa_error *error)
{
    if (!app || !visitor || !visitor->catalog || !visitor->view)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 content visitor requires actual retained owners");
    for (size_t i = 0; i < app->provider_count; ++i) {
        struct application_q3_guest *engine = q3g_engine(app->providers[i]);
        if (!engine) continue;
        if (engine->client_candidate || engine->calls)
            return application_fail(error, QA_ERROR_ARGUMENT, "Q3 content source has an unfinished replacement");
        if (engine->client_descriptor) {
            const qa_launch_instance *descriptor = qa_launch_instance_lease_view(engine->client_descriptor);
            if (!visitor->catalog(visitor->context, qa_launch_instance_catalog(descriptor), error) ||
                !visitor->view(visitor->context, descriptor->content, error)) return false;
        }
        for (q3g_artifact *artifact = engine->artifacts; artifact; artifact = artifact->next) {
            const qa_launch_instance *descriptor = qa_launch_instance_lease_view(artifact->descriptor);
            if (!descriptor || descriptor->content != artifact->view || !artifact->resource ||
                artifact->acquisition.resource_id != qa_resource_id(artifact->resource) ||
                !qa_vfs_acquisition_valid(artifact->view, &artifact->acquisition, error) ||
                !visitor->catalog(visitor->context, qa_launch_instance_catalog(descriptor), error) ||
                !visitor->view(visitor->context, artifact->view, error)) return false;
        }
    }
    return true;
}

bool application_guest_q3_factory_reuse(qa_application *app, application_provider *provider,
    qa_world *world, const qa_product *product, const qa_launch_choices *choices, qa_error *error)
{
    struct application_q3_guest *engine = q3g_engine(provider);
    if (!app || !provider || provider->application != app || !world || !product || !choices ||
        !engine || engine->world != world || provider->product != product ||
        product->family != QA_GAME_Q3 || choices->seat_count > 64 || engine->calls ||
        engine->roles || engine->game || engine->artifacts || engine->restore_pending ||
        engine->map_ready || !application_guest_q3_console_idle(engine))
        return application_fail(error, QA_ERROR_ARGUMENT,
            "Original Q3 construction lost its retained preconfiguration engine");
    for (size_t i = 0; i < 64; ++i)
        if (engine->seats[i] != (i < choices->seat_count ? choices->seats[i].id : UINT32_MAX))
            return application_fail(error, QA_ERROR_ARGUMENT,
                "Original Q3 construction changed its prepared physical seat topology");
    return true;
}

bool application_guest_q3_console_prepare(qa_application *app, application_provider *provider,
    qa_world *world, const qa_product *product, const qa_launch_choices *choices,
    qa_console **console, qa_cvars **cvars, qa_command_context *command, qa_error *error)
{
    if (!app || !provider || provider->application != app || !world || !product || !choices ||
        !console || !cvars || !command || provider->constructed || provider->attached ||
        provider->close_pending || product->family != QA_GAME_Q3 || !provider->launch ||
        !provider->launch->selection.artifact)
        return application_fail(error, QA_ERROR_ARGUMENT,
            "Original Q3 preparation requires its genuine detached world and descriptor");
    *console = NULL; *cvars = NULL;
    if (!q3g_engine(provider) && !application_guest_q3_create_empty(app, provider, world,
        product, choices, false, error)) return false;
    if (!application_guest_q3_factory_reuse(app, provider, world, product, choices, error)) return false;
    if (q3g_primary_role(provider->launch->selection.artifact) != QA_QVM_GAME) return true;
    *console = application_guest_q3_console_owner(provider);
    *cvars = application_guest_q3_console_registry(provider);
    *command = (qa_command_context){.owner = provider->owner,
        .dialect = QA_CONSOLE_Q3, .origin = QA_COMMAND_SERVER};
    return (*console && *cvars) || application_fail(error, QA_ERROR_ARGUMENT,
        "Original Q3 preparation did not retain its actual private GAME console");
}
