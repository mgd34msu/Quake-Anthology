#include "internal.h"
#include "guest_qc_internal.h"
#include "qa/application_selected_effects.h"
#include "qa/game_q2_bots.h"
#include "qa/persistence_content.h"
#include <string.h>

static bool observed(const qa_application *app, bool retained)
{
    return app && app->session && app->world && !app->destroy_requested &&
        app->state == QA_APPLICATION_RUNNING && app->map_view_ready &&
        !app->q3_round_active && !app->q3_world_restart && !app->routing_snapshot &&
        !app->frame_preparing &&
        (app->operation == APPLICATION_IDLE || app->operation == APPLICATION_ADVANCING ||
            (retained && app->operation == APPLICATION_PERSISTING && qa_application_content_graph_read(app))) &&
        qa_session_safe(app->session) && !qa_session_faulted(app->session) &&
        qa_world_idle(app->world);
}
static bool ready(const qa_application *app) { return observed(app, false); }

static bool provider_ready(const application_provider *provider)
{
    return provider && provider->constructed && provider->attached &&
        !provider->close_pending && provider->launch && provider->launch->content &&
        provider->product;
}

static bool primary_clock(qa_application *app, application_provider *primary,
    qa_application_selected_effects *view, qa_error *error)
{
    qa_clock_state clock;
    if (!qa_session_clock(app->session, primary->owner, &clock))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Effect sampling lost its actual world source frame");
    view->primary_frame = clock.frame;
    view->primary_kind = QA_APPLICATION_EFFECTS_EXTERNAL;
    switch (primary->kind) {
    case APPLICATION_PROVIDER_Q1: {
        double elapsed;
        view->primary_kind = QA_APPLICATION_EFFECTS_Q1;
        view->primary_native.q1 = primary->state.q1;
        if (!qa_q1_game_clock_read(view->primary_native.q1, &view->primary_time_ns, &elapsed))
            return application_fail(error, QA_ERROR_ARGUMENT, "Effect sampling lost its actual Quake world clock");
        break;
    }
    case APPLICATION_PROVIDER_Q2: {
        bool intermission; uint64_t started;
        view->primary_kind = QA_APPLICATION_EFFECTS_Q2;
        view->primary_native.q2 = primary->state.q2;
        if (!qa_q2_bot_clock_read(view->primary_native.q2, &view->primary_time_ns, &intermission, &started, error)) return false;
        break;
    }
    case APPLICATION_PROVIDER_Q3:
        view->primary_kind = QA_APPLICATION_EFFECTS_Q3;
        view->primary_native.q3 = primary->state.q3;
        if (!qa_q3_destroy_ready(view->primary_native.q3))
            return application_fail(error, QA_ERROR_ARGUMENT, "Effect sampling needs its actual idle Q3 world source");
        return qa_q3_source_clock(view->primary_native.q3, &view->sample_time_ms, error);
    case APPLICATION_PROVIDER_QC:
        view->primary_time_ns = primary->state.qc.engine->source_time_ns;
        break;
    case APPLICATION_PROVIDER_QVM:
    case APPLICATION_PROVIDER_NATIVE:
        view->primary_time_ns = clock.frame.time_ns;
        break;
    }
    uint32_t milliseconds = (uint32_t)(view->primary_time_ns / UINT64_C(1000000));
    memcpy(&view->sample_time_ms, &milliseconds, sizeof(milliseconds));
    return true;
}

static bool observe(qa_application *app, application_provider *provider,
    qa_actor_id actor, bool retained, qa_application_selected_effects *out, qa_error *error)
{
    application_provider *primary = application_world_provider(app, QA_ROLE_ENTITIES, "");
    if (!provider_ready(provider) || !provider_ready(primary))
        return application_fail(error, QA_ERROR_ARGUMENT,
            "Selected effects lost their actual constructed source owners");
    qa_clock_state clock;
    qa_application_selected_effects view = {.actor = actor, .provider = provider->owner,
        .primary = primary->owner, .session = app->session, .publication = qa_application_launch(app),
        .launch = provider->launch, .content = provider->launch->content,
        .product = provider->product->id, .family = provider->product->family,
        .publication_generation = app->publication_generation, .map_revision = app->map_revision,
        .application_frame = application_frame_revision(app), .kind = QA_APPLICATION_EFFECTS_EXTERNAL,
        .observation = QA_APPLICATION_EFFECTS_PRODUCER};
    if (!view.publication || !qa_session_clock(app->session, provider->owner, &clock))
        return application_fail(error, QA_ERROR_NOT_FOUND,
            "Selected effects have no actual publication or completed source clock");
    view.source_frame = clock.frame;
    if (!primary_clock(app, primary, &view, error)) return false;
    switch (provider->kind) {
    case APPLICATION_PROVIDER_Q1: {
        double elapsed;
        view.kind = QA_APPLICATION_EFFECTS_Q1;
        view.native.q1 = provider->state.q1;
        if (!qa_q1_game_clock_read(view.native.q1, &view.source_time_ns, &elapsed))
            return application_fail(error, QA_ERROR_ARGUMENT, "Selected Q1 effects source is unavailable");
        break;
    }
    case APPLICATION_PROVIDER_Q2: {
        bool intermission;
        uint64_t started;
        view.kind = QA_APPLICATION_EFFECTS_Q2;
        view.native.q2 = provider->state.q2;
        if (!qa_q2_bot_clock_read(view.native.q2, &view.source_time_ns, &intermission, &started, error))
            return false;
        break;
    }
    case APPLICATION_PROVIDER_Q3:
        view.kind = QA_APPLICATION_EFFECTS_Q3;
        view.native.q3 = provider->state.q3;
        if (!qa_q3_destroy_ready(view.native.q3) ||
            !qa_q3_source_clock(view.native.q3, &view.q3_time_ms, error) ||
            !qa_q3_source_match_context_read(view.native.q3, &view.q3_product, &view.q3_match_start_ms, error))
            return application_fail(error, QA_ERROR_ARGUMENT, "Selected Q3 effects source is not idle");
        break;
    case APPLICATION_PROVIDER_QC:
        view.source_time_ns = provider->state.qc.engine->source_time_ns;
        break;
    case APPLICATION_PROVIDER_QVM:
    case APPLICATION_PROVIDER_NATIVE:
        view.source_time_ns = clock.frame.time_ns;
        break;
    }
    if (!observed(app, retained) ||
        application_world_provider(app, QA_ROLE_ENTITIES, "") != primary)
        return application_fail(error, QA_ERROR_ARGUMENT, "Selected effects source changed during observation");
    *out = view;
    return true;
}

bool qa_application_selected_effects_read(qa_application *app, qa_actor_id actor,
    qa_application_selected_effects *out, bool *found, qa_error *error)
{
    if (!out || !found || !ready(app) || !qa_actors_get(qa_session_actors(app->session), actor))
        return application_fail(error, QA_ERROR_ARGUMENT,
            "Selected effects require their live full actor at a completed source frame");
    *found = false;
    application_provider *provider = application_provider_for(app, actor, QA_ROLE_EFFECTS, "");
    if (!provider) return true;
    qa_application_selected_effects view;
    if (!observe(app, provider, actor, false, &view, error)) return false;
    if (application_provider_for(app, actor, QA_ROLE_EFFECTS, "") != provider ||
        !qa_actors_get(qa_session_actors(app->session), actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Selected effects lost their routed full actor");
    view.observation = QA_APPLICATION_EFFECTS_ROUTED_ACTOR;
    *out = view; *found = true; return true;
}

static bool same_frame(qa_source_frame a, qa_source_frame b)
{
    return a.provider == b.provider && a.kind == b.kind && a.phase == b.phase &&
        a.number == b.number && a.start_ns == b.start_ns && a.time_ns == b.time_ns &&
        a.elapsed_ns == b.elapsed_ns;
}

static bool same_source(const qa_application_selected_effects *left,
    const qa_application_selected_effects *saved)
{
    qa_application_selected_effects actual = *left;
    if (actual.kind != saved->kind) return false;
    bool same_native = actual.kind == QA_APPLICATION_EFFECTS_EXTERNAL ||
        (actual.kind == QA_APPLICATION_EFFECTS_Q1 && actual.native.q1 == saved->native.q1) ||
        (actual.kind == QA_APPLICATION_EFFECTS_Q2 && actual.native.q2 == saved->native.q2) ||
        (actual.kind == QA_APPLICATION_EFFECTS_Q3 && actual.native.q3 == saved->native.q3);
    bool same_primary = actual.primary_kind == saved->primary_kind &&
        (actual.primary_kind == QA_APPLICATION_EFFECTS_EXTERNAL ||
        (actual.primary_kind == QA_APPLICATION_EFFECTS_Q1 && actual.primary_native.q1 == saved->primary_native.q1) ||
        (actual.primary_kind == QA_APPLICATION_EFFECTS_Q2 && actual.primary_native.q2 == saved->primary_native.q2) ||
        (actual.primary_kind == QA_APPLICATION_EFFECTS_Q3 && actual.primary_native.q3 == saved->primary_native.q3));
    return actual.provider == saved->provider && actual.primary == saved->primary &&
        actual.session == saved->session && actual.publication == saved->publication &&
        actual.launch == saved->launch && actual.content == saved->content && actual.product == saved->product &&
        actual.family == saved->family && actual.kind == saved->kind &&
        actual.observation == saved->observation && same_native && same_primary &&
        same_frame(actual.primary_frame, saved->primary_frame) && actual.primary_time_ns == saved->primary_time_ns &&
        actual.sample_time_ms == saved->sample_time_ms &&
        actual.publication_generation == saved->publication_generation && actual.map_revision == saved->map_revision &&
        actual.application_frame == saved->application_frame && actual.source_time_ns == saved->source_time_ns &&
        actual.q3_time_ms == saved->q3_time_ms && actual.q3_product == saved->q3_product &&
        actual.q3_match_start_ms == saved->q3_match_start_ms && same_frame(actual.source_frame, saved->source_frame);
}

bool qa_application_selected_effects_current(qa_application *app,
    const qa_application_selected_effects *saved)
{
    qa_application_selected_effects actual;
    bool found;
    if (saved && saved->observation == QA_APPLICATION_EFFECTS_PRODUCER)
        return qa_actor_id_equal(saved->actor, (qa_actor_id){0}) &&
            qa_application_effects_producer_read(app, saved->provider, &actual, NULL) && same_source(&actual, saved);
    return saved && qa_application_selected_effects_read(app, saved->actor, &actual, &found, NULL) &&
        found && same_source(&actual, saved);
}

static application_provider *event_provider(qa_application *app, qa_actor_owner owner)
{
    for (size_t i = 0; i < app->provider_count; ++i)
        if (app->providers[i] && app->providers[i]->owner == owner) return app->providers[i];
    return NULL;
}

bool qa_application_effects_producer_read(qa_application *app, qa_actor_owner owner,
    qa_application_selected_effects *out, qa_error *error)
{
    if (!out || !ready(app) || !owner)
        return application_fail(error, QA_ERROR_ARGUMENT, "Effect sampling requires its constructed completed producer");
    return observe(app, event_provider(app, owner), (qa_actor_id){0}, false, out, error);
}
bool qa_application_effects_retained_read(qa_application *app, qa_actor_owner owner,
    qa_application_selected_effects *out, qa_error *error)
{
    if (!out || !owner || !observed(app, true) || !qa_application_content_graph_read(app))
        return application_fail(error, QA_ERROR_ARGUMENT, "Effect continuation requires its actual retained source and content lease");
    return observe(app, event_provider(app, owner), (qa_actor_id){0}, true, out, error);
}

bool qa_application_effect_event_read(qa_application *app, size_t ordinal,
    qa_application_effect_event *out, qa_error *error)
{
    if (!out || !ready(app) || ordinal >= app->event_count)
        return application_fail(error, QA_ERROR_ARGUMENT, "Effect event requires its completed canonical queue row");
    const qa_builtin_event *event = &app->events[ordinal].event;
    application_provider *provider = event_provider(app, event->provider);
    qa_application_effect_event view = {.event = event, .ordinal = ordinal,
        .queue_generation = app->protocol_events_generation};
    if (!observe(app, provider, event->actor, false, &view.source, error)) return false;
    if (view.source.family != event->family)
        return application_fail(error, QA_ERROR_FORMAT, "Effect event differs from its actual producer family");
    *out = view; return true;
}

bool qa_application_effect_event_current(qa_application *app,
    const qa_application_effect_event *saved)
{
    if (!saved || !ready(app) || saved->queue_generation != app->protocol_events_generation ||
        saved->ordinal >= app->event_count || saved->event != &app->events[saved->ordinal].event)
        return false;
    qa_application_effect_event actual;
    return qa_application_effect_event_read(app, saved->ordinal, &actual, NULL) &&
        qa_actor_id_equal(actual.source.actor, saved->source.actor) && same_source(&actual.source, &saved->source);
}

bool qa_application_selected_effects_q3_binding(qa_application *app,
    const qa_application_selected_effects *source, qa_q3_source_binding *out,
    bool *found, qa_error *error)
{
    if (!out || !found || !source || source->kind != QA_APPLICATION_EFFECTS_Q3 ||
        !qa_application_selected_effects_current(app, source))
        return application_fail(error, QA_ERROR_ARGUMENT, "Selected Q3 binding requires its actual current effects source");
    *found = false;
    uint32_t slot;
    if (!qa_q3_source_actor_slot(source->native.q3, source->actor, &slot, NULL)) return true;
    qa_q3_source_binding binding;
    if (!qa_q3_source_binding_read(source->native.q3, slot, &binding, error) ||
        !qa_actor_id_equal(binding.actor, source->actor) || binding.number != (int32_t)slot ||
        !qa_application_selected_effects_current(app, source))
        return application_fail(error, QA_ERROR_ARGUMENT, "Selected Q3 effects lost their full physical actor binding");
    *out = binding; *found = true;
    return true;
}
