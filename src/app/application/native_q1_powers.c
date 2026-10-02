#include "native_q1_powers.h"
#include "map_players_private.h"
#include "qa/game_q1_bots.h"

#include <math.h>

typedef struct power_binding {
    application_provider *publisher, *primary;
    qa_application *application;
    qa_actor_id actor;
} power_binding;

static bool current(power_binding *binding, qa_error *error)
{
    qa_application *app = binding->application;
    application_provider *publisher = binding->publisher, *primary = binding->primary;
    if (!app || app->destroy_requested || app->finalizing || !app->session ||
        !app->combat || !app->players || app->players->map_provider != primary ||
        application_world_provider(app, QA_ROLE_ENTITIES, "") != primary ||
        !publisher || publisher->application != app || publisher->kind != APPLICATION_PROVIDER_Q1 ||
        !publisher->state.q1 || !publisher->constructed || !publisher->attached || publisher->close_pending ||
        !primary || primary->application != app || !primary->constructed ||
        !primary->attached || primary->close_pending ||
        !qa_actors_get(qa_session_actors(app->session), binding->actor))
        return application_fail(error, QA_ERROR_ARGUMENT,
            "Q1 timed power lost its genuine published source owners");
    bool published = false;
    for (size_t i = 0; i < app->provider_count; ++i)
        if (app->providers[i] == publisher) { published = true; break; }
    if (!published)
        return application_fail(error, QA_ERROR_ARGUMENT,
            "Q1 timed power publisher has no current application admission");
    for (size_t i = 0; i < app->players->count; ++i) {
        const application_player_record *record = app->players->records + i;
        if (record->retiring || !qa_actor_id_equal(record->actor, binding->actor)) continue;
        if (primary->kind == APPLICATION_PROVIDER_Q1) {
            uint32_t slot;
            if (!qa_q1_native_client_slot(primary->state.q1, binding->actor, &slot, error)) return false;
            if (slot != record->client_slot)
                return application_fail(error, QA_ERROR_ARGUMENT,
                    "Q1 timed power actor differs from its physical source client");
        }
        return true;
    }
    return application_fail(error, QA_ERROR_ARGUMENT,
        "Q1 timed power has no actual live player roster receiver");
}

bool application_native_q1_powerup(void *opaque, qa_actor_id actor, qa_q1_power power,
    double expires, qa_error *error)
{
    application_provider *publisher = opaque;
    qa_application *app = publisher ? publisher->application : NULL;
    power_binding binding = {.publisher = publisher, .application = app, .actor = actor,
        .primary = app ? application_world_provider(app, QA_ROLE_ENTITIES, "") : NULL};
    if (power < QA_Q1_QUAD || power >= QA_Q1_POWER_COUNT || !isfinite(expires))
        return application_fail(error, QA_ERROR_ARGUMENT, "Invalid source Q1 power publication");
    if (!current(&binding, error)) return false;
    if (power != QA_Q1_INVULNERABILITY) return true;
    qa_clock_state clock;
    if (!qa_session_clock(app->session, binding.primary->owner, &clock) ||
        clock.frame.provider != binding.primary->owner ||
        clock.frame.kind != binding.primary->component.clock.kind)
        return application_fail(error, QA_ERROR_ARGUMENT,
            "Q1 timed power has no actual primary ENTITIES clock");
    bool god = false;
    if (binding.primary->kind == APPLICATION_PROVIDER_Q1) {
        qa_q1_source_client_view client;
        if (!qa_q1_source_client_read(binding.primary->state.q1, actor, &client))
            return application_fail(error, QA_ERROR_ARGUMENT,
                "Q1 timed power lost its actual physical God mode owner");
        god = client.god_mode;
    }
    qa_combat_state traits;
    if (!qa_combat_read_traits(app->combat, actor, &traits, error) ||
        !current(&binding, error)) return false;
    traits.invulnerable = expires > (double)clock.frame.time_ns / 1000000000.0 || god;
    return qa_combat_set_traits(app->combat, actor, &traits, error) && current(&binding, error);
}

bool application_native_q1_set_gravity(void *opaque, qa_actor_id actor, float scale,
    qa_error *error)
{
    application_provider *publisher = opaque;
    qa_application *app = publisher ? publisher->application : NULL;
    power_binding binding = {.publisher = publisher, .application = app, .actor = actor,
        .primary = app ? application_world_provider(app, QA_ROLE_ENTITIES, "") : NULL};
    if (!isfinite(scale) || scale < 0)
        return application_fail(error, QA_ERROR_ARGUMENT, "Invalid source Q1 gravity scale");
    return current(&binding, error) && application_control_gravity(app, actor, scale, error) &&
        current(&binding, error);
}
