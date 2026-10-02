#include "guest_native_q2_private.h"
#include "qa/application_native_q2_presentation.h"
#include "qa/game_q2_bots.h"
#include "qa/game_q2_combat.h"

bool qa_application_native_q2_source_profile_read(qa_application *app, qa_actor_owner owner,
    qa_q2_edition *out, bool *found, qa_error *error)
{
    if (!app || !owner || !out || !found || app->destroy_requested)
        return application_fail(error,QA_ERROR_ARGUMENT,"Q2 Source profile requires its actual emitting owner");
    application_provider *provider=NULL;
    for (application_provider *p=app->live_providers;p;p=p->next_live)
        if (p->owner==owner) { provider=p; break; }
    *found=false;
    if (!provider || !provider->product || provider->product->family!=QA_GAME_Q2) return true;
    if (provider->application!=app || !provider->constructed || provider->close_pending)
        return application_fail(error,QA_ERROR_ARGUMENT,"Q2 Source profile names an unavailable GAME owner");
    qa_q2_edition edition;
    if (provider->kind==APPLICATION_PROVIDER_Q2) {
        qa_q2_combat_rules rules;
        if (!provider->state.q2 || !qa_q2_combat_rules_read(provider->state.q2,&rules) ||
            rules.owner!=owner || (rules.edition!=QA_Q2_CLASSIC && rules.edition!=QA_Q2_RERELEASE))
            return application_fail(error,QA_ERROR_FORMAT,"Q2 Source profile lost its compiled GAME rules");
        edition=rules.edition;
    } else if (provider->kind==APPLICATION_PROVIDER_NATIVE) {
        const struct application_native_q2 *engine=provider->state.native.q2_engine;
        if (!engine || engine->provider!=provider)
            return application_fail(error,QA_ERROR_FORMAT,"Q2 Source profile lost its original GAME owner");
        if (engine->profile==QA_NATIVE_Q2_CGAME_API2023) return true;
        if (engine->profile!=QA_NATIVE_Q2_GAME_API3 && engine->profile!=QA_NATIVE_Q2_GAME_API2023)
            return application_fail(error,QA_ERROR_FORMAT,"Q2 Source profile has an unsupported original GAME ABI");
        edition=engine->profile==QA_NATIVE_Q2_GAME_API3 ? QA_Q2_CLASSIC : QA_Q2_RERELEASE;
    } else return true;
    *out=edition; *found=true;
    return true;
}

static bool ready(const qa_application *app, bool retained)
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

static bool frame_equal(qa_source_frame left, qa_source_frame right, bool phase)
{
    return left.provider == right.provider && left.kind == right.kind &&
        (!phase || left.phase == right.phase) && left.number == right.number &&
        left.start_ns == right.start_ns && left.elapsed_ns == right.elapsed_ns &&
        left.time_ns == right.time_ns;
}

static bool observe(qa_application *app, bool retained,
    qa_application_native_q2_presentation *out, bool *found, qa_error *error)
{
    if (!out || !found || !ready(app, retained) ||
        (retained && !qa_application_content_graph_read(app)))
        return application_fail(error, QA_ERROR_ARGUMENT,
            "Q2 presentation requires its completed published world source");
    *found = false;
    application_provider *provider = application_world_provider(app, QA_ROLE_ENTITIES, "");
    if (!provider || !provider->product || provider->product->family != QA_GAME_Q2)
        return true;
    if (provider->application != app || !provider->constructed || !provider->attached ||
        !provider->map_bound || provider->close_pending || !provider->launch ||
        !provider->launch->content || !provider->owner)
        return application_fail(error, QA_ERROR_ARGUMENT,
            "Q2 presentation lost its actual constructed physical source");
    qa_application_native_q2_presentation view = {.session = app->session,
        .publication = qa_application_launch(app), .launch = provider->launch,
        .content = provider->launch->content, .source_owner = provider->owner,
        .content_product = provider->launch->selection.product,
        .clock_config = provider->component.clock,
        .publication_generation = app->publication_generation, .map_revision = app->map_revision,
        .retained = retained};
    if (!view.publication || !qa_session_clock(app->session, provider->owner, &view.clock) ||
        view.clock.frame.provider != provider->owner || view.clock.frame.phase != QA_FRAME_EXIT ||
        view.clock.frame.number != view.clock.frame_number ||
        (view.clock.frame.kind != QA_CLOCK_Q2_CLASSIC && view.clock.frame.kind != QA_CLOCK_Q2_RERELEASE) ||
        view.clock_config.kind != view.clock.frame.kind)
        return application_fail(error, QA_ERROR_FORMAT,
            "Q2 presentation has no matching completed physical source clock");
    if (provider->kind == APPLICATION_PROVIDER_Q2) {
        qa_q2_combat_rules rules;
        bool intermission;
        uint64_t started;
        if (!provider->state.q2 || qa_q2_current_actor(provider->state.q2).registry ||
            !qa_q2_combat_rules_read(provider->state.q2, &rules) || rules.owner != provider->owner ||
            !qa_q2_bot_clock_read(provider->state.q2, &view.server_time_ns, &intermission, &started, error))
            return application_fail(error, QA_ERROR_ARGUMENT,
                "Q2 presentation lost its idle physical GAME clock");
        view.kind = QA_APPLICATION_NATIVE_Q2_BUILTIN;
        view.source.game = provider->state.q2;
        view.edition = rules.edition;
    } else if (provider->kind == APPLICATION_PROVIDER_NATIVE) {
        const struct application_native_q2 *engine = provider->state.native.q2_engine;
        if (!engine || engine->provider != provider || engine->world != app->world ||
            !engine->initialized || !engine->map_ready || engine->shutting_down || engine->activation_failed ||
            (engine->profile != QA_NATIVE_Q2_GAME_API3 && engine->profile != QA_NATIVE_Q2_GAME_API2023) ||
            !provider->state.native.host || !application_native_q2_idle(provider) ||
            qa_native_terminal(qa_native_host_instance(provider->state.native.host)) ||
            (view.clock.frame.number && !frame_equal(engine->frame, view.clock.frame, false)))
            return application_fail(error, QA_ERROR_ARGUMENT,
                "Q2 presentation lost its completed original GAME invocation");
        view.kind = QA_APPLICATION_NATIVE_Q2_ORIGINAL;
        view.source.original.host = provider->state.native.host;
        view.source.original.profile = engine->profile;
        view.edition = engine->profile == QA_NATIVE_Q2_GAME_API3 ? QA_Q2_CLASSIC : QA_Q2_RERELEASE;
        view.server_time_ns = view.clock.frame.time_ns;
    } else {
        return application_fail(error, QA_ERROR_UNSUPPORTED,
            "Q2 presentation requires an actual compiled or original Q2 GAME");
    }
    if (view.server_time_ns != view.clock.frame.time_ns ||
        (view.edition == QA_Q2_CLASSIC ? QA_CLOCK_Q2_CLASSIC : QA_CLOCK_Q2_RERELEASE) != view.clock.frame.kind)
        return application_fail(error, QA_ERROR_FORMAT,
            "Q2 physical GAME time differs from its completed source frame");
    *out = view; *found = true;
    return true;
}

bool qa_application_native_q2_presentation_selected(qa_application *app,
    qa_application_native_q2_presentation *out, bool *found, qa_error *error)
{
    return observe(app, false, out, found, error);
}

bool qa_application_native_q2_presentation_retained_selected(qa_application *app,
    qa_application_native_q2_presentation *out, bool *found, qa_error *error)
{
    return observe(app, true, out, found, error);
}

static bool config_equal(qa_clock_config left, qa_clock_config right)
{
    return left.kind == right.kind && left.initial_time_ns == right.initial_time_ns &&
        left.interval_ns == right.interval_ns && left.minimum_frame_ns == right.minimum_frame_ns &&
        left.maximum_frame_ns == right.maximum_frame_ns && left.initial_lead_ns == right.initial_lead_ns &&
        left.maximum_steps == right.maximum_steps;
}

bool qa_application_native_q2_presentation_current(qa_application *app,
    const qa_application_native_q2_presentation *saved)
{
    qa_application_native_q2_presentation actual;
    bool found;
    if (!saved || !observe(app, saved->retained, &actual, &found, NULL) || !found ||
        actual.session != saved->session || actual.publication != saved->publication ||
        actual.launch != saved->launch || actual.content != saved->content ||
        actual.source_owner != saved->source_owner || actual.content_product != saved->content_product ||
        actual.edition != saved->edition || actual.kind != saved->kind ||
        actual.server_time_ns != saved->server_time_ns ||
        actual.publication_generation != saved->publication_generation || actual.map_revision != saved->map_revision ||
        !config_equal(actual.clock_config, saved->clock_config) ||
        actual.clock.host_origin_ns != saved->clock.host_origin_ns || actual.clock.elapsed_ns != saved->clock.elapsed_ns ||
        actual.clock.debt_ns != saved->clock.debt_ns || actual.clock.frame_number != saved->clock.frame_number ||
        actual.clock.paused != saved->clock.paused || !frame_equal(actual.clock.frame, saved->clock.frame, true))
        return false;
    return actual.kind == QA_APPLICATION_NATIVE_Q2_BUILTIN ? actual.source.game == saved->source.game :
        actual.source.original.host == saved->source.original.host &&
        actual.source.original.profile == saved->source.original.profile;
}
