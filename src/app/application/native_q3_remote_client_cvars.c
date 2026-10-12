#include "native_q3_remote_client.h"
#include "native_q3_client_settings.h"
#include "native_q3_system_info.h"

static bool enter(qa_native_q3_remote_client_service *service, qa_error *error)
{
    if (!qa_native_q3_remote_client_current(service) || service->updating || service->cache_revision == UINT64_MAX)
        return native_client_fail(error, QA_ERROR_ARGUMENT, "Remote CGAME cache stage requires its idle actual service");
    service->updating = true; ++service->cache_revision; return true;
}
static bool current(void *context)
{ return qa_native_q3_remote_client_current(context); }
static bool configstring(void *context, uint32_t index, const char **text, qa_error *error)
{
    qa_native_q3_remote_client_service *service = context;
    const qa_q3_gamestate *state = service->services.network.gamestate(service->services.network.context);
    if (!state || !qa_native_q3_remote_client_current(service))
        return native_client_fail(error, QA_ERROR_ARGUMENT, "Remote force-model reload lost its reached gamestate");
    *text = qa_q3_configstring(state, index); return true;
}
static bool reload_client_info(void *context, uint32_t slot, const char *text, qa_error *error)
{
    qa_native_q3_remote_client_service *service = context;
    return service->services.reload_client_info(service->services.context, slot, text, error);
}
static native_client_cache_access cache_access(qa_native_q3_remote_client_service *service)
{
    return (native_client_cache_access){.context = service, .current = current,
        .configstring = configstring, .reload_client_info = reload_client_info,
        .registry = service->services.basis.client.cvars, .owner = service->services.basis.client.service_owner,
        .product = service->services.basis.product, .cache = service->cache, .refs=&service->cvar_refs, .count = native_client_definition_count,
        .oversized_error = "Remote Cvar_Update exceeds MAX_CVAR_VALUE_STRING",
        .reload_memory_error = "Retaining reached remote player configstring"};
}
bool qa_native_q3_remote_client_register(qa_native_q3_remote_client_service *service, qa_error *error)
{
    if (!enter(service, error)) return false;
    native_client_cache_access access = cache_access(service);
    bool ok = native_client_cache_register(&access, "Native remote Q3 CGAME", &service->local_server,
        &service->force_model_count, error);
    if (ok) service->registered = true;
    service->updating = false; return ok;
}
bool qa_native_q3_remote_client_userinfo_initialize(qa_native_q3_remote_client_service *service,
    const char *name, qa_error *error)
{
    if (!name || !service || service->registered || service->updating || !qa_native_q3_remote_client_current(service))
        return native_client_fail(error, QA_ERROR_ARGUMENT, "Remote userinfo requires its real pre-registration CLIENT");
    native_client_cache_access access = cache_access(service);
    const native_client_userinfo_text description = {"Native remote seat userinfo", "Native remote seat identity",
        "Selected remote CHARACTER", "Remote CHARACTER declaration exceeds capacity",
        "Formatting actual remote CHARACTER userinfo"};
    service->updating = true;
    bool ok = native_client_cache_userinfo(&access, &service->character, name, &description, error);
    service->updating = false; return ok;
}
bool qa_native_q3_remote_client_force_model_change(qa_native_q3_remote_client_service *service, qa_error *error)
{
    if (!service || !service->registered || service->updating || !qa_native_q3_remote_client_current(service))
        return native_client_fail(error, QA_ERROR_ARGUMENT, "Remote force-model refresh lost its registered CLIENT");
    native_client_cache_access access = cache_access(service);
    service->updating = true;
    bool ok = native_client_cache_reload(&access, error);
    service->updating = false; return ok;
}
bool qa_native_q3_remote_client_update(qa_native_q3_remote_client_service *service, qa_error *error)
{
    if (!service || !service->registered || !enter(service, error)) return false;
    native_client_cache_access access = cache_access(service);
    bool ok = native_client_cache_update(&access, &service->overlay_initial,
        &service->overlay_count, &service->force_model_count, error);
    service->updating = false; return ok;
}
static bool system_info(qa_native_q3_remote_client_service *service, qa_error *error)
{
    const qa_q3_gamestate *state = service->services.network.gamestate(service->services.network.context);
    if (!state || !qa_native_q3_remote_client_current(service)) return native_client_fail(error, QA_ERROR_ARGUMENT, "Remote SystemInfo lost its reached gamestate");
    native_q3_system_info_policy policy={
        .skip_time_controls=service->services.basis.client.client_time_cvars!=NULL,
        .skip_same_timescale=true,.after_set=current,.context=service,
        .memory_error="Retaining reached remote SystemInfo"};
    return native_q3_system_info_apply(service->services.basis.client.cvars,&service->system_info,
        qa_q3_configstring(state,1),policy,error);
}
bool qa_native_q3_remote_client_system_info(qa_native_q3_remote_client_service *service, qa_error *error)
{
    if (!service || service->updating || !qa_native_q3_remote_client_current(service))
        return native_client_fail(error, QA_ERROR_ARGUMENT, "Remote SystemInfo stage lost its physical CLIENT");
    service->updating = true; bool ok = system_info(service, error); service->updating = false; return ok;
}
bool qa_native_q3_remote_client_prepare(qa_native_q3_remote_client_service *service, qa_error *error)
{
    if (!service || service->registered || service->updating || !qa_native_q3_remote_client_current(service))
        return native_client_fail(error, QA_ERROR_ARGUMENT, "Remote preparation requires its actual pre-Init CLIENT");
    service->updating = true;
    bool ok = system_info(service, error) && qa_cvars_set(service->services.basis.client.cvars, "sv_running", "0", true, error) &&
        qa_native_q3_remote_client_current(service);
    service->updating = false; return ok;
}
