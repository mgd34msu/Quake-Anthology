#include "native_q1_composition_player.h"
#include "native_q1_composition.h"
#include "qa/game_q1_rogue.h"
#include "guest_native_q2_private.h"
#include "native_q2_callbacks.h"
#include "native_q2_inventory_scanner.h"
#include "native_q2_inventory_rows.h"
#include "qa/native_host_q2_wire.h"
#include "qa/console_cvar_observer.h"

static application_provider *rogue_player(qa_application *app, qa_actor_owner owner,
    qa_actor_id actor, qa_error *error) {
    bool observer;
    if (!application_native_q1_composition_player_current(app, owner, QA_MODE_ROGUE,
        actor, &observer, error)) return NULL;
    application_provider *source = application_world_provider(app, QA_ROLE_ENTITIES, "");
    if (!source || source->owner != owner || source->kind != APPLICATION_PROVIDER_Q1 ||
        !source->state.q1) {
        application_fail(error, QA_ERROR_ARGUMENT, "Rogue player lost its actual native source");
        return NULL;
    }
    return source;
}
bool application_native_q1_rogue_state(void *context, qa_actor_owner owner,
    qa_actor_id actor, qa_actor_id *out, qa_error *error) {
    qa_application *app = context;
    application_provider *source = rogue_player(app, owner, actor, error);
    qa_actor_id state;
    if (!out || !source || !qa_q1_rogue_state(source->state.q1, actor, &state, error) ||
        rogue_player(app, owner, actor, error) != source ||
        !qa_q1_rogue_state_current(source->state.q1, actor, state, error)) return false;
    *out = state;
    return true;
}
bool application_native_q1_rogue_state_current(void *context, qa_actor_owner owner,
    qa_actor_id actor, qa_actor_id state, qa_error *error) {
    application_provider *source = rogue_player(context, owner, actor, error);
    qa_actor_id actual;
    bool found;
    return source && qa_q1_rogue_state_current(source->state.q1, actor, state, error) &&
        qa_q1_rogue_state_find(source->state.q1, actor, &actual, &found, error) && found &&
        qa_actor_id_equal(actual, state);
}
bool application_native_q1_rogue_number_read(void *context, qa_actor_owner owner,
    qa_actor_id actor, qa_actor_id state, uint32_t field, double *out, qa_error *error) {
    qa_application *app = context;
    application_provider *source = rogue_player(app, owner, actor, error);
    double value;
    if (!out || field >= QA_Q1_ROGUE_FIELDS || !source ||
        !application_native_q1_rogue_state_current(app, owner, actor, state, error) ||
        !qa_q1_rogue_number_read(source->state.q1, state, (qa_q1_rogue_field)field,
            &value, error) || rogue_player(app, owner, actor, error) != source ||
        !application_native_q1_rogue_state_current(app, owner, actor, state, error)) return false;
    *out = value;
    return true;
}
bool application_native_q1_rogue_number_write(void *context, qa_actor_owner owner,
    qa_actor_id actor, qa_actor_id state, uint32_t field, double value, qa_error *error) {
    qa_application *app = context;
    application_provider *source = rogue_player(app, owner, actor, error);
    return field < QA_Q1_ROGUE_FIELDS && source &&
        application_native_q1_rogue_state_current(app, owner, actor, state, error) &&
        qa_q1_rogue_number_write(source->state.q1, state, (qa_q1_rogue_field)field, value, error) &&
        rogue_player(app, owner, actor, error) == source &&
        application_native_q1_rogue_state_current(app, owner, actor, state, error);
}

static bool selected_q2_returned(const struct application_native_q2 *engine) {
    return !engine->baseline && !engine->calls &&
        (!engine->callbacks || application_native_q2_callbacks_current(engine->callbacks)) &&
        application_native_q2_inventory_scanner_returned(engine->inventory_scanner) &&
        application_native_q2_inventory_rows_idle(engine->inventory_rows) &&
        qa_console_idle(engine->console) &&
        (!engine->cvars || qa_cvars_observer_idle(engine->cvars));
}

bool application_native_q1_selected_q2_frame(qa_application *app, qa_actor_id actor,
    double *out, qa_error *error) {
    application_provider *provider = app ? application_provider_for(app, actor, QA_ROLE_CHARACTER, "") : NULL;
    struct application_native_q2 *engine = provider && provider->kind == APPLICATION_PROVIDER_NATIVE ?
        provider->state.native.q2_engine : NULL;
    qa_clock_state clock;
    if (!out || !engine || !app->session || !app->world || app->destroy_requested ||
        (app->operation != APPLICATION_IDLE && app->operation != APPLICATION_ADVANCING) ||
        !qa_actors_get(qa_session_actors(app->session), actor) ||
        provider->application != app || !provider->constructed || !provider->attached ||
        provider->close_pending || !provider->map_bound || engine->provider != provider ||
        engine->world != app->world || !engine->initialized || !engine->map_ready ||
        engine->shutting_down || engine->activation_failed || !provider->state.native.host ||
        !selected_q2_returned(engine) ||
        (engine->profile != QA_NATIVE_Q2_GAME_API3 && engine->profile != QA_NATIVE_Q2_GAME_API2023) ||
        qa_native_terminal(qa_native_host_instance(provider->state.native.host)) ||
        !qa_session_clock(app->session, provider->owner, &clock) || clock.frame.provider != provider->owner ||
        clock.frame.kind != (engine->profile == QA_NATIVE_Q2_GAME_API3 ? QA_CLOCK_Q2_CLASSIC : QA_CLOCK_Q2_RERELEASE))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q1 carried flag lost its returned original Q2 character");
    uint32_t slot = 0;
    for (uint32_t i = 1; i < 257; ++i) {
        const application_native_q2_client *client = engine->clients + i;
        if (!client->connected || !client->begun || client->disconnect_started ||
            !qa_actor_id_equal(client->actor, actor)) continue;
        if (slot) return application_fail(error, QA_ERROR_FORMAT, "Original Q2 character aliases physical client slots");
        slot = i;
    }
    if (!slot) return application_fail(error, QA_ERROR_NOT_FOUND, "Q1 carried flag has no begun original Q2 character");
    double frame;
    if (!qa_native_host_q2_character_frame(provider->state.native.host, slot, actor, &frame, error)) return false;
    if (application_provider_for(app, actor, QA_ROLE_CHARACTER, "") != provider ||
        !selected_q2_returned(engine) || provider->close_pending ||
        !engine->clients[slot].connected || !engine->clients[slot].begun ||
        engine->clients[slot].disconnect_started || !qa_actor_id_equal(engine->clients[slot].actor, actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Original Q2 flag animation changed its actual client binding");
    *out = frame;
    return true;
}
