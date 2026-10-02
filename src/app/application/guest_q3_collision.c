#include "guest_q3_private.h"
#include "qa/application_q3_collision.h"
#include "qa/application_q3_factory.h"

static bool collision_current(void *context, const qa_q3_host *host, const qa_qvm *vm,
    const qa_qvm_image *image, const qa_collision_geometry *geometry, qa_error *error)
{
    q3g_role *role = context;
    if (!role || !role->engine || !role->engine->provider)
        return application_fail(error, QA_ERROR_ARGUMENT, "Collision scene lost its actual CGAME owner");
    struct application_q3_guest *engine = role->engine;
    application_provider *provider = engine->provider;
    qa_application *app = provider->application;
    bool retained = false;
    for (q3g_role *r = engine->roles; r; r = r->next) if (r == role) retained = true;
    const qa_launch_instance *descriptor = engine->client_descriptor ?
        qa_launch_instance_lease_view(engine->client_descriptor) : provider->launch;
    qa_q3_host_client_context actual;
    if (!retained || !role->ready || role->retired || role->source_cleared ||
        engine->restore_pending || engine->round.phase != Q3G_ROUND_NONE ||
        !provider->constructed || !provider->attached || provider->close_pending ||
        role->kind != QA_QVM_CGAME || role->host != host || role->vm != vm || role->image != image ||
        !role->artifact || !role->descriptor || !descriptor ||
        descriptor->storage != role->descriptor->storage || descriptor->content != role->descriptor->content ||
        !qa_sha256_equal(&descriptor->identity, &role->descriptor->identity) ||
        role->artifact->view != descriptor->content || role->artifact->image != image ||
        !role->collision_services.geometry ||
        role->collision_services.geometry(role->collision_services.context) != geometry ||
        !qa_q3_host_client_context_read(host, &actual) || actual.session != app->session ||
        actual.owner != provider->owner || actual.service_owner != role->service_owner ||
        actual.role != QA_QVM_CGAME || actual.command_context.seat != role->seat ||
        !actual.console || !actual.cvars || !actual.frontend_lifetime)
        return application_fail(error, QA_ERROR_ARGUMENT, "Collision scene changed its actual artifact, map or host namespace");
    if (role->local_client) {
        qa_application_q3_client_host local;
        bool present = false;
        if (!qa_application_q3_client_host_read(app, provider->owner, role->seat, &local, &present, error)) return false;
        if (!present || local.host != host || local.context.frontend_lifetime != actual.frontend_lifetime)
            return application_fail(error, QA_ERROR_ARGUMENT, "Collision scene changed its genuine source actor binding");
    } else {
        qa_application_q3_client_context remote;
        if (!qa_application_q3_remote_context_read(app, provider->owner, role->seat, &remote, error)) return false;
        if (remote.service_owner != actual.service_owner || remote.frontend_lifetime != actual.frontend_lifetime ||
            remote.console != actual.console || remote.cvars != actual.cvars)
            return application_fail(error, QA_ERROR_ARGUMENT, "Collision scene changed its genuine remote client namespace");
    }
    return true;
}
bool application_guest_q3_collision_bind(q3g_role *role, qa_error *error)
{
    if (!role || !role->artifact)
        return application_fail(error, QA_ERROR_ARGUMENT, "Collision declaration lost its retained artifact");
    if (!role->artifact->collision_profile.present) return true;
    return qa_q3_host_collision_scene_bind(role->host, role->vm, role->image,
        &role->artifact->collision_profile, collision_current, role, error);
}
bool qa_application_q3_collision_scene_hold(qa_application *app, qa_actor_owner receiver,
    uint32_t seat, uint64_t service_owner, qa_q3_host_collision_scene **out, bool *present, qa_error *error)
{
    if (!app || !receiver || !service_owner || !out || !present || *out || app->destroy_requested)
        return application_fail(error, QA_ERROR_ARGUMENT, "Collision hold requires its actual physical CLIENT namespace");
    q3g_role *found = NULL;
    application_provider **providers = app->routing_providers ? app->routing_providers : app->providers;
    size_t count = app->routing_providers ? app->routing_provider_count : app->provider_count;
    for (size_t i = 0; i < count; ++i) {
        struct application_q3_guest *engine = providers[i] && providers[i]->owner == receiver ? q3g_engine(providers[i]) : NULL;
        if (!engine) continue;
        for (q3g_role *role = engine->roles; role; role = role->next)
            if (role->kind == QA_QVM_CGAME && role->seat == seat) {
                if (found) return application_fail(error, QA_ERROR_ARGUMENT, "Collision hold has ambiguous CGAME inventory");
                found = role;
            }
    }
    if (!found || found->service_owner != service_owner || !found->ready || found->retired ||
        !found->host || found->source_cleared || found->engine->restore_pending)
        return application_fail(error, QA_ERROR_ARGUMENT, "Collision hold lost its actual completed CGAME role");
    qa_application_startup_source source;
    qa_q3_host_client_context host;
    if (!qa_application_q3_client_configuration_read(app, receiver, seat, &source, error) ||
        !qa_q3_host_client_context_read(found->host, &host) || !source.descriptor || !found->descriptor ||
        source.descriptor->storage != found->descriptor->storage ||
        source.descriptor->content != found->descriptor->content ||
        !qa_sha256_equal(&source.descriptor->identity, &found->descriptor->identity) ||
        host.session != app->session || host.owner != receiver || host.service_owner != service_owner ||
        host.role != QA_QVM_CGAME || host.command_context.seat != seat ||
        host.console != source.console || host.cvars != source.cvars || !host.frontend_lifetime)
        return application_fail(error, QA_ERROR_ARGUMENT, "Collision hold changed its physical CLIENT declaration or host");
    if (found->local_client) {
        qa_application_q3_client_host local;
        bool held = false;
        if (!qa_application_q3_client_host_read(app, receiver, seat, &local, &held, error)) return false;
        if (!held || local.host != found->host)
            return application_fail(error, QA_ERROR_ARGUMENT, "Collision hold changed its actual local source binding");
    } else {
        qa_application_q3_client_context remote;
        if (!qa_application_q3_remote_context_read(app, receiver, seat, &remote, error)) return false;
        if (remote.service_owner != service_owner || remote.frontend_lifetime != host.frontend_lifetime)
            return application_fail(error, QA_ERROR_ARGUMENT, "Collision hold changed its actual remote source binding");
    }
    if (!found->artifact || !found->artifact->collision_profile.present) {
        *out = NULL; *present = false; return true;
    }
    if (!qa_q3_host_collision_hold(found->host, out, error)) return false;
    *present = true; return true;
}
