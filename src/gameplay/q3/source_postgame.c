/* Victory pads from id Software's code/game/g_arenas.c and its TypeScript port.
 * Copyright (C) 1999-2005 Id Software, Inc.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include "source_postgame.h"
#include "internal.h"
#include "qa/game_q3_wire.h"

static bool postgame_kind(const q3_actor *entry)
{
    return entry && (entry->kind == Q3_ACTOR_PODIUM || entry->kind == Q3_ACTOR_VICTORY_MODEL);
}

bool qa_q3_source_postgame_client_state(const qa_q3_game *game, uint32_t slot,
    qa_q3_player_state *out, qa_error *error)
{
    if (!game || !out || slot >= QA_Q3_SOURCE_CLIENTS || game->source_restored)
        return q3_fail(error, "postgame PS read exceeds the original fixed source clients");
    *out = game->client_actors[slot].state.player;
    return true;
}

static float source_yaw(qa_vec3 direction)
{
    float yaw;
    if (direction.x == 0 && direction.y == 0) return 0;
    if (direction.x != 0) {
        float radians = (float)atan2((double)direction.y, (double)direction.x);
        yaw = q3_source_float_divide(q3_source_float_multiply(radians, 180), Q3_PI);
    } else yaw = direction.y > 0 ? 90 : 270;
    return yaw < 0 ? q3_source_float_add(yaw, 360) : yaw;
}

static bool podium_origin(qa_q3_game *game, qa_vec3 *out, qa_error *error)
{
    if (!game->options.hooks.postgame_cvar_integer)
        return q3_fail(error, "podium placement has no actual live engine cvar trap");
    qa_vec3 forward;
    q3_source_angle_vectors(game->match_state.intermission_angles, &forward, NULL, NULL);
    qa_vec3 origin = game->match_state.intermission_origin;
    int32_t distance, drop;
    for (unsigned axis = 0; axis < 3; ++axis) {
        if (!game->options.hooks.postgame_cvar_integer(game->options.hooks.context,
                "g_podiumDist", &distance, error)) return false;
        float component = q3_source_float_add(q3_source_vec_component(origin, axis),
            q3_source_float_multiply(q3_source_vec_component(forward, axis), (float)distance));
        if (!axis) origin.x = component;
        else if (axis == 1) origin.y = component;
        else origin.z = component;
    }
    if (!game->options.hooks.postgame_cvar_integer(game->options.hooks.context,
            "g_podiumDrop", &drop, error)) return false;
    origin.z = q3_source_float_add(origin.z, -(float)drop);
    *out = origin;
    return true;
}

static bool set_row_origin(qa_q3_game *game, uint32_t slot, qa_vec3 origin,
                           const qa_trajectory *angular, int32_t ground, qa_error *error)
{
    qa_actor_id actor;
    if (!q3_source_row_body_ensure(game, slot, &actor, error)) return false;
    qa_body_state body;
    if (!qa_world_body_read(game->options.services.world, actor, &body, error)) return false;
    if (!qa_q3_wire_entity_motion_write(game, actor,
            &(qa_trajectory){.type = QA_TRAJECTORY_STATIONARY, .base = origin},
            angular, ground, error)) return false;
    body.origin = origin;
    return qa_world_body_write(game->options.services.world, actor, &body, error);
}
static qa_trajectory trajectory(const qa_q3_trajectory *);
static bool source_entity(qa_q3_game *game, uint32_t slot, qa_q3_entity *out,
                           qa_error *error)
{
    if (slot >= game->source_count)
        return q3_fail(error, "podium reference exceeds actual raw source rows");
    const q3_actor *entry = q3_actor_const(game, game->source_entities[slot].actor);
    if (postgame_kind(entry)) { *out = entry->state.postgame.entity; return true; }
    qa_q3_wire_visibility visible;
    return qa_q3_wire_entity_read(game, slot, out, &visible, error);
}
static bool set_origin(qa_q3_game *game, qa_actor_id actor, qa_vec3 origin, qa_error *error)
{
    uint32_t slot;
    qa_q3_entity entity;
    if (!qa_q3_source_actor_slot(game, actor, &slot, error) ||
        !source_entity(game, slot, &entity, error)) return false;
    qa_trajectory angular = trajectory(&entity.apos);
    return set_row_origin(game, slot, origin, &angular, entity.groundEntityNum, error);
}

static bool place_model(qa_q3_game *game, uint32_t model, qa_actor_id podium,
    qa_vec3 offset, qa_error *error)
{
    qa_body_state pad;
    if (!qa_world_body_read(game->options.services.world, podium, &pad, error)) return false;
    qa_q3_entity entity;
    if (!source_entity(game, model, &entity, error)) return false;
    qa_vec3 angles = qa_v3(0, source_yaw(qa_vec_sub(game->match_state.intermission_origin,
                                                 pad.origin)), 0);
    qa_trajectory angular = trajectory(&entity.apos);
    angular.base = angles;
    qa_vec3 forward, right, up;
    q3_source_angle_vectors(angles, &forward, &right, &up);
    qa_vec3 origin = qa_vec_add(pad.origin, qa_vec_scale(forward, offset.x));
    origin = qa_vec_add(origin, qa_vec_scale(right, offset.y));
    origin = qa_vec_add(origin, qa_vec_scale(up, offset.z));
    return set_row_origin(game, model, origin, &angular, entity.groundEntityNum, error);
}

static bool spawn_podium(qa_q3_game *game, qa_actor_id *out, qa_error *error)
{
    qa_string_id classname;
    if (!qa_builtin_resource(&game->options.services, "podium", &classname, error)) return false;
    qa_actor_collision collision = {.family = QA_COLLISION_Q3, .shape = QA_SHAPE_BOX,
        .contents = 1, .role = QA_COLLISION_SOLID};
    qa_actor_id actor;
    if (!q3_spawn_raw_actor(game, classname, &actor, error)) return false;
    uint32_t slot;
    if (!qa_q3_source_actor_slot(game, actor, &slot, error)) return false;
    qa_q3_entity original; qa_q3_wire_visibility visible;
    if (!qa_q3_wire_entity_read(game, slot, &original, &visible, error) ||
        !qa_world_set_collision(game->options.services.world, actor, &collision, error)) return false;
    game->source_entities[slot].classname = classname;
    game->actors[actor.slot] = (q3_actor){.actor = actor, .kind = Q3_ACTOR_PODIUM, .alpha = 1,
        .state.postgame.entity = original};
    q3_actor *entry = q3_actor_get(game, actor);
    game->source_entities[slot].client_slot = -1;
    entry->state.postgame.entity.number = (int32_t)slot;
    entry->state.postgame.entity.eType = 0;
    int32_t model;
    if (!qa_q3_model_index(game, "models/mapobjects/podium/podium4.md3", &model, error)) return false;
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PODIUM)
        return q3_fail(error, "podium was replaced during model registration");
    entry->state.postgame.entity.modelindex = model;
    qa_vec3 origin;
    if (!podium_origin(game, &origin, error) || !set_origin(game, actor, origin, error)) return false;
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PODIUM)
        return q3_fail(error, "podium was replaced during its origin producer");
    entry->state.postgame.entity.apos.base[1] = source_yaw(
        qa_vec_sub(game->match_state.intermission_origin, origin));
    if (!q3_wire_entity_ready(game, actor, error) ||
        !qa_q3_wire_link(game, actor, NULL, error)) return false;
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PODIUM)
        return q3_fail(error, "podium was replaced during its genuine link");
    q3_wire_entity_source *think = q3_wire_entity(game, actor);
    think->arena_think = Q3_POSTGAME_THINK_PLACEMENT;
    think->arena_nextthink = q3_add_time(game->now_ms, 100);
    *out = actor;
    return true;
}

static bool spawn_model(qa_q3_game *game, qa_actor_id podium, uint32_t client,
    qa_vec3 offset, qa_actor_id *out, qa_error *error)
{
    if (client >= QA_Q3_SOURCE_CLIENTS) return q3_fail(error, "victory model has no fixed source client");
    qa_actor_id actor;
    if (!q3_spawn_raw_actor(game, game->source_noclass, &actor, error))
        return false;
    qa_q3_entity original;
    qa_q3_wire_visibility visible;
    qa_q3_wire_client_body body_source;
    if (!qa_q3_wire_entity_read(game, client, &original, &visible, error) ||
        !qa_q3_wire_client_source_body_read(game, client, &body_source, error))
        return q3_rollback_spawn(game, actor, error);
    qa_q3_source_binding source = game->source_entities[client];
    qa_actor_collision collision = {.family = QA_COLLISION_Q3,
        .shape = body_source.model_shape == QA_SHAPE_CAPSULE ? QA_SHAPE_CAPSULE : QA_SHAPE_BOX,
        .contents = Q3_CONTENTS_BODY, .role = QA_COLLISION_SOLID,
        .has_q3_owner = true, .q3_owner_number = source.owner_number};
    qa_body_state body;
    if (!qa_world_body_read(game->options.services.world, actor, &body, error))
        return q3_rollback_spawn(game, actor, error);
    body.bounds = body_source.current.bounds;
    body.ground = game->source_entities[QA_Q3_SOURCE_WORLD].actor;
    if (!qa_world_body_write(game->options.services.world, actor, &body, error))
        return q3_rollback_spawn(game, actor, error);
    uint32_t slot;
    if (!qa_q3_source_actor_slot(game, actor, &slot, error)) return false;
    game->source_entities[slot].server_flags = source.server_flags;
    game->source_entities[slot].owner_number = source.owner_number;
    collision.q3_entity_number = (int32_t)slot;
    if (!qa_world_set_collision(game->options.services.world, actor, &collision, error)) return false;
    qa_combat_state combat;
    if (!qa_combat_read_traits(game->options.services.combat, actor, &combat, error)) return false;
    combat.can_take_damage = false;
    if (!qa_combat_set_traits(game->options.services.combat, actor, &combat, error)) return false;
    game->actors[actor.slot] = (q3_actor){.actor = actor, .kind = Q3_ACTOR_VICTORY_MODEL, .alpha = 1};
    q3_actor *entry = q3_actor_get(game, actor);
    if (source.client_slot < 0 || source.client_slot >= (int32_t)QA_Q3_SOURCE_CLIENTS)
        return q3_fail(error, "victory model requires its genuine source client pointer");
    qa_string_id classname;
    if (!qa_builtin_resource(&game->options.services,
            game->clients[source.client_slot].netname, &classname, error) ||
        !qa_actors_set_metadata(qa_session_actor_registry(game->options.services.session),
            actor, game->options.owner, classname, error))
        return q3_rollback_spawn(game, actor, error);
    game->source_entities[slot].classname = classname;
    game->source_entities[slot].client_slot = source.client_slot;
    entry->state.postgame.timestamp = game->now_ms;
    entry->state.postgame.physics_object = true;
    entry->state.postgame.entity = original;
    qa_q3_entity *entity = &entry->state.postgame.entity;
    entity->eType = 1;
    entity->eFlags = entity->powerups = entity->loopSound = 0;
    entity->number = (int32_t)slot;
    entity->event = 0;
    entity->pos.type = QA_TRAJECTORY_STATIONARY;
    entity->groundEntityNum = QA_Q3_SOURCE_WORLD;
    entity->legsAnim = 22;
    entity->torsoAnim = 11;
    if (!entity->weapon) entity->weapon = QA_Q3_W_MACHINEGUN;
    if (entity->weapon == QA_Q3_W_GAUNTLET) entity->torsoAnim = 12;
    entry->state.postgame.count = game->client_actors[client].state.player.rank & ~0x4000;
    if (!place_model(game, slot, podium, offset, error) ||
        !q3_wire_entity_ready(game, actor, error) ||
        !qa_q3_wire_link(game, actor, NULL, error)) return false;
    *out = actor;
    return true;
}

bool qa_q3_source_reset_podium_players(qa_q3_game *game, qa_error *error)
{
    if (!game || game->source_restored)
        return q3_fail(error, "podium reset requires its actual source level");
    for (size_t i = 0; i < 3; ++i) game->podium_players[i] = QA_Q3_SOURCE_NONE;
    return true;
}

static bool spawn_victory_pads(qa_q3_game *game, qa_error *error)
{
    if (!qa_q3_source_reset_podium_players(game, error)) return false;
    qa_actor_id podium, player;
    if (!spawn_podium(game, &podium, error) ||
        !spawn_model(game, podium, game->client_counts.sorted_clients[0], qa_v3(0, 0, 74),
                     &player, error)) return false;
    q3_actor *entry = q3_actor_get(game, player);
    if (!entry || entry->kind != Q3_ACTOR_VICTORY_MODEL)
        return q3_fail(error, "first victory model lost its source generation");
    q3_wire_entity_source *think = q3_wire_entity(game, player);
    think->arena_nextthink = q3_add_time(game->now_ms, 2000);
    think->arena_think = Q3_POSTGAME_THINK_CELEBRATE_START;
    if (!qa_q3_source_actor_slot(game, player, &game->podium_players[0], error)) return false;
    if (!spawn_model(game, podium, game->client_counts.sorted_clients[1], qa_v3(-10, 60, 54),
                     &player, error)) return false;
    if (!qa_q3_source_actor_slot(game, player, &game->podium_players[1], error)) return false;
    if (game->client_counts.num_non_spectator > 2) {
        if (!spawn_model(game, podium, game->client_counts.sorted_clients[2], qa_v3(-19, -60, 45),
                         &player, error)) return false;
        if (!qa_q3_source_actor_slot(game, player, &game->podium_players[2], error)) return false;
    }
    return true;
}

bool qa_q3_source_spawn_victory_pads(qa_q3_game *game, qa_error *error)
{
    if (!game || game->source_restored || game->observation_depth == SIZE_MAX)
        return q3_fail(error, "victory pads require their live native source level");
    ++game->observation_depth;
    bool okay = spawn_victory_pads(game, error);
    --game->observation_depth;
    return okay;
}

bool qa_q3_source_abort_podium(qa_q3_game *game, qa_error *error)
{
    if (!game || game->source_restored)
        return q3_fail(error, "podium abort requires its actual source level");
    if (game->options.rules.game_type != 2 || game->podium_players[0] == QA_Q3_SOURCE_NONE) return true;
    q3_wire_entity_source *think = q3_wire_entity_slot(game, game->podium_players[0]);
    if (!think) return q3_fail(error, "podium abort exceeds its retained physical source row");
    think->arena_nextthink = game->now_ms;
    think->arena_think = Q3_POSTGAME_THINK_CELEBRATE_STOP;
    return true;
}

void q3_postgame_native_think_assigned(qa_q3_game *game, qa_actor_id actor)
{
    q3_wire_entity_source *source = q3_wire_entity(game, actor);
    if (source) source->arena_think = source->arena_nextthink = 0;
}
int32_t q3_postgame_think_time(const qa_q3_game *game, qa_actor_id actor, int32_t fallback)
{
    uint32_t slot;
    if (!qa_q3_source_actor_slot(game, actor, &slot, NULL)) return fallback;
    q3_wire_entity_source *source = q3_wire_entity_slot((qa_q3_game *)game, slot);
    return source && source->arena_think ? source->arena_nextthink : fallback;
}
void q3_postgame_nextthink_assigned(qa_q3_game *game, qa_actor_id actor, int32_t time)
{
    q3_wire_entity_source *source = q3_wire_entity(game, actor);
    if (source && source->arena_think) source->arena_nextthink = time;
}

bool q3_postgame_think_override(qa_q3_game *game, qa_actor_id actor, bool *handled,
                                 qa_error *error)
{
    *handled = false;
    q3_wire_entity_source *source = q3_wire_entity(game, actor);
    if (!source || !source->arena_think) return true;
    *handled = true;
    float scheduled = (float)source->arena_nextthink;
    if (scheduled <= 0 || (double)scheduled > (double)game->now_ms) return true;
    int32_t think = source->arena_think;
    source->arena_nextthink = 0;
    switch (think) {
    case Q3_POSTGAME_THINK_PLACEMENT: {
        source->arena_nextthink = q3_add_time(game->now_ms, 100);
        qa_vec3 origin;
        if (!podium_origin(game, &origin, error) || !set_origin(game, actor, origin, error)) return false;
        static const qa_vec3 offsets[3] = {{0, 0, 74}, {-10, 60, 54}, {-19, -60, 45}};
        for (unsigned index = 0; index < 3; ++index)
            if (game->podium_players[index] != QA_Q3_SOURCE_NONE &&
                !place_model(game, game->podium_players[index], actor, offsets[index], error)) return false;
        return true;
    }
    case Q3_POSTGAME_THINK_CELEBRATE_START: case Q3_POSTGAME_THINK_CELEBRATE_STOP: {
        uint32_t slot;
        qa_q3_entity entity;
        if (!qa_q3_source_actor_slot(game, actor, &slot, error) ||
            !source_entity(game, slot, &entity, error)) return false;
        int32_t animation = think == Q3_POSTGAME_THINK_CELEBRATE_START ? 6 :
            entity.weapon == QA_Q3_W_GAUNTLET ? 12 : 11;
        int32_t torso = ((entity.torsoAnim & 128) ^ 128) | animation;
        q3_actor *entry = q3_actor_get(game, actor);
        if (postgame_kind(entry)) entry->state.postgame.entity.torsoAnim = torso;
        else if (entry && entry->kind == Q3_ACTOR_TEMPORARY)
            entry->state.temporary.entity.torsoAnim = torso;
        else source->torso = torso;
        if (think == Q3_POSTGAME_THINK_CELEBRATE_START) {
            source->arena_nextthink = q3_add_time(game->now_ms, 2294);
            source->arena_think = Q3_POSTGAME_THINK_CELEBRATE_STOP;
            return q3_add_event(game, actor, 76, 0, error);
        }
        return true;
    }
    default:
        return q3_fail(error, "victory entity reached an absent source think callback");
    }
}
static bool postgame_think(qa_q3_game *game, qa_actor_id actor, qa_error *error)
{
    bool handled;
    return q3_postgame_think_override(game, actor, &handled, error);
}

static qa_trajectory trajectory(const qa_q3_trajectory *source)
{
    return (qa_trajectory){.type = (qa_trajectory_type)source->type,
        .time_ms = source->time, .duration_ms = source->duration,
        .base = qa_v3(source->base[0], source->base[1], source->base[2]),
        .delta = qa_v3(source->delta[0], source->delta[1], source->delta[2])};
}

bool q3_postgame_step(qa_q3_game *game, qa_actor_id actor, qa_error *error)
{
    q3_actor *entry = q3_actor_get(game, actor);
    if (!postgame_kind(entry)) return true;
    if (entry->kind == Q3_ACTOR_PODIUM || !entry->state.postgame.physics_object)
        return postgame_think(game, actor, error);
    qa_q3_entity *entity = &entry->state.postgame.entity;
    if (entity->groundEntityNum == -1 && entity->pos.type != QA_TRAJECTORY_GRAVITY) {
        entity->pos.type = QA_TRAJECTORY_GRAVITY;
        entity->pos.time = game->now_ms;
    }
    if (entity->pos.type == QA_TRAJECTORY_STATIONARY) return postgame_think(game, actor, error);
    qa_trajectory position = trajectory(&entity->pos);
    qa_vec3 destination;
    qa_body_state body;
    if (!qa_trajectory_position(&position, game->now_ms, 800, &destination, error) ||
        !qa_world_body_read(game->options.services.world, actor, &body, error)) return false;
    uint32_t slot;
    if (!qa_q3_source_actor_slot(game, actor, &slot, error)) return false;
    int32_t owner_number = game->source_entities[slot].owner_number;
    qa_actor_id owner = owner_number >= 0 && owner_number < (int32_t)QA_Q3_SOURCE_WORLD &&
        game->source_entities[owner_number].in_use
            ? game->source_entities[owner_number].actor : (qa_actor_id){0};
    qa_trace_query query = {.start = body.origin, .end = destination, .pass_actor = owner,
        .shape = {.kind = QA_SHAPE_BOX, .bounds = body.bounds},
        .policy = qa_collision_default_policy(QA_COLLISION_Q3)};
    query.policy.contents_mask = UINT32_C(0x10001);
    qa_trace_result trace;
    if (!qa_world_trace(game->options.services.world, &query, &trace, error)) return false;
    if (!q3_actor_get(game, actor)) return true;
    body.origin = trace.end;
    if (!qa_world_body_write(game->options.services.world, actor, &body, error) ||
        !qa_q3_wire_link(game, actor, NULL, error) || !postgame_think(game, actor, error)) return false;
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_VICTORY_MODEL) return true;
    float fraction = trace.start_solid || trace.all_solid ? 0 : trace.fraction;
    if (fraction == 1) return true;
    qa_point_query point = {.point = body.origin, .policy = qa_collision_default_policy(QA_COLLISION_Q3)};
    qa_point_contents contents;
    if (!qa_world_point_contents(game->options.services.world, &point, &contents, error)) return false;
    if (contents.contents & INT32_MIN)
        return qa_session_release(game->options.services.session, actor, error);
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_VICTORY_MODEL) return true;
    position = trajectory(&entry->state.postgame.entity.pos);
    qa_vec3 velocity;
    int32_t hit_time = qa_physics_q3_hit_time(game->previous_ms, game->now_ms, fraction);
    if (!qa_trajectory_velocity(&position, hit_time, 800, &velocity, error)) return false;
    qa_vec3 normal = trace.contact ? trace.contact_plane.normal : qa_v3(0, 0, 0);
    float dot = qa_vec_dot(velocity, normal);
    velocity = qa_vec_scale(qa_vec_add(velocity,
        qa_vec_scale(normal, q3_source_float_multiply(-2, dot))), entry->state.postgame.physics_bounce);
    entity = &entry->state.postgame.entity;
    entity->pos.delta[0] = velocity.x; entity->pos.delta[1] = velocity.y; entity->pos.delta[2] = velocity.z;
    if (normal.z > 0 && velocity.z < 40) {
        qa_vec3 stopped = qa_physics_q3_snap(qa_v3(trace.end.x, trace.end.y,
            q3_source_float_add(trace.end.z, 1)));
        if (!set_origin(game, actor, stopped, error)) return false;
        entry = q3_actor_get(game, actor);
        if (!entry || entry->kind != Q3_ACTOR_VICTORY_MODEL) return true;
        entry->state.postgame.entity.groundEntityNum = trace.hit == QA_TRACE_HIT_WORLD
            ? (int32_t)QA_Q3_SOURCE_WORLD : trace.hit == QA_TRACE_HIT_ACTOR
                ? q3_entity_number(game, trace.actor) : (int32_t)QA_Q3_SOURCE_NONE;
        if (!qa_world_body_read(game->options.services.world, actor, &body, error)) return false;
        body.ground = trace.hit == QA_TRACE_HIT_WORLD ? game->source_entities[QA_Q3_SOURCE_WORLD].actor
            : trace.hit == QA_TRACE_HIT_ACTOR ? trace.actor : (qa_actor_id){0};
    } else {
        if (!qa_world_body_read(game->options.services.world, actor, &body, error)) return false;
        body.origin = qa_vec_add(body.origin, normal);
        entity->pos.base[0] = body.origin.x; entity->pos.base[1] = body.origin.y;
        entity->pos.base[2] = body.origin.z; entity->pos.time = game->now_ms;
    }
    return qa_world_body_write(game->options.services.world, actor, &body, error);
}
