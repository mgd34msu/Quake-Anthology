#include "native_q3_client.h"
#include "native_q3_system_info.h"
#include "qa/text.h"
#include "qa/game_q3_configstrings.h"
#include "qa/source_frame_time.h"
#include <stdlib.h>
#include <string.h>

static const char *const time_names[]={"timescale","fixedtime","com_cameraMode",
    "cm_noAreas","cm_noCurves","cm_playerCurveClip"};
static char *retain_text(const char *text)
{
    size_t length=strlen(text); char *copy=malloc(length+1);
    if (copy) memcpy(copy,text,length+1);
    return copy;
}
bool native_q3_system_info_apply(qa_cvars *cvars, native_q3_system_info *retained, const char *received,
    native_q3_system_info_policy policy, qa_error *error)
{
    char copy[QA_Q3_GAMESTATE_CHARS];
    size_t length=strlen(received);
    memcpy(copy,received,length+1);
    bool same=policy.skip_same_timescale && retained->present && !strcmp(copy,retained->text),ok=true;
    const char *cursor=copy; qa_bytes key,value;
    while (ok && *cursor && qa_info_next(&cursor,&key,&value)) {
        char *name=(char *)key.data,*text=(char *)value.data;
        char *name_end=name+key.size,*value_end=text+value.size;
        char name_separator=*name_end,value_separator=*value_end;
        *name_end=0; *value_end=0;
        bool skip=!key.size || qa_text_ascii_equal(name,"cl_allowdownload") ||
            (same && qa_text_ascii_equal(name,"timescale"));
        if (policy.skip_time_controls) for (size_t i=0;i<3;++i)
            skip=skip || qa_text_ascii_equal(name,time_names[i]);
        if (!skip) ok=qa_cvars_set(cvars,name,text,true,error) &&
            (!policy.after_set || policy.after_set(policy.context));
        *name_end=name_separator; *value_end=value_separator;
    }
    if (ok) { memcpy(retained->text,copy,length+1); retained->present=true; }
    return ok;
}
bool native_client_time_register(qa_native_q3_client_service *service,qa_error *error)
{
    qa_cvars *source=service->services.client.client_time_cvars;
    if (!source) return true;
    for (size_t i=0;i<6;++i) {
        const qa_cvar_view *value=qa_cvars_find(source,time_names[i]);
        if (!value) continue;
        char *reset=retain_text(value->reset_value); uint32_t flags=value->flags;
        if (!reset) return native_client_fail(error,QA_ERROR_MEMORY,"Retaining source timing declaration");
        bool ok=qa_cvars_register(service->services.client.cvars,time_names[i],reset,flags,
            service->services.client.service_owner,"Shared source frame control",error);
        free(reset);
        if (!ok) return false;
    }
    return true;
}
static bool refresh_system_info(qa_native_q3_client_service *service,qa_error *error)
{
    const char *borrowed; uint64_t revision;
    if (!qa_native_q3_wire_reader_configstring(service->services.wire_reader,1,&borrowed,&revision,error)) return false;
    if (service->has_system_info_revision && service->system_info_revision==revision) return true;
    native_q3_system_info_policy policy={
        .skip_time_controls=service->services.client.client_time_cvars!=NULL,
        .skip_same_timescale=true};
    bool ok=native_q3_system_info_apply(service->services.client.cvars,&service->system_info,borrowed,policy,error);
    if (ok) {
        service->system_info_revision=revision; service->has_system_info_revision=true;
    }
    return ok;
}
bool qa_native_q3_client_prepare(qa_native_q3_client_service *service,qa_error *error)
{
    if (!service || service->registered || service->updating || !qa_native_q3_client_service_current(service))
        return native_client_fail(error,QA_ERROR_ARGUMENT,"Native CGAME preparation requires its actual pre-Init constructor");
    service->updating=true;
    bool ok=refresh_system_info(service,error) &&
        qa_cvars_set(service->services.client.cvars,"sv_running","1",true,error);
    service->updating=false; return ok;
}
bool qa_native_q3_client_refresh(qa_native_q3_client_service *service,qa_error *error)
{
    if (!service || service->updating || !qa_native_q3_client_service_current(service))
        return native_client_fail(error,QA_ERROR_ARGUMENT,"Native CGAME frame refresh requires its actual seat owner");
    service->updating=true;
    bool ok=refresh_system_info(service,error);
    service->updating=false; return ok;
}
bool qa_native_q3_client_system_info(qa_native_q3_client_service *service,qa_error *error)
{
    if (!service || service->updating || !qa_native_q3_client_service_current(service))
        return native_client_fail(error,QA_ERROR_ARGUMENT,"Native CGAME SystemInfo requires its actual reached client gamestate");
    service->updating=true;
    bool ok=refresh_system_info(service,error);
    service->updating=false; return ok;
}
bool qa_native_q3_client_frame_time(qa_native_q3_client_service *service,double supplied,
    double *out,qa_error *error)
{
    if (!service || !out || !qa_native_q3_client_service_current(service))
        return native_client_fail(error,QA_ERROR_ARGUMENT,"Native CGAME FrameTime lacks its actual client context");
    qa_cvars *owner=service->services.client.client_time_cvars;
    if (!owner) { *out=supplied; return true; }
    return qa_source_frame_time_sample(&service->frame_time,supplied,false,true,out,error);
}
bool native_client_time_fields(qa_source_save_io *io,qa_native_q3_client_service *service)
{
    bool present=service->system_info.present,owner=service->services.client.client_time_cvars!=NULL;
    bool alias=owner && service->services.client.client_time_cvars==service->services.client.cvars;
    /* Consume the retired mirror metadata in the existing save layout. */
    bool saved_owner=owner,saved_alias=alias,bound=false,names[6]={0};
    if (!qa_source_save_bool(io,&saved_owner) || saved_owner!=owner ||
        !qa_source_save_bool(io,&saved_alias) || saved_alias!=alias ||
        !qa_source_save_bool(io,&bound) ||
        (bound && (!owner || alias || !service->services.client.initialized)) || !qa_source_save_bool(io,&present)) return false;
    for (size_t i=0;i<6;++i) if (!qa_source_save_bool(io,&names[i]) ||
        (names[i] && (!owner || alias))) return false;
    if (present) {
        size_t length=io->direction==QA_SOURCE_SAVE_WRITE?strlen(service->system_info.text):0;
        if (!qa_source_save_count(io,&length,QA_Q3_BIG_INFO_CHARS-1)) return false;
        if (!qa_source_save_bytes(io,service->system_info.text,length) || memchr(service->system_info.text,0,length)) return false;
        service->system_info.text[length]=0;
    }
    service->system_info.present=present;
    return true;
}
