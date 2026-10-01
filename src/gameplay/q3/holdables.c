#include "internal.h"

bool q3_killbox(qa_q3_game *game, qa_actor_id actor, qa_error *error) {
    qa_body_state body;
    if (!q3_source_body_read(game, actor, &body, error))
        return false;
    if (!qa_actors_get(qa_session_actors(game->options.services.session), actor))
        return true;
    q3_snapshot_frame *frame = q3_bounds_snapshot(
        game, qa_bounds_translate(body.bounds, body.origin), QA_COLLISION_SOLID, error);
    if (!frame)
        return false;
    bool ok = true;
    for (size_t i = 0; i < frame->snapshot.count; ++i) {
        if (!qa_actors_get(qa_session_actors(game->options.services.session), actor))
            break;
        qa_actor_id candidate = frame->snapshot.ids[i];
        if (qa_actor_id_equal(candidate, actor))
            continue;
        qa_linked_body linked;
        if (!qa_world_linked(game->options.services.world, candidate, &linked))
            continue;
        bool player = q3_is_player(game, candidate);
        if (!qa_actors_get(qa_session_actors(game->options.services.session), actor))
            break;
        if (!player || !qa_actors_get(qa_session_actors(game->options.services.session), candidate))
            continue;
        qa_combat_state state;
        qa_error ignored = {0};
        if (!qa_combat_read(game->options.services.combat, candidate, &state, &ignored) ||
            !state.can_take_damage)
            continue;
        if (!qa_actors_get(qa_session_actors(game->options.services.session), actor))
            break;
        if (!qa_actors_get(qa_session_actors(game->options.services.session), candidate))
            continue;
        if (!q3_damage(game, candidate, actor, actor, QA_Q3_W_NONE, 18, 12, 100000,
                       qa_v3(0, 0, 0), body.origin, false, NULL, error))
            ok = false;
        if (!ok)
            break;
        if (!qa_actors_get(qa_session_actors(game->options.services.session), actor))
            break;
    }
    frame->active = false;
    return ok;
}
static bool teleport_player(qa_q3_game *game, qa_actor_id actor, qa_vec3 origin, qa_vec3 angles,
                            qa_error *error) {
    if (!qa_vec_finite(origin) || !qa_vec_finite(angles))
        return q3_fail(error, "invalid Q3 teleport destination");
    if (!q3_is_player(game, actor))
        return q3_fail(error, "Q3 teleport requires a live player");
    if (!qa_actors_get(qa_session_actors(game->options.services.session), actor))
        return true;
    qa_body_state body;
    if (!q3_source_body_read(game, actor, &body, error))
        return false;
    if (!qa_actors_get(qa_session_actors(game->options.services.session), actor))
        return true;
    q3_actor *entry = q3_actor_get(game, actor);
    qa_builtin_actor_traits traits = {0};
    uint32_t native_slot;
    bool native = qa_q3_native_client_slot(game, actor, &native_slot, NULL);
    if (!native && game->options.services.actor_traits)
        (void)game->options.services.actor_traits(game->options.services.context, actor, &traits);
    if (!qa_actors_get(qa_session_actors(game->options.services.session), actor))
        return true;
    entry = q3_actor_get(game, actor);
    if (native && (!entry || entry->kind != Q3_ACTOR_PLAYER))
        return true;
    bool spectator = native ? game->clients[native_slot].session.team == 3 : traits.spectator;
    if (!spectator) {
        if (!q3_teleport_event_at(game, actor, body.origin, false, error) ||
            !q3_event(game, actor, (qa_actor_id){0}, QA_BUILTIN_TELEPORT, 43, 0,
                      body.origin, origin, qa_v3(0, 0, 0), error))
            return false;
        if (!qa_actors_get(qa_session_actors(game->options.services.session), actor))
            return true;
        if (!q3_teleport_event_at(game, actor, origin, true, error) ||
            !q3_event(game, actor, (qa_actor_id){0}, QA_BUILTIN_TELEPORT, 42, 0, origin,
                      body.origin, qa_v3(0, 0, 0), error))
            return false;
    }
    if (!qa_actors_get(qa_session_actors(game->options.services.session), actor))
        return true;
    if (!qa_world_unlink(game->options.services.world, actor, error))
        return false;
    if (!qa_actors_get(qa_session_actors(game->options.services.session), actor))
        return true;
    body.origin = qa_vec_add(origin, qa_v3(0, 0, 1));
    body.angles = angles;
    body.ground = (qa_actor_id){0};
    qa_vec3 forward;
    q3_source_angle_vectors(angles, &forward, NULL, NULL);
    body.velocity = qa_vec_scale(forward, 400);
    if (!qa_world_body_write(game->options.services.world, actor, &body, error))
        return false;
    q3_source_origin_written(game, actor, body.origin);
    if (!qa_actors_get(qa_session_actors(game->options.services.session), actor))
        return true;
    entry = q3_actor_get(game, actor);
    if (entry && entry->kind == Q3_ACTOR_PLAYER) {
        qa_q3_player_state *player = &entry->state.player;
        player->flags ^= 4u;
        q3_force_view(player, angles, 160);
    }
    if (game->options.services.motion_changed) {
        qa_builtin_motion_change change = {.reason = QA_BUILTIN_MOTION_TELEPORT,
                                           .body = body,
                                           .view_angles = angles,
                                           .force_view_angles = true,
                                           .hold_ns = UINT64_C(160000000)};
        if (!game->options.services.motion_changed(game->options.services.context, actor, &change,
                                                   error))
            return false;
    }
    if (!qa_actors_get(qa_session_actors(game->options.services.session), actor))
        return true;
    if (!spectator) {
        if (!q3_killbox(game, actor, error))
            return false;
        if (!qa_actors_get(qa_session_actors(game->options.services.session), actor))
            return true;
    }
    uint32_t client_slot;
    if (qa_q3_native_client_slot(game, actor, &client_slot, NULL) &&
        !qa_q3_wire_player_publish(game, actor, true, false, 0, error))
        return false;
    if (!qa_actors_get(qa_session_actors(game->options.services.session), actor))
        return true;
    if (!spectator) {
        uint32_t source_slot;
        return qa_q3_source_actor_slot(game, actor, &source_slot, NULL)
            ? qa_q3_wire_link(game, actor, NULL, error)
            : qa_world_link(game->options.services.world, actor, NULL, error);
    }
    return true;
}
bool qa_q3_teleport(qa_q3_game *game, qa_actor_id actor, qa_vec3 origin, qa_vec3 angles,
                    qa_error *error) {
    if (!game || game->source_restored || game->observation_depth == SIZE_MAX)
        return q3_fail(error, "invalid Q3 teleport action boundary");
    ++game->observation_depth;
    bool okay = teleport_player(game, actor, origin, angles, error);
    --game->observation_depth;
    return okay;
}
qa_actor_id q3_portal_destination(qa_q3_game *game, int32_t sequence) {
    for (uint32_t slot = QA_Q3_SOURCE_CLIENTS; slot < game->source_count; ++slot) {
        qa_q3_source_binding binding = game->source_entities[slot];
        q3_actor *entry = binding.in_use ? q3_actor_get(game, binding.actor) : NULL;
        if (entry && entry->kind == Q3_ACTOR_PORTAL && !entry->state.portal.source &&
            entry->state.portal.sequence == sequence)
            return binding.actor;
    }
    return (qa_actor_id){0};
}
static bool portal_drop(qa_q3_game *game, qa_actor_id actor, qa_error *error) {
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry)
        return true;
    bool source = entry->state.player.portal_id != 0;
    int32_t sequence = entry->state.player.portal_id;
    qa_actor_id destination = source ? q3_portal_destination(game, sequence) : (qa_actor_id){0};
    qa_body_state body;
    if (!q3_source_body_read(game, actor, &body, error))
        return false;
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER)
        return true;
    uint32_t player_slot;
    qa_vec3 player_origin = body.origin, player_angles = body.angles;
    if (qa_q3_native_client_slot(game, actor, &player_slot, NULL)) {
        qa_q3_entity player_source;
        qa_q3_wire_visibility player_visibility;
        if (!qa_q3_wire_entity_read(game, player_slot, &player_source, &player_visibility, error))
            return false;
        player_origin = qa_v3(player_source.pos.base[0], player_source.pos.base[1],
                               player_source.pos.base[2]);
        player_angles = qa_v3(player_source.apos.base[0], player_source.apos.base[1],
                               player_source.apos.base[2]);
    }
    qa_actor_collision collision = {.family = QA_COLLISION_Q3,
                                    .shape = QA_SHAPE_BOX,
                                    .contents =
                                        INT32_C(0x04000000) | (source ? Q3_CONTENTS_TRIGGER : 0),
                                    .role = source ? QA_COLLISION_BOTH : QA_COLLISION_SOLID};
    qa_combat_state combat = {.health = 200, .mass = 200, .can_take_damage = true};
    body.origin = qa_physics_q3_snap(player_origin);
    body.angles = qa_v3(0, 0, 0);
    qa_vec3 portal_angles = source ? qa_v3(0, 0, 0)
        : player_angles;
    body.velocity = qa_v3(0, 0, 0);
    body.ground = (qa_actor_id){0};
    qa_builtin_spawn spawn = {.owner = game->options.owner,
                              .body = body,
                              .collision = &collision,
                              .combat = &combat};
    if (!qa_builtin_resource(&game->options.services,
            source ? "hi_portal source" : "hi_portal destination", &spawn.definition, error)) return false;
    qa_actor_id portal;
    if (!q3_spawn_actor(game, &spawn, &portal, error))
        return false;
    q3_actor *p = &game->actors[portal.slot];
    *p = (q3_actor){
        .actor = portal,
        .kind = Q3_ACTOR_PORTAL,
        .alpha = 1,
        .state.portal = {.owner = actor,
                         .destination = destination,
                         .expire_at = source ? 0 : q3_add_time(game->now_ms, 120000),
                         .activate_at = q3_add_time(game->now_ms, source ? 1000 : INT_MAX / 2),
                         .angles = portal_angles,
                         .sequence = sequence,
                         .enabled = !source,
                         .source = source}};
    q3_postgame_native_think_assigned(game, portal);
    int32_t model_index;
    if (!qa_q3_model_index(game, source ? "models/powerups/teleporter/tele_enter.md3"
                                       : "models/powerups/teleporter/tele_exit.md3",
                           &model_index, error))
        return q3_rollback_spawn(game, portal, error);
    q3_wire_entity_source *wire = q3_wire_entity(game, portal);
    if (!wire)
        return q3_rollback_spawn(game, portal, error);
    wire->model = model_index;
    wire->position = (qa_trajectory){.type = QA_TRAJECTORY_STATIONARY, .base = body.origin};
    wire->authored_angles = portal_angles;
    if (!qa_q3_wire_link(game, portal, NULL, error) ||
        !q3_wire_entity_ready(game, portal, error))
        return q3_rollback_spawn(game, portal, error);
    p = q3_actor_get(game, portal);
    if (!p || p->kind != Q3_ACTOR_PORTAL) {
        q3_fail(error, "Q3 portal retired during admission");
        return q3_rollback_spawn(game, portal, error);
    }
    if (source && q3_actor_get(game, destination)) {
        qa_body_state dest;
        if (!q3_source_body_read(game, destination, &dest, error))
            return q3_rollback_spawn(game, portal, error);
        p = q3_actor_get(game, portal);
        if (!p || p->kind != Q3_ACTOR_PORTAL) {
            q3_fail(error, "Q3 portal retired during admission");
            return q3_rollback_spawn(game, portal, error);
        }
        p->state.portal.fallback = dest.origin;
    }
    entry = q3_actor_get(game, actor);
    if (!entry)
        return !q3_actor_get(game, portal) ||
               qa_session_release(game->options.services.session, portal, error);
    qa_actor_id published_portal = source ? (qa_actor_id){0} : portal;
    entry->state.player.portal = published_portal;
    if (!source) {
        game->portal_sequence = q3_add_time(game->portal_sequence, 1);
        entry->state.player.portal_id = game->portal_sequence;
        p->state.portal.sequence = game->portal_sequence;
        entry->state.player.holdable = QA_Q3_H_PORTAL;
    } else
        entry->state.player.portal_id = 0;
    qa_string_id resource;
    if (!qa_builtin_resource(&game->options.services,
                             source ? "models/powerups/teleporter/tele_enter.md3"
                                    : "models/powerups/teleporter/tele_exit.md3",
                             &resource, error))
        return false;
    qa_builtin_event event = {.kind = QA_BUILTIN_ITEM,
                              .family = QA_GAME_Q3,
                              .provider = game->options.owner,
                              .actor = portal,
                              .origin = body.origin,
                              .resource = resource,
                              .time_ns = (uint64_t)(uint32_t)game->now_ms * UINT64_C(1000000)};
    if (!qa_builtin_emit(&game->options.services, &event, error))
        return false;
    return true;
}
bool q3_portal_step(qa_q3_game *game, qa_actor_id actor, qa_error *error) {
    bool replaced;
    if (!q3_postgame_think_override(game, actor, &replaced, error)) return false;
    if (replaced) return true;
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PORTAL)
        return true;
    if (entry->state.portal.source && !entry->state.portal.enabled) {
        if (game->now_ms < entry->state.portal.activate_at)
            return true;
        entry->state.portal.enabled = true;
        entry->state.portal.expire_at = q3_add_time(game->now_ms, 120000);
        return true;
    }
    return game->now_ms < entry->state.portal.expire_at ||
           qa_session_release(game->options.services.session, actor, error);
}
bool q3_use_holdable(qa_q3_game *game, qa_actor_id actor, qa_q3_holdable holdable,
                     qa_error *error) {
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER)
        return q3_fail(error, "holdable requires Q3 player state");
    if (game->options.product == QA_Q3_ARENA && holdable > QA_Q3_H_MEDKIT)
        return q3_fail(error, "holdable requires Team Arena");
    if (!q3_ranking_holdable(game, actor, holdable, error))
        return false;
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER)
        return true;
    switch (holdable) {
    case QA_Q3_H_NONE:
        return true;
    case QA_Q3_H_MEDKIT:
        return qa_combat_set_health(game->options.services.combat, actor,
                                    (float)entry->state.player.max_health + 25, error);
    case QA_Q3_H_TELEPORTER: {
        if (!game->options.hooks.teleport_destination)
            return q3_fail(error, "personal teleporter has no map spawn owner");
        if (game->options.hooks.objective_drop &&
            !game->options.hooks.objective_drop(game->options.hooks.context, actor, error))
            return false;
        if (!q3_actor_get(game, actor))
            return true;
        qa_vec3 origin, angles;
        if (!game->options.hooks.teleport_destination(game->options.hooks.context, actor, &origin,
                                                      &angles, error))
            return false;
        return !q3_actor_get(game, actor) || qa_q3_teleport(game, actor, origin, angles, error);
    }
    case QA_Q3_H_INVULNERABILITY:
        entry->state.player.invulnerability_until = q3_add_time(game->now_ms, 10000);
        return true;
    case QA_Q3_H_PORTAL:
        return portal_drop(game, actor, error);
    case QA_Q3_H_KAMIKAZE:
        return q3_start_kamikaze(game, actor, actor, true, error);
    }
    return q3_fail(error, "unknown Q3 holdable");
}
bool q3_start_kamikaze(qa_q3_game *game, qa_actor_id source, qa_actor_id attacker, bool kill,
                       qa_error *error) {
    qa_body_state body;
    if (!q3_source_body_read(game, source, &body, error))
        return false;
    uint32_t source_slot;
    if (qa_q3_source_actor_slot(game, source, &source_slot, NULL)) {
        qa_q3_entity source_entity;
        qa_q3_wire_visibility source_visibility;
        if (!qa_q3_wire_entity_read(game, source_slot, &source_entity, &source_visibility, error))
            return false;
        body.origin = qa_v3(source_entity.pos.base[0], source_entity.pos.base[1],
                             source_entity.pos.base[2]);
    }
    if (!q3_actor_get(game, source))
        return true;
    body.origin = qa_physics_q3_snap(body.origin);
    body.angles = qa_v3(0, 0, 0);
    body.velocity = qa_v3(0, 0, 0);
    body.bounds = (qa_bounds){0};
    body.ground = (qa_actor_id){0};
    qa_builtin_spawn spawn = {.owner = game->options.owner, .body = body};
    if (!qa_builtin_resource(&game->options.services, "kamikaze", &spawn.definition, error)) return false;
    qa_actor_id explosion;
    if (!q3_spawn_actor(game, &spawn, &explosion, error))
        return false;
    game->actors[explosion.slot] = (q3_actor){
        .actor = explosion,
        .kind = Q3_ACTOR_KAMIKAZE,
        .alpha = 1,
        .state.kamikaze = {
            .attacker = attacker, .start = game->now_ms, .next = q3_add_time(game->now_ms, 100)}};
    q3_postgame_native_think_assigned(game, explosion);
    q3_wire_entity_source *wire = q3_wire_entity(game, explosion);
    if (!wire)
        return q3_rollback_spawn(game, explosion, error);
    wire->type = 13 + 68;
    wire->position = (qa_trajectory){.type = QA_TRAJECTORY_STATIONARY, .base = body.origin};
    if (!q3_wire_event_time(game, explosion, game->now_ms, error) ||
        !qa_q3_wire_link(game, explosion, NULL, error) ||
        !q3_wire_entity_ready(game, explosion, error))
        return q3_rollback_spawn(game, explosion, error);
    q3_actor *entry = q3_actor_get(game, source);
    if (entry && entry->kind == Q3_ACTOR_PLAYER) {
        entry->state.player.flags &= ~0x200u;
        entry->state.player.invulnerability_until = 0;
    }
    if (!q3_event(game, explosion, attacker, QA_BUILTIN_EXPLOSION, 68, 0, body.origin,
                  qa_v3(0, 0, 0), qa_v3(0, 0, 0), error))
        return false;
    if (kill && !q3_damage(game, source, source, source, QA_Q3_W_NONE, 26, 8, 100000,
                           qa_v3(0, 0, 0), body.origin, false, NULL, error))
        return false;
    qa_actor_id temporary;
    if (!q3_wire_temp_entity(game, body.origin, 47, &temporary, error))
        return false;
    qa_q3_entity *event = q3_wire_temporary(game, temporary);
    uint32_t temporary_slot;
    if (!event || !qa_q3_source_actor_slot(game, temporary, &temporary_slot, error))
        return q3_fail(error, "Q3 kamikaze team sound lost its actual source event row");
    event->eventParm = 13;
    game->source_entities[temporary_slot].server_flags |= 32u;
    return q3_event(game, explosion, attacker, QA_BUILTIN_SOUND, 47, 13, body.origin,
                    qa_v3(0, 0, 0), qa_v3(0, 0, 0), error);
}
static float bounds_distance(qa_vec3 point, qa_bounds bounds) {
    qa_vec3 d = qa_v3(fmaxf(bounds.mins.x - point.x, fmaxf(0, point.x - bounds.maxs.x)),
                      fmaxf(bounds.mins.y - point.y, fmaxf(0, point.y - bounds.maxs.y)),
                      fmaxf(bounds.mins.z - point.z, fmaxf(0, point.z - bounds.maxs.z)));
    return qa_vec_length(d);
}
static bool kamikaze_area(qa_q3_game *game, qa_actor_id explosion, qa_actor_id attacker,
                          qa_vec3 origin, float radius, float damage, bool shock, qa_error *error) {
    radius = fmaxf(1, radius);
    qa_vec3 extent = qa_v3(radius, radius, radius);
    q3_snapshot_frame *frame = q3_bounds_snapshot(
        game, (qa_bounds){qa_vec_sub(origin, extent), qa_vec_add(origin, extent)},
        QA_COLLISION_BOTH, error);
    if (!frame)
        return false;
    bool ok = true;
    for (size_t i = 0; i < frame->snapshot.count; ++i) {
        qa_actor_id actor = frame->snapshot.ids[i];
        if (actor.slot >= game->capacity ||
            !qa_actors_get(qa_session_actors(game->options.services.session), actor))
            continue;
        q3_kamikaze_cooldown *cooldown = &game->kamikaze_cooldowns[actor.slot];
        if (!qa_actor_id_equal(cooldown->actor, actor))
            *cooldown = (q3_kamikaze_cooldown){.actor = actor};
        if ((shock ? cooldown->shock_after : cooldown->damage_after) > game->now_ms)
            continue;
        qa_combat_state combat;
        qa_error ignored = {0};
        if (!qa_combat_read(game->options.services.combat, actor, &combat, &ignored) ||
            (!shock && !combat.can_take_damage))
            continue;
        qa_linked_body linked;
        if (!qa_world_linked(game->options.services.world, actor, &linked))
            continue;
        if (bounds_distance(origin, linked.absolute_bounds) >= radius)
            continue;
        qa_vec3 direction = qa_vec_add(qa_vec_sub(linked.state.origin, origin), qa_v3(0, 0, 24));
        if (!q3_damage(game, actor, attacker, explosion, QA_Q3_W_NONE, 26, 1u | 16u, damage,
                       direction, origin, true, NULL, error)) {
            ok = false;
            break;
        }
        if (!qa_actors_get(qa_session_actors(game->options.services.session), actor))
            continue;
        cooldown = &game->kamikaze_cooldowns[actor.slot];
        if (shock && q3_is_player(game, actor)) {
            qa_body_state body;
            if (!q3_source_body_read(game, actor, &body, error)) {
                ok = false;
                break;
            }
            qa_vec3 horizontal = qa_vec_normalize(qa_v3(direction.x, direction.y, 0));
            body.velocity = qa_v3(horizontal.x * 400, horizontal.y * 400, 100);
            if (!qa_world_body_write(game->options.services.world, actor, &body, error)) {
                ok = false;
                break;
            }
            if (game->options.services.motion_changed) {
                qa_builtin_motion_change change = {.reason = QA_BUILTIN_MOTION_LAUNCH,
                                                   .body = body};
                if (!game->options.services.motion_changed(game->options.services.context, actor,
                                                           &change, error)) {
                    ok = false;
                    break;
                }
            }
        }
        if (!qa_actors_get(qa_session_actors(game->options.services.session), actor))
            continue;
        cooldown = &game->kamikaze_cooldowns[actor.slot];
        if (shock)
            cooldown->shock_after = q3_add_time(game->now_ms, 3000);
        else
            cooldown->damage_after = q3_add_time(game->now_ms, 3000);
    }
    frame->active = false;
    return ok;
}
bool q3_kamikaze_step(qa_q3_game *game, qa_actor_id actor, qa_error *error) {
    bool replaced;
    if (!q3_postgame_think_override(game, actor, &replaced, error)) return false;
    if (replaced) return true;
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_KAMIKAZE || game->now_ms < entry->state.kamikaze.next)
        return true;
    qa_body_state body;
    if (!q3_source_body_read(game, actor, &body, error))
        return false;
    entry->state.kamikaze.elapsed += 100;
    int32_t elapsed = entry->state.kamikaze.elapsed;
    qa_actor_id attacker = entry->state.kamikaze.attacker;
    if (!kamikaze_area(game, actor, attacker, body.origin, (float)((elapsed * 1320) / 2000), 25,
                       true, error))
        return false;
    if (!q3_actor_get(game, actor))
        return true;
    if (elapsed >= 250 &&
        !kamikaze_area(game, actor, attacker, body.origin, (float)(((elapsed - 250) * 720) / 1750),
                       400, false, error))
        return false;
    if (!q3_actor_get(game, actor))
        return true;
    if (elapsed >= 2000)
        return qa_session_release(game->options.services.session, actor, error);
    entry = q3_actor_get(game, actor);
    if (!entry)
        return true;
    entry->state.kamikaze.next = q3_add_time(game->now_ms, 100);
    float pitch_angle = q3_crandom(game) * 2;
    float yaw_angle = q3_crandom(game) * 2;
    qa_vec3 angles = qa_v3(pitch_angle, yaw_angle, 0), previous = entry->state.kamikaze.angles;
    entry->state.kamikaze.angles = angles;
    for (uint32_t slot = 0; slot < QA_Q3_SOURCE_CLIENTS; ++slot) {
        qa_q3_source_binding binding = game->source_entities[slot];
        qa_actor_id target = binding.actor;
        if (!binding.in_use || !q3_actor_get(game, target)) continue;
        q3_actor *candidate = q3_actor_get(game, target);
        qa_builtin_actor_traits traits = {0};
        bool described =
            game->options.services.actor_traits &&
            game->options.services.actor_traits(game->options.services.context, target, &traits);
        if (!q3_actor_get(game, actor)) return true;
        if (!game->source_entities[slot].in_use ||
            !qa_actor_id_equal(game->source_entities[slot].actor, target)) continue;
        if (!described && candidate && candidate->kind == Q3_ACTOR_PLAYER)
            traits.grounded = candidate->state.player.ground_entity_number >= 0 &&
                              candidate->state.player.ground_entity_number != 1023;
        qa_body_state player_body;
        if (!q3_source_body_read(game, target, &player_body, error))
            return false;
        if (traits.grounded) {
            player_body.velocity.x += q3_crandom(game) * 120;
            player_body.velocity.y += q3_crandom(game) * 120;
            player_body.velocity.z = 30 + q3_random(game) * 25;
            if (!qa_world_body_write(game->options.services.world, target, &player_body, error))
                return false;
        }
        int32_t yaw = (int32_t)((angles.y - previous.y) * 65536 / 360) & 65535;
        int32_t pitch = (int32_t)((angles.x - previous.x) * 65536 / 360) & 65535;
        candidate = q3_actor_get(game, target);
        if (candidate && candidate->kind == Q3_ACTOR_PLAYER) {
            candidate->state.player.delta_yaw_word =
                q3_add_time(candidate->state.player.delta_yaw_word, yaw);
            candidate->state.player.delta_pitch_word =
                q3_add_time(candidate->state.player.delta_pitch_word, pitch);
        }
        if (game->options.services.motion_changed) {
            qa_builtin_motion_change change = {.reason = QA_BUILTIN_MOTION_LAUNCH,
                                               .body = player_body,
                                               .angular_kick = qa_vec_sub(angles, previous),
                                               .apply_angular_kick = true};
            if (!game->options.services.motion_changed(game->options.services.context, target,
                                                       &change, error))
                return false;
        }
    }
    return true;
}
