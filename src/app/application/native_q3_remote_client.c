#include "native_q3_remote_client.h"
#include "qa/source_frame_time.h"
#include "qa/console_cvar_observer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static application_provider *receiver(qa_application *app, qa_actor_owner owner)
{
    application_provider *found = NULL;
    for (size_t i = 0; app && i < app->provider_count; ++i)
        if (app->providers[i] && app->providers[i]->owner == owner) {
            if (found) return NULL;
            found = app->providers[i];
        }
    return found;
}
struct qa_native_q3_remote_client_transport {
    qa_application *application;
    application_provider *provider;
    qa_native_q3_remote_transport_services services;
    qa_launch_instance_lease *descriptor;
    size_t actions;
    bool attached, retiring, released;
};
static bool transport_source(const qa_native_q3_remote_client_transport *transport,
    qa_application_q3_remote_source *out)
{
    if (!transport || transport->retiring || !transport->attached || !out) return false;
    const qa_application_q3_remote_source *held = &transport->services.source;
    qa_application_q3_remote_source actual;
    if (!application_native_q3_remote_role_transport_current(transport->provider, held->receiver.seat, transport) ||
        !application_native_q3_remote_role_source_read(transport->provider, held->receiver.seat,
            held->connection_epoch, &actual, NULL) || !qa_application_q3_remote_source_current(transport->application, &actual)) return false;
    const qa_application_q3_client_context *a = &actual.receiver, *b = &held->receiver;
    if (actual.descriptor->storage != held->descriptor->storage || actual.descriptor->content != held->descriptor->content ||
        actual.descriptor->identity != held->descriptor->identity ||
        actual.configuration_generation != held->configuration_generation || a->session != b->session ||
        a->receiver != b->receiver || a->seat != b->seat || a->service_owner != b->service_owner ||
        a->frontend_lifetime != b->frontend_lifetime || a->console != b->console || a->cvars != b->cvars ||
        a->client_time_cvars != b->client_time_cvars || a->client_time_owner != b->client_time_owner ||
        !transport->services.current(transport->services.context, &actual)) return false;
    *out = actual; return true;
}
bool qa_native_q3_remote_client_transport_current(const qa_native_q3_remote_client_transport *transport)
{ qa_application_q3_remote_source source; return transport_source(transport, &source); }
bool qa_native_q3_remote_client_transport_idle(const qa_native_q3_remote_client_transport *transport)
{
    return transport && !transport->actions && (transport->released ||
        transport->services.idle(transport->services.context));
}
bool qa_native_q3_remote_client_transport_create(qa_application *app, qa_native_q3_remote_transport_services *services,
    qa_native_q3_remote_client_transport **out, qa_error *error)
{
    application_provider *provider = services ? receiver(app, services->source.receiver.receiver) : NULL;
    if (!app || !services || !out || *out || !provider || !services->context || !services->current ||
        !services->idle || !services->milliseconds || !services->forward || !services->release || !services->source.receiver.native_source ||
        !qa_application_q3_remote_source_current(app, &services->source) ||
        !services->current(services->context, &services->source))
        return native_client_fail(error, QA_ERROR_ARGUMENT, "Remote forwarding requires its actual retained CLIENT transport");
    qa_native_q3_remote_client_transport *transport = calloc(1, sizeof(*transport));
    if (!transport) return native_client_fail(error, QA_ERROR_MEMORY, "Retaining remote CLIENT transport forwarding");
    transport->application = app; transport->provider = provider; transport->services = *services;
    if (!qa_launch_instance_retain_metadata(services->source.descriptor, &transport->descriptor, error)) {
        free(transport); return false;
    }
    transport->services.source.descriptor = qa_launch_instance_lease_view(transport->descriptor);
    if (!application_native_q3_remote_role_transport_attach(provider, &transport->services.source, transport, error)) {
        qa_launch_instance_lease_release(transport->descriptor); free(transport); return false;
    }
    transport->attached = true; *services = (qa_native_q3_remote_transport_services){0}; *out = transport; return true;
}
static bool transport_invocation(qa_native_q3_remote_client_transport *transport,
    const qa_command_invocation *call, qa_error *error)
{
    qa_application_q3_remote_source source; qa_command_context expected;
    if (!call || !call->argc || !call->argv || !call->raw || !call->args_text ||
        !transport_source(transport, &source) || call->console != source.receiver.console ||
        !qa_application_capture_command_context(transport->application, &source.receiver.command_context, &expected, error))
        return native_client_fail(error, QA_ERROR_ARGUMENT, "Remote forwarding lost its captured physical CLIENT invocation");
    const qa_command_context *actual = &call->context;
    bool sender=actual->owner==expected.owner && actual->cvar_view==expected.cvar_view &&
        actual->dialect==expected.dialect && actual->origin==expected.origin;
    bool engine=!actual->owner && actual->cvar_view==qa_cvars_view_identity(qa_application_cvars(transport->application)) &&
        qa_console_invocation_delivered_view(call,expected.cvar_view,source.receiver.receiver,source.receiver.service_owner);
    return ((sender || engine) && actual->session == expected.session && actual->seat == expected.seat &&
        actual->client == expected.client && actual->registry == expected.registry && actual->generation == expected.generation &&
        qa_actor_id_equal(actual->actor, expected.actor) &&
        qa_application_command_context_active(transport->application, actual)) ||
        native_client_fail(error, QA_ERROR_ARGUMENT, "Remote forwarding changed its original CLIENT origin");
}
bool qa_native_q3_remote_client_transport_forward(qa_native_q3_remote_client_transport *transport,
    const qa_command_invocation *call, qa_error *error)
{
    if (!transport_invocation(transport, call, error)) return false;
    if (transport->actions == SIZE_MAX)
        return native_client_fail(error, QA_ERROR_MEMORY, "Remote transport invocation borrow exceeds capacity");
    ++transport->actions;
    bool ok = transport->services.forward(transport->services.context, call, error);
    --transport->actions; return ok && transport_invocation(transport, call, error);
}
bool qa_native_q3_remote_client_transport_milliseconds(qa_native_q3_remote_client_transport *transport,
    const qa_command_invocation *call, int32_t *out, qa_error *error)
{
    if (!out) return native_client_fail(error, QA_ERROR_ARGUMENT, "Remote transport clock requires an output word");
    if (!transport_invocation(transport, call, error)) return false;
    if (transport->actions == SIZE_MAX)
        return native_client_fail(error, QA_ERROR_MEMORY, "Remote transport clock borrow exceeds capacity");
    ++transport->actions;
    uint32_t word = transport->services.milliseconds(transport->services.context);
    --transport->actions;
    if (!transport_invocation(transport, call, error)) return false;
    memcpy(out, &word, sizeof(word)); return true;
}
bool qa_native_q3_remote_client_transport_destroy(qa_native_q3_remote_client_transport **owned, qa_error *error)
{
    if (!owned || !*owned) return true;
    qa_native_q3_remote_client_transport *transport = *owned;
    if (!qa_native_q3_remote_client_transport_idle(transport) || (transport->attached &&
        !application_native_q3_remote_role_transport_detach_ready(transport->provider,
            transport->services.source.receiver.seat, transport, error)))
        return native_client_fail(error, QA_ERROR_ARGUMENT, "Remote transport retirement retains a CLIENT invocation");
    transport->retiring = true;
    if (!transport->released) {
        if (!transport->services.release(transport->services.context, error)) return false;
        transport->released = true;
    }
    if (transport->attached) {
        if (!application_native_q3_remote_role_transport_detach(transport->provider,
            transport->services.source.receiver.seat, transport, error)) return false;
        transport->attached = false;
    }
    qa_launch_instance_lease_release(transport->descriptor); free(transport); *owned = NULL; return true;
}
bool qa_native_q3_remote_client_publication_read(qa_application *app,
    const qa_application_q3_remote_source *source, uint64_t *out, qa_error *error)
{
    application_provider *provider = source ? receiver(app, source->receiver.receiver) : NULL;
    if (!out || !provider || provider->kind != APPLICATION_PROVIDER_Q3 ||
        !provider->constructed || !provider->attached || provider->close_pending ||
        !qa_application_q3_remote_source_current(app, source))
        return native_client_fail(error, QA_ERROR_ARGUMENT, "Remote native publication lost its actual selected CLIENT source");
    *out = app->publication_generation; return true;
}
bool qa_native_q3_remote_client_service_read(qa_application *app,
    const qa_application_q3_remote_source *source, qa_native_q3_remote_client_service **out, qa_error *error)
{
    uint64_t publication;
    if (!qa_native_q3_remote_client_publication_read(app, source, &publication, error)) return false;
    return application_native_q3_remote_role_service_read(receiver(app, source->receiver.receiver),
        source->receiver.seat, out, error);
}
bool qa_native_q3_remote_client_product_read(qa_application *app,
    const qa_application_q3_remote_source *source, qa_q3_product *out, qa_error *error)
{
    uint64_t publication;
    if (!qa_native_q3_remote_client_publication_read(app, source, &publication, error)) return false;
    return application_native_q3_remote_role_product(receiver(app, source->receiver.receiver), source->receiver.seat, out, error);
}
static bool basis_current(const qa_native_q3_remote_client_services *services)
{
    const qa_native_q3_remote_client_basis *basis = &services->basis;
    application_provider *provider = receiver(basis->application, basis->client.receiver);
    qa_application_q3_remote_source source;
    qa_q3_product product = provider && provider->product && !strcmp(provider->product->campaign, "missionpack") ?
        QA_Q3_TEAM_ARENA : QA_Q3_ARENA;
    return provider && provider->constructed && provider->attached && !provider->close_pending &&
        provider->kind == APPLICATION_PROVIDER_Q3 && provider->launch &&
        provider->launch->selection.runtime == QA_PROGRAM_BUILTIN && provider->product &&
        provider->product->family == QA_GAME_Q3 && basis->product == product &&
        basis->session == basis->application->session && basis->descriptor && basis->descriptor->storage &&
        basis->descriptor->selection.runtime == QA_PROGRAM_BUILTIN && !basis->descriptor->artifact &&
        basis->content == basis->descriptor->content && basis->content_product == basis->descriptor->selection.product &&
        basis->connection.owner && basis->connection.generation && basis->epoch && basis->configuration_generation &&
        basis->publication_generation == basis->application->publication_generation && basis->map && basis->geometry &&
        basis->gamestate && basis->gamestate->client_number >= 0 && basis->gamestate->client_number < 64 &&
        basis->physical_client == (uint32_t)basis->gamestate->client_number &&
        application_native_q3_remote_role_source_read(provider, basis->client.seat, basis->epoch, &source, NULL) &&
        source.descriptor->storage == basis->descriptor->storage && source.descriptor->content == basis->content &&
        source.configuration_generation == basis->configuration_generation &&
        qa_application_q3_remote_context_current(basis->application, &basis->client) &&
        services->current(services->context, basis);
}
bool qa_native_q3_remote_client_current(const qa_native_q3_remote_client_service *service)
{
    return service && !service->retiring && basis_current(&service->services) &&
        service->character.current(service->character.lifetime, &service->character);
}
bool qa_native_q3_remote_client_idle(const qa_native_q3_remote_client_service *service)
{
    return service && !service->updating && !service->actions &&
        (service->retiring || qa_cvars_observer_idle(service->services.basis.client.cvars)) && service->services.idle(service->services.context);
}
bool native_remote_client_allocate(qa_native_q3_remote_client_services *services,
    qa_native_q3_character_selection *character, qa_native_q3_remote_client_service **out, qa_error *error)
{
    if (!services || !character || !out || *out || !services->input || !services->context ||
        !services->current || !services->idle || !services->release || !services->reliable || !services->console ||
        !services->milliseconds || !services->console_command || !services->forward ||
        !services->reload_client_info || !services->network.gamestate || !services->network.current_snapshot ||
        !services->network.snapshot || !services->network.server_command || !services->network.current_command ||
        !services->network.user_command || !services->network.command_values || !services->network.source_actor ||
        !character->lifetime || !character->current || !character->release || !character->model || !character->skin ||
        !character->head_model || !character->head_skin || !basis_current(services) ||
        !character->current(character->lifetime, character))
        return native_client_fail(error, QA_ERROR_ARGUMENT, "Remote native CGAME requires its genuine received CLIENT and retained frontend services");
    const qa_native_q3_remote_client_basis *basis = &services->basis;
    const qa_command_context *origins[] = {&services->reliable_origin, &services->console_origin};
    for (size_t i = 0; i < 2; ++i) if (origins[i]->owner != basis->client.receiver ||
        origins[i]->seat != basis->client.seat || origins[i]->dialect != QA_CONSOLE_Q3 ||
        origins[i]->origin != QA_COMMAND_SEAT || !qa_application_command_context_active(basis->application, origins[i]))
        return native_client_fail(error, QA_ERROR_ARGUMENT, "Remote native commands lack their actual receiver/input origins");
    qa_native_q3_remote_client_service *service = calloc(1, sizeof(*service));
    if (!service) return native_client_fail(error, QA_ERROR_MEMORY, "Retaining remote native CGAME service");
    service->services = *services; service->character = *character; service->overlay_initial = true;
    qa_source_frame_time_bind(services->basis.client.cvars,&service->frame_time);
    native_client_cache_access refs={.registry=service->services.basis.client.cvars,
        .product=service->services.basis.product,.refs=&service->cvar_refs};
    native_client_cache_bind(&refs,false);
    service->provider = receiver(basis->application, basis->client.receiver);
    if (!qa_launch_instance_retain_metadata(basis->descriptor, &service->descriptor, error)) { free(service); return false; }
    service->services.basis.descriptor = qa_launch_instance_lease_view(service->descriptor);
    *out = service; return true;
}
bool native_remote_client_commit(qa_native_q3_remote_client_service *service, qa_error *error)
{
    if (!application_native_q3_remote_role_attach(service->provider, service->services.basis.client.seat, service, error)) return false;
    service->attached = true; return true;
}
bool qa_native_q3_remote_client_create(qa_native_q3_remote_client_services *services,
    qa_native_q3_character_selection *character, qa_native_q3_remote_client_service **out, qa_error *error)
{
    if (!out || *out) return native_client_fail(error, QA_ERROR_ARGUMENT, "Remote native construction requires an empty owned output");
    qa_native_q3_remote_client_service *service = NULL;
    if (!native_remote_client_allocate(services, character, &service, error)) return false;
    if (!native_remote_client_commit(service, error)) { qa_launch_instance_lease_release(service->descriptor); free(service); return false; }
    *services = (qa_native_q3_remote_client_services){0}; *character = (qa_native_q3_character_selection){0};
    *out = service; return true;
}
bool qa_native_q3_remote_client_retire_ready(const qa_native_q3_remote_client_service *service, qa_error *error)
{
    if (!service) return true;
    if (!qa_native_q3_remote_client_idle(service))
        return native_client_fail(error, QA_ERROR_ARGUMENT, "Remote native CGAME retains an active frontend callback");
    return !service->attached || application_native_q3_remote_role_detach_ready(service->provider,
        service->services.basis.client.seat, service, error);
}
bool qa_native_q3_remote_client_destroy(qa_native_q3_remote_client_service *service, qa_error *error)
{
    if (!service) return true;
    if (!qa_native_q3_remote_client_retire_ready(service, error)) return false;
    if (service->attached) {
        if (!application_native_q3_remote_role_detach(service->provider, service->services.basis.client.seat, service, error)) return false;
        service->attached = false;
    }
    service->retiring = true;
    if (!service->services.release(service->services.context, error)) return false;
    service->character.release(service->character.lifetime);
    qa_launch_instance_lease_release(service->descriptor); free(service->system_info); free(service); return true;
}
bool qa_native_q3_remote_client_basis_read(const qa_native_q3_remote_client_service *service,
    qa_native_q3_remote_client_basis *out, qa_error *error)
{
    if (!out || !qa_native_q3_remote_client_current(service))
        return native_client_fail(error, QA_ERROR_ARGUMENT, "Remote native CGAME basis lost its actual transport witness");
    *out = service->services.basis; return true;
}
bool qa_native_q3_remote_client_source_bind(qa_native_q3_remote_client_service *service,
    const qa_native_q3_remote_client_basis *basis, qa_error *error)
{
    if (!service || service->retiring || !basis || !basis->descriptor || service->updating || service->actions)
        return native_client_fail(error, QA_ERROR_ARGUMENT, "Remote native source binding requires its idle retained service");
    const qa_native_q3_remote_client_basis *old = &service->services.basis;
    if (basis->application != old->application || basis->session != old->session ||
        basis->descriptor->storage != old->descriptor->storage || basis->content != old->content ||
        basis->content_product != old->content_product || basis->product != old->product ||
        basis->connection.owner != old->connection.owner || basis->connection.generation != old->connection.generation ||
        basis->connection.slot != old->connection.slot || basis->epoch != old->epoch ||
        basis->restart_generation < old->restart_generation || basis->map != old->map || basis->geometry != old->geometry ||
        basis->configuration_generation != old->configuration_generation || basis->client.console != old->client.console ||
        basis->client.cvars != old->client.cvars || basis->client.service_owner != old->client.service_owner)
        return native_client_fail(error, QA_ERROR_ARGUMENT, "Remote native source binding changed its real CLIENT/content/connection owner");
    qa_native_q3_remote_client_services candidate = service->services; candidate.basis = *basis;
    if (!basis_current(&candidate)) return native_client_fail(error, QA_ERROR_ARGUMENT, "Remote native source binding is no longer current");
    service->services.basis = *basis;
    service->services.basis.descriptor = qa_launch_instance_lease_view(service->descriptor); return true;
}
const qa_native_q3_remote_client_services *qa_native_q3_remote_client_services_read(const qa_native_q3_remote_client_service *service)
{ return qa_native_q3_remote_client_current(service) ? &service->services : NULL; }
const qa_native_q3_character_selection *qa_native_q3_remote_client_character(const qa_native_q3_remote_client_service *service)
{ return qa_native_q3_remote_client_current(service) ? &service->character : NULL; }
bool qa_native_q3_remote_client_initialized(qa_native_q3_remote_client_service *service, qa_error *error)
{
    if (!service || !service->registered || service->services.basis.client.initialized ||
        service->updating || service->actions || !qa_native_q3_remote_client_current(service) ||
        !application_native_q3_remote_role_initialized(service->provider,
            service->services.basis.client.seat, service, error))
        return native_client_fail(error, QA_ERROR_ARGUMENT, "Remote native CGAME Init completion lost its registered physical service");
    service->services.basis.client.initialized = true; return true;
}
bool qa_native_q3_remote_client_video_reset(qa_native_q3_remote_client_service *service,qa_error *error)
{
    if(!service || !qa_native_q3_remote_client_idle(service) || service->cache_revision==UINT64_MAX ||
        !qa_native_q3_remote_client_current(service) ||
        !application_native_q3_remote_role_video_reset(service->provider,
            service->services.basis.client.seat,service,error))
        return native_client_fail(error,QA_ERROR_ARGUMENT,"Remote CG video reset requires its returned actual service/cache");
    service->services.basis.client.initialized=false;
    service->registered=false; service->overlay_initial=true;
    service->force_model_count=service->overlay_count=0; service->local_server=0;
    memset(service->cache,0,sizeof(service->cache)); ++service->cache_revision;
    free(service->system_info); service->system_info=NULL;
    return qa_native_q3_remote_client_current(service);
}
bool qa_native_q3_remote_client_cvar_read(const qa_native_q3_remote_client_service *service,qa_native_q3_cvar_id id,
    qa_native_q3_client_cvar *out,qa_error *error)
{
    if (!out || !qa_native_q3_remote_client_current(service))
        return native_client_fail(error,QA_ERROR_ARGUMENT,"Remote native cvar cache lost its physical CLIENT");
    if (!native_client_cache_read(&service->cvar_refs,service->cache,native_client_definition_count,id,out,error)) return false;
    if (id==QA_NATIVE_Q3_CVAR_cg_drawStatus && service->services.status_visible && !service->services.status_visible(service->services.context)) {
        strcpy(out->value,"0"); out->number=0; out->integer=0;
    }
    return true;
}
bool qa_native_q3_remote_client_cvar_number(qa_native_q3_remote_client_service *service,qa_native_q3_cvar_id id,float value,qa_error *error)
{
    qa_native_q3_client_cvar old;
    if (!qa_native_q3_remote_client_cvar_read(service,id,&old,error) || service->cache_revision==UINT64_MAX) return false;
    service->cache[service->cvar_refs.ordinals[id]].number=value; ++service->cache_revision; return true;
}
bool qa_native_q3_remote_client_cvar_integer(qa_native_q3_remote_client_service *service,qa_native_q3_cvar_id id,int32_t value,qa_error *error)
{
    qa_native_q3_client_cvar old;
    if (!qa_native_q3_remote_client_cvar_read(service,id,&old,error) || service->cache_revision==UINT64_MAX) return false;
    service->cache[service->cvar_refs.ordinals[id]].integer=value; ++service->cache_revision; return true;
}
bool qa_native_q3_remote_client_cache_read(const qa_native_q3_remote_client_service *service,
    qa_native_q3_remote_client_cache *out, qa_error *error)
{
    if (!out || !service || !service->registered || !qa_native_q3_remote_client_current(service))
        return native_client_fail(error, QA_ERROR_ARGUMENT, "Remote prediction requires its actual updated CGAME cache");
    qa_native_q3_remote_client_cache value = {.owner = service, .revision = service->cache_revision};
#define READ(field,symbol,member) value.field = service->cache[service->cvar_refs.ordinals[QA_NATIVE_Q3_CVAR_##symbol]].member
    READ(no_predict,cg_nopredict,integer); READ(synchronous_clients,cg_synchronousClients,integer);
    READ(predict_items,cg_predictItems,integer); READ(pmove_fixed,pmove_fixed,integer); READ(pmove_msec,pmove_msec,integer);
    READ(error_decay,cg_errorDecay,number); READ(error_decay_integer,cg_errorDecay,integer); READ(show_miss,cg_showmiss,integer);
#undef READ
    *out = value; return true;
}
bool qa_native_q3_remote_client_cache_current(const qa_native_q3_remote_client_service *service,
    const qa_native_q3_remote_client_cache *cache)
{ return service && cache && cache->owner == service && cache->revision == service->cache_revision && qa_native_q3_remote_client_current(service); }
bool qa_native_q3_remote_client_local_server_read(const qa_native_q3_remote_client_service *service,
    int32_t *out, qa_error *error)
{
    if (!out || !service || !service->registered || !qa_native_q3_remote_client_current(service))
        return native_client_fail(error, QA_ERROR_ARGUMENT, "Remote localServer requires its actual registered CLIENT cache");
    *out = service->local_server; return true;
}
bool qa_native_q3_remote_client_command_values(qa_native_q3_remote_client_service *service, int32_t weapon, float sensitivity, qa_error *error)
{
    if (!qa_native_q3_remote_client_current(service) || service->actions == SIZE_MAX)
        return native_client_fail(error, QA_ERROR_ARGUMENT, "Remote command values lost the actual input owner");
    ++service->actions; bool ok = service->services.network.command_values(service->services.network.context, weapon, sensitivity, error);
    --service->actions; return ok && qa_native_q3_remote_client_current(service);
}
static bool command(qa_native_q3_remote_client_service *service, const char *text, bool reliable, qa_error *error)
{
    if (!text || !qa_native_q3_remote_client_current(service) || service->actions == SIZE_MAX)
        return native_client_fail(error, QA_ERROR_ARGUMENT, "Remote CGAME command lost its actual CLIENT origin");
    qa_command_context origin = reliable ? service->services.reliable_origin : service->services.console_origin;
    if (!reliable) { origin.script = "q3-cgame"; origin.direct = false; origin.console_text = false; }
    ++service->actions;
    bool ok = reliable ? service->services.reliable(service->services.context, &origin, text, error) :
        service->services.console(service->services.context, &origin, text, error);
    --service->actions; return ok && qa_native_q3_remote_client_current(service);
}
bool qa_native_q3_remote_client_reliable(qa_native_q3_remote_client_service *service, const char *text, qa_error *error)
{ return command(service, text, true, error); }
bool qa_native_q3_remote_client_console(qa_native_q3_remote_client_service *service, const char *text, qa_error *error)
{ return command(service, text, false, error); }
static bool invocation_current(qa_native_q3_remote_client_service *service,
    const qa_command_invocation *call, qa_error *error)
{
    qa_command_context expected;
    if (!call || !call->argc || !call->argv || !call->raw || !call->args_text ||
        !qa_native_q3_remote_client_current(service) ||
        call->console != service->services.basis.client.console ||
        !qa_application_capture_command_context(service->services.basis.application,
            &service->services.basis.client.command_context, &expected, error))
        return native_client_fail(error, QA_ERROR_ARGUMENT, "Remote console lost its actual CLIENT invocation");
    const qa_command_context *actual = &call->context;
    const qa_native_q3_remote_client_basis *basis=&service->services.basis;
    bool sender=actual->owner==expected.owner && actual->cvar_view==expected.cvar_view &&
        actual->dialect==expected.dialect && actual->origin==expected.origin;
    bool engine=!actual->owner && actual->cvar_view==qa_cvars_view_identity(qa_application_cvars(basis->application)) &&
        qa_console_invocation_delivered_view(call,expected.cvar_view,basis->client.receiver,basis->client.service_owner);
    if ((!sender && !engine) || actual->session != expected.session || actual->seat != expected.seat ||
        actual->client != expected.client || actual->registry != expected.registry || actual->generation != expected.generation ||
        !qa_actor_id_equal(actual->actor, expected.actor) ||
        !qa_application_command_context_active(basis->application, actual))
        return native_client_fail(error, QA_ERROR_ARGUMENT, "Remote console changed its captured CLIENT origin");
    return true;
}
qa_command_result qa_native_q3_remote_client_command(qa_native_q3_remote_client_service *service,
    const qa_command_invocation *call, qa_error *error)
{
    if (!invocation_current(service, call, error)) return QA_COMMAND_FAILED;
    if (service->actions == SIZE_MAX) {
        native_client_fail(error, QA_ERROR_MEMORY, "Remote CLIENT command borrow exceeds capacity");
        return QA_COMMAND_FAILED;
    }
    ++service->actions;
    qa_command_result result = service->services.console_command(service->services.context, call, error);
    --service->actions;
    return invocation_current(service, call, error) ? result : QA_COMMAND_FAILED;
}
bool qa_native_q3_remote_client_forward(qa_native_q3_remote_client_service *service,
    const qa_command_invocation *call, qa_error *error)
{
    if (!invocation_current(service, call, error)) return false;
    if (service->actions == SIZE_MAX)
        return native_client_fail(error, QA_ERROR_MEMORY, "Remote CLIENT forwarding borrow exceeds capacity");
    ++service->actions;
    bool okay = service->services.forward(service->services.context, call, error);
    --service->actions; return okay && invocation_current(service, call, error);
}
bool qa_native_q3_remote_client_milliseconds(qa_native_q3_remote_client_service *service,
    const qa_command_invocation *call, int32_t *out, qa_error *error)
{
    if (!out) return native_client_fail(error, QA_ERROR_ARGUMENT, "Remote CLIENT clock requires an output word");
    if (!invocation_current(service, call, error)) return false;
    if (service->actions == SIZE_MAX)
        return native_client_fail(error, QA_ERROR_MEMORY, "Remote CLIENT clock borrow exceeds capacity");
    ++service->actions;
    uint32_t word = service->services.milliseconds(service->services.context);
    --service->actions;
    if (!invocation_current(service, call, error)) return false;
    memcpy(out, &word, sizeof(word)); return true;
}
bool qa_native_q3_remote_client_set_timescale(qa_native_q3_remote_client_service *service, float value, qa_error *error)
{
    if (!qa_native_q3_remote_client_current(service) || service->updating)
        return native_client_fail(error, QA_ERROR_ARGUMENT, "Remote timescale write requires the actual CLIENT registry");
    char text[64]; int length = snprintf(text, sizeof(text), "%f", (double)value);
    if (length < 0 || (size_t)length >= sizeof(text)) return native_client_fail(error, QA_ERROR_FORMAT, "Remote timescale formatting exceeds source capacity");
    service->updating = true;
    bool ok = qa_cvars_set(service->services.basis.client.cvars, "timescale", text, false, error);
    service->updating = false; return ok && qa_native_q3_remote_client_current(service);
}
bool qa_native_q3_remote_client_set_view_size(qa_native_q3_remote_client_service *service, int32_t value, qa_error *error)
{
    if (!qa_native_q3_remote_client_current(service) || service->updating)
        return native_client_fail(error, QA_ERROR_ARGUMENT, "Remote view size write requires the actual CLIENT registry");
    char text[32]; int length = snprintf(text, sizeof(text), "%d", (int)value);
    if (length < 0 || (size_t)length >= sizeof(text))
        return native_client_fail(error, QA_ERROR_FORMAT, "Remote view size formatting exceeds source capacity");
    service->updating = true;
    bool ok = qa_cvars_set(service->services.basis.client.cvars, "cg_viewsize", text, false, error);
    service->updating = false; return ok && qa_native_q3_remote_client_current(service);
}
bool qa_native_q3_remote_client_frame_time(qa_native_q3_remote_client_service *service, double supplied, double *out, qa_error *error)
{
    if (!out || !qa_native_q3_remote_client_current(service))
        return native_client_fail(error, QA_ERROR_ARGUMENT, "Remote elapsed time lost its real CLIENT registry");
    return qa_source_frame_time_sample(&service->frame_time, supplied, false, false, out, error);
}
