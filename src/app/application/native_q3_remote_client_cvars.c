#include "native_q3_remote_client.h"
#include <stdlib.h>
#include <string.h>

static bool enabled(const qa_native_q3_remote_client_service *service, size_t index)
{ return !native_client_definitions[index].missionpack || service->services.basis.product == QA_Q3_TEAM_ARENA; }
static bool copy(qa_native_q3_remote_client_service *service, size_t index, const qa_cvar_view *engine, bool force, qa_error *error)
{
    qa_native_q3_client_cvar *value = &service->cache[index];
    if (!force && value->modification_count == engine->modification_count) return true;
    value->modification_count = engine->modification_count;
    size_t length = strlen(engine->value);
    if (length >= sizeof(value->value)) return native_client_fail(error, QA_ERROR_FORMAT, "Remote Cvar_Update exceeds MAX_CVAR_VALUE_STRING");
    memcpy(value->value, engine->value, length + 1); value->number = engine->number; value->integer = engine->integer; return true;
}
static bool enter(qa_native_q3_remote_client_service *service, qa_error *error)
{
    if (!qa_native_q3_remote_client_current(service) || service->updating || service->cache_revision == UINT64_MAX)
        return native_client_fail(error, QA_ERROR_ARGUMENT, "Remote CGAME cache stage requires its idle actual service");
    service->updating = true; ++service->cache_revision; return true;
}
bool qa_native_q3_remote_client_register(qa_native_q3_remote_client_service *service, qa_error *error)
{
    if (!enter(service, error)) return false;
    bool ok = true; qa_cvars *registry = service->services.basis.client.cvars;
    uint64_t owner = service->services.basis.client.service_owner;
    for (size_t i = 0; ok && i < native_client_definition_count; ++i) if (enabled(service, i)) {
        const native_client_definition *definition = &native_client_definitions[i];
        const char *reset = !strcmp(definition->symbol, "cg_deferPlayers") && service->services.basis.product == QA_Q3_TEAM_ARENA ? "0" : definition->value;
        ok = qa_cvars_register(registry, definition->name, reset, definition->flags, owner, "Native remote Q3 CGAME", error) &&
            qa_native_q3_remote_client_current(service);
        const qa_cvar_view *value = ok ? qa_cvars_find(registry, definition->name) : NULL;
        if (ok) ok = value && copy(service, i, value, true, error);
    }
    const qa_cvar_view *running = ok ? qa_cvars_find(registry, "sv_running") : NULL;
    if (ok) {
        service->local_server = running ? running->integer : 0;
        service->force_model_count = service->cache[native_remote_client_symbol(service, "cg_forceModel")].modification_count;
        const char *team_model = service->services.basis.product == QA_Q3_TEAM_ARENA ? "james" : "sarge";
        const char *team_head = service->services.basis.product == QA_Q3_TEAM_ARENA ? "*james" : "sarge";
        ok = qa_cvars_register(registry, "model", "sarge", QA_CVAR_USERINFO | QA_CVAR_ARCHIVE, owner, "Q3 body model", error) &&
            qa_cvars_register(registry, "headmodel", "sarge", QA_CVAR_USERINFO | QA_CVAR_ARCHIVE, owner, "Q3 head model", error) &&
            qa_cvars_register(registry, "team_model", team_model, QA_CVAR_USERINFO | QA_CVAR_ARCHIVE, owner, "Q3 team model", error) &&
            qa_cvars_register(registry, "team_headmodel", team_head, QA_CVAR_USERINFO | QA_CVAR_ARCHIVE, owner, "Q3 team head", error) &&
            qa_native_q3_remote_client_current(service);
    }
    if (ok) service->registered = true;
    service->updating = false; return ok;
}
bool qa_native_q3_remote_client_userinfo_initialize(qa_native_q3_remote_client_service *service, const char *name, qa_error *error)
{
    if (!name || !service || service->registered || service->updating || !qa_native_q3_remote_client_current(service))
        return native_client_fail(error, QA_ERROR_ARGUMENT, "Remote userinfo requires its real pre-registration CLIENT");
    static const struct { const char *name, *value; uint32_t flags; } definitions[] = {
        {"cl_timeNudge","0",QA_CVAR_TEMPORARY}, {"rate","25000",QA_CVAR_ARCHIVE | QA_CVAR_USERINFO},
        {"cl_maxpackets","30",QA_CVAR_ARCHIVE}, {"cl_packetdup","1",QA_CVAR_ARCHIVE}, {"snaps","20",QA_CVAR_ARCHIVE | QA_CVAR_USERINFO},
        {"color1","4",QA_CVAR_ARCHIVE | QA_CVAR_USERINFO}, {"color2","5",QA_CVAR_ARCHIVE | QA_CVAR_USERINFO},
        {"sex","male",QA_CVAR_ARCHIVE | QA_CVAR_USERINFO}, {"cl_anonymous","0",QA_CVAR_ARCHIVE | QA_CVAR_USERINFO},
        {"cg_predictItems","1",QA_CVAR_ARCHIVE | QA_CVAR_USERINFO}, {"teamtask","0",QA_CVAR_USERINFO}, {"password","",QA_CVAR_USERINFO},
        {"handicap","100",QA_CVAR_ARCHIVE | QA_CVAR_USERINFO}, {"cl_maxPing","800",QA_CVAR_ARCHIVE},
        {"cl_serverStatusResendTime","750",0}, {"sv_master1","master.quake3arena.com",0}
    };
    service->updating = true; bool ok = true;
    qa_cvars *registry = service->services.basis.client.cvars; uint64_t owner = service->services.basis.client.service_owner;
    for (size_t i = 0; ok && i < 5; ++i) ok = qa_cvars_register(registry, definitions[i].name, definitions[i].value,
        definitions[i].flags, owner, "Native remote seat userinfo", error) && qa_native_q3_remote_client_current(service);
    if (ok) ok = qa_cvars_register(registry, "name", name, QA_CVAR_ARCHIVE | QA_CVAR_USERINFO, owner, "Native remote seat identity", error) &&
        qa_native_q3_remote_client_current(service);
    const char *names[] = {"model", "headmodel", "team_model", "team_headmodel"};
    for (size_t i = 0; ok && i < 4; ++i) {
        const char *model = i % 2 && *service->character.head_model ? service->character.head_model : service->character.model;
        const char *skin = i % 2 ? service->character.head_skin : service->character.skin;
        size_t a = strlen(model), b = strlen(skin);
        if (a > SIZE_MAX - b - 2) { ok = native_client_fail(error, QA_ERROR_MEMORY, "Remote CHARACTER declaration exceeds capacity"); break; }
        char *value = malloc(a + b + 2);
        if (!value) { ok = native_client_fail(error, QA_ERROR_MEMORY, "Formatting actual remote CHARACTER userinfo"); break; }
        memcpy(value, model, a); value[a] = '/'; memcpy(value + a + 1, skin, b + 1);
        ok = qa_cvars_register(registry, names[i], value, QA_CVAR_ARCHIVE | QA_CVAR_USERINFO, owner, "Selected remote CHARACTER", error) &&
            qa_native_q3_remote_client_current(service); free(value);
    }
    for (size_t i = 5; ok && i < sizeof(definitions) / sizeof(*definitions); ++i)
        ok = qa_cvars_register(registry, definitions[i].name, definitions[i].value, definitions[i].flags, owner,
            "Native remote seat userinfo", error) && qa_native_q3_remote_client_current(service);
    service->updating = false; return ok;
}
static bool reload(qa_native_q3_remote_client_service *service, qa_error *error)
{
    for (uint32_t slot = 0; slot < 64; ++slot) {
        const qa_q3_gamestate *state = service->services.network.gamestate(service->services.network.context);
        if (!state || !qa_native_q3_remote_client_current(service))
            return native_client_fail(error, QA_ERROR_ARGUMENT, "Remote force-model reload lost its reached gamestate");
        const char *text = qa_q3_configstring(state, 544 + slot); size_t length = strlen(text);
        if (!length) continue;
        char *retained = malloc(length + 1);
        if (!retained) return native_client_fail(error, QA_ERROR_MEMORY, "Retaining reached remote player configstring");
        memcpy(retained, text, length + 1);
        bool ok = service->services.reload_client_info(service->services.context, slot, retained, error) &&
            qa_native_q3_remote_client_current(service); free(retained); if (!ok) return false;
    }
    return true;
}
bool qa_native_q3_remote_client_force_model_change(qa_native_q3_remote_client_service *service, qa_error *error)
{
    if (!service || !service->registered || service->updating || !qa_native_q3_remote_client_current(service))
        return native_client_fail(error, QA_ERROR_ARGUMENT, "Remote force-model refresh lost its registered CLIENT");
    service->updating = true; bool ok = reload(service, error); service->updating = false; return ok;
}
bool qa_native_q3_remote_client_update(qa_native_q3_remote_client_service *service, qa_error *error)
{
    if (!service || !service->registered || !enter(service, error)) return false;
    bool ok = true;
    for (size_t i = 0; ok && i < native_client_definition_count; ++i) if (enabled(service, i)) {
        const qa_cvar_view *value = qa_cvars_find(service->services.basis.client.cvars, native_client_definitions[i].name);
        if (value) ok = copy(service, i, value, false, error);
    }
    qa_native_q3_client_cvar *overlay = &service->cache[native_remote_client_symbol(service, "cg_drawTeamOverlay")];
    if (ok && (service->overlay_initial || service->overlay_count != overlay->modification_count)) {
        service->overlay_initial = false; service->overlay_count = overlay->modification_count;
        ok = qa_cvars_set(service->services.basis.client.cvars, "teamoverlay", overlay->integer > 0 ? "1" : "0", true, error) &&
            qa_native_q3_remote_client_current(service) &&
            qa_cvars_set(service->services.basis.client.cvars, "teamoverlay", "1", true, error) && qa_native_q3_remote_client_current(service);
    }
    qa_native_q3_client_cvar *force = &service->cache[native_remote_client_symbol(service, "cg_forceModel")];
    if (ok && service->force_model_count != force->modification_count) {
        service->force_model_count = force->modification_count; ok = reload(service, error);
    }
    service->updating = false; return ok;
}
static bool same_name(const char *a, const char *b)
{
    while (*a && *b) { unsigned x = (unsigned char)*a++, y = (unsigned char)*b++;
        if (x >= 'A' && x <= 'Z') x += 'a' - 'A'; if (y >= 'A' && y <= 'Z') y += 'a' - 'A'; if (x != y) return false; }
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
