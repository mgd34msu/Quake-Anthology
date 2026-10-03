#include "guest_projection_private.h"
#include "guest_input_private.h"
#include "client_events.h"

bool application_guest_actor_admit(application_provider *provider, qa_actor_id actor,
                                   qa_error *error)
{
    struct application_q3_guest *engine = q3g_engine(provider);
    if (!engine || !engine->game || !engine->game->host ||
        !qa_actors_get(qa_session_actors(provider->application->session), actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Guest admission requires a live canonical actor and game host");
    uint32_t slot;
    qa_q3_host_game_data data;
    if (!qa_q3_host_actor_slot(engine->game->host, actor, &slot, error)) return false;
    if (qa_q3_host_game_data_read(engine->game->host, &data) && slot < data.client_count &&
        !engine->clients[slot].begun) return true;
    return application_guest_projection_admit(engine->game, actor, error);
}

bool application_guest_client_drop(application_provider *provider, uint32_t slot,
                                   const char *reason, qa_error *error)
{
    struct application_q3_guest *engine = q3g_engine(provider);
    if (!engine || !engine->game || slot >= 64)
        return application_fail(error, QA_ERROR_ARGUMENT, "Guest drop requires a source client owner");
    q3g_client *client = &engine->clients[slot];
    if (client->pending_retirement) return true;
    if (!client->allocated && !client->actor.registry) return true;
    char *copy = q3g_copy_text(reason ? reason : "", error);
    if (!copy) return false;
    if (!qa_q3_host_retire_input(engine->game->host, slot, true, error)) { free(copy); return false; }
    free(client->retirement_reason); client->retirement_reason = copy;
    client->disconnect_pending = client->connected || client->pending_bot;
    client->connected = false;
    client->pending_retirement = true;
    client->pending_bot = false;
    return true;
}

bool application_guest_clients_drain(application_provider *provider, qa_error *error)
{
    struct application_q3_guest *engine = q3g_engine(provider);
    if (!engine || !engine->game || engine->draining_clients || engine->calls ||
        !qa_world_idle(engine->world)) return true;
    qa_source_frame frame;
    bool round_frame = engine->round.phase == Q3G_ROUND_SETTLING && engine->round.source_entry &&
        provider->application->operation == APPLICATION_ADVANCING &&
        qa_session_active_frame(provider->application->session, provider->owner, &frame) &&
        frame.kind == QA_CLOCK_Q3 && frame.phase == QA_FRAME_ENTRY &&
        frame.elapsed_ns == UINT64_C(100000000) && frame.number == engine->round.last_frame;
    if (engine->round.phase != Q3G_ROUND_NONE &&
        (engine->round.phase != Q3G_ROUND_SETTLING ||
         (provider->application->operation != APPLICATION_IDLE &&
          provider->application->operation != APPLICATION_CONFIGURING && !round_frame)))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 round client retirement requires its completed publication boundary");
    engine->draining_clients = true;
    ++engine->calls;
    bool ok = true;
    qa_error first = {0};
    for (uint32_t slot = 0; slot < 64; ++slot) {
        q3g_client *client = &engine->clients[slot];
        if (!client->pending_retirement) continue;
        qa_error current = {0};
        if (client->disconnect_pending && !client->disconnect_started) {
            client->disconnect_started = true;
            client->disconnect_pending = false;
            if (engine->game->initialized && !engine->game->retired) {
                int32_t argument = (int32_t)slot, result;
                bool called = engine->round.phase == Q3G_ROUND_SETTLING ?
                    q3g_round_call(engine->game, 5, &argument, 1, &result, &current) :
                    q3g_call(engine->game, 5, &argument, 1, &result, &current);
                if (!called && ok) {
                    ok = false; first = current;
                }
            }
        }
        if (!qa_q3_host_input_idle(engine->game->host, slot) || !qa_world_idle(engine->world) ||
            !application_guest_input_actor_idle(provider->application, client->actor)) continue;
        qa_actor_id actor = client->actor;
        if (client->roster_attached) {
            const qa_actor_record *retiring = qa_actors_get(
                qa_session_actors(provider->application->session), actor);
            if (retiring && retiring->owner == provider->owner &&
                !application_client_declared_disconnect(provider->application, actor, &current)) {
                if (ok) { ok = false; first = current; }
                continue;
            }
            if (!application_players_guest_detach(provider->application, provider, slot, actor, &current)) {
                if (ok) { ok = false; first = current; }
                continue;
            }
            client->roster_attached = false;
        }
        const qa_actor_record *record = qa_actors_get(qa_session_actors(provider->application->session), actor);
        if (record && record->owner != provider->owner) {
            if (!application_guest_projection_detach(engine->game, actor, &current) ||
                !qa_q3_host_detach_actor(engine->game->host, slot, actor, &current)) {
                if (ok) { ok = false; first = current; }
                continue;
            }
        } else if (record && !qa_session_release(provider->application->session, actor, &current)) {
            if (ok) { ok = false; first = current; }
            if (qa_actors_get(qa_session_actors(provider->application->session), actor)) continue;
        }
        if (!qa_q3_host_retire_input(engine->game->host, slot, false, &current)) {
            if (ok) { ok = false; first = current; }
            continue;
        }
        client->fire = (q3g_fire_continuation){0};
        client->actor = (qa_actor_id){0};
        client->allocated = client->connected = client->begun = client->bot = false;
        client->carry_pending = false;
        client->reserved = false;
        client->pending_retirement = client->disconnect_started = false;
        free(client->retirement_reason); client->retirement_reason = NULL;
    }
    --engine->calls;
    engine->draining_clients = false;
    if (!ok && engine->round.phase == Q3G_ROUND_SETTLING)
        return q3g_round_fail(engine, &first, error);
    if (!ok && error) *error = first;
    return ok;
}
