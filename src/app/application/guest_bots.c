#include "guest_projection_private.h"

bool application_guest_bot_allocate(application_provider *provider, int32_t *out,
                                     qa_error *error)
{
    struct application_q3_guest *engine = q3g_engine(provider);
    qa_q3_host_game_data data;
    if (!engine || !engine->game || !out ||
        !qa_q3_host_game_data_read(engine->game->host, &data))
        return application_fail(error, QA_ERROR_ARGUMENT, "Guest bot allocation requires located canonical source records");
    uint32_t count = data.client_count < 64 ? data.client_count : 64;
    const qa_cvar_view *maximum = qa_cvars_find(provider->application->cvars, "sv_maxclients");
    if (maximum) {
        if (maximum->integer < 1 || maximum->integer > 64)
            return application_fail(error, QA_ERROR_FORMAT, "Guest source maximum clients exceeds its admitted server ABI");
        if ((uint32_t)maximum->integer < count) count = (uint32_t)maximum->integer;
    }
    for (uint32_t slot = 0; slot < count && slot < data.entity_count; ++slot) {
        q3g_client *client = &engine->clients[slot];
        if (engine->seats[slot] != UINT32_MAX || client->allocated || client->pending_retirement ||
            client->actor.registry) continue;
        qa_actor_id actor;
        if (!qa_q3_host_actor(engine->game->host, slot, true, &actor, error)) return false;
        if (!actor.registry || !qa_actors_get(qa_session_actors(provider->application->session), actor))
            return application_fail(error, QA_ERROR_FORMAT, "Guest bot allocator did not admit a canonical actor");
        client->actor = actor;
        client->pending_bot = true;
        client->disconnect_started = false;
        client->entered_ns = qa_session_elapsed(provider->application->session);
        *out = (int32_t)slot; return true;
    }
    *out = -1; return true;
}

static bool bots_admit(application_provider *provider, qa_error *error)
{
    struct application_q3_guest *engine = q3g_engine(provider);
    if (!engine || !engine->game || !engine->game->initialized) return true;
    for (uint32_t slot = 0; slot < 64; ++slot) {
        q3g_client *client = &engine->clients[slot];
        if (!client->pending_bot || client->pending_retirement) continue;
        qa_actor_id actor;
        if (!qa_q3_host_actor(engine->game->host, slot, false, &actor, error) ||
            !qa_actor_id_equal(actor, client->actor) ||
            !qa_actors_get(qa_session_actors(provider->application->session), actor))
            return application_fail(error, QA_ERROR_ARGUMENT, "Pending guest bot actor generation retired");
        client->begun = true;
        if (!application_guest_actor_admit(provider, actor, error)) return false;
        if (!client->roster_attached) {
            qa_combat_state state;
            qa_q3_player player;
            char name[QA_Q3_BIG_INFO_CHARS], skin[QA_Q3_BIG_INFO_CHARS];
            if (!application_guest_player_state(provider, actor, &state, error) ||
                !qa_q3_host_source_player(engine->game->host, slot, &player, error) ||
                !qa_q3_info_value(client->userinfo ? client->userinfo : "", "name", name, sizeof(name), error) ||
                !qa_q3_info_value(client->userinfo ? client->userinfo : "", "model", skin, sizeof(skin), error)) return false;
            qa_builtin_player_info info = {.name = name, .skin = skin, .slot = slot,
                .ping = player.ping, .entered_ns = client->entered_ns, .view_height = (float)player.viewheight,
                .connected = true, .spectator = player.pmType == 2, .dead = state.health <= 0};
            if (!application_players_guest_attach(provider->application, provider, slot, actor,
                    &info, error)) return false;
            client->roster_attached = true;
        }
        if (client->pending_retirement ||
            !qa_actors_get(qa_session_actors(provider->application->session), actor)) continue;
        /* Source-internal ClientConnect and ClientBegin have completed before
         * this boundary; they do not call the application client wrappers. */
        client->allocated = client->connected = client->bot = true;
        client->pending_bot = false;
    }
    return true;
}

bool application_guest_bots_admit(application_provider *provider, qa_error *error)
{
    struct application_q3_guest *engine = q3g_engine(provider);
    if (!engine) return true;
    ++engine->calls;
    bool ok = bots_admit(provider, error);
    --engine->calls; return ok;
}

bool application_guest_bot_free(application_provider *provider, int32_t slot,
                                 qa_error *error)
{
    struct application_q3_guest *engine = q3g_engine(provider);
    if (!engine || !engine->game || slot < 0 || slot >= 64)
        return application_fail(error, QA_ERROR_ARGUMENT, "Guest bot release requires its canonical source slot");
    q3g_client *client = &engine->clients[slot];
    if (!client->bot && !client->pending_bot) return true;
    if (client->pending_retirement) return true;
    if (!qa_q3_host_retire_input(engine->game->host, (uint32_t)slot, true, error)) return false;
    client->connected = false;
    client->pending_bot = false;
    client->pending_retirement = true;
    /* FreeBotClient is the source's server release. ClientDisconnect is
     * already source-owned; invoking it here would repeat that lifecycle. */
    client->disconnect_pending = false;
    return true;
}
