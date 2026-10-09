#include "native_q3_end_frame.h"
#include "native_q3_clients.h"
#include "native_q3_console.h"
#include "native_q3_settings.h"
#include "qa/game_q3_source.h"
#include "qa/game_q3_wire.h"

static bool end_frame(application_provider *provider,
    const qa_source_frame *frame, qa_error *error)
{
    qa_application *app = provider ? provider->application : NULL;
    qa_source_frame active;
    int32_t time;
    if (!app || !frame || provider->kind != APPLICATION_PROVIDER_Q3 ||
        !provider->state.q3 || !provider->constructed || !provider->attached ||
        provider->close_pending || app->destroy_requested ||
        app->operation != APPLICATION_ADVANCING || frame->provider != provider->owner ||
        frame->kind != QA_RULESET_Q3 || frame->phase != QA_CLIENT_END_FRAME ||
        !qa_session_active_frame(app->session, provider->owner, &active) ||
        active.provider != frame->provider || active.kind != frame->kind ||
        active.phase != frame->phase || active.number != frame->number ||
        active.start_ns != frame->start_ns || active.time_ns != frame->time_ns ||
        active.elapsed_ns != frame->elapsed_ns)
        return application_fail(error, QA_ERROR_ARGUMENT,
            "native Q3 ClientEndFrame has no actual source END frame");
    if (!qa_q3_source_clock(provider->state.q3, &time, error)) return false;
    return (uint32_t)time == (uint32_t)(frame->time_ns / UINT64_C(1000000)) ||
        application_fail(error, QA_ERROR_ARGUMENT,
            "native Q3 ClientEndFrame clock differs from its ENTRY");
}

static bool client_source(application_provider *provider, qa_actor_id actor,
    uint32_t *slot, qa_error *error)
{
    qa_q3_source_binding binding;
    if (!qa_actors_get(qa_session_actors(provider->application->session), actor))
        return application_fail(error, QA_ERROR_NOT_FOUND,
            "native Q3 ClientEndFrame has no current physical client");
    if (!qa_q3_native_client_slot(provider->state.q3, actor, slot, error) ||
        !qa_q3_source_binding_read(provider->state.q3, *slot, &binding, error)) return false;
    return (binding.in_use && qa_actor_id_equal(binding.actor, actor)) ||
        application_fail(error, QA_ERROR_NOT_FOUND,
            "native Q3 ClientEndFrame client binding has retired");
}

static bool client_current(application_provider *provider, qa_actor_id actor,
    uint32_t slot, const qa_source_frame *frame, bool *live, qa_error *error)
{
    qa_application *app = provider->application;
    qa_q3_source_binding binding;
    *live = false;
    if (app->destroy_requested || provider->close_pending) return true;
    if (!end_frame(provider, frame, error) ||
        !qa_q3_source_binding_read(provider->state.q3, slot, &binding, error)) return false;
    *live = binding.in_use && qa_actor_id_equal(binding.actor, actor) &&
        qa_actors_get(qa_session_actors(app->session), actor);
    if (*live) {
        uint32_t current_slot;
        if (!qa_q3_native_client_slot(provider->state.q3, actor, &current_slot, error) ||
            current_slot != slot)
            return application_fail(error, QA_ERROR_NOT_FOUND,
                "native Q3 ClientEndFrame changed its physical client binding");
    }
    return true;
}

static bool spectator_end_frame(application_provider *provider, qa_actor_id actor,
    uint32_t slot, const qa_source_frame *frame, qa_q3_client_session session,
    qa_error *error)
{
    qa_q3_game *game = provider->state.q3;
    if (session.spectator_state == QA_Q3_SPECTATOR_FOLLOW) {
        int32_t number = session.spectator_client;
        if (number == -1 || number == -2) {
            qa_q3_source_client_counts counts;
            if (!qa_q3_source_client_counts_read(game, &counts, error)) return false;
            number = number == -1 ? counts.follow1 : counts.follow2;
        }
        if (number >= 0) {
            qa_q3_native_client target;
            if (number >= (int32_t)QA_Q3_NATIVE_CLIENTS)
                return application_fail(error, QA_ERROR_FORMAT,
                    "native Q3 spectator target exceeds the fixed client records");
            if (!qa_q3_client_slot_read(game, (uint32_t)number, &target, error)) return false;
            if (target.connected == QA_Q3_CLIENT_CONNECTED && target.session.team != 3) {
                qa_q3_player followed;
                bool live;
                if (!qa_q3_wire_player_read(game, (uint32_t)number, &followed, error) ||
                    !client_current(provider, actor, slot, frame, &live, error)) return false;
                if (!live) return true;
                return qa_q3_client_follow_copy(game, actor, &followed, error) &&
                    client_current(provider, actor, slot, frame, &live, error);
            }
            if (session.spectator_client >= 0) {
                bool live;
                session.spectator_state = QA_Q3_SPECTATOR_FREE;
                if (!qa_q3_client_session_slot_write(game, slot,
                        QA_Q3_CLIENT_SESSION_STATE, &session, error) ||
                    !application_native_q3_client_begin(provider, actor, NULL, NULL, error) ||
                    !client_current(provider, actor, slot, frame, &live, error)) return false;
                if (!live) return true;
            }
        }
    }
    if (!qa_q3_client_session_slot_read(game, slot, &session, error)) return false;
    bool live;
    return qa_q3_client_follow_scoreboard(game, actor,
        session.spectator_state == QA_Q3_SPECTATOR_SCOREBOARD, error) &&
        client_current(provider, actor, slot, frame, &live, error);
}

static bool client_end_frame(application_provider *provider, qa_actor_id actor,
    uint32_t slot, const qa_source_frame *frame, qa_error *error)
{
    qa_q3_client_session session;
    qa_q3_game *game = provider->state.q3;
    if (!qa_q3_client_session_slot_read(game, slot, &session, error)) return false;
    if (session.team == 3)
        return spectator_end_frame(provider, actor, slot, frame, session, error);

    int32_t water_level, water_type;
    if (!qa_q3_client_movement_water_read(game, actor,
        &water_level, &water_type, error)) return false;
    bool publish, live;
    if (!qa_q3_client_end_prepare(game, actor, water_level,
            water_type, &publish, error) ||
        !client_current(provider, actor, slot, frame, &live, error)) return false;
    if (!live || !publish) return true;

    int32_t smooth, command_time;
    if (!application_native_q3_settings_integer_at(provider, APPLICATION_Q3_SETTING_G_SMOOTH_CLIENTS, &smooth, error) ||
        !qa_q3_client_command_time(game, actor, &command_time, error) ||
        !qa_q3_wire_player_publish(game, actor, true, smooth != 0, command_time, error) ||
        !client_current(provider, actor, slot, frame, &live, error)) return false;
    if (!live) return true;
    return qa_q3_wire_player_pending(game, actor, error) &&
        client_current(provider, actor, slot, frame, &live, error);
}

bool application_native_q3_source_client_end(void *opaque, qa_actor_id actor,
    const qa_source_frame *frame, qa_error *error)
{
    application_provider *provider = opaque;
    uint32_t slot;
    if (!end_frame(provider, frame, error) ||
        !client_source(provider, actor, &slot, error) ||
        !application_native_q3_console_borrow(provider, error)) return false;
    bool ok = client_end_frame(provider, actor, slot, frame, error);
    application_native_q3_console_release(provider);
    return ok;
}
