#include "internal.h"
#include "qa/application_selected_q3_character.h"
#include "qa/game_q1_bots.h"
#include "qa/game_q3_source.h"
#include "qa/qc_visual.h"
#include <math.h>

static bool ready(const qa_application *app)
{
    return app && app->session && app->world && !app->destroy_requested &&
        app->state == QA_APPLICATION_RUNNING && app->map_view_ready &&
        !app->q3_round_active && !app->q3_world_restart && !app->routing_snapshot &&
        !app->frame_preparing &&
        (app->operation == APPLICATION_IDLE || app->operation == APPLICATION_ADVANCING) &&
        qa_session_safe(app->session) && !qa_session_faulted(app->session) &&
        qa_world_idle(app->world);
}

static bool provider_ready(const application_provider *provider)
{
    return provider && provider->constructed && provider->attached &&
        !provider->close_pending && provider->launch && provider->launch->content &&
        provider->product;
}

static bool original_visual(application_provider *source, qa_actor_id actor,
    float *scale, float *opacity, bool *intermission, qa_error *error)
{
    if (!provider_ready(source))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Selected Q3 character lost its actual world source");
    if (source->kind == APPLICATION_PROVIDER_Q1) {
        qa_q1_presentation visual;
        bool source_intermission;
        if (!application_source_intermission_read(source, &source_intermission, error)) return false;
        *intermission |= source_intermission;
        if (qa_q1_game_presentation(source->state.q1, actor, &visual)) {
            *scale = visual.scale == 0 ? 1 : visual.scale;
            *opacity = visual.alpha == 0 ? 1 : fmaxf(0, fminf(1, visual.alpha));
        }
    } else if (source->kind == APPLICATION_PROVIDER_Q2 || source->kind == APPLICATION_PROVIDER_Q3) {
        bool source_intermission;
        if (!application_source_intermission_read(source, &source_intermission, error)) return false;
        *intermission |= source_intermission;
    } else if (source->kind == APPLICATION_PROVIDER_QC && source->product->family == QA_GAME_Q1) {
        const qa_qc_instance *instance = source->state.qc.instance;
        if (!instance || !qa_qc_idle(instance))
            return application_fail(error, QA_ERROR_ARGUMENT, "Selected Q3 character needs its actual idle Quake source");
        bool source_intermission;
        if (!application_source_intermission_read(source, &source_intermission, error)) return false;
        *intermission |= source_intermission;
        for (uint32_t slot = 1; slot < qa_qc_entity_count(instance); ++slot) {
            qa_qc_slot_binding binding;
            if (!qa_qc_slot(instance, slot, &binding))
                return application_fail(error, QA_ERROR_FORMAT, "Quake character physical row is missing");
            if (binding.kind == QA_QC_SLOT_FREE || !qa_actor_id_equal(binding.actor, actor)) continue;
            qa_qc_visual visual;
            if (!qa_qc_visual_read(instance, slot, actor, &visual, error)) return false;
            *scale = visual.scale == 0 ? 1 : visual.scale;
            *opacity = visual.alpha == 0 ? 1 : fmaxf(0, fminf(1, visual.alpha));
            break;
        }
    }
    return true;
}

bool qa_application_selected_q3_character_read(qa_application *app, qa_actor_id actor,
    qa_application_selected_q3_character *out, bool *found, qa_error *error)
{
    if (!out || !found || !ready(app) || !qa_actors_get(qa_session_actors(app->session), actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Selected Q3 character needs its live full actor at a completed frame");
    *found = false;
    application_provider *provider = application_provider_for(app, actor, QA_ROLE_CHARACTER, "");
    if (!provider || provider->kind != APPLICATION_PROVIDER_Q3) return true;
    if (!provider_ready(provider) || !provider->state.q3 || !qa_q3_destroy_ready(provider->state.q3))
        return application_fail(error, QA_ERROR_ARGUMENT, "Selected Q3 character owner is not idle");
    qa_q3_player_state player;
    qa_player_state control;
    if (!qa_q3_player_read(provider->state.q3, actor, &player) ||
        !(player.selections & QA_Q3_CHARACTER) ||
        !qa_application_control_read(app, actor, &control) || app->controls[actor.slot].retired) return true;
    qa_application_selected_q3_character view = {.actor = actor, .provider = provider->owner,
        .game = provider->state.q3, .launch = provider->launch, .product = provider->product->id,
        .publication_generation = app->publication_generation, .map_revision = app->map_revision,
        .application_frame = application_frame_revision(app), .control_sequence = control.command_sequence,
        .view_angles = control.view_angles, .legs_animation = player.legs_animation,
        .torso_animation = player.torso_animation, .source_flags = player.flags,
        .movement_direction = control.state.kind == QA_RULESET_Q3 ? control.state.data.q3.movement_direction : 0,
        .scale = 1, .opacity = 1};
    qa_clock_state clock;
    if (!qa_session_clock(app->session, provider->owner, &clock) ||
        !qa_q3_source_clock(provider->state.q3, &view.source_time_ms, error) ||
        !qa_world_body_read(app->world, actor, &view.body, error))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Selected Q3 character lost its actual clock or body");
    view.source_frame = clock.frame;
    bool intermission = application_control_intermission(&control.state);
    if (!original_visual(application_world_provider(app, QA_ROLE_ENTITIES, ""), actor,
        &view.scale, &view.opacity, &intermission, error)) return false;
    view.present = !control.cutscene && !player.cutscene.active && !intermission &&
        !player.spectator && !player.gibbed && !(player.flags & 128u);
    if (!ready(app) || application_provider_for(app, actor, QA_ROLE_CHARACTER, "") != provider ||
        !qa_actors_get(qa_session_actors(app->session), actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Selected Q3 character changed during observation");
    *out = view; *found = true;
    return true;
}

static bool same_vector(qa_vec3 a, qa_vec3 b)
{ return a.x == b.x && a.y == b.y && a.z == b.z; }

bool qa_application_selected_q3_character_current(qa_application *app,
    const qa_application_selected_q3_character *saved)
{
    qa_application_selected_q3_character actual;
    bool found;
    return saved && qa_application_selected_q3_character_read(app, saved->actor, &actual, &found, NULL) && found &&
        actual.provider == saved->provider && actual.game == saved->game && actual.launch == saved->launch &&
        actual.product == saved->product && actual.publication_generation == saved->publication_generation &&
        actual.map_revision == saved->map_revision && actual.application_frame == saved->application_frame &&
        actual.control_sequence == saved->control_sequence && actual.source_time_ms == saved->source_time_ms &&
        same_vector(actual.body.origin, saved->body.origin) && same_vector(actual.body.velocity, saved->body.velocity) &&
        same_vector(actual.view_angles, saved->view_angles) && actual.movement_direction == saved->movement_direction &&
        actual.legs_animation == saved->legs_animation && actual.torso_animation == saved->torso_animation &&
        actual.source_flags == saved->source_flags && actual.scale == saved->scale && actual.opacity == saved->opacity &&
        actual.present == saved->present &&
        actual.source_frame.provider == saved->source_frame.provider &&
        actual.source_frame.kind == saved->source_frame.kind && actual.source_frame.phase == saved->source_frame.phase &&
        actual.source_frame.number == saved->source_frame.number && actual.source_frame.start_ns == saved->source_frame.start_ns &&
        actual.source_frame.time_ns == saved->source_frame.time_ns && actual.source_frame.elapsed_ns == saved->source_frame.elapsed_ns;
}
