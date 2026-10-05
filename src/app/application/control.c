#include "internal.h"
#include "map_players_private.h"
#include "guest_native_q2_private.h"
#include "guest_native_q2_input.h"
#include "native_q2_client_stages.h"
#include "guest_input_private.h"
#include "control_frame.h"
#include "client_outputs.h"
#include "guest_q2_control.h"
#include "guest_qc_profile.h"
#include "native_q3_settings.h"
#include "native_q3_wire_state.h"
#include "native_q1_composition_rogue.h"
#include "native_q1_composition_birth.h"
#include "native_q1_spectator.h"
#include "guest_q3_components.h"
#include "guest_q3_component_input.h"
#include "qa/game_q3_source.h"
#include "qa/game_q3_clients.h"
#include "qa/game_q1_bots.h"
#include "qa/game_q3_wire.h"
#include "qa/source_number.h"
#include "qa/text.h"

#include <limits.h>
#include <float.h>
#include <fenv.h>
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
    bool external_nq_physics;
    uint64_t application_elapsed_ns;
} application_move_call;

struct application_control_turn {
    application_move_call move;
    qa_movement_input input;
    application_provider **parked_owners;
    struct application_qc_parked_input **parked;
    size_t parked_count;
};
static bool guest_complete(qa_application *, qa_actor_id, const qa_movement_command *,
    const qa_q3_player *, const application_control_external_stage *, qa_error *);

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
    if (move->context.source_nqcmd) {
        const qa_think *pending = qa_scheduler_pending(scheduler, actor);
        if (pending && pending->execution_provider != move->context.frame.provider) {
            *result = (qa_think_result){.alive =
                qa_actors_get(qa_session_actors(move->application->session), actor) != NULL};
            return true;
        }
    }
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
        !source->state.native.q2_engine->callbacks &&
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
    if (source->kind == APPLICATION_PROVIDER_NATIVE && source->state.native.q2_engine &&
        !source->state.native.q2_engine->callbacks)
        return application_q2_control_outputs(source->state.native.q2_engine, actor, out, error);
    if (source->kind == APPLICATION_PROVIDER_Q2 || source->kind == APPLICATION_PROVIDER_Q3) {
        bool intermission;
        if (!application_source_intermission_read(source, &intermission, error)) return false;
        if (intermission) return true;
        if (source->kind == APPLICATION_PROVIDER_Q2) {
            qa_q2_player_info player;
            if (qa_q2_player_read(source->state.q2, actor, &player) && player.chase_target.registry) return true;
        }
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

static int32_t gravity_truncated(double gravity)
{
    if (gravity >= INT32_MAX) return INT32_MAX;
    if (gravity <= INT32_MIN) return INT32_MIN;
    return (int32_t)trunc(gravity);
}

static int16_t gravity_short(double gravity)
{
    int32_t value = gravity_truncated(gravity);
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
            record->state.data.nq.flags &= ~(uint32_t)APPLICATION_Q1_ONGROUND;
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
                : (double)record->application->physics->gravity *
                      (double)record->gravity_multiplier);
        if (record->ground.hit == QA_TRACE_HIT_NONE)
            record->state.data.q2.flags &= ~(uint32_t)APPLICATION_Q2_ONGROUND;
        else
            record->state.data.q2.flags |= APPLICATION_Q2_ONGROUND;
        break;
    case QA_MOVEMENT_Q2_RERELEASE:
        record->state.data.q2r.gravity = gravity_short(
            record->application->physics == NULL
                ? 800.0f
                : (double)record->application->physics->gravity *
                      (double)record->gravity_multiplier);
        if (record->ground.hit == QA_TRACE_HIT_NONE)
            record->state.data.q2r.flags &= ~(uint32_t)APPLICATION_Q2_ONGROUND;
        else
            record->state.data.q2r.flags |= APPLICATION_Q2_ONGROUND;
        break;
    case QA_MOVEMENT_Q3:
        {
            const application_control_context *source = application_control_frame_current(record->application, record->actor);
            if (!source || !source->source_usercmd)
                record->state.data.q3.gravity = gravity_truncated(
                    record->application->physics == NULL ? 800.0f :
                    (double)record->application->physics->gravity * (double)record->gravity_multiplier);
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

static qa_movement_ground state_ground(const qa_movement_state *state,
                                      qa_movement_ground fallback)
{
    switch (state->kind) {
    case QA_MOVEMENT_NETQUAKE:
        return (state->data.nq.flags & APPLICATION_Q1_ONGROUND)
            ? state->data.nq.ground : (qa_movement_ground){0};
    case QA_MOVEMENT_QUAKEWORLD:
        return state->data.qw.ground;
    case QA_MOVEMENT_Q3:
        return state->data.q3.ground;
    case QA_MOVEMENT_Q2_CLASSIC:
    case QA_MOVEMENT_Q2_RERELEASE:
        return fallback;
    }
    return fallback;
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
    shadow.ground = state_ground(call->state, shadow.ground);
    state_body(&shadow, &body, combat.health);
    *call->state = shadow.state; *call->bounds = body.bounds;
    call->environment->health = combat.health;
    call->environment->has_body_bounds = false;
    call->environment->has_mode = move->control->player_mode_set &&
        !(move->context.source_usercmd && call->state->kind == QA_MOVEMENT_Q3);
    call->environment->mode = move->control->player_mode;
    call->environment->flight = move->control->flight;
    call->environment->gravity_multiplier = move->control->gravity_multiplier;
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

static void character_fixed_pose(qa_movement_input *input)
{
    if (!input->environment.fixed_pose || !input->environment.fixed_crouched) return;
    qa_bounds bounds = input->environment.pose.bounds;
    qa_bounds expanded = input->invulnerability_bounds;
    bool is_expanded = bounds.mins.x == expanded.mins.x && bounds.mins.y == expanded.mins.y &&
        bounds.mins.z == expanded.mins.z && bounds.maxs.x == expanded.maxs.x &&
        bounds.maxs.y == expanded.maxs.y && bounds.maxs.z == expanded.maxs.z;
    input->environment.pose = input->crouched;
    if (is_expanded) input->environment.pose.bounds = expanded;
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

static bool q1_source_weapon_impulse(qa_application *app, qa_actor_id actor,
    qa_movement_command *command, qa_q1_input *input, qa_error *error)
{
    application_provider *source = application_world_provider(app, QA_ROLE_ENTITIES, "");
    qa_q1_source_client_view client;
    if (!source || source->kind != APPLICATION_PROVIDER_Q1 ||
        !qa_q1_source_client_read(source->state.q1, actor, &client) || !client.impulse)
        return true;
    bool consumed;
    if (!application_native_q1_source_impulse(app, actor, input, &consumed, error)) return false;
    if (consumed) {
        application_control_frames_consume_impulse(app, actor, command->sequence);
        command->impulse = input->impulse = 0;
    }
    return true;
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
    bool impulse_consumed;
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
        okay = application_native_q1_ctf_prethink(provider, call->actor, &call->input, error);
    if (okay && call->input.impulse && qa_actors_get(qa_session_actors(session), call->actor)) {
        qa_q1_source_client_view client;
        okay = qa_q1_source_client_read(provider->state.q1, call->actor, &client);
        if (!okay) application_fail(error, QA_ERROR_NOT_FOUND, "Q1 prethink lost its actual source input client");
        else call->impulse_consumed = client.impulse == 0;
    }
    if (okay && qa_actors_get(qa_session_actors(session), call->actor))
        okay = qa_q1_player_prethink(provider->state.q1, call->actor, error);
    qa_q1_game_operation_end(&operation);
    return okay;
}

bool application_control_q1_source_prethink(qa_application *app, qa_actor_id actor,
                                              const qa_movement_command *command, qa_error *error)
{
    application_provider *map = application_world_provider(app, QA_ROLE_ENTITIES, "");
    const application_control_context *context = application_control_frame_current(app, actor);
    bool raw_nq = context && context->source_nqcmd;
    qa_source_frame actual;
    if (!map || map->kind != APPLICATION_PROVIDER_Q1 ||
        !qa_q1_player_source_present(map->state.q1, actor) ||
        !qa_session_active_frame(app->session, map->owner, &actual) ||
        (!raw_nq && (application_control_frames_q1_prepared(app, actor, map->owner) ||
         !application_control_frames_q1_command_ready(app, actor, map->owner, command)))) return true;
    if (raw_nq) command = &context->source_command;
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
    if (call.impulse_consumed && command && live(app, actor))
        application_control_frames_consume_impulse(app, actor, command->sequence);
    return !live(app, actor) || application_control_frames_q1_complete(app, actor, map->owner, error);
}

static int32_t input_angle_word(float degrees)
{
    return (int32_t)(uint16_t)(int32_t)(fmodf(degrees, 360.0f) * (65536.0f / 360.0f));
}

static qa_vec3 command_angle_feedback(const qa_movement_command *command)
{
    if (command->kind != QA_MOVEMENT_Q3 && command->kind != QA_MOVEMENT_Q2_CLASSIC)
        return command->angles;
    float angles[3];
    for (size_t i = 0; i < 3; ++i) {
        uint32_t bits = (uint32_t)command->angle_words[i] & UINT32_C(65535);
        int32_t word = bits < UINT32_C(32768) ? (int32_t)bits : (int32_t)bits - 65536;
        angles[i] = (float)word * (360.0f / 65536.0f);
    }
    return qa_v3(angles[0], angles[1], angles[2]);
}

typedef struct application_control_mod_input {
    struct application_control_mod_input *next;
    qa_application *application;
    qa_actor_id actor;
    application_control_context source;
    qa_movement_state *state;
    qa_movement_command *command;
    qa_vec3 aim;
    int32_t impulse;
    uint64_t elapsed_ns, command_sequence;
    bool borrowed, suspended;
    size_t count;
    application_q3_component_input **scopes;
    struct application_native_q2_input **native_scopes;
    size_t native_count;
} application_control_mod_input;

static qa_vec3 component_aim(const qa_movement_state *state, const qa_movement_command *command)
{
    if (command->kind == state->kind &&
        (command->kind == QA_MOVEMENT_Q3 || command->kind == QA_MOVEMENT_Q2_CLASSIC)) {
        float angles[3];
        for (unsigned i = 0; i < 3; ++i) {
            int32_t delta = command->kind == QA_MOVEMENT_Q3 ? state->data.q3.delta_angle_words[i]
                : state->data.q2.delta_angle_shorts[i];
            angles[i] = (float)(uint16_t)((uint32_t)command->angle_words[i] + (uint32_t)delta) * (360.0f / 65536.0f);
        }
        return qa_v3(angles[0], angles[1], angles[2]);
    }
    return command->kind == state->kind && command->kind == QA_MOVEMENT_Q2_RERELEASE
        ? qa_vec_add(command->angles, state->data.q2r.delta_angles) : command->angles;
}

static bool component_input_current(const application_control_mod_input *scope, qa_error *error)
{
    const application_control_context *current = application_control_frame_current(scope->application, scope->actor);
    application_control_context active = scope->source;
    bool admitted = current != NULL;
    if (!current) {
        if (active.command_only) admitted = qa_session_active_command(scope->application->session,
            application_control_provider(&scope->source), &active.command) && qa_actor_id_equal(active.command.actor, scope->actor);
        else admitted = qa_session_active_frame(scope->application->session,
            application_control_provider(&scope->source), &active.frame);
        current = &active;
    }
    if (!scope->borrowed || scope->suspended || !live(scope->application, scope->actor) || !admitted ||
        application_control_provider(current) != application_control_provider(&scope->source) ||
        application_control_kind(current) != application_control_kind(&scope->source) ||
        application_control_time(current) != application_control_time(&scope->source) ||
        current->command_only != scope->source.command_only ||
        !scope->command || scope->command->sequence != scope->command_sequence ||
        (current->command_only && (current->command.phase != scope->source.command.phase ||
            current->command.completed_frame_number != scope->source.command.completed_frame_number ||
            current->command.elapsed_ns != scope->source.command.elapsed_ns ||
            current->command.host_elapsed_ns != scope->source.command.host_elapsed_ns)))
        return application_fail(error, QA_ERROR_ARGUMENT, "Component input lost its actual command borrow");
    return true;
}

static double component_move_scale(qa_movement_kind kind)
{ return kind == QA_MOVEMENT_Q3 ? 127.0 : kind == QA_MOVEMENT_NETQUAKE || kind == QA_MOVEMENT_QUAKEWORLD ? 320.0 : 200.0; }

static bool component_input_values(void *context, application_q3_mod_inputs *out, qa_error *error)
{
    application_control_mod_input *scope = context;
    if (!component_input_current(scope, error)) return false;
    const qa_movement_command *command = scope->command;
    double scale = component_move_scale(command->kind);
    bool rerelease = command->kind == QA_MOVEMENT_Q2_RERELEASE;
    bool q1 = command->kind == QA_MOVEMENT_NETQUAKE || command->kind == QA_MOVEMENT_QUAKEWORLD;
    application_q3_mod_inputs values = {0};
    values.values[Q3_MOD_SELF] = (application_q3_mod_value){.kind=Q3_MOD_VALUE_ACTOR,.as.actor=scope->actor};
    values.values[Q3_MOD_VIEW_ANGLES] = (application_q3_mod_value){.kind=Q3_MOD_VALUE_VECTOR,.as.vector=scope->aim};
#define SCALAR(name, value) values.values[name] = (application_q3_mod_value){.kind=Q3_MOD_VALUE_SCALAR,.as.scalar=(value)}
    SCALAR(Q3_MOD_ATTACK, (command->buttons & 1u) != 0);
    SCALAR(Q3_MOD_JUMP, q1 ? (command->buttons & 2u) != 0 : rerelease ? (command->buttons & 8u) != 0 : command->up_move > 0);
    SCALAR(Q3_MOD_IMPULSE, scope->impulse);
    SCALAR(Q3_MOD_FORWARD, command->forward_move / scale);
    SCALAR(Q3_MOD_SIDE, command->side_move / scale);
    SCALAR(Q3_MOD_UP, rerelease ? (command->buttons & 8u) ? 1 : (command->buttons & 16u) ? -1 : 0 : command->up_move / scale);
    SCALAR(Q3_MOD_TIME, (double)application_control_time(&scope->source) / 1e9);
    SCALAR(Q3_MOD_ELAPSED, (double)scope->elapsed_ns / 1e9);
#undef SCALAR
    *out = values; return true;
}

static bool component_input_set(application_control_mod_input *scope, application_q3_mod_input input,
    double value, const qa_vec3 *angles, qa_error *error)
{
    qa_movement_command next = *scope->command;
    if (input == Q3_MOD_VIEW_ANGLES) {
        if (!angles || !qa_vec_finite(*angles))
            return application_fail(error, QA_ERROR_ARGUMENT, "Component aim output must be finite");
        const double difference[] = {(double)angles->x - scope->aim.x,
            (double)angles->y - scope->aim.y, (double)angles->z - scope->aim.z};
        if (next.kind == QA_MOVEMENT_NETQUAKE) next.angles = *angles;
        else if (next.kind == QA_MOVEMENT_QUAKEWORLD || next.kind == QA_MOVEMENT_Q2_RERELEASE) {
            next.angles = qa_v3((float)qa_source_fround((double)next.angles.x + difference[0]),
                (float)qa_source_fround((double)next.angles.y + difference[1]),
                (float)qa_source_fround((double)next.angles.z + difference[2]));
            if (!qa_vec_finite(next.angles))
                return application_fail(error, QA_ERROR_ARGUMENT, "Component aim exceeds its command fields");
        } else {
            for (unsigned i = 0; i < 3; ++i) {
                uint32_t wrapped = (uint32_t)qa_number_to_i32(difference[i] * 65536.0 / 360.0);
                wrapped += (uint32_t)next.angle_words[i];
                memcpy(&next.angle_words[i], &wrapped, sizeof(wrapped));
            }
        }
        scope->aim = *angles;
    } else {
        if (!isfinite(value)) return application_fail(error, QA_ERROR_ARGUMENT, "Component input output must be finite");
        switch (input) {
        case Q3_MOD_ATTACK:
            next.buttons = value != 0 ? next.buttons | 1u : next.buttons & ~1u; break;
        case Q3_MOD_JUMP:
            if (next.kind == QA_MOVEMENT_NETQUAKE || next.kind == QA_MOVEMENT_QUAKEWORLD)
                next.buttons = value != 0 ? next.buttons | 2u : next.buttons & ~2u;
            else if (next.kind == QA_MOVEMENT_Q2_RERELEASE)
                next.buttons = value != 0 ? next.buttons | 8u : next.buttons & ~8u;
            else next.up_move = value == 0 ? fminf(0, next.up_move)
                : fmaxf(fmaxf(10, next.up_move), (float)component_move_scale(next.kind));
            break;
        case Q3_MOD_IMPULSE:
            if (value < 0 || value > 255 || value != trunc(value))
                return application_fail(error, QA_ERROR_ARGUMENT, "Component impulse output must fit one byte");
            scope->impulse = (int32_t)value;
            if (next.kind != QA_MOVEMENT_Q3 && next.kind != QA_MOVEMENT_Q2_RERELEASE) next.impulse = (uint8_t)value;
            break;
        case Q3_MOD_FORWARD: case Q3_MOD_SIDE: case Q3_MOD_UP: {
            if (input == Q3_MOD_UP && next.kind == QA_MOVEMENT_Q2_RERELEASE) {
                next.buttons = (next.buttons & ~(8u | 16u)) | (value > 0 ? 8u : value < 0 ? 16u : 0); break;
            }
            double scaled = value * component_move_scale(next.kind);
            if (next.kind == QA_MOVEMENT_Q2_RERELEASE) {
                if (!isfinite(scaled) || !isfinite(qa_source_fround(scaled)))
                    return application_fail(error, QA_ERROR_ARGUMENT, "Component movement exceeds its float command fields");
            } else {
                double minimum = next.kind == QA_MOVEMENT_Q3 ? -127 : -32768;
                double maximum = next.kind == QA_MOVEMENT_Q3 ? 127 : 32767;
                if (!isfinite(scaled) || scaled < minimum || scaled > maximum)
                    return application_fail(error, QA_ERROR_ARGUMENT, "Component movement exceeds its command fields");
                scaled = trunc(scaled);
            }
            float published = (float)qa_source_fround(scaled);
            if (input == Q3_MOD_FORWARD) next.forward_move = published;
            else if (input == Q3_MOD_SIDE) next.side_move = published;
            else next.up_move = published;
            break;
        }
        default: return application_fail(error, QA_ERROR_ARGUMENT, "Component output does not name a command input");
        }
    }
    *scope->command = next; return true;
}

static bool component_input_output(void *context, const application_q3_mod_output *output, qa_error *error)
{
    application_control_mod_input *scope = context;
    if (!component_input_current(scope, error)) return false;
    if (!output->consume) return component_input_set(scope, output->input,
        output->input == Q3_MOD_VIEW_ANGLES ? 0 : output->value.scalar,
        output->input == Q3_MOD_VIEW_ANGLES ? &output->value.angles : NULL, error);
    if (output->value.inputs & ~((1u << Q3_MOD_INPUT_COUNT) - 1u))
        return application_fail(error, QA_ERROR_ARGUMENT, "Component consume names an unknown input");
    for (unsigned i = 0; i < Q3_MOD_INPUT_COUNT; ++i) if (output->value.inputs & (1u << i)) {
        qa_vec3 zero = {0};
        if (!component_input_set(scope, (application_q3_mod_input)i, 0, i == Q3_MOD_VIEW_ANGLES ? &zero : NULL, error)) return false;
    }
    return true;
}

static void component_input_free(application_control_mod_input *scope)
{
    application_control_mod_input *head = application_control_mod_head(scope->application);
    application_control_mod_input **link = &head;
    while (*link && *link != scope) link = &(*link)->next;
    if (*link) *link = scope->next;
    application_control_mod_head_set(scope->application, head);
    free(scope->native_scopes); free(scope->scopes); free(scope);
}

static bool component_input_close(application_control_mod_input **in, bool completed, qa_error *error)
{
    application_control_mod_input *scope = *in;
    if (!scope) return true;
    bool ok = true; qa_error first = {0};
    if (completed) for (size_t i = 0; i < scope->count; ++i) {
        qa_error current = {0};
        if (!application_q3_component_input_complete(scope->scopes[i], true, component_input_values, scope, &current)) {
            if (ok) first = current;
            ok = false;
        }
    }
    for (size_t i = 0; i < scope->native_count; ++i) {
        qa_error current = {0};
        if (!application_native_q2_input_complete(scope->native_scopes[i], completed, &current)) {
            if (ok) first = current;
            ok = false;
        }
    }
    for (size_t i = scope->native_count; i > 0; --i) {
        qa_error current = {0};
        if (!application_native_q2_input_abort(scope->native_scopes + i - 1, &current)) {
            if (ok) first = current;
            ok = false;
        }
        if (scope->native_scopes[i - 1]) break;
    }
    bool native_retained = false;
    for (size_t i = 0; i < scope->native_count; ++i) native_retained |= scope->native_scopes[i] != NULL;
    for (size_t i = native_retained ? 0 : scope->count; i > 0; --i) {
        qa_error current = {0};
        bool closed = application_q3_component_input_abort(scope->scopes + i - 1, &current);
        if (!closed) { if (ok) first = current; ok = false; }
        if (scope->scopes[i - 1]) break;
    }
    bool retained = native_retained;
    for (size_t i = 0; i < scope->count; ++i) retained |= scope->scopes[i] != NULL;
    if (!retained) { component_input_free(scope); *in = NULL; }
    else scope->borrowed = false;
    if (!ok && error) *error = first;
    return ok;
}

bool application_control_mod_abort_all(qa_application *app, qa_error *error)
{
    while (application_control_mod_head(app)) {
        application_control_mod_input *scope = application_control_mod_head(app);
        scope->borrowed = false;
        if (!component_input_close(&scope, false, error)) return false;
    }
    return true;
}

static bool component_input_boundary(application_move_call *move, qa_movement_state *state,
    qa_movement_command *command, const qa_vec3 *aim, application_source_input_scope *scope,
    bool before, bool slice, uint64_t elapsed_ns, qa_error *error)
{
    if (!before) {
        application_control_mod_input *input = scope->components;
        if (!input) return true;
        input->state = state; input->command = command; input->borrowed = true; input->suspended = false;
        input->aim = aim ? *aim : component_aim(state, command);
        return component_input_close(&scope->components, live(move->application, move->control->actor), error);
    }
    size_t count = application_q3_components_count(move->application);
    size_t native_count = 0;
    for (size_t i = 0; i < move->application->provider_count; ++i) {
        application_provider *provider = move->application->providers[i];
        if (provider->kind == APPLICATION_PROVIDER_NATIVE && provider->attached &&
            provider->state.native.q2_engine && provider->state.native.q2_engine->callbacks) ++native_count;
    }
    if (!count && !native_count) return true;
    if (scope->components) return application_fail(error, QA_ERROR_ARGUMENT, "Component input scope is already entered");
    const application_control_context *source = application_control_frame_current(move->application, move->control->actor);
    if (!source) {
        if (!move->execution) return application_fail(error, QA_ERROR_ARGUMENT, "Component input requires its actual source owner");
        if (qa_session_active_command(move->application->session, move->execution->owner, &move->context.command) &&
            qa_actor_id_equal(move->context.command.actor, move->control->actor)) move->context.command_only = true;
        else if (!qa_session_active_frame(move->application->session, move->execution->owner, &move->context.frame))
            return application_fail(error, QA_ERROR_ARGUMENT, "Component input requires its actual source admission");
        source = &move->context;
    }
    application_control_mod_input *input = calloc(1, sizeof(*input));
    if (!input) return application_fail(error, QA_ERROR_MEMORY, "Retaining canonical component input");
    input->scopes = count ? calloc(count, sizeof(*input->scopes)) : NULL;
    input->native_scopes = native_count ? calloc(native_count, sizeof(*input->native_scopes)) : NULL;
    if ((count && !input->scopes) || (native_count && !input->native_scopes)) {
        free(input->native_scopes); free(input->scopes); free(input);
        return application_fail(error, QA_ERROR_MEMORY, "Retaining component input roster");
    }
    input->application = move->application; input->actor = move->control->actor; input->source = *source;
    input->state = state; input->command = command; input->command_sequence = command->sequence;
    input->elapsed_ns = elapsed_ns; input->borrowed = true;
    input->aim = aim ? *aim : component_aim(state, command);
    input->next = application_control_mod_head(move->application);
    input->impulse = command->impulse;
    if (command->kind == QA_MOVEMENT_Q3 || command->kind == QA_MOVEMENT_Q2_RERELEASE) {
        if (source->unified_has_impulse) input->impulse = source->unified_impulse;
        else if (input->next && qa_actor_id_equal(input->next->actor, input->actor)) input->impulse = input->next->impulse;
    }
    application_control_mod_head_set(move->application, input); scope->components = input;
    for (size_t i = 0; i < count && live(move->application, input->actor); ++i) {
        application_q3_component *component = NULL;
        if (!application_q3_components_at(move->application, i, &component, error)) return false;
        input->count = i + 1;
        if (!application_q3_component_input_begin(component, input->actor, slice, component_input_values,
            component_input_output, input, input->scopes + i, error)) return false;
    }
    for (size_t i = 0; i < move->application->provider_count && live(move->application, input->actor); ++i) {
        application_provider *provider = move->application->providers[i];
        if (provider->kind != APPLICATION_PROVIDER_NATIVE || !provider->attached ||
            !provider->state.native.q2_engine || !provider->state.native.q2_engine->callbacks) continue;
        struct application_native_q2 *native=provider->state.native.q2_engine;
        if(native->input_stage && qa_actor_id_equal(native->input_stage->actor,input->actor) &&
            native->input_stage->current(native->input_stage->context,input->actor) &&
            application_native_q2_declared_raw_capable(provider) &&
            application_native_q2_callbacks_transfer_current(native->callbacks) &&
            source->source_q2cmd && application_control_provider(source)==provider->owner)
            continue;
        size_t index = input->native_count++;
        if (!application_native_q2_input_begin(provider->state.native.q2_engine, input->actor, slice,
            component_input_values, component_input_output, input, input->native_scopes + index, error)) return false;
    }
    return true;
}

bool application_control_mod_usercmd(void *context, qa_actor_id actor,
    const application_q3_mod_inputs *inputs, const qa_q3_player *player,
    qa_q3_usercmd *out, qa_error *error)
{
    qa_application *app = context;
    if (!app || !inputs || !player || !out)
        return application_fail(error, QA_ERROR_ARGUMENT, "Component usercmd needs its actual working input and source player");
    application_control_mod_input *scope = application_control_mod_head(app);
    while (scope && !qa_actor_id_equal(scope->actor, actor)) scope = scope->next;
    if (!scope || !component_input_current(scope, error))
        return scope ? false : application_fail(error, QA_ERROR_ARGUMENT, "Component usercmd has no entered canonical input");
    application_q3_mod_inputs current;
    if (!component_input_values(scope, &current, error)) return false;
    for (unsigned i = 0; i < Q3_MOD_INPUT_COUNT; ++i) {
        const application_q3_mod_value *left = &current.values[i], *right = &inputs->values[i];
        if (left->kind != right->kind || (left->kind == Q3_MOD_VALUE_SCALAR && left->as.scalar != right->as.scalar) ||
            (left->kind == Q3_MOD_VALUE_VECTOR && !same_vector(left->as.vector, right->as.vector)))
            return application_fail(error, QA_ERROR_ARGUMENT, "Component usercmd input is not the current command projection");
    }
    const qa_movement_command *command = scope->command;
    uint64_t milliseconds = application_control_time(&scope->source) / UINT64_C(1000000);
    if (command->kind != QA_MOVEMENT_Q3 && milliseconds > INT32_MAX)
        return application_fail(error, QA_ERROR_ARGUMENT, "Component command clock exceeds the actual Q3 ABI");
    qa_q3_usercmd result = {.serverTime = command->kind == QA_MOVEMENT_Q3 ? command->server_time_ms : (int32_t)milliseconds,
        .buttons = (int32_t)(command->kind == QA_MOVEMENT_Q3 ? command->buttons : command->buttons & 1u),
        .weapon = (uint8_t)player->weapon};
    const double aim[] = {scope->aim.x, scope->aim.y, scope->aim.z};
    for (unsigned i = 0; i < 3; ++i) {
        uint32_t word = (uint32_t)(uint16_t)(int32_t)(fmod(aim[i], 360.0) * 65536.0 / 360.0);
        word -= (uint32_t)player->deltaAngles[i];
        memcpy(result.angles + i, &word, sizeof(word));
    }
    double scale = 127.0 / component_move_scale(command->kind);
    double forward = trunc(fmax(-127, fmin(127, command->forward_move * scale)));
    double side = trunc(fmax(-127, fmin(127, command->side_move * scale)));
    double up = command->kind == QA_MOVEMENT_Q2_RERELEASE ? command->buttons & 8u ? 127 : command->buttons & 16u ? -127 : 0 :
        (command->kind == QA_MOVEMENT_NETQUAKE || command->kind == QA_MOVEMENT_QUAKEWORLD) && (command->buttons & 2u) ? 127 :
        trunc(fmax(-127, fmin(127, command->up_move * scale)));
    if (command->kind == QA_MOVEMENT_Q3) {
        const float axes[] = {command->forward_move, command->side_move, command->up_move};
        for (unsigned i = 0; i < 3; ++i) if (!isfinite(axes[i]) || axes[i] < -128 || axes[i] > 127 || axes[i] != truncf(axes[i]))
            return application_fail(error, QA_ERROR_ARGUMENT, "Component Q3 command does not fit its actual byte axes");
        forward = axes[0]; side = axes[1]; up = axes[2];
    }
    result.forwardmove = (int8_t)forward; result.rightmove = (int8_t)side; result.upmove = (int8_t)up;
    if (!component_input_current(scope, error)) return false;
    *out = result; return true;
}

static void component_input_retarget(application_source_input_scope *scope,
    qa_movement_state *state, qa_movement_command *command, bool suspended)
{
    if (!scope->components) return;
    scope->components->state = state; scope->components->command = command;
    scope->components->suspended = suspended;
}

static bool qc_input_body(application_move_call *move, qa_movement_state *state,
                       qa_movement_command *command, const qa_vec3 *absolute_aim,
                       application_source_input_scope *scope,
                       bool before, bool slice,
                       uint64_t elapsed_ns, qa_error *error)
{
    static const qa_launch_role roles[] = {QA_ROLE_MOVEMENT, QA_ROLE_CHARACTER, QA_ROLE_ARSENAL,
        QA_ROLE_INVENTORY, QA_ROLE_COMBAT, QA_ROLE_EFFECTS, QA_ROLE_EQUIPMENT};
    if (before && (scope->owners || scope->count))
        return application_fail(error, QA_ERROR_ARGUMENT, "Source input scope is already open");
    size_t owner_count = scope->count;
    if (before) {
        bool has_qc = false;
        for (size_t i = 0; i < move->application->provider_count; ++i)
            has_qc |= move->application->providers[i] &&
                move->application->providers[i]->kind == APPLICATION_PROVIDER_QC;
        if (!has_qc) return true;
        size_t role_count = sizeof(roles) / sizeof(roles[0]);
        if (move->application->provider_count > SIZE_MAX / sizeof(*scope->owners) - role_count)
            return application_fail(error, QA_ERROR_MEMORY, "Source input owner inventory is too large");
        size_t capacity = move->application->provider_count + role_count;
        scope->owners = calloc(capacity, sizeof(*scope->owners));
        if (!scope->owners)
            return application_fail(error, QA_ERROR_MEMORY, "Allocating Source input owner inventory");
        for (size_t i = 0; i < role_count; ++i) {
            application_provider *owner = application_provider_for(move->application, move->control->actor, roles[i], "");
            if (!owner || owner->kind != APPLICATION_PROVIDER_QC) continue;
            bool duplicate = false;
            for (size_t j = 0; j < owner_count; ++j) duplicate |= scope->owners[j] == owner;
            if (!duplicate) scope->owners[owner_count++] = owner;
        }
        for (size_t i = 0; i < move->application->provider_count; ++i) {
            application_provider *owner = move->application->providers[i];
            if (!owner || owner->kind != APPLICATION_PROVIDER_QC || !owner->constructed ||
                !owner->attached || owner->close_pending || !owner->state.qc.qualified) continue;
            bool duplicate = false, subscribed = false;
            for (size_t j = 0; j < owner_count; ++j) duplicate |= scope->owners[j] == owner;
            if (duplicate) continue;
            const struct application_qc_profile *profile = owner->state.qc.qualified;
            for (size_t j = 0; j < profile->input_count; ++j)
                subscribed |= profile->input[j].movement_slice == slice;
            if (!subscribed) continue;
            bool member = false;
            if (!application_qc_control_source_client(owner, move->control->actor, &member, error)) {
                free(scope->owners); scope->owners = NULL;
                return false;
            }
            if (member) scope->owners[owner_count++] = owner;
        }
    }
    if (!owner_count) { free(scope->owners); scope->owners = NULL; return true; }
    if (before) { scope->actor = move->control->actor; scope->slice = slice; }
    move->qc_input_active = true;
    qa_movement_command semantic = *command;
    semantic.angles = absolute_aim ? *absolute_aim : component_aim(state, command);
    qa_vec3 original_aim = semantic.angles;
    for (size_t i = 0; i < owner_count; ++i) {
        application_provider *owner = scope->owners[i];
        if (!owner || owner->kind != APPLICATION_PROVIDER_QC) continue;
        if (!owner->state.qc.qualified && move->context.source_nqcmd && owner == move->execution)
            continue;
        if (!owner->state.qc.qualified && move->context.path == APPLICATION_CONTROL_QW_GROUP && owner == move->execution)
            continue;
        if (!owner->state.qc.qualified && move->context.path == APPLICATION_CONTROL_NQ_TURN &&
            owner == move->execution && (!before || slice || move->context.stage != APPLICATION_CONTROL_PREPARE))
            continue;
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
    if (!before) {
        free(scope->owners); scope->owners = NULL;
        scope->count = 0; scope->actor = (qa_actor_id){0}; scope->slice = false;
    }
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

static bool qc_input(application_move_call *move, qa_movement_state *state,
    qa_movement_command *command, const qa_vec3 *aim, application_source_input_scope *scope,
    bool before, bool slice, uint64_t elapsed_ns, qa_error *error)
{
    if (before) {
        scope->actor = move->control->actor; scope->slice = slice;
        if (!qc_input_body(move, state, command, aim, scope, true, slice, elapsed_ns, error)) return false;
        return component_input_boundary(move, state, command, aim, scope, true, slice, elapsed_ns, error);
    }
    if (!component_input_boundary(move, state, command, aim, scope, false, slice, elapsed_ns, error)) return false;
    return qc_input_body(move, state, command, aim, scope, false, slice, elapsed_ns, error);
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
    const application_control_context *current = application_control_frame_current(application, actor);
    if (current) move.context = *current;
    move.execution = control_execution(application, actor);
    return qc_input(&move, state, command, absolute_aim, scope, before, slice, elapsed_ns, error);
}

bool application_control_source_abort(application_source_input_scope *scope, qa_error *error)
{
    if (!scope)
        return application_fail(error, QA_ERROR_ARGUMENT, "Source input cleanup needs its scope");
    bool ok = true;
    qa_error first = {0};
    if (scope->components) {
        scope->components->borrowed = false;
        if (!component_input_close(&scope->components, false, &first)) ok = false;
        if (scope->components) { if (error) *error = first; return false; }
    }
    while (scope->count) {
        application_provider *provider = scope->owners[--scope->count];
        scope->owners[scope->count] = NULL;
        if (!provider) continue;
        qa_error current = {0};
        if (!application_qc_input_abort(provider, scope->actor, scope->slice, &current) && ok) {
            ok = false; first = current;
        }
    }
    free(scope->owners); scope->owners = NULL;
    scope->actor = (qa_actor_id){0}; scope->slice = false;
    if (!ok && error) *error = first;
    return ok;
}

static qa_movement_control move_phase_body(void *opaque, qa_movement_phase phase,
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
    bool foreign_nq = move->context.source_nqcmd && call->state->kind != QA_MOVEMENT_NETQUAKE;
    if (foreign_nq && move->context.stage == APPLICATION_CONTROL_PHYSICS &&
        (phase == QA_MOVE_INPUT_BEGIN || phase == QA_MOVE_INPUT_END))
        return QA_MOVEMENT_CONTINUE;
    bool source_phase = (phase == QA_MOVE_PRETHINK || phase == QA_MOVE_THINK || phase == QA_MOVE_POSTTHINK) &&
        (!foreign_nq || move->in_source_outer) &&
        !(move->external_nq_physics && phase == QA_MOVE_POSTTHINK && !move->in_source_outer);
    if (source_phase) {
        qa_body_state source_body;
        if (!qa_world_body_read(move->application->world, actor, &source_body, error)) return QA_MOVEMENT_ERROR;
        qa_movement_ground ground = state_ground(call->state,
            source_body.ground.registry ? (qa_movement_ground){.hit = QA_TRACE_HIT_ACTOR,
                .actor = source_body.ground} : (qa_movement_ground){0});
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

    if (phase == QA_MOVE_INPUT_BEGIN && qw_spectator && move->world &&
        move->world->kind == APPLICATION_PROVIDER_Q1 &&
        move->world->component.clock.kind == QA_CLOCK_QUAKEWORLD) {
        qa_q1_input input = q1_input(call);
        input.view_angles = call_view_angles(move, call);
        move->committed = true;
        if (!qa_q1_player_source_input(move->world->state.q1, actor, &input, error))
            return QA_MOVEMENT_ERROR;
    }
    if (phase == QA_MOVE_INPUT_BEGIN && !qw_spectator && !move->q1_input &&
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
    if (source_phase && phase == QA_MOVE_PRETHINK && !qw_spectator && !move->q1_prethink &&
        move->world && move->world->kind == APPLICATION_PROVIDER_Q1 &&
        qa_q1_player_source_present(move->world->state.q1, actor)) {
        move->committed = true;
        if (!application_control_q1_source_prethink(move->application, actor, call->command, error))
            return QA_MOVEMENT_ERROR;
        move->q1_prethink = true;
        move->q1_map_frame = true;
    }
    if (source_phase && phase == QA_MOVE_PRETHINK && !qw_spectator && !move->q1_map_frame) {
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
    if (source_phase && phase == QA_MOVE_PRETHINK && !qw_spectator && move->execution && move->execution->kind == APPLICATION_PROVIDER_Q1 &&
        move->context.path != APPLICATION_CONTROL_NQ_TURN && call->state->kind != QA_MOVEMENT_NETQUAKE &&
        (move->context.path != APPLICATION_CONTROL_MIXED || move->in_source_outer)) {
        qa_think_result result; move->committed = true;
        if (!source_think(move, actor, call, &result, error)) return QA_MOVEMENT_ERROR;
        if (!live(move->application, actor)) return QA_MOVEMENT_REMOVED;
    }
    bool source_weapon_slice = application_guest_input_weapon_slice(move->application, actor);
    if (phase == QA_MOVE_WEAPON && !source_weapon_slice && !qw_spectator &&
        !move->context.defer_postthink) {
        qa_q1_input input = q1_input(call);
        input.view_angles = call_view_angles(move, call);
        if (!q1_source_weapon_impulse(move->application, actor, call->command, &input, error))
            return QA_MOVEMENT_ERROR;
        if (!live(move->application, actor)) return QA_MOVEMENT_REMOVED;
    }
    if (phase == QA_MOVE_WEAPON && !source_weapon_slice &&
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
        qa_movement_ground ground = state_ground(call->state, move->control->ground);
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
        if (phase == QA_MOVE_WEAPON && source_weapon_slice && move->q3[index] == move->arsenal)
            continue;
        move->committed = true;
        qa_movement_control result = qa_q3_movement_phase_selected(
            move->q3[index]->state.q3, phase, call, move->q3[index] == move->arsenal, error);
        if (result != QA_MOVEMENT_CONTINUE)
            return result;
    }

    if (phase == QA_MOVE_WEAPON && !source_weapon_slice && move->arsenal != NULL &&
        move->arsenal->kind == APPLICATION_PROVIDER_Q3 &&
        call->command->kind != QA_MOVEMENT_Q3 &&
        q3_present(move->arsenal, actor)) {
        qa_q3_controls controls = {
            .attack = (call->command->buttons & 1u) != 0,
            .use_holdable = move->context.unified_intent ? move->context.unified_holdable :
                move->context.source_usercmd ? move->context.source_holdable :
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

static qa_movement_control move_phase(void *opaque, qa_movement_phase phase,
    qa_movement_call *call, qa_error *error)
{
    application_move_call *move = opaque;
    const qa_movement_call *previous = application_control_frames_call_swap(move->application, call);
    qa_movement_control result = move_phase_body(opaque, phase, call, error);
    application_control_frames_call_swap(move->application, previous);
    return result;
}

static qa_movement_control move_touch_body(void *opaque,
                                      const qa_trace_result *trace,
                                      qa_movement_call *call,
                                      qa_error *error)
{
    application_move_call *move = opaque;
    if (move->context.source_usercmd) return QA_MOVEMENT_CONTINUE;
    if (!publish_result_body(move, call->state, *call->bounds,
            state_ground(call->state, move->control->ground),
            call_view_angles(move, call), false, false, error))
        return QA_MOVEMENT_ERROR;
    if (!live(move->application, call->actor)) return QA_MOVEMENT_REMOVED;
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

static qa_movement_control move_touch(void *opaque, const qa_trace_result *trace,
    qa_movement_call *call, qa_error *error)
{
    application_move_call *move = opaque;
    const qa_movement_call *previous = application_control_frames_call_swap(move->application, call);
    qa_movement_control result = move_touch_body(opaque, trace, call, error);
    application_control_frames_call_swap(move->application, previous);
    return result;
}

static qa_movement_control move_effect_body(void *opaque,
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

static qa_movement_control move_effect(void *opaque, const qa_movement_effect *effect,
    qa_movement_call *call, qa_error *error)
{
    application_move_call *move = opaque;
    const qa_movement_call *previous = application_control_frames_call_swap(move->application, call);
    qa_movement_control result = move_effect_body(opaque, effect, call, error);
    application_control_frames_call_swap(move->application, previous);
    return result;
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

static bool movement_numeric(qa_application *app, application_provider *provider,
    qa_movement_kind kind, bool prediction, qa_application_movement_numeric *out, qa_error *error)
{
    *out = (qa_application_movement_numeric){0};
    if (!prediction && provider->kind != APPLICATION_PROVIDER_Q1 && provider->kind != APPLICATION_PROVIDER_Q2 &&
        provider->kind != APPLICATION_PROVIDER_Q3 && provider->kind != APPLICATION_PROVIDER_QC) return true;
#if !defined(__GNUC__) && !defined(__clang__)
    return application_fail(error, QA_ERROR_UNSUPPORTED, "Native movement has no declared C contraction policy");
#else
    qa_application_numeric_rounding rounding;
    switch (fegetround()) {
    case FE_TONEAREST: rounding = QA_APPLICATION_ROUND_NEAREST; break;
    case FE_DOWNWARD: rounding = QA_APPLICATION_ROUND_DOWN; break;
    case FE_UPWARD: rounding = QA_APPLICATION_ROUND_UP; break;
    case FE_TOWARDZERO: rounding = QA_APPLICATION_ROUND_ZERO; break;
    default: return application_fail(error, QA_ERROR_UNSUPPORTED, "Native movement has an unknown rounding environment");
    }
    qa_string_id id;
    if (!qa_strings_intern_cstr(qa_session_strings(app->session), "qa:numeric/movement-c", &id, error)) return false;
    *out = (qa_application_movement_numeric){.id = id, .radix = FLT_RADIX,
        .scalar_mantissa_bits = FLT_MANT_DIG, .double_mantissa_bits = DBL_MANT_DIG,
        .evaluation_method = FLT_EVAL_METHOD, .rounding = rounding, .native_c = true,
        .qw_origin_binary64 = kind == QA_MOVEMENT_QUAKEWORLD};
    return true;
#endif
}

static bool control_numeric_current(qa_application *app, qa_actor_id actor,
    const qa_application_movement_numeric *numeric, bool prediction, qa_error *error)
{
    application_provider *provider;
    if (!app || !numeric || !live(app, actor) || !movement_provider(app, actor, &provider, error)) return false;
    bool native = prediction || provider->kind == APPLICATION_PROVIDER_Q1 || provider->kind == APPLICATION_PROVIDER_Q2 ||
        provider->kind == APPLICATION_PROVIDER_Q3 || provider->kind == APPLICATION_PROVIDER_QC;
    if (!native) return (!numeric->native_c && !numeric->id && !numeric->radix &&
        !numeric->scalar_mantissa_bits && !numeric->double_mantissa_bits &&
        !numeric->evaluation_method && !numeric->rounding && !numeric->qw_origin_binary64) ||
        application_fail(error, QA_ERROR_FORMAT, "External movement cannot claim native C arithmetic");
    qa_application_numeric_rounding rounding;
    switch (fegetround()) {
    case FE_TONEAREST: rounding = QA_APPLICATION_ROUND_NEAREST; break;
    case FE_DOWNWARD: rounding = QA_APPLICATION_ROUND_DOWN; break;
    case FE_UPWARD: rounding = QA_APPLICATION_ROUND_UP; break;
    case FE_TOWARDZERO: rounding = QA_APPLICATION_ROUND_ZERO; break;
    default: return application_fail(error, QA_ERROR_UNSUPPORTED, "Native movement lost its rounding environment");
    }
    const char *id = qa_strings_cstr(qa_session_strings(app->session), numeric->id);
    return (numeric->native_c && id && !strcmp(id, "qa:numeric/movement-c") &&
        numeric->radix == FLT_RADIX && numeric->scalar_mantissa_bits == FLT_MANT_DIG &&
        numeric->double_mantissa_bits == DBL_MANT_DIG && numeric->evaluation_method == FLT_EVAL_METHOD &&
        numeric->rounding == rounding && numeric->qw_origin_binary64 ==
            (provider->component.clock.kind == QA_CLOCK_QUAKEWORLD)) ||
        application_fail(error, QA_ERROR_UNSUPPORTED, "Native movement numeric recipe differs from its execution environment");
}

bool application_control_numeric_current(qa_application *app, qa_actor_id actor,
    const qa_application_movement_numeric *numeric, qa_error *error)
{ return control_numeric_current(app, actor, numeric, false, error); }

bool application_control_prediction_numeric_current(qa_application *app, qa_actor_id actor,
    const qa_application_movement_numeric *numeric, qa_error *error)
{ return control_numeric_current(app, actor, numeric, true, error); }

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
    qa_application_movement_numeric numeric, prediction_numeric;
    if (!movement_numeric(application, provider, kind, false, &numeric, error) ||
        !movement_numeric(application, provider, kind, true, &prediction_numeric, error)) return false;
    qa_movement_input defaults = qa_movement_input_default(kind, actor);
    *record = (application_control_record){
        .application = application,
        .actor = actor,
        .state = qa_movement_state_default(kind, body.origin),
        .profile = selected_profile(application, provider),
        .numeric = numeric,
        .prediction_numeric = prediction_numeric,
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

bool application_control_guest_equipment(qa_application *app, application_provider *primary,
    qa_actor_id actor, application_q3_weapons *weapons, qa_q3_equipment_motion *out, qa_error *error)
{
    if (!app || !primary || primary->application != app || primary->kind != APPLICATION_PROVIDER_QVM ||
        !primary->constructed || !primary->attached || primary->close_pending || !out ||
        !live(app, actor) || actor.slot >= app->control_capacity ||
        !app->controls[actor.slot].active || app->controls[actor.slot].retired ||
        !qa_actor_id_equal(app->controls[actor.slot].actor, actor) ||
        application_world_provider(app, QA_ROLE_ENTITIES, "") != primary)
        return application_fail(error, QA_ERROR_ARGUMENT,
            "Original equipment motion requires its admitted source player");
    qa_q3_equipment_motion motion = {.speed_multiplier = 1};
    application_provider *arsenal = application_provider_for(app, actor, QA_ROLE_ARSENAL, "");
    if (!arsenal || !arsenal->constructed || !arsenal->attached || arsenal->close_pending)
        return application_fail(error, QA_ERROR_ARGUMENT, "Original equipment motion lost its selected arsenal");
    /* Original QVM primary equipment is independently owned by the selected
     * native Q3 source. The primary built-in missionpack exception cannot
     * occur for this admitted original GAME owner. */
    if (arsenal->kind != APPLICATION_PROVIDER_Q3) { *out = motion; return true; }
    qa_q3_player_state player;
    if (!qa_q3_player_read(arsenal->state.q3, actor, &player) || !(player.selections & QA_Q3_ARSENAL))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Selected equipment motion lost its actual arsenal player");
    bool available;
    if (!weapons)
        return application_fail(error, QA_ERROR_UNSUPPORTED,
            "Selected equipment motion needs its original weapon availability profile");
    if (!application_q3_weapons_available(weapons, actor, false, &available, error)) return false;
    if (!available) { *out = motion; return true; }
    application_provider *character = application_provider_for(app, actor, QA_ROLE_CHARACTER, "");
    if (!character || !character->constructed || !character->attached || character->close_pending)
        return application_fail(error, QA_ERROR_ARGUMENT, "Original equipment pose lost its selected character");
    qa_movement_input postures = qa_movement_input_default(
        character->component.clock.kind == QA_CLOCK_Q3 ? QA_MOVEMENT_Q3 : QA_MOVEMENT_NETQUAKE, actor);
    if (!qa_q3_selected_equipment_motion(arsenal->state.q3, actor, &postures.crouched, &motion, error)) return false;
    if (!live(app, actor) || application_provider_for(app, actor, QA_ROLE_ARSENAL, "") != arsenal ||
        application_provider_for(app, actor, QA_ROLE_CHARACTER, "") != character ||
        application_world_provider(app, QA_ROLE_ENTITIES, "") != primary)
        return application_fail(error, QA_ERROR_NOT_FOUND, "Original equipment motion source changed during pose admission");
    int32_t haste;
    if (!application_q3_weapons_powerup_until(weapons, actor, APPLICATION_Q3_HASTE, &haste, error)) return false;
    motion.speed_multiplier /= haste == 0 ? 1.0 : 1.3;
    *out = motion;
    return true;
}

typedef struct guest_weapon_delay {
    qa_application *app;
    application_provider *primary, *arsenal;
    application_q3_weapons *weapons;
} guest_weapon_delay;

static bool guest_weapon_delay_current(const guest_weapon_delay *delay, qa_actor_id actor,
    qa_error *error)
{
    return (live(delay->app, actor) && delay->primary->constructed && delay->primary->attached &&
        !delay->primary->close_pending && delay->arsenal->constructed && delay->arsenal->attached &&
        !delay->arsenal->close_pending &&
        application_world_provider(delay->app, QA_ROLE_ENTITIES, "") == delay->primary &&
        application_provider_for(delay->app, actor, QA_ROLE_ARSENAL, "") == delay->arsenal &&
        application_guest_input_weapon_slice(delay->app, actor)) ||
        application_fail(error, QA_ERROR_NOT_FOUND, "Original weapon slice lost its source or selected arsenal");
}

static bool guest_weapon_firing_delay(void *context, qa_actor_id actor, int32_t input,
    int32_t *out, qa_error *error)
{
    guest_weapon_delay *delay = context;
    return guest_weapon_delay_current(delay, actor, error) &&
        application_q3_weapons_delay(delay->weapons, actor, input, out, error) &&
        guest_weapon_delay_current(delay, actor, error);
}

bool application_control_guest_weapon_step(qa_application *app, qa_actor_id actor,
    const qa_movement_command *command, const qa_q3_player *player, bool reached, qa_error *error)
{
    application_provider *primary = app ? application_world_provider(app, QA_ROLE_ENTITIES, "") : NULL;
    struct application_q3_guest *engine = primary ? q3g_engine(primary) : NULL;
    application_provider *arsenal = app ? application_provider_for(app, actor, QA_ROLE_ARSENAL, "") : NULL;
    if (!app || !command || !player || command->kind != QA_MOVEMENT_Q3 ||
        !primary || primary->kind != APPLICATION_PROVIDER_QVM || !engine || !engine->game ||
        !engine->game->weapons || !arsenal || arsenal == primary || !live(app, actor) ||
        actor.slot >= app->control_capacity || !app->controls[actor.slot].active ||
        app->controls[actor.slot].retired || !qa_actor_id_equal(app->controls[actor.slot].actor, actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Selected weapons need their actual completed original slice");
    guest_weapon_delay delay = {app, primary, arsenal, engine->game->weapons};
    if (!guest_weapon_delay_current(&delay, actor, error)) return false;
    qa_movement_command applied = *command;
    qa_vec3 aim = qa_v3(player->viewangles[0], player->viewangles[1], player->viewangles[2]);
    qa_application_control_view control;
    if (!qa_vec_finite(aim) || !qa_application_control_read(app, actor, &control))
        return application_fail(error, QA_ERROR_ARGUMENT, "Original weapon slice lost its current source view");
    int32_t source_water;
    if (!application_q3_weapons_water_level(delay.weapons, actor, &source_water, error) ||
        !guest_weapon_delay_current(&delay, actor, error)) return false;
    bool selected = qa_equipment_primary_selected(app->equipment, actor);
    if (!selected || !reached) applied.buttons &= ~1u;
    bool okay;
    if (arsenal->kind == APPLICATION_PROVIDER_Q3) {
        qa_q3_arsenal_source source = {.view_angles = aim, .view_height = (float)player->viewheight,
            .health = (float)player->stats[0], .legs_animation = player->legsAnim,
            .torso_animation = player->torsoAnim, .legs_timer_ms = player->legsTimer,
            .torso_timer_ms = player->torsoTimer, .context = &delay, .firing_delay = guest_weapon_firing_delay};
        qa_q3_controls controls = {.attack = (applied.buttons & 1u) != 0,
            .use_holdable = (applied.buttons & 4u) != 0, .requested_weapon = (qa_q3_weapon)applied.weapon};
        okay = qa_q3_arsenal_source_step(arsenal->state.q3, actor, &controls,
            (float)applied.milliseconds, &source, error);
    } else if (arsenal->kind == APPLICATION_PROVIDER_Q1) {
        qa_q1_player_view admitted;
        uint64_t time; double elapsed;
        if (!qa_q1_player_read(arsenal->state.q1, actor, &admitted) ||
            !qa_q1_game_clock_read(arsenal->state.q1, &time, &elapsed))
            return application_fail(error, QA_ERROR_NOT_FOUND, "Original slice has no admitted selected Q1 arsenal");
        qa_q1_game_operation operation = {0};
        if (!qa_q1_game_command_begin(arsenal->state.q1, time,
            (uint64_t)applied.milliseconds * UINT64_C(1000000), &operation, error)) return false;
        qa_q1_input controls = {.view_angles = aim,
            .attack = player->stats[0] > 0 && (applied.buttons & 1u) && !(applied.buttons & 2u),
            .water_level = (uint8_t)(source_water < 0 ? 0 : source_water > 3 ? 3 : source_water),
            .water_type = control.water_type};
        okay = qa_q1_player_input(arsenal->state.q1, actor, &controls, error) &&
            (!live(app, actor) || qa_q1_player_postthink(arsenal->state.q1, actor, error));
        if (okay && !qa_q1_game_operation_live(&operation))
            okay = application_fail(error, QA_ERROR_NOT_FOUND, "Selected Q1 weapon slice retired its actual owner");
        qa_q1_game_operation_end(&operation);
    } else if (arsenal->kind == APPLICATION_PROVIDER_Q2) {
        qa_q2_weapon_input controls;
        okay = application_q2_weapon_input(arsenal, actor, &controls, error);
        if (okay && live(app, actor)) {
            controls.angles = aim; controls.view_height = (float)player->viewheight;
            controls.attack = player->stats[0] > 0 && (applied.buttons & 1u);
            controls.quad_until_ns = 0; controls.haste = false;
            okay = qa_q2_weapon_early_turn(arsenal->state.q2, actor, &controls, error);
        }
    } else return application_fail(error, QA_ERROR_UNSUPPORTED,
        "Original slice needs a genuine selected arsenal step capability");
    return okay && (!live(app, actor) || guest_weapon_delay_current(&delay, actor, error));
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
    if (!move->character || !move->character->constructed ||
        !move->character->attached || move->character->close_pending)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "Movement posture lost its selected character owner");
    qa_movement_input postures = qa_movement_input_default(
        move->character->component.clock.kind == QA_CLOCK_Q3 ? QA_MOVEMENT_Q3 : QA_MOVEMENT_NETQUAKE,
        record->actor);
    input->standing = postures.standing;
    input->crouched = postures.crouched;
    input->dead = postures.dead;
    input->invulnerability_bounds = postures.invulnerability_bounds;
    input->state = record->state;
    application_control_frames_state(application, record->actor, &input->state);
    input->command = *command;
    input->profile = record->profile;
    input->shape.bounds = record->state.kind == QA_MOVEMENT_Q3
        ? input->standing.bounds : body.bounds;
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
    if (move->context.source_nqcmd && move->execution && move->execution->kind == APPLICATION_PROVIDER_QC) {
        move->committed = true;
        if (!application_qc_player_command(move->execution, record->actor,
            &move->context.source_command, error)) return false;
    }
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
        bool prepare_weapon = move->q3[index] != move->arsenal ||
            !application_guest_input_weapon_slice(application, record->actor);
        if (!qa_q3_prepare_movement_selected(move->q3[index]->state.q3,
                                    record->actor, input, prepare_weapon, error))
            return false;
        if (move->context.source_usercmd && input->state.kind == QA_MOVEMENT_Q3 &&
            move->q3[index]->owner == move->context.command.provider)
            input->environment.speed_multiplier = 1;
        if (!live(application, record->actor))
            return true;
    }
    character_fixed_pose(input);
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
        qa_q1_game_operation rogue_operation = {0};
        if (!application_control_q1_world_begin(map, &rogue_operation, error)) return false;
        bool rogue_ok = application_native_q1_rogue_after_physics(map, actor, error);
        qa_q1_game_operation_end(&rogue_operation);
        if (!rogue_ok) return false;
        if (!live(move->application, actor)) return true;
        if (map->close_pending || !map->constructed || !map->attached ||
            application_world_provider(move->application, QA_ROLE_ENTITIES, "") != map)
            return application_fail(error, QA_ERROR_ARGUMENT, "Rogue physics retired its actual world source");
        if (!qa_q1_game_map_after_physics(map->state.q1, actor, error))
            return false;
    }
    if (!live(move->application, actor))
        return true;
    const application_control_context *context = application_control_frame_current(move->application, actor);
    if (character != NULL && character->kind == APPLICATION_PROVIDER_Q2 &&
        !(context && context->source_q2cmd && character == source)) {
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
        .command = &input->command, .bounds = input->state.kind == QA_MOVEMENT_Q3
            ? &input->current_bounds : &input->shape.bounds,
        .view_height = &move->control->view_height, .water_level = &move->control->water_level,
        .water_type = &move->control->water_type, .time_ns = input->time_ns,
        .milliseconds = input->command.milliseconds, .elapsed_seconds = (float)((double)input->elapsed_ns / 1e9),
        .source_time_ms = input->command.server_time_ms, .profile = &input->profile,
        .environment = &input->environment};
}

static bool external_stage_current(const application_control_external_stage *stage)
{
    application_move_call *move = stage ? stage->state : NULL;
    if (!move || move->application != stage->application ||
        !qa_actor_id_equal(move->control->actor, stage->actor) ||
        !live(move->application, stage->actor) || !move->control->moving || move->control->retired)
        return false;
    const application_control_context *actual = application_control_frame_current(move->application, stage->actor);
    qa_source_frame active;
    return actual && actual->source_nqcmd && !actual->command_only && actual->retained &&
        actual->stage == APPLICATION_CONTROL_PHYSICS && actual->path == APPLICATION_CONTROL_NQ_TURN &&
        stage->source.source_nqcmd && stage->source.stage == actual->stage &&
        qa_actor_id_equal(actual->actor, stage->actor) &&
        actual->frame.provider == stage->source.frame.provider && actual->frame.kind == QA_CLOCK_NETQUAKE &&
        actual->frame.number == stage->source.frame.number && actual->frame.time_ns == stage->source.frame.time_ns &&
        actual->frame.start_ns == stage->source.frame.start_ns && actual->frame.elapsed_ns == stage->source.frame.elapsed_ns &&
        actual->source_command.sequence == stage->source.source_command.sequence &&
        qa_session_active_frame(move->application->session, actual->frame.provider, &active) &&
        active.number == actual->frame.number && active.time_ns == actual->frame.time_ns &&
        active.start_ns == actual->frame.start_ns && active.elapsed_ns == actual->frame.elapsed_ns;
}
static bool external_stage_input(const application_control_external_stage *stage,
    qa_movement_state *state, qa_movement_command *command, const qa_vec3 *aim,
    application_source_input_scope *scope, bool before, bool slice, uint64_t elapsed, qa_error *error)
{
    if (!external_stage_current(stage) || !state || !command || !scope)
        return application_fail(error, QA_ERROR_ARGUMENT, "External source input lost its retained physics turn");
    /* PREPARE already applied the whole selected command. Only the genuine
     * external PmoveSingle slice opens its own qualified source slice scope. */
    if (!slice) return true;
    application_move_call *move = stage->state;
    if (move->external_nq_physics) return true;
    return qc_input(move, state, command, aim, scope, before, true, elapsed, error) &&
        (!live(move->application, stage->actor) || external_stage_current(stage));
}
static bool external_stage_locomotion(const application_control_external_stage *stage,
    const qa_movement_command *command, qa_movement_command *applied, qa_error *error)
{
    if (!external_stage_current(stage) || !command || !applied)
        return application_fail(error, QA_ERROR_ARGUMENT, "External locomotion lost its retained physics turn");
    application_move_call *move = stage->state;
    if (move->external_nq_physics) {
        /* NQ physics consumes the retained source interval once. The genuine
         * external weapon Pmove slices read that already-published motion. */
        *applied = *command;
        return true;
    }
    qa_movement_input input = *move->input;
    input.state = move->control->state; input.command = *command;
    input.elapsed_ns = (uint64_t)command->milliseconds * UINT64_C(1000000);
    input.current_bounds = input.state.kind == QA_MOVEMENT_Q3
        ? move->input->current_bounds : move->input->shape.bounds;
    input.has_current_bounds = true;
    qa_movement_input *previous = move->input; move->input = &input;
    qa_movement_services services = movement_services(move);
    bool ok = qa_movement_move(&input, &services, &move->control->result, error);
    if (ok && live(move->application, stage->actor)) {
        qa_movement_result *result = &move->control->result;
        ok = publish_result_body(move, &result->state, result->bounds, result->ground,
            result->view_angles, false, false, error);
        if (ok) {
            move->control->state = result->state; move->control->bounds = result->bounds;
            move->control->ground = result->ground; move->control->view_angles = result->view_angles;
            move->control->view_offset = result->view_offset; move->control->view_height = result->view_height;
            move->control->water_level = result->water_level; move->control->water_type = result->water_type;
            *applied = move->command_applied ? move->applied_command : input.command;
        }
    }
    move->input = previous;
    previous->state = move->control->state;
    previous->shape.bounds = previous->state.kind == QA_MOVEMENT_Q3
        ? previous->standing.bounds : move->control->bounds;
    previous->current_bounds = move->control->bounds;
    previous->has_current_bounds = true;
    /* Kernel phase callbacks borrowed the nested state. Restore the retained
     * turn's state before the external client resumes reading its player. */
    application_control_frames_state(move->application, stage->actor,
                                     &previous->state);
    return ok && (!live(move->application, stage->actor) || external_stage_current(stage));
}
static bool external_stage_complete(const application_control_external_stage *stage,
    const qa_movement_command *command, const qa_q3_player *player, qa_error *error)
{
    if (!external_stage_current(stage))
        return application_fail(error, QA_ERROR_ARGUMENT, "Original completion lost its retained physics owner");
    return guest_complete(stage->application, stage->actor, command, player, stage, error);
}

static bool unified_arsenal_input(qa_application *app, qa_actor_id actor,
    const application_control_context *context, qa_movement_command *command, qa_error *error)
{
    if (!context->unified_command) return true;
    application_provider *arsenal = application_provider_for(app, actor, QA_ROLE_ARSENAL, "");
    if (context->unified_intent && (!arsenal || arsenal->owner != context->arsenal))
        return application_fail(error, QA_ERROR_ARGUMENT, "Unified arsenal command changed its selected owner");
    application_provider *source = application_world_provider(app, QA_ROLE_ENTITIES, "");
    if (context->unified_intent && context->unified_has_impulse &&
        !(source && source->kind == APPLICATION_PROVIDER_Q1)) command->impulse = context->unified_impulse;
    if (context->weapon && arsenal &&
        (arsenal->kind == APPLICATION_PROVIDER_Q1 || arsenal->kind == APPLICATION_PROVIDER_Q2 ||
         arsenal->kind == APPLICATION_PROVIDER_Q3)) {
        qa_actor_owner item_owner;
        if (!qa_inventory_item_owner(app->inventory, actor, context->weapon, &item_owner, NULL) ||
            item_owner != arsenal->owner) return false;
        qa_item_id current = 0;
        if (arsenal->kind == APPLICATION_PROVIDER_Q1 &&
            !qa_application_weapon_read(app, actor, &current, error)) return false;
        if (current != context->weapon) {
            qa_q1_game_operation operation = {0};
            if (arsenal->kind == APPLICATION_PROVIDER_Q1 &&
                !application_control_q1_world_begin(arsenal, &operation, error)) return false;
            bool selected = qa_inventory_item_action(app->inventory, actor, context->weapon, QA_ITEM_USE, error);
            qa_q1_game_operation_end(&operation);
            if (!selected) return false;
        }
        if (!live(app, actor)) return true;
        if (application_provider_for(app, actor, QA_ROLE_ARSENAL, "") != arsenal)
            return application_fail(error, QA_ERROR_ARGUMENT, "Unified weapon action retired its selected owner");
    }
    if (arsenal && arsenal->kind == APPLICATION_PROVIDER_Q3) {
        qa_q3_player_state player;
        if (!qa_q3_player_read(arsenal->state.q3, actor, &player))
            return application_fail(error, QA_ERROR_NOT_FOUND, "Unified Q3 arsenal player is absent");
        if (context->unified_intent || command->kind != QA_MOVEMENT_Q3)
            command->weapon = (uint8_t)player.requested_weapon;
    }
    return true;
}

static bool native_q2_weapon_current(application_provider *source, application_provider *arsenal,
    qa_actor_id actor, const qa_movement_command *command, uint64_t time_ns, qa_error *error)
{
    qa_application *app = source ? source->application : NULL;
    struct application_native_q2 *engine = source && source->kind == APPLICATION_PROVIDER_NATIVE
        ? source->state.native.q2_engine : NULL;
    const application_control_context *current = app ? application_control_frame_current(app, actor) : NULL;
    const application_native_q2_input_stage *stage = engine ? engine->input_stage : NULL;
    if (!app || !command || !arsenal || arsenal == source || !live(app, actor) ||
        !source->constructed || !source->attached || source->close_pending ||
        !arsenal->constructed || !arsenal->attached || arsenal->close_pending ||
        !engine || !engine->input_arsenal || engine->input_command != command ||
        !stage || !stage->current || !qa_actor_id_equal(stage->actor, actor) || stage->time_ns != time_ns ||
        !stage->current(stage->context, actor) || !current || !current->source_q2cmd ||
        !current->command_only || !current->retained || !qa_actor_id_equal(current->actor, actor) ||
        current->command.provider != source->owner || current->command.time_ns != time_ns ||
        current->command.kind != source->component.clock.kind ||
        current->source_command.kind != command->kind || current->source_command.sequence != command->sequence ||
        current->source_elapsed_ns != (uint64_t)command->milliseconds * UINT64_C(1000000) ||
        application_world_provider(app, QA_ROLE_ENTITIES, "") != source ||
        application_provider_for(app, actor, QA_ROLE_MOVEMENT, "") != source ||
        application_provider_for(app, actor, QA_ROLE_ARSENAL, "") != arsenal ||
        !application_native_q2_source_client(source, actor) || actor.slot >= app->control_capacity)
        return application_fail(error, QA_ERROR_NOT_FOUND, "Native Q2 weapon decision lost its actual Source and selected arsenal");
    const application_control_record *control = &app->controls[actor.slot];
    if (!control->active || control->retired || control->moving || !qa_actor_id_equal(control->actor, actor))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Native Q2 weapon decision lost its full control generation");
    return true;
}

bool application_control_native_q2_weapon_step(application_provider *source, qa_actor_id actor,
    const qa_movement_command *command, uint64_t source_time_ns, qa_error *error)
{
    qa_application *app = source ? source->application : NULL;
    application_provider *arsenal = app ? application_provider_for(app, actor, QA_ROLE_ARSENAL, "") : NULL;
    if (!native_q2_weapon_current(source, arsenal, actor, command, source_time_ns, error)) return false;
    if (arsenal->kind != APPLICATION_PROVIDER_Q1 && arsenal->kind != APPLICATION_PROVIDER_Q2 &&
        arsenal->kind != APPLICATION_PROVIDER_Q3)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Selected original arsenal has no isolated native Q2 weapon-decision capability");
    qa_q2_wire_movement physical;
    qa_combat_state combat;
    if (!application_native_q2_input_read(source, actor, &physical, error) ||
        !qa_combat_read(app->combat, actor, &combat, error)) return false;
    if (!native_q2_weapon_current(source, arsenal, actor, command, source_time_ns, error)) return false;
    application_control_record *control = &app->controls[actor.slot];
    control->state = physical.state; control->bounds = physical.bounds; control->ground = physical.ground;
    control->view_angles = physical.view_angles; control->view_offset = physical.view_offset;
    control->view_height = physical.view_height; control->water_level = physical.water_level;
    memcpy(&control->water_type, &physical.water_type, sizeof(control->water_type));
    control->command_angles = physical.command_angles;
    control->previous_buttons = control->buttons; control->buttons = command->buttons;
    qa_movement_command applied = *command;
    const application_control_context *context = application_control_frame_current(app, actor);
    if (!unified_arsenal_input(app, actor, context, &applied, error)) return false;
    if (!live(app, actor)) return true;
    if (!native_q2_weapon_current(source, arsenal, actor, command, source_time_ns, error)) return false;
    bool attack = combat.health > 0 && !control->cutscene && (applied.buttons & 1u) &&
        !source->state.native.q2_engine->input_arsenal_committed &&
        (!app->equipment || qa_equipment_primary_selected(app->equipment, actor));
    bool okay;
    if (arsenal->kind == APPLICATION_PROVIDER_Q1) {
        qa_q1_player_view player;
        if (!qa_q1_player_read(arsenal->state.q1, actor, &player))
            return application_fail(error, QA_ERROR_NOT_FOUND, "Selected Q1 weapon decision has no admitted player");
        qa_vec3 point = qa_movement_origin(&physical.state);
        point.z += physical.bounds.mins.z + 1;
        qa_point_query query = {.point = point, .pass_actor = actor,
            .policy = qa_collision_default_policy(QA_COLLISION_Q1)};
        qa_point_contents contents;
        if (!qa_world_point_contents(app->world, &query, &contents, error) ||
            !native_q2_weapon_current(source, arsenal, actor, command, source_time_ns, error)) return false;
        qa_q1_game_operation operation = {0};
        if (!qa_q1_game_command_begin(arsenal->state.q1, source_time_ns,
            (uint64_t)applied.milliseconds * UINT64_C(1000000), &operation, error)) return false;
        qa_q1_input input = {.view_angles = physical.view_angles, .attack = attack,
            .water_level = (uint8_t)(physical.water_level < 0 ? 0 : physical.water_level > 3 ? 3 : physical.water_level),
            .water_type = contents.contents};
        okay = qa_q1_player_input(arsenal->state.q1, actor, &input, error) &&
            (!live(app, actor) || qa_q1_player_postthink(arsenal->state.q1, actor, error));
        if (okay && !qa_q1_game_operation_live(&operation))
            okay = application_fail(error, QA_ERROR_NOT_FOUND, "Selected Q1 weapon decision retired its source operation");
        qa_q1_game_operation_end(&operation);
    } else if (arsenal->kind == APPLICATION_PROVIDER_Q2) {
        qa_q2_weapon_input input;
        okay = application_q2_weapon_input(arsenal, actor, &input, error);
        if (okay && live(app, actor)) {
            if (!native_q2_weapon_current(source, arsenal, actor, command, source_time_ns, error)) return false;
            input.angles = physical.view_angles; input.view_height = physical.view_height;
            input.gravity = physical.state.kind == QA_MOVEMENT_Q2_CLASSIC
                ? (float)physical.state.data.q2.gravity : (float)physical.state.data.q2r.gravity;
            input.attack = attack;
            okay = qa_q2_weapon_early_turn(arsenal->state.q2, actor, &input, error);
        }
    } else {
        qa_q3_player_state player;
        if (!qa_q3_player_read(arsenal->state.q3, actor, &player) || !(player.selections & QA_Q3_ARSENAL))
            return application_fail(error, QA_ERROR_NOT_FOUND, "Selected Q3 weapon decision has no admitted arsenal player");
        qa_q3_controls input = {.attack = attack,
            .use_holdable = context->unified_intent && context->unified_holdable,
            .requested_weapon = player.requested_weapon};
        okay = qa_q3_player_set_view(arsenal->state.q3, actor, physical.view_angles, physical.view_height, error) &&
            (!live(app, actor) || qa_q3_arsenal_step(arsenal->state.q3, actor, &input, (float)applied.milliseconds, error));
    }
    return okay && (!live(app, actor) || native_q2_weapon_current(source, arsenal, actor, command, source_time_ns, error));
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
    if (!application_control_numeric_current(application, actor, &record->numeric, error)) return false;
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
        if (!context->source_usercmd && !context->source_guestcmd && !context->source_qwcmd && !context->source_nqcmd && !context->source_q2cmd) {
            record->command_sequence = command->sequence;
            record->command_seen = true;
        }
        return true;
    }
    qa_movement_command effective_command = *command;
    if (context->stage != APPLICATION_CONTROL_PHYSICS &&
        !unified_arsenal_input(application, actor, context, &effective_command, error)) return false;
    if (!live(application, actor)) return true;
    command = &effective_command;
    application_provider *movement;
    if (!movement_provider(application, actor, &movement, error))
        return false;
    bool preparing = context->stage == APPLICATION_CONTROL_PREPARE;
    bool physics = context->stage == APPLICATION_CONTROL_PHYSICS;
    application_provider *selected_arsenal = application_provider_for(application, actor, QA_ROLE_ARSENAL, "");
    bool original_stage = context->source_nqcmd &&
        ((movement->kind == APPLICATION_PROVIDER_QVM ||
          (movement->kind == APPLICATION_PROVIDER_NATIVE && movement->component.clock.kind == QA_CLOCK_Q3)) ||
         (selected_arsenal && (selected_arsenal->kind == APPLICATION_PROVIDER_QVM ||
          (selected_arsenal->kind == APPLICATION_PROVIDER_NATIVE && selected_arsenal->component.clock.kind == QA_CLOCK_Q3))));
    bool native_q2_stage = context->source_nqcmd && movement->kind == APPLICATION_PROVIDER_NATIVE &&
        movement->state.native.q2_engine;
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
    if (profile.kind == QA_MOVEMENT_NETQUAKE && record->profile.kind == QA_MOVEMENT_NETQUAKE)
        profile.data.nq.no_clip_angle_hack = record->profile.data.nq.no_clip_angle_hack;
    application_provider *execution = control_execution(application, actor);
    application_provider *source = application_world_provider(application, QA_ROLE_ENTITIES, "");
    if (context->source_qwcmd || context->source_nqcmd || context->source_q2cmd || context->source_usercmd || context->source_guestcmd) {
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
        component_input_retarget(&move.qc_command, &input.state, &input.command, false);
        component_input_retarget(&move.qc_slice, &input.state, &input.command, false);
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
    if (ok && !physics && context->weapon && !context->unified_command) {
        move.committed = true;
        ok = application_bot_weapon_apply(application, actor, context->arsenal, context->weapon, error);
    }
    if (ok && !physics && live(application, actor)) ok = prepare_input(&move, command, &input, error);
    if (ok && physics) {
        qa_movement_call call = input_call(&move);
        ok = refresh_source_call(&move, &call, error);
        if (input.state.kind != QA_MOVEMENT_Q3) input.current_bounds = input.shape.bounds;
    }
    if (ok && preparing && (!original_stage || record->state.kind == QA_MOVEMENT_NETQUAKE) && live(application, actor)) {
        qa_movement_call call = input_call(&move);
        ok = move_phase(&move, QA_MOVE_INPUT_BEGIN, &call, error) != QA_MOVEMENT_ERROR;
    }
    if (ok && preparing && live(application, actor)) ok = record_nq_equipment(&move, error);
    bool foreign_nq = context->source_nqcmd && record->state.kind != QA_MOVEMENT_NETQUAKE;
    bool mixed_outer = context->path == APPLICATION_CONTROL_MIXED ||
        (context->source_nqcmd && physics && (foreign_nq || original_stage));
    move.mixed_source_outer = mixed_outer && !(original_stage && !foreign_nq) &&
        execution && execution->kind == APPLICATION_PROVIDER_QC &&
        !execution->state.qc.qualified;
    if (ok && mixed_outer && !(original_stage && !foreign_nq) && live(application, actor)) {
        qa_movement_call call = input_call(&move);
        move.in_source_outer = true;
        ok = move_phase(&move, QA_MOVE_PRETHINK, &call, error) != QA_MOVEMENT_ERROR;
        if (ok && foreign_nq && live(application, actor))
            ok = move_phase(&move, QA_MOVE_THINK, &call, error) != QA_MOVEMENT_ERROR;
        move.in_source_outer = false;
        if (input.state.kind != QA_MOVEMENT_Q3) input.current_bounds = input.shape.bounds;
    }
    if (ok && live(application, actor)) effective_command = input.command;
    qa_movement_services services = movement_services(&move);
    bool external_handled = false;
    if (ok && context->source_nqcmd && physics && original_stage && !foreign_nq && live(application, actor)) {
        move.external_nq_physics = true;
        ok = qa_movement_physics_netquake(&input, &services, &record->result, error);
        if (ok && live(application, actor)) {
            qa_movement_result *result = &record->result;
            ok = publish_result_body(&move, &result->state, result->bounds, result->ground,
                result->view_angles, false, false, error);
            if (ok) {
                record->state = result->state; record->bounds = result->bounds;
                record->ground = result->ground; record->view_angles = result->view_angles;
                record->view_offset = result->view_offset; record->view_height = result->view_height;
                record->water_level = result->water_level; record->water_type = result->water_type;
                input.state = record->state; input.shape.bounds = record->bounds;
                application_control_frames_state(application, actor, &input.state);
            }
        }
    }
    if (ok && context->source_nqcmd && physics && (original_stage || native_q2_stage) && live(application, actor)) {
        application_control_external_stage stage = {.application = application, .actor = actor,
            .source = *context, .state = &move, .current = external_stage_current,
            .input = external_stage_input, .locomotion = external_stage_locomotion,
            .complete = external_stage_complete};
        move.committed = true;
        ok = native_q2_stage ? application_native_q2_stage_move(movement, actor, &input.command,
                &stage, &external_handled, error) :
            application_arsenal_guest_stage_move(application, actor, &input.command, &stage, &external_handled, error);
        if (ok && !external_handled)
            ok = application_fail(error, QA_ERROR_UNSUPPORTED, "Selected external movement has no admitted stage adapter");
        if (ok && live(application, actor)) {
            move.touched_triggers = true;
            input.state = record->result.state; input.shape.bounds = record->result.bounds;
        }
    }
    if (ok && live(application, actor)) {
        if (foreign_nq && preparing) {
            qa_movement_result *result = &record->result;
            *result = (qa_movement_result){.contacts = result->contacts, .contact_capacity = result->contact_capacity,
                .status = QA_MOVEMENT_ACTIVE, .actor = actor, .command_sequence = input.command.sequence,
                .state = input.state, .bounds = input.shape.bounds, .ground = record->ground,
                .view_angles = record->view_angles, .view_offset = record->view_offset,
                .view_height = record->view_height, .water_level = record->water_level,
                .water_type = record->water_type};
        } else if (!external_handled) ok = preparing ? qa_movement_prepare_netquake(&input, &services, &record->result, error)
            : physics && !foreign_nq ? qa_movement_physics_netquake(&input, &services, &record->result, error)
            : qa_movement_move(&input, &services, &record->result, error);
    }
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
    if (ok && !native_q2_stage && !context->source_usercmd && live(application, actor) &&
        (record->state.kind == QA_MOVEMENT_Q2_CLASSIC ||
         record->state.kind == QA_MOVEMENT_Q2_RERELEASE))
        ok = qa_movement_apply_q2_contacts(&input, &services, &record->result,
                                           error);
    if (ok && !native_q2_stage && live(application, actor) &&
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
        record->command_angles = command_angle_feedback(command);
        bool received; uint64_t sequence;
        (void)application_control_frames_sequence(application, actor, &received, &sequence);
        if (!context->source_usercmd && !context->source_guestcmd && !context->source_qwcmd && !context->source_nqcmd && !context->source_q2cmd &&
            (!context->retained || received)) {
            record->command_sequence = command->sequence;
            record->command_seen = true;
        }
    }
    if (ok && !preparing && !application_guest_input_weapon_slice(application, actor) &&
        live(application, actor) && move.arsenal != NULL &&
        move.arsenal->kind == APPLICATION_PROVIDER_Q2) {
        qa_q2_weapon_input controls;
        ok = application_q2_weapon_input(move.arsenal, actor, &controls, error);
        if (ok && live(application, actor)) {
            move.committed = true;
            ok = qa_q2_weapon_early_turn(move.arsenal->state.q2, actor, &controls, error);
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
            record->command_angles = command_angle_feedback(&effective_command);
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
        component_input_retarget(&prepared->move.qc_command, &prepared->input.state, &prepared->input.command, true);
        component_input_retarget(&prepared->move.qc_slice, &prepared->input.state, &prepared->input.command, true);
        application_source_input_scope *scopes[] = {&prepared->move.qc_command, &prepared->move.qc_slice};
        size_t parked_capacity = scopes[0]->count;
        if (scopes[1]->count > SIZE_MAX - parked_capacity) ok = false;
        else parked_capacity += scopes[1]->count;
        if (ok && (parked_capacity > SIZE_MAX / sizeof(*prepared->parked_owners) ||
                   parked_capacity > SIZE_MAX / sizeof(*prepared->parked))) ok = false;
        if (ok && parked_capacity) {
            prepared->parked_owners = calloc(parked_capacity, sizeof(*prepared->parked_owners));
            prepared->parked = calloc(parked_capacity, sizeof(*prepared->parked));
            ok = prepared->parked_owners && prepared->parked;
        }
        if (!ok) application_fail(error, QA_ERROR_MEMORY, "Allocating parked Source input inventory");
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
    if (!app || !source || !fields || (fields & ~(uint32_t)QA_Q3_SOURCE_PM_ALL) ||
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
    free(turn->parked_owners); free(turn->parked);
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
    free(turn->parked_owners); turn->parked_owners = NULL;
    free(turn->parked); turn->parked = NULL;
    turn->parked_count = 0;
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
    input.command.sequence = record->command_sequence;
    input.command.angles = record->command_angles; input.time_ns = application_control_time(context);
    input.elapsed_ns = application_control_elapsed(context);
    application_move_call move = {.application = app, .control = record, .input = &input,
        .execution = execution, .context = *context,
        .arsenal = application_provider_for(app, actor, QA_ROLE_ARSENAL, "")};
    qa_movement_call call = input_call(&move);
    bool ok = move_phase(&move, QA_MOVE_POSTTHINK, &call, error) != QA_MOVEMENT_ERROR;
    if (!ok || !live(app, actor)) goto finished;
    qa_q1_input source_input = q1_input(&call);
    source_input.view_angles = call_view_angles(&move, &call);
    if (record->state.kind == QA_MOVEMENT_QUAKEWORLD && record->state.data.qw.spectator &&
        execution && execution->kind == APPLICATION_PROVIDER_Q1 &&
        execution->component.clock.kind == QA_CLOCK_QUAKEWORLD) {
        if (context->source_qwcmd) source_input.impulse = context->source_command.impulse;
        record->state = input.state;
        record->bounds = input.shape.bounds;
        ok = application_native_q1_spectator_postthink(execution, actor, &source_input, error);
        goto finished;
    }
    ok = q1_source_weapon_impulse(app, actor, &input.command, &source_input, error);
    if (!ok || !live(app, actor)) goto finished;
    if (move.arsenal && move.arsenal->kind == APPLICATION_PROVIDER_Q1) {
        qa_q1_player_view view;
        if (qa_q1_player_read(move.arsenal->state.q1, actor, &view) &&
            !qa_q1_player_postthink(move.arsenal->state.q1, actor, error)) {
            ok = false;
            goto finished;
        }
    }
    if (live(app, actor)) { record->state = input.state; record->bounds = input.shape.bounds; }
finished:
    application_control_frames_state(app, actor, NULL);
    return ok;
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

static bool guest_complete(qa_application *application, qa_actor_id actor,
                                         const qa_movement_command *command,
                                         const qa_q3_player *player,
                                         const application_control_external_stage *stage, qa_error *error)
{
    const application_control_context *context = application_control_frame_current(application, actor);
    if (!application || !command || !player || !context || actor.slot >= application->control_capacity)
        return application_fail(error, QA_ERROR_ARGUMENT, "Guest movement completion is missing");
    if (!live(application, actor)) return true;
    application_control_record *record = &application->controls[actor.slot];
    if (!record->active || !qa_actor_id_equal(record->actor, actor) ||
        (stage ? !record->moving || !external_stage_current(stage) : record->moving) ||
        record->state.kind != QA_MOVEMENT_Q3)
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
    bool ok = (stage || begin_q1_operations(&move, error)) &&
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
        if (!stage) {
            record->previous_buttons = record->buttons; record->buttons = command->buttons;
            record->command_angles = command_angle_feedback(command);
        }
        if (!stage && !context->source_guestcmd) {
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
        if (stage) return external_stage_current(stage);
        bool source_weapons = application_guest_input_source_weapons(application, actor);
        if (!source_weapons && command->milliseconds) {
            qa_q1_input source_input = {.view_angles = record->view_angles,
                .attack = (input.command.buttons & 1u) != 0, .jump = command_jump(&input.command),
                .use = (input.command.buttons & 4u) != 0, .impulse = input.command.impulse,
                .water_level = (uint8_t)(record->water_level < 0 ? 0 : record->water_level > 3 ? 3 : record->water_level),
                .water_type = record->water_type};
            ok = q1_source_weapon_impulse(application, actor, &input.command, &source_input, error);
        }
        if (!ok || !live(application, actor)) goto guest_weapons_finished;
        if (!source_weapons && move.arsenal && move.arsenal->kind == APPLICATION_PROVIDER_Q2) {
            qa_q2_weapon_input controls;
            ok = application_q2_weapon_input(move.arsenal, actor, &controls, error);
            if (ok && live(application, actor)) {
                ok = qa_q2_weapon_early_turn(move.arsenal->state.q2, actor, &controls, error);
            }
        } else if (!source_weapons && command->milliseconds && move.arsenal && move.arsenal->kind == APPLICATION_PROVIDER_Q1) {
            qa_q1_input controls = {.view_angles = record->view_angles,
                .attack = (command->buttons & 1u) != 0, .jump = command_jump(command),
                .use = (command->buttons & 4u) != 0, .impulse = input.command.impulse,
                .water_level = (uint8_t)record->water_level, .water_type = record->water_type};
            ok = qa_q1_player_input(move.arsenal->state.q1, actor, &controls, error) &&
                (!live(application, actor) || qa_q1_player_postthink(move.arsenal->state.q1, actor, error));
        } else if (!source_weapons && command->milliseconds && move.arsenal && move.arsenal->kind == APPLICATION_PROVIDER_Q3) {
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
guest_weapons_finished:
        if (ok && live(application, actor)) ok = finish_native_players(&move, &record->result, error);
    }
    if (stage) return ok && (!live(application, actor) || external_stage_current(stage));
    if (record->retired && qa_actor_id_equal(record->actor, actor)) {
        qa_movement_result_free(&record->result); *record = (application_control_record){0};
    } else if (record->active && qa_actor_id_equal(record->actor, actor)) record->moving = false;
    if (!ok) application_fault(application, error);
    end_q1_operations(&move);
    return ok;
}

bool application_control_guest_complete(qa_application *application, qa_actor_id actor,
    const qa_movement_command *command, const qa_q3_player *player, qa_error *error)
{ return guest_complete(application, actor, command, player, NULL, error); }

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

bool application_control_water_read(const qa_application *app, qa_actor_id actor,
    int32_t *water_type, int32_t *water_level, qa_error *error)
{
    if (!app || !water_type || !water_level || !live(app, actor) || actor.slot >= app->control_capacity)
        return application_fail(error, QA_ERROR_ARGUMENT, "Source water needs its live full actor");
    const application_control_record *record = &app->controls[actor.slot];
    if (!record->active || record->retired || !qa_actor_id_equal(record->actor, actor))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Source water lost its admitted movement owner");
    const qa_movement_call *call = application_control_frames_call_current(app, actor);
    int32_t type = record->water_type, level = record->water_level;
    if (call) {
        if (!call->state || !call->water_type || !call->water_level || call->state->kind != record->state.kind)
            return application_fail(error, QA_ERROR_ARGUMENT, "Source water has no genuine current movement fields");
        type = *call->water_type; level = *call->water_level;
    } else if (record->moving || application_control_frames_state_current(app, actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Source water requires its actual active movement call");
    *water_type = type; *water_level = level;
    return true;
}

bool qa_application_q3_input_values_read(qa_application *app,uint32_t seat,qa_actor_id actor,
    uint8_t *weapon,float *sensitivity,bool *present,qa_error *error)
{
    qa_actor_id local;
    if (!app || !weapon || !sensitivity || !present || app->destroy_requested ||
        app->state!=QA_APPLICATION_RUNNING || app->operation!=APPLICATION_IDLE ||
        !qa_application_player_actor(app,seat,&local) || !qa_actor_id_equal(local,actor))
        return application_fail(error,QA_ERROR_ARGUMENT,"Q3 command selection needs its actual returned local actor");
    application_provider *primary=application_world_provider(app,QA_ROLE_ENTITIES,"");
    if (!primary || !primary->constructed || !primary->attached || primary->close_pending)
        return application_fail(error,QA_ERROR_ARGUMENT,"Q3 command selection lost its actual primary GAME");
    uint8_t selected=0; float scale=1; bool found=false;
    if (primary->kind==APPLICATION_PROVIDER_Q3) {
        if (!application_native_q3_input_values_read(primary,seat,actor,&selected,&scale,error)) return false;
        found=true;
    } else if (q3g_engine(primary)) {
        if (!application_q3_guest_input_values_read(primary,seat,actor,&selected,&scale,error)) return false;
        found=true;
    }
    application_provider *arsenal=application_provider_for(app,actor,QA_ROLE_ARSENAL,"");
    if (arsenal && arsenal!=primary && arsenal->kind==APPLICATION_PROVIDER_Q3) {
        qa_q3_player_state player;
        if (!arsenal->constructed || !arsenal->attached || arsenal->close_pending ||
            !qa_q3_player_read(arsenal->state.q3,actor,&player) || !(player.selections&QA_Q3_ARSENAL))
            return application_fail(error,QA_ERROR_ARGUMENT,"Q3 command selection lost its actual selected arsenal");
        selected=(uint8_t)player.requested_weapon; found=true;
    }
    if (found) { *weapon=selected; *sensitivity=scale; }
    *present=found;
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

bool qa_application_control_prediction_read(qa_application *application,
    qa_actor_id actor, qa_application_control_prediction_configuration *out,
    qa_error *error)
{
    if (!application || !out || !live(application, actor) ||
        actor.slot >= application->control_capacity || !application->world ||
        !application->combat || !application->physics ||
        application->destroy_requested)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "Prediction configuration needs its actual live player");
    const application_control_record *record = &application->controls[actor.slot];
    application_provider *movement = NULL;
    application_provider *character = application_provider_for(application, actor, QA_ROLE_CHARACTER, "");
    application_provider *arsenal = application_provider_for(application, actor, QA_ROLE_ARSENAL, "");
    if (!record->active || record->retired || record->moving ||
        !qa_actor_id_equal(record->actor, actor) ||
        !movement_provider(application, actor, &movement, error))
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "Prediction configuration lost its selected control owner");
    if (movement->close_pending || !character || !arsenal ||
        !character->constructed || !character->attached || character->close_pending ||
        !arsenal->constructed || !arsenal->attached || arsenal->close_pending ||
        movement_kind(movement->component.clock.kind) != record->state.kind)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "Prediction configuration lost an admitted selected role");
    if (!application_control_numeric_current(application, actor, &record->numeric, error) ||
        !application_control_prediction_numeric_current(application, actor, &record->prediction_numeric, error)) return false;
    qa_body_state body;
    qa_combat_state combat;
    if (!qa_world_body_read(application->world, actor, &body, error) ||
        !qa_combat_read_traits(application->combat, actor, &combat, error)) return false;
    qa_application_control_prediction_configuration result = {
        .movement = movement->owner, .character = character->owner, .arsenal = arsenal->owner,
        .profile_id = movement->owner, .numeric = record->numeric, .prediction_numeric = record->prediction_numeric,
        .input = qa_movement_input_default(record->state.kind, actor),
        .q2r_pml_origin = record->q2r_pml_origin,
        .view_angles = record->view_angles, .command_angles = record->command_angles,
        .ground = record->ground, .view_height = record->view_height,
        .water_level = record->water_level, .water_type = record->water_type,
        .q3_character = character->component.clock.kind == QA_CLOCK_Q3,
        .q3_arsenal = arsenal->component.clock.kind == QA_CLOCK_Q3,
        .native_q3_character = character->kind == APPLICATION_PROVIDER_Q3,
        .native_q3_arsenal = arsenal->kind == APPLICATION_PROVIDER_Q3,
        .requested_weapon = -1,
    };
    uint64_t registration_order;
    if (!qa_session_component_recipe(application->session, movement->owner, &result.clock, &registration_order))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Prediction lost its admitted movement clock recipe");
    if (result.native_q3_arsenal) {
        qa_q3_player_state player;
        if (!qa_q3_player_read(arsenal->state.q3, actor, &player) ||
            !(player.selections & QA_Q3_ARSENAL))
            return application_fail(error, QA_ERROR_ARGUMENT,
                                    "Prediction lost its genuine native Q3 arsenal record");
        result.fractional_weapon_ms = player.fractional_weapon_ms;
        result.external_weapon_slot = player.external_slot;
        result.requested_weapon = player.requested_weapon == player.weapon ? -1 : (int32_t)player.requested_weapon;
    }
    result.input.state = record->state;
    result.input.profile = record->profile;
    /* Copied-state prediction has no QC PlayerPreThink invocation to consume
     * jump. Its admitted C kernel owns that action independently of Source. */
    if (record->state.kind == QA_MOVEMENT_NETQUAKE)
        result.input.profile.data.nq.source_jump_authority = false;
    application_provider *physical = application_world_provider(application, QA_ROLE_ENTITIES, "");
    if (record->state.kind == QA_MOVEMENT_Q3 && physical && physical->kind == APPLICATION_PROVIDER_Q3) {
        uint32_t source_slot;
        if (qa_q3_native_client_slot(physical->state.q3, actor, &source_slot, NULL)) {
            qa_q3_native_client source_client;
            qa_q3_client_session source_session;
            int32_t fixed, step, dmflags;
            if (!qa_q3_client_read(physical->state.q3, actor, &source_client, error) ||
                !qa_q3_client_session_read(physical->state.q3, actor, &source_session, error) ||
                !application_native_q3_settings_integer(physical, "pmove_fixed", &fixed, error) ||
                !application_native_q3_settings_integer(physical, "pmove_msec", &step, error) ||
                !application_native_q3_settings_integer(physical, "dmflags", &dmflags, error)) return false;
            bool spectator = source_session.team == 3;
            if (!spectator && (fixed || source_client.pmove_fixed) && step <= 0)
                return application_fail(error, QA_ERROR_ARGUMENT, "Prediction fixed Pmove lost its genuine cached interval");
            result.input.profile.data.q3.fixed_ms = !spectator && (fixed || source_client.pmove_fixed) ? (uint32_t)step : 0;
            result.input.profile.data.q3.no_footsteps = !spectator && ((uint32_t)dmflags & 32u);
        }
    }
    result.input.shape.bounds = result.input.current_bounds = body.bounds;
    result.input.has_current_bounds = true;
    result.input.view_offset = record->view_offset;
    result.input.environment.health = combat.health;
    result.input.environment.invulnerable = combat.invulnerable;
    result.input.environment.flight = record->flight;
    result.input.environment.gravity_multiplier = record->gravity_multiplier;
    result.input.environment.has_mode = record->player_mode_set;
    result.input.environment.mode = record->player_mode;
    result.input.prediction = true;
    if (physical && physical->kind == APPLICATION_PROVIDER_QVM) {
        struct application_q3_guest *engine = q3g_engine(physical);
        if (engine && engine->game && engine->game->weapons) {
            int32_t flight, haste;
            if (!application_q3_weapons_powerup_until(engine->game->weapons, actor,
                    APPLICATION_Q3_FLIGHT, &flight, error) ||
                !application_q3_weapons_powerup_until(engine->game->weapons, actor,
                    APPLICATION_Q3_HASTE, &haste, error)) return false;
            if (!live(application, actor) ||
                application_world_provider(application, QA_ROLE_ENTITIES, "") != physical ||
                physical->close_pending || !physical->constructed || !physical->attached)
                return application_fail(error, QA_ERROR_NOT_FOUND,
                    "Prediction Source powers retired their actual original GAME owner");
            result.input.environment.flight = flight != 0;
            result.input.environment.haste = haste != 0;
        }
    }
    application_provider *effects = application_provider_for(application, actor, QA_ROLE_EFFECTS, "");
    application_provider *q3[] = {movement, character, arsenal, effects};
    for (size_t i = 0; i < sizeof(q3) / sizeof(q3[0]); ++i) {
        if (!q3_present(q3[i], actor)) continue;
        bool duplicate = false;
        for (size_t j = 0; j < i; ++j) duplicate |= q3[j] == q3[i];
        if (!duplicate && !qa_q3_movement_environment(q3[i]->state.q3, actor, &result.input.environment, error)) return false;
    }
    application_client_outputs outputs;
    if (!application_control_outputs(application, actor, &outputs, error)) return false;
    result.input.environment.has_body_bounds = outputs.has_body_bounds;
    if (outputs.has_body_bounds) result.input.environment.body_bounds = outputs.body_bounds;
    if (outputs.has_mode) { result.input.environment.has_mode = true; result.input.environment.mode = outputs.mode; }
    result.input.environment.has_stance = outputs.has_stance;
    result.input.environment.crouched = outputs.crouched;
    result.has_client_view_offset = outputs.has_view_offset;
    if (outputs.has_view_offset) result.client_view_offset = outputs.view_offset;
    /* The selected character owns standing dimensions and view height. The
     * actual current body bounds remain a separate snapshot field. */
    qa_movement_input postures = qa_movement_input_default(
        result.q3_character ? QA_MOVEMENT_Q3 : QA_MOVEMENT_NETQUAKE, actor);
    result.input.standing = postures.standing;
    result.input.crouched = postures.crouched;
    result.input.dead = postures.dead;
    result.input.invulnerability_bounds = postures.invulnerability_bounds;
    character_fixed_pose(&result.input);
    if (record->state.kind == QA_MOVEMENT_Q3)
        result.input.shape.bounds = result.input.standing.bounds;
    if (record->state.kind == QA_MOVEMENT_NETQUAKE)
        result.input.profile.data.nq.parameters.gravity = application->physics->gravity;
    else if (record->state.kind == QA_MOVEMENT_QUAKEWORLD)
        result.input.profile.data.qw.parameters.gravity = application->physics->gravity;
    *out = result;
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
            out->has_client_view_offset = true;
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

static bool player_mode(qa_application *application, qa_actor_id actor,
    application_control_record *record, qa_movement_mode mode, bool spectator, qa_error *error)
{
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
    if (mode != QA_MOVEMENT_MODE_NORMAL) record->flight = false;
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
    return player_mode(application, actor, record, mode, spectator, error);
}

bool application_control_spawn_reset(qa_application *application, qa_actor_id actor,
    bool spectator, qa_error *error)
{
    qa_body_state body;
    application_control_record *record;
    if (!qa_world_body_read(application->world, actor, &body, error) ||
        !application_control_ensure(application, actor, body.angles, &record, error)) return false;
    record->flight = record->cutscene = false;
    record->cutscene_character = NULL;
    record->saved_mode_valid = false;
    application_control_body_reset(application, actor);
    return player_mode(application, actor, record, QA_MOVEMENT_MODE_NORMAL, spectator, error);
}

bool application_control_death(qa_application *application, qa_actor_id actor, qa_error *error)
{
    if (actor.slot >= application->control_capacity ||
        !qa_actors_get(qa_session_actors(application->session), actor)) return true;
    application_control_record *record = application->controls + actor.slot;
    if (!record->active || record->retired || !qa_actor_id_equal(record->actor, actor)) return true;
    qa_combat_state combat;
    if (!qa_combat_read(application->combat, actor, &combat, error)) return false;
    if (combat.health <= 0) record->flight = false;
    return true;
}

bool application_control_toggle_motion(qa_application *application, qa_actor_id actor,
    qa_physics_motion motion, bool spectator, bool *enabled, qa_error *error)
{
    if (!application || !enabled ||
        (motion != QA_PHYSICS_NOCLIP && motion != QA_PHYSICS_FLY))
        return application_fail(error, QA_ERROR_ARGUMENT, "Motion cheat requires its selected mode and output");
    qa_body_state body;
    application_control_record *record;
    if (!qa_world_body_read(application->world, actor, &body, error) ||
        !application_control_ensure(application, actor, body.angles, &record, error))
        return false;
    bool flying = motion == QA_PHYSICS_FLY;
    if (flying && record->state.kind == QA_MOVEMENT_QUAKEWORLD)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "QuakeWorld movement has no fly mode");
    bool noclip = record->player_mode_set && record->player_mode == QA_MOVEMENT_MODE_NOCLIP;
    if (!record->player_mode_set && record->state.kind == QA_MOVEMENT_NETQUAKE)
        noclip = record->state.data.nq.move_type == 8;
    bool flight = record->flight;
    if (!record->player_mode_set && record->state.kind == QA_MOVEMENT_NETQUAKE)
        flight = flight || record->state.data.nq.move_type == 5;
    *enabled = flying ? !(flight && !noclip) : !noclip;
    record->flight = flying && *enabled;
    if (!flying && record->state.kind == QA_MOVEMENT_NETQUAKE)
        record->profile.data.nq.no_clip_angle_hack = *enabled;
    return player_mode(application, actor, record,
        !flying && *enabled ? QA_MOVEMENT_MODE_NOCLIP : QA_MOVEMENT_MODE_NORMAL, spectator, error);
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

static void force_state_view(qa_movement_state *state, qa_vec3 view,
                             qa_vec3 command_view, qa_vec3 command)
{
    qa_vec3 delta = qa_vec_sub(command_view, command);
    float angles[3] = {delta.x, delta.y, delta.z};
    switch (state->kind) {
    case QA_MOVEMENT_NETQUAKE:
        state->data.nq.view_angles = view;
        break;
    case QA_MOVEMENT_QUAKEWORLD:
        state->data.qw.angles = view;
        break;
    case QA_MOVEMENT_Q2_CLASSIC:
        for (size_t i = 0; i < 3; ++i) {
            uint32_t bits = (uint32_t)(int32_t)
                ((fmodf(angles[i], 360.0f) * 65536.0f) / 360.0f) & UINT32_C(65535);
            state->data.q2.delta_angle_shorts[i] =
                (int16_t)(bits < UINT32_C(32768) ? (int32_t)bits : (int32_t)bits - 65536);
        }
        break;
    case QA_MOVEMENT_Q2_RERELEASE:
        state->data.q2r.delta_angles = delta;
        break;
    case QA_MOVEMENT_Q3: {
        float target[3] = {command_view.x, command_view.y, command_view.z};
        float base[3] = {command.x, command.y, command.z};
        for (size_t i = 0; i < 3; ++i)
            state->data.q3.delta_angle_words[i] = input_angle_word(target[i]) - input_angle_word(base[i]);
        state->data.q3.view_angles = view;
        break;
    }
    }
}

bool application_control_motion_changed(
    qa_application *application, qa_actor_id actor,
    const qa_builtin_motion_change *change, qa_error *error)
{
    if (application == NULL || change == NULL ||
        !qa_vec_finite(change->body.origin) ||
        !qa_vec_finite(change->body.velocity) ||
        !qa_vec_finite(change->view_angles) ||
        (change->has_command_view_angles && !qa_vec_finite(change->command_view_angles)))
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
        qa_movement_state *active = application_control_frames_state_current(application, actor);
        qa_vec3 command_view = change->has_command_view_angles ? change->command_view_angles : change->view_angles;
        qa_vec3 command = change->preserve_command_angles ? record->command_angles : change->view_angles;
        if (!qa_vec_finite(qa_vec_sub(command_view, command)) ||
            (record->moving && !active) || (active && active->kind != record->state.kind))
            return application_fail(error, QA_ERROR_ARGUMENT,
                                    "Forced view lost its actual movement continuation");
        force_state_view(&record->state, change->view_angles, command_view, command);
        if (active && active != &record->state)
            force_state_view(active, change->view_angles, command_view, command);
        record->view_angles = change->view_angles;
        if (!change->preserve_command_angles)
            record->command_angles = change->view_angles;
    }
    return true;
}

bool application_control_gravity(qa_application *application, qa_actor_id actor,
                                 float scale, qa_error *error)
{
    if (!live(application, actor) || !isfinite(scale) || scale < 0 ||
        actor.slot >= application->control_capacity || !application->physics ||
        !isfinite(application->physics->gravity) || application->destroy_requested ||
        application->finalizing)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "Source gravity needs its admitted live control and physics owner");
    application_control_record *record = &application->controls[actor.slot];
    application_provider *movement = NULL;
    if (!record->active || record->retired || record->application != application ||
        !qa_actor_id_equal(record->actor, actor) ||
        !movement_provider(application, actor, &movement, error))
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "Source gravity lost its actual selected control owner");
    if (movement->close_pending ||
        movement_kind(movement->component.clock.kind) != record->state.kind)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "Source gravity differs from its admitted movement family");
    qa_movement_state *active = application_control_frames_state_current(application, actor);
    qa_movement_state state = active ? *active : record->state;
    if ((record->moving && !active) || state.kind != record->state.kind)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "Source gravity lost its active movement continuation");
    double gravity = trunc((double)application->physics->gravity * (double)scale);
    switch (state.kind) {
    case QA_MOVEMENT_Q2_CLASSIC:
    case QA_MOVEMENT_Q2_RERELEASE:
        if (!isfinite(gravity) || gravity < INT16_MIN || gravity > INT16_MAX)
            return application_fail(error, QA_ERROR_UNSUPPORTED,
                                    "Source gravity exceeds the genuine Q2 movement field");
        if (state.kind == QA_MOVEMENT_Q2_CLASSIC) state.data.q2.gravity = (int16_t)gravity;
        else state.data.q2r.gravity = (int16_t)gravity;
        break;
    case QA_MOVEMENT_Q3:
        if (!isfinite(gravity) || gravity < INT32_MIN || gravity > INT32_MAX)
            return application_fail(error, QA_ERROR_UNSUPPORTED,
                                    "Source gravity exceeds the genuine Q3 movement field");
        state.data.q3.gravity = (int32_t)gravity;
        break;
    case QA_MOVEMENT_NETQUAKE:
    case QA_MOVEMENT_QUAKEWORLD:
        break;
    }
    record->gravity_multiplier = scale;
    record->state = state;
    if (active) *active = state;
    return true;
}

bool application_control_source_spawn(qa_application *application,
                                      qa_actor_id actor, qa_vec3 view_angles,
                                      qa_error *error)
{
    if (!live(application, actor) || !qa_vec_finite(view_angles) ||
        actor.slot >= application->control_capacity || !application->world ||
        !application->combat || application->destroy_requested || application->finalizing)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "Source spawn needs its admitted live control");
    application_control_record *record = &application->controls[actor.slot];
    application_provider *source = application_world_provider(application, QA_ROLE_ENTITIES, "");
    application_provider *character = application_provider_for(application, actor, QA_ROLE_CHARACTER, "");
    application_provider *map_source = source;
    if (map_source && map_source->kind == APPLICATION_PROVIDER_QC && character &&
        character->kind == APPLICATION_PROVIDER_Q1) source = character;
    uint32_t slot;
    uint64_t source_time;
    double source_elapsed;
    if (!record->active || record->retired || !qa_actor_id_equal(record->actor, actor) ||
        !source || source->kind != APPLICATION_PROVIDER_Q1 || !source->constructed ||
        !source->attached || source->close_pending || !source->state.q1 ||
        !character || !character->constructed || !character->attached || character->close_pending)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "Source spawn lost its actual client or character owner");
    if (!qa_q1_native_client_slot(source->state.q1, actor, &slot, error)) return false;
    if (source != map_source) {
        const application_player_record *player = NULL;
        if (!application->players || application->players->map_provider != map_source ||
            !map_source->constructed || !map_source->attached || map_source->close_pending)
            return application_fail(error, QA_ERROR_ARGUMENT,
                "Selected Q1 Source spawn lost its actual QC map owner");
        for (size_t i = 0; i < application->players->count; ++i) {
            const application_player_record *row = application->players->records + i;
            if (!row->retiring && !row->source_begin_pending && row->character == source &&
                qa_actor_id_equal(row->actor, actor) && row->client_slot == slot) { player = row; break; }
        }
        if (!player) return application_fail(error, QA_ERROR_ARGUMENT,
            "Selected Q1 Source spawn differs from its actual physical roster");
    }
    if (!qa_q1_game_clock_read(source->state.q1, &source_time, &source_elapsed))
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "Source spawn has no admitted Q1 client clock");
    qa_body_state body;
    qa_combat_state combat;
    if (!qa_world_body_read(application->world, actor, &body, error) ||
        !qa_combat_read_traits(application->combat, actor, &combat, error)) return false;
    qa_movement_input postures = qa_movement_input_default(
        character->component.clock.kind == QA_CLOCK_Q3 ? QA_MOVEMENT_Q3 : QA_MOVEMENT_NETQUAKE,
        actor);
    if (!same_bounds(body.bounds, postures.standing.bounds) || body.ground.registry ||
        !same_vector(body.angles, view_angles))
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "Source spawn must follow its committed standing body");
    qa_movement_state *active = application_control_frames_state_current(application, actor);
    qa_movement_state state = active ? *active : record->state;
    if ((record->moving && !active) || state.kind != record->state.kind)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "Source spawn lost its active movement continuation");
    if (!qa_movement_set_origin(&state, body.origin, error) ||
        !qa_movement_set_velocity(&state, body.velocity, error)) return false;
    switch (state.kind) {
    case QA_MOVEMENT_NETQUAKE:
        state.data.nq.old_origin = body.origin;
        state.data.nq.angles = state.data.nq.view_angles = view_angles;
        state.data.nq.flags &= ~(uint32_t)APPLICATION_Q1_ONGROUND;
        state.data.nq.move_type = 3;
        state.data.nq.ground = (qa_movement_ground){0};
        state.data.nq.fix_angle = false;
        state.data.nq.teleport_time_seconds = (double)source_time / 1000000000.0;
        state.data.nq.health = combat.health;
        break;
    case QA_MOVEMENT_QUAKEWORLD:
        state.data.qw.angles = view_angles;
        state.data.qw.ground = (qa_movement_ground){0};
        break;
    case QA_MOVEMENT_Q2_CLASSIC:
        state.data.q2.flags = 0;
        state.data.q2.time_eight_ms = 0;
        memset(state.data.q2.delta_angle_shorts, 0, sizeof(state.data.q2.delta_angle_shorts));
        state.data.q2.type = 0;
        break;
    case QA_MOVEMENT_Q2_RERELEASE:
        state.data.q2r.flags = 0;
        state.data.q2r.time_ms = 0;
        state.data.q2r.delta_angles = qa_v3(0, 0, 0);
        state.data.q2r.type = 0;
        break;
    case QA_MOVEMENT_Q3:
        state.data.q3.view_angles = view_angles;
        memset(state.data.q3.delta_angle_words, 0, sizeof(state.data.q3.delta_angle_words));
        state.data.q3.movement_type = 0;
        state.data.q3.ground = (qa_movement_ground){0};
        state.data.q3.movement_flags &= ~(UINT32_C(32) | UINT32_C(64) | UINT32_C(256));
        state.data.q3.movement_time_ms = 0;
        break;
    }
    record->state = state;
    if (active) *active = state;
    record->ground = (qa_movement_ground){0};
    record->standing_bounds = record->bounds = postures.standing.bounds;
    record->view_angles = view_angles;
    record->view_height = postures.standing.view_height;
    record->view_offset = qa_v3(0, 0, record->view_height);
    return application_control_spawn_reset(application, actor, false, error);
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
    if (record->application == application && !record->retired &&
        qa_actors_get(qa_session_actors(application->session), actor)) {
        application_provider *source = application_world_provider(record->application, QA_ROLE_ENTITIES, "");
        application_provider *character = application_provider_for(record->application, actor, QA_ROLE_CHARACTER, "");
        qa_q1_source_client_view client;
        qa_q1_character_view pose;
        if (source && source->kind == APPLICATION_PROVIDER_Q1 && source->constructed &&
            source->attached && !source->close_pending && source->component.clock.kind == QA_CLOCK_QUAKEWORLD &&
            character && character->kind == APPLICATION_PROVIDER_Q1 && character->constructed &&
            character->attached && !character->close_pending &&
            qa_q1_source_client_read(source->state.q1, actor, &client) && !client.observer &&
            qa_q1_character_read(character->state.q1, actor, &pose) &&
            pose.life == QA_Q1_DEAD && pose.frame == 60 && pose.next_frame_seconds == -1) {
            const char *model = qa_strings_cstr(qa_session_strings(application->session), pose.model);
            if (model && !strcmp(model, "progs/player.mdl")) {
                out->motion = pose.motion;
                out->solid = pose.solid;
            }
        }
    }
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
