#include "native_q3_client.h"
#include "qa/console_cvar_observer.h"
#include <stdlib.h>
#include <string.h>

bool native_client_cvar_fields(qa_source_save_io *io,qa_native_q3_client_cvar *value)
{
    size_t length=io->direction==QA_SOURCE_SAVE_WRITE?strlen(value->value):0;
    if (!qa_source_save_count(io,&length,sizeof(value->value)-1)) return false;
    if (io->direction==QA_SOURCE_SAVE_READ) memset(value->value,0,sizeof(value->value));
    if (!qa_source_save_bytes(io,value->value,length) || memchr(value->value,0,length)) return false;
    value->value[length]=0;
    return qa_source_save_f32(io,&value->number) && qa_source_save_i32(io,&value->integer) &&
        qa_source_save_u64(io,&value->modification_count);
}
static bool configuration_fields(qa_source_save_io *io,qa_native_q3_client_service *service)
{
    uint8_t magic[4]={'Q','N','C','G'}; uint32_t product=service->product;
    size_t count=service->count;
    if (!qa_source_save_bytes(io,magic,sizeof(magic)) || memcmp(magic,"QNCG",sizeof(magic)) ||
        !qa_source_save_u32(io,&product) ||
        product!=(uint32_t)service->product || !qa_source_save_count(io,&count,QA_NATIVE_CLIENT_CVARS) ||
        count!=service->count || !qa_source_save_bool(io,&service->registered) ||
        !qa_source_save_bool(io,&service->services.client.initialized) ||
        !qa_source_save_u64(io,&service->force_model_count) ||
        !qa_source_save_u64(io,&service->overlay_count) ||
        !qa_source_save_bool(io,&service->overlay_initial) ||
        !qa_source_save_i32(io,&service->local_server) || !native_client_time_fields(io,service) ||
        (service->services.client.initialized && !service->registered) ||
        (service->time_bound && !service->services.client.initialized)) return false;
    for (size_t i=0;i<count;++i) {
        if (!native_client_cvar_fields(io,&service->cache[i])) return false;
        if (native_client_definitions[i].missionpack && service->product!=QA_Q3_TEAM_ARENA) {
            const qa_native_q3_client_cvar *value=&service->cache[i];
            if (*value->value || value->number!=0 || value->integer || value->modification_count) return false;
        }
    }
    /* Failed registration retains its real partially copied constructor
     * prefix. registered=false is not evidence that every cache is zero. */
    return true;
}
bool qa_native_q3_client_checkpoint(const qa_native_q3_client_service *service,qa_buffer *out,qa_error *error)
{
    if (!service || !qa_native_q3_client_service_idle(service) || !out || out->data || out->size ||
        !qa_native_q3_client_service_current(service) ||
        !qa_cvars_observer_idle(service->services.client.cvars) ||
        !qa_cvars_observer_idle(service->services.client.source_cvars) ||
        (service->services.client.client_time_cvars && !qa_cvars_observer_idle(service->services.client.client_time_cvars)))
        return native_client_fail(error,QA_ERROR_ARGUMENT,"Native CGAME capture requires its actual idle constructor graph");
    qa_native_q3_client_service state=*service; qa_source_save_io io={0};
    bool ok=qa_source_save_writer(&io,NULL,error) && configuration_fields(&io,&state) &&
        qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); return ok;
}
bool qa_native_q3_client_restore(qa_application *app,const qa_native_q3_client_basis *source,
    qa_native_q3_client_services *services,qa_native_q3_character_selection *character,qa_bytes bytes,
    qa_native_q3_client_service **out,qa_error *error)
{
    if (!out) return native_client_fail(error,QA_ERROR_ARGUMENT,"Native CGAME import requires output");
    qa_native_q3_client_service *service=NULL;
    if (!native_client_allocate_bound(app,source,services,character,&service,error)) return false;
    qa_source_save_io io={0};
    bool ok=qa_source_save_reader(&io,NULL,bytes,error) && configuration_fields(&io,service) &&
        qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    if (ok && service->time_bound) {
        service->time_bound=false;
        ok=native_client_time_bind(service,true,error);
    }
    if (ok && service->registered) for (size_t i=0;ok && i<service->count;++i) {
        const native_client_definition *definition=&native_client_definitions[i];
        if (definition->missionpack && service->product!=QA_Q3_TEAM_ARENA) continue;
        /* A retained cache can intentionally differ from engine values after
         * direct numeric writes or before CG_UpdateCvars. Existence of its
         * actual restored registration is the only registry requirement. */
        ok=qa_cvars_find(service->services.client.cvars,definition->name)!=NULL;
    }
    if (!ok) {
        native_client_time_close(service); free(service->system_info);
        qa_launch_instance_lease_release(service->source_lease); free(service);
        if (!error || error->code==QA_OK) native_client_fail(error,QA_ERROR_FORMAT,"Invalid native CGAME constructor continuation");
        return false;
    }
    *services=(qa_native_q3_client_services){0}; *character=(qa_native_q3_character_selection){0};
    *out=service; return true;
}
