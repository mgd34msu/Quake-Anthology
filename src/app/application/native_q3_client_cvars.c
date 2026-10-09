#include "native_q3_client.h"
#include "native_q3_client_settings.h"
#include "qa/game_q3_configstrings.h"
#include "qa/application_native_q3_cvars.h"
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <stdio.h>

#define A QA_CVAR_ARCHIVE
#define C QA_CVAR_CHEAT
#define R QA_CVAR_READONLY
#define U QA_CVAR_USERINFO
#define S QA_CVAR_SERVERINFO
#define CV(symbol,name,value,flags) {QA_NATIVE_Q3_CVAR_##symbol,#symbol,name,value,flags,false},
#define MP(symbol,name,value,flags) {QA_NATIVE_Q3_CVAR_##symbol,#symbol,name,value,flags,true},
const native_client_definition native_client_definitions[] = {
#include "qa/native_q3_client_cvars.def"
};
const size_t native_client_definition_count = sizeof(native_client_definitions)/sizeof(*native_client_definitions);
_Static_assert(sizeof(native_client_definitions)/sizeof(*native_client_definitions)==QA_NATIVE_CLIENT_CVARS,
    "CGAME continuation schema must cover the exact registration table");
#undef CV
#undef MP
#undef A
#undef C
#undef R
#undef U
#undef S

static const struct { const char *name, *value; uint32_t flags; } userinfo_definitions[] = {
    {"cl_timeNudge", "0", QA_CVAR_TEMPORARY},
    {"rate", "25000", QA_CVAR_ARCHIVE | QA_CVAR_USERINFO},
    {"cl_maxpackets", "30", QA_CVAR_ARCHIVE},
    {"cl_packetdup", "1", QA_CVAR_ARCHIVE},
    {"snaps", "20", QA_CVAR_ARCHIVE | QA_CVAR_USERINFO},
    {"color1", "4", QA_CVAR_ARCHIVE | QA_CVAR_USERINFO},
    {"color2", "5", QA_CVAR_ARCHIVE | QA_CVAR_USERINFO},
    {"sex", "male", QA_CVAR_ARCHIVE | QA_CVAR_USERINFO},
    {"cl_anonymous", "0", QA_CVAR_ARCHIVE | QA_CVAR_USERINFO},
    {"cg_predictItems", "1", QA_CVAR_ARCHIVE | QA_CVAR_USERINFO},
    {"teamtask", "0", QA_CVAR_USERINFO}, {"password", "", QA_CVAR_USERINFO},
    {"handicap", "100", QA_CVAR_ARCHIVE | QA_CVAR_USERINFO},
    {"cl_maxPing", "800", QA_CVAR_ARCHIVE},
    {"cl_serverStatusResendTime", "750", 0}, {"sv_master1", "master.quake3arena.com", 0}
};

size_t qa_native_q3_cvar_definition_count(qa_q3_product product)
{
    if(product!=QA_Q3_ARENA && product!=QA_Q3_TEAM_ARENA)return 0;
    size_t count=0;
    for(size_t i=0;i<native_client_definition_count;++i)
        if(!native_client_definitions[i].missionpack || product==QA_Q3_TEAM_ARENA)++count;
    return count;
}
bool qa_native_q3_cvar_definition_at(qa_q3_product product,size_t ordinal,qa_native_q3_cvar_definition *out)
{
    if(!out || (product!=QA_Q3_ARENA && product!=QA_Q3_TEAM_ARENA))return false;
    for(size_t i=0;i<native_client_definition_count;++i) {
        const native_client_definition *definition=&native_client_definitions[i];
        if(definition->missionpack && product!=QA_Q3_TEAM_ARENA)continue;
        if(ordinal--)continue;
        *out=(qa_native_q3_cvar_definition){definition->symbol,definition->name,
            product==QA_Q3_TEAM_ARENA && !strcmp(definition->symbol,"cg_deferPlayers")?"0":definition->value,
            definition->flags,definition->id}; return true;
    }
    return false;
}

bool qa_native_q3_client_defaults(const qa_launch_instance *descriptor, qa_cvars *registry,
    const qa_command_context *command, const char *configured_model, qa_error *error)
{
    qa_catalog *catalog = descriptor ? qa_launch_instance_catalog(descriptor) : NULL;
    const qa_product *product = catalog ? qa_catalog_product(catalog, descriptor->selection.product) : NULL;
    if (!descriptor || !descriptor->storage || !descriptor->content || !product ||
        product->family != QA_GAME_Q3 || !registry || qa_cvars_dialect(registry) != QA_RULESET_Q3 ||
        !command || !command->owner || command->origin != QA_COMMAND_SEAT ||
        command->dialect != QA_RULESET_Q3)
        return native_client_fail(error, QA_ERROR_ARGUMENT, "Q3 defaults require their actual catalog CLIENT and seat registry");
    qa_native_q3_character_declaration defaults;
    if (!configured_model) {
        if (!qa_native_q3_character_default_declaration(QA_GAME_Q3, &defaults, error)) return false;
        configured_model = defaults.model;
    }
    if (!*configured_model)
        return native_client_fail(error, QA_ERROR_ARGUMENT, "Q3 CLIENT model declaration is empty");
    size_t length = strlen(configured_model);
    if (length > SIZE_MAX - 9)
        return native_client_fail(error, QA_ERROR_MEMORY, "Q3 CLIENT model declaration exceeds storage");
    char *model = malloc(length + 9);
    if (!model) return native_client_fail(error, QA_ERROR_MEMORY, "Retaining actual Q3 CLIENT model declaration");
    memcpy(model, configured_model, length); memcpy(model + length, "/default", 9);
    const char *description = "Q3 CLIENT baseline";
    bool ok = true;
    for (size_t i = 0; ok && i < 5; ++i)
        ok = qa_cvars_register(registry, userinfo_definitions[i].name,
            userinfo_definitions[i].value, userinfo_definitions[i].flags,
            command->owner, description, error);
    char name[64];
    if (!command->seat) memcpy(name, "Player", 7);
    else snprintf(name, sizeof(name), "Player %" PRIu64, (uint64_t)command->seat + 1);
    const uint32_t identity = QA_CVAR_ARCHIVE | QA_CVAR_USERINFO;
    if (ok) ok = qa_cvars_register(registry, "name", name, identity, command->owner, description, error);
    static const char *const names[] = {"model", "headmodel", "team_model", "team_headmodel"};
    for (size_t i = 0; ok && i < sizeof(names) / sizeof(*names); ++i)
        ok = qa_cvars_register(registry, names[i], model, identity, command->owner, description, error);
    for (size_t i = 5; ok && i < sizeof(userinfo_definitions) / sizeof(*userinfo_definitions); ++i)
        ok = qa_cvars_register(registry, userinfo_definitions[i].name,
            userinfo_definitions[i].value, userinfo_definitions[i].flags,
            command->owner, description, error);
    free(model); return ok;
}

qa_native_q3_cvar_id qa_native_q3_cvar_id_for_symbol(const char *symbol)
{
    if (!symbol) return QA_NATIVE_Q3_CVAR_COUNT;
    for (size_t i=0;i<native_client_definition_count;++i)
        if (!strcmp(symbol,native_client_definitions[i].symbol)) return native_client_definitions[i].id;
    return QA_NATIVE_Q3_CVAR_COUNT;
}
void native_client_cache_bind(const native_client_cache_access *access,bool dense)
{
    size_t ordinal=0;
    for (size_t i=0;i<native_client_definition_count;++i) {
        const native_client_definition *definition=&native_client_definitions[i];
        access->refs->ordinals[i]=UINT8_MAX;
        access->refs->rows[i]=(qa_cvar_handle){0};
        if (definition->missionpack && access->product!=QA_Q3_TEAM_ARENA) continue;
        access->refs->ordinals[i]=(uint8_t)(dense?ordinal++:i);
        access->refs->rows[i]=qa_cvars_resolve(access->registry,definition->name);
    }
    access->refs->model=qa_cvars_resolve(access->registry,"model");
    access->refs->head_model=qa_cvars_resolve(access->registry,"headmodel");
}
bool native_client_cache_copy_row(const native_client_cache_access *access,qa_native_q3_cvar_id id,
    bool force,qa_error *error)
{
    if (force) access->refs->rows[id]=qa_cvars_resolve(access->registry,native_client_definitions[id].name);
    const qa_cvar_view *value=qa_cvars_read(access->registry,access->refs->rows[id]);
    if (!value) return access->missing_error ? native_client_fail(error,QA_ERROR_NOT_FOUND,access->missing_error) : !force;
    return application_q3_client_cache_copy(&access->cache[access->refs->ordinals[id]],value,force,
        access->oversized_error,error);
}
bool native_client_cache_refresh(const native_client_cache_access *access,qa_error *error)
{
    for (size_t i=0;i<native_client_definition_count;++i)
        if (access->refs->ordinals[i]!=UINT8_MAX &&
            !native_client_cache_copy_row(access,(qa_native_q3_cvar_id)i,false,error)) return false;
    return true;
}
bool native_client_cache_read(const qa_native_q3_cvar_refs *refs,const qa_native_q3_client_cvar *cache,
    size_t count,qa_native_q3_cvar_id id,qa_native_q3_client_cvar *out,qa_error *error)
{
    if ((unsigned)id>=QA_NATIVE_Q3_CVAR_COUNT || refs->ordinals[id]>=count)
        return native_client_fail(error,QA_ERROR_NOT_FOUND,"Native CGAME cvar symbol is absent for this product");
    *out=cache[refs->ordinals[id]]; return true;
}
const qa_native_q3_cvar_refs *qa_native_q3_client_cvar_refs(const qa_native_q3_client_service *service)
{ return service ? &service->cvar_refs : NULL; }
bool qa_native_q3_client_cvar_read(const qa_native_q3_client_service *service,qa_native_q3_cvar_id id,
    qa_native_q3_client_cvar *out,qa_error *error)
{
    if (!service || !out || !qa_native_q3_client_service_current(service))
        return native_client_fail(error,QA_ERROR_ARGUMENT,"Native CGAME cvar cache lost its actual seat owner");
    if (!native_client_cache_read(&service->cvar_refs,service->cache,service->count,id,out,error)) return false;
    if (id==QA_NATIVE_Q3_CVAR_cg_drawStatus && service->services.status_visible &&
        !service->services.status_visible(service->services.context)) {
        strcpy(out->value,"0"); out->number=0; out->integer=0;
    }
    return true;
}
bool qa_native_q3_client_cvar_number(qa_native_q3_client_service *service,qa_native_q3_cvar_id id,
    float number,qa_error *error)
{
    qa_native_q3_client_cvar value;
    if (!qa_native_q3_client_cvar_read(service,id,&value,error)) return false;
    service->cache[service->cvar_refs.ordinals[id]].number=number; return true;
}
bool qa_native_q3_client_cvar_integer(qa_native_q3_client_service *service,qa_native_q3_cvar_id id,
    int32_t integer,qa_error *error)
{
    qa_native_q3_client_cvar value;
    if (!qa_native_q3_client_cvar_read(service,id,&value,error)) return false;
    service->cache[service->cvar_refs.ordinals[id]].integer=integer; return true;
}

bool native_client_cache_register(const native_client_cache_access *access,
    const char *description, int32_t *local_server, uint64_t *force_count, qa_error *error)
{
    native_client_cache_bind(access,false);
    for (size_t i = 0; i < access->count; ++i) {
        const native_client_definition *definition = &native_client_definitions[i];
        if (definition->missionpack && access->product != QA_Q3_TEAM_ARENA) continue;
        const char *reset = definition->value;
        if (!strcmp(definition->symbol, "cg_deferPlayers") && access->product == QA_Q3_TEAM_ARENA)
            reset = "0";
        if (!qa_cvars_register(access->registry, definition->name, reset, definition->flags,
            access->owner, description, error) || !access->current(access->context)) return false;
        if (!native_client_cache_copy_row(access,definition->id,true,error)) return false;
    }
    const qa_cvar_view *running = qa_cvars_find(access->registry, "sv_running");
    *local_server = running ? running->integer : 0;
    *force_count = access->cache[access->refs->ordinals[QA_NATIVE_Q3_CVAR_cg_forceModel]].modification_count;
    /* Actual selected CHARACTER values survive these SDK reset declarations. */
    const char *team_model = access->product == QA_Q3_TEAM_ARENA ? "james" : "sarge";
    const char *team_head = access->product == QA_Q3_TEAM_ARENA ? "*james" : "sarge";
    bool ok=qa_cvars_register(access->registry, "model", "sarge", QA_CVAR_USERINFO | QA_CVAR_ARCHIVE,
        access->owner, "Q3 body model", error) &&
        qa_cvars_register(access->registry, "headmodel", "sarge", QA_CVAR_USERINFO | QA_CVAR_ARCHIVE,
        access->owner, "Q3 head model", error) &&
        qa_cvars_register(access->registry, "team_model", team_model, QA_CVAR_USERINFO | QA_CVAR_ARCHIVE,
        access->owner, "Q3 team model", error) &&
        qa_cvars_register(access->registry, "team_headmodel", team_head, QA_CVAR_USERINFO | QA_CVAR_ARCHIVE,
        access->owner, "Q3 team head", error) && access->current(access->context);
    if (ok) {
        access->refs->model=qa_cvars_resolve(access->registry,"model");
        access->refs->head_model=qa_cvars_resolve(access->registry,"headmodel");
    }
    return ok;
}

bool native_client_cache_userinfo(const native_client_cache_access *access,
    const qa_native_q3_character_selection *character, const char *name,
    const native_client_userinfo_text *description, qa_error *error)
{
    /* Preserve the actual initializeQ3ClientCvars interleaved declaration order. */
    for (size_t i = 0; i < 5; ++i)
        if (!qa_cvars_register(access->registry, userinfo_definitions[i].name,
            userinfo_definitions[i].value, userinfo_definitions[i].flags, access->owner,
            description->defaults, error) || !access->current(access->context)) return false;
    if (!qa_cvars_register(access->registry, "name", name, QA_CVAR_ARCHIVE | QA_CVAR_USERINFO,
        access->owner, description->identity, error) || !access->current(access->context)) return false;
    const char *names[] = {"model", "headmodel", "team_model", "team_headmodel"};
    for (size_t i = 0; i < 4; ++i) {
        const char *model = i % 2 && *character->head_model ? character->head_model : character->model;
        const char *skin = i % 2 ? character->head_skin : character->skin;
        size_t a = strlen(model), b = strlen(skin);
        if (a > SIZE_MAX - b - 2)
            return native_client_fail(error, QA_ERROR_MEMORY, description->capacity_error);
        char *value = malloc(a + b + 2);
        if (!value) return native_client_fail(error, QA_ERROR_MEMORY, description->memory_error);
        memcpy(value, model, a); value[a] = '/'; memcpy(value + a + 1, skin, b + 1);
        bool ok = qa_cvars_register(access->registry, names[i], value,
            QA_CVAR_ARCHIVE | QA_CVAR_USERINFO, access->owner, description->character, error);
        free(value);
        if (!ok || !access->current(access->context)) return false;
    }
    for (size_t i = 5; i < sizeof(userinfo_definitions) / sizeof(*userinfo_definitions); ++i)
        if (!qa_cvars_register(access->registry, userinfo_definitions[i].name,
            userinfo_definitions[i].value, userinfo_definitions[i].flags, access->owner,
            description->defaults, error) || !access->current(access->context)) return false;
    return true;
}

bool native_client_cache_reload(const native_client_cache_access *access, qa_error *error)
{
    for (uint32_t slot = 0; slot < 64; ++slot) {
        const char *text;
        if (!access->configstring(access->context, 544 + slot, &text, error)) return false;
        size_t length = strlen(text);
        if (!length) continue;
        char *retained = malloc(length + 1);
        if (!retained) return native_client_fail(error, QA_ERROR_MEMORY, access->reload_memory_error);
        memcpy(retained, text, length + 1);
        bool ok = access->reload_client_info(access->context, slot, retained, error) &&
            access->current(access->context);
        free(retained);
        if (!ok) return false;
    }
    return true;
}

bool native_client_cache_update(const native_client_cache_access *access,
    bool *overlay_initial, uint64_t *overlay_count, uint64_t *force_count, qa_error *error)
{
    if (!native_client_cache_refresh(access,error)) return false;
    qa_native_q3_client_cvar *overlay = &access->cache[access->refs->ordinals[QA_NATIVE_Q3_CVAR_cg_drawTeamOverlay]];
    if (*overlay_initial || *overlay_count != overlay->modification_count) {
        *overlay_initial = false; *overlay_count = overlay->modification_count;
        if (!qa_cvars_set(access->registry, "teamoverlay", overlay->integer > 0 ? "1" : "0", true, error) ||
            !access->current(access->context) ||
            !qa_cvars_set(access->registry, "teamoverlay", "1", true, error) ||
            !access->current(access->context)) return false;
    }
    qa_native_q3_client_cvar *force = &access->cache[access->refs->ordinals[QA_NATIVE_Q3_CVAR_cg_forceModel]];
    if (*force_count != force->modification_count) {
        *force_count = force->modification_count;
        return native_client_cache_reload(access, error);
    }
    return true;
}

static bool current(void *context)
{ return qa_native_q3_client_service_current(context); }
static bool configstring(void *context, uint32_t index, const char **text, qa_error *error)
{
    qa_native_q3_client_service *service = context;
    uint64_t revision;
    return qa_native_q3_wire_reader_configstring(service->services.wire_reader, index, text, &revision, error);
}
static bool reload_client_info(void *context, uint32_t slot, const char *text, qa_error *error)
{
    qa_native_q3_client_service *service = context;
    return service->services.reload_client_info(service->services.context, slot, text, error);
}
static native_client_cache_access cache_access(qa_native_q3_client_service *service)
{
    return (native_client_cache_access){.context = service, .current = current,
        .configstring = configstring, .reload_client_info = reload_client_info,
        .registry = service->services.client.cvars, .owner = service->services.client.service_owner,
        .product = service->product, .cache = service->cache, .count = service->count, .refs=&service->cvar_refs,
        .oversized_error = "Cvar_Update source exceeds MAX_CVAR_VALUE_STRING",
        .reload_memory_error = "Retaining reached native client-info value"};
}

bool qa_native_q3_client_register(qa_native_q3_client_service *service, qa_error *error)
{
    if (!service || service->updating || !qa_native_q3_client_service_current(service))
        return native_client_fail(error, QA_ERROR_ARGUMENT, "Native CGAME registration requires its live constructor");
    native_client_cache_access access = cache_access(service);
    service->updating = true;
    bool ok = native_client_cache_register(&access, "Native Q3 CGAME", &service->local_server,
        &service->force_model_count, error);
    if (ok) service->registered = true;
    service->updating = false; return ok;
}
bool qa_native_q3_client_userinfo_initialize(qa_native_q3_client_service *service,
    const char *name, qa_error *error)
{
    if (!name || !service || service->updating || service->registered || !qa_native_q3_client_service_current(service))
        return native_client_fail(error, QA_ERROR_ARGUMENT, "Native Q3 userinfo requires its actual pre-registration constructor");
    native_client_cache_access access = cache_access(service);
    const native_client_userinfo_text description = {"Native Q3 seat userinfo", "Native Q3 seat identity",
        "Selected CHARACTER declaration", "CHARACTER declaration exceeds userinfo capacity",
        "Formatting actual CHARACTER userinfo declaration"};
    service->updating = true;
    bool ok = native_client_cache_userinfo(&access, &service->character, name, &description, error);
    service->updating = false; return ok;
}
bool qa_native_q3_client_force_model_change(qa_native_q3_client_service *service, qa_error *error)
{
    if (!service || !service->registered || service->updating || !qa_native_q3_client_service_current(service))
        return native_client_fail(error, QA_ERROR_ARGUMENT, "Native CGAME force-model update lacks its idle registered owner");
    native_client_cache_access access = cache_access(service);
    service->updating = true;
    bool ok = native_client_cache_reload(&access, error);
    service->updating = false; return ok;
}
bool qa_native_q3_client_update(qa_native_q3_client_service *service, qa_error *error)
{
    if (!service || !service->registered || service->updating || !qa_native_q3_client_service_current(service))
        return native_client_fail(error, QA_ERROR_ARGUMENT, "Native CGAME update lacks its idle registered owner");
    native_client_cache_access access = cache_access(service);
    service->updating = true;
    bool ok = native_client_cache_update(&access, &service->overlay_initial,
        &service->overlay_count, &service->force_model_count, error);
    service->updating = false; return ok;
}
