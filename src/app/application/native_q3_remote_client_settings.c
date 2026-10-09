#include "native_q3_remote_client_settings.h"
#include "native_q3_client_settings.h"
#include "native_q3_remote_client.h"

static bool fail(qa_error *error,const char *message)
{ qa_error_set(error,QA_ERROR_ARGUMENT,0,"%s",message); return false; }
static bool completed(const qa_native_q3_remote_client_service *service,
    const qa_native_q3_remote_client_cache *cache,qa_error *error)
{
    return qa_native_q3_remote_client_cache_current(service,cache) ||
        fail(error,"Remote CGAME settings changed during their cache projection");
}
static bool remote_settings_cvar(const void *context,qa_native_q3_cvar_id id,
    qa_native_q3_client_cvar *out,qa_error *error)
{ return qa_native_q3_remote_client_cvar_read(context,id,out,error); }

bool application_native_q3_remote_client_view_settings(const qa_native_q3_remote_client_service *service,
    int32_t dm_flags,bool ragepro,q3n_view_settings *out,qa_error *error)
{
    qa_native_q3_remote_client_cache witness;
    if(!out || !qa_native_q3_remote_client_cache_read(service,&witness,error))return false;
    application_q3_client_settings_source source={.context=service,.read=remote_settings_cvar};
    q3n_view_settings value;
    if(!application_q3_client_view_settings(&source,dm_flags,ragepro,&value,error) ||
       !completed(service,&witness,error))return false;
    *out=value; return true;
}
bool application_native_q3_remote_client_hud_settings(const qa_native_q3_remote_client_service *service,
    q3n_hud_settings *out,qa_error *error)
{
    qa_native_q3_remote_client_cache witness;
    if(!out || !qa_native_q3_remote_client_cache_read(service,&witness,error))return false;
    application_q3_client_settings_source source={.context=service,.read=remote_settings_cvar};
    q3n_hud_settings value;
    if(!application_q3_client_hud_settings(&source,&value,error) ||
       !qa_native_q3_remote_client_local_server_read(service,&value.local_server,error) ||
       !completed(service,&witness,error))return false;
    *out=value; return true;
}
bool application_native_q3_remote_client_set_view_size(void *context,int32_t size,qa_error *error)
{ return qa_native_q3_remote_client_set_view_size(context,size,error); }
bool application_native_q3_remote_client_set_orbit_angle(void *context,float angle,qa_error *error)
{ return qa_native_q3_remote_client_cvar_number(context,QA_NATIVE_Q3_CVAR_cg_thirdPersonAngle,angle,error); }

bool application_native_q3_remote_client_info_settings(const qa_native_q3_remote_client_service *service,
    size_t memory_remaining,bool loading,q3n_client_settings *out,qa_error *error)
{
    qa_native_q3_remote_client_cache witness; qa_native_q3_remote_client_basis basis;
    if(!out || !qa_native_q3_remote_client_cache_read(service,&witness,error) ||
       !qa_native_q3_remote_client_basis_read(service,&basis,error))return false;
    application_q3_client_settings_source source={.context=service,.read=remote_settings_cvar,
        .cvars=basis.client.cvars,.refs=&service->cvar_refs,.product=basis.product};
    q3n_client_settings value;
    if(!application_q3_client_info_settings(&source,memory_remaining,loading,&value,error) ||
       !completed(service,&witness,error))return false;
    *out=value; return true;
}
bool application_native_q3_remote_client_frame_settings(const qa_native_q3_remote_client_service *service,
    int32_t dm_flags,bool ragepro,size_t memory_remaining,bool loading,bool demo,uint32_t stereo,
    q3n_native_frame_options *out,qa_error *error)
{
    qa_native_q3_remote_client_cache witness; qa_native_q3_remote_client_basis basis;
    if(!out || stereo>2 || !qa_native_q3_remote_client_cache_read(service,&witness,error) ||
       !qa_native_q3_remote_client_basis_read(service,&basis,error))return false;
    application_q3_client_settings_source source={.context=service,.read=remote_settings_cvar,
        .cvars=basis.client.cvars,.refs=&service->cvar_refs,.product=basis.product};
    q3n_native_frame_options value;
    if(!application_q3_client_frame_settings(&source,dm_flags,ragepro,memory_remaining,loading,demo,stereo,&value,error) ||
       !qa_native_q3_remote_client_local_server_read(service,&value.hud.local_server,error) ||
       !completed(service,&witness,error))return false;
    *out=value; return true;
}
