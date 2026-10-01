#include "internal.h"
#include "guest_native_q2_private.h"
#include "guest_input_private.h"
#include "control_frame.h"
#include "client_outputs.h"
#include "guest_q2_control.h"
#include "native_q3_settings.h"
#include "qa/game_q3_source.h"
#include "qa/game_q3_wire.h"

#include <limits.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

typedef struct application_move_call {
    qa_application *application;
    application_control_record *control;
    qa_movement_input *input;
    application_provider *movement;
    application_provider *character;
    application_provider *arsenal;
    application_provider *effects;
    application_provider *world;
    application_provider *execution;
    application_control_context context;
    application_provider *q3[5];
    size_t q3_count;
    qa_q1_game_operation q1_operations[5];
    size_t q1_operation_count;
    qa_movement_command applied_command;
    const qa_vec3 *completed_view_angles;
    bool command_applied;
    bool qc_input_active;
    application_source_input_scope qc_command, qc_slice;
    bool committed;
    bool touched_triggers;
    bool q1_input;
    bool q1_prethink;
    bool q1_map_frame;
    bool mixed_source_outer;
    bool in_source_outer;
    uint64_t application_elapsed_ns;
} application_move_call;

struct application_control_turn {
    application_move_call move;
    qa_movement_input input;
    application_provider *parked_owners[14];
    struct application_qc_parked_input *parked[14];
    size_t parked_count;
};

static application_provider *control_execution(qa_application *app, qa_actor_id actor)
{
    qa_actor_owner owner;
    if (!qa_session_execution(app->session, actor, &owner)) return NULL;
    for (size_t i = 0; i < app->provider_count; ++i)
        if (app->providers[i]->owner == owner) return app->providers[i];
    return NULL;
}

static void end_q1_operations(application_move_call *move)
{
    while (move->q1_operation_count)
        qa_q1_game_operation_end(&move->q1_operations[--move->q1_operation_count]);
}

bool application_control_q1_world_begin(application_provider *provider,
                                         qa_q1_game_operation *operation, qa_error *error)
{
    qa_application *app = provider ? provider->application : NULL;
    application_provider *map = app ? application_world_provider(app, QA_ROLE_ENTITIES, "") : NULL;
    qa_source_frame frame; qa_clock_state retained;
    if (!provider || provider->kind != APPLICATION_PROVIDER_Q1 || !map || !map->component_attached)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q1 weapon clock needs its actual world source");
    if (!qa_session_active_frame(app->session, map->owner, &frame)) {
        if (!qa_session_clock(app->session, map->owner, &retained))
            return application_fail(error, QA_ERROR_ARGUMENT, "Q1 weapon clock lost its actual world interval");
        frame = retained.frame;
    }
    uint64_t time = frame.time_ns;
    if (frame.kind != provider->component.clock.kind &&
        frame.kind != QA_CLOCK_NETQUAKE && frame.kind != QA_CLOCK_QUAKEWORLD) {
        if (time < frame.elapsed_ns)
            return application_fail(error, QA_ERROR_ARGUMENT, "Q1 weapon clock has an invalid world interval");
        time -= frame.elapsed_ns;
    }
    return qa_q1_game_command_begin(provider->state.q1, time, frame.elapsed_ns, operation, error);
}

static bool begin_q1_operations(application_move_call *move, qa_error *error)
{
    application_provider *providers[] = {move->movement, move->character, move->arsenal,
                                         move->effects, move->world};
    for (size_t i = 0; i < sizeof(providers) / sizeof(providers[0]); ++i) {
        application_provider *provider = providers[i];
        if (!provider || provider->kind != APPLICATION_PROVIDER_Q1) continue;
        bool duplicate = false;
        for (size_t j = 0; j < move->q1_operation_count; ++j)
            duplicate |= move->q1_operations[j].game == provider->state.q1;
        bool weapon_clock = provider == move->arsenal && provider != move->world;
        if (!duplicate && !(weapon_clock ? application_control_q1_world_begin(provider,
                &move->q1_operations[move->q1_operation_count], error) :
                qa_q1_game_operation_begin(provider->state.q1,
                &move->q1_operations[move->q1_operation_count], error))) {
            end_q1_operations(move);
            return false;
        }
        if (!duplicate) ++move->q1_operation_count;
    }
    return true;
}

static bool source_think(application_move_call *move, qa_actor_id actor,
                           const qa_movement_call *call, qa_think_result *result, qa_error *error)
{
    qa_scheduler *scheduler = qa_session_scheduler(move->application->session);
    if (move->context.command_only) {
        uint64_t source_time; double source_elapsed;
        if (!move->execution || move->execution->kind != APPLICATION_PROVIDER_Q1 ||
            !qa_q1_game_clock_read(move->execution->state.q1, &source_time, &source_elapsed))
            return application_fail(error, QA_ERROR_ARGUMENT, "Native command think lost its source gameplay clock");
        return qa_scheduler_run_command_once(scheduler, actor, &move->context.command,
            source_time, move->context.path == APPLICATION_CONTROL_QW_GROUP
                ? (uint64_t)call->milliseconds * UINT64_C(1000000)
                : application_control_elapsed(&move->context),
            QA_THINK_DURING_PHYSICS, result, error);
    }
    return qa_scheduler_run_once(scheduler, actor, &move->context.frame,
        QA_THINK_DURING_PHYSICS, result, error);
}

enum {
    APPLICATION_Q1_ONGROUND = 512u,
    APPLICATION_Q2_ONGROUND = 4u,
};

static bool live(const qa_application *application, qa_actor_id actor)
{
    return application != NULL && application->session != NULL &&
           qa_actors_get(qa_session_actors(application->session), actor) != NULL;
}

bool application_control_output_admit(const qa_application *app, qa_actor_id actor,
    uint8_t channels, qa_error *error)
{
    if (!app || !channels || (channels & ~((1u << APPLICATION_CLIENT_OUTPUT_COUNT) - 1u)) ||
        actor.slot >= app->control_capacity || !live(app, actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Client outputs need a live admitted control");
    const application_control_record *control = &app->controls[actor.slot];
    if (!control->active || control->retired || !qa_actor_id_equal(control->actor, actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Client outputs have no selected movement owner");
    application_provider *source = application_world_provider((qa_application *)app, QA_ROLE_ENTITIES, "");
    if (!source || !source->constructed || !source->attached || source->close_pending)
        return application_fail(error, QA_ERROR_ARGUMENT, "Client outputs have no actual source owner");
    if ((channels & (1u << APPLICATION_CLIENT_STANCE)) &&
        (control->state.kind == QA_MOVEMENT_NETQUAKE ||
         (control->state.kind == QA_MOVEMENT_QUAKEWORLD &&
          source->kind == APPLICATION_PROVIDER_QC && !source->state.qc.qualified &&
          source->component.clock.kind == QA_CLOCK_QUAKEWORLD)))
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Original Quake movement has no crouch-request interface");
    if (source->kind == APPLICATION_PROVIDER_QVM ||
        (source->kind == APPLICATION_PROVIDER_NATIVE && source->component.clock.kind == QA_CLOCK_Q3))
        return application_arsenal_guest_output_admit(source, channels, error);
    if ((channels & (1u << APPLICATION_CLIENT_BODY_SHAPE)) &&
        source->kind == APPLICATION_PROVIDER_NATIVE && source->state.native.q2_engine &&
        source->state.native.q2_engine->profile == QA_NATIVE_Q2_GAME_API2023 &&
        !application_q2_control_body_admitted(source->state.native.q2_engine))
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Original rerelease movement has no admitted body trace interface");
    return true;
}

bool application_control_outputs(const qa_application *app, qa_actor_id actor,
    application_client_outputs *out, qa_error *error)
{
    if (!app || !out || actor.slot >= app->control_capacity || !live(app, actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Client output use needs its actual live actor");
    *out = (application_client_outputs){0};
    const application_control_record *record = &app->controls[actor.slot];
    qa_combat_state combat;
    if (!record->active || !qa_actor_id_equal(record->actor, actor) ||
        !qa_combat_read_traits(app->combat, actor, &combat, error)) return false;
    if (combat.health <= 0 || record->cutscene) return true;
    application_provider *source = application_world_provider((qa_application *)app, QA_ROLE_ENTITIES, "");
    if (!source || !source->constructed || !source->attached || source->close_pending)
        return application_fail(error, QA_ERROR_ARGUMENT, "Client output use lost its actual source owner");
    if (source->kind == APPLICATION_PROVIDER_QVM ||
        (source->kind == APPLICATION_PROVIDER_NATIVE && source->component.clock.kind == QA_CLOCK_Q3))
        return application_arsenal_guest_outputs(source, actor, out, error);
    if (source->kind == APPLICATION_PROVIDER_NATIVE && source->state.native.q2_engine)
        return application_q2_control_outputs(source->state.native.q2_engine, actor, out, error);
    if (source->kind == APPLICATION_PROVIDER_Q2) {
        qa_q2_player_info player;
        if (qa_q2_players_in_intermission(source->state.q2) ||
            (qa_q2_player_read(source->state.q2, actor, &player) && player.chase_target.registry)) return true;
    } else if (source->kind == APPLICATION_PROVIDER_Q3) {
        qa_q3_source_match_state match;
        if (!qa_q3_source_match_state_read(source->state.q3, &match, error)) return false;
        if (match.intermission_time_ms) return true;
    }
    return application_qc_control_outputs(app, actor, out, error);
}

static qa_movement_kind movement_kind(qa_clock_kind kind)
{
    switch (kind) {
    case QA_CLOCK_NETQUAKE:
        return QA_MOVEMENT_NETQUAKE;
    case QA_CLOCK_QUAKEWORLD:
        return QA_MOVEMENT_QUAKEWORLD;
    case QA_CLOCK_Q2_CLASSIC:
        return QA_MOVEMENT_Q2_CLASSIC;
    case QA_CLOCK_Q2_RERELEASE:
        return QA_MOVEMENT_Q2_RERELEASE;
    case QA_CLOCK_Q3:
        return QA_MOVEMENT_Q3;
    }
    return QA_MOVEMENT_NETQUAKE;
}

static qa_collision_family movement_family(qa_movement_kind kind)
{
    if (kind == QA_MOVEMENT_NETQUAKE || kind == QA_MOVEMENT_QUAKEWORLD)
        return QA_COLLISION_Q1;
    return kind == QA_MOVEMENT_Q3 ? QA_COLLISION_Q3 : QA_COLLISION_Q2;
}

static const qa_launch_snapshot *active_snapshot(const qa_application *application)
{
    if (application == NULL)
        return NULL;
    if (application->routing_snapshot != NULL)
        return application->routing_snapshot;
    return application->configuration == NULL
               ? NULL
               : qa_configuration_current(application->configuration);
}

static const qa_product *provider_product(const qa_application *application,
                                          const application_provider *provider)
{
    const qa_launch_snapshot *snapshot = active_snapshot(application);
    return snapshot == NULL || provider == NULL || provider->launch == NULL
               ? NULL
               : qa_catalog_product(qa_launch_snapshot_catalog(snapshot),
                                    provider->launch->selection.product);
}

static qa_movement_profile selected_profile(
    const qa_application *application, const application_provider *provider)
{
    qa_movement_kind kind = movement_kind(provider->component.clock.kind);
    qa_movement_profile profile = qa_movement_profile_default(kind);
    const qa_product *product = provider_product(application, provider);
    if (kind == QA_MOVEMENT_NETQUAKE && product != NULL)
        profile.data.nq.edition =
            strcmp(product->campaign, "quake64") == 0
                ? QA_Q1_QUAKE64
                : product->edition == QA_EDITION_RERELEASE
                      ? QA_Q1_RERELEASE
                      : QA_Q1_CLASSIC;
    else if (kind == QA_MOVEMENT_QUAKEWORLD)
        profile.data.qw.shared_controls = true;
    else if (kind == QA_MOVEMENT_Q2_RERELEASE && product != NULL)
        profile.data.q2r.n64_physics = strcmp(product->campaign, "n64") == 0;
    else if (kind == QA_MOVEMENT_Q3 && product != NULL)
        profile.data.q3.missionpack =
            strcmp(product->campaign, "missionpack") == 0;
    return profile;
}

static int32_t gravity_integer(float gravity)
{
    if (gravity >= (float)INT32_MAX)
        return INT32_MAX;
    if (gravity <= (float)INT32_MIN)
        return INT32_MIN;
    return (int32_t)lrintf(gravity);
}

static int16_t gravity_short(float gravity)
{
    int32_t value = gravity_integer(gravity);
    return value > INT16_MAX   ? INT16_MAX
           : value < INT16_MIN ? INT16_MIN
                               : (int16_t)value;
}

static void state_body(application_control_record *record,
                       const qa_body_state *body, float health)
{
    (void)qa_movement_set_origin(&record->state, body->origin, NULL);
    (void)qa_movement_set_velocity(&record->state, body->velocity, NULL);
    switch (record->state.kind) {
    case QA_MOVEMENT_NETQUAKE:
        record->state.data.nq.angles = body->angles;
        record->state.data.nq.health = health;
        if (body->ground.registry != 0)
            record->ground = (qa_movement_ground){.hit = QA_TRACE_HIT_ACTOR,
                                                   .actor = body->ground};
        record->state.data.nq.ground = record->ground;
        if (record->ground.hit == QA_TRACE_HIT_NONE)
            record->state.data.nq.flags &= ~APPLICATION_Q1_ONGROUND;
        else
            record->state.data.nq.flags |= APPLICATION_Q1_ONGROUND;
        break;
    case QA_MOVEMENT_QUAKEWORLD:
        record->state.data.qw.angles = record->view_angles;
        record->state.data.qw.dead = health <= 0;
        if (body->ground.registry != 0)
            record->ground = (qa_movement_ground){.hit = QA_TRACE_HIT_ACTOR,
                                                   .actor = body->ground};
        record->state.data.qw.ground = record->ground;
        break;
    case QA_MOVEMENT_Q2_CLASSIC:
        record->state.data.q2.gravity = gravity_short(
            record->application->physics == NULL
                ? 800.0f
                : record->application->physics->gravity *
                      record->gravity_multiplier);
        if (record->ground.hit == QA_TRACE_HIT_NONE)
            record->state.data.q2.flags &= ~APPLICATION_Q2_ONGROUND;
        else
            record->state.data.q2.flags |= APPLICATION_Q2_ONGROUND;
        break;
    case QA_MOVEMENT_Q2_RERELEASE:
        record->state.data.q2r.gravity = gravity_short(
            record->application->physics == NULL
                ? 800.0f
                : record->application->physics->gravity *
                      record->gravity_multiplier);
        if (record->ground.hit == QA_TRACE_HIT_NONE)
            record->state.data.q2r.flags &= ~APPLICATION_Q2_ONGROUND;
        else
            record->state.data.q2r.flags |= APPLICATION_Q2_ONGROUND;
        break;
    case QA_MOVEMENT_Q3:
        {
            const application_control_context *source = application_control_frame_current(record->application, record->actor);
            if (!source || !source->source_usercmd)
                record->state.data.q3.gravity = gravity_integer(
                    record->application->physics == NULL ? 800.0f :
                    record->application->physics->gravity * record->gravity_multiplier);
        }
        if (body->ground.registry != 0)
            record->ground = (qa_movement_ground){.hit = QA_TRACE_HIT_ACTOR,
                                                   .actor = body->ground};
        record->state.data.q3.ground = record->ground;
        break;
    }
}

static bool same_vector(qa_vec3 left, qa_vec3 right)
{
    return left.x == right.x && left.y == right.y && left.z == right.z;
}

static bool same_bounds(qa_bounds left, qa_bounds right)
{
    return same_vector(left.mins, right.mins) &&
           same_vector(left.maxs, right.maxs);
}

static qa_actor_id result_ground(const qa_movement_ground *ground)
{
    return ground->hit == QA_TRACE_HIT_ACTOR ? ground->actor
                                             : (qa_actor_id){0};
}

static qa_vec3 result_angles(const qa_movement_result *result,
                             qa_vec3 fallback)
{
    if (result->state.kind == QA_MOVEMENT_NETQUAKE)
        return result->state.data.nq.angles;
    if (result->state.kind == QA_MOVEMENT_QUAKEWORLD)
        return result->state.data.qw.angles;
    return (qa_vec3){0, result->view_angles.y, fallback.z};
}

static bool publish_result_body(application_move_call *move,
                              qa_movement_state *state, qa_bounds bounds,
                              qa_movement_ground ground,
                              qa_vec3 view_angles, bool triggers,
                              bool link, qa_error *error)
{
    qa_application *application = move->application;
    qa_actor_id actor = move->control->actor;
    if (!live(application, actor))
        return true;
    qa_body_state body;
    if (!qa_world_body_read(application->world, actor, &body, error))
        return false;
    body.origin = qa_movement_origin(state);
    body.velocity = qa_movement_velocity(state);
    body.bounds = bounds;
    body.ground = result_ground(&ground);
    body.angles = state->kind == QA_MOVEMENT_NETQUAKE
                      ? state->data.nq.angles
                  : state->kind == QA_MOVEMENT_QUAKEWORLD
                      ? state->data.qw.angles
                      : (qa_vec3){0, view_angles.y, body.angles.z};
    if (move->execution && move->execution->kind == APPLICATION_PROVIDER_QC &&
        !application_qc_control_body(move->execution, actor, state, &body, error)) return false;
    move->committed = true;
    if (!qa_world_body_write(application->world, actor, &body, error)) return false;
    if ((link && !qa_world_link(application->world, actor, NULL, error)) ||
        (triggers && !qa_physics_touch_triggers(application->physics, actor, error))) return false;
    if (triggers)
        move->touched_triggers = true;
    if (!live(application, actor))
        return true;

    qa_body_state resumed;
    qa_combat_state combat;
    if (!qa_world_body_read(application->world, actor, &resumed, error) ||
        !qa_combat_read_traits(application->combat, actor, &combat, error))
        return false;
    application_control_record shadow = *move->control;
    shadow.state = *state;
    shadow.ground = ground;
    state_body(&shadow, &resumed, combat.health);
    if (move->execution && move->execution->kind == APPLICATION_PROVIDER_QC &&
        !move->execution->state.qc.qualified && !application_qc_control_state(move->execution,
            actor, &shadow.state, &bounds, &move->input->environment, &move->control->view_angles, error)) return false;
    *state = shadow.state;
    return true;
}

static bool refresh_source_call(application_move_call *move, qa_movement_call *call, qa_error *error)
{
    qa_body_state body; qa_combat_state combat;
    if (!live(move->application, call->actor)) return true;
    if (!qa_world_body_read(move->application->world, call->actor, &body, error) ||
        !qa_combat_read_traits(move->application->combat, call->actor, &combat, error)) return false;
    application_control_record shadow = *move->control;
    shadow.state = *call->state;
    state_body(&shadow, &body, combat.health);
    *call->state = shadow.state; *call->bounds = body.bounds;
    call->environment->health = combat.health;
    call->environment->has_body_bounds = false;
    call->environment->has_mode = move->control->player_mode_set &&
        !(move->context.source_usercmd && call->state->kind == QA_MOVEMENT_Q3);
    call->environment->mode = move->control->player_mode;
    call->environment->flight = move->control->flight;
    if (move->execution && move->execution->kind == APPLICATION_PROVIDER_QC &&
        !move->execution->state.qc.qualified && !application_qc_control_state(move->execution,
            call->actor, call->state, call->bounds, call->environment, &move->control->view_angles, error)) return false;
    application_client_outputs outputs;
    if (!application_control_outputs(move->application, call->actor, &outputs, error) ||
        !application_control_body_request(move->application, call->actor, body.bounds, &outputs, error)) return false;
    call->environment->has_body_bounds = outputs.has_body_bounds;
    if (outputs.has_body_bounds) call->environment->body_bounds = outputs.body_bounds;
    if (outputs.has_mode) { call->environment->has_mode = true; call->environment->mode = outputs.mode; }
    call->environment->has_stance = outputs.has_stance; call->environment->crouched = outputs.crouched;
    call->state_replaced = true;
    return true;
}

static bool move_trace(void *opaque, const qa_trace_query *query,
                       qa_trace_result *out, qa_error *error)
{
    application_move_call *move = opaque;
    return qa_world_trace(move->application->world, query, out, error);
}

static bool move_contents(void *opaque, const qa_point_query *query,
                          qa_point_contents *out, qa_error *error)
{
    application_move_call *move = opaque;
    return qa_world_point_contents(move->application->world, query, out,
                                   error);
}

static bool q3_present(application_provider *provider, qa_actor_id actor)
{
    qa_q3_player_state ignored;
    return provider != NULL && provider->kind == APPLICATION_PROVIDER_Q3 &&
           qa_q3_player_read(provider->state.q3, actor, &ignored);
}

static void add_q3(application_move_call *move,
                   application_provider *provider)
{
    if (!q3_present(provider, move->control->actor))
        return;
    for (size_t index = 0; index < move->q3_count; ++index)
        if (move->q3[index] == provider)
            return;
    move->q3[move->q3_count++] = provider;
}

static qa_movement_control callback_result(application_move_call *move,
                                           bool ok)
{
    if (!ok)
        return QA_MOVEMENT_ERROR;
    return live(move->application, move->control->actor)
               ? QA_MOVEMENT_CONTINUE
               : QA_MOVEMENT_REMOVED;
}

static bool command_jump(const qa_movement_command *command)
{
    if (command->kind == QA_MOVEMENT_NETQUAKE || command->kind == QA_MOVEMENT_QUAKEWORLD)
        return (command->buttons & 2u) != 0;
    return command->up_move > 0 || (command->kind == QA_MOVEMENT_Q2_RERELEASE &&
                                  (command->buttons & 8u) != 0);
}

static qa_q1_input q1_input(const qa_movement_call *call)
{
    return (qa_q1_input){
        .view_angles = call->state->kind == QA_MOVEMENT_NETQUAKE
                           ? call->state->data.nq.view_angles
                           : call->state->kind == QA_MOVEMENT_QUAKEWORLD
                                 ? call->state->data.qw.angles
                                 : call->command->angles,
        .attack = (call->command->buttons & 1u) != 0,
        .jump = command_jump(call->command),
        .use = (call->command->buttons & 4u) != 0,
        .impulse = call->command->impulse,
        .water_level = (uint8_t)(*call->water_level < 0
                                     ? 0
                                     : *call->water_level > 3
                                           ? 3
                                           : *call->water_level),
        .water_type = *call->water_type,
    };
}

static qa_vec3 call_view_angles(const application_move_call *move, const qa_movement_call *call)
{
    if (call->state->kind == QA_MOVEMENT_NETQUAKE)
        return call->state->data.nq.view_angles;
    if (call->state->kind == QA_MOVEMENT_QUAKEWORLD)
        return call->state->data.qw.angles;
    if (call->state->kind == QA_MOVEMENT_Q3)
        return call->state->data.q3.view_angles;
    if (move->completed_view_angles)
        return *move->completed_view_angles;
    return call->command->angles;
}

typedef struct application_q1_source_input_call {
    application_provider *provider;
    qa_actor_id actor;
    qa_q1_input input;
} application_q1_source_input_call;

static bool q1_source_prethink(void *opaque, qa_session *session, qa_error *error)
{
    application_q1_source_input_call *call = opaque;
    application_provider *provider = call->provider;
    if (session != provider->application->session)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q1 source input lost its session");
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(provider->state.q1, &operation, error)) return false;
    bool okay = qa_q1_player_source_input(provider->state.q1, call->actor, &call->input, error);
    if (okay && qa_actors_get(qa_session_actors(session), call->actor))
        okay = qa_q1_player_prethink(provider->state.q1, call->actor, error);
    qa_q1_game_operation_end(&operation);
    return okay;
}

bool application_control_q1_source_prethink(qa_application *app, qa_actor_id actor,
                                              const qa_movement_command *command, qa_error *error)
{
    application_provider *map = application_world_provider(app, QA_ROLE_ENTITIES, "");
    qa_source_frame actual;
    if (!map || map->kind != APPLICATION_PROVIDER_Q1 ||
        !qa_q1_player_source_present(map->state.q1, actor) ||
        !qa_session_active_frame(app->session, map->owner, &actual) ||
        application_control_frames_q1_prepared(app, actor, map->owner) ||
        !application_control_frames_q1_command_ready(app, actor, map->owner, command)) return true;
    application_control_record *record = &app->controls[actor.slot];
    qa_vec3 angles = command ? command->angles : record->view_angles;
    if (command && (command->kind == QA_MOVEMENT_Q3 || command->kind == QA_MOVEMENT_Q2_CLASSIC)) {
        float aim[3];
        for (size_t i = 0; i < 3; ++i) {
            int32_t delta = command->kind == QA_MOVEMENT_Q3 ? record->state.data.q3.delta_angle_words[i]
                : record->state.data.q2.delta_angle_shorts[i];
            aim[i] = (float)(uint16_t)((uint32_t)command->angle_words[i] + (uint32_t)delta) *
                (360.0f / 65536.0f);
        }
        angles = qa_v3(aim[0], aim[1], aim[2]);
    } else if (command && command->kind == QA_MOVEMENT_Q2_RERELEASE)
        angles = qa_vec_add(angles, record->state.data.q2r.delta_angles);
    uint32_t buttons = command ? command->buttons : record->buttons;
    application_q1_source_input_call call = {.provider = map, .actor = actor, .input = {
        .view_angles = angles, .attack = !record->cutscene && (buttons & 1u) != 0,
        .jump = !record->cutscene && (command ? command_jump(command) : (buttons & 2u) != 0),
        .use = (buttons & 4u) != 0, .impulse = command ? command->impulse : 0,
        .water_level = (uint8_t)(record->water_level < 0 ? 0 : record->water_level > 3 ? 3 : record->water_level),
        .water_type = record->water_type}};
    if (!qa_session_invoke(app->session, actor, QA_INVOKE_PHYSICS, q1_source_prethink, &call, error)) return false;
    return !live(app, actor) || application_control_frames_q1_complete(app, actor, map->owner, error);
}

static int32_t input_angle_word(float degrees)
{
    return (int32_t)(uint16_t)(int32_t)(fmodf(degrees, 360.0f) * (65536.0f / 360.0f));
}

static bool qc_input(application_move_call *move, qa_movement_state *state,
                       qa_movement_command *command, const qa_vec3 *absolute_aim,
                       application_source_input_scope *scope,
                       bool before, bool slice,
                       uint64_t elapsed_ns, qa_error *error)
{
    static const qa_launch_role roles[] = {QA_ROLE_MOVEMENT, QA_ROLE_CHARACTER, QA_ROLE_ARSENAL,
        QA_ROLE_INVENTORY, QA_ROLE_COMBAT, QA_ROLE_EFFECTS, QA_ROLE_EQUIPMENT};
    application_provider *owners[sizeof(roles) / sizeof(roles[0])];
    if (before && scope->count)
        return application_fail(error, QA_ERROR_ARGUMENT, "Source input scope is already open");
    for (size_t i = 0; i < sizeof(roles) / sizeof(roles[0]); ++i)
        owners[i] = before ? application_provider_for(move->application, move->control->actor, roles[i], "")
                          : i < scope->count ? scope->owners[i] : NULL;
    bool has_qc = false;
    for (size_t i = 0; i < sizeof(owners) / sizeof(owners[0]); ++i)
        has_qc |= owners[i] && owners[i]->kind == APPLICATION_PROVIDER_QC;
    if (!has_qc) return true;
    if (before) { scope->actor = move->control->actor; scope->slice = slice; }
    move->qc_input_active = true;
    qa_movement_command semantic = *command;
    if (absolute_aim) semantic.angles = *absolute_aim;
    else if (command->kind == state->kind &&
        (state->kind == QA_MOVEMENT_Q3 || state->kind == QA_MOVEMENT_Q2_CLASSIC)) {
        float aim[3];
        for (size_t i = 0; i < 3; ++i) {
            int32_t delta = state->kind == QA_MOVEMENT_Q3 ? state->data.q3.delta_angle_words[i]
                                                       : state->data.q2.delta_angle_shorts[i];
            aim[i] = (float)(uint16_t)((uint32_t)command->angle_words[i] + (uint32_t)delta) *
                     (360.0f / 65536.0f);
        }
        semantic.angles = qa_v3(aim[0], aim[1], aim[2]);
    } else if (command->kind == state->kind && state->kind == QA_MOVEMENT_Q2_RERELEASE)
        semantic.angles = qa_vec_add(command->angles, state->data.q2r.delta_angles);
    qa_vec3 original_aim = semantic.angles;
    for (size_t i = 0; i < sizeof(owners) / sizeof(owners[0]); ++i) {
        application_provider *owner = owners[i];
        if (!owner || owner->kind != APPLICATION_PROVIDER_QC) continue;
        if (!owner->state.qc.qualified && move->context.path == APPLICATION_CONTROL_QW_GROUP && owner == move->execution)
            continue;
        if (!owner->state.qc.qualified && move->context.path == APPLICATION_CONTROL_NQ_TURN &&
            owner == move->execution && (!before || slice || move->context.stage != APPLICATION_CONTROL_PREPARE))
            continue;
        bool duplicate = false;
        for (size_t j = 0; j < i; ++j) duplicate |= owners[j] == owner;
        if (duplicate) continue;
        move->committed = true;
        if (!before) scope->owners[i] = NULL;
        if (!application_qc_input(owner, move->control->actor, &semantic,
                                   before, slice, elapsed_ns, error)) {
            qa_error cleanup = {0};
            (void)application_control_source_abort(scope, &cleanup);
            return false;
        }
        if (before) scope->owners[scope->count++] = owner;
        if (!live(move->application, move->control->actor)) {
            return application_control_source_abort(scope, error);
        }
    }
    if (!before) *scope = (application_source_input_scope){0};
    bool changed_aim = !same_vector(original_aim, semantic.angles);
    if (changed_aim && command->kind == state->kind &&
        (state->kind == QA_MOVEMENT_Q3 || state->kind == QA_MOVEMENT_Q2_CLASSIC)) {
        const float angles[] = {semantic.angles.x, semantic.angles.y, semantic.angles.z};
        for (size_t i = 0; i < 3; ++i) {
            int32_t delta = state->kind == QA_MOVEMENT_Q3 ? state->data.q3.delta_angle_words[i]
                                                       : state->data.q2.delta_angle_shorts[i];
            uint32_t word = (uint32_t)input_angle_word(angles[i]) - (uint32_t)delta;
            if (state->kind == QA_MOVEMENT_Q3)
                memcpy(&semantic.angle_words[i], &word, sizeof(word));
            else semantic.angle_words[i] = (int32_t)(uint16_t)word;
        }
    }
    semantic.angles = changed_aim ? command->kind == state->kind && state->kind == QA_MOVEMENT_Q2_RERELEASE
        ? qa_vec_sub(semantic.angles, state->data.q2r.delta_angles) : semantic.angles : command->angles;
    *command = semantic;
    return true;
}

bool application_control_source_input(qa_application *application, qa_actor_id actor,
                                       qa_movement_state *state, qa_movement_command *command,
                                       const qa_vec3 *absolute_aim,
                                       application_source_input_scope *scope,
                                       bool before, bool slice, uint64_t elapsed_ns,
                                       qa_error *error)
{
    if (!live(application, actor) || !state || !command || !scope ||
        actor.slot >= application->control_capacity)
        return application_fail(error, QA_ERROR_ARGUMENT, "Source input needs a live admitted continuation");
    application_control_record *record = &application->controls[actor.slot];
    if (!record->active || record->retired || !qa_actor_id_equal(record->actor, actor))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Source input continuation is not current");
    application_move_call move = {.application = application, .control = record,
        .movement = application_provider_for(application, actor, QA_ROLE_MOVEMENT, ""),
        .character = application_provider_for(application, actor, QA_ROLE_CHARACTER, ""),
        .arsenal = application_provider_for(application, actor, QA_ROLE_ARSENAL, ""),
        .effects = application_provider_for(application, actor, QA_ROLE_EFFECTS, "")};
    return qc_input(&move, state, command, absolute_aim, scope, before, slice, elapsed_ns, error);
}

bool application_control_source_abort(application_source_input_scope *scope, qa_error *error)
{
    if (!scope)
        return application_fail(error, QA_ERROR_ARGUMENT, "Source input cleanup needs its scope");
    bool ok = true;
    qa_error first = {0};
    while (scope->count) {
        application_provider *provider = scope->owners[--scope->count];
        scope->owners[scope->count] = NULL;
        if (!provider) continue;
        qa_error current = {0};
        if (!application_qc_input_abort(provider, scope->actor, scope->slice, &current) && ok) {
            ok = false; first = current;
        }
    }
    *scope = (application_source_input_scope){0};
    if (!ok && error) *error = first;
    return ok;
}

static qa_movement_control move_phase(void *opaque, qa_movement_phase phase,
                                      qa_movement_call *call,
                                      qa_error *error)
{
    application_move_call *move = opaque;
    qa_actor_id actor = move->control->actor;
    application_control_frames_state(move->application, actor, call->state);
    bool qw_spectator = move->context.path == APPLICATION_CONTROL_QW_GROUP &&
        call->state->kind == QA_MOVEMENT_QUAKEWORLD && call->state->data.qw.spectator != 0;
    if (phase == QA_MOVE_INPUT_ABORT) {
        qa_error cleanup = {0};
        bool ok = application_control_source_abort(&move->qc_slice, &cleanup);
        if (!ok && error && error->code == QA_OK) *error = cleanup;
        return !ok ? QA_MOVEMENT_ERROR : live(move->application, actor)
            ? QA_MOVEMENT_CONTINUE : QA_MOVEMENT_REMOVED;
    }
    if (!live(move->application, actor)) {
        if (phase == QA_MOVE_INPUT_END)
            (void)application_control_source_abort(&move->qc_slice, error);
        return QA_MOVEMENT_REMOVED;
    }
    bool source_phase = phase == QA_MOVE_PRETHINK || phase == QA_MOVE_THINK || phase == QA_MOVE_POSTTHINK;
    if (source_phase) {
        qa_body_state source_body;
        if (!qa_world_body_read(move->application->world, actor, &source_body, error)) return QA_MOVEMENT_ERROR;
        qa_movement_ground ground = call->state->kind == QA_MOVEMENT_NETQUAKE ? call->state->data.nq.ground
            : call->state->kind == QA_MOVEMENT_QUAKEWORLD ? call->state->data.qw.ground
            : source_body.ground.registry ? (qa_movement_ground){.hit = QA_TRACE_HIT_ACTOR, .actor = source_body.ground}
            : (qa_movement_ground){0};
        if (!publish_result_body(move, call->state, *call->bounds, ground,
            call_view_angles(move, call), false, false, error)) return QA_MOVEMENT_ERROR;
        if (!live(move->application, actor)) return QA_MOVEMENT_REMOVED;
        if ((!move->mixed_source_outer || move->in_source_outer) &&
            move->execution && move->execution->kind == APPLICATION_PROVIDER_QC &&
            !move->execution->state.qc.qualified &&
            !(phase == QA_MOVE_POSTTHINK && move->context.defer_postthink)) {
            move->committed = true;
            if (!application_qc_control_phase(move->execution, actor, move->context.path,
                phase, &move->context, call, error)) return QA_MOVEMENT_ERROR;
            if (!live(move->application, actor)) return QA_MOVEMENT_REMOVED;
        }
        if (move->execution && move->execution->kind == APPLICATION_PROVIDER_Q1 && phase == QA_MOVE_THINK) {
            qa_think_result result;
            move->committed = true;
            if (!source_think(move, actor, call, &result, error)) return QA_MOVEMENT_ERROR;
            if (!live(move->application, actor)) return QA_MOVEMENT_REMOVED;
        }
        if (!refresh_source_call(move, call, error)) return QA_MOVEMENT_ERROR;
    }
    if (!move->context.source_input_applied && (phase == QA_MOVE_INPUT_BEGIN || phase == QA_MOVE_INPUT_END)) {
        uint64_t elapsed_ns = call->state->kind == QA_MOVEMENT_NETQUAKE
            ? move->input->elapsed_ns : (uint64_t)call->milliseconds * UINT64_C(1000000);
        if (!qc_input(move, call->state, call->command, NULL, &move->qc_slice, phase == QA_MOVE_INPUT_BEGIN,
                        true, elapsed_ns, error)) return QA_MOVEMENT_ERROR;
        if (!live(move->application, actor)) return QA_MOVEMENT_REMOVED;
        if (phase == QA_MOVE_INPUT_END && move->qc_input_active) {
            move->applied_command = *call->command;
            move->command_applied = true;
        }
    }

    if (phase == QA_MOVE_INPUT_BEGIN && !move->q1_input &&
        move->arsenal != NULL &&
        move->arsenal->kind == APPLICATION_PROVIDER_Q1) {
        qa_q1_player_view view;
        if (qa_q1_player_read(move->arsenal->state.q1, actor, &view)) {
            qa_q1_input input = q1_input(call);
            input.view_angles = call_view_angles(move, call);
            move->committed = true;
            if (!qa_q1_player_input(move->arsenal->state.q1, actor, &input,
                                    error))
                return QA_MOVEMENT_ERROR;
        }
        move->q1_input = true;
    }
    if (phase == QA_MOVE_PRETHINK && !qw_spectator && !move->q1_prethink &&
        move->world && move->world->kind == APPLICATION_PROVIDER_Q1 &&
        qa_q1_player_source_present(move->world->state.q1, actor)) {
        move->committed = true;
        if (!application_control_q1_source_prethink(move->application, actor, call->command, error))
            return QA_MOVEMENT_ERROR;
        move->q1_prethink = true;
        move->q1_map_frame = true;
    }
    if (phase == QA_MOVE_PRETHINK && !qw_spectator && !move->q1_map_frame) {
        if (!live(move->application, actor))
            return QA_MOVEMENT_REMOVED;
        application_provider *map = application_world_provider(move->application,
                                                                QA_ROLE_ENTITIES, "");
        move->q1_map_frame = true;
        if (map && map->constructed && map->kind == APPLICATION_PROVIDER_Q1 &&
            map != move->arsenal) {
            move->committed = true;
            if (!qa_q1_game_map_addon_player_frame(map->state.q1, actor,
                                                    move->control->view_offset, error))
                return QA_MOVEMENT_ERROR;
        }
    }
    if (phase == QA_MOVE_PRETHINK && !qw_spectator && move->execution && move->execution->kind == APPLICATION_PROVIDER_Q1 &&
        move->context.path != APPLICATION_CONTROL_NQ_TURN && call->state->kind != QA_MOVEMENT_NETQUAKE &&
        (move->context.path != APPLICATION_CONTROL_MIXED || move->in_source_outer)) {
        qa_think_result result; move->committed = true;
        if (!source_think(move, actor, call, &result, error)) return QA_MOVEMENT_ERROR;
        if (!live(move->application, actor)) return QA_MOVEMENT_REMOVED;
    }
    if (phase == QA_MOVE_WEAPON &&
        !qw_spectator &&
        !move->context.defer_postthink &&
        move->arsenal != NULL &&
        move->arsenal->kind == APPLICATION_PROVIDER_Q1) {
        qa_q1_player_view view;
        if (qa_q1_player_read(move->arsenal->state.q1, actor, &view)) {
            qa_q1_input input = q1_input(call);
            input.view_angles = call_view_angles(move, call);
            move->committed = true;
            if (!qa_q1_player_input(move->arsenal->state.q1, actor, &input, error) ||
                !qa_q1_player_postthink(move->arsenal->state.q1, actor,
                                        error))
                return QA_MOVEMENT_ERROR;
        }
    }

    if (phase == QA_MOVE_LINK || phase == QA_MOVE_LINK_TRIGGERS) {
        qa_movement_ground ground = move->control->ground;
        if (call->state->kind == QA_MOVEMENT_NETQUAKE)
            ground = call->state->data.nq.ground;
        else if (call->state->kind == QA_MOVEMENT_QUAKEWORLD)
            ground = call->state->data.qw.ground;
        else if (call->state->kind == QA_MOVEMENT_Q3)
            ground = call->state->data.q3.ground;
        if (!publish_result_body(move, call->state, *call->bounds, ground,
            call_view_angles(move, call), false, false, error)) return QA_MOVEMENT_ERROR;
        if (move->execution && move->execution->kind == APPLICATION_PROVIDER_QC &&
            !move->execution->state.qc.qualified && !application_qc_control_phase(move->execution, actor,
                move->context.path, phase, &move->context, call, error)) return QA_MOVEMENT_ERROR;
        if (!live(move->application, actor)) return QA_MOVEMENT_REMOVED;
        if (!move->context.source_usercmd &&
            (!qa_world_link(move->application->world, actor, NULL, error) ||
             (phase == QA_MOVE_LINK_TRIGGERS && !qa_physics_touch_triggers(move->application->physics, actor, error))))
            return QA_MOVEMENT_ERROR;
        if (!move->context.source_usercmd && phase == QA_MOVE_LINK_TRIGGERS) move->touched_triggers = true;
        if (!live(move->application, actor))
            return QA_MOVEMENT_REMOVED;
        if (!refresh_source_call(move, call, error)) return QA_MOVEMENT_ERROR;
    }

    if (source_phase && !refresh_source_call(move, call, error)) return QA_MOVEMENT_ERROR;

    for (size_t index = 0; index < move->q3_count; ++index) {
        move->committed = true;
        qa_movement_control result = qa_q3_movement_phase_selected(
            move->q3[index]->state.q3, phase, call, move->q3[index] == move->arsenal, error);
        if (result != QA_MOVEMENT_CONTINUE)
            return result;
    }

    if (phase == QA_MOVE_WEAPON && move->arsenal != NULL &&
        move->arsenal->kind == APPLICATION_PROVIDER_Q3 &&
        call->command->kind != QA_MOVEMENT_Q3 &&
        q3_present(move->arsenal, actor)) {
        qa_q3_controls controls = {
            .attack = (call->command->buttons & 1u) != 0,
            .use_holdable = move->context.source_usercmd ? move->context.source_holdable :
                (call->command->buttons & 4u) != 0,
            .requested_weapon = (qa_q3_weapon)call->command->weapon,
            .prediction = call->prediction,
        };
        if (move->context.source_usercmd && move->arsenal == move->world) {
            qa_q3_player_state source;
            if (!qa_q3_player_read(move->arsenal->state.q3, actor, &source))
                return QA_MOVEMENT_REMOVED;
            controls.gauntlet_contact_known = true; controls.gauntlet_contact = source.gauntlet_contact;
        }
        move->committed = true;
        if (!qa_q3_player_set_view(move->arsenal->state.q3, actor,
                call_view_angles(move, call),
                *call->view_height, error) ||
            !qa_q3_arsenal_step(move->arsenal->state.q3, actor, &controls,
                                call->elapsed_seconds * 1000.0f, error))
            return QA_MOVEMENT_ERROR;
    }
    return live(move->application, actor) ? QA_MOVEMENT_CONTINUE
                                          : QA_MOVEMENT_REMOVED;
}

static qa_movement_control move_touch(void *opaque,
                                      const qa_trace_result *trace,
                                      qa_movement_call *call,
                                      qa_error *error)
{
    application_move_call *move = opaque;
    if (move->context.source_usercmd) return QA_MOVEMENT_CONTINUE;
    bool first;
    if (!application_control_group_touch_once(move->application, move->control->actor, trace->actor, &first, error))
        return QA_MOVEMENT_ERROR;
    if (!first) return QA_MOVEMENT_CONTINUE;
    move->committed = true;
    qa_movement_control result = callback_result(
        move, qa_physics_impact(move->application->physics,
                                move->control->actor, trace, error));
    if (result == QA_MOVEMENT_CONTINUE && !refresh_source_call(move, call, error)) return QA_MOVEMENT_ERROR;
    return result;
}

static qa_game_family control_family(qa_movement_kind kind)
{
    if (kind == QA_MOVEMENT_NETQUAKE || kind == QA_MOVEMENT_QUAKEWORLD)
        return QA_GAME_Q1;
    return kind == QA_MOVEMENT_Q3 ? QA_GAME_Q3 : QA_GAME_Q2;
}

static qa_movement_control move_effect(void *opaque,
                                       const qa_movement_effect *effect,
                                       qa_movement_call *call,
                                       qa_error *error)
{
    application_move_call *move = opaque;
    for (size_t index = 0; index < move->q3_count; ++index) {
        move->committed = true;
        qa_movement_control result = qa_q3_movement_effect(
            move->q3[index]->state.q3, effect, call, error);
        if (result != QA_MOVEMENT_CONTINUE)
            return result;
    }
    if (effect->kind != QA_MOVE_EFFECT_SOUND || effect->sound == NULL)
        return live(move->application, move->control->actor)
                   ? QA_MOVEMENT_CONTINUE
                   : QA_MOVEMENT_REMOVED;
    qa_builtin_services services = application_builtin_services(
        move->application, move->application->world,
        move->application->physics);
    qa_string_id resource;
    if (!qa_builtin_resource(&services, effect->sound, &resource, error))
        return QA_MOVEMENT_ERROR;
    qa_body_state body;
    if (!qa_world_body_read(move->application->world, move->control->actor,
                            &body, error))
        return QA_MOVEMENT_ERROR;
    move->committed = true;
    return callback_result(
        move, application_emit(
                  move->application,
                  &(qa_builtin_event){
                      .kind = QA_BUILTIN_SOUND,
                      .family = control_family(move->control->state.kind),
                      .provider = move->movement == NULL
                                      ? 0
                                      : move->movement->owner,
                      .actor = move->control->actor,
                      .time_ns = effect->time_ns,
                      .resource = resource,
                      .origin = body.origin,
                      .volume = 1.0f,
                      .attenuation = 1.0f,
                  },
                  error));
}

static bool move_firing(void *opaque, const qa_movement_call *call)
{
    (void)opaque;
    return (call->command->buttons & 1u) != 0;
}

static bool move_is_bsp(void *opaque, const qa_trace_result *trace,
                        bool *out, qa_error *error)
{
    application_move_call *move = opaque;
    if (out == NULL)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "movement BSP query needs an output");
    if (trace->hit == QA_TRACE_HIT_WORLD) {
        *out = true;
        return true;
    }
    if (trace->hit != QA_TRACE_HIT_ACTOR) {
        *out = false;
        return true;
    }
    qa_actor_collision collision;
    qa_error local = {0};
    bool found = qa_world_get_collision(move->application->world, trace->actor,
                                        &collision, &local);
    if (!found && local.code != QA_OK) {
        if (error != NULL)
            *error = local;
        return false;
    }
    *out = found && collision.inline_model;
    return true;
}

static qa_movement_services movement_services(application_move_call *move)
{
    return (qa_movement_services){
        .context = move,
        .trace = move_trace,
        .point_contents = move_contents,
        .phase = move_phase,
        .touch = move_touch,
        .effect = move_effect,
        .firing = move_firing,
        .is_bsp = move_is_bsp,
    };
}

static bool movement_provider(qa_application *application, qa_actor_id actor,
                              application_provider **out, qa_error *error)
{
    application_provider *provider = application_provider_for(
        application, actor, QA_ROLE_MOVEMENT, "");
    if (provider == NULL || !provider->constructed || !provider->attached)
        return application_fail(error, QA_ERROR_NOT_FOUND,
                                "actor has no active selected movement provider");
    if (provider->kind != APPLICATION_PROVIDER_Q1 &&
        provider->kind != APPLICATION_PROVIDER_Q2 &&
        provider->kind != APPLICATION_PROVIDER_Q3 &&
        provider->kind != APPLICATION_PROVIDER_QC &&
        provider->kind != APPLICATION_PROVIDER_QVM &&
        !(provider->kind == APPLICATION_PROVIDER_NATIVE &&
          (provider->component.clock.kind == QA_CLOCK_Q3 ||
           provider->state.native.q2_engine != NULL)))
        return application_fail(
            error, QA_ERROR_UNSUPPORTED,
            "external selected movement adapter is not installed");
    *out = provider;
    return true;
}

bool application_control_ensure(qa_application *application, qa_actor_id actor,
                                qa_vec3 view_angles,
                                application_control_record **out,
                                qa_error *error)
{
    if (application == NULL || out == NULL || application->world == NULL ||
        application->combat == NULL || application->physics == NULL ||
        actor.slot >= application->control_capacity ||
        !qa_vec_finite(view_angles) || !live(application, actor))
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "control admission needs a live shared actor");
    application_control_record *record = &application->controls[actor.slot];
    if (record->active && qa_actor_id_equal(record->actor, actor)) {
        *out = record;
        return true;
    }
    if (record->moving || record->retired)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "control slot is retiring from a nested move");
    if (record->active) {
        qa_movement_result_free(&record->result);
        *record = (application_control_record){0};
    }
    application_provider *provider;
    qa_body_state body;
    qa_combat_state combat;
    if (!movement_provider(application, actor, &provider, error) ||
        !qa_world_body_read(application->world, actor, &body, error) ||
        !qa_combat_read_traits(application->combat, actor, &combat, error))
        return false;

    qa_movement_kind kind = movement_kind(provider->component.clock.kind);
    qa_movement_input defaults = qa_movement_input_default(kind, actor);
    *record = (application_control_record){
        .application = application,
        .actor = actor,
        .state = qa_movement_state_default(kind, body.origin),
        .profile = selected_profile(application, provider),
        .standing_bounds = defaults.standing.bounds,
        .bounds = body.bounds,
        .view_angles = view_angles,
        .command_angles = view_angles,
        .view_offset = qa_v3(0, 0, defaults.standing.view_height),
        .view_height = defaults.standing.view_height,
        .gravity_multiplier = 1.0f,
        .active = true,
    };
    if (application->equipment != NULL) {
        float scale = qa_equipment_gravity_scale(application->equipment, actor);
        if (isfinite(scale) && scale >= 0)
            record->gravity_multiplier = scale;
    }
    state_body(record, &body, combat.health);
    if (kind == QA_MOVEMENT_NETQUAKE)
        record->state.data.nq.view_angles = view_angles;
    else if (kind == QA_MOVEMENT_QUAKEWORLD)
        record->state.data.qw.angles = view_angles;
    else if (kind == QA_MOVEMENT_Q3)
        record->state.data.q3.view_angles = view_angles;
    *out = record;
    return true;
}

bool application_control_detach(qa_application *application, qa_actor_id actor,
                                qa_error *error)
{
    if (!application || !actor.registry)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "control detach needs an actor identity");
    if (!application_guest_input_actor_idle(application, actor))
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "control detach cannot interrupt guest input");
    if (actor.slot >= application->control_capacity) return true;
    application_control_record *record = &application->controls[actor.slot];
    if (!qa_actor_id_equal(record->actor, actor)) return true;
    if (record->moving || record->retired)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "control detach cannot interrupt actor movement");
    application_control_frames_release(application, actor);
    qa_movement_result_free(&record->result);
    *record = (application_control_record){0};
    return true;
}

bool qa_application_control_admit(qa_application *application,
                                  qa_actor_id actor, qa_vec3 view_angles,
                                  qa_error *error)
{
    if (application == NULL || application->state == QA_APPLICATION_FAULTED ||
        application->state == QA_APPLICATION_STOPPING)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "control admission needs a healthy application");
    application_control_record *ignored;
    return application_control_ensure(application, actor, view_angles, &ignored,
                                      error);
}

static bool prepare_input(application_move_call *move,
                          const qa_movement_command *command,
                          qa_movement_input *input, qa_error *error)
{
    application_control_record *record = move->control;
    qa_application *application = move->application;
    qa_body_state body;
    qa_combat_state combat;
    if (!qa_world_body_read(application->world, record->actor, &body, error) ||
        !qa_combat_read_traits(application->combat, record->actor, &combat,
                               error))
        return false;
    state_body(record, &body, combat.health);
    *input = qa_movement_input_default(record->state.kind, record->actor);
    input->state = record->state;
    application_control_frames_state(application, record->actor, &input->state);
    input->command = *command;
    input->profile = record->profile;
    input->shape.bounds = body.bounds;
    input->current_bounds = body.bounds;
    input->has_current_bounds = true;
    input->environment.health = combat.health;
    input->environment.flight = record->flight;
    input->environment.gravity_multiplier = record->gravity_multiplier;
    input->time_ns = application_control_time(&move->context);
    input->elapsed_ns = record->state.kind == QA_MOVEMENT_NETQUAKE
        ? application_control_elapsed(&move->context) : (uint64_t)command->milliseconds * UINT64_C(1000000);
    uint32_t application_ms = command->milliseconds;
    if (record->state.kind == QA_MOVEMENT_Q3) {
        int64_t difference = (int64_t)command->server_time_ms - input->state.data.q3.command_time_ms;
        application_ms = difference < 0 ? 0 : difference > 1000 ? 1000 : (uint32_t)difference;
    } else if (record->state.kind == QA_MOVEMENT_QUAKEWORLD) {
        uint32_t maximum = input->profile.data.qw.maximum_command_ms, slices = 1;
        if (!maximum || application_ms > 255)
            return application_fail(error, QA_ERROR_ARGUMENT, "QW input needs a valid source command interval");
        while (application_ms > maximum) { application_ms /= 2; slices *= 2; }
        application_ms *= slices;
    }
    move->application_elapsed_ns = record->state.kind == QA_MOVEMENT_NETQUAKE
        ? input->elapsed_ns : (uint64_t)application_ms * UINT64_C(1000000);
    if (!move->context.source_input_applied && !qc_input(move, &input->state, &input->command, NULL, &move->qc_command, true, false,
                    move->application_elapsed_ns, error)) return false;
    if (!live(application, record->actor)) return true;
    input->view_offset = record->view_offset;
    input->q2r_pml_origin = &record->q2r_pml_origin;
    input->environment.has_body_bounds = false;
    if (record->player_mode_set && !(move->context.source_usercmd && input->state.kind == QA_MOVEMENT_Q3)) {
        bool spectator = (record->state.kind == QA_MOVEMENT_QUAKEWORLD &&
                          record->state.data.qw.spectator) ||
                         (record->state.kind == QA_MOVEMENT_Q2_CLASSIC &&
                          record->state.data.q2.type == 1) ||
                         (record->state.kind == QA_MOVEMENT_Q2_RERELEASE &&
                          record->state.data.q2r.type == 3) ||
                         (record->state.kind == QA_MOVEMENT_Q3 &&
                          record->state.data.q3.movement_type == 2);
        input->environment.has_mode = !spectator;
        input->environment.mode = record->player_mode;
    }
    if (move->execution && move->execution->kind == APPLICATION_PROVIDER_QC &&
        !move->execution->state.qc.qualified && !application_qc_control_state(move->execution,
            record->actor, &input->state, &input->shape.bounds, &input->environment, &record->view_angles, error)) return false;
    for (size_t index = 0; index < move->q3_count; ++index) {
        move->committed = true;
        if (!qa_q3_prepare_movement(move->q3[index]->state.q3,
                                    record->actor, input, error))
            return false;
        if (move->context.source_usercmd && input->state.kind == QA_MOVEMENT_Q3 &&
            move->q3[index]->owner == move->context.command.provider)
            input->environment.speed_multiplier = 1;
        if (!live(application, record->actor))
            return true;
    }
    if (move->context.source_usercmd && input->state.kind == QA_MOVEMENT_Q3) {
        application_provider *source = move->world;
        qa_q3_native_client client;
        qa_q3_client_session session;
        uint32_t slot, flags;
        int32_t fixed, step, dmflags;
        if (!source || source->kind != APPLICATION_PROVIDER_Q3 ||
            source->owner != move->context.command.provider ||
            !qa_q3_native_client_slot(source->state.q3, record->actor, &slot, error) ||
            !qa_q3_client_read(source->state.q3, record->actor, &client, error) ||
            !qa_q3_client_session_read(source->state.q3, record->actor, &session, error) ||
            !qa_q3_client_server_flags(source->state.q3, slot, &flags, error) ||
            !application_native_q3_settings_integer(source, "pmove_fixed", &fixed, error) ||
            !application_native_q3_settings_integer(source, "pmove_msec", &step, error) ||
            !application_native_q3_settings_integer(source, "dmflags", &dmflags, error)) return false;
        bool spectator = session.team == 3;
        if (!spectator && (fixed || client.pmove_fixed) && step <= 0)
            return application_fail(error, QA_ERROR_ARGUMENT, "Native fixed Pmove has no positive cached interval");
        input->profile.data.q3.fixed_ms = !spectator && (fixed || client.pmove_fixed) ? (uint32_t)step : 0;
        input->profile.data.q3.no_footsteps = !spectator && ((uint32_t)dmflags & 32u);
        input->has_trace_policy = true;
        input->trace_policy = qa_collision_default_policy(QA_COLLISION_Q3);
        input->trace_policy.contents_mask = input->state.data.q3.movement_type == 2 ||
            input->state.data.q3.movement_type == 3 ? UINT32_C(0x10001) :
            flags & 8u ? UINT32_C(0x6010001) : UINT32_C(0x2010001);
    }
    application_client_outputs outputs;
    if (!application_control_outputs(application, record->actor, &outputs, error) ||
        !application_control_body_request(application, record->actor, body.bounds, &outputs, error)) return false;
    input->environment.has_body_bounds = outputs.has_body_bounds;
    if (outputs.has_body_bounds) input->environment.body_bounds = outputs.body_bounds;
    if (outputs.has_mode) { input->environment.has_mode = true; input->environment.mode = outputs.mode; }
    input->environment.has_stance = outputs.has_stance; input->environment.crouched = outputs.crouched;
    if (application->equipment != NULL && move->context.path != APPLICATION_CONTROL_NQ_TURN) {
        qa_equipment_state equipment;
        if (qa_equipment_read(application->equipment, record->actor,
                              &equipment)) {
            qa_equipment_controls controls = {
                .view_angles = record->view_angles,
                .previous_velocity = body.velocity,
                .view_height = record->view_height,
                .gravity = application->physics->gravity,
                .grapple_held = (input->command.buttons & 1u) != 0,
                .grenade_held = (input->command.buttons & 1u) != 0,
                .jump = command_jump(&input->command),
                .primary_attack = (input->command.buttons & 1u) != 0,
                .water_level = (uint8_t)(record->water_level < 0
                                             ? 0
                                             : record->water_level > 3
                                                   ? 3
                                                   : record->water_level),
                .water_type = record->water_type,
            };
            move->committed = true;
            if (!qa_equipment_input(application->equipment, record->actor,
                                    &controls, error)) return false;
            if (move->context.path != APPLICATION_CONTROL_NQ_TURN &&
                !qa_equipment_step(application->equipment, record->actor,
                                   input->time_ns, input->elapsed_ns,
                                   combat.health > 0 ? QA_Q2_HAND_ALIVE
                                                     : QA_Q2_HAND_DEAD,
                                   error))
                return false;
            qa_vec3 velocity = qa_movement_velocity(&input->state);
            bool apply = false;
            if (move->context.path != APPLICATION_CONTROL_NQ_TURN &&
                (!qa_equipment_q3_pull(application->equipment, record->actor,
                                      &velocity, &apply, error) ||
                (apply && !qa_movement_set_velocity(&input->state, velocity,
                                                    error))))
                return false;
        }
    }
    return true;
}

static bool step_nq_equipment(application_move_call *move, qa_error *error)
{
    qa_application *app = move->application;
    qa_actor_id actor = move->control->actor;
    qa_equipment_state equipment; qa_combat_state combat;
    if (!app->equipment || !live(app, actor) || !qa_equipment_read(app->equipment, actor, &equipment)) return true;
    if (!qa_combat_read_traits(app->combat, actor, &combat, error)) return false;
    move->committed = true;
    return qa_equipment_step(app->equipment, actor, move->input->time_ns, move->input->elapsed_ns,
        combat.health > 0 ? QA_Q2_HAND_ALIVE : QA_Q2_HAND_DEAD, error);
}

static bool record_nq_equipment(application_move_call *move, qa_error *error)
{
    qa_application *app = move->application; qa_actor_id actor = move->control->actor;
    qa_equipment_state equipment; qa_body_state body;
    if (!app->equipment || !live(app, actor) || !qa_equipment_read(app->equipment, actor, &equipment)) return true;
    if (!qa_world_body_read(app->world, actor, &body, error)) return false;
    const qa_movement_command *command = &move->input->command;
    qa_equipment_controls controls = {.view_angles = command->angles, .previous_velocity = body.velocity,
        .view_height = move->control->view_height, .gravity = app->physics->gravity,
        .grapple_held = (command->buttons & 1u) != 0, .grenade_held = (command->buttons & 1u) != 0,
        .jump = command_jump(command), .primary_attack = (command->buttons & 1u) != 0,
        .water_level = (uint8_t)(move->control->water_level < 0 ? 0 : move->control->water_level > 3 ? 3 : move->control->water_level),
        .water_type = move->control->water_type};
    move->committed = true;
    return qa_equipment_input(app->equipment, actor, &controls, error);
}

static bool finish_native_players(application_move_call *move,
                                  const qa_movement_result *result,
                                  qa_error *error)
{
    qa_actor_id actor = move->control->actor;
    if (!live(move->application, actor))
        return true;
    application_provider *character = move->character;
    application_provider *source = move->world;
    if (source && source->kind == APPLICATION_PROVIDER_Q1 &&
        qa_q1_player_source_present(source->state.q1, actor)) {
        qa_q1_game_operation operation = {0};
        move->committed = true;
        if (!application_control_q1_world_begin(source, &operation, error)) return false;
        bool okay = qa_q1_player_after_physics(source->state.q1, actor, error);
        qa_q1_game_operation_end(&operation);
        if (!okay) return false;
    }
    if (!live(move->application, actor))
        return true;
    if (character != NULL && character->kind == APPLICATION_PROVIDER_Q1) {
        qa_q1_character_view view;
        if (qa_q1_character_read(character->state.q1, actor, &view)) {
            qa_q1_character_input input = {
                .attack = (move->input->command.buttons & 1u) != 0,
                .jump = command_jump(&move->input->command),
                .use = (move->input->command.buttons & 4u) != 0,
                .water_level = (uint8_t)(result->water_level < 0
                                             ? 0
                                             : result->water_level > 3
                                                   ? 3
                                                   : result->water_level),
                .water_type = result->water_type,
            };
            move->committed = true;
            if (!qa_q1_character_frame(character->state.q1, actor, &input,
                                       error) ||
                !qa_q1_character_post_move(character->state.q1, actor, error))
                return false;
        }
    }
    if (!live(move->application, actor))
        return true;
    application_provider *map = application_world_provider(move->application,
                                                           QA_ROLE_ENTITIES, "");
    if (map != NULL && map->constructed && map->attached && !map->close_pending &&
        map->kind == APPLICATION_PROVIDER_Q1) {
        move->committed = true;
        if (!qa_q1_game_rogue_earthquake(map->state.q1, actor, error))
            return false;
        if (!live(move->application, actor))
            return true;
        if (map->close_pending || !map->constructed || !map->attached ||
            application_world_provider(move->application, QA_ROLE_ENTITIES, "") != map)
            return application_fail(error, QA_ERROR_ARGUMENT,
                                    "Authored earthquake retired its selected world source");
        if (!qa_q1_game_map_after_physics(map->state.q1, actor, error))
            return false;
    }
    if (!live(move->application, actor))
        return true;
    if (character != NULL && character->kind == APPLICATION_PROVIDER_Q2) {
        qa_q2_player_info info;
        if (qa_q2_player_read(character->state.q2, actor, &info)) {
            move->committed = true;
            if (!qa_q2_player_after_movement(character->state.q2, actor,
                                             error))
                return false;
        }
    }
    if (move->application->equipment != NULL) {
        qa_equipment_state equipment;
        if (qa_equipment_read(move->application->equipment, actor,
                              &equipment)) {
            move->committed = true;
            if (!qa_equipment_after_movement(
                    move->application->equipment, actor, false,
                    move->input->time_ns,
                    move->input->elapsed_ns, error))
                return false;
        }
    }
    return true;
}

static qa_movement_call input_call(application_move_call *move)
{
    qa_movement_input *input = move->input;
    return (qa_movement_call){.actor = input->actor, .state = &input->state,
        .command = &input->command, .bounds = &input->shape.bounds,
        .view_height = &move->control->view_height, .water_level = &move->control->water_level,
        .water_type = &move->control->water_type, .time_ns = input->time_ns,
        .milliseconds = input->command.milliseconds, .elapsed_seconds = (float)((double)input->elapsed_ns / 1e9),
        .source_time_ms = input->command.server_time_ms, .profile = &input->profile,
        .environment = &input->environment};
}

static bool control_move(qa_application *application,
                                 qa_actor_id actor,
                                 const qa_movement_command *command,
                                 qa_movement_command *applied,
                                 struct application_control_turn **turn,
                                 qa_error *error)
{
    const application_control_context *context = application_control_frame_current(application, actor);
    if (application == NULL || command == NULL ||
        context == NULL ||
        (application->state != QA_APPLICATION_RUNNING &&
         !(context->source_usercmd && application->state == QA_APPLICATION_READY &&
           application->operation == APPLICATION_CONFIGURING)) ||
        (application->operation != APPLICATION_IDLE &&
         application->operation != APPLICATION_ADVANCING &&
         !(context->source_usercmd && application->operation == APPLICATION_CONFIGURING)) ||
        actor.slot >= application->control_capacity || !live(application, actor))
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "movement command needs a running live control");
    application_control_record *record = &application->controls[actor.slot];
    if (!record->active || !qa_actor_id_equal(record->actor, actor))
        return application_fail(error, QA_ERROR_NOT_FOUND,
                                "movement actor is not admitted");
    if (record->moving)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "movement cannot reenter the same actor");
    if (command->kind != record->state.kind)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "command dialect differs from selected movement");
    if (record->command_seen && command->sequence <= record->command_sequence &&
        !context->retained &&
        !application_guest_input_applying(application, actor))
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "movement command sequence did not advance");
    if (application->q1_paused) {
        if (applied) *applied = *command;
        return true;
    }
    if (record->cutscene) {
        if (context->stage == APPLICATION_CONTROL_PHYSICS && turn && *turn) {
            struct application_control_turn *prepared = *turn; *turn = NULL;
            if (!application_control_turn_abort(prepared, error)) return false;
        }
        if (applied) { *applied = *command; applied->buttons = 0; }
        record->previous_buttons = record->buttons;
        record->buttons = 0;
        if (!context->source_usercmd && !context->source_guestcmd && !context->source_qwcmd) {
            record->command_sequence = command->sequence;
            record->command_seen = true;
        }
        return true;
    }
    qa_movement_command effective_command = *command;
    command = &effective_command;
    application_provider *movement;
    if (!movement_provider(application, actor, &movement, error))
        return false;
    bool preparing = context->stage == APPLICATION_CONTROL_PREPARE;
    bool physics = context->stage == APPLICATION_CONTROL_PHYSICS;
    if ((preparing || physics) && !turn)
        return application_fail(error, QA_ERROR_ARGUMENT, "Split source movement needs its retained turn");
    if (physics && !*turn)
        return application_fail(error, QA_ERROR_ARGUMENT, "NQ physics has no prepared input turn");
    bool guest_handled = false;
    application_operation guest_previous_operation = application->operation;
    application->operation = APPLICATION_ADVANCING;
    bool guest_ok = preparing || physics ? true : movement->kind == APPLICATION_PROVIDER_NATIVE &&
                   movement->state.native.q2_engine != NULL
        ? application_native_q2_move(movement, actor, command, &guest_handled, error)
        : application_arsenal_guest_move(application, actor, command, &guest_handled, error);
    application->operation = guest_previous_operation;
    if (!guest_ok || guest_handled) {
        if (guest_ok && applied) *applied = *command;
        if (!guest_ok) application_fault(application, error);
        return guest_ok;
    }
    qa_movement_profile profile = selected_profile(application, movement);
    application_provider *execution = control_execution(application, actor);
    application_provider *source = application_world_provider(application, QA_ROLE_ENTITIES, "");
    if (context->source_qwcmd || context->source_usercmd || context->source_guestcmd) {
        if (!source || source->owner != application_control_provider(context))
            return application_fail(error, QA_ERROR_ARGUMENT, "Raw source command lost its physical source owner");
        execution = source;
    } else if (context->path == APPLICATION_CONTROL_NQ_TURN && source &&
        source->owner == application_control_provider(context) && source->constructed &&
        source->attached && !source->close_pending && source->component.command_actor &&
        source->component.command_actor(source->component.state, application->session, actor) &&
        source->component.clock.kind == QA_CLOCK_NETQUAKE &&
        (source->kind == APPLICATION_PROVIDER_Q1 ||
         (source->kind == APPLICATION_PROVIDER_QC && !source->state.qc.qualified))) {
        execution = source;
    }
    if (profile.kind == QA_MOVEMENT_NETQUAKE && context->path == APPLICATION_CONTROL_NQ_TURN &&
        execution && execution->kind == APPLICATION_PROVIDER_QC && !execution->state.qc.qualified)
        profile.data.nq.source_jump_authority = true;
    if (profile.kind == QA_MOVEMENT_QUAKEWORLD && context->path == APPLICATION_CONTROL_QW_GROUP)
        profile.data.qw.shared_controls = false;
    if (execution && execution->kind == APPLICATION_PROVIDER_QC &&
        !application_qc_control_profile(execution, actor, &profile, error)) return false;
    if (profile.kind != record->state.kind)
        return application_fail(
            error, QA_ERROR_UNSUPPORTED,
            "changing movement family requires a control rebind");
    record->profile = profile;
    struct application_control_turn *prepared = preparing ? calloc(1, sizeof(*prepared)) : physics ? *turn : NULL;
    if (preparing && !prepared) return application_fail(error, QA_ERROR_MEMORY, "Allocating prepared NQ command");

    qa_movement_input input;
    application_move_call move = {
        .application = application,
        .control = record,
        .input = &input,
        .movement = movement,
        .character = application_provider_for(application, actor,
                                              QA_ROLE_CHARACTER, ""),
        .arsenal = application_provider_for(application, actor,
                                            QA_ROLE_ARSENAL, ""),
        .effects = application_provider_for(application, actor,
                                            QA_ROLE_EFFECTS, ""),
        .world = application_world_provider(application, QA_ROLE_ENTITIES, ""),
        .execution = execution,
        .context = *context,
    };
    if (physics) {
        move = prepared->move; input = prepared->input;
        move.input = &input; move.control = record; move.context = *context;
        input.time_ns = application_control_time(context); input.elapsed_ns = application_control_elapsed(context);
        effective_command = input.command; command = &effective_command;
        *turn = NULL;
    }
    add_q3(&move, move.movement);
    add_q3(&move, move.character);
    add_q3(&move, move.arsenal);
    add_q3(&move, move.effects);
    if (context->source_usercmd) add_q3(&move, move.world);
    application_operation previous_operation = application->operation;
    application->operation = APPLICATION_ADVANCING;
    record->moving = true;
    bool ok = begin_q1_operations(&move, error);
    if (ok && physics) ok = application_control_turn_resume(prepared, error);
    if (ok && !physics && context->weapon) {
        move.committed = true;
        ok = application_bot_weapon_apply(application, actor, context->arsenal, context->weapon, error);
    }
    if (ok && !physics && live(application, actor)) ok = prepare_input(&move, command, &input, error);
    if (ok && physics) {
        qa_movement_call call = input_call(&move);
        ok = refresh_source_call(&move, &call, error);
        input.current_bounds = input.shape.bounds;
    }
    if (ok && preparing && live(application, actor)) {
        qa_movement_call call = input_call(&move);
        ok = move_phase(&move, QA_MOVE_INPUT_BEGIN, &call, error) != QA_MOVEMENT_ERROR;
    }
    if (ok && preparing && live(application, actor)) ok = record_nq_equipment(&move, error);
    bool mixed_outer = context->path == APPLICATION_CONTROL_MIXED;
    move.mixed_source_outer = mixed_outer && execution && execution->kind == APPLICATION_PROVIDER_QC &&
        !execution->state.qc.qualified;
    if (ok && mixed_outer && live(application, actor)) {
        qa_movement_call call = input_call(&move);
        move.in_source_outer = true;
        ok = move_phase(&move, QA_MOVE_PRETHINK, &call, error) != QA_MOVEMENT_ERROR;
        move.in_source_outer = false;
        input.current_bounds = input.shape.bounds;
    }
    if (ok && live(application, actor)) effective_command = input.command;
    qa_movement_services services = movement_services(&move);
    if (ok && live(application, actor))
        ok = preparing ? qa_movement_prepare_netquake(&input, &services, &record->result, error)
            : physics ? qa_movement_physics_netquake(&input, &services, &record->result, error)
            : qa_movement_move(&input, &services, &record->result, error);
    if (ok && preparing && live(application, actor))
        ok = publish_result_body(&move, &record->result.state, record->result.bounds,
            record->result.ground, record->result.view_angles, false, false, error);
    if (ok && preparing && live(application, actor) && move.execution &&
        move.execution->kind == APPLICATION_PROVIDER_QC && !move.execution->state.qc.qualified) {
        qa_movement_call call = input_call(&move);
        call.state = &record->result.state; call.bounds = &record->result.bounds;
        call.water_level = &record->result.water_level; call.water_type = &record->result.water_type;
        ok = application_qc_control_phase(move.execution, actor, context->path,
            QA_MOVE_INPUT_END, context, &call, error);
    }
    if (ok && !preparing && live(application, actor) &&
        record->result.status == QA_MOVEMENT_ACTIVE) {
        qa_body_state before;
        if (!qa_world_body_read(application->world, actor, &before, error))
            ok = false;
        else {
            qa_body_state desired = before;
            desired.origin = qa_movement_origin(&record->result.state);
            desired.velocity = qa_movement_velocity(&record->result.state);
            desired.bounds = record->result.bounds;
            desired.ground = result_ground(&record->result.ground);
            desired.angles = result_angles(&record->result, before.angles);
            if (move.execution && move.execution->kind == APPLICATION_PROVIDER_QC &&
                !application_qc_control_body(move.execution, actor, &record->result.state, &desired, error)) ok = false;
            bool changed = !same_vector(before.origin, desired.origin) ||
                           !same_vector(before.velocity, desired.velocity) ||
                           !same_vector(before.angles, desired.angles) ||
                           !same_bounds(before.bounds, desired.bounds) ||
                           !qa_actor_id_equal(before.ground, desired.ground);
            qa_linked_body linked;
            bool is_linked =
                qa_world_linked(application->world, actor, &linked);
            move.committed = true;
            if (ok && ((changed &&
                 !qa_world_body_write(application->world, actor, &desired,
                                      error)) ||
                (!context->source_usercmd && (changed || !is_linked) &&
                 !qa_world_link(application->world, actor, NULL, error)) ||
                (!context->source_usercmd && !move.touched_triggers &&
                 !qa_physics_touch_triggers(application->physics, actor,
                                            error))))
                ok = false;
            else if (ok && !context->source_usercmd)
                move.touched_triggers = true;
        }
    }
    if (ok && !context->source_usercmd && live(application, actor) &&
        (record->state.kind == QA_MOVEMENT_Q2_CLASSIC ||
         record->state.kind == QA_MOVEMENT_Q2_RERELEASE))
        ok = qa_movement_apply_q2_contacts(&input, &services, &record->result,
                                           error);
    if (ok && live(application, actor) &&
        (record->state.kind == QA_MOVEMENT_Q2_CLASSIC || record->state.kind == QA_MOVEMENT_Q2_RERELEASE)) {
        qa_movement_call call = input_call(&move);
        call.state = &record->result.state; call.bounds = &record->result.bounds;
        call.view_height = &record->result.view_height; call.water_level = &record->result.water_level;
        call.water_type = &record->result.water_type;
        call.elapsed_seconds = (float)input.command.milliseconds / 1000.0f;
        move.completed_view_angles = &record->result.view_angles;
        ok = move_phase(&move, QA_MOVE_WEAPON, &call, error) != QA_MOVEMENT_ERROR;
        if (ok && live(application, actor)) ok = refresh_source_call(&move, &call, error);
    }
    if (ok && mixed_outer && live(application, actor)) {
        qa_movement_call call = input_call(&move);
        call.state = &record->result.state; call.bounds = &record->result.bounds;
        call.view_height = &record->result.view_height; call.water_level = &record->result.water_level;
        call.water_type = &record->result.water_type;
        move.in_source_outer = true;
        ok = move_phase(&move, QA_MOVE_POSTTHINK, &call, error) != QA_MOVEMENT_ERROR;
        move.in_source_outer = false;
    }
    if (ok && move.command_applied) {
        uint32_t milliseconds = effective_command.milliseconds;
        int32_t server_time = effective_command.server_time_ms;
        effective_command = move.applied_command;
        effective_command.milliseconds = milliseconds;
        effective_command.server_time_ms = server_time;
        input.command = effective_command;
    }
    if (ok && live(application, actor) && record->active &&
        qa_actor_id_equal(record->actor, actor)) {
        record->state = record->result.state;
        record->bounds = record->result.bounds;
        record->ground = record->result.ground;
        record->view_angles = record->result.view_angles;
        record->view_offset = record->result.view_offset;
        record->view_height = record->result.view_height;
        record->water_level = record->result.water_level;
        record->water_type = record->result.water_type;
        record->previous_buttons = record->buttons;
        record->buttons = command->buttons;
        record->command_angles = command->angles;
        bool received; uint64_t sequence;
        (void)application_control_frames_sequence(application, actor, &received, &sequence);
        if (!context->source_usercmd && !context->source_guestcmd && !context->source_qwcmd && (!context->retained || received)) {
            record->command_sequence = command->sequence;
            record->command_seen = true;
        }
    }
    if (ok && !preparing && live(application, actor) && move.arsenal != NULL &&
        move.arsenal->kind == APPLICATION_PROVIDER_Q2) {
        qa_q2_weapon_input controls;
        ok = application_q2_weapon_input(move.arsenal, actor, &controls, error);
        if (ok && live(application, actor)) {
            controls.latched_attack = controls.attack && (record->previous_buttons & 1u) == 0;
            move.committed = true;
            ok = qa_q2_weapon_controls(move.arsenal->state.q2, actor, &controls, error);
        }
    }
    if (ok && !preparing && live(application, actor))
        ok = finish_native_players(&move, &record->result, error);
    if (ok && !context->source_input_applied && physics && live(application, actor))
        ok = qc_input(&move, &record->state, &effective_command, NULL, &move.qc_slice, false, true,
            input.elapsed_ns, error);
    if (ok && !context->source_input_applied && !preparing && live(application, actor)) {
        ok = qc_input(&move, &record->state, &effective_command, NULL, &move.qc_command, false, false,
                        move.application_elapsed_ns, error);
        if (ok && live(application, actor)) {
            record->buttons = effective_command.buttons;
            record->command_angles = effective_command.angles;
        }
    }
    qa_error cleanup = {0};
    bool retain = ok && preparing && live(application, actor);
    if (physics && prepared) {
        prepared->move.qc_slice = move.qc_slice; prepared->move.qc_command = move.qc_command;
        bool aborted = application_control_turn_abort(prepared, &cleanup); prepared = NULL;
        move.qc_slice = (application_source_input_scope){0}; move.qc_command = (application_source_input_scope){0};
        if (!aborted) { if (ok && error) *error = cleanup; ok = false; }
    }
    if (!retain && !application_control_source_abort(&move.qc_slice, &cleanup)) {
        if (ok && error) *error = cleanup;
        ok = false;
    }
    if (!retain && !application_control_source_abort(&move.qc_command, &cleanup)) {
        if (ok && error) *error = cleanup;
        ok = false;
    }
    if (ok && physics && live(application, actor)) {
        ok = step_nq_equipment(&move, error);
        if (ok && live(application, actor)) {
            qa_movement_call call = input_call(&move); call.state = &record->state; call.bounds = &record->bounds;
            ok = refresh_source_call(&move, &call, error);
        }
    }
    if (record->retired && qa_actor_id_equal(record->actor, actor)) {
        qa_movement_result_free(&record->result);
        *record = (application_control_record){0};
    } else if (record->active && qa_actor_id_equal(record->actor, actor))
        record->moving = false;
    if (!ok && move.committed)
        application_fault(application, error);
    end_q1_operations(&move);
    if (retain) {
        prepared->move = move; prepared->input = input;
        prepared->move.input = &prepared->input;
        application_source_input_scope *scopes[] = {&prepared->move.qc_command, &prepared->move.qc_slice};
        for (size_t s = 0; ok && s < 2; ++s) for (size_t i = 0; ok && i < scopes[s]->count; ++i) {
            application_provider *provider = scopes[s]->owners[i];
            if (!provider || !provider->state.qc.qualified) continue;
            bool duplicate = false;
            for (size_t j = 0; j < prepared->parked_count; ++j) duplicate |= prepared->parked_owners[j] == provider;
            if (duplicate) continue;
            struct application_qc_parked_input *handle = NULL;
            ok = application_qc_input_park(provider, actor, &handle, error);
            if (ok && handle) {
                size_t index = prepared->parked_count++;
                prepared->parked_owners[index] = provider; prepared->parked[index] = handle;
            }
        }
        if (ok) *turn = prepared;
        else {
            qa_error aborted = {0};
            (void)application_control_turn_abort(prepared, &aborted);
            application_fault(application, error);
        }
    } else free(prepared);
    application->operation = previous_operation;
    application_control_frames_state(application, actor, NULL);
    if (ok && applied) *applied = effective_command;
    return ok;
}

bool application_control_move_applied(qa_application *application, qa_actor_id actor,
                                       const qa_movement_command *command,
                                       qa_movement_command *applied, qa_error *error)
{
    return control_move(application, actor, command, applied, NULL, error);
}

bool application_control_q3_flags(application_provider *provider, qa_actor_id actor,
    uint32_t clear, uint32_t set, qa_error *error)
{
    qa_application *app = provider ? provider->application : NULL;
    uint32_t slot;
    if (!app || provider->kind != APPLICATION_PROVIDER_Q3 || !provider->attached || provider->close_pending ||
        !provider->state.q3 || actor.slot >= app->control_capacity || !live(app, actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 movement flags need an actual native source client");
    if (!qa_q3_native_client_slot(provider->state.q3, actor, &slot, error)) return false;
    application_control_record *record = &app->controls[actor.slot];
    if (!record->active || record->retired || !qa_actor_id_equal(record->actor, actor) ||
        record->state.kind != QA_MOVEMENT_Q3)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 movement flags need the actual selected control state");
    record->state.data.q3.movement_flags = (record->state.data.q3.movement_flags & ~clear) | set;
    qa_movement_state *active = application_control_frames_state_current(app, actor);
    if (active && active->kind == QA_MOVEMENT_Q3)
        active->data.q3.movement_flags = (active->data.q3.movement_flags & ~clear) | set;
    return true;
}

static void q3_policy_apply(qa_q3_movement_state *state, uint8_t fields,
    const qa_q3_wire_policy *policy)
{
    if (fields & QA_Q3_WIRE_PM_TYPE) state->movement_type = policy->pm_type;
    if (fields & QA_Q3_WIRE_PM_BOB_CYCLE) state->bob_cycle = policy->bob_cycle;
    if (fields & QA_Q3_WIRE_PM_FLAGS) state->movement_flags = (uint32_t)policy->pm_flags;
    if (fields & QA_Q3_WIRE_PM_TIME) state->movement_time_ms = policy->pm_time;
    if (fields & QA_Q3_WIRE_PM_GRAVITY) state->gravity = policy->gravity;
    if (fields & QA_Q3_WIRE_PM_SPEED) state->speed = policy->speed;
    if (fields & QA_Q3_WIRE_PM_DIRECTION) state->movement_direction = policy->movement_dir;
}

bool application_control_q3_policy(application_provider *provider, qa_actor_id actor,
    uint8_t fields, const qa_q3_wire_policy *policy, qa_error *error)
{
    qa_application *app = provider ? provider->application : NULL;
    uint32_t slot;
    if (!app || !policy || !fields || (fields & ~QA_Q3_WIRE_PM_ALL) ||
        provider->kind != APPLICATION_PROVIDER_Q3 || !provider->constructed || !provider->attached ||
        provider->close_pending || actor.slot >= app->control_capacity || !live(app, actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native PM policy needs its actual source client");
    if (!qa_q3_native_client_slot(provider->state.q3, actor, &slot, error)) return false;
    application_control_record *record = &app->controls[actor.slot];
    if (!record->active || record->retired || !qa_actor_id_equal(record->actor, actor) ||
        record->state.kind != QA_MOVEMENT_Q3)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native PM policy needs the actual selected Q3 control");
    q3_policy_apply(&record->state.data.q3, fields, policy);
    qa_movement_state *active = application_control_frames_state_current(app, actor);
    if (active && active->kind == QA_MOVEMENT_Q3) q3_policy_apply(&active->data.q3, fields, policy);
    return true;
}

static void q3_source_state_apply(qa_q3_movement_state *state,
    const qa_q3_player_state *source, uint32_t fields, qa_actor_id pad)
{
    if (fields & QA_Q3_SOURCE_PM_COMMAND) state->command_time_ms = source->command_time_ms;
    if (fields & QA_Q3_SOURCE_PM_EVENTS) state->event_sequence = source->event_sequence;
    if (fields & QA_Q3_SOURCE_PM_FRAME) state->movement_frame = source->pmove_frame_count;
    if (fields & QA_Q3_SOURCE_PM_JUMPPAD) {
        state->jump_pad = pad; state->jump_pad_frame = source->jumppad_frame;
    }
    if (fields & QA_Q3_SOURCE_PM_DELTAS) {
        state->delta_angle_words[0] = source->delta_pitch_word;
        state->delta_angle_words[1] = source->delta_yaw_word;
        state->delta_angle_words[2] = source->delta_roll_word;
    }
    if (fields & QA_Q3_SOURCE_PM_VIEW) {
        state->view_angles = source->view_angles; state->view_height = source->view_height;
    }
}

bool application_control_q3_source_state(application_provider *provider, qa_actor_id actor,
    const qa_q3_player_state *source, uint32_t fields, qa_error *error)
{
    qa_application *app = provider ? provider->application : NULL;
    uint32_t slot;
    if (!app || !source || !fields || (fields & ~QA_Q3_SOURCE_PM_ALL) ||
        provider->kind != APPLICATION_PROVIDER_Q3 || !provider->constructed || !provider->attached ||
        provider->close_pending || actor.slot >= app->control_capacity || !live(app, actor) ||
        ((fields & QA_Q3_SOURCE_PM_VIEW) &&
         (!qa_vec_finite(source->view_angles) || !isfinite(source->view_height))))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native movement assignments need their actual source client");
    if (!qa_q3_native_client_slot(provider->state.q3, actor, &slot, error)) return false;
    application_control_record *record = &app->controls[actor.slot];
    if (!record->active || record->retired || !qa_actor_id_equal(record->actor, actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native movement assignment lost its selected control");
    if (record->state.kind != QA_MOVEMENT_Q3) return true;
    qa_actor_id pad = {0};
    if ((fields & QA_Q3_SOURCE_PM_JUMPPAD) && source->jumppad_entity > 0) {
        qa_q3_source_binding binding;
        if (!qa_q3_source_binding_read(provider->state.q3, (uint32_t)source->jumppad_entity, &binding, error)) return false;
        if (binding.in_use) pad = binding.actor;
    }
    q3_source_state_apply(&record->state.data.q3, source, fields, pad);
    qa_movement_state *active = application_control_frames_state_current(app, actor);
    if (active && active->kind == QA_MOVEMENT_Q3) q3_source_state_apply(&active->data.q3, source, fields, pad);
    return true;
}

const qa_movement_result *application_control_q3_result(application_provider *provider,
    qa_actor_id actor, qa_error *error)
{
    qa_application *app = provider ? provider->application : NULL;
    qa_source_command command;
    uint32_t slot;
    if (!app || provider->kind != APPLICATION_PROVIDER_Q3 || !provider->constructed ||
        !provider->attached || provider->close_pending || actor.slot >= app->control_capacity ||
        !live(app, actor) || !qa_q3_native_client_slot(provider->state.q3, actor, &slot, error) ||
        !qa_session_active_command(app->session, provider->owner, &command) ||
        !qa_actor_id_equal(command.actor, actor)) {
        application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 result needs its current source command"); return NULL;
    }
    application_control_record *record = &app->controls[actor.slot];
    if (!record->active || record->retired || record->moving || !qa_actor_id_equal(record->actor, actor) ||
        !qa_actor_id_equal(record->result.actor, actor)) {
        application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 result has no completed selected movement"); return NULL;
    }
    return &record->result;
}

bool application_control_stage_move(qa_application *application, qa_actor_id actor,
                                     const qa_movement_command *command,
                                     struct application_control_turn **turn, qa_error *error)
{
    return control_move(application, actor, command, NULL, turn, error);
}

bool application_control_turn_abort(struct application_control_turn *turn, qa_error *error)
{
    if (!turn) return true;
    qa_error first = {0};
    bool ok = true;
    for (size_t i = 0; i < turn->parked_count; ++i) if (turn->parked[i]) {
        qa_error current = {0};
        bool aborted = application_qc_input_parked_abort(turn->parked_owners[i], turn->parked[i], &current);
        turn->parked[i] = NULL;
        application_source_input_scope *scopes[] = {&turn->move.qc_slice, &turn->move.qc_command};
        for (size_t s = 0; s < 2; ++s) for (size_t j = 0; j < scopes[s]->count; ++j)
            if (scopes[s]->owners[j] == turn->parked_owners[i]) scopes[s]->owners[j] = NULL;
        if (!aborted) { if (ok) first = current; ok = false; }
    }
    qa_error active = {0};
    if (!application_control_source_abort(&turn->move.qc_slice, &active)) { if (ok) first = active; ok = false; }
    qa_error next = {0};
    if (!application_control_source_abort(&turn->move.qc_command, &next)) {
        if (ok) first = next;
        ok = false;
    }
    if (!ok && error) *error = first;
    free(turn);
    return ok;
}

bool application_control_turn_resume(struct application_control_turn *turn, qa_error *error)
{
    if (!turn) return true;
    for (size_t i = 0; i < turn->parked_count; ++i) if (turn->parked[i]) {
        if (!application_qc_input_resume(turn->parked_owners[i], turn->parked[i], error)) return false;
        turn->parked[i] = NULL;
    }
    return true;
}

bool application_control_group_post(qa_application *app, qa_actor_id actor, qa_error *error)
{
    if (!live(app, actor) || actor.slot >= app->control_capacity) return true;
    application_control_record *record = &app->controls[actor.slot];
    const application_control_context *context = application_control_frame_current(app, actor);
    if (!context) return application_fail(error, QA_ERROR_ARGUMENT, "QW group postthink has no admitted source frame");
    application_provider *execution = control_execution(app, actor);
    if (context->source_qwcmd) {
        execution = application_world_provider(app, QA_ROLE_ENTITIES, "");
        if (!execution || execution->owner != context->command.provider)
            return application_fail(error, QA_ERROR_ARGUMENT, "QW source postthink lost its physical source owner");
    }
    qa_movement_input input = qa_movement_input_default(record->state.kind, actor);
    input.state = record->state; input.shape.bounds = record->bounds; input.profile = record->profile;
    input.command.kind = record->state.kind; input.command.buttons = record->buttons;
    input.command.angles = record->command_angles; input.time_ns = application_control_time(context);
    input.elapsed_ns = application_control_elapsed(context);
    application_move_call move = {.application = app, .control = record, .input = &input,
        .execution = execution, .context = *context,
        .arsenal = application_provider_for(app, actor, QA_ROLE_ARSENAL, "")};
    qa_movement_call call = input_call(&move);
    if (move_phase(&move, QA_MOVE_POSTTHINK, &call, error) == QA_MOVEMENT_ERROR) return false;
    if (!live(app, actor)) return true;
    if (move.arsenal && move.arsenal->kind == APPLICATION_PROVIDER_Q1) {
        qa_q1_player_view view;
        if (qa_q1_player_read(move.arsenal->state.q1, actor, &view) &&
            !qa_q1_player_postthink(move.arsenal->state.q1, actor, error)) return false;
    }
    if (live(app, actor)) { record->state = input.state; record->bounds = input.shape.bounds; }
    return true;
}

bool qa_application_control_move(qa_application *application, qa_actor_id actor,
                                  const qa_movement_command *command, qa_error *error)
{
    return application_control_frames_receive(application, actor, command, 1, error);
}

bool qa_application_control_commands(qa_application *application, qa_actor_id actor,
                                      const qa_movement_command *commands, size_t count, qa_error *error)
{
    return application_control_frames_receive(application, actor, commands, count, error);
}

bool application_control_guest_complete(qa_application *application, qa_actor_id actor,
                                         const qa_movement_command *command,
                                         const qa_q3_player *player, qa_error *error)
{
    const application_control_context *context = application_control_frame_current(application, actor);
    if (!application || !command || !player || !context || actor.slot >= application->control_capacity)
        return application_fail(error, QA_ERROR_ARGUMENT, "Guest movement completion is missing");
    if (!live(application, actor)) return true;
    application_control_record *record = &application->controls[actor.slot];
    if (!record->active || !qa_actor_id_equal(record->actor, actor) ||
        record->moving || record->state.kind != QA_MOVEMENT_Q3)
        return application_fail(error, QA_ERROR_ARGUMENT, "Guest movement has no Q3 continuation");
    record->moving = true;
    qa_movement_input input = qa_movement_input_default(QA_MOVEMENT_Q3, actor);
    input.command = *command;
    input.time_ns = application_control_time(context);
    input.elapsed_ns = (uint64_t)command->milliseconds * UINT64_C(1000000);
    application_move_call move = {.application = application, .control = record, .input = &input,
        .movement = application_provider_for(application, actor, QA_ROLE_MOVEMENT, ""),
        .arsenal = application_provider_for(application, actor, QA_ROLE_ARSENAL, ""),
        .character = application_provider_for(application, actor, QA_ROLE_CHARACTER, ""),
        .effects = application_provider_for(application, actor, QA_ROLE_EFFECTS, ""),
        .world = application_world_provider(application, QA_ROLE_ENTITIES, ""),
        .execution = control_execution(application, actor), .context = *context};
    qa_body_state body;
    bool ok = begin_q1_operations(&move, error) &&
              qa_world_body_read(application->world, actor, &body, error);
    if (ok && live(application, actor)) {
        body.origin = qa_v3(player->origin[0], player->origin[1], player->origin[2]);
        body.velocity = qa_v3(player->velocity[0], player->velocity[1], player->velocity[2]);
        body.angles = qa_v3(0, player->viewangles[1], body.angles.z);
        body.ground = (qa_actor_id){0};
        if (player->groundEntityNum == 1022) body.ground = application->physics->world_actor;
        else if (player->groundEntityNum >= 0 && player->groundEntityNum < 1022) {
            application_provider *provider = application_provider_for(application, actor, QA_ROLE_MOVEMENT, "");
            struct application_q3_guest *engine = q3g_engine(provider);
            if (!engine || !engine->game)
                ok = application_fail(error, QA_ERROR_NOT_FOUND, "Guest movement source disappeared");
            else ok = qa_q3_host_actor(engine->game->host, (uint32_t)player->groundEntityNum,
                                       false, &body.ground, error);
        }
        if (ok && live(application, actor))
            ok = qa_world_body_write(application->world, actor, &body, error) &&
                 (!live(application, actor) || qa_world_link(application->world, actor, NULL, error));
        if (ok && live(application, actor))
            ok = qa_world_body_read(application->world, actor, &body, error);
    }
    if (ok && live(application, actor)) {
        qa_q3_movement_state *state = &record->state.data.q3;
        state->command_time_ms = player->commandTime; state->movement_type = player->pmType;
        state->bob_cycle = player->bobCycle; state->movement_flags = (uint32_t)player->pmFlags;
        state->movement_time_ms = player->pmTime; state->origin = body.origin; state->velocity = body.velocity;
        state->gravity = player->gravity; state->speed = player->speed;
        memcpy(state->delta_angle_words, player->deltaAngles, sizeof(state->delta_angle_words));
        state->movement_direction = player->movementDir;
        state->grapple_point = qa_v3(player->grapplePoint[0], player->grapplePoint[1], player->grapplePoint[2]);
        state->flags = (uint32_t)player->eFlags;
        state->view_angles = qa_v3(player->viewangles[0], player->viewangles[1], player->viewangles[2]);
        state->view_height = (float)player->viewheight;
        state->ground = body.ground.registry
            ? (qa_movement_ground){.hit = qa_actor_id_equal(body.ground, application->physics->world_actor)
                                               ? QA_TRACE_HIT_WORLD : QA_TRACE_HIT_ACTOR, .actor = body.ground}
            : (qa_movement_ground){0};
        state->event_sequence = (uint32_t)player->eventSequence;
        state->movement_frame = player->pmoveFramecount; state->jump_pad_frame = player->jumppadFrame;
        record->bounds = body.bounds; record->ground = state->ground;
        record->view_angles = state->view_angles; record->view_height = state->view_height;
        record->view_offset = qa_v3(0, 0, state->view_height);
        record->previous_buttons = record->buttons; record->buttons = command->buttons;
        record->command_angles = command->angles;
        if (!context->source_guestcmd) {
            record->command_sequence = command->sequence; record->command_seen = true;
        }
        record->result.status = QA_MOVEMENT_ACTIVE; record->result.actor = actor;
        record->result.command_sequence = command->sequence; record->result.state = record->state;
        record->result.bounds = record->bounds; record->result.ground = record->ground;
        record->result.view_angles = record->view_angles; record->result.view_offset = record->view_offset;
        record->result.view_height = record->view_height; record->result.contact_count = 0;
        record->result.water_level = record->water_level; record->result.water_type = record->water_type;
        record->result.impact_delta = 0;
        input.state = record->state;
        if (move.arsenal && move.arsenal->kind == APPLICATION_PROVIDER_Q2) {
            qa_q2_weapon_input controls;
            ok = application_q2_weapon_input(move.arsenal, actor, &controls, error);
            if (ok && live(application, actor)) {
                controls.latched_attack = controls.attack && (record->previous_buttons & 1u) == 0;
                ok = qa_q2_weapon_controls(move.arsenal->state.q2, actor, &controls, error);
            }
        } else if (command->milliseconds && move.arsenal && move.arsenal->kind == APPLICATION_PROVIDER_Q1) {
            qa_q1_input controls = {.view_angles = record->view_angles,
                .attack = (command->buttons & 1u) != 0, .jump = command_jump(command),
                .use = (command->buttons & 4u) != 0, .impulse = command->impulse,
                .water_level = (uint8_t)record->water_level, .water_type = record->water_type};
            ok = qa_q1_player_input(move.arsenal->state.q1, actor, &controls, error) &&
                (!live(application, actor) || qa_q1_player_postthink(move.arsenal->state.q1, actor, error));
        } else if (command->milliseconds && move.arsenal && move.arsenal->kind == APPLICATION_PROVIDER_Q3) {
            qa_movement_call call = {.actor = actor, .state = &record->state,
                .command = &input.command, .bounds = &record->bounds,
                .view_height = &record->view_height, .water_level = &record->water_level,
                .water_type = &record->water_type, .milliseconds = command->milliseconds,
                .elapsed_seconds = (float)command->milliseconds / 1000.0f};
            qa_movement_control result = qa_q3_movement_phase_selected(move.arsenal->state.q3,
                QA_MOVE_WEAPON, &call, true, error);
            ok = result != QA_MOVEMENT_ERROR;
            if (ok && live(application, actor)) {
                record->result.state = record->state;
                record->result.bounds = record->bounds;
                record->result.view_height = record->view_height;
                record->result.water_level = record->water_level;
                record->result.water_type = record->water_type;
            }
        }
        if (ok && live(application, actor)) ok = finish_native_players(&move, &record->result, error);
    }
    if (record->retired && qa_actor_id_equal(record->actor, actor)) {
        qa_movement_result_free(&record->result); *record = (application_control_record){0};
    } else if (record->active && qa_actor_id_equal(record->actor, actor)) record->moving = false;
    if (!ok) application_fault(application, error);
    end_q1_operations(&move);
    return ok;
}

bool application_control_velocity(qa_application *application, qa_actor_id actor,
                                   qa_vec3 velocity, qa_error *error)
{
    if (!live(application, actor) || !qa_vec_finite(velocity))
        return application_fail(error, QA_ERROR_ARGUMENT, "Guest velocity needs a live actor and finite vector");
    if (actor.slot >= application->control_capacity)
        return true;
    application_control_record *record = &application->controls[actor.slot];
    if (!record->active || !qa_actor_id_equal(record->actor, actor))
        return true;
    if (record->retired)
        return application_fail(error, QA_ERROR_NOT_FOUND, "Guest velocity control is retired");
    if (!qa_movement_set_velocity(&record->state, velocity, error))
        return false;
    if (qa_actor_id_equal(record->result.actor, actor))
        return qa_movement_set_velocity(&record->result.state, velocity, error);
    return true;
}

bool qa_application_control_read(const qa_application *application,
                                 qa_actor_id actor,
                                 qa_application_control_view *out)
{
    if (application == NULL || out == NULL ||
        actor.slot >= application->control_capacity)
        return false;
    const application_control_record *record =
        &application->controls[actor.slot];
    if (!record->active || !qa_actor_id_equal(record->actor, actor))
        return false;
    *out = (qa_application_control_view){
        .actor = actor,
        .state = record->state,
        .profile = record->profile,
        .bounds = record->bounds,
        .ground = record->ground,
        .view_angles = record->view_angles,
        .command_angles = record->command_angles,
        .view_offset = record->view_offset,
        .command_sequence = record->command_sequence,
        .buttons = record->buttons,
        .previous_buttons = record->previous_buttons,
        .water_level = record->water_level,
        .water_type = record->water_type,
        .view_height = record->view_height,
        .gravity_multiplier = record->gravity_multiplier,
        .flight = record->flight,
        .cutscene = record->cutscene,
    };
    qa_movement_state *active = application_control_frames_state_current(application, actor);
    if (active) out->state = *active;
    return true;
}

bool qa_application_control_camera(const qa_application *application,
                                   qa_actor_id actor,
                                   qa_application_camera_view *out)
{
    if (out == NULL)
        return false;
    qa_application_control_view view;
    qa_body_state body;
    if (!qa_application_control_read(application, actor, &view) ||
        !qa_world_body_read(application->world, actor, &body, NULL))
        return false;
    *out = (qa_application_camera_view){
        .actor = actor,
        .origin = body.origin,
        .angles = view.view_angles,
        .view_offset = view.view_offset,
        .view_height = view.view_height,
        .cutscene = view.cutscene,
    };
    application_client_outputs outputs;
    if (!application_control_outputs(application, actor, &outputs, NULL)) return false;
    if (outputs.has_view_offset) {
        bool crouched = view.state.kind == QA_MOVEMENT_Q3 ? (view.state.data.q3.movement_flags & 1u) != 0 :
            view.state.kind == QA_MOVEMENT_Q2_CLASSIC ? (view.state.data.q2.flags & 1u) != 0 :
            view.state.kind == QA_MOVEMENT_Q2_RERELEASE ? (view.state.data.q2r.flags & 1u) != 0 :
            view.state.kind == QA_MOVEMENT_QUAKEWORLD &&
                body.bounds.maxs.z < qa_movement_input_default(view.state.kind, actor).standing.bounds.maxs.z;
        application_provider *source = application_world_provider((qa_application *)application, QA_ROLE_ENTITIES, "");
        if (source && (source->kind == APPLICATION_PROVIDER_QVM ||
            (source->kind == APPLICATION_PROVIDER_NATIVE && source->component.clock.kind == QA_CLOCK_Q3)) &&
            !application_arsenal_guest_crouched(source, actor, &crouched, NULL)) return false;
        if (source && source->kind == APPLICATION_PROVIDER_NATIVE && source->state.native.q2_engine &&
            !application_q2_control_crouched(source->state.native.q2_engine, actor, &crouched, NULL)) return false;
        if (!outputs.has_stance || outputs.crouched || !crouched) {
            out->view_offset = outputs.view_offset; out->view_height = outputs.view_offset.z;
        }
    }
    return true;
}

static int32_t state_mode(const qa_movement_state *state)
{
    switch (state->kind) {
    case QA_MOVEMENT_NETQUAKE:
        return state->data.nq.move_type;
    case QA_MOVEMENT_QUAKEWORLD:
        return state->data.qw.spectator;
    case QA_MOVEMENT_Q2_CLASSIC:
        return state->data.q2.type;
    case QA_MOVEMENT_Q2_RERELEASE:
        return state->data.q2r.type;
    case QA_MOVEMENT_Q3:
        return state->data.q3.movement_type;
    }
    return 0;
}

static void set_state_mode(qa_movement_state *state, int32_t mode)
{
    switch (state->kind) {
    case QA_MOVEMENT_NETQUAKE:
        state->data.nq.move_type = mode;
        break;
    case QA_MOVEMENT_QUAKEWORLD:
        state->data.qw.spectator = mode;
        break;
    case QA_MOVEMENT_Q2_CLASSIC:
        state->data.q2.type = mode;
        break;
    case QA_MOVEMENT_Q2_RERELEASE:
        state->data.q2r.type = mode;
        break;
    case QA_MOVEMENT_Q3:
        state->data.q3.movement_type = mode;
        break;
    }
}

static int32_t freeze_mode(qa_movement_kind kind)
{
    return kind == QA_MOVEMENT_Q2_CLASSIC || kind == QA_MOVEMENT_Q3
               ? 4
           : kind == QA_MOVEMENT_Q2_RERELEASE ? 6
                                              : 0;
}

bool application_control_player_mode(qa_application *application, qa_actor_id actor,
                                      qa_movement_mode mode, bool spectator, qa_error *error)
{
    if (application == NULL || (unsigned)mode > QA_MOVEMENT_MODE_FREEZE)
        return application_fail(error, QA_ERROR_ARGUMENT, "Invalid selected player movement mode");
    qa_body_state body;
    application_control_record *record;
    if (!qa_world_body_read(application->world, actor, &body, error) ||
        !application_control_ensure(application, actor, body.angles, &record, error))
        return false;
    int32_t encoded;
    switch (record->state.kind) {
    case QA_MOVEMENT_NETQUAKE:
        encoded = mode == QA_MOVEMENT_MODE_FREEZE ? 0
                  : mode == QA_MOVEMENT_MODE_NOCLIP || spectator ? 8 : 3;
        break;
    case QA_MOVEMENT_QUAKEWORLD:
        encoded = mode == QA_MOVEMENT_MODE_NOCLIP || spectator;
        break;
    case QA_MOVEMENT_Q2_CLASSIC:
        encoded = mode == QA_MOVEMENT_MODE_FREEZE ? 4
                  : mode == QA_MOVEMENT_MODE_NOCLIP || spectator ? 1 : 0;
        break;
    case QA_MOVEMENT_Q2_RERELEASE:
        encoded = mode == QA_MOVEMENT_MODE_FREEZE ? 6
                  : spectator ? 3 : mode == QA_MOVEMENT_MODE_NOCLIP ? 2 : 0;
        break;
    case QA_MOVEMENT_Q3:
        encoded = mode == QA_MOVEMENT_MODE_FREEZE ? 4
                  : spectator ? 2 : mode == QA_MOVEMENT_MODE_NOCLIP ? 1 : 0;
        break;
    default:
        return application_fail(error, QA_ERROR_ARGUMENT, "Unknown selected movement dialect");
    }
    set_state_mode(&record->state, encoded);
    record->player_mode = mode == QA_MOVEMENT_MODE_NORMAL && spectator
                              ? QA_MOVEMENT_MODE_NOCLIP : mode;
    record->player_mode_set = true;
    qa_physics_services physics = application_physics_services(application);
    qa_physics_properties properties;
    if (physics.read != NULL && physics.read(physics.context, actor, &properties)) {
        properties.motion = mode == QA_MOVEMENT_MODE_FREEZE ? QA_PHYSICS_STATIONARY
                            : mode == QA_MOVEMENT_MODE_NOCLIP || spectator ? QA_PHYSICS_NOCLIP
                            : properties.motion == QA_PHYSICS_NOCLIP ? QA_PHYSICS_STATIONARY
                                                                     : properties.motion;
        if (physics.write != NULL && !physics.write(physics.context, actor, &properties, error))
            return false;
    }
    return true;
}

static bool character_cutscene(qa_application *application, qa_actor_id actor,
                               qa_vec3 origin, qa_vec3 angles,
                               qa_vec3 view_offset, bool active,
                               qa_error *error)
{
    application_provider *provider = application_provider_for(
        application, actor, QA_ROLE_CHARACTER, "");
    if (provider == NULL)
        return application_fail(error, QA_ERROR_NOT_FOUND,
                                "controlled actor has no selected character");
    switch (provider->kind) {
    case APPLICATION_PROVIDER_Q1:
        return qa_q1_character_cutscene(provider->state.q1, actor, view_offset,
                                        !active, error);
    case APPLICATION_PROVIDER_Q2:
        return !active || qa_q2_clear_input(provider->state.q2, actor, error);
    case APPLICATION_PROVIDER_Q3:
        return active
                   ? qa_q3_character_cutscene(provider->state.q3, actor, origin,
                                              angles, view_offset, error)
                   : qa_q3_character_cutscene_clear(provider->state.q3, actor,
                                                    error);
    case APPLICATION_PROVIDER_QVM:
        return !active || provider->state.qvm.host == NULL ||
               qa_q3_host_player_cutscene(provider->state.qvm.host, actor,
                                          origin, angles,
                                          gravity_integer(view_offset.z), error);
    case APPLICATION_PROVIDER_NATIVE:
        return !active || provider->state.native.q3_host == NULL ||
               qa_q3_host_player_cutscene(provider->state.native.q3_host,
                                          actor, origin, angles,
                                          gravity_integer(view_offset.z), error);
    case APPLICATION_PROVIDER_QC:
        return application_fail(
            error, QA_ERROR_UNSUPPORTED,
            "QuakeC selected character has no cinematic control adapter");
    }
    return false;
}

bool application_control_cutscene(qa_application *application,
                                  qa_actor_id actor, qa_vec3 origin,
                                  qa_vec3 angles, qa_vec3 view_offset,
                                  qa_error *error)
{
    if (application == NULL || !qa_vec_finite(origin) ||
        !qa_vec_finite(angles) || !qa_vec_finite(view_offset))
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "cinematic control contains a nonfinite pose");
    application_control_record *record;
    if (!application_control_ensure(application, actor, angles, &record, error))
        return false;
    qa_body_state body;
    qa_combat_state combat;
    if (!qa_world_body_read(application->world, actor, &body, error) ||
        !qa_combat_read_traits(application->combat, actor, &combat, error))
        return false;
    bool entering = !record->cutscene;
    if (entering) {
        record->saved_damageable = combat.can_take_damage;
        record->saved_mode = state_mode(&record->state);
        record->saved_mode_valid = true;
        record->saved_view_offset = record->view_offset;
    }
    body.origin = origin;
    body.angles = angles;
    body.velocity = qa_v3(0, 0, 0);
    body.ground = (qa_actor_id){0};
    combat.can_take_damage = false;
    if (!qa_world_body_write(application->world, actor, &body, error) ||
        !qa_combat_set_traits(application->combat, actor, &combat, error)) {
        application_fault(application, error);
        return false;
    }
    qa_builtin_motion_change change = {
        .reason = QA_BUILTIN_MOTION_RESET,
        .body = body,
        .view_angles = angles,
        .force_view_angles = true,
    };
    if (!application_record_motion_change(application, actor, &change,
                                          error)) {
        application_fault(application, error);
        return false;
    }
    record = &application->controls[actor.slot];
    if (!record->active || !qa_actor_id_equal(record->actor, actor))
        return true;
    record->cutscene = true;
    record->view_angles = angles;
    record->command_angles = angles;
    record->view_offset = view_offset;
    record->view_height = view_offset.z;
    set_state_mode(&record->state, freeze_mode(record->state.kind));
    (void)qa_movement_set_origin(&record->state, origin, NULL);
    (void)qa_movement_set_velocity(&record->state, qa_v3(0, 0, 0), NULL);
    if (!character_cutscene(application, actor, origin, angles, view_offset,
                            true, error) ||
        !qa_world_link(application->world, actor, NULL, error)) {
        application_fault(application, error);
        return false;
    }
    return true;
}

bool qa_application_control_end_cutscene(qa_application *application,
                                         qa_actor_id actor,
                                         qa_error *error)
{
    if (application == NULL || actor.slot >= application->control_capacity)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "invalid cinematic control release");
    application_control_record *record = &application->controls[actor.slot];
    if (!record->active || !qa_actor_id_equal(record->actor, actor))
        return application_fail(error, QA_ERROR_NOT_FOUND,
                                "cinematic control actor is not admitted");
    if (!record->cutscene)
        return true;
    qa_body_state body;
    if (!qa_world_body_read(application->world, actor, &body, error) ||
        !character_cutscene(application, actor, body.origin,
                            record->view_angles, record->saved_view_offset,
                            false, error))
        return false;
    qa_combat_state combat;
    if (!qa_combat_read_traits(application->combat, actor, &combat, error))
        return false;
    combat.can_take_damage = record->saved_damageable;
    if (!qa_combat_set_traits(application->combat, actor, &combat, error)) {
        application_fault(application, error);
        return false;
    }
    if (record->saved_mode_valid)
        set_state_mode(&record->state, record->saved_mode);
    record->view_offset = record->saved_view_offset;
    record->view_height = record->saved_view_offset.z;
    record->cutscene = false;
    record->saved_mode_valid = false;
    return true;
}

bool application_control_motion_changed(
    qa_application *application, qa_actor_id actor,
    const qa_builtin_motion_change *change, qa_error *error)
{
    if (application == NULL || change == NULL ||
        !qa_vec_finite(change->body.origin) ||
        !qa_vec_finite(change->body.velocity) ||
        !qa_vec_finite(change->view_angles))
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "invalid selected movement discontinuity");
    application_control_record *record;
    if (!application_control_ensure(application, actor, change->view_angles,
                                    &record, error))
        return false;
    if (change->reason == QA_BUILTIN_MOTION_RESET && record->cutscene) {
        if (!character_cutscene(application, actor, change->body.origin,
                                change->view_angles, record->saved_view_offset,
                                false, error))
            return false;
        if (record->saved_mode_valid)
            set_state_mode(&record->state, record->saved_mode);
        record->cutscene = false;
        record->saved_mode_valid = false;
        record->view_offset = record->saved_view_offset;
        record->view_height = record->saved_view_offset.z;
    }
    qa_combat_state combat;
    if (!qa_combat_read_traits(application->combat, actor, &combat, error))
        return false;
    state_body(record, &change->body, combat.health);
    if (change->force_view_angles) {
        record->view_angles = change->view_angles;
        record->command_angles = change->view_angles;
    }
    return true;
}

bool application_controlled(const qa_application *application,
                            qa_actor_id actor)
{
    if (application == NULL || actor.slot >= application->control_capacity)
        return false;
    const application_control_record *record =
        &application->controls[actor.slot];
    return record->active && record->cutscene &&
           qa_actor_id_equal(record->actor, actor);
}

bool application_control_physics_read(const qa_application *application,
                                      qa_actor_id actor,
                                      qa_physics_properties *out)
{
    if (application == NULL || out == NULL ||
        actor.slot >= application->control_capacity)
        return false;
    const application_control_record *record =
        &application->controls[actor.slot];
    if (!record->active || !qa_actor_id_equal(record->actor, actor))
        return false;
    *out = qa_physics_properties_default(movement_family(record->state.kind));
    out->motion = record->cutscene ? QA_PHYSICS_STATIONARY : QA_PHYSICS_STEP;
    out->solid = QA_PHYSICS_BOX;
    out->flags = QA_PHYSICS_PLAYER;
    if (record->ground.hit != QA_TRACE_HIT_NONE)
        out->flags |= QA_PHYSICS_ONGROUND;
    if (record->flight)
        out->flags |= QA_PHYSICS_FLYING;
    out->q2_rerelease = record->state.kind == QA_MOVEMENT_Q2_RERELEASE;
    out->water_level = record->water_level;
    out->water_type = record->water_type;
    out->gravity_scale = record->gravity_multiplier;
    return true;
}

bool application_control_physics_write(qa_application *application,
                                       qa_actor_id actor,
                                       const qa_physics_properties *value,
                                       qa_error *error)
{
    if (application == NULL || value == NULL ||
        actor.slot >= application->control_capacity ||
        !isfinite(value->gravity_scale) || value->gravity_scale < 0)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "invalid selected player physics state");
    application_control_record *record = &application->controls[actor.slot];
    if (!record->active || !qa_actor_id_equal(record->actor, actor))
        return application_fail(error, QA_ERROR_NOT_FOUND,
                                "selected player physics is not admitted");
    record->flight = (value->flags & QA_PHYSICS_FLYING) != 0;
    record->water_level = value->water_level;
    record->water_type = value->water_type;
    record->gravity_multiplier = value->gravity_scale;
    return true;
}
