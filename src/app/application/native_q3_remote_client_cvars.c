#include "native_q3_remote_client.h"
#include "native_q3_client_settings.h"
#include <stdlib.h>
#include <string.h>

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
        .product = service->services.basis.product, .cache = service->cache, .count = native_client_definition_count,
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
static bool same_name(const char *a, const char *b)
{
    while (*a && *b) { unsigned x = (unsigned char)*a++, y = (unsigned char)*b++;
        if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
        if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
        if (x != y) return false; }
    return !*a && !*b;
}
static bool system_info(qa_native_q3_remote_client_service *service, qa_error *error)
{
    const qa_q3_gamestate *state = service->services.network.gamestate(service->services.network.context);
    if (!state || !qa_native_q3_remote_client_current(service)) return native_client_fail(error, QA_ERROR_ARGUMENT, "Remote SystemInfo lost its reached gamestate");
    const char *text = qa_q3_configstring(state, 1); size_t length = strlen(text);
    char *retained = malloc(length + 1), *working = malloc(length + 1);
    if (!retained || !working) { free(retained); free(working); return native_client_fail(error, QA_ERROR_MEMORY, "Retaining reached remote SystemInfo"); }
    memcpy(retained, text, length + 1); memcpy(working, text, length + 1);
    bool same = service->system_info && !strcmp(retained, service->system_info), ok = true;
    char *cursor = working; if (*cursor == '\\') ++cursor;
    while (ok && *cursor) {
        char *name = cursor, *separator = strchr(cursor, '\\'); if (!separator) break;
        *separator = 0; char *value = separator + 1; separator = strchr(value, '\\');
        if (separator) { *separator = 0; cursor = separator + 1; } else cursor = value + strlen(value);
        bool frame_owner = service->services.basis.client.client_time_cvars != NULL &&
            (same_name(name, "timescale") || same_name(name, "fixedtime") || same_name(name, "com_cameraMode"));
        if (*name && !same_name(name, "cl_allowdownload") && !frame_owner && !(same && same_name(name, "timescale")))
            ok = qa_cvars_set(service->services.basis.client.cvars, name, value, true, error) && qa_native_q3_remote_client_current(service);
    }
    free(working);
    if (ok) { free(service->system_info); service->system_info = retained; } else free(retained);
    return ok;
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
