#include "native_q3_client.h"
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
static bool same_name(const char *text,const char *name)
{
    while (*text && *name) {
        unsigned a=(unsigned char)*text++,b=(unsigned char)*name++;
        if (a>='A' && a<='Z') a+='a'-'A';
        if (b>='A' && b<='Z') b+='a'-'A';
        if (a!=b) return false;
    }
    return !*text && !*name;
}
static bool refresh_system_info(qa_native_q3_client_service *service,qa_error *error)
{
    const char *borrowed; uint64_t revision;
    if (!qa_native_q3_wire_reader_configstring(service->services.wire_reader,1,&borrowed,&revision,error)) return false;
    if (service->has_system_info_revision && service->system_info_revision==revision) return true;
    char *retained=retain_text(borrowed),*working=retain_text(borrowed);
    if (!retained || !working) { free(retained); free(working); return native_client_fail(error,QA_ERROR_MEMORY,"Retaining actual native SystemInfo"); }
    bool same=service->system_info && !strcmp(retained,service->system_info),ok=true;
    char *cursor=working;
    if (*cursor=='\\') ++cursor;
    while (ok && *cursor) {
        char *name=cursor,*separator=strchr(cursor,'\\');
        if (!separator) break;
        *separator=0; char *value=separator+1;
        separator=strchr(value,'\\');
        if (separator) { *separator=0; cursor=separator+1; } else cursor=value+strlen(value);
        bool skip=! *name || same_name(name,"cl_allowdownload") || (same && same_name(name,"timescale"));
        if (service->services.client.client_time_cvars) for (size_t i=0;i<3;++i)
            skip=skip || same_name(name,time_names[i]);
        if (!skip) ok=qa_cvars_set(service->services.client.cvars,name,value,true,error);
    }
    free(working);
    if (ok) {
        free(service->system_info); service->system_info=retained;
        service->system_info_revision=revision; service->has_system_info_revision=true;
    }
    else free(retained);
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
    bool present=service->system_info!=NULL,owner=service->services.client.client_time_cvars!=NULL;
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
        size_t length=io->direction==QA_SOURCE_SAVE_WRITE?strlen(service->system_info):0;
        if (!qa_source_save_count(io,&length,QA_Q3_BIG_INFO_CHARS-1)) return false;
        if (io->direction==QA_SOURCE_SAVE_READ) {
            service->system_info=malloc(length+1);
            if (!service->system_info) return false;
        }
        if (!qa_source_save_bytes(io,service->system_info,length) || memchr(service->system_info,0,length)) return false;
        service->system_info[length]=0;
    }
    return true;
}
