#include "qa/application_native_q3_remote_content.h"
#include "native_q3_remote_role.h"
#include "qa/catalog_save.h"

static application_provider *admission_receiver(qa_application *app,
    const qa_application_native_q3_remote_content_admission *request, qa_error *error)
{
    if (!app || !request || app->operation != APPLICATION_IDLE || app->frame_preparing ||
        app->q3_round_active || app->q3_world_restart || app->destroy_requested ||
        !qa_session_safe(app->session) || qa_session_faulted(app->session) || !qa_world_idle(app->world) ||
        !request->previous.receiver.native_source || request->previous.receiver.initialized ||
        !request->connection_epoch || request->connection_epoch != request->previous.connection_epoch ||
        !request->previous.configuration_generation || request->previous.configuration_generation == UINT64_MAX ||
        !qa_application_q3_remote_source_current(app, &request->previous)) {
        application_fail(error, QA_ERROR_ARGUMENT, "Builtin content admission requires its idle current pre-Init CLIENT source");
        return NULL;
    }
    application_provider *provider = NULL;
    for (size_t i = 0; i < app->provider_count; ++i)
        if (app->providers[i] && app->providers[i]->owner == request->previous.receiver.receiver) {
            if (provider) {
                application_fail(error, QA_ERROR_ARGUMENT, "Builtin content admission has ambiguous receiver ownership");
                return NULL;
            }
            provider = app->providers[i];
        }
    qa_native_q3_remote_client_service *service = NULL;
    if (!provider || provider->application != app || provider->kind != APPLICATION_PROVIDER_Q3 ||
        !provider->constructed || !provider->attached || provider->close_pending || !provider->launch ||
        provider->launch->selection.runtime != QA_PROGRAM_BUILTIN || provider->launch->artifact ||
        !application_native_q3_remote_roles_idle(provider)) {
        application_fail(error, QA_ERROR_ARGUMENT, "Builtin content admission requires its actual unborrowed compiled receiver");
        return NULL;
    }
    if (!application_native_q3_remote_role_service_read(provider, request->previous.receiver.seat, &service, error))
        return NULL;
    if (service) {
        application_fail(error, QA_ERROR_ARGUMENT, "Builtin content admission retains a live remote service");
        return NULL;
    }
    return provider;
}

bool qa_application_native_q3_remote_content_admit(qa_application *app,
    const qa_application_native_q3_remote_content_admission *request,
    qa_application_q3_remote_source *out, qa_error *error)
{
    if (!out || !request || !request->catalog || !request->prepared_mounts ||
        !request->producer || !request->prepared_current)
        return application_fail(error, QA_ERROR_ARGUMENT, "Builtin content admission requires its actual prepared content producer and output");
    if (!admission_receiver(app, request, error)) return false;
    const qa_product *product = qa_catalog_product(request->catalog, request->product);
    if (!product || product->family != QA_GAME_Q3 ||
        qa_vfs_resources(request->prepared_mounts) != qa_catalog_resources(request->catalog) ||
        request->prepared_mounts == request->previous.descriptor->content)
        return application_fail(error, QA_ERROR_ARGUMENT, "Builtin content admission lost its genuine detached Q3 catalog and prepared view");
    if (!request->prepared_current(request->producer, request, error)) return false;

    qa_launch_instance_lease *descriptor = NULL;
    if (!qa_launch_instance_prepare_builtin_client_metadata(request->previous.descriptor,
        request->catalog, request->product, request->prepared_mounts, &descriptor, error)) return false;

    /* Allocation and cloning precede the final pure producer/current proof.
     * No source or frontend callback follows this proof before the real bind. */
    bool ok = request->prepared_current(request->producer, request, error);
    application_provider *provider = ok ? admission_receiver(app, request, error) : NULL;
    ok = provider != NULL;
    if (ok) {
        app->operation = APPLICATION_CONFIGURING;
        ok = application_native_q3_remote_role_descriptor_bind(provider, request->previous.receiver.seat,
            qa_launch_instance_lease_view(descriptor), request->connection_epoch,
            request->previous.configuration_generation + 1, error);
        app->operation = APPLICATION_IDLE;
    }
    qa_launch_instance_lease_release(descriptor);
    if (!ok) return false;
    return qa_application_q3_remote_source_read(app, provider->owner,
        request->previous.receiver.seat, request->connection_epoch, out, error);
}
