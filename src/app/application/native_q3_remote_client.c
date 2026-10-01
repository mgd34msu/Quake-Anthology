#include "native_q3_remote_client.h"
#include "qa/source_frame_time.h"
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
bool qa_native_q3_remote_client_destroy(qa_native_q3_remote_client_service *service, qa_error *error)
{
    if (!service) return true;
    if (!qa_native_q3_remote_client_idle(service))
        return native_client_fail(error, QA_ERROR_ARGUMENT, "Remote native CGAME retains an active frontend callback");
    service->retiring = true;
    if (service->attached) {
        if (!application_native_q3_remote_role_detach(service->provider, service->services.basis.client.seat, service, error)) return false;
        service->attached = false;
    }
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
size_t native_remote_client_symbol(const qa_native_q3_remote_client_service *service, const char *symbol)
{
    for (size_t i = 0; symbol && i < native_client_definition_count; ++i)
        if ((!native_client_definitions[i].missionpack || service->services.basis.product == QA_Q3_TEAM_ARENA) &&
            !strcmp(native_client_definitions[i].symbol, symbol)) return i;
    return SIZE_MAX;
}
bool qa_native_q3_remote_client_cvar_read(const qa_native_q3_remote_client_service *service, const char *symbol,
    qa_native_q3_client_cvar *out, qa_error *error)
{
    if (!out || !qa_native_q3_remote_client_current(service))
        return native_client_fail(error, QA_ERROR_ARGUMENT, "Remote native cvar cache lost its physical CLIENT");
    size_t index = native_remote_client_symbol(service, symbol);
    if (index == SIZE_MAX) return native_client_fail(error, QA_ERROR_NOT_FOUND, "Remote CGAME cvar symbol is absent for its compiled product");
    *out = service->cache[index];
    if (!strcmp(symbol, "cg_drawStatus") && service->services.status_visible && !service->services.status_visible(service->services.context)) {
        strcpy(out->value, "0"); out->number = 0; out->integer = 0;
    }
    return true;
}
bool qa_native_q3_remote_client_cvar_number(qa_native_q3_remote_client_service *service, const char *symbol, float value, qa_error *error)
{
    qa_native_q3_client_cvar old;
    if (!qa_native_q3_remote_client_cvar_read(service, symbol, &old, error) || service->cache_revision == UINT64_MAX) return false;
    service->cache[native_remote_client_symbol(service, symbol)].number = value; ++service->cache_revision; return true;
}
bool qa_native_q3_remote_client_cvar_integer(qa_native_q3_remote_client_service *service, const char *symbol, int32_t value, qa_error *error)
{
    qa_native_q3_client_cvar old;
    if (!qa_native_q3_remote_client_cvar_read(service, symbol, &old, error) || service->cache_revision == UINT64_MAX) return false;
    service->cache[native_remote_client_symbol(service, symbol)].integer = value; ++service->cache_revision; return true;
}
bool qa_native_q3_remote_client_cache_read(const qa_native_q3_remote_client_service *service,
    qa_native_q3_remote_client_cache *out, qa_error *error)
{
    if (!out || !service || !service->registered || !qa_native_q3_remote_client_current(service))
        return native_client_fail(error, QA_ERROR_ARGUMENT, "Remote prediction requires its actual updated CGAME cache");
    qa_native_q3_remote_client_cache value = {.owner = service, .revision = service->cache_revision};
#define READ(field,symbol,member) value.field = service->cache[native_remote_client_symbol(service,#symbol)].member
    READ(no_predict,cg_nopredict,integer); READ(synchronous_clients,cg_synchronousClients,integer);
    READ(predict_items,cg_predictItems,integer); READ(pmove_fixed,pmove_fixed,integer); READ(pmove_msec,pmove_msec,integer);
    READ(error_decay,cg_errorDecay,number); READ(error_decay_integer,cg_errorDecay,integer); READ(show_miss,cg_showmiss,integer);
#undef READ
    *out = value; return true;
}
bool qa_native_q3_remote_client_cache_current(const qa_native_q3_remote_client_service *service,
    const qa_native_q3_remote_client_cache *cache)
{ return service && cache && cache->owner == service && cache->revision == service->cache_revision && qa_native_q3_remote_client_current(service); }
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
bool qa_native_q3_remote_client_frame_time(qa_native_q3_remote_client_service *service, double supplied, double *out, qa_error *error)
{
    if (!out || !qa_native_q3_remote_client_current(service))
        return native_client_fail(error, QA_ERROR_ARGUMENT, "Remote elapsed time lost its real CLIENT registry");
    return qa_source_frame_time_sample(service->services.basis.client.cvars, supplied, false, false, out, error);
}
