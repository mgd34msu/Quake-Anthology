#include "remote_q3_session.h"
#include "remote_q3_transport.h"
#include "source_renderer_runtime.h"

static bool fail(qa_error *error,const char *text)
{ return error && error->code!=QA_OK?false:frontend_fail(error,QA_ERROR_ARGUMENT,text); }
static bool recipe_read(const frontend_remote_q3_resources *resources,
    qa_application_native_q3_client_modules_recipe *out,qa_error *error)
{
    qa_frontend *f=frontend_remote_q3_frontend(resources?resources->owner:NULL);
    return f && frontend_remote_q3_resources_current(resources) &&
        qa_application_native_q3_client_modules_recipe_read(f->application,
            &resources->domain.source,resources->domain.gamestate,out,error);
}
static bool init_current(qa_frontend *f,const frontend_network_client_domain *domain,
    const qa_application_q3_remote_init *request,qa_error *error)
{
    const qa_application_q3_remote_source *source=request?&request->source:NULL;
    const qa_application_q3_client_context *a=source?&source->receiver:NULL;
    const qa_application_q3_client_context *b=domain?&domain->source.receiver:NULL;
    return f && domain && source && source->descriptor && domain->source.descriptor && request->current &&
        frontend_network_client_domain_current(f,domain) &&
        qa_application_q3_remote_source_current(f->application,source) &&
        source->descriptor->storage==domain->source.descriptor->storage &&
        source->configuration_generation==domain->source.configuration_generation &&
        source->connection_epoch==domain->epoch && a->native_source &&
        a->session==b->session && a->receiver==b->receiver && a->seat==b->seat &&
        a->service_owner==b->service_owner && a->frontend_lifetime==b->frontend_lifetime &&
        a->console==b->console && a->cvars==b->cvars &&
        request->server_message==domain->initial.server_message &&
        request->last_executed_server_command==domain->initial.last_executed_server_command &&
        request->client_number==domain->initial.client_number &&
        request->current(request->connection,domain->epoch,request->server_message,
            request->last_executed_server_command,request->client_number,error);
}
static bool create_decoded(qa_frontend *f,const frontend_network_client_domain *domain,
    const qa_application_q3_remote_init *request,frontend_remote_q3 **out,qa_error *error)
{
    if(!out || *out || !init_current(f,domain,request,error))
        return fail(error,"Remote session requires its actual decoded CLIENT Init entry");
    if(!frontend_remote_q3_resources_create(f,domain,out,error)) return false;
    frontend_remote_q3 *row=*out;
    frontend_remote_q3_resources resources;
    qa_application_native_q3_client_modules_recipe recipe;
    if(!frontend_remote_q3_resources_read(row,&resources,error) || !recipe_read(&resources,&recipe,error)) return false;
    frontend_remote_q3_runtime *runtime=NULL;
    frontend_remote_q3_frame *frames=NULL;
    if(recipe.pure) {
        frontend_remote_q3_transport *transport=NULL;
        if(!frontend_remote_q3_transport_create(row,&transport,error)) return false;
    } else {
        frontend_remote_q3_frame_callbacks callbacks;
        if(!frontend_remote_q3_services_create(row,error) ||
            !frontend_remote_q3_runtime_create(row,&runtime,error) ||
            !frontend_remote_q3_runtime_callbacks_read(runtime,&callbacks,error) ||
            !frontend_remote_q3_frame_create(row,&callbacks,&frames,error) ||
            !frontend_remote_q3_runtime_bind_frames(runtime,frames,error)) return false;
    }
    frontend_remote_q3_modules *modules=NULL;
    if(!frontend_remote_q3_modules_create(row,&modules,error)) return false;
    application_native_q3_client_modules *owner=frontend_remote_q3_modules_owner(modules);
    if(!owner || !init_current(f,domain,request,error) ||
        !qa_application_native_q3_client_modules_initialize(owner,request,error)) return false;
    if(!recipe.pure && !frontend_remote_q3_frame_initialize(frames,runtime,
        frontend_remote_q3_runtime_initialize,error)) return false;
    if(!frontend_source_renderer_end_registration(f,error)) return false;
    frontend_remote_q3_session_view completed;
    return frontend_remote_q3_session_read(row,&completed,error);
}
bool frontend_remote_q3_session_create(qa_frontend *f,const frontend_network_client_domain *domain,
    const qa_application_q3_remote_init *request,frontend_remote_q3 **out,qa_error *error)
{
    if(!f) return fail(error,"Remote session has no actual frontend parent");
    bool preparing=f->preparing; f->preparing=true;
    bool okay=create_decoded(f,domain,request,out,error);
    f->preparing=preparing; return okay;
}
bool frontend_remote_q3_session_create_initial(qa_frontend *f,const frontend_network_client_attempt *attempt,
    frontend_remote_q3_initial **initial,frontend_remote_q3_modules **modules,qa_error *error)
{
    if(!f || !initial || *initial || !modules || *modules ||
        !frontend_network_client_attempt_current(f,attempt))
        return fail(error,"Initial UI session requires its actual CLIENT attempt and empty owned outputs");
    bool preparing=f->preparing; f->preparing=true;
    bool okay=frontend_remote_q3_initial_create(f,attempt,initial,error) &&
        frontend_remote_q3_initial_transport_create(*initial,error) &&
        frontend_remote_q3_modules_create_initial(f,*initial,modules,error);
    if(okay) {
        frontend_remote_q3_initial_view view; frontend_remote_q3_module_media media;
        application_native_q3_client_modules *owner=frontend_remote_q3_modules_owner(*modules);
        okay=owner && frontend_remote_q3_initial_read(*initial,&view,error) &&
            qa_application_native_q3_client_modules_initialize_ui(owner,view.attempt.phase!=QA_Q3_DISCONNECTED,error) &&
            frontend_remote_q3_modules_media_read(*modules,QA_QVM_UI,&media,error) &&
            frontend_remote_q3_modules_media_current(&media) && frontend_remote_q3_initial_read(*initial,&view,error);
    }
    f->preparing=preparing; return okay;
}
bool frontend_remote_q3_session_retire_initial(frontend_remote_q3_initial **initial,
    frontend_remote_q3_modules **modules,qa_error *error)
{
    if(!initial || !modules || (*modules &&
        frontend_remote_q3_modules_initial_parent(*modules)!=*initial))
        return fail(error,"Initial session retirement requires its actual retained module parent");
    return frontend_remote_q3_modules_destroy(modules,error) && frontend_remote_q3_initial_destroy(initial,error);
}
bool frontend_remote_q3_session_read(const frontend_remote_q3 *row,
    frontend_remote_q3_session_view *out,qa_error *error)
{
    frontend_remote_q3_resources resources;
    qa_application_native_q3_client_modules_recipe recipe;
    if(!out || !frontend_remote_q3_resources_read(row,&resources,error) || !recipe_read(&resources,&recipe,error)) return false;
    frontend_remote_q3_modules *modules=frontend_remote_q3_modules_read(row);
    application_native_q3_client_modules *owner=frontend_remote_q3_modules_owner(modules);
    frontend_remote_q3_runtime *runtime=frontend_remote_q3_runtime_read(row);
    frontend_remote_q3_frame *frames=frontend_remote_q3_frames_read(row);
    frontend_remote_q3_transport *transport=frontend_remote_q3_transport_read(row);
    frontend_remote_q3_module_media ui,cgame;
    if(!modules || !owner || !frontend_remote_q3_modules_idle(modules) ||
        !qa_application_native_q3_client_modules_current(owner,&resources.domain.source) ||
        !frontend_remote_q3_modules_media_read(modules,QA_QVM_UI,&ui,error) ||
        !frontend_remote_q3_modules_media_current(&ui)) return fail(error,"Remote session lacks its actual initialized source UI");
    if(recipe.pure) {
        if(runtime || frames || !frontend_remote_q3_transport_current(transport) ||
            !frontend_remote_q3_transport_idle(transport) ||
            !frontend_remote_q3_modules_media_read(modules,QA_QVM_CGAME,&cgame,error) ||
            !frontend_remote_q3_modules_media_current(&cgame))
            return fail(error,"Pure session lacks its actual acquired CGAME Init receipt");
    } else {
        frontend_remote_q3_services_view services; q3n_remote_source_view source;
        if(transport || !runtime || !frames || frontend_remote_q3_frame_parent(frames)!=row ||
            frontend_remote_q3_frame_callbacks_context(frames)!=runtime ||
            frontend_remote_q3_runtime_frames(runtime)!=frames ||
            !frontend_remote_q3_runtime_idle(runtime) || !frontend_remote_q3_frame_idle(frames) ||
            !frontend_remote_q3_runtime_initialized_current(runtime) ||
            !frontend_remote_q3_frame_initialized_current(frames) ||
            !frontend_remote_q3_services_read(row,&services,error) ||
            !q3n_remote_source_read(services.source,&source,error) || !source.basis.client.initialized ||
            !q3n_media_remote_current(services.media,&source,error) ||
            !q3n_clients_remote_current(services.clients,&source,error))
            return fail(error,"Compiled session lacks its real completed CGAME media and frame parents");
    }
    if(!frontend_remote_q3_resources_current(&resources)) return fail(error,"Remote session changed during observation");
    *out=(frontend_remote_q3_session_view){resources,modules,owner,runtime,frames,recipe.pure}; return true;
}
bool frontend_remote_q3_session_current(const frontend_remote_q3_session_view *view)
{
    frontend_remote_q3_session_view actual;
    return view && frontend_remote_q3_resources_current(&view->resources) &&
        frontend_remote_q3_session_read(view->resources.owner,&actual,NULL) &&
        actual.modules==view->modules && actual.module_owner==view->module_owner &&
        actual.runtime==view->runtime && actual.frames==view->frames && actual.pure==view->pure;
}
static bool role_current(void *context,const frontend_q3_content_role_receipt *receipt,qa_error *error)
{
    frontend_remote_q3_modules *modules=context;
    frontend_remote_q3_module_media actual;
    if(!receipt || !frontend_remote_q3_modules_media_read(modules,receipt->role,&actual,error) ||
        !frontend_remote_q3_modules_media_current(&actual)) return false;
    const qa_application_q3_role_receipt *r=&actual.receipt;
    return receipt->receiver==r->receiver && receipt->seat==r->seat && receipt->service_owner==r->service_owner &&
        receipt->configuration_generation==r->configuration_generation && receipt->connection_epoch==r->connection_epoch &&
        receipt->descriptor==r->descriptor && receipt->artifact==r->artifact && receipt->acquisition==r->acquisition &&
        receipt->artifact_view==r->artifact_view && receipt->media_views==actual.media_views &&
        receipt->media_view_count==actual.media_view_count && receipt->producer==modules && receipt->current==role_current;
}
static bool role_read(frontend_remote_q3_modules *modules,qa_qvm_role role,
    frontend_q3_content_role_receipt *out,qa_error *error)
{
    frontend_remote_q3_module_media media;
    if(!out || !frontend_remote_q3_modules_media_read(modules,role,&media,error) ||
        !frontend_remote_q3_modules_media_current(&media)) return false;
    const qa_application_q3_role_receipt *r=&media.receipt;
    *out=(frontend_q3_content_role_receipt){.role=r->role,.receiver=r->receiver,.seat=r->seat,
        .service_owner=r->service_owner,.configuration_generation=r->configuration_generation,
        .connection_epoch=r->connection_epoch,.descriptor=r->descriptor,.artifact=r->artifact,
        .acquisition=r->acquisition,.artifact_view=r->artifact_view,.media_views=media.media_views,
        .media_view_count=media.media_view_count,.producer=modules,.current=role_current};
    return role_current(modules,out,error);
}
static bool native_current(void *context,const frontend_q3_content_native_receipt *receipt,qa_error *error)
{
    frontend_remote_q3 *row=context; frontend_remote_q3_session_view view;
    frontend_remote_q3_services_view services;
    return receipt && frontend_remote_q3_session_read(row,&view,error) && !view.pure &&
        frontend_remote_q3_services_read(row,&services,error) && receipt->client==services.client &&
        receipt->media_view==view.resources.mounts && receipt->producer==row && receipt->current==native_current;
}
bool frontend_remote_q3_session_native_media_read(frontend_remote_q3 *row,
    frontend_q3_content_native_receipt *cgame,frontend_q3_content_role_receipt *ui,qa_error *error)
{
    frontend_remote_q3_session_view view; frontend_remote_q3_services_view services;
    if(!cgame || !ui || !frontend_remote_q3_session_read(row,&view,error) || view.pure ||
        !frontend_remote_q3_services_read(row,&services,error) || !role_read(view.modules,QA_QVM_UI,ui,error)) return false;
    *cgame=(frontend_q3_content_native_receipt){services.client,view.resources.mounts,row,native_current};
    return native_current(row,cgame,error) && role_current(view.modules,ui,error);
}
bool frontend_remote_q3_session_modules_media_read(frontend_remote_q3 *row,
    const application_native_q3_client_modules **modules,frontend_q3_content_role_receipt *cgame,
    frontend_q3_content_role_receipt *ui,qa_error *error)
{
    frontend_remote_q3_session_view view;
    if(!modules || !cgame || !ui || !frontend_remote_q3_session_read(row,&view,error) || !view.pure ||
        !role_read(view.modules,QA_QVM_CGAME,cgame,error) || !role_read(view.modules,QA_QVM_UI,ui,error)) return false;
    *modules=view.module_owner;
    return frontend_remote_q3_session_current(&view) && role_current(view.modules,cgame,error) &&
        role_current(view.modules,ui,error);
}
static bool acquired_draw(const frontend_remote_q3_session_view *view,uint32_t stereo,
    qa_audio_listener *listener,bool *present,qa_error *error)
{
    qa_frontend *f=frontend_remote_q3_frontend(view->resources.owner);
    frontend_remote_q3_module_media media; q3n_remote_publication publication;
    if(!f || !frontend_remote_q3_modules_media_read(view->modules,QA_QVM_CGAME,&media,error) ||
        !qa_q3_presentation_frame(media.presentation,&f->frame,
            frontend_viewport(f,view->resources.physical_seat),error) ||
        !frontend_remote_q3_modules_listener_begin(view->modules,QA_QVM_CGAME,error) ||
        !frontend_network_native_publication_read(f,&publication,error) ||
        !frontend_network_native_publication_current(f,&publication)) return false;
    int32_t arguments[]={view->resources.domain.source.receiver.source_milliseconds,(int32_t)stereo,
        publication.demo_playback?1:0};
    int32_t result;
    if(!qa_application_native_q3_client_modules_call(view->module_owner,QA_QVM_CGAME,3,
        arguments,3,&result,error) || !frontend_remote_q3_modules_listener_read(view->modules,
        QA_QVM_CGAME,listener,present,error)) return false;
    if(qa_input_seat_catcher(view->resources.input,
        view->resources.domain.source.receiver.service_owner)&QA_INPUT_CATCH_UI) {
        frontend_remote_q3_module_media ui;
        uint32_t word=(uint32_t)(f->wall_time_ns/UINT64_C(1000000));
        int32_t realtime; memcpy(&realtime,&word,sizeof(realtime));
        if(!frontend_remote_q3_modules_media_read(view->modules,QA_QVM_UI,&ui,error) ||
            !frontend_remote_q3_modules_media_current(&ui) ||
            !qa_q3_presentation_frame(ui.presentation,&f->frame,
                frontend_viewport(f,view->resources.physical_seat),error) ||
            !qa_application_native_q3_client_modules_call(view->module_owner,QA_QVM_UI,5,
                &realtime,1,&result,error)) return false;
    }
    frontend_remote_q3_session_view returned;
    return frontend_remote_q3_session_read(view->resources.owner,&returned,error) &&
        returned.module_owner==view->module_owner && returned.pure;
}
bool frontend_remote_q3_session_draw(frontend_remote_q3 *row,frontend_remote_prediction *predictor,
    uint32_t stereo,qa_audio_listener *listener,bool *has_listener,qa_error *error)
{
    if(has_listener) *has_listener=false;
    frontend_remote_q3_session_view view;
    if(!listener || !has_listener || stereo>2 || !frontend_remote_q3_session_read(row,&view,error)) return false;
    if(view.pure) return acquired_draw(&view,stereo,listener,has_listener,error);
    if(!predictor) return fail(error,"Compiled remote Draw requires its actual paired predictor");
    bool okay=frontend_remote_q3_runtime_prepare(view.runtime,stereo,error);
    bool information=false,active=false,predicted=false;
    if(okay) okay=frontend_remote_q3_frame_information(view.frames,view.runtime,
        frontend_remote_q3_runtime_information_draw,&information,error);
    if(okay && !information) {
        frontend_remote_q3_services_view services; q3n_remote_source_view source;
        qa_native_q3_remote_client_cache cache;
        okay=frontend_remote_q3_services_read(row,&services,error) &&
            q3n_remote_source_read(services.source,&source,error) &&
            qa_native_q3_remote_client_cache_read(services.client,&cache,error);
        if(okay) {
            frontend_remote_snapshot_settings settings={source.publication.demo_playback,
                cache.no_predict!=0,cache.synchronous_clients!=0};
            okay=qa_native_q3_remote_client_cache_current(services.client,&cache) &&
                q3n_remote_source_current(&source) && frontend_remote_q3_frame_process(view.frames,&settings,error) &&
                frontend_remote_q3_runtime_before_prediction(view.runtime,&active,error);
        }
        if(okay && active) {
            okay=frontend_remote_q3_frame_predict(view.frames,predictor,&predicted,error);
            if(okay && !predicted) okay=fail(error,"Active CGAME Draw lacks its genuine prediction completion");
            if(okay) okay=frontend_remote_q3_frame_draw(view.frames,view.runtime,frontend_remote_q3_runtime_draw,error);
        } else if(okay) okay=frontend_remote_q3_frame_waiting(view.frames,view.runtime,
            frontend_remote_q3_runtime_waiting_draw,error);
    }
    qa_error ended={0};
    bool returned=frontend_remote_q3_runtime_frame_end(view.runtime,okay,&ended);
    if(!returned && error) {
        if(okay || error->code==QA_OK) *error=ended;
        else {
            qa_error original=*error;
            qa_error_set(error,original.code,original.offset,"%s; remote frame cleanup: %s",
                original.message,ended.message);
        }
    }
    if(!okay || !returned) return false;
    frontend_remote_q3_session_view completed;
    return frontend_remote_q3_runtime_listener(view.runtime,listener,has_listener,error) &&
        frontend_remote_q3_session_read(row,&completed,error) && completed.runtime==view.runtime &&
        completed.frames==view.frames && completed.module_owner==view.module_owner && !completed.pure;
}
