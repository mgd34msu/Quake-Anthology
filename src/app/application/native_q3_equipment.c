#include "native_q3_equipment.h"
#include "native_q3_wire.h"
#include "native_q3_wire_state.h"
#include "qa/game_q3_source.h"

static bool source_ready(application_provider *provider, qa_actor_id actor)
{
    qa_application *app = provider ? provider->application : NULL;
    return app && app->session && app->world && !app->destroy_requested &&
        app->state == QA_APPLICATION_RUNNING && app->map_view_ready &&
        !app->q3_round_active && !app->q3_world_restart && !app->routing_snapshot &&
        (app->operation == APPLICATION_IDLE || app->operation == APPLICATION_ADVANCING) &&
        qa_session_safe(app->session) && !qa_session_faulted(app->session) &&
        qa_world_idle(app->world) && provider->kind == APPLICATION_PROVIDER_Q3 &&
        provider->state.q3 && provider->native_q3_wire && provider->constructed &&
        provider->attached && !provider->close_pending && provider->launch &&
        provider->launch->content && provider->product && provider->product->family == QA_GAME_Q3 &&
        qa_actors_get(qa_session_actors(app->session), actor) &&
        application_provider_for(app, actor, QA_ROLE_ARSENAL, "") == provider &&
        application_native_q3_wire_idle(provider) && qa_q3_destroy_ready(provider->state.q3);
}

static bool physical(application_provider *provider, qa_actor_id actor, uint32_t *slot,
    qa_q3_source_binding *binding, qa_error *error)
{
    uint32_t actual;
    if (!source_ready(provider, actor) ||
        !qa_q3_native_client_slot(provider->state.q3, actor, &actual, error) ||
        !qa_q3_source_binding_read(provider->state.q3, actual, binding, error) ||
        !qa_actor_id_equal(binding->actor, actor) || binding->client_slot != (int32_t)actual ||
        !binding->body_attached)
        return application_fail(error, QA_ERROR_ARGUMENT, "Selected native Q3 equipment lost its full physical client binding");
    *slot = actual;
    return true;
}

static bool same_frame(const qa_source_frame *first, const qa_source_frame *second)
{
    return first->provider == second->provider && first->kind == second->kind &&
        first->phase == second->phase && first->number == second->number &&
        first->start_ns == second->start_ns && first->time_ns == second->time_ns &&
        first->elapsed_ns == second->elapsed_ns;
}

bool application_native_q3_equipment_current(application_provider *provider,
    const application_native_q3_equipment_view *view)
{
    qa_clock_state clock;
    qa_q3_source_binding binding;
    uint32_t slot;
    int32_t time, wire_time;
    if (!view || !physical(provider, view->actor, &slot, &binding, NULL)) return false;
    qa_application *app = provider->application;
    return provider->owner == view->provider && provider->state.q3 == view->game &&
        provider->launch == view->launch && provider->product == view->product &&
        app->publication_generation == view->publication_generation && app->map_revision == view->map_revision &&
        slot == view->source_slot && binding.in_use == view->binding.in_use &&
        qa_session_clock(app->session, provider->owner, &clock) &&
        same_frame(&clock.frame, &view->source_frame) &&
        application_q3_wire_time(provider, &wire_time, NULL) &&
        qa_q3_source_clock(view->game, &time, NULL) && time == view->source_time_ms &&
        (!clock.frame.number || time == wire_time);
}

bool application_native_q3_equipment_read(application_provider *provider, qa_actor_id actor,
    application_native_q3_equipment_view *out, qa_error *error)
{
    application_native_q3_equipment_view view = {.actor = actor};
    if (!out)
        return application_fail(error, QA_ERROR_ARGUMENT, "Selected native Q3 equipment requires an output");
    if (!physical(provider, actor, &view.source_slot, &view.binding, error)) return false;
    qa_application *app = provider->application;
    qa_clock_state clock;
    int32_t wire_time;
    view.provider = provider->owner; view.game = provider->state.q3;
    view.launch = provider->launch; view.product = provider->product;
    view.publication_generation = app->publication_generation; view.map_revision = app->map_revision;
    if (!qa_session_clock(app->session, provider->owner, &clock) ||
        !application_q3_wire_time(provider, &wire_time, error) ||
        !qa_q3_source_clock(view.game, &view.source_time_ms, error) ||
        !qa_q3_wire_player_read(view.game, view.source_slot, &view.player, error)) return false;
    if (!qa_q3_player_read(view.game, actor, &view.arsenal) ||
        !qa_q3_player_fire_read(view.game, actor, &view.fire))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Selected native Q3 equipment lost its genuine arsenal continuation");
    view.source_frame = clock.frame;
    if ((clock.frame.number && view.source_time_ms != wire_time) ||
        !application_native_q3_equipment_current(provider, &view))
        return application_fail(error, QA_ERROR_ARGUMENT, "Selected native Q3 equipment changed its completed source cut");
    *out = view;
    return true;
}
