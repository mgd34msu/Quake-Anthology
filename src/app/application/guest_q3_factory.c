#include "guest_q3_factory.h"
#include "guest_q3_private.h"
#include "guest_q3_console.h"
#include "qa/application_q3_factory.h"
#include "native_q3_console.h"
#include "q3_product.h"
#include "qa/network_q3.h"

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

bool qa_application_q3_preconstruction_source_read(qa_application *app,
    qa_actor_owner receiver, qa_qvm_role kind, uint32_t seat,
    qa_application_q3_client_preparation *out, qa_error *error)
{
    application_provider *provider = receiver_provider(app, receiver);
    struct application_q3_guest *engine = q3g_engine(provider);
    q3g_role *role = engine ? engine->constructing_role : NULL;
    qa_q3_host_options *services = engine ? engine->constructing_services : NULL;
    if (!app || !out || !role || !services || !engine->calls || role->kind != kind ||
        role->seat != seat || role->host || services->owner != receiver || services->role != kind ||
        services->session != app->session || services->world != engine->world ||
        services->service_owner != role->service_owner || services->mounts != role->descriptor->content ||
        services->command_context.owner != receiver ||
        services->command_context.seat != (kind == QA_QVM_GAME ? 0 : seat))
        return application_fail(error, QA_ERROR_ARGUMENT, "Source preparation requires its real pending host construction");
    application_provider *source = kind == QA_QVM_GAME ? provider : q3g_game_source(app);
    qa_console *console = NULL; qa_cvars *cvars = NULL;
    if (source) {
        if (source->close_pending || !source->product || !source->product_catalog ||
            qa_catalog_product(source->product_catalog, source->product->id) != source->product)
            return application_fail(error, QA_ERROR_ARGUMENT, "Prepared GAME lost its actual source catalog");
        if (source->kind == APPLICATION_PROVIDER_Q3) {
            if (!application_native_q3_console_at(source, &console, &cvars, NULL))
                return application_fail(error, QA_ERROR_ARGUMENT, "Prepared native GAME lost its actual console");
        } else {
            console = application_guest_q3_console_owner(source);
            cvars = application_guest_q3_console_registry(source);
        }
        if (!console || !cvars)
            return application_fail(error, QA_ERROR_ARGUMENT, "Prepared original GAME lost its actual console");
    }
    *out = (qa_application_q3_client_preparation){.receiver_descriptor = role->descriptor,
        .receiver_catalog = qa_launch_instance_catalog(role->descriptor),
        .receiver_product = qa_catalog_product(qa_launch_instance_catalog(role->descriptor), role->descriptor->selection.product),
        .game_descriptor = source ? source->launch : NULL, .receiver = receiver,
        .source_owner = source ? source->owner : 0, .role = kind, .seat = seat,
        .source_client = UINT32_MAX, .source_catalog = source ? source->product_catalog : NULL,
        .source_product = source ? source->product : NULL, .source_console = console,
        .source_cvars = cvars, .product_policy = application_q3_product_source_policy(app),
        .services = services, .equipment_services = engine->constructing_equipment_services,
        .restoring = engine->restore_pending, .restored_cvars = engine->restored_client_cvars,
        .cvars_role = engine->restore_pending ? engine->restored_client_role : kind,
        .cvars_seat = engine->restore_pending ? engine->restored_client_seat : seat};
    return true;
}

bool qa_application_q3_equipment_requests(qa_application *app, qa_actor_owner receiver,
    uint32_t seat, bool *hud, bool *view, qa_error *error)
{
    application_provider *provider = receiver_provider(app, receiver);
    struct application_q3_guest *engine = q3g_engine(provider);
    q3g_role *role = NULL;
    if (engine) for (q3g_role *r = engine->roles; r; r = r->next)
        if (r->kind == QA_QVM_CGAME && r->seat == seat && r->ready && !r->retired) {
            if (role) return application_fail(error, QA_ERROR_ARGUMENT, "Ambiguous actual equipment receiver");
            role = r;
        }
    qa_q3_host_client_context host;
    if (!app || !hud || !view || app->destroy_requested || !provider ||
        !provider->constructed || !provider->attached || provider->close_pending ||
        !role || !role->committed || !role->init_succeeded || !role->equipment ||
        !qa_q3_host_client_context_read(role->host, &host) || host.session != app->session ||
        host.owner != receiver || host.role != QA_QVM_CGAME || host.service_owner != role->service_owner ||
        host.command_context.owner != receiver || host.command_context.seat != seat ||
        host.command_context.dialect != QA_CONSOLE_Q3)
        return application_fail(error, QA_ERROR_ARGUMENT, "Equipment requests lack their true current CGAME owner");
    *hud = application_q3_equipment_hud(role->equipment);
    *view = application_q3_equipment_view(role->equipment);
    return true;
}

bool qa_application_q3_role_loading(qa_application *app, qa_actor_owner receiver,
    qa_qvm_role kind, uint32_t seat, bool *out, qa_error *error)
{
    application_provider *provider = receiver_provider(app, receiver);
    if (!app || !out || !provider || !provider->constructed || !provider->attached ||
        provider->close_pending || app->destroy_requested || kind == QA_QVM_GAME || kind > QA_QVM_UI)
        return application_fail(error, QA_ERROR_ARGUMENT, "Loading observation requires its live actual receiver");
    *out = application_q3_guest_role_loading(provider, kind, seat);
    return true;
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

static application_provider *menu_provider(qa_application *app, uint32_t seat)
{
    const qa_launch_snapshot *snapshot = app->routing_snapshot ? app->routing_snapshot : qa_application_launch(app);
    const qa_launch_choices *choices = qa_launch_snapshot_choices(snapshot);
    const qa_launch_binding *binding = choices ? qa_launch_binding_for(choices,
        (qa_launch_scope){.kind = QA_SCOPE_SEAT, .seat = seat}, QA_ROLE_MENU, "") : NULL;
    if (!binding && choices) binding = qa_launch_binding_for(choices,
        (qa_launch_scope){.kind = QA_SCOPE_WORLD}, QA_ROLE_MENU, "");
    application_provider **providers = app->routing_providers ? app->routing_providers : app->providers;
    size_t count = app->routing_providers ? app->routing_provider_count : app->provider_count;
    for (size_t i = 0; binding && i < count; ++i)
        if (providers[i] && providers[i]->launch &&
            !strcmp(providers[i]->launch->selection.instance, binding->instance)) return providers[i];
    return NULL;
}

bool qa_application_q3_remote_recipe_read(qa_application *app,
    const qa_application_q3_remote_source *source, const qa_q3_gamestate *decoded,
    qa_application_q3_remote_recipe *out, qa_error *error)
{
    if (!out || !decoded || decoded->client_number < 0 || decoded->client_number >= 64 ||
        !qa_application_q3_remote_source_current(app, source))
        return application_fail(error, QA_ERROR_ARGUMENT, "Remote recipe requires its actual retained receiver");
    qa_application_q3_remote_recipe recipe = {0};
    if (!qa_application_q3_role_artifact_read(app, source->receiver.receiver,
        QA_QVM_CGAME, source->receiver.seat, &recipe.cgame, error)) return false;
    application_provider *menu = menu_provider(app, source->receiver.seat);
    recipe.menu_receiver = menu ? menu->owner : 0;
    recipe.replace_ui = true;
    if (!qa_application_q3_role_artifact_read(app, source->receiver.receiver,
        QA_QVM_UI, source->receiver.seat, &recipe.ui, error)) return false;
    char pure[1024];
    if (!qa_q3_info_value(qa_q3_configstring(decoded, 1), "sv_pure", pure, sizeof(pure), error)) return false;
    application_provider *provider = receiver_provider(app, source->receiver.receiver);
    struct application_q3_guest *engine = q3g_engine(provider);
    q3g_role *cgame = NULL, *ui = NULL;
    for (q3g_role *role = engine->roles; role; role = role->next)
        if (role->seat == source->receiver.seat && role->ready && !role->retired) {
            if (role->kind == QA_QVM_CGAME) cgame = role;
            if (role->kind == QA_QVM_UI) ui = role;
        }
    bool bytecode = strtol(pure, NULL, 10) != 0;
    const qa_cvar_view *restricted = qa_cvars_find(source->receiver.cvars, "fs_restrict");
    bytecode |= restricted && restricted->number != 0;
    recipe.cgame_path = bytecode && !cgame->artifact->qvm ? "vm/cgame.qvm" : recipe.cgame.path;
    recipe.ui_path = bytecode && !ui->artifact->qvm ? "vm/ui.qvm" : recipe.ui.path;
    recipe.cgame_runtime = bytecode || cgame->artifact->qvm ? QA_PROGRAM_QVM : QA_PROGRAM_NATIVE;
    *out = recipe; return true;
}

bool qa_application_q3_remote_clear(qa_application *app,
    const qa_application_q3_remote_source *previous,
    qa_application_q3_remote_source *out, qa_error *error)
{
    if (!app || !out || app->operation != APPLICATION_IDLE || app->frame_preparing ||
        app->q3_round_active || app->q3_world_restart || !qa_session_safe(app->session) ||
        !qa_world_idle(app->world) || !qa_application_q3_remote_source_current(app, previous))
        return application_fail(error, QA_ERROR_ARGUMENT, "Remote clear requires its current idle decoder boundary");
    application_provider *provider = receiver_provider(app, previous->receiver.receiver);
    struct application_q3_guest *engine = q3g_engine(provider);
    if (!engine || !application_q3_guest_idle(provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "Remote clear has an active source callback");
    for (q3g_role *role = engine->roles; role; role = role->next)
        if (role->kind != QA_QVM_GAME && role->seat == previous->receiver.seat && role->local_client)
            return application_fail(error, QA_ERROR_ARGUMENT, "Remote clear encountered a real local GAME client");
    for (q3g_role *role = engine->roles; role; role = role->next)
        if (role->kind != QA_QVM_GAME && role->seat == previous->receiver.seat)
            role->source_cleared = true;
    app->operation = APPLICATION_CONFIGURING;
    bool ok = true;
    for (q3g_role *role = engine->roles; role && ok; role = role->next)
        if (role->kind != QA_QVM_GAME && role->seat == previous->receiver.seat) {
            ok = q3g_role_shutdown_source(role, false, error);
        }
    app->operation = APPLICATION_IDLE;
    return ok && qa_application_q3_remote_source_read(app, provider->owner,
        previous->receiver.seat, previous->connection_epoch, out, error);
}

bool qa_application_q3_remote_rebind(qa_application *app,
    const qa_application_q3_remote_binding *request, qa_application_q3_remote_source *out, qa_error *error)
{
    if (!app || !request || !out || !request->current || !request->connection ||
        app->operation != APPLICATION_IDLE || app->frame_preparing || app->destroy_requested ||
        app->q3_round_active || app->q3_world_restart || !qa_session_safe(app->session) ||
        qa_session_faulted(app->session) || !qa_world_idle(app->world) ||
        request->new_epoch <= request->previous.connection_epoch ||
        !qa_application_q3_remote_source_current(app, &request->previous) ||
        !request->current(request->connection, request->previous.connection_epoch, request->new_epoch, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "Remote rebind requires its actual closed transport and fresh admission");
    application_provider *provider = receiver_provider(app, request->previous.receiver.receiver);
    struct application_q3_guest *engine = q3g_engine(provider);
    if (!engine || engine->calls || engine->initializing_role || engine->client_candidate ||
        engine->restore_pending || !application_q3_guest_idle(provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "Remote rebind has an unfinished source lifecycle");
    bool cgame = false, ui = false;
    for (q3g_role *role = engine->roles; role; role = role->next)
        if (role->kind != QA_QVM_GAME && role->seat == request->previous.receiver.seat) {
            if (!role->ready || role->retired || role->local_client || !role->source_cleared ||
                role->initialized || role->init_succeeded ||
                !same_descriptor(role->descriptor, request->previous.descriptor))
                return application_fail(error, QA_ERROR_ARGUMENT, "Remote rebind requires its retained cleared source roles");
            if (role->kind == QA_QVM_CGAME) {
                if (cgame) return application_fail(error, QA_ERROR_ARGUMENT, "Ambiguous retained reconnect CGAME");
                cgame = true;
            }
            if (role->kind == QA_QVM_UI) {
                if (ui) return application_fail(error, QA_ERROR_ARGUMENT, "Ambiguous retained reconnect UI");
                ui = true;
            }
        }
    if (!cgame || !ui || !qa_application_q3_remote_source_current(app, &request->previous) ||
        !request->current(request->connection, request->previous.connection_epoch, request->new_epoch, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "Remote rebind lost its actual admission proof");
    qa_application_q3_remote_source rebound = request->previous;
    rebound.connection_epoch = request->new_epoch;
    engine->connection_epoch = request->new_epoch;
    *out = rebound;
    return true;
}

bool qa_application_q3_remote_replace(qa_application *app,
    const qa_application_q3_remote_replacement *request, qa_application_q3_remote_source *out, qa_error *error)
{
    if (!app || !request || !out || app->operation != APPLICATION_IDLE || app->frame_preparing ||
        app->q3_round_active || app->q3_world_restart || app->destroy_requested ||
        !qa_session_safe(app->session) || qa_session_faulted(app->session) || !qa_world_idle(app->world) ||
        !request->catalog || !request->prepared_mounts || !request->cgame_path || !*request->cgame_path ||
        !request->ui_path || !*request->ui_path || request->connection_epoch != request->previous.connection_epoch ||
        (request->cgame_runtime != QA_PROGRAM_QVM && request->cgame_runtime != QA_PROGRAM_NATIVE) ||
        request->previous.configuration_generation == UINT64_MAX ||
        !qa_application_q3_remote_source_current(app, &request->previous))
        return application_fail(error, QA_ERROR_ARGUMENT, "Remote replacement requires its current detached content and idle receiver");
    application_provider *provider = receiver_provider(app, request->previous.receiver.receiver);
    struct application_q3_guest *engine = q3g_engine(provider);
    if (!engine || engine->calls || engine->client_candidate || !application_q3_guest_idle(provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "Remote replacement has an active receiver source");
    qa_launch_instance_lease *descriptor = NULL;
    if (!qa_launch_instance_prepare_client_metadata(request->previous.descriptor, request->catalog,
        request->product, request->prepared_mounts, request->cgame_path, request->cgame_runtime,
        &descriptor, error)) return false;
    q3g_role *cgame = NULL, *ui = NULL;
    app->operation = APPLICATION_CONFIGURING;
    engine->client_candidate = qa_launch_instance_lease_view(descriptor);
    bool ok = q3g_role_create(engine, QA_QVM_CGAME, request->previous.receiver.seat,
        request->cgame_path, true, &cgame, error) && q3g_role_create(engine, QA_QVM_UI,
        request->previous.receiver.seat, request->ui_path, false, &ui, error);
    engine->client_candidate = NULL;
    if (ok && (cgame->local_client || (ui && ui->local_client)))
        ok = application_fail(error, QA_ERROR_ARGUMENT, "Remote replacement acquired a local GAME binding");
    if (ok && engine->game) cgame->primary = false;
    if (ok) { cgame->source_cleared = true; if (ui) ui->source_cleared = true; }
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
        if (ui) { ui->next = engine->roles; engine->roles = ui; }
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

static bool role_artifact_read(qa_application *app, qa_actor_owner receiver,
    qa_qvm_role kind, uint32_t seat, bool initialized,
    qa_application_q3_role_artifact *out, qa_error *error)
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
        !app || app->destroy_requested || !role ||
        (initialized && (!role->committed || !role->init_succeeded || role->source_cleared)) || !role->artifact ||
        !role->artifact->resource || !qa_vfs_acquisition_retained(role->artifact->view,
            &role->artifact->acquisition, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "Role artifact requires its actual retained opening and lifecycle");
    out->path = role->path;
    out->source = (qa_application_q3_role_receipt){.role = kind, .receiver = receiver, .seat = seat,
        .service_owner = role->service_owner, .configuration_generation = kind != QA_QVM_GAME && engine->client_generation ?
            engine->client_generation : qa_application_configuration_generation(app),
        .connection_epoch = kind != QA_QVM_GAME ? engine->connection_epoch : 0, .descriptor = role->descriptor,
        .artifact = role->artifact->resource, .acquisition = &role->artifact->acquisition,
        .artifact_view = role->artifact->view};
    return true;
}

bool qa_application_q3_role_artifact_read(qa_application *app, qa_actor_owner receiver,
    qa_qvm_role kind, uint32_t seat, qa_application_q3_role_artifact *out, qa_error *error)
{ return role_artifact_read(app, receiver, kind, seat, false, out, error); }

bool qa_application_q3_role_receipt_read(qa_application *app, qa_actor_owner receiver,
    qa_qvm_role kind, uint32_t seat, qa_application_q3_role_receipt *out, qa_error *error)
{
    qa_application_q3_role_artifact actual;
    if (!out || !role_artifact_read(app, receiver, kind, seat, true, &actual, error)) return false;
    *out = actual.source; return true;
}

bool qa_application_q3_remote_initialize(qa_application *app,
    const qa_application_q3_remote_init *request, qa_error *error)
{
    if (!app || !request || app->operation != APPLICATION_IDLE || !request->current ||
        request->server_message < 0 ||
        request->client_number < 0 || request->client_number >= 64 ||
        !qa_application_q3_remote_source_current(app, &request->source) ||
        !request->current(request->connection, request->source.connection_epoch,
            request->server_message, request->last_executed_server_command,
            request->client_number, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "Remote Init requires its actual decoded connection counters");
    application_provider *provider = receiver_provider(app, request->source.receiver.receiver);
    struct application_q3_guest *engine = q3g_engine(provider);
    q3g_role *cgame = NULL, *ui = NULL;
    for (q3g_role *role = engine->roles; role; role = role->next)
        if (role->seat == request->source.receiver.seat && role->ready && !role->retired) {
            if (role->kind == QA_QVM_CGAME) cgame = role;
            if (role->kind == QA_QVM_UI) ui = role;
        }
    if (!cgame || !ui || engine->initializing_role)
        return application_fail(error, QA_ERROR_ARGUMENT, "Remote Init lost its retained source role");
    engine->initializing_role = ui;
    bool ok = application_q3_guest_role_initialize(provider, QA_QVM_UI, request->source.receiver.seat,
        request->server_message, request->last_executed_server_command, request->client_number, true, error);
    engine->initializing_role = NULL;
    if (ok) ok = request->current(request->connection, request->source.connection_epoch,
        request->server_message, request->last_executed_server_command, request->client_number, error);
    if (ok) {
        engine->initializing_role = cgame;
        ok = application_q3_guest_role_initialize(provider, QA_QVM_CGAME, request->source.receiver.seat,
            request->server_message, request->last_executed_server_command, request->client_number, false, error);
        engine->initializing_role = NULL;
    }
    if (ok) ok = request->current(request->connection, request->source.connection_epoch,
        request->server_message, request->last_executed_server_command, request->client_number, error);
    if (ok) { cgame->source_cleared = false; ui->source_cleared = false; }
    return ok;
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

bool qa_application_q3_source_loading_screen(qa_application *app, qa_actor_owner receiver,
    uint32_t seat, bool *drawn, qa_error *error)
{
    application_provider *provider = receiver_provider(app, receiver);
    struct application_q3_guest *engine = q3g_engine(provider);
    if (!app || !drawn || !provider || !provider->constructed || !provider->attached ||
        provider->close_pending || app->destroy_requested)
        return application_fail(error, QA_ERROR_ARGUMENT, "Loading screen requires its actual live source receiver");
    *drawn = false;
    q3g_role *cgame = engine ? engine->initializing_role : NULL;
    if (!cgame || cgame->kind != QA_QVM_CGAME || cgame->seat != seat ||
        !engine->calls || cgame->init_succeeded) return true;
    q3g_role *ui = NULL;
    for (q3g_role *role = engine->roles; role; role = role->next)
        if (role->kind == QA_QVM_UI && role->seat == seat && role->ready && !role->retired) {
            if (ui) return application_fail(error, QA_ERROR_ARGUMENT, "Ambiguous actual source loading UI");
            ui = role;
        }
    if (!ui || !ui->committed || !ui->initialized || !ui->init_succeeded ||
        !same_descriptor(ui->descriptor, cgame->descriptor))
        return application_fail(error, QA_ERROR_ARGUMENT, "CGAME loading lost its successfully initialized source UI");
    int32_t overlay = 1, result;
    engine->initializing_role = ui;
    bool ok = q3g_call(ui, 10, &overlay, 1, &result, error);
    engine->initializing_role = cgame;
    if (ok) *drawn = true;
    return ok;
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
