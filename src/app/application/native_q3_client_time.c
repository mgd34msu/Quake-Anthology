#include "native_q3_client.h"
#include "native_q3_settings.h"
#include "qa/game_q3_configstrings.h"
#include "../frontend/frame_time.h"
#include <stdlib.h>
#include <string.h>

static const char *const time_names[]={"timescale","fixedtime","com_cameraMode",
    "cm_noAreas","cm_noCurves","cm_playerCurveClip"};
static char *retain_text(const char *text)
{
    size_t length=strlen(text); char *copy=malloc(length+1);
    if (copy) memcpy(copy,text,length+1); return copy;
}
static bool write_value(qa_native_q3_client_service *service,qa_cvars *registry,
    const char *name,const char *value,qa_error *error)
{
    if (!qa_native_q3_client_service_current(service))
        return native_client_fail(error,QA_ERROR_ARGUMENT,"Native CGAME timing owner retired before publication");
    bool ok=qa_cvars_set(registry,name,value,true,error);
    return ok && (qa_native_q3_client_service_current(service) ||
        native_client_fail(error,QA_ERROR_ARGUMENT,"Native CGAME timing owner retired during publication"));
}
static bool refresh_time(qa_native_q3_client_service *service,bool subscribed,qa_error *error)
{
    qa_cvars *owner=service->services.client.client_time_cvars,*mirror=service->services.client.cvars;
    if (!owner || owner==mirror) return true;
    bool ok=true; bool suppressed[6]={0};
    if (subscribed) for (size_t i=0;ok && i<6;++i) if (service->time_names[i]) {
        ok=qa_cvars_observer_suppress(mirror,service->time_mirror_tokens[i],true,error);
        suppressed[i]=ok;
    }
    for (size_t i=0;ok && i<(subscribed?6u:3u);++i) {
        if (subscribed && !service->time_names[i]) continue;
        const qa_cvar_view *value=qa_cvars_find(owner,time_names[i]);
        if (!value) {
            if (subscribed) ok=native_client_fail(error,QA_ERROR_FORMAT,"Native CGAME subscribed source control retired");
            continue;
        }
        char *copy=retain_text(value->value);
        if (!copy) { ok=native_client_fail(error,QA_ERROR_MEMORY,"Retaining native CGAME frame-control value"); break; }
        ok=write_value(service,mirror,time_names[i],copy,error); free(copy);
    }
    for (size_t i=0;i<6;++i) if (suppressed[i]) {
        qa_error cleanup={0};
        if (!qa_cvars_observer_suppress(mirror,service->time_mirror_tokens[i],false,&cleanup)) {
            if (ok && error) *error=cleanup; ok=false;
        }
    }
    return ok;
}
static bool time_enter(qa_native_q3_client_service *service,qa_error *error)
{
    if (!qa_native_q3_client_service_current(service) || service->time_busy==SIZE_MAX)
        return native_client_fail(error,QA_ERROR_ARGUMENT,"Native CGAME timing callback has no live role");
    ++service->time_busy; return true;
}
static bool owner_published(void *context,qa_cvars *registry,const char *name,qa_error *error)
{
    qa_native_q3_client_service *service=context; (void)name;
    if (!time_enter(service,error)) return false;
    bool ok=registry==service->services.client.client_time_cvars && refresh_time(service,true,error);
    --service->time_busy; return ok;
}
static bool mirror_published(void *context,qa_cvars *registry,const char *name,qa_error *error)
{
    qa_native_q3_client_service *service=context;
    if (!time_enter(service,error)) return false;
    const qa_cvar_view *value=registry==service->services.client.cvars?qa_cvars_find(registry,name):NULL;
    char *copy=value?retain_text(value->value):NULL;
    bool ok=copy && write_value(service,service->services.client.client_time_cvars,name,copy,error);
    if (!copy) native_client_fail(error,value?QA_ERROR_MEMORY:QA_ERROR_FORMAT,"Native CGAME frame-control publication lost its value");
    free(copy); --service->time_busy; return ok;
}
void native_client_time_close(qa_native_q3_client_service *service)
{
    for (size_t i=0;i<6;++i) {
        if (service->time_owner_tokens[i]) qa_cvars_unobserve(service->services.client.client_time_cvars,service->time_owner_tokens[i]);
        if (service->time_mirror_tokens[i]) qa_cvars_unobserve(service->services.client.cvars,service->time_mirror_tokens[i]);
        service->time_owner_tokens[i]=service->time_mirror_tokens[i]=0;
    }
    service->time_bound=false;
}
bool native_client_time_bind(qa_native_q3_client_service *service,bool restoring,qa_error *error)
{
    qa_cvars *owner=service->services.client.client_time_cvars,*mirror=service->services.client.cvars;
    if (!owner || owner==mirror) return true;
    if (service->time_bound || service->time_busy || qa_cvars_dialect(owner)!=QA_CONSOLE_Q3)
        return native_client_fail(error,QA_ERROR_ARGUMENT,"Native CGAME frame-time binding has changed its actual owner");
    if (!restoring) {
        memset(service->time_names,0,sizeof(service->time_names));
        for (size_t i=0;i<6;++i) {
            const qa_cvar_view *value=qa_cvars_find(owner,time_names[i]);
            if (!value) continue;
            service->time_names[i]=true;
            /* Copy borrowed registry strings before registry callbacks. */
            char *reset=retain_text(value->reset_value); uint32_t flags=value->flags;
            if (!reset) return native_client_fail(error,QA_ERROR_MEMORY,"Retaining actual source timing declaration");
            bool ok=qa_cvars_register(mirror,time_names[i],reset,flags,service->services.client.service_owner,
                "Shared source frame control",error);
            free(reset);
            if (!ok || !qa_native_q3_client_service_current(service)) return false;
        }
        if (!refresh_time(service,false,error)) return false;
        /* SharedCvarMirror's first refresh includes collision controls too. */
        for (size_t i=3;i<6;++i) if (service->time_names[i]) {
            const qa_cvar_view *value=qa_cvars_find(owner,time_names[i]);
            char *copy=value?retain_text(value->value):NULL;
            bool ok=copy && write_value(service,mirror,time_names[i],copy,error);
            free(copy); if (!ok) return false;
        }
    }
    for (size_t i=0;i<6;++i) if (service->time_names[i]) {
        if (!qa_cvars_find(owner,time_names[i]) || !qa_cvars_find(mirror,time_names[i]) ||
            !qa_cvars_observe(mirror,time_names[i],service->services.client.service_owner,
                mirror_published,service,&service->time_mirror_tokens[i],error) ||
            !qa_cvars_observe(owner,time_names[i],service->services.client.service_owner,
                owner_published,service,&service->time_owner_tokens[i],error)) {
            native_client_time_close(service); return false;
        }
    }
    service->time_bound=true; return true;
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
        if (!skip) ok=write_value(service,service->services.client.cvars,name,value,error);
    }
    free(working);
    if (ok) { free(service->system_info); service->system_info=retained; }
    else free(retained);
    return ok && refresh_time(service,service->time_bound,error);
}
static bool refresh_settings(qa_native_q3_client_service *service,qa_error *error)
{
    application_provider *source=application_world_provider(service->application,QA_ROLE_ENTITIES,"");
    qa_cvars *registry=service->services.client.source_cvars;
    struct {char *name,*value;} settings[QA_NATIVE_CLIENT_CVARS]={0};
    size_t count=0; bool ok=true;
    /* serverSettings() captures its source snapshot array before a CGAME set
     * can reenter and change GAME cvars. Keep that precise publication cut. */
    size_t extent=qa_cvars_count(registry);
    for (size_t i=0;ok && i<extent;++i) {
        const qa_cvar_view *value=qa_cvars_at(registry,i);
        if (!value) { ok=native_client_fail(error,QA_ERROR_FORMAT,"Native GAME settings registry changed its retained order"); break; }
        const char *name=value->name; bool shared=false;
        for (size_t j=0;j<service->count;++j) if ((!native_client_definitions[j].missionpack ||
            service->product==QA_Q3_TEAM_ARENA) && same_name(name,native_client_definitions[j].name)) { shared=true; break; }
        if (!shared) continue;
        const application_native_q3_cvar_snapshot *declared;
        if (!application_native_q3_settings_snapshot(source,name,&declared,NULL)) continue;
        if (count==QA_NATIVE_CLIENT_CVARS) { ok=native_client_fail(error,QA_ERROR_FORMAT,"Native GAME shared setting inventory is ambiguous"); break; }
        settings[count].name=retain_text(name); settings[count].value=retain_text(value->value); ++count;
        if (!settings[count-1].name || !settings[count-1].value)
            ok=native_client_fail(error,QA_ERROR_MEMORY,"Retaining actual GAME setting for CGAME");
    }
    for (size_t i=0;ok && i<count;++i)
        ok=write_value(service,service->services.client.cvars,settings[i].name,settings[i].value,error);
    for (size_t i=0;i<count;++i) { free(settings[i].name); free(settings[i].value); }
    return ok;
}
bool qa_native_q3_client_prepare(qa_native_q3_client_service *service,qa_error *error)
{
    if (!service || service->registered || service->updating || !qa_native_q3_client_service_current(service))
        return native_client_fail(error,QA_ERROR_ARGUMENT,"Native CGAME preparation requires its actual pre-Init constructor");
    service->updating=true;
    bool ok=refresh_settings(service,error) && refresh_system_info(service,error) &&
        write_value(service,service->services.client.cvars,"sv_running","1",error);
    service->updating=false; return ok;
}
bool qa_native_q3_client_refresh(qa_native_q3_client_service *service,qa_error *error)
{
    if (!service || service->updating || !qa_native_q3_client_service_current(service))
        return native_client_fail(error,QA_ERROR_ARGUMENT,"Native CGAME frame refresh requires its actual seat owner");
    service->updating=true;
    bool ok=refresh_system_info(service,error) && refresh_settings(service,error);
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
    return frontend_frame_time_sample(owner,supplied,false,true,out,error);
}
bool native_client_time_fields(qa_source_save_io *io,qa_native_q3_client_service *service)
{
    bool present=service->system_info!=NULL,owner=service->services.client.client_time_cvars!=NULL;
    bool alias=owner && service->services.client.client_time_cvars==service->services.client.cvars;
    bool saved_owner=owner,saved_alias=alias;
    if (!qa_source_save_bool(io,&saved_owner) || saved_owner!=owner ||
        !qa_source_save_bool(io,&saved_alias) || saved_alias!=alias ||
        !qa_source_save_bool(io,&service->time_bound) ||
        (service->time_bound && (!owner || alias)) || !qa_source_save_bool(io,&present)) return false;
    for (size_t i=0;i<6;++i) if (!qa_source_save_bool(io,&service->time_names[i]) ||
        (service->time_names[i] && (!owner || alias))) return false;
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
