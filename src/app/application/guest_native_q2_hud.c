#include "guest_native_q2_private.h"
#include <limits.h>

struct application_native_q2 *application_native_q2_hud_source(
    struct application_native_q2 *engine, uint32_t *slot, qa_error *error)
{
    uint32_t seat = UINT32_MAX;
    for (size_t i = 1; i < 257; ++i)
        if (engine->clients[i].reserved) {
            if (seat != UINT32_MAX) {
                application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 cgame requires one admitted seat");
                return NULL;
            }
            seat = engine->clients[i].seat;
        }
    qa_application *app = engine->provider->application;
    qa_actor_id actor;
    if (seat == UINT32_MAX || !qa_application_player_actor(app, seat, &actor)) {
        application_fail(error, QA_ERROR_NOT_FOUND, "Native Q2 HUD has no live local player");
        return NULL;
    }
    application_provider *provider = application_provider_for(app, actor, QA_ROLE_CHARACTER, NULL);
    struct application_native_q2 *source = provider && provider->kind == APPLICATION_PROVIDER_NATIVE
        ? provider->state.native.q2_engine : NULL;
    if (!source || source->profile != QA_NATIVE_Q2_GAME_API2023 || !source->map_ready ||
        !source->initialized || source->shutting_down) {
        application_fail(error, QA_ERROR_UNSUPPORTED, "External Q2 cgame requires an admitted KEX public player-state producer");
        return NULL;
    }
    for (uint32_t i = 1; i < 257; ++i)
        if (source->clients[i].connected && source->clients[i].begun &&
            qa_actor_id_equal(source->clients[i].actor, actor)) {
            *slot = i; return source;
        }
    application_fail(error, QA_ERROR_NOT_FOUND, "Native Q2 HUD source client projection is unavailable");
    return NULL;
}

bool application_native_q2_import(void *opaque, const qa_native_host_q2_application_call *call,
    qa_native_value *result, qa_error *error)
{
    struct application_native_q2 *engine = opaque;
    const char *name = call->import->name;
    if (!strcmp(name, "CL_GetClientName") || !strcmp(name, "CL_GetClientPic") ||
        !strcmp(name, "CL_GetClientDogtag")) {
        uint32_t slot;
        struct application_native_q2 *source = application_native_q2_hud_source(engine, &slot, error);
        if (!source) return false;
        int32_t index = call->import->arguments[0].as.i32;
        if (index < 0 || index >= 256)
            return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 HUD client index is invalid");
        const char *skin = source->configstrings[11582 + index];
        if (!skin) skin = "";
        const char *first = strchr(skin, '\\');
        char text[1024] = {0};
        if (!strcmp(name, "CL_GetClientName")) {
            size_t bytes = first ? (size_t)(first - skin) : 0;
            if (bytes >= sizeof(text)) return application_fail(error, QA_ERROR_FORMAT, "Native Q2 client name is too long");
            memcpy(text, skin, bytes);
        } else if (*skin) {
            const char *model_skin = first ? first + 1 : skin;
            const char *last = strchr(model_skin, '\\');
            if (!strcmp(name, "CL_GetClientDogtag")) {
                const char *dogtag = last && last[1] ? last + 1 : "default";
                int bytes = snprintf(text, sizeof(text), "%s.pcx", dogtag);
                if (bytes < 0 || (size_t)bytes >= sizeof(text))
                    return application_fail(error, QA_ERROR_FORMAT, "Native Q2 client dogtag is too long");
            } else {
                size_t bytes = last ? (size_t)(last - model_skin) : strlen(model_skin);
                if (bytes && bytes < sizeof(text) - 17) {
                    memcpy(text, "/players/", 9); memcpy(text + 9, model_skin, bytes);
                    memcpy(text + 9 + bytes, "_i.pcx", 7);
                } else if (bytes) return application_fail(error, QA_ERROR_FORMAT, "Native Q2 client icon is too long");
            }
        }
        return qa_native_host_q2_retain_string(call->host, text, &result->as.address, error);
    }
    if (!strcmp(name, "CL_FrameValid") || !strcmp(name, "CL_ClientTime") ||
        !strcmp(name, "CL_ServerFrame") || !strcmp(name, "CL_ServerProtocol") ||
        !strcmp(name, "CL_GetWarnAmmoCount")) {
        uint32_t slot;
        struct application_native_q2 *source = application_native_q2_hud_source(engine, &slot, error);
        if (!source) return false;
        if (!strcmp(name, "CL_FrameValid")) result->as.u8 = source->map_ready && source->clients[slot].begun;
        else if (!strcmp(name, "CL_ClientTime")) result->as.u64 = source->frame.time_ns / UINT64_C(1000000);
        else if (!strcmp(name, "CL_ServerFrame")) result->as.i32 = (int32_t)(uint32_t)source->frame.number;
        else if (!strcmp(name, "CL_ServerProtocol")) result->as.i32 = 2023;
        else {
            int32_t weapon = call->import->arguments[0].as.i32;
            if (weapon < 0 || weapon >= 32)
                return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 weapon wheel index is invalid");
            /* CS_WHEEL_WEAPONS = CS_PLAYERSKINS + MAX_CLIENTS + MAX_GENERAL. */
            const char *text = source->configstrings[12350 + weapon];
            if (!text || !*text)
                return application_fail(error, QA_ERROR_NOT_FOUND, "Native Q2 ammo warning has no source wheel definition");
            for (size_t i = 0; i < 6; ++i) {
                text = strchr(text, '|');
                if (!text) return application_fail(error, QA_ERROR_FORMAT, "Native Q2 weapon wheel definition is incomplete");
                ++text;
            }
            char *end; long count = strtol(text, &end, 10);
            if (end == text || (*end && *end != '|') || count < INT32_MIN || count > INT32_MAX)
                return application_fail(error, QA_ERROR_FORMAT, "Native Q2 ammo warning quantity is invalid");
            result->as.i32 = (int32_t)count;
        }
        return true;
    }
    return engine->application
        ? engine->application(engine->application_context, call, result, error)
        : application_fail(error, QA_ERROR_UNSUPPORTED, "Native Q2 application import is unbound");
}

static bool parse_config(struct application_native_q2 *engine, int32_t index,
    const char *text, qa_error *error)
{
    qa_native_instance *instance = qa_native_host_instance(engine->provider->state.native.host);
    qa_native_address address = 0;
    size_t bytes = strlen(text) + 1;
    bool ok = qa_native_allocate(instance, bytes, INT32_MIN + 10, &address, error) &&
        qa_native_write(instance, address, (qa_bytes){(const uint8_t *)text, bytes}, error);
    if (ok) {
        qa_native_value args[] = {{.type = QA_NATIVE_I32, .as.i32 = index},
            {.type = QA_NATIVE_ADDRESS, .as.address = address}};
        ok = qa_native_call(instance, "ParseConfigString", args, 2, NULL, error);
    }
    if (address) {
        qa_error cleanup = {0};
        bool freed = qa_native_free(instance, address, &cleanup);
        if (!freed && ok) { ok = false; if (error) *error = cleanup; }
    }
    return ok;
}

bool application_native_q2_draw_hud(application_provider *provider, uint32_t seat,
    uint32_t milliseconds, qa_error *error)
{
    (void)milliseconds;
    struct application_native_q2 *engine = provider ? provider->state.native.q2_engine : NULL;
    if (!engine || engine->profile != QA_NATIVE_Q2_CGAME_API2023 || engine->shutting_down ||
        !application_native_q2_idle(provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 HUD requires its idle cgame owner");
    uint32_t slot;
    struct application_native_q2 *source = application_native_q2_hud_source(engine, &slot, error);
    if (!source) return false;
    bool admitted = false;
    for (size_t i = 1; i < 257; ++i)
        if (engine->clients[i].reserved && engine->clients[i].seat == seat) admitted = true;
    if (!admitted) return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 HUD cannot draw another seat");
    if (!application_native_q2_activate(engine, error)) return false;
    ++engine->calls; ++source->calls;
    bool ok = true;
    if (!engine->initialized) {
        ok = qa_native_host_initialize(provider->state.native.host, 0, 0, false, error);
        if (ok) engine->initialized = true;
        if (ok) ok = qa_native_call(qa_native_host_instance(provider->state.native.host),
            "TouchPics", NULL, 0, NULL, error);
    }
    bool changed = engine->hud_source_owner != source->provider->owner ||
        engine->hud_config_revision != source->config_revision;
    for (uint32_t i = 0; ok && changed && i < engine->configstring_count; ++i) {
        const char *text = source->configstrings[i] ? source->configstrings[i] : "";
        const char *prior = engine->configstrings[i] ? engine->configstrings[i] : "";
        if (!strcmp(text, prior)) continue;
        size_t bytes = strlen(text) + 1;
        char *copy = malloc(bytes);
        if (!copy) { ok = application_fail(error, QA_ERROR_MEMORY, "Retaining native Q2 cgame configstring"); break; }
        memcpy(copy, text, bytes);
        ok = parse_config(engine, (int32_t)i, text, error);
        if (ok) { free(engine->configstrings[i]); engine->configstrings[i] = copy; }
        else free(copy);
    }
    if (ok) {
        engine->hud_source_owner = source->provider->owner;
        engine->hud_config_revision = source->config_revision;
    }
    qa_buffer player = {0}; qa_native_host_q2_hud_view view;
    uint8_t data[1536] = {0};
    if (ok) ok = qa_native_host_q2_player_state(source->provider->state.native.host, slot, &player, error);
    if (ok && !engine->platform.hud_view)
        ok = application_fail(error, QA_ERROR_UNSUPPORTED, "Native Q2 HUD viewport owner is absent");
    if (ok) ok = engine->platform.hud_view(engine->platform.context, seat, &view, error);
    if (ok) {
        memcpy(data, source->clients[slot].layout, 1024);
        for (size_t i = 0; i < 256; ++i)
            qa_store_u16le(data + 1024 + i * 2, (uint16_t)source->clients[slot].inventory[i]);
        ok = qa_native_host_q2_draw_hud(provider->state.native.host, seat, &view,
            (int32_t)slot - 1, (qa_bytes){data, sizeof(data)},
            (qa_bytes){player.data, player.size}, error);
    }
    qa_buffer_free(&player);
    --source->calls; --engine->calls;
    if (ok) engine->map_ready = provider->map_bound = true;
    return ok;
}
