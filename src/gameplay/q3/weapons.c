#include "internal.h"

typedef struct q3_attack_geometry {
    qa_vec3 muzzle, forward, right, up;
    float factor;
    qa_q3_weapon firing_weapon;
} q3_attack_geometry;
static bool attack_geometry(qa_q3_game *game, qa_actor_id actor, q3_attack_geometry *out,
                            qa_error *error) {
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER)
        return q3_fail(error, "Q3 weapon requires an admitted player");
    qa_body_state body;
    if (!q3_source_body_read(game, actor, &body, error))
        return false;
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER)
        return q3_fail(error, "Q3 weapon player changed during its body read");
    uint32_t source_slot;
    qa_q3_weapon firing_weapon = entry->state.player.weapon;
    if (qa_q3_native_client_slot(game, actor, &source_slot, NULL)) {
        qa_q3_wire_player_publication source;
        if (!qa_q3_wire_player_publication_read(game, actor, &source, error))
            return false;
        body.origin = source.position;
        firing_weapon = (qa_q3_weapon)source.weapon;
    }
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER)
        return q3_fail(error, "Q3 weapon player changed during its body read");
    qa_q3_player_state *player = &entry->state.player;
    q3_source_angle_vectors(player->view_angles, &out->forward, &out->right, &out->up);
    out->muzzle = qa_vec_add(body.origin, qa_v3(0, 0, player->view_height));
    out->muzzle = qa_physics_q3_snap(qa_vec_add(out->muzzle, qa_vec_scale(out->forward, 14)));
    bool handled = false;
    float factor = 0;
    if (game->options.hooks.selected_damage_factor &&
        !game->options.hooks.selected_damage_factor(game->options.hooks.context,
            actor, &factor, &handled, error)) return false;
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER)
        return q3_fail(error, "Q3 weapon player changed during its Source damage factor");
    player = &entry->state.player;
    if (handled) {
        if (!isfinite(factor) || factor < 0) return q3_fail(error, "Source weapon damage factor is invalid");
        if (game->options.product == QA_Q3_TEAM_ARENA &&
            (player->selections & QA_Q3_EQUIPMENT) && player->persistent == QA_Q3_P_DOUBLER)
            factor *= 2;
        out->factor = factor;
    } else out->factor = q3_damage_factor(game, player);
    out->firing_weapon = firing_weapon;
    return true;
}
static bool target_state(qa_q3_game *game, qa_actor_id actor, qa_combat_state *state) {
    qa_error ignored = {0};
    return qa_actors_get(qa_session_actors(game->options.services.session), actor) &&
           qa_combat_read(game->options.services.combat, actor, state, &ignored) &&
           state->can_take_damage;
}
static bool player_target(qa_q3_game *game, qa_actor_id actor) { return q3_is_player(game, actor); }
bool q3_invulnerability(qa_q3_game *game, qa_actor_id actor, qa_vec3 direction, qa_vec3 point,
                        qa_vec3 *impact, qa_vec3 *normal, bool *hit, qa_error *error) {
    *hit = false;
    const q3_actor *entry = q3_actor_const(game, actor);
    if (game->options.product != QA_Q3_TEAM_ARENA || !entry || entry->kind != Q3_ACTOR_PLAYER ||
        entry->state.player.invulnerability_until <= game->now_ms)
        return true;
    qa_body_state body;
    if (!q3_source_body_read(game, actor, &body, error))
        return false;
    qa_vec3 backwards = qa_vec_scale(qa_vec_normalize(direction), -1);
    qa_vec3 offset = qa_vec_sub(point, body.origin);
    float b = 2 * qa_vec_dot(backwards, offset), c = qa_vec_dot(offset, offset) - 42 * 42;
    float discriminant = b * b - 4 * c;
    if (discriminant < 0)
        return true;
    *impact = qa_vec_add(point, qa_vec_scale(backwards, (-b + sqrtf(discriminant)) * 0.5f));
    *normal = qa_vec_normalize(qa_vec_sub(*impact, body.origin));
    *hit = true;
    qa_actor_id temporary;
    if (!q3_wire_temp_entity(game, body.origin, 71, &temporary, error))
        return false;
    qa_q3_entity *event = q3_wire_temporary(game, temporary);
    if (!event) return q3_fail(error, "Q3 invulnerability impact lost its temporary entity");
    qa_vec3 offset_angles = qa_vec_sub(*impact, body.origin);
    float yaw = 0, pitch = offset_angles.z > 0 ? 90 : 270;
    if (offset_angles.x || offset_angles.y) {
        yaw = offset_angles.x ? q3_source_float_divide(q3_source_float_multiply(
            (float)atan2((double)offset_angles.y, (double)offset_angles.x), 180), Q3_PI)
            : offset_angles.y > 0 ? 90 : 270;
        if (yaw < 0) yaw = q3_source_float_add(yaw, 360);
        float horizontal = (float)sqrt((double)q3_source_float_add(
            q3_source_float_multiply(offset_angles.x, offset_angles.x),
            q3_source_float_multiply(offset_angles.y, offset_angles.y)));
        pitch = q3_source_float_divide(q3_source_float_multiply(
            (float)atan2((double)offset_angles.z, (double)horizontal), 180), Q3_PI);
        if (pitch < 0) pitch = q3_source_float_add(pitch, 360);
    }
    pitch = q3_source_float_add(-pitch, 90);
    if (pitch > 360) pitch = q3_source_float_add(pitch, -360);
    event->angles[0] = pitch; event->angles[1] = yaw; event->angles[2] = 0;
    return q3_event(game, actor, (qa_actor_id){0}, QA_BUILTIN_IMPACT, 71, 0, body.origin, *impact,
                    *normal, error);
}
static bool shielded(qa_q3_game *game, qa_actor_id actor) {
    const q3_actor *entry = q3_actor_const(game, actor);
    return game->options.product == QA_Q3_TEAM_ARENA && entry && entry->kind == Q3_ACTOR_PLAYER &&
           entry->state.player.invulnerability_until > game->now_ms;
}
static qa_vec3 reflected_end(qa_vec3 start, qa_vec3 point, qa_vec3 normal) {
    qa_vec3 incoming = qa_vec_sub(point, start);
    qa_vec3 reflected =
        qa_vec_sub(incoming, qa_vec_scale(normal, 2 * qa_vec_dot(incoming, normal)));
    return qa_vec_add(point, qa_vec_scale(qa_vec_normalize(reflected), 8192));
}
static bool beam_event(qa_q3_game *game, qa_actor_id shooter, int32_t code, int32_t parm,
                       qa_vec3 origin, qa_vec3 destination, qa_vec3 normal, qa_error *error) {
    int32_t client_number = 0;
    uint32_t slot;
    if (code == 53 && qa_q3_native_client_slot(game, shooter, &slot, NULL)) {
        qa_q3_wire_player_publication source;
        if (!qa_q3_wire_player_publication_read(game, shooter, &source, error)) return false;
        client_number = source.client_number;
    } else {
        const q3_actor *player = q3_actor_const(game, shooter);
        if (player && player->kind == Q3_ACTOR_PLAYER)
            client_number = player->state.player.client_number;
    }
    qa_actor_id temporary;
    if (!q3_wire_temp_entity(game, origin, code, &temporary, error)) return false;
    qa_q3_entity *event = q3_wire_temporary(game, temporary);
    if (!event) return q3_fail(error, "Q3 beam lost its temporary entity");
    event->origin2[0] = destination.x; event->origin2[1] = destination.y;
    event->origin2[2] = destination.z;
    if (code == 53) { event->clientNum = client_number; event->eventParm = parm; }
    return q3_event(game, shooter, (qa_actor_id){0}, QA_BUILTIN_BEAM, code, parm,
                    origin, destination, normal, error);
}
static bool impact_event(qa_q3_game *game, qa_actor_id shooter, qa_q3_weapon weapon,
                         const qa_trace_result *trace, qa_vec3 point, bool bullet,
                         qa_error *error) {
    qa_combat_state state;
    bool flesh = trace->hit == QA_TRACE_HIT_ACTOR && target_state(game, trace->actor, &state) &&
                 player_target(game, trace->actor);
    uint8_t normal = 0;
    (void)qa_normal_byte(trace->contact_plane.normal, &normal);
    int32_t code = bullet ? (flesh ? 48 : 49) : (flesh ? 50 : 51);
    int32_t shooter_number = q3_entity_number(game, shooter);
    int32_t target_number = flesh ? q3_entity_number(game, trace->actor) : 0;
    qa_actor_id temporary;
    if (!q3_wire_temp_entity(game, point, code, &temporary, error)) return false;
    qa_q3_entity *event = q3_wire_temporary(game, temporary);
    if (!event) return q3_fail(error, "Q3 weapon impact lost its temporary entity");
    event->eventParm = bullet && flesh ? target_number : normal;
    if (bullet) event->otherEntityNum = shooter_number;
    else if (flesh) { event->otherEntityNum = target_number; event->weapon = weapon; }
    return q3_event(game, shooter, trace->actor, QA_BUILTIN_IMPACT, code,
                    flesh ? q3_entity_number(game, trace->actor) : (int32_t)normal, point,
                    qa_v3((float)weapon, 0, 0), trace->contact_plane.normal, error);
}
static bool bullet(qa_q3_game *game, qa_actor_id shooter, qa_q3_weapon weapon,
                   q3_attack_geometry attack, float spread, float damage, qa_error *error) {
    float angle = q3_source_float_multiply(
        q3_source_float_multiply(q3_random(game), Q3_PI), 2.0f);
    float vertical = q3_source_float_multiply(
        q3_source_float_multiply(
            q3_source_float_multiply((float)sin((double)angle), q3_crandom(game)), spread),
        16.0f);
    float horizontal = q3_source_float_multiply(
        q3_source_float_multiply(
            q3_source_float_multiply((float)cos((double)angle), q3_crandom(game)), spread),
        16.0f);
    qa_vec3 end =
        qa_vec_add(qa_vec_add(qa_vec_add(attack.muzzle, qa_vec_scale(attack.forward, 131072)),
                              qa_vec_scale(attack.right, horizontal)),
                   qa_vec_scale(attack.up, vertical));
    qa_actor_id pass = shooter;
    for (unsigned i = 0; i < 10; ++i) {
        qa_trace_result trace;
        if (!q3_trace(game, attack.muzzle, end, pass, Q3_MASK_SHOT, &trace, error))
            return false;
        if (trace.surface_flags & Q3_SURF_NOIMPACT)
            return true;
        qa_vec3 point = qa_physics_q3_snap_towards(trace.end, attack.muzzle);
        qa_combat_state target;
        bool damageable =
            trace.hit == QA_TRACE_HIT_ACTOR && target_state(game, trace.actor, &target);
        if (!impact_event(game, shooter, weapon, &trace, point, true, error))
            return false;
        if (!qa_actors_get(qa_session_actors(game->options.services.session), shooter))
            return true;
        if (damageable && q3_accuracy(game, trace.actor, shooter))
            q3_credit_accuracy(game, shooter);
        if (!damageable)
            break;
        if (shielded(game, trace.actor)) {
            qa_vec3 impact, normal;
            bool hit;
            if (!q3_invulnerability(game, trace.actor, attack.forward, point, &impact, &normal,
                                    &hit, error))
                return false;
            if (hit) {
                end = reflected_end(attack.muzzle, impact, normal);
                attack.muzzle = impact;
                pass = (qa_actor_id){0};
            } else {
                attack.muzzle = point;
                pass = trace.actor;
            }
            continue;
        }
        return q3_damage(game, trace.actor, shooter, shooter, weapon, 3, 0,
                         truncf(damage * attack.factor), attack.forward, point, false, NULL, error);
    }
    return true;
}
bool q3_gauntlet(qa_q3_game *game, qa_actor_id shooter, bool *hit, qa_error *error) {
    *hit = false;
    q3_attack_geometry attack;
    if (!attack_geometry(game, shooter, &attack, error))
        return false;
    qa_trace_result trace;
    if (!q3_trace(game, attack.muzzle, qa_vec_add(attack.muzzle, qa_vec_scale(attack.forward, 32)),
                  shooter, Q3_MASK_SHOT, &trace, error))
        return false;
    qa_combat_state state;
    if ((trace.surface_flags & Q3_SURF_NOIMPACT) || trace.hit != QA_TRACE_HIT_ACTOR ||
        !target_state(game, trace.actor, &state))
        return true;
    q3_actor *entry = q3_actor_get(game, shooter);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER)
        return true;
    *hit = true;
    bool flesh = player_target(game, trace.actor);
    entry = q3_actor_get(game, shooter);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER ||
        !qa_actors_get(qa_session_actors(game->options.services.session), trace.actor))
        return true;
    if (flesh &&
        !impact_event(game, shooter, attack.firing_weapon, &trace, trace.end, false, error))
        return false;
    entry = q3_actor_get(game, shooter);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER)
        return true;
    if (entry->state.player.powerups[QA_Q3_P_QUAD] && !q3_add_event(game, shooter, 61, 0, error))
        return false;
    entry = q3_actor_get(game, shooter);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER ||
        !qa_actors_get(qa_session_actors(game->options.services.session), trace.actor))
        return true;
    return q3_damage(game, trace.actor, shooter, shooter, QA_Q3_W_GAUNTLET, 2, 0,
                     truncf(50 * attack.factor), attack.forward, trace.end, false, NULL, error);
}
static bool lightning(qa_q3_game *game, qa_actor_id shooter, q3_attack_geometry attack,
                      qa_error *error) {
    qa_actor_id pass = shooter;
    for (unsigned i = 0; i < 10; ++i) {
        qa_trace_result trace;
        if (!q3_trace(game, attack.muzzle,
                      qa_vec_add(attack.muzzle, qa_vec_scale(attack.forward, 768)), pass,
                      Q3_MASK_SHOT, &trace, error))
            return false;
        if (i && !beam_event(game, shooter, 73, 0, attack.muzzle,
                           qa_physics_q3_snap(trace.end), qa_v3(0, 0, 0), error))
            return false;
        if (trace.hit == QA_TRACE_HIT_NONE)
            return true;
        qa_combat_state state;
        if (trace.hit == QA_TRACE_HIT_ACTOR && target_state(game, trace.actor, &state)) {
            if (shielded(game, trace.actor)) {
                qa_vec3 impact, normal;
                bool hit;
                if (!q3_invulnerability(game, trace.actor, attack.forward, trace.end, &impact,
                                        &normal, &hit, error))
                    return false;
                if (hit) {
                    qa_vec3 end = reflected_end(attack.muzzle, impact, normal);
                    attack.muzzle = impact;
                    attack.forward = qa_vec_normalize(qa_vec_sub(end, impact));
                    pass = (qa_actor_id){0};
                } else {
                    attack.muzzle = trace.end;
                    pass = trace.actor;
                }
                continue;
            }
            if (!q3_damage(game, trace.actor, shooter, shooter, QA_Q3_W_LIGHTNING, 11, 0,
                           truncf(8 * attack.factor), attack.forward, trace.end, false, NULL,
                           error))
                return false;
        }
        if (!(trace.surface_flags & Q3_SURF_NOIMPACT) &&
            !impact_event(game, shooter, attack.firing_weapon, &trace, trace.end, false, error))
            return false;
        if (q3_accuracy(game, trace.actor, shooter))
            q3_credit_accuracy(game, shooter);
        break;
    }
    return true;
}
static qa_vec3 source_normalize(qa_vec3 value) {
    float length = (float)sqrt((double)qa_vec_dot(value, value));
    return length == 0 ? value : qa_vec_scale(value, q3_source_float_divide(1, length));
}
static qa_vec3 perpendicular(qa_vec3 direction) {
    qa_vec3 axis = qa_v3(1, 0, 0);
    float minimum = 1;
    if (fabsf(direction.x) < minimum)
        minimum = fabsf(direction.x);
    if (fabsf(direction.y) < minimum) {
        axis = qa_v3(0, 1, 0);
        minimum = fabsf(direction.y);
    }
    if (fabsf(direction.z) < minimum)
        axis = qa_v3(0, 0, 1);
    float inverse = q3_source_float_divide(1, qa_vec_dot(direction, direction));
    float distance = q3_source_float_multiply(qa_vec_dot(axis, direction), inverse);
    qa_vec3 normal = qa_vec_scale(direction, inverse);
    return source_normalize(qa_vec_sub(axis, qa_vec_scale(normal, distance)));
}
static float seeded_crandom(uint32_t *seed) {
    *seed = *seed * UINT32_C(69069) + 1;
    return 2.0f * ((float)(*seed & 65535u) / 65536.0f - 0.5f);
}
static bool shotgun(qa_q3_game *game, qa_actor_id shooter, q3_attack_geometry attack,
                    qa_error *error) {
    qa_vec3 direction = qa_physics_q3_snap(qa_vec_scale(attack.forward, 4096));
    int32_t shooter_number = q3_entity_number(game, shooter);
    qa_actor_id temporary;
    if (!q3_wire_temp_entity(game, attack.muzzle, 54, &temporary, error)) return false;
    qa_q3_entity *event = q3_wire_temporary(game, temporary);
    if (!event) return q3_fail(error, "Q3 shotgun lost its temporary entity");
    event->origin2[0] = direction.x; event->origin2[1] = direction.y; event->origin2[2] = direction.z;
    uint32_t seed = q3_rand(game) & 255u;
    event->eventParm = (int32_t)seed; event->otherEntityNum = shooter_number;
    if (!q3_event(game, shooter, (qa_actor_id){0}, QA_BUILTIN_SHOT, 54, (int32_t)seed,
                  attack.muzzle, direction, qa_v3(0, 0, 0), error))
        return false;
    qa_vec3 forward = source_normalize(direction), right = perpendicular(forward),
            up = qa_vec_cross(forward, right);
    bool credited = false;
    for (unsigned pellet = 0; pellet < 11; ++pellet) {
        if (!q3_actor_get(game, shooter))
            break;
        float horizontal = seeded_crandom(&seed) * 700 * 16;
        float vertical = seeded_crandom(&seed) * 700 * 16;
        qa_vec3 start = attack.muzzle;
        qa_vec3 end = qa_vec_add(qa_vec_add(qa_vec_add(start, qa_vec_scale(forward, 131072)),
                                            qa_vec_scale(right, horizontal)),
                                 qa_vec_scale(up, vertical));
        qa_actor_id pass = shooter;
        for (unsigned reflection = 0; reflection < 10; ++reflection) {
            qa_trace_result trace;
            if (!q3_trace(game, start, end, pass, Q3_MASK_SHOT, &trace, error))
                return false;
            qa_combat_state state;
            if ((trace.surface_flags & Q3_SURF_NOIMPACT) || trace.hit != QA_TRACE_HIT_ACTOR ||
                !target_state(game, trace.actor, &state))
                break;
            if (shielded(game, trace.actor)) {
                qa_vec3 impact, normal;
                bool hit;
                if (!q3_invulnerability(game, trace.actor, attack.forward, trace.end, &impact,
                                        &normal, &hit, error))
                    return false;
                if (hit) {
                    end = reflected_end(start, impact, normal);
                    start = impact;
                    pass = (qa_actor_id){0};
                } else {
                    start = trace.end;
                    pass = trace.actor;
                }
                continue;
            }
            if (!q3_damage(game, trace.actor, shooter, shooter, QA_Q3_W_SHOTGUN, 1, 0,
                           truncf(10 * attack.factor), attack.forward, trace.end, false, NULL,
                           error))
                return false;
            if (!credited && q3_accuracy(game, trace.actor, shooter)) {
                q3_credit_accuracy(game, shooter);
                credited = true;
            }
            break;
        }
    }
    return true;
}
static bool rail(qa_q3_game *game, qa_actor_id shooter, q3_attack_geometry attack,
                 qa_error *error) {
    struct {
        qa_actor_id actor;
        bool source;
    } removed[4];
    size_t removed_count = 0;
    qa_vec3 end = qa_vec_add(attack.muzzle, qa_vec_scale(attack.forward, 8192));
    qa_actor_id pass = shooter;
    qa_trace_result trace = {0};
    int hits = 0;
    bool ok = true, traced = false;
    for (unsigned i = 0; i < 4 && q3_actor_get(game, shooter); ++i) {
        if (!q3_trace(game, attack.muzzle, end, pass, Q3_MASK_SHOT, &trace, error)) {
            ok = false;
            break;
        }
        traced = true;
        if (trace.hit != QA_TRACE_HIT_ACTOR)
            break;
        qa_combat_state state;
        if (target_state(game, trace.actor, &state)) {
            if (shielded(game, trace.actor)) {
                qa_vec3 impact, normal;
                bool hit;
                if (!q3_invulnerability(game, trace.actor, attack.forward, trace.end, &impact,
                                        &normal, &hit, error)) {
                    ok = false;
                    break;
                }
                if (hit) {
                    end = reflected_end(attack.muzzle, impact, normal);
                    qa_vec3 start =
                        qa_vec_add(qa_vec_add(attack.muzzle, qa_vec_scale(attack.right, 4)),
                                   qa_vec_scale(attack.up, -1));
                    if (!beam_event(game, shooter, 53, 255,
                                  qa_physics_q3_snap_towards(trace.end, attack.muzzle), start,
                                  qa_v3(0, 0, 0), error)) {
                        ok = false;
                        break;
                    }
                    attack.muzzle = impact;
                    pass = (qa_actor_id){0};
                }
            } else {
                if (q3_accuracy(game, trace.actor, shooter))
                    ++hits;
                if (!q3_damage(game, trace.actor, shooter, shooter, QA_Q3_W_RAIL, 10, 0,
                               truncf(100 * attack.factor), attack.forward, trace.end, false, NULL,
                               error)) {
                    ok = false;
                    break;
                }
            }
        }
        if (trace.contents & 1)
            break;
        if (qa_actors_get(qa_session_actors(game->options.services.session), trace.actor)) {
            uint32_t source_slot;
            bool source = qa_q3_source_actor_slot(game, trace.actor, &source_slot, NULL);
            bool unlinked = source
                ? qa_world_unlink(game->options.services.world, trace.actor, error)
                : qa_world_suspend_collision(game->options.services.world, trace.actor, error);
            if (!unlinked) {
                ok = false;
                break;
            }
            removed[removed_count].actor = trace.actor;
            removed[removed_count++].source = source;
        }
    }
    for (size_t i = 0; i < removed_count; ++i) {
        qa_actor_id actor = removed[i].actor;
        if (!qa_actors_get(qa_session_actors(game->options.services.session), actor))
            continue;
        qa_error restore_error = {0};
        bool linked = removed[i].source
            ? qa_q3_wire_link(game, actor, NULL, &restore_error)
            : qa_world_link(game->options.services.world, actor, NULL, &restore_error);
        if (!linked && ok) {
            ok = false;
            if (error)
                *error = restore_error;
        }
    }
    if (!ok)
        return false;
    if (traced) {
        uint8_t normal = 0;
        (void)qa_normal_byte(trace.contact_plane.normal, &normal);
        qa_vec3 start = qa_vec_add(qa_vec_add(attack.muzzle, qa_vec_scale(attack.right, 4)),
                                   qa_vec_scale(attack.up, -1));
        if (!beam_event(game, shooter, 53,
                      trace.surface_flags & Q3_SURF_NOIMPACT ? 255 : normal,
                      qa_physics_q3_snap_towards(trace.end, attack.muzzle), start,
                      trace.contact_plane.normal, error))
            return false;
    }
    q3_actor *entry = q3_actor_get(game, shooter);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER)
        return true;
    qa_q3_player_state *player = &entry->state.player;
    if (!hits)
        player->rail_streak = 0;
    else {
        player->rail_streak = q3_add_time(player->rail_streak, hits);
        player->accuracy_hits = q3_add_time(player->accuracy_hits, 1);
        if (player->rail_streak >= 2) {
            player->rail_streak -= 2;
            player->impressive_count = q3_add_time(player->impressive_count, 1);
            player->flags = (player->flags & ~UINT32_C(0x38848)) | 0x8000u;
            player->reward_until = q3_add_time(game->now_ms, 2000);
            if (!q3_ranking_reward(game, shooter, 0x8000u, error))
                return false;
        }
    }
    return true;
}
static bool fire_weapon(qa_q3_game *game, qa_actor_id shooter, qa_q3_weapon weapon,
                        qa_error *error) {
    if (weapon < QA_Q3_W_NONE || weapon >= QA_Q3_WEAPON_COUNT ||
        (game->options.product == QA_Q3_ARENA && weapon > QA_Q3_W_GRAPPLE))
        return q3_fail(error, "weapon outside selected Q3 product");
    q3_attack_geometry attack;
    if (!attack_geometry(game, shooter, &attack, error))
        return false;
    if (!q3_ranking_fire(game, shooter, weapon, error))
        return false;
    q3_actor *entry = q3_actor_get(game, shooter);
    if (!entry || entry->kind != Q3_ACTOR_PLAYER)
        return true;
    if (weapon != QA_Q3_W_GAUNTLET && weapon != QA_Q3_W_GRAPPLE)
        entry->state.player.accuracy_shots =
            q3_add_time(entry->state.player.accuracy_shots, weapon == QA_Q3_W_NAIL ? 15 : 1);
    switch (weapon) {
    case QA_Q3_W_NONE:
    case QA_Q3_W_GAUNTLET:
        return true;
    case QA_Q3_W_MACHINEGUN:
        return bullet(game, shooter, weapon, attack, 200,
                      game->options.rules.game_type == 3 ? 5 : 7, error);
    case QA_Q3_W_CHAINGUN:
        return bullet(game, shooter, weapon, attack, 600, 7, error);
    case QA_Q3_W_SHOTGUN:
        return shotgun(game, shooter, attack, error);
    case QA_Q3_W_LIGHTNING:
        return lightning(game, shooter, attack, error);
    case QA_Q3_W_RAIL:
        return rail(game, shooter, attack, error);
    case QA_Q3_W_GRAPPLE:
        if (entry->state.player.fire_held || entry->state.player.hook.registry)
            return true;
        entry->state.player.fire_held = true;
        break;
    case QA_Q3_W_GRENADE:
    case QA_Q3_W_PROX:
        attack.forward.z += 0.2f;
        attack.forward = qa_vec_normalize(attack.forward);
        break;
    default:
        break;
    }
    unsigned count = weapon == QA_Q3_W_NAIL ? 15u : 1u;
    for (unsigned i = 0; i < count; ++i) {
        if (!q3_actor_get(game, shooter))
            break;
        qa_actor_id missile;
        if (!q3_launch(game, shooter, weapon, attack.muzzle, attack.forward, attack.right,
                       attack.up, attack.factor, &missile, error))
            return false;
    }
    return true;
}

bool qa_q3_fire_weapon(qa_q3_game *game, qa_actor_id shooter, qa_q3_weapon weapon,
                       qa_error *error) {
    if (!game || game->source_restored || game->observation_depth == SIZE_MAX)
        return q3_fail(error, "invalid Q3 weapon action boundary");
    ++game->observation_depth;
    bool okay = fire_weapon(game, shooter, weapon, error);
    const q3_actor *player = q3_actor_const(game, shooter);
    if (okay && weapon != QA_Q3_W_NONE && player && player->kind == Q3_ACTOR_PLAYER &&
        (player->state.player.selections & QA_Q3_ARSENAL) && game->options.hooks.selected_weapon_fired)
        okay = game->options.hooks.selected_weapon_fired(game->options.hooks.context, shooter, weapon, error);
    --game->observation_depth;
    return okay;
}
