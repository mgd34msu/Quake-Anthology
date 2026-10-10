#include "native_q3_client.h"
#include "qa/console_cvar_observer.h"
#include "internal.h"
#include "native_q3_console.h"
#include "native_q3_wire_state.h"
#include "qa/application_q3_round.h"
#include "qa/game_q3_clients.h"
#include "qa/console_cvar_observer.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

bool native_client_fail(qa_error *error,qa_status status,const char *message)
{ qa_error_set(error,status,0,"%s",message); return false; }

static application_provider *physical_source(const qa_native_q3_client_service *service)
{
    qa_application *app=service->application;
    if (!app || app->destroy_requested || app->session!=service->services.client.session ||
        app->publication_generation!=service->services.publication_generation ||
        app->map_revision!=service->services.map_revision) return NULL;
    application_provider *provider=application_world_provider(app,QA_ROLE_ENTITIES,"");
    return provider && provider->application==app && provider->constructed && provider->attached &&
        !provider->close_pending && provider->map_bound && provider->kind==APPLICATION_PROVIDER_Q3 &&
        provider->state.q3==service->source_game && provider->owner==service->services.client.source_owner &&
        provider->launch && provider->launch->storage==qa_launch_instance_lease_view(service->source_lease)->storage &&
        provider->launch->selection.product==service->content_product &&
        application_native_q3_console_registry(provider)==service->services.client.source_cvars?provider:NULL;
}
static bool reader_binding(qa_application *app,const qa_native_q3_client_services *services,
    const qa_q3_game *game,qa_native_q3_wire_basis *out,qa_error *error)
{
    return services->wire_reader && qa_native_q3_wire_reader_basis(services->wire_reader,out,error) &&
        out->application==app && out->session==services->client.session && out->source_game==game &&
        out->source_cvars==services->client.source_cvars && out->source_owner==services->client.source_owner &&
        out->receiver==services->client.receiver && qa_actor_id_equal(out->actor,services->client.source_actor) &&
        out->seat==services->client.seat && out->physical_client==services->client.source_client &&
        out->publication_generation==services->publication_generation && out->map_revision==services->map_revision;
}
bool qa_native_q3_client_service_current(const qa_native_q3_client_service *service)
{
    application_provider *provider=service?physical_source(service):NULL;
    qa_native_q3_wire_basis reader;
    if (!provider || !reader_binding(service->application,&service->services,service->source_game,&reader,NULL) ||
        reader.product!=service->product || !service->services.current(service->services.context,&service->services) ||
        !service->character.current(service->character.lifetime,&service->character)) return false;
    qa_actor_id actual; uint32_t physical; qa_q3_source_binding binding; qa_q3_native_client client;
    application_native_q3_wire_client_view transport; bool admitted;
    return qa_application_player_actor(service->application,service->services.client.seat,&actual) &&
        qa_actor_id_equal(actual,service->services.client.source_actor) &&
        qa_q3_native_client_slot(service->source_game,actual,&physical,NULL) &&
        physical==service->services.client.source_client &&
        qa_q3_source_binding_read(service->source_game,physical,&binding,NULL) &&
        qa_actor_id_equal(actual,binding.actor) && binding.in_use && binding.body_attached &&
        qa_q3_client_slot_read(service->source_game,physical,&client,NULL) &&
        client.rule.connected==QA_Q3_CLIENT_CONNECTED &&
        application_native_q3_wire_client_read(provider,physical,&transport,&admitted,NULL) && admitted &&
        transport.begun && !transport.bot && transport.seat==service->services.client.seat &&
        qa_actor_id_equal(transport.actor,actual);
}
static bool valid_origin(const qa_native_q3_client_services *services,const qa_command_context *origin)
{
    return origin->seat==services->client.seat && origin->dialect==QA_RULESET_Q3 &&
        origin->origin==QA_COMMAND_SEAT && origin->client==services->source_client_origin &&
        qa_actor_id_equal(origin->actor,services->client.source_actor);
}
bool qa_native_q3_client_source_basis_read(qa_application *app,
    const qa_native_q3_client_services *services,qa_native_q3_client_basis *out,qa_error *error)
{
    if (!app || !services || !out || app->destroy_requested || services->client.session!=app->session ||
        !services->wire_reader || !services->current ||
        app->publication_generation!=services->publication_generation || app->map_revision!=services->map_revision)
        return native_client_fail(error,QA_ERROR_ARGUMENT,"Native CGAME import lacks its installed source-reader lease");
    application_provider *provider=application_world_provider(app,QA_ROLE_ENTITIES,"");
    if (!provider || provider->application!=app || provider->owner!=services->client.source_owner ||
        provider->kind!=APPLICATION_PROVIDER_Q3 || !provider->state.q3 || !provider->constructed ||
        !provider->attached || provider->close_pending || !provider->map_bound || !provider->launch ||
        !provider->launch->content || !provider->launch->storage ||
        application_native_q3_console_registry(provider)!=services->client.source_cvars ||
        !services->current(services->context,services))
        return native_client_fail(error,QA_ERROR_ARGUMENT,"Native CGAME import differs from its physical GAME constructor");
    qa_actor_id actor; uint32_t slot; qa_q3_source_binding binding;
    qa_q3_native_client client;
    application_native_q3_wire_client_view transport; bool admitted;
    qa_q3_product product; int32_t match_start; qa_native_q3_wire_basis reader;
    if (!qa_application_player_actor(app,services->client.seat,&actor) ||
        !qa_actor_id_equal(actor,services->client.source_actor) ||
        !qa_q3_native_client_slot(provider->state.q3,actor,&slot,error) || slot!=services->client.source_client ||
        !qa_q3_source_binding_read(provider->state.q3,slot,&binding,error) || !binding.in_use || !binding.body_attached ||
        !qa_actor_id_equal(binding.actor,actor) ||
        !qa_q3_client_slot_read(provider->state.q3,slot,&client,error) || client.rule.connected!=QA_Q3_CLIENT_CONNECTED ||
        !application_native_q3_wire_client_read(provider,slot,&transport,&admitted,error) || !admitted ||
        !transport.begun || transport.bot || transport.seat!=services->client.seat ||
        !qa_actor_id_equal(transport.actor,actor) ||
        !qa_q3_source_match_context_read(provider->state.q3,&product,&match_start,error) ||
        !reader_binding(app,services,provider->state.q3,&reader,error) || reader.product!=product)
        return native_client_fail(error,QA_ERROR_ARGUMENT,"Native CGAME import lacks its true physical local client");
    *out=(qa_native_q3_client_basis){.application=app,.session=app->session,.source_game=provider->state.q3,
        .source_launch=provider->launch,.content=provider->launch->content,.source_owner=provider->owner,
        .receiver=services->client.receiver,.viewing_actor=actor,.content_product=provider->launch->selection.product,
        .product=product,.seat=services->client.seat,.physical_client=slot,
        .publication_generation=app->publication_generation,.map_revision=app->map_revision};
    return true;
}
bool native_client_allocate_bound(qa_application *app,const qa_native_q3_client_basis *source,
    const qa_native_q3_client_services *services,const qa_native_q3_character_selection *character,
    qa_native_q3_client_service **out,qa_error *error)
{
    if (!app || !source || !services || !character || !out ||
        source->application!=app ||
        services->client.session!=source->session || services->client.source_owner!=source->source_owner ||
        !services->client.receiver || !services->client.service_owner || !services->client.frontend_lifetime ||
        !services->client.console || !services->client.cvars || !services->client.source_cvars || !services->input ||
        !services->wire_reader || !services->command_values ||
        qa_cvars_dialect(services->client.cvars)!=QA_RULESET_Q3 ||
        !services->client.native_source || services->publication_generation!=source->publication_generation ||
        services->map_revision!=source->map_revision || !services->current || !services->idle || !services->release ||
        !services->reliable || !services->console || !services->reload_client_info ||
        !valid_origin(services,&services->reliable_origin) || !valid_origin(services,&services->console_origin) ||
        (services->client.client_time_cvars && (services->client.client_time_cvars!=services->client.source_cvars ||
         services->client.client_time_owner!=source->source_owner)) ||
        !character->owner || !character->product || !character->launch || !character->content ||
        character->publication_generation!=source->publication_generation ||
        !character->definition || !character->model || !*character->model ||
        !character->skin || !*character->skin || !character->head_model ||
        !character->head_skin || !*character->head_skin || !character->lifetime ||
        !character->current || !character->release || native_client_definition_count>QA_NATIVE_CLIENT_CVARS)
        return native_client_fail(error,QA_ERROR_ARGUMENT,"Native CGAME constructor lacks actual GAME/seat/CHARACTER services");
    qa_native_q3_client_basis actual;
    if (!qa_native_q3_client_source_basis_read(app,services,&actual,error) ||
        actual.source_game!=source->source_game || !source->source_launch ||
        actual.source_launch->storage!=source->source_launch->storage || actual.content!=source->content ||
        actual.product!=source->product || actual.content_product!=source->content_product ||
        actual.physical_client!=source->physical_client || actual.seat!=source->seat ||
        actual.receiver!=source->receiver || !qa_actor_id_equal(actual.viewing_actor,source->viewing_actor))
        return native_client_fail(error,QA_ERROR_ARGUMENT,"Native CGAME constructor differs from its installed source basis");
    qa_native_q3_client_service *service=calloc(1,sizeof(*service));
    if (!service) return native_client_fail(error,QA_ERROR_MEMORY,"Allocating native CGAME seat configuration");
    service->application=app; service->services=*services; service->character=*character;
    qa_source_frame_time_bind(services->client.client_time_cvars,&service->frame_time);
    service->source_game=source->source_game; service->product=source->product;
    service->content_product=source->content_product; service->count=native_client_definition_count;
    service->overlay_initial=true;
    native_client_cache_access refs={.registry=service->services.client.cvars,.product=service->product,
        .refs=&service->cvar_refs};
    native_client_cache_bind(&refs,false);
    if (!qa_launch_instance_retain_metadata(source->source_launch,&service->source_lease,error) ||
        !qa_native_q3_client_service_current(service)) {
        qa_launch_instance_lease_release(service->source_lease); free(service); return false;
    }
    *out=service; return true;
}
bool native_client_allocate(qa_application *app,const qa_application_native_q3_presentation *source,
    const qa_native_q3_client_services *services,const qa_native_q3_character_selection *character,
    qa_native_q3_client_service **out,qa_error *error)
{
    if (!qa_application_native_q3_presentation_current(app,source))
        return native_client_fail(error,QA_ERROR_ARGUMENT,"Native CGAME fresh construction lacks a completed GAME cut");
    uint32_t slot; qa_actor_id actor; qa_q3_player player; bool found;
    if (!services || !qa_application_native_q3_presentation_local(app,source,services->client.seat,
        &slot,&actor,&player,&found,error) || !found || slot!=services->client.source_client ||
        !qa_actor_id_equal(actor,services->client.source_actor))
        return native_client_fail(error,QA_ERROR_ARGUMENT,"Native CGAME fresh construction lacks its actual viewing client");
    qa_native_q3_client_basis basis;
    if (!qa_native_q3_client_source_basis_read(app,services,&basis,error)) return false;
    return native_client_allocate_bound(app,&basis,services,character,out,error);
}
bool qa_native_q3_client_service_create(qa_application *app,const qa_application_native_q3_presentation *source,
    qa_native_q3_client_services *services,qa_native_q3_character_selection *character,
    qa_native_q3_client_service **out,qa_error *error)
{
    if (!out) return native_client_fail(error,QA_ERROR_ARGUMENT,"Native CGAME constructor requires output");
    if (!services || services->client.initialized)
        return native_client_fail(error,QA_ERROR_ARGUMENT,"Fresh native CGAME services must precede actual Init");
    qa_native_q3_client_service *service;
    if (!native_client_allocate(app,source,services,character,&service,error)) return false;
    *services=(qa_native_q3_client_services){0}; *character=(qa_native_q3_character_selection){0};
    *out=service; return true;
}
bool qa_native_q3_client_service_admit(qa_native_q3_client_service *service,
    qa_actor_id previous, qa_actor_id admitted, qa_error *error)
{
    application_provider *provider = service ? physical_source(service) : NULL;
    qa_actor_id local;
    application_native_q3_wire_client_view transport;
    bool present;
    if (!provider || !service->application->q3_round_active ||
        !qa_application_q3_round_callback_ready(service->application, provider->owner, error) ||
        !qa_native_q3_client_service_idle(service) ||
        !qa_actor_id_equal(service->services.client.source_actor, previous) ||
        admitted.registry != previous.registry || qa_actor_id_equal(admitted, previous) ||
        !qa_application_player_actor(service->application, service->services.client.seat, &local) ||
        !qa_actor_id_equal(local, admitted) ||
        !application_native_q3_wire_client_admission_read(provider,
            service->services.client.source_client, &transport, &present, error) || !present ||
        !transport.begun || transport.bot || transport.seat != service->services.client.seat ||
        !qa_actor_id_equal(transport.actor, admitted) ||
        !service->character.current(service->character.lifetime, &service->character))
        return native_client_fail(error, QA_ERROR_ARGUMENT,
            "Native round admission requires its retained client and fresh physical actor");
    service->services.client.source_actor = admitted;
    service->has_system_info_revision=false;
    return true;
}

bool qa_native_q3_client_service_destroy(qa_native_q3_client_service *service,qa_error *error)
{
    if (!service) return true;
    if (!qa_native_q3_client_service_retire_ready(service,error)) return false;
    free(service->system_info);
    service->character.release(service->character.lifetime);
    service->services.release(service->services.context);
    qa_launch_instance_lease_release(service->source_lease); free(service); return true;
}
bool qa_native_q3_client_service_idle(const qa_native_q3_client_service *service)
{
    return !service || (!service->updating && !service->action_busy &&
        qa_native_q3_wire_reader_idle(service->services.wire_reader) &&
        service->services.idle(service->services.context) &&
        qa_cvars_observer_idle(service->services.client.cvars) &&
        qa_cvars_observer_idle(service->services.client.source_cvars) &&
        (!service->services.client.client_time_cvars ||
         qa_cvars_observer_idle(service->services.client.client_time_cvars)));
}
bool qa_native_q3_client_service_retire_ready(const qa_native_q3_client_service *service,qa_error *error)
{
    return qa_native_q3_client_service_idle(service) ||
        native_client_fail(error,QA_ERROR_ARGUMENT,"Native CGAME service has active configuration/publication callbacks");
}
bool qa_native_q3_client_initialized(qa_native_q3_client_service *service,qa_error *error)
{
    if (!service || !service->registered || !qa_native_q3_client_service_idle(service) ||
        !qa_native_q3_client_service_current(service))
        return native_client_fail(error,QA_ERROR_ARGUMENT,"Native CGAME Init completion requires its actual registered constructor");
    service->services.client.initialized=true;
    service->updating=true;
    bool ok=native_client_time_register(service,error);
    service->updating=false; return ok;
}
bool qa_native_q3_client_video_reset(qa_native_q3_client_service *service,qa_error *error)
{
    if (!service || !qa_native_q3_client_service_idle(service) ||
        !qa_native_q3_client_service_current(service))
        return native_client_fail(error,QA_ERROR_ARGUMENT,"Native CG video reset requires its returned actual service");
    memset(service->cache,0,sizeof(service->cache));
    service->force_model_count=service->overlay_count=0;
    service->local_server=0; service->registered=false; service->overlay_initial=true;
    service->services.client.initialized=false;
    free(service->system_info); service->system_info=NULL;
    service->has_system_info_revision=false;
    return qa_native_q3_client_service_current(service);
}
bool qa_native_q3_client_context_read(qa_native_q3_client_service *service,
    qa_application_q3_client_context *out,qa_error *error)
{
    if (!out || !qa_native_q3_client_service_current(service))
        return native_client_fail(error,QA_ERROR_ARGUMENT,"Native CGAME context lost its source actor or input lifetime");
    qa_application_native_q3_presentation source;
    if (!qa_application_native_q3_presentation_read(service->application,service->services.client.source_owner,&source,error)) return false;
    qa_application_q3_client_context client=service->services.client;
    client.command_context.actor=client.source_actor;
    client.source_frame=source.source_frame; client.source_milliseconds=source.source_time_ms;
    *out=client; return true;
}
const qa_native_q3_character_selection *qa_native_q3_client_character(const qa_native_q3_client_service *service)
{ return qa_native_q3_client_service_current(service)?&service->character:NULL; }
const qa_native_q3_client_services *qa_native_q3_client_services_read(const qa_native_q3_client_service *service)
{ return qa_native_q3_client_service_current(service)?&service->services:NULL; }
bool qa_native_q3_client_basis_read(const qa_native_q3_client_service *service,
    qa_native_q3_client_basis *out,qa_error *error)
{
    if (!out || !qa_native_q3_client_service_current(service))
        return native_client_fail(error,QA_ERROR_ARGUMENT,"Native CGAME source basis lost its actual installed lease");
    const qa_launch_instance *launch=qa_launch_instance_lease_view(service->source_lease);
    *out=(qa_native_q3_client_basis){.application=service->application,.session=service->services.client.session,
        .source_game=service->source_game,.source_launch=launch,.content=launch->content,
        .source_owner=service->services.client.source_owner,.receiver=service->services.client.receiver,
        .viewing_actor=service->services.client.source_actor,.content_product=service->content_product,
        .product=service->product,.seat=service->services.client.seat,.physical_client=service->services.client.source_client,
        .publication_generation=service->services.publication_generation,.map_revision=service->services.map_revision};
    return true;
}
bool qa_native_q3_client_command_values(qa_native_q3_client_service *service,int32_t weapon,
    float sensitivity,qa_error *error)
{
    if (!qa_native_q3_client_service_current(service) || service->action_busy==SIZE_MAX)
        return native_client_fail(error,QA_ERROR_ARGUMENT,"Native CGAME command values lost the actual viewing input owner");
    ++service->action_busy;
    bool ok=service->services.command_values(service->services.context,weapon,sensitivity,error);
    --service->action_busy;
    return ok && qa_native_q3_client_service_current(service);
}
bool qa_native_q3_client_set_timescale(qa_native_q3_client_service *service,float value,qa_error *error)
{
    if (!service || service->updating || !qa_native_q3_client_service_current(service))
        return native_client_fail(error,QA_ERROR_ARGUMENT,"Native CGAME timescale lacks its actual primary owner");
    char text[64]; int length=snprintf(text,sizeof(text),"%f",(double)value);
    if (length<0 || (size_t)length>=sizeof(text))
        return native_client_fail(error,QA_ERROR_FORMAT,"Native CGAME timescale source formatting exceeds capacity");
    service->updating=true;
    bool ok=qa_cvars_set(service->services.client.cvars,"timescale",text,false,error) &&
        qa_native_q3_client_service_current(service);
    service->updating=false; return ok;
}
bool qa_native_q3_client_reliable(qa_native_q3_client_service *service,const char *text,qa_error *error)
{
    if (!text || !qa_native_q3_client_service_current(service) || service->action_busy==SIZE_MAX)
        return native_client_fail(error,QA_ERROR_ARGUMENT,"Native CGAME reliable command lacks its actual local caller");
    ++service->action_busy;
    qa_command_context origin = service->services.reliable_origin;
    origin.actor = service->services.client.source_actor;
    bool ok=service->services.reliable(service->services.context,&origin,text,error);
    --service->action_busy;
    return ok && qa_native_q3_client_service_current(service);
}
bool qa_native_q3_client_console(qa_native_q3_client_service *service,const char *text,qa_error *error)
{
    if (!text || !qa_native_q3_client_service_current(service) || service->action_busy==SIZE_MAX)
        return native_client_fail(error,QA_ERROR_ARGUMENT,"Native CGAME console command lacks its actual input receipt");
    qa_command_context origin=service->services.console_origin;
    origin.actor=service->services.client.source_actor;
    origin.script="q3-cgame"; origin.direct=false; origin.console_text=false;
    ++service->action_busy;
    bool ok=service->services.console(service->services.context,&origin,text,error);
    --service->action_busy;
    return ok && qa_native_q3_client_service_current(service);
}
