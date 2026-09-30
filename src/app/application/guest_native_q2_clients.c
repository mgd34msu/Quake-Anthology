#include "guest_native_q2_private.h"

static struct application_native_q2 *client_owner(application_provider *provider,
    uint32_t slot, bool connected, qa_error *error)
{
    struct application_native_q2 *engine = provider ? provider->state.native.q2_engine : NULL;
    if (!engine || !engine->initialized || engine->profile == QA_NATIVE_Q2_CGAME_API2023 ||
        !slot || slot >= 257 || engine->calls || !application_native_q2_idle(provider) ||
        (connected && !engine->clients[slot].connected)) {
        application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 client requires its idle admitted source slot");
        return NULL;
    }
    return engine;
}

bool application_native_q2_client_admit(application_provider *provider, uint32_t slot,
    qa_actor_id actor, const char *userinfo, const char *social_id, bool bot,
    bool *accepted, qa_error *error)
{
    struct application_native_q2 *engine = client_owner(provider, slot, false, error);
    if (!engine || !accepted || !userinfo) return false;
    *accepted = false;
    const qa_cvar_view *maximum = qa_cvars_find(engine->cvars, "maxclients");
    if (!maximum || maximum->integer < 1 || maximum->integer > 256 || slot > (uint32_t)maximum->integer)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 client exceeds the source maximum clients");
    if (!qa_actors_get(qa_session_actors(provider->application->session), actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 projected client generation is not live");
    application_native_q2_client *client = &engine->clients[slot];
    if (client->connected || (client->actor.registry && !qa_actor_id_equal(client->actor, actor)))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 source client slot is occupied");
    client->actor = actor; client->bot = bot; client->disconnect_started = false;
    qa_native_host_client_request request = {.slot = slot, .userinfo = userinfo,
        .social_id = social_id ? social_id : "", .bot = bot};
    ++engine->calls;
    bool ok = qa_native_host_client_connect(provider->state.native.host, &request, accepted, error);
    --engine->calls;
    if (ok && *accepted) client->connected = true;
    else {
        qa_error cleanup = {0};
        bool detached = qa_native_host_detach_actor(provider->state.native.host, slot, actor, &cleanup);
        if (!detached && ok) { ok = false; if (error) *error = cleanup; }
        if (detached) client->actor = (qa_actor_id){0};
    }
    return ok;
}

bool application_native_q2_client_begin(application_provider *provider, uint32_t slot, qa_error *error)
{
    struct application_native_q2 *engine = client_owner(provider, slot, true, error);
    if (!engine) return false;
    if (engine->clients[slot].begun) return true;
    ++engine->calls;
    bool ok = qa_native_host_client_begin(provider->state.native.host, slot, error);
    --engine->calls;
    if (ok) engine->clients[slot].begun = true;
    return ok;
}

bool application_native_q2_client_userinfo(application_provider *provider, uint32_t slot,
    const char *userinfo, qa_error *error)
{
    struct application_native_q2 *engine = client_owner(provider, slot, true, error);
    if (!engine || !userinfo) return false;
    ++engine->calls;
    bool ok = qa_native_host_client_userinfo(provider->state.native.host, slot, userinfo, error);
    --engine->calls;
    return ok;
}

bool application_native_q2_client_disconnect(application_provider *provider, uint32_t slot, qa_error *error)
{
    struct application_native_q2 *engine = client_owner(provider, slot, false, error);
    if (!engine) return false;
    application_native_q2_client *client = &engine->clients[slot];
    if (!client->actor.registry) return true;
    qa_actor_id actor = client->actor;
    bool ok = true; qa_error first = {0};
    if (client->connected && !client->disconnect_started) {
        client->disconnect_started = true; client->connected = client->begun = false;
        ++engine->calls;
        ok = qa_native_host_client_disconnect(provider->state.native.host, slot, &first);
        --engine->calls;
    }
    qa_error current = {0};
    if (!qa_native_host_detach_actor(provider->state.native.host, slot, actor, &current)) {
        if (error) *error = ok ? current : first;
        return false;
    }
    client->actor = (qa_actor_id){0}; client->bot = false;
    if (!ok && error) *error = first;
    return ok;
}

bool application_native_q2_client_think(application_provider *provider, uint32_t slot,
    qa_bytes command, qa_error *error)
{
    struct application_native_q2 *engine = client_owner(provider, slot, true, error);
    if (!engine || !engine->clients[slot].begun) return false;
    engine->current_client = slot;
    ++engine->calls;
    bool ok = qa_native_host_client_think(provider->state.native.host, slot, command, error);
    --engine->calls; engine->current_client = 0;
    return ok;
}

bool application_native_q2_console_command(application_provider *provider, qa_actor_id actor,
    const char *text, bool *handled, qa_error *error)
{
    struct application_native_q2 *engine = provider ? provider->state.native.q2_engine : NULL;
    if (!engine || !engine->initialized || !handled || !text || engine->calls ||
        engine->profile == QA_NATIVE_Q2_CGAME_API2023)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 console export requires an idle initialized game");
    *handled = false;
    uint32_t slot = 0;
    if (actor.registry) {
        if (!qa_actors_get(qa_session_actors(provider->application->session), actor))
            return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 command actor generation retired");
        for (uint32_t i = 1; i < 257; ++i)
            if (engine->clients[i].connected && engine->clients[i].begun &&
                qa_actor_id_equal(engine->clients[i].actor, actor)) { slot = i; break; }
        if (!slot) return true;
    }
    qa_command_tokens tokens = {0};
    if (!qa_command_tokenize(text, engine->command_context.dialect, false, &tokens, error)) return false;
    qa_command_tokens prior = engine->arguments; engine->arguments = tokens;
    ++engine->calls;
    bool ok = slot ? qa_native_host_client_command(provider->state.native.host, slot, error) :
        qa_native_host_server_command(provider->state.native.host, error);
    --engine->calls;
    qa_command_tokens_free(&engine->arguments); engine->arguments = prior;
    if (ok) *handled = true;
    return ok;
}
