#include "remote_q2_private.h"
#include <math.h>
#include <stdlib.h>
#include <float.h>

static qa_vec3 vector(const float *value) { return qa_v3(value[0], value[1], value[2]); }
static const qa_q2_player *player(const frontend_remote_q2 *row)
{
    uint32_t seat; qa_error error = {0};
    return row->frame.valid && frontend_remote_q2_wire_seat(row, &seat, &error) && seat < row->frame.player_count ?
        &row->frame.players[seat].player : NULL;
}
void remote_q2_prediction_config(frontend_remote_q2 *row, uint16_t index)
{
    if (index == row->layout.max_clients) {
        row->collision_clients = strtoul(frontend_remote_q2_config(row, index), NULL, 10);
        row->collision_dirty = true;
    }
    if (!row->collision_models || index < row->layout.models ||
        (size_t)(index - row->layout.models) >= row->layout.max_models) return;
    qa_entity_model_field *entry = row->collision_models + index - row->layout.models;
    *entry = (qa_entity_model_field){0};
    const char *path = frontend_remote_q2_config(row, index);
    if (*path == '*') {
        char *end; unsigned long model = strtoul(path + 1, &end, 10);
        if (end != path + 1 && !*end && model && model < qa_collision_model_count(row->geometry))
            *entry = (qa_entity_model_field){.model = (uint32_t)model, .present = true};
    }
    row->collision_dirty = true;
}
qa_bounds remote_q2_solid_bounds(const frontend_remote_q2 *row, uint32_t solid)
{
    qa_net_protocol_id protocol = row->options.domain.protocol;
    bool short_solid = protocol.kind == QA_NET_Q2_34 ||
        (protocol.kind == QA_NET_R1Q2_35 && row->data.protocol_revision < 1905);
    bool v2 = remote_q2_float_movement(row) || protocol.kind == QA_NET_Q2PRIVATE_4038 ||
        (protocol.kind == QA_NET_Q2PRO_36 &&
            ((row->data.protocol_revision >= 1024 && (row->data.wire_flags & 8)) ||
             (row->data.protocol_revision >= 1025 && (row->data.wire_flags & 16))));
    float x = short_solid ? (float)(solid & 31) * 8 : (float)(solid & 255);
    float y = v2 ? (float)((solid >> 8) & 255) : x;
    float down = short_solid ? (float)((solid >> 5) & 31) * 8 :
        v2 ? (float)((solid >> 16) & 255) : (float)((solid >> 8) & 255);
    float up = short_solid ? (float)((solid >> 10) & 63) * 8 - 32 :
        v2 ? (float)((solid >> 24) & 255) - 32 : (float)(solid >> 16) - 32768;
    return (qa_bounds){qa_v3(-x, -y, -down), qa_v3(x, y, up)};
}
bool remote_q2_prediction_publish(frontend_remote_q2 *row, qa_error *error)
{
    if (!row->collision_world) return true;
    qa_actor_registry *actors = qa_world_actors(row->collision_world);
    for (size_t i = 0; i < row->collision_count; ++i)
        if (qa_actors_get(actors, row->collision_actors[i]) &&
            !qa_world_unlink(row->collision_world, row->collision_actors[i], error)) return false;
    row->collision_count = 0; row->collision_viewer = (qa_actor_id){0};
    qa_world_query_rules rules = {.actors = row->collision_actors,
        .brush_contents_only = true, .contents_ignore_pass = true};
    qa_world_set_query_rules(row->collision_world, &rules);
    if (!row->frame.valid || !row->options.entity_actor) return true;
    uint32_t self_index; int32_t self_number;
    if (!frontend_remote_q2_wire_seat(row, &self_index, error) ||
        !frontend_remote_q2_player_number(row, &row->frame, self_index, &self_number)) return false;
    qa_actor_owner owner = 0;
    qa_actor_definition definition = 0;
    if (self_number >= 0) {
        if (!row->options.entity_actor(row->options.context, &row->options.domain,
                (uint32_t)self_number + 1, &row->collision_viewer, error)) return false;
        const qa_actor_record *viewer = qa_actors_get(actors, row->collision_viewer);
        if (viewer) { owner = viewer->owner; definition = viewer->definition; }
    }
    bool extended = remote_q2_float_movement(row) ||
        (row->options.domain.protocol.kind == QA_NET_Q2PRO_36 &&
            row->data.protocol_revision >= 1025 && (row->data.wire_flags & 16u));
    for (size_t i = 0; i < row->frame.entity_count; ++i) {
        const qa_q2_entity *entity = row->frame.entities + i;
        if (!entity->solid) continue;
        uint32_t model = 0;
        qa_body_state state = {.origin = vector(entity->origin)};
        qa_actor_collision collision = {.family = QA_COLLISION_Q2, .shape = QA_SHAPE_BOX,
            .role = QA_COLLISION_SOLID};
        if (entity->solid == 31) {
            if (entity->modelindex >= row->layout.max_models ||
                !row->collision_models[entity->modelindex].present) continue;
            model = row->collision_models[entity->modelindex].model;
            if (!qa_collision_model_bounds(row->geometry, model, &state.bounds, error)) return false;
            state.angles = vector(entity->angles);
            collision.inline_model = true; collision.model = model;
            collision.contents = extended && entity->number <= row->collision_clients ?
                qa_collision_bit(QA_CONTENT_PLAYER) : (qa_collision_bits){UINT64_MAX, UINT64_MAX};
        } else {
            state.bounds = remote_q2_solid_bounds(row, entity->solid);
            collision.contents = qa_collision_bit(extended && entity->number <= row->collision_clients ?
                QA_CONTENT_PLAYER : QA_CONTENT_MONSTER);
        }
        qa_actor_id actor;
        const qa_actor_record *record = owner ? qa_actors_at_source(actors, owner, entity->number) : NULL;
        if (record && record->definition == definition) actor = record->id;
        else if (!row->options.entity_actor(row->options.context, &row->options.domain,
                entity->number, &actor, error)) return false;
        if (row->collision_count == row->collision_capacity)
            return remote_q2_fail(error, QA_ERROR_MEMORY, "Received solids exceed the loaded entity capacity");
        if (!qa_world_body_write(row->collision_world, actor, &state, error) ||
            !qa_world_set_collision(row->collision_world, actor, &collision, error) ||
            !qa_world_link(row->collision_world, actor, NULL, error)) return false;
        row->collision_actors[row->collision_count++] = actor;
    }
    rules.count = row->collision_count;
    qa_world_set_query_rules(row->collision_world, &rules);
    row->collision_dirty = false; return true;
}
bool remote_q2_trace(void *context, const qa_trace_query *query, qa_trace_result *out, qa_error *error)
{
    frontend_remote_q2 *row = context;
    qa_trace_query q = *query; q.target = (qa_collision_target){0};
    return qa_world_trace_excluding(row->collision_world, &q, &row->collision_viewer,
        row->collision_viewer.registry ? 1 : 0, out, error);
}
static bool contents(void *context, const qa_point_query *query, qa_point_contents *out, qa_error *error)
{
    frontend_remote_q2 *row = context;
    qa_point_query q = *query; q.target = (qa_collision_target){0};
    return qa_world_point_contents(row->collision_world, &q, out, error);
}
static bool is_brush(void *context, const qa_trace_result *hit, bool *out, qa_error *error)
{ (void)context; (void)error; *out = hit->hit == QA_TRACE_HIT_WORLD || hit->model != 0; return true; }
void remote_q2_prediction_receive(frontend_remote_q2 *row)
{
    const qa_q2_player *received = player(row);
    row->predicted = false;
    const remote_q2_sent_command *command = &row->commands[row->acknowledged_command & 63];
    if (!received || !command->valid || command->command_number != row->acknowledged_command || !command->predicted) {
        row->prediction_error = qa_v3(0, 0, 0); return;
    }
    qa_vec3 origin = remote_q2_float_movement(row) ? vector(received->pmove.origin_f) :
        qa_v3((float)received->pmove.origin[0] * .125f, (float)received->pmove.origin[1] * .125f,
            (float)received->pmove.origin[2] * .125f);
    qa_vec3 delta = qa_vec_sub(origin, command->origin);
    row->prediction_error = fabsf(delta.x) + fabsf(delta.y) + fabsf(delta.z) <= 80 ? delta : qa_v3(0, 0, 0);
}
bool remote_q2_prediction_replay(frontend_remote_q2 *row, qa_error *error)
{
    row->predicted = false;
    const qa_q2_player *received = player(row);
    const qa_cvar_view *predict = qa_cvars_read(row->options.domain.cvars, row->cvar_handles.legacy.cl_predict);
    if (!received || !row->media_ready || !row->geometry || !row->options.entity_actor || !row->sent_set ||
        (predict && predict->number == 0) || (received->pmove.flags & 64) ||
        row->last_command < row->acknowledged_command || row->last_command - row->acknowledged_command >= 64) return true;
    uint64_t pending = row->last_command - row->acknowledged_command;
    for (uint64_t offset = 1; offset <= pending; ++offset) {
        uint64_t number = row->acknowledged_command + offset;
        if (!row->commands[number & 63].valid || row->commands[number & 63].command_number != number) return true;
    }
    qa_actor_id actor;
    bool rerelease = remote_q2_float_movement(row);
    bool wide = row->options.domain.protocol.kind == QA_NET_Q2PRO_36 &&
        row->data.protocol_revision >= 1025 && (row->data.wire_flags & 16u);
    qa_ruleset_id kind = rerelease ? QA_RULESET_Q2_RERELEASE : QA_RULESET_Q2_CLASSIC;
    qa_movement_state state = qa_movement_state_default(kind, qa_v3(0, 0, 0));
    if (rerelease) {
        state.data.q2r = (qa_q2r_movement_state){.type = received->pmove.type,
            .origin = vector(received->pmove.origin_f), .velocity = vector(received->pmove.velocity_f),
            .flags = (uint32_t)received->pmove.flags, .time_ms = (uint32_t)received->pmove.time,
            .gravity = (int16_t)received->pmove.gravity,
            .delta_angles = received->pmove.float_delta_angles ? vector(received->pmove.delta_angles_f) :
                qa_v3(received->pmove.delta_angles[0] * (360.0f / 65536),
                    received->pmove.delta_angles[1] * (360.0f / 65536),
                    received->pmove.delta_angles[2] * (360.0f / 65536)),
            .view_height = (float)received->pmove.viewheight};
    } else {
        state.data.q2.type = received->pmove.type; state.data.q2.flags = (uint32_t)received->pmove.flags;
        state.data.q2.wide_coordinates = wide;
        if (received->pmove.time < 0 || received->pmove.time > (wide ? UINT16_MAX : UINT8_MAX))
            return remote_q2_fail(error, QA_ERROR_FORMAT, "Q2 prediction timer leaves its actual wire profile");
        if (wide) state.data.q2.wide.time_ms = (uint16_t)received->pmove.time;
        else state.data.q2.time_eight_ms = (uint8_t)received->pmove.time;
        state.data.q2.gravity = (int16_t)received->pmove.gravity;
        for (size_t i = 0; i < 3; ++i) {
            int32_t minimum = wide ? -INT32_C(4194304) : INT16_MIN;
            int32_t maximum = wide ? INT32_C(4194303) : INT16_MAX;
            if (received->pmove.origin[i] < minimum || received->pmove.origin[i] > maximum ||
                received->pmove.velocity[i] < minimum || received->pmove.velocity[i] > maximum)
                return remote_q2_fail(error, QA_ERROR_FORMAT, "Q2 prediction coordinates leave their actual wire profile");
            qa_q2_movement_coordinate_set(&state.data.q2, false, (unsigned)i, received->pmove.origin[i]);
            qa_q2_movement_coordinate_set(&state.data.q2, true, (unsigned)i, received->pmove.velocity[i]);
            state.data.q2.delta_angle_shorts[i] = received->pmove.delta_angles[i];
        }
    }
    qa_movement_profile profile = qa_movement_profile_default(kind);
    const char *air_text = frontend_remote_q2_config(row, row->layout.air_accelerate); char *end;
    double air = strtod(air_text, &end);
    while (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r') ++end;
    if ((*air_text && (*end || !isfinite(air))) || air < -FLT_MAX || air > FLT_MAX)
        return remote_q2_fail(error, QA_ERROR_FORMAT, "Q2 prediction air acceleration is not a finite source number");
    if (rerelease) { profile.data.q2r.air_accelerate = (float)air;
        profile.data.q2r.n64_physics = strtod(frontend_remote_q2_config(row, 12103), NULL) != 0; }
    else { profile.data.q2.air_accelerate = (float)air; profile.data.q2.strafejump_hack = row->data.strafejump_hack; }
    qa_movement_services services = {.context = row, .trace = remote_q2_trace, .point_contents = contents, .is_bsp = is_brush};
    qa_movement_result result = {0}; bool ok = true; qa_vec3 pml = row->prediction_pml;
    ++row->busy;
    uint32_t player_index; int32_t player_number;
    if (!frontend_remote_q2_wire_seat(row, &player_index, error) ||
        !frontend_remote_q2_player_number(row, &row->frame, player_index, &player_number) ||
        player_number < 0 || !row->options.entity_actor(row->options.context, &row->options.domain,
        (uint32_t)player_number + 1, &actor, error) || !actor.registry || !remote_q2_live(row, error)) {
        --row->busy; return false;
    }
    for (uint64_t offset = 1; ok && offset <= pending; ++offset) {
        uint64_t number = row->acknowledged_command + offset;
        remote_q2_sent_command *sent = row->commands + (number & 63); const qa_q2_usercmd *command = &sent->command;
        qa_movement_input input = qa_movement_input_default(kind, actor);
        input.state = state; input.profile = profile; input.prediction = true; input.snap_initial = number == row->acknowledged_command + 1;
        input.q2r_pml_origin = &pml; input.time_ns = sent->sent_ns;
        input.elapsed_ns = (uint64_t)command->msec * 1000000; input.view_offset = vector(received->viewoffset);
        input.environment.health = (float)received->stats[1]; input.environment.gravity_multiplier = 1;
        input.standing.bounds = (qa_bounds){qa_v3(-16, -16, -24), qa_v3(16, 16, 32)};
        input.crouched.bounds = input.dead.bounds = (qa_bounds){qa_v3(-16, -16, -24), qa_v3(16, 16, 4)};
        input.standing.view_height = 22; input.crouched.view_height = input.dead.view_height = -2;
        input.command = (qa_usercmd){.kind = kind, .sequence = number, .milliseconds = command->msec,
            .server_frame = command->server_frame, .forward_move = command->forwardmove,
            .side_move = command->sidemove, .up_move = command->upmove, .buttons = command->buttons,
            .impulse = command->impulse, .light_level = command->lightlevel};
        input.command.angles = qa_v3((float)(uint16_t)command->angles[0] * (360.0f / 65536),
            (float)(uint16_t)command->angles[1] * (360.0f / 65536), (float)(uint16_t)command->angles[2] * (360.0f / 65536));
        for (size_t i = 0; i < 3; ++i) input.command.angle_words[i] = command->angles[i];
        ok = qa_movement_move(&input, &services, &result, error) && result.status == QA_MOVEMENT_ACTIVE;
        if (ok) { state = result.state; sent->origin = qa_movement_origin(&state); sent->predicted = true; }
    }
    if (ok) {
        row->prediction_origin = qa_movement_origin(&state); row->prediction_pml = pml;
        row->prediction_angles = result.command_sequence ? result.view_angles : vector(received->viewangles);
        qa_collision_plane plane = {0};
        for (size_t i = 0; i < result.contact_count; ++i) {
            const qa_trace_result *hit = &result.contacts[i].trace;
            if (hit->hit == result.ground.hit && hit->model == result.ground.model &&
                qa_actor_id_equal(hit->actor, result.ground.actor) && hit->plane.normal.z >= .7f) plane = hit->plane;
        }
        const remote_q2_sent_command *old = &row->commands[(row->last_command - 1) & 63];
        if (result.command_sequence && old->valid && old->predicted && old->command_number + 1 == row->last_command &&
            result.ground.hit != QA_TRACE_HIT_NONE && (row->prediction_command != row->last_command ||
                row->prediction_frame != row->frame.server_frame)) {
            float change = row->prediction_origin.z - old->origin.z;
            bool stepped = rerelease ? fabsf(change) > 1 && fabsf(change) < 20 &&
                ((received->pmove.flags & 4) || result.step_clip) && state.data.q2r.type <= 1 &&
                (row->prediction_ground.hit != result.ground.hit || row->prediction_ground.model != result.ground.model ||
                    !qa_actor_id_equal(row->prediction_ground.actor, result.ground.actor) ||
                    row->prediction_plane.normal.x != plane.normal.x || row->prediction_plane.normal.y != plane.normal.y ||
                    row->prediction_plane.normal.z != plane.normal.z || row->prediction_plane.distance != plane.distance) :
                change > 63.0f / 8 && change < 20;
            if (stepped) {
                double elapsed = row->sample_ns >= row->prediction_step_ns ?
                    (double)(row->sample_ns - row->prediction_step_ns) / 1000000 : 100;
                float remaining = rerelease && elapsed < 100 ? row->prediction_step * (1 - (float)elapsed * .01f) : 0;
                row->prediction_step = rerelease ? fmaxf(-32, fminf(32, remaining + change)) : change;
                row->prediction_step_ns = row->sample_ns;
            }
        }
        if (result.command_sequence) { row->prediction_ground = result.ground; row->prediction_plane = plane; }
        row->prediction_command = row->last_command; row->prediction_frame = row->frame.server_frame;
        row->predicted = true;
    }
    qa_movement_result_free(&result); --row->busy;
    return ok && remote_q2_live(row, error);
}
